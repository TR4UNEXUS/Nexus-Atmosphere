// Host tests of the actual wire/admission helpers. No Horizon IPC emulation.
#include "../common/nexus_hoc_protocol.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace nh = nexus_hoc;
static unsigned checks;
static void Check(bool value, const char *name) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}

static nh::HelloRequest Hello(std::uint32_t fw = nh::Firmware2300) {
    return {nh::Magic, nh::Abi, sizeof(nh::HelloRequest), fw, 1, 0};
}
static nh::Descriptor Descriptor(std::uint32_t fw = nh::Firmware2300) {
    nh::Descriptor d{};
    d.magic = nh::Magic; d.abi = nh::Abi; d.size = sizeof(d);
    d.engine_version = 260; d.cust_revision = 1; d.cust_size = 26824;
    d.firmware = fw; d.flags = nh::SupportsPcv | nh::SupportsPtm;
    d.data_bytes = nh::DataBytes; d.code_bytes = nh::CodeBytes;
    d.runtime_bytes = nh::RuntimeBytes; d.payload_offset = nh::PayloadOffset;
    d.max_image_bytes = nh::MaxImageBytes; d.generation = 19;
    d.settings_hash[31] = 0xA5;
    return d;
}
static nh::PatchRequest Patch(std::uint32_t kind = nh::Pcv) {
    nh::PatchRequest r{};
    r.magic = nh::Magic; r.abi = nh::Abi; r.size = sizeof(r); r.kind = kind;
    r.generation = 19; r.sequence = 27;
    r.program_id = kind == nh::Pcv ? nh::PcvProgramId : nh::PtmProgramId;
    r.target_address = 0x7100000000ULL;
    r.module_size = 0x20000;
    r.image_size = r.module_size + (kind == nh::Pcv ? nh::ArenaBytes : 0);
    r.module_id[0] = 0x81; r.input_hash[31] = 0x73;
    return r;
}
static bool Valid(const nh::PatchRequest &r) {
    return nh::ValidPatch(r, 19, static_cast<std::size_t>(r.image_size),
                         static_cast<std::size_t>(r.image_size));
}
static nh::PatchReply Reply(const nh::PatchRequest &r) {
    nh::PatchReply p{};
    p.magic = nh::Magic; p.abi = nh::Abi; p.size = sizeof(p); p.kind = r.kind;
    p.generation = r.generation; p.sequence = r.sequence; p.phase = nh::Prepared;
    p.image_size = r.image_size; p.output_hash[0] = 0x23;
    return p;
}
template<class T, class V, class F>
static void RejectField(const T &good, V T::*member, V bad, F valid, const char *name) {
    T r = good; r.*member = bad; Check(!valid(r), name);
}

static void HandshakeTests() {
    for (auto fw : {nh::Firmware2300, nh::Firmware2301}) {
        auto h = Hello(fw);
        auto valid_h = [fw](const auto &r) { return nh::ValidHello(r, fw); };
        Check(valid_h(h), "supported hello");
        for (auto member : {&nh::HelloRequest::magic, &nh::HelloRequest::abi,
                            &nh::HelloRequest::size, &nh::HelloRequest::firmware})
            RejectField(h, member, std::uint32_t{0}, valid_h, "hello required field");
        RejectField(h, &nh::HelloRequest::reserved, std::uint64_t{1}, valid_h, "hello reserved");
        RejectField(h, &nh::HelloRequest::loader_revision, std::uint64_t{2}, valid_h, "hello loader revision");
        auto d = Descriptor(fw);
        auto valid_d = [fw](const auto &r) { return nh::ValidDescriptor(r, fw); };
        Check(valid_d(d), "supported descriptor");
        for (auto member : {&nh::Descriptor::magic, &nh::Descriptor::abi, &nh::Descriptor::size,
                            &nh::Descriptor::engine_version, &nh::Descriptor::cust_revision,
                            &nh::Descriptor::cust_size, &nh::Descriptor::firmware,
                            &nh::Descriptor::flags, &nh::Descriptor::data_bytes,
                            &nh::Descriptor::code_bytes, &nh::Descriptor::runtime_bytes,
                            &nh::Descriptor::payload_offset, &nh::Descriptor::max_image_bytes})
            RejectField(d, member, std::uint32_t{0}, valid_d, "descriptor required field");
        RejectField(d, &nh::Descriptor::reserved, std::uint32_t{1}, valid_d, "descriptor reserved");
        RejectField(d, &nh::Descriptor::generation, std::uint64_t{0}, valid_d, "descriptor generation");
        RejectField(d, &nh::Descriptor::cust_size, std::uint32_t{11}, valid_d, "short CUST");
        RejectField(d, &nh::Descriptor::cust_size, std::uint32_t{0x10001}, valid_d, "large CUST");
        for (auto flags : {nh::SupportsPcv, nh::SupportsPtm, std::uint32_t{7}})
            RejectField(d, &nh::Descriptor::flags, flags, valid_d, "partial/unknown features");
        std::memset(d.settings_hash, 0, sizeof(d.settings_hash));
        Check(!valid_d(d), "missing settings fingerprint");
        d = Descriptor(fw); ++d.engine_version; ++d.cust_revision;
        Check(valid_d(d), "independent compatible engine revisions");
        Check(!nh::ValidHello(h, fw == nh::Firmware2300 ? nh::Firmware2301 : nh::Firmware2300), "firmware mismatch");
    }
    for (std::uint32_t fw : {0U, 0x16000000U, 0x17000001U, 0x17000200U, 0x18000000U, UINT32_MAX}) {
        Check(!nh::SupportedFirmware(fw), "unsupported firmware");
        Check(!nh::ValidHello(Hello(fw), fw), "unsupported hello");
        Check(!nh::ValidDescriptor(Descriptor(fw), fw), "unsupported descriptor");
    }
}

static void PatchTests() {
    for (auto kind : {nh::Pcv, nh::Ptm}) {
        auto good = Patch(kind);
        Check(Valid(good), "valid patch");
        for (auto member : {&nh::PatchRequest::magic, &nh::PatchRequest::abi,
                            &nh::PatchRequest::size, &nh::PatchRequest::kind})
            RejectField(good, member, std::uint32_t{0}, Valid, "patch required field");
        for (auto member : {&nh::PatchRequest::generation, &nh::PatchRequest::sequence,
                            &nh::PatchRequest::program_id, &nh::PatchRequest::target_address,
                            &nh::PatchRequest::module_size, &nh::PatchRequest::image_size})
            RejectField(good, member, std::uint64_t{0}, Valid, "patch required field64");
        RejectField(good, &nh::PatchRequest::generation, std::uint64_t{20}, Valid, "wrong generation");
        RejectField(good, &nh::PatchRequest::kind, std::uint32_t{3}, Valid, "unknown kind");
        RejectField(good, &nh::PatchRequest::program_id, nh::LoaderProgramId, Valid, "wrong target program");
        Check(!nh::ValidPatch(good, 0, good.image_size, good.image_size), "zero expected generation");
        for (int delta : {-1, 1}) {
            Check(!nh::ValidPatch(good, 19, good.image_size + delta, good.image_size), "input size mismatch");
            Check(!nh::ValidPatch(good, 19, good.image_size, good.image_size + delta), "output size mismatch");
        }
        auto r = good; std::memset(r.module_id, 0, sizeof(r.module_id));
        Check(!Valid(r), "empty module id");
        r = good; std::memset(r.input_hash, 0, sizeof(r.input_hash));
        Check(!Valid(r), "empty input digest");
        r = good; std::memset(r.module_id, 0xFC, sizeof(r.module_id));
        Check(Valid(r), "wire does not pretend to have an NSO allowlist");
        r = good; r.image_size = nh::MaxImageBytes;
        r.module_size = r.image_size - (kind == nh::Pcv ? nh::ArenaBytes : 0);
        Check(Valid(r), "exact capacity");
        r.image_size += nh::PageSize; r.module_size += nh::PageSize;
        Check(!Valid(r), "over capacity");
        r = good; r.module_size = nh::PageSize;
        r.image_size = r.module_size + (kind == nh::Pcv ? nh::ArenaBytes : 0);
        Check(Valid(r), "smallest wire module");
        r.module_size = r.image_size + nh::PageSize;
        Check(!Valid(r), "module exceeds whole image");
        r = good; r.module_size -= nh::PageSize;
        Check(!Valid(r), "wrong arena extent");
        r = good; r.target_address = UINT64_MAX - good.image_size + 1;
        Check(!Valid(r), "wrapping target range");
        r.target_address -= nh::PageSize;
        Check(Valid(r), "nonwrapping high aligned target range");
        for (std::uint32_t low = 1; low < nh::PageSize; ++low) {
            r = good; r.target_address += low; Check(!Valid(r), "unaligned target");
            r = good; r.module_size += low; Check(!Valid(r), "unaligned module");
            r = good; r.image_size += low; Check(!Valid(r), "unaligned image");
        }
        for (std::uint64_t size : {std::uint64_t{1}, std::uint64_t{nh::PageSize - 1},
                                  UINT64_MAX, UINT64_MAX - nh::PageSize + 1}) {
            r = good; r.image_size = size; r.module_size = size;
            Check(!Valid(r), "invalid or wrapping image extent");
        }
    }
}

static void ReplyCommitTests() {
    for (auto kind : {nh::Pcv, nh::Ptm}) {
        const auto request = Patch(kind);
        const auto good = Reply(request);
        const auto valid = [&request](const auto &r) { return nh::ValidReply(r, request, nh::Prepared); };
        Check(valid(good), "matching prepared reply");
        for (auto member : {&nh::PatchReply::magic, &nh::PatchReply::abi, &nh::PatchReply::size,
                            &nh::PatchReply::kind, &nh::PatchReply::phase})
            RejectField(good, member, std::uint32_t{0}, valid, "reply required field");
        for (auto member : {&nh::PatchReply::generation, &nh::PatchReply::sequence, &nh::PatchReply::image_size})
            RejectField(good, member, good.*member + 1, valid, "reply stale/foreign identity");
        RejectField(good, &nh::PatchReply::reserved, std::uint32_t{1}, valid, "reply reserved");
        RejectField(good, &nh::PatchReply::phase, nh::Failed, valid, "failed reply");
        auto reply = good; std::memset(reply.output_hash, 0, sizeof(reply.output_hash));
        Check(!valid(reply), "reply empty digest");
        nh::CommitRequest c{};
        c.magic = nh::Magic; c.abi = nh::Abi; c.size = sizeof(c); c.kind = kind;
        c.generation = good.generation; c.sequence = good.sequence;
        std::memcpy(c.output_hash, good.output_hash, sizeof(c.output_hash));
        const auto valid_c = [&good](const auto &r) { return nh::ValidCommit(r, good); };
        Check(valid_c(c), "exact commit");
        for (auto member : {&nh::CommitRequest::magic, &nh::CommitRequest::abi,
                            &nh::CommitRequest::size, &nh::CommitRequest::kind})
            RejectField(c, member, std::uint32_t{0}, valid_c, "commit required field");
        RejectField(c, &nh::CommitRequest::generation, c.generation + 1, valid_c, "stale commit generation");
        RejectField(c, &nh::CommitRequest::sequence, c.sequence + 1, valid_c, "stale commit sequence");
        for (std::size_t byte = 0; byte < sizeof(c.output_hash); ++byte) {
            auto changed = c; changed.output_hash[byte] ^= 1;
            Check(!valid_c(changed), "commit digest mismatch");
        }
        for (auto phase : {nh::Empty, nh::Committed, nh::Failed}) {
            reply = good; reply.phase = phase;
            Check(!nh::ValidCommit(c, reply), "commit requires pending preparation");
        }
    }
}
int main() {
    static_assert(std::is_standard_layout_v<nh::Status>);
    static_assert(std::is_trivially_copyable_v<nh::PatchRequest>);
    HandshakeTests(); PatchTests(); ReplyCommitTests();
    std::cout << checks << " protocol checks passed\n";
}
