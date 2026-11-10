// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nexus_hoc {
inline constexpr char ServiceName[] = "nx:hoc";
inline constexpr std::uint64_t HelperProgramId = 0x4200000000484F43ULL;
inline constexpr std::uint64_t LoaderProgramId = 0x0100000000000001ULL;
inline constexpr std::uint64_t PcvProgramId = 0x010000000000001AULL;
inline constexpr std::uint64_t PtmProgramId = 0x0100000000000010ULL;
inline constexpr std::uint32_t Magic = 0x31434F48; // HOC1
inline constexpr std::uint32_t Abi = 1;
inline constexpr std::uint32_t HelloCommand = 0, PatchCommand = 1, CommitCommand = 2, StatusCommand = 3;
inline constexpr std::uint32_t PageSize = 0x1000;
inline constexpr std::uint32_t DataBytes = PageSize, CodeBytes = PageSize, RuntimeBytes = PageSize;
inline constexpr std::uint32_t ArenaBytes = DataBytes + CodeBytes + RuntimeBytes;
inline constexpr std::uint32_t PayloadOffset = 0x100;
inline constexpr std::uint32_t MaxImageBytes = 4 * 1024 * 1024;
inline constexpr std::uint32_t SupportsPcv = 1, SupportsPtm = 2;
inline constexpr std::uint32_t Pcv = 1, Ptm = 2;
inline constexpr std::uint32_t Empty = 0, Prepared = 1, Committed = 2, Failed = 3;
inline constexpr std::uint32_t Firmware2300 = 0x17000000, Firmware2301 = 0x17000100;

struct HelloRequest {
    std::uint32_t magic, abi, size, firmware;
    std::uint64_t loader_revision, reserved;
};
struct Descriptor {
    std::uint32_t magic, abi, size, engine_version;
    std::uint32_t cust_revision, cust_size, firmware, flags;
    std::uint32_t data_bytes, code_bytes, runtime_bytes, payload_offset;
    std::uint32_t max_image_bytes, reserved;
    std::uint64_t generation;
    std::uint8_t settings_hash[32];
};
struct PatchRequest {
    std::uint32_t magic, abi, size, kind;
    std::uint64_t generation, sequence, program_id, target_address;
    std::uint64_t module_size, image_size;
    std::uint8_t module_id[32], input_hash[32];
};
struct PatchReply {
    std::uint32_t magic, abi, size, kind;
    std::uint64_t generation, sequence;
    std::uint32_t phase, reserved;
    std::uint64_t image_size;
    std::uint8_t output_hash[32];
};
struct CommitRequest {
    std::uint32_t magic, abi, size, kind;
    std::uint64_t generation, sequence;
    std::uint8_t output_hash[32];
};
struct Status {
    Descriptor descriptor;
    std::uint32_t pcv_phase, ptm_phase, last_error, reserved;
    std::uint64_t pcv_sequence, ptm_sequence;
    std::uint8_t pcv_module_id[32], ptm_module_id[32];
    std::uint32_t pcv_prepares, ptm_prepares, pcv_commits, ptm_commits;
};
// SF ClientProcessId consumes a raw u64 slot as well as the HIPC PID descriptor.
// The receiver replaces this placeholder with the kernel-supplied caller PID.
template<typename T> struct AuthenticatedInput {
    std::uint64_t pid_placeholder;
    T request;
};
static_assert(sizeof(HelloRequest) == 32);
static_assert(sizeof(Descriptor) == 96);
static_assert(sizeof(PatchRequest) == 128);
static_assert(sizeof(PatchReply) == 80);
static_assert(sizeof(CommitRequest) == 64);
static_assert(sizeof(Status) == 208);
static_assert(sizeof(AuthenticatedInput<HelloRequest>) == 40);
static_assert(sizeof(AuthenticatedInput<PatchRequest>) == 136);
static_assert(sizeof(AuthenticatedInput<CommitRequest>) == 72);
static_assert(offsetof(PatchRequest, module_id) == 64);
static_assert(offsetof(Descriptor, generation) == 56);

constexpr bool SupportedFirmware(std::uint32_t value) {
    return value == Firmware2300 || value == Firmware2301;
}
inline bool Nonzero(const std::uint8_t *p, std::size_t n) {
    std::uint8_t bits = 0;
    for (std::size_t i = 0; i < n; ++i) bits |= p[i];
    return bits != 0;
}
inline bool ValidHello(const HelloRequest &r, std::uint32_t firmware) {
    return r.magic == Magic && r.abi == Abi && r.size == sizeof(r) && r.firmware == firmware
        && SupportedFirmware(firmware) && r.loader_revision == 1 && r.reserved == 0;
}
inline bool ValidDescriptor(const Descriptor &d, std::uint32_t firmware) {
    return d.magic == Magic && d.abi == Abi && d.size == sizeof(d) && d.engine_version != 0
        && d.cust_revision != 0 && d.cust_size >= 12 && d.cust_size <= 0x10000
        && d.firmware == firmware && SupportedFirmware(firmware)
        && d.flags == (SupportsPcv | SupportsPtm) && d.data_bytes == DataBytes
        && d.code_bytes == CodeBytes && d.runtime_bytes == RuntimeBytes
        && d.payload_offset == PayloadOffset && d.max_image_bytes == MaxImageBytes
        && d.reserved == 0 && d.generation != 0 && Nonzero(d.settings_hash, sizeof(d.settings_hash));
}
inline bool ValidPatch(const PatchRequest &r, std::uint64_t generation, std::size_t input_size, std::size_t output_size) {
    if (r.magic != Magic || r.abi != Abi || r.size != sizeof(r) || r.generation != generation || generation == 0
        || r.sequence == 0 || (r.kind != Pcv && r.kind != Ptm)
        || r.program_id != (r.kind == Pcv ? PcvProgramId : PtmProgramId)
        || r.image_size == 0 || r.image_size > MaxImageBytes || r.image_size % PageSize != 0
        || r.module_size == 0 || r.module_size % PageSize != 0 || r.module_size > r.image_size
        || input_size != r.image_size || output_size != r.image_size
        || r.target_address < PageSize || r.target_address % PageSize != 0
        || r.target_address > UINT64_MAX - r.image_size || !Nonzero(r.module_id, sizeof(r.module_id))
        || !Nonzero(r.input_hash, sizeof(r.input_hash))) return false;
    return r.image_size - r.module_size == (r.kind == Pcv ? ArenaBytes : 0);
}
inline bool ValidReply(const PatchReply &r, const PatchRequest &request, std::uint32_t phase) {
    return r.magic == Magic && r.abi == Abi && r.size == sizeof(r) && r.kind == request.kind
        && r.generation == request.generation && r.sequence == request.sequence && r.phase == phase
        && r.reserved == 0 && r.image_size == request.image_size && Nonzero(r.output_hash, sizeof(r.output_hash));
}
inline bool ValidCommit(const CommitRequest &c, const PatchReply &prepared) {
    return c.magic == Magic && c.abi == Abi && c.size == sizeof(c) && c.kind == prepared.kind
        && c.generation == prepared.generation && c.sequence == prepared.sequence
        && prepared.phase == Prepared && std::memcmp(c.output_hash, prepared.output_hash, 32) == 0;
}
} // namespace nexus_hoc
