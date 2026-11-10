// Tests the production server state machine, not a substitute model of it.
#include "../Atmosphere/stratosphere/hoc_patch/source/hoc_service_core.hpp"
#include <cstdlib>
#include <iostream>

using namespace nexus_hoc;
static unsigned checks;
static void Check(bool value, const char *name) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
static constexpr std::uint64_t Owner = 23, Foreign = 24, Generation = 37;
static constexpr std::uintptr_t Input = 0x10000000, Output = 0x20000000;
static constexpr std::uint8_t Hash[32] = {0xC2};
static HelloRequest HelloRequestForTest() { return {Magic, Abi, sizeof(HelloRequest), Firmware2300, 1, 0}; }
static Descriptor DescriptorForTest() {
    Descriptor d{};
    d.magic = Magic; d.abi = Abi; d.size = sizeof(d); d.engine_version = 260;
    d.cust_revision = 1; d.cust_size = 26824; d.firmware = Firmware2300;
    d.flags = SupportsPcv | SupportsPtm; d.data_bytes = DataBytes; d.code_bytes = CodeBytes;
    d.runtime_bytes = RuntimeBytes; d.payload_offset = PayloadOffset;
    d.max_image_bytes = MaxImageBytes; d.generation = Generation; d.settings_hash[31] = 3;
    return d;
}
static PatchRequest Request(std::uint32_t kind = Pcv, std::uint64_t sequence = 1) {
    PatchRequest r{};
    r.magic = Magic; r.abi = Abi; r.size = sizeof(r); r.kind = kind;
    r.generation = Generation; r.sequence = sequence;
    r.program_id = kind == Pcv ? PcvProgramId : PtmProgramId;
    r.target_address = 0x7100000000ULL; r.module_size = 0x10000;
    r.image_size = r.module_size + (kind == Pcv ? ArenaBytes : 0);
    r.module_id[31] = 4; r.input_hash[0] = 5;
    return r;
}
static bool Can(const ServiceState &state, const PatchRequest &r, std::uint64_t pid = Owner) {
    return state.CanPrepare(r, pid, Input, r.image_size, Output, r.image_size);
}
static ServiceState Fresh(bool hello = true) {
    ServiceState state; state.Initialize(DescriptorForTest());
    if (hello) Check(state.Hello(HelloRequestForTest(), Owner), "owner hello");
    return state;
}
static CommitRequest CommitFor(const ServiceState &state) {
    const auto &r = state.Reply();
    CommitRequest c{Magic, Abi, sizeof(CommitRequest), r.kind, r.generation, r.sequence, {}};
    std::memcpy(c.output_hash, r.output_hash, sizeof(c.output_hash));
    return c;
}
static void Prepare(ServiceState &state, const PatchRequest &r) {
    Check(Can(state, r), "preparation allowed");
    state.BeginPrepare(r);
    Check((r.kind == Pcv ? state.Snapshot().pcv_phase : state.Snapshot().ptm_phase) == Failed,
          "incomplete patcher is conservatively failed");
    state.CompletePrepare(Hash);
    Check(ValidReply(state.Reply(), r, Prepared), "completed preparation reply");
}
static void Commit(ServiceState &state) {
    Check(state.CanCommit(CommitFor(state), Owner), "commit allowed");
    state.CompleteCommit();
}
static void AuthenticationAndOrdering() {
    Check(ServiceState::AuthorizedIdentity(Owner, Owner, LoaderProgramId), "actual Loader identity");
    for (auto client : {std::uint64_t{0}, Owner, Foreign})
        for (auto handle : {std::uint64_t{0}, Owner, Foreign})
            for (auto program : {std::uint64_t{0}, LoaderProgramId, HelperProgramId, PcvProgramId})
                Check(ServiceState::AuthorizedIdentity(client, handle, program)
                      == (client != 0 && client == handle && program == LoaderProgramId), "kernel identity combination");
    auto state = Fresh(false);
    Check(!Can(state, Request()), "no patch before hello");
    Check(!state.Hello(HelloRequestForTest(), 0), "zero hello caller");
    auto hello = HelloRequestForTest(); hello.reserved = 1;
    Check(!state.Hello(hello, Foreign), "malformed hello cannot seize ownership");
    Check(state.Hello(HelloRequestForTest(), Owner), "owner admission");
    Check(!state.Hello(HelloRequestForTest(), Foreign), "ownership cannot transfer on reconnect");
    Check(!Can(state, Request(), Foreign), "foreign patch denied");
    Check(!Can(state, Request(), 0), "zero caller patch denied");
    Check(!Can(state, Request(Ptm)), "PTM before PCV denied");
    Prepare(state, Request());
    Check(state.Hello(HelloRequestForTest(), Owner), "owner can reconnect without clearing state");
    Check(!Can(state, Request(Pcv, 2)), "pending PCV cannot prepare again");
    Check(!Can(state, Request(Ptm, 2)), "PTM cannot bypass pending PCV");
    auto commit = CommitFor(state);
    Check(!state.CanCommit(commit, 0), "zero caller commit denied");
    Check(!state.CanCommit(commit, Foreign), "foreign commit denied");
    auto altered = commit; ++altered.sequence;
    Check(!state.CanCommit(altered, Owner), "wrong sequence denied");
    altered = commit; ++altered.generation;
    Check(!state.CanCommit(altered, Owner), "wrong generation denied");
    altered = commit; altered.output_hash[31] ^= 1;
    Check(!state.CanCommit(altered, Owner), "wrong digest denied");
    Commit(state);
    Check(!state.CanCommit(commit, Owner), "duplicate commit denied");
    Check(!Can(state, Request(Pcv, 2)), "PCV cannot be patched twice");
    Check(!Can(state, Request(Ptm, 1)), "global sequence replay denied");
    Prepare(state, Request(Ptm, 2)); Commit(state);
    const auto &status = state.Snapshot();
    Check(status.pcv_phase == Committed && status.ptm_phase == Committed, "both targets committed");
    Check(status.pcv_prepares == 1 && status.ptm_prepares == 1
          && status.pcv_commits == 1 && status.ptm_commits == 1, "single transition counters");
    Check(status.pcv_sequence == 1 && status.ptm_sequence == 2, "actual sequences retained");
    Check(status.pcv_module_id[31] == 4 && status.ptm_module_id[31] == 4, "actual module IDs retained");
    Check(!Can(state, Request(Pcv, 3)) && !Can(state, Request(Ptm, 3)), "no repeated hardware side effects");
}
static void FailureAndReplay() {
    auto state = Fresh(); const auto request = Request();
    state.BeginPrepare(request);
    Check(!state.CanCommit(CommitFor(state), Owner), "unfinished preparation cannot commit");
    Check(!Can(state, Request(Pcv, 2)) && !Can(state, Request(Ptm, 2)), "failed preparation remains sticky");
    Check(state.Hello(HelloRequestForTest(), Owner), "failed state reconnect recognized");
    Check(!Can(state, Request(Pcv, 3)), "reconnect cannot clear failure");
    state = Fresh(); Prepare(state, request);
    state.FailCommit(0x1234);
    Check(state.Snapshot().pcv_phase == Failed && state.Snapshot().last_error == 0x1234, "commit failure recorded");
    Check(state.Snapshot().pcv_commits == 0, "failed commit not counted");
    Check(!Can(state, Request(Ptm, 2)) && !state.CanCommit(CommitFor(state), Owner), "failed commit cannot resume");
    state = Fresh(); Prepare(state, request); Commit(state);
    Prepare(state, Request(Ptm, 2)); state.FailCommit(0x5678);
    Check(state.Snapshot().pcv_phase == Committed && state.Snapshot().ptm_phase == Failed,
          "late PTM failure does not pretend to roll back running PCV");
    Check(state.Snapshot().pcv_commits == 1 && state.Snapshot().ptm_commits == 0, "late failure counters");
    state = Fresh(); Prepare(state, Request(Pcv, UINT64_MAX)); Commit(state);
    Check(!Can(state, Request(Ptm, 0)) && !Can(state, Request(Ptm, 1))
          && !Can(state, Request(Ptm, UINT64_MAX)), "sequence cannot wrap");
}
static void BufferAdmission() {
    auto state = Fresh(); auto request = Request();
    const auto n = static_cast<std::size_t>(request.image_size);
    auto valid = [&](std::uintptr_t in, std::size_t ins, std::uintptr_t out, std::size_t outs) {
        return state.CanPrepare(request, Owner, in, ins, out, outs);
    };
    Check(valid(Input, n, Input + n, n), "touching non-overlapping buffers accepted");
    Check(valid(Output, n, Output - n, n), "reverse-order disjoint buffers accepted");
    Check(!valid(Input, n, Input, n), "identical virtual buffers denied");
    Check(!valid(Input, n, Input + PageSize, n), "forward overlap denied");
    Check(!valid(Input + PageSize, n, Input, n), "reverse overlap denied");
    Check(!valid(0, n, Output, n) && !valid(Input, n, 0, n), "null buffers denied");
    Check(!valid(Input, n - 1, Output, n) && !valid(Input, n, Output, n + 1), "wrong descriptor sizes denied");
    for (std::uintptr_t offset = 1; offset < PageSize; ++offset) {
        Check(!valid(Input + offset, n, Output, n), "unaligned input denied");
        Check(!valid(Input, n, Output + offset, n), "unaligned output denied");
    }
    const auto wrapping = UINTPTR_MAX - n + 1;
    Check(!valid(wrapping, n, Output, n), "input overflow denied");
    Check(!valid(Input, n, wrapping, n), "output overflow denied");
    request.generation = Generation + 1;
    Check(!valid(Input, n, Output, n), "descriptor generation enforced by production state");
}
int main() {
    AuthenticationAndOrdering(); FailureAndReplay(); BufferAdmission();
    std::cout << checks << " service-state checks passed\n";
}
