/* Bounded splash-only Zstd adapter. GPL-2.0, like the surrounding Atmosphere code. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ams::boot::splash {
    constexpr std::size_t ZstdWorkspaceSize = 0x20000;
    bool DecodeZstdBlock(void *workspace, std::size_t workspace_size, void *dst,
                         std::size_t expected, const void *src, std::size_t size);

    struct ZstdDecompressor {
        void *workspace;
        std::size_t workspace_size;
        void *scratch;
        std::size_t scratch_size;
        std::size_t block_capacity;

        int operator()(void *dst, std::size_t expected, const void *src, std::size_t size) const {
            if (!workspace || !scratch || !src || !size || dst != scratch ||
                reinterpret_cast<std::uintptr_t>(workspace) % 8 ||
                workspace_size < ZstdWorkspaceSize ||
                !expected || expected > block_capacity ||
                expected > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                scratch_size < expected) {
                return -1;
            }
            return DecodeZstdBlock(workspace, workspace_size, dst, expected,
                                   src, size) ? static_cast<int>(expected) : -1;
        }
    };
}
