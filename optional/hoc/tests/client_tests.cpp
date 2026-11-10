// Runs the actual production Loader client with deterministic OS/IPC fixtures.
#include "../Atmosphere/stratosphere/loader/source/nexus_hoc_client.cpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>
namespace nh = nexus_hoc;
namespace client = ams::ldr::nexus_oc;
static unsigned checks;
static void Check(bool value, const char *name) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
namespace fixture {
enum class Mode { Good, HelloFail, MissingHandle, BadDescriptor, PatchFail,
    WrongMagic, WrongAbi, WrongSize, WrongKind, WrongGeneration, WrongSequence,
    WrongPhase, WrongReserved, WrongImageSize, WrongHash, CommitFail, CommitWrongHash, CommitWrongPhase };
static Mode mode;
static nh::PatchReply last_reply;
static const void *target_image;
template<class Input, class Output>
Result Dispatch(Service *service, unsigned command, const Input &input, Output &out, const DispatchOptions &o) {
    ++dispatch_calls;
    Check(serviceIsActive(service), "IPC uses active named-port session");
    Check(o.in_send_pid && o.in_num_handles == 1 && o.in_handles[0] == CUR_PROCESS_HANDLE,
          "all mutating requests carry kernel PID and caller process handle");
    Check(input.pid_placeholder == 0, "raw ClientProcessId slot is initialized");
    if constexpr (std::is_same_v<Output, nh::Descriptor>) {
        static_assert(sizeof(Input) == 40 && offsetof(Input, request) == 8);
        Check(command == nh::HelloCommand, "hello command and wire size");
        Check(o.out_handles && o.out_handle_attrs[0] == SfOutHandleAttr_HipcCopy, "helper identity handle requested");
        Check(nh::ValidHello(input.request, firmware), "actual hello wire decodes");
        if (mode == Mode::HelloFail) return {31};
        if (mode != Mode::MissingHandle) *o.out_handles = 92;
        out = {nh::Magic, nh::Abi, sizeof(out), 260, 1, 26824, firmware,
               nh::SupportsPcv | nh::SupportsPtm, nh::DataBytes, nh::CodeBytes,
               nh::RuntimeBytes, nh::PayloadOffset, nh::MaxImageBytes, 0, 9, {1}};
        if (mode == Mode::BadDescriptor) out.abi = 3;
    } else if constexpr (std::is_same_v<Input, nh::AuthenticatedInput<nh::PatchRequest>>) {
        static_assert(sizeof(Input) == 136 && offsetof(Input, request) == 8);
        Check(command == nh::PatchCommand, "patch command and wire size");
        const auto &r = input.request;
        Check(nh::ValidPatch(r, 9, o.buffers[0].size, o.buffers[1].size), "actual patch wire decodes");
        Check(o.buffer_attrs[0] == (SfBufferAttr_In | SfBufferAttr_HipcMapAlias)
              && o.buffer_attrs[1] == (SfBufferAttr_Out | SfBufferAttr_HipcMapAlias), "separate input/output descriptors");
        Check(o.buffers[0].pointer == client::g_source && o.buffers[1].pointer == client::g_destination,
              "only dedicated BSS arrays sent over IPC");
        Check(o.buffers[0].pointer != target_image && o.buffers[1].pointer != target_image,
              "target process mapping never exposed to IPC");
        Check(reinterpret_cast<std::uintptr_t>(o.buffers[0].pointer) % nh::PageSize == 0
              && reinterpret_cast<std::uintptr_t>(o.buffers[1].pointer) % nh::PageSize == 0,
              "scratch addresses page aligned");
        Check(static_cast<u8 *>(o.buffers[0].pointer) + nh::MaxImageBytes <= o.buffers[1].pointer
              || static_cast<u8 *>(o.buffers[1].pointer) + nh::MaxImageBytes <= o.buffers[0].pointer,
              "scratch arrays do not overlap");
        u8 digest[32]; ams::crypto::GenerateSha256(digest, sizeof(digest), o.buffers[0].pointer, o.buffers[0].size);
        Check(std::memcmp(digest, r.input_hash, sizeof(digest)) == 0, "client digest covers actual input");
        std::memcpy(o.buffers[1].pointer, o.buffers[0].pointer, o.buffers[0].size);
        static_cast<u8 *>(o.buffers[1].pointer)[0] ^= 0x5A;
        out = {nh::Magic, nh::Abi, sizeof(out), r.kind, r.generation, r.sequence,
               nh::Prepared, 0, r.image_size, {}};
        ams::crypto::GenerateSha256(out.output_hash, sizeof(out.output_hash), o.buffers[1].pointer, o.buffers[1].size);
        if (mode == Mode::PatchFail) return {32};
        switch (mode) {
            case Mode::WrongMagic: ++out.magic; break;
            case Mode::WrongAbi: ++out.abi; break;
            case Mode::WrongSize: --out.size; break;
            case Mode::WrongKind: out.kind = 99; break;
            case Mode::WrongGeneration: ++out.generation; break;
            case Mode::WrongSequence: ++out.sequence; break;
            case Mode::WrongPhase: out.phase = nh::Failed; break;
            case Mode::WrongReserved: ++out.reserved; break;
            case Mode::WrongImageSize: ++out.image_size; break;
            case Mode::WrongHash: out.output_hash[31] ^= 1; break;
            default: break;
        }
        last_reply = out;
    } else {
        static_assert(sizeof(Input) == 72 && offsetof(Input, request) == 8);
        Check(command == nh::CommitCommand, "commit command and wire size");
        Check(nh::ValidCommit(input.request, last_reply), "actual commit wire decodes");
        if (mode == Mode::CommitFail) return {33};
        out = last_reply; out.phase = nh::Committed;
        if (mode == Mode::CommitWrongHash) out.output_hash[17] ^= 1;
        if (mode == Mode::CommitWrongPhase) out.phase = nh::Prepared;
    }
    return {};
}
}
static void Reset(fixture::Mode mode = fixture::Mode::Good) {
    fixture::mode = mode; fixture::last_reply = {};
    fixture::tick_ms = fixture::connect_calls = fixture::close_calls = fixture::service_closes = fixture::dispatch_calls = 0;
    fixture::unavailable_attempts = 0; fixture::connect_error = {};
    fixture::last_closed_handle = INVALID_HANDLE;
    fixture::firmware = nh::Firmware2300; fixture::helper_id = nh::HelperProgramId;
    client::g_helper = {}; client::g_descriptor = {}; client::g_pending_request = {}; client::g_pending_reply = {};
    client::g_next_sequence = 1; client::g_admitted = client::g_pending = client::g_failed = false;
}
static std::array<u8, 32> ModuleId() { return {0xC9}; }
static Result PatchImage(std::vector<u8> &image) {
    const auto id = ModuleId(); fixture::target_image = image.data();
    return client::Patch(nh::Pcv, id.data(), 0x7100000000ULL, image.data(),
                         image.size() - nh::ArenaBytes, image.size());
}
static Result PublishProcessFixture(bool g_is_pcv, bool g_is_ptm, Handle *out) {
    namespace nexus_oc = client;
    namespace svc = ams::svc;
    struct { Handle process_handle; } info{191};
    // Generated directly from the current production CreateProcess function.
    // Full-call ordering and the source hash are separately recorded by runner.
#include "../evidence/process_commit_fragment.inc"
    *out = info.process_handle;
    R_SUCCEED();
}
int main() {
    Reset(); Check(R_SUCCEEDED(client::EnsureAvailable()), "hello succeeds");
    Check(fixture::close_calls == 1 && fixture::service_closes == 0, "identity handle closed; session retained");
    Check(R_SUCCEEDED(client::EnsureAvailable()) && fixture::dispatch_calls == 1, "admission cached once");
    for (auto mode : {fixture::Mode::HelloFail, fixture::Mode::MissingHandle, fixture::Mode::BadDescriptor}) {
        Reset(mode); Check(R_FAILED(client::EnsureAvailable()), "bad hello rejected");
        auto calls = fixture::dispatch_calls; Check(R_FAILED(client::EnsureAvailable()) && calls == fixture::dispatch_calls, "hello failure stays poisoned");
        Check(fixture::service_closes == 1, "failed session closed");
    }
    Reset(); fixture::helper_id = nh::LoaderProgramId;
    Check(R_FAILED(client::EnsureAvailable()) && fixture::close_calls == 1, "wrong peer ProgramId rejected and handle closed");
    Reset(); fixture::unavailable_attempts = 3;
    Check(R_SUCCEEDED(client::EnsureAvailable()) && fixture::tick_ms == 30, "bounded late helper discovery");
    Reset(); fixture::unavailable_attempts = 10000;
    Check(client::EnsureAvailable() == ams::svc::ResultTimedOut() && fixture::tick_ms == 5000
          && fixture::dispatch_calls == 0, "absent helper stops at connection budget");
    Reset(); fixture::connect_error = {88};
    Check(R_FAILED(client::EnsureAvailable()) && fixture::tick_ms == 0, "non-NotFound connection failure immediate");
    Reset(); fixture::firmware = 0x18000000;
    Check(R_FAILED(client::EnsureAvailable()) && fixture::connect_calls == 0, "unsupported firmware rejected before connection");
    for (auto mode : {fixture::Mode::PatchFail, fixture::Mode::WrongMagic, fixture::Mode::WrongAbi,
         fixture::Mode::WrongSize, fixture::Mode::WrongKind, fixture::Mode::WrongGeneration,
         fixture::Mode::WrongSequence, fixture::Mode::WrongPhase, fixture::Mode::WrongReserved,
         fixture::Mode::WrongImageSize, fixture::Mode::WrongHash}) {
        Reset(mode); std::vector<u8> image(0x10000 + nh::ArenaBytes, 0xA5); const auto original = image;
        Check(R_FAILED(PatchImage(image)), "failed/malformed patch reply rejected");
        Check(image == original, "failed/malformed reply never modifies target image");
        Check(client::g_failed && !client::g_pending, "failure discards pending state");
        auto calls = fixture::dispatch_calls;
        Check(R_FAILED(client::Commit()) && calls == fixture::dispatch_calls, "failure cannot commit");
    }
    for (auto mode : {fixture::Mode::CommitFail, fixture::Mode::CommitWrongHash, fixture::Mode::CommitWrongPhase}) {
        Reset(); std::vector<u8> image(0x10000 + nh::ArenaBytes, 0xA5);
        Check(R_SUCCEEDED(PatchImage(image)) && image[0] == (0xA5 ^ 0x5A), "valid reply copied to target");
        fixture::mode = mode; Check(R_FAILED(client::Commit()), "bad commit acknowledgement rejected");
        Check(client::g_failed && !client::g_pending && fixture::service_closes == 1, "commit failure poisons session");
    }
    Reset(); std::vector<u8> image(0x10000 + nh::ArenaBytes, 0xA5);
    Check(R_SUCCEEDED(PatchImage(image)), "prepare before permission failure");
    client::InvalidatePending();
    Check(client::g_failed && !client::g_pending && fixture::service_closes == 1, "post-permission failure cleanup poisons pending operation");
    Check(R_FAILED(client::Commit()), "poisoned operation cannot publish success");
    Reset(); Check(R_SUCCEEDED(PatchImage(image)), "prepare before success");
    Check(R_SUCCEEDED(client::Commit()) && !client::g_pending && !client::g_failed, "valid commit accepted");
    client::InvalidatePending(); Check(!client::g_failed, "scope cleanup after successful commit is harmless");
    const auto calls = fixture::dispatch_calls;
    Check(R_FAILED(client::Commit()) && calls == fixture::dispatch_calls, "duplicate client commit denied");
    for (auto mode : {fixture::Mode::CommitFail, fixture::Mode::CommitWrongHash, fixture::Mode::CommitWrongPhase}) {
        Reset(); image.assign(0x10000 + nh::ArenaBytes, 0xA5);
        Check(R_SUCCEEDED(PatchImage(image)), "prepare for production publication block");
        fixture::mode = mode; Handle published = INVALID_HANDLE;
        const auto close_before = fixture::close_calls;
        Check(R_FAILED(PublishProcessFixture(true, false, &published)), "production block returns commit failure");
        Check(published == INVALID_HANDLE && fixture::close_calls == close_before + 1
              && fixture::last_closed_handle == 191, "production block closes unpublished process handle");
        Check(client::g_failed && !client::g_pending, "production scope exit leaves failed operation poisoned");
    }
    Reset(); image.assign(0x10000 + nh::ArenaBytes, 0xA5);
    Check(R_SUCCEEDED(PatchImage(image)), "prepare for successful production publication");
    Handle published = INVALID_HANDLE;
    const auto close_before = fixture::close_calls;
    Check(R_SUCCEEDED(PublishProcessFixture(true, false, &published)) && published == 191,
          "production block publishes only after successful commit");
    Check(fixture::close_calls == close_before && !client::g_failed, "successful target remains owned by caller");
    std::cout << checks << " actual-client fixture checks passed\n";
}
