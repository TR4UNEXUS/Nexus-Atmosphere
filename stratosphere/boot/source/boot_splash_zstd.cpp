/* Splash-only static standard-Zstd context. GPL-2.0, like Atmosphere. */
#include <stratosphere.hpp>
#include "boot_splash_zstd.hpp"
#define ZSTD_STATIC_LINKING_ONLY
#define ZSTD_ZBIC_SUPPORT 0
#define ZSTDLIB_VISIBLE static
#define ZSTDLIB_HIDDEN static
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../../libraries/libstratosphere/source/util/zstd.h"
#include "../../../libraries/libstratosphere/source/util/zstd.inc"
#pragma GCC diagnostic pop

namespace ams::boot::splash {
    static_assert(sizeof(ZSTD_DCtx) <= ZstdWorkspaceSize);

    bool DecodeZstdBlock(void *workspace, std::size_t workspace_size, void *dst,
                         std::size_t expected, const void *src, std::size_t size) {
        if (!workspace || !dst || !src || !size || !expected ||
            reinterpret_cast<std::uintptr_t>(workspace) % 8 ||
            workspace_size < ZstdWorkspaceSize || ZSTD_estimateDCtxSize() > workspace_size) {
            return false;
        }
        auto *context = ZSTD_initStaticDCtx(workspace, workspace_size);
        if (!context) return false;
        // Separate read-only source and bounded output; no in-place margin needed.
        // A static context cannot allocate a heap buffer. Frame checksums stay enabled.
        const auto decoded = ZSTD_decompressDCtx(context, dst, expected, src, size);
        return !ZSTD_isError(decoded) && decoded == expected;
    }
}
