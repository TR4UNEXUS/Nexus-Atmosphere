// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "nexus_hoc_protocol.hpp"

namespace kip_target {
inline constexpr char LegacyPath[] = "sdmc:/atmosphere/kips/hoc.kip";
inline constexpr char SeparatedPath[] = "sdmc:/atmosphere/nexus-hoc/hoc.kip";
enum class Mode { Unresolved, Legacy, Separated, Blocked };
enum class Discovery { Missing, Ready, Error };

struct Io {
    std::size_t (*read)(void *, std::size_t, FILE *);
    std::size_t (*write)(const void *, std::size_t, FILE *);
    int (*seek)(FILE *, long, int);
    long (*tell)(FILE *);
    int (*flush)(FILE *);
};
inline std::size_t Read(void *p, std::size_t n, FILE *f) { return std::fread(p, 1, n, f); }
inline std::size_t Write(const void *p, std::size_t n, FILE *f) { return std::fwrite(p, 1, n, f); }
inline constexpr Io FileIo{Read, Write, std::fseek, std::ftell, std::fflush};
using HashMemory = void (*)(const void *, std::size_t, std::uint8_t *);
using HashFile = bool (*)(FILE *, long, std::size_t, std::uint8_t *, const Io &);

class Guard {
    Mode m_mode = Mode::Unresolved;
    nexus_hoc::Descriptor m_descriptor{};
    std::uint8_t m_expected_hash[32]{};

    static std::uint32_t Le32(const std::uint8_t *p) {
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8)
            | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    }
    static std::uint64_t Le64(const std::uint8_t *p) {
        return Le32(p) | (std::uint64_t(Le32(p + 4)) << 32);
    }
    bool TableIdentity(const void *table, std::size_t size) const {
        const auto *p = static_cast<const std::uint8_t *>(table);
        return table && size == m_descriptor.cust_size && size >= 12
            && std::memcmp(p, "CUST", 4) == 0 && Le32(p + 4) == m_descriptor.cust_revision
            && Le32(p + 8) == m_descriptor.engine_version;
    }
    bool Locate(FILE *f, long &offset, const Io &io) const {
        if (!f || m_mode != Mode::Separated) return false;
        std::uint8_t header[0x100];
        if (io.seek(f, 0, SEEK_SET) || io.read(header, sizeof(header), f) != sizeof(header)
            || std::memcmp(header, "KIP1", 4) || Le64(header + 0x10) != nexus_hoc::HelperProgramId
            || (header[0x1f] & 4)) return false; // CUST data must be directly editable.
        const std::uint64_t start = 0x100ULL + Le32(header + 0x28) + Le32(header + 0x38);
        const std::uint64_t size = Le32(header + 0x48);
        if (size != Le32(header + 0x44) || size < m_descriptor.cust_size
            || start > 16 * 1024 * 1024 || size > 16 * 1024 * 1024 - start
            || io.seek(f, 0, SEEK_END)) return false;
        const long file_size = io.tell(f);
        if (file_size < 0 || std::uint64_t(file_size) < start + size) return false;

        std::uint8_t block[512];
        bool found = false;
        for (std::uint64_t relative = 0; relative + 4 <= size;) {
            const std::size_t bytes = std::min<std::uint64_t>(sizeof(block), size - relative);
            if (io.seek(f, static_cast<long>(start + relative), SEEK_SET)
                || io.read(block, bytes, f) != bytes) return false;
            for (std::size_t i = 0; i + 4 <= bytes; ++i) {
                if (std::memcmp(block + i, "CUST", 4)) continue;
                const std::uint64_t at = relative + i;
                if (found || at > size - m_descriptor.cust_size) return false;
                std::uint8_t identity[12];
                offset = static_cast<long>(start + at);
                if (io.seek(f, offset, SEEK_SET) || io.read(identity, sizeof(identity), f) != sizeof(identity)
                    || Le32(identity + 4) != m_descriptor.cust_revision
                    || Le32(identity + 8) != m_descriptor.engine_version) return false;
                found = true;
            }
            if (bytes < 4) break;
            relative += bytes - 3; // Preserve markers straddling chunk boundaries.
        }
        return found;
    }
public:
    Mode GetMode() const { return m_mode; }
    const char *Path() const {
        return m_mode == Mode::Legacy ? LegacyPath : m_mode == Mode::Separated ? SeparatedPath : nullptr;
    }
    void Block() { m_mode = Mode::Blocked; }
    void Select(Discovery discovery, const nexus_hoc::Status &status, std::uint32_t firmware,
                std::size_t cust_size, std::uint32_t cust_revision, std::uint32_t engine_version) {
        if (m_mode != Mode::Unresolved) return;
        // HS2 is a split-only build. A USB-only boot must never select or
        // modify an unrelated, older combined Loader left on the SD card.
        if (discovery == Discovery::Missing) { m_mode = Mode::Blocked; return; }
        m_mode = Mode::Blocked;
        if (discovery != Discovery::Ready || !nexus_hoc::ValidDescriptor(status.descriptor, firmware)
            || status.descriptor.cust_size != cust_size || status.descriptor.cust_revision != cust_revision
            || status.descriptor.engine_version != engine_version || status.reserved || status.last_error
            || status.pcv_phase != nexus_hoc::Committed || status.ptm_phase != nexus_hoc::Committed
            || !status.pcv_sequence || !status.ptm_sequence || !status.pcv_commits || !status.ptm_commits
            || !nexus_hoc::Nonzero(status.pcv_module_id, 32) || !nexus_hoc::Nonzero(status.ptm_module_id, 32)) return;
        m_descriptor = status.descriptor;
        std::memcpy(m_expected_hash, status.descriptor.settings_hash, sizeof(m_expected_hash));
        m_mode = Mode::Separated;
    }

    bool ReadTable(FILE *file, void *table, std::size_t size, HashMemory hash, const Io &io = FileIo) {
        long offset = 0;
        std::uint8_t actual[32];
        if (!table || size != m_descriptor.cust_size || !Locate(file, offset, io)
            || io.seek(file, offset, SEEK_SET) || io.read(table, size, file) != size
            || !TableIdentity(table, size)) { Block(); return false; }
        hash(table, size, actual);
        if (std::memcmp(actual, m_expected_hash, sizeof(actual))) { Block(); return false; }
        return true;
    }

    bool WriteTable(FILE *file, const void *table, std::size_t size,
                    HashMemory hash, HashFile hash_file, const Io &io = FileIo) {
        long offset = 0;
        std::uint8_t current[32], wanted[32], written[32];
        if (!TableIdentity(table, size) || !Locate(file, offset, io)
            || !hash_file(file, offset, size, current, io)
            || std::memcmp(current, m_expected_hash, sizeof(current))) { Block(); return false; }
        hash(table, size, wanted);
        if (io.seek(file, offset, SEEK_SET) || io.write(table, size, file) != size || io.flush(file)
            || !hash_file(file, offset, size, written, io) || std::memcmp(written, wanted, sizeof(written))) {
            Block(); return false;
        }
        // Only this completed, verified save may advance the accepted file hash.
        std::memcpy(m_expected_hash, wanted, sizeof(m_expected_hash));
        return true;
    }
};
} // namespace kip_target
