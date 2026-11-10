// SPDX-License-Identifier: GPL-2.0-only
#include <switch.h>
#include "kip_target.hpp"
#include "kip_target_core.hpp"

namespace kip_target {
namespace {
    Guard g_guard;
    void Hash(const void *data, std::size_t size, std::uint8_t *out) { sha256CalculateHash(out, data, size); }
    bool HashRange(FILE *file, long offset, std::size_t size, std::uint8_t *out, const Io &io) {
        if (io.seek(file, offset, SEEK_SET)) return false;
        Sha256Context context;
        sha256ContextCreate(&context);
        std::uint8_t block[512];
        while (size) {
            const std::size_t bytes = std::min(size, sizeof(block));
            if (io.read(block, bytes, file) != bytes) return false;
            sha256ContextUpdate(&context, block, bytes);
            size -= bytes;
        }
        sha256ContextGetHash(&context, out);
        return true;
    }
    void Resolve(std::size_t cust_size) {
        if (g_guard.GetMode() != Mode::Unresolved) return;
        Handle handle = INVALID_HANDLE;
        const Result result = svcConnectToNamedPort(&handle, nexus_hoc::ServiceName);
        nexus_hoc::Status status{};
        Discovery discovery = Discovery::Error;
        if (result == MAKERESULT(Module_Kernel, 121)) { // Kernel ResultNotFound.
            discovery = Discovery::Missing;
        } else if (R_SUCCEEDED(result)) {
            Service helper;
            serviceCreate(&helper, handle);
            const Result query = serviceDispatchOut(&helper, nexus_hoc::StatusCommand, status);
            serviceClose(&helper);
            if (R_SUCCEEDED(query)) discovery = Discovery::Ready;
        }
        // Called by GetKipData after QLaunch. A split boot cannot reach this
        // point without both Loader commits; any inconsistent service blocks
        // file operations rather than selecting the old combined Loader.
        g_guard.Select(discovery, status, hosversionGet() << 8, cust_size, CUST_REV, KIP_VERSION);
    }
}
bool IsCommittedSeparatedBoot(std::size_t cust_size) {
    Resolve(cust_size);
    return g_guard.GetMode() == Mode::Separated;
}
FILE *Open(const char *mode, std::size_t cust_size) {
    Resolve(cust_size);
    const char *path = g_guard.Path();
    if (!path) return nullptr;
    FILE *file = std::fopen(path, mode);
    if (!file && IsSeparated()) g_guard.Block();
    return file;
}
const char *Path() { return g_guard.Path(); }
bool IsSeparated() { return g_guard.GetMode() == Mode::Separated; }
bool UseGuard() { return g_guard.GetMode() != Mode::Legacy; }
bool ReadTable(FILE *file, void *table, std::size_t size) { return g_guard.ReadTable(file, table, size, Hash); }
bool WriteTable(FILE *file, const void *table, std::size_t size) { return g_guard.WriteTable(file, table, size, Hash, HashRange); }
} // namespace kip_target
