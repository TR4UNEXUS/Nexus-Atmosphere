/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <vapours.hpp>

/*
 * Network block device (NBD) backend for the Nexus USB storage service.
 *
 * When sdmc:/atmosphere/config/nexus-nbd.ini enables it, the mass-storage
 * Device talks to an NBD server over TCP instead of a USB drive. The Device
 * still receives SCSI commands from the rest of Nexus and translates them, so
 * the filesystem, routing, sleep and eject code stay unchanged.
 *
 * Only the fixed-newstyle handshake with NBD_OPT_EXPORT_NAME and simple
 * replies are used. Requests are issued one at a time.
 */
namespace ams::usb::nbd {

    constexpr inline u32 BlockSize = 512;

    struct Config {
        bool enabled;
        char host[16];          /* dotted IPv4 address */
        u16 port;
        char export_name[64];
        u32 reconnect_ms;       /* how long one I/O may wait for the server to come back */
    };

    /* Called once by ams_mitm before the storage worker starts. */
    void Configure(const Config &config);
    bool IsEnabled();
    const Config &GetConfig();

    /* Parse "key=value" lines (host, port, export, reconnect_ms, enabled). */
    bool ParseConfig(Config *out, const char *text, size_t size);

    namespace diagnostics {
        inline std::atomic<u32> connects{};
        inline std::atomic<u32> connect_failures{};
        inline std::atomic<u32> reconnects{};
        inline std::atomic<u32> io_failures{};
        inline std::atomic<u32> last_errno{};
        inline std::atomic<u32> last_stage{};
    }

    class Connection {
        NON_COPYABLE(Connection);
        NON_MOVEABLE(Connection);
        private:
            s32 m_fd{-1};
            u64 m_size{};
            u16 m_flags{};
            u64 m_cookie{};
        public:
            Connection() = default;
            ~Connection() { this->Close(); }

            /* Connect and negotiate. Fails if the server is unreachable before deadline_ns. */
            Result Open(const Config &config, u64 deadline_ns);
            void Close();
            bool IsOpen() const { return m_fd >= 0; }

            u64 GetSize() const { return m_size; }
            bool IsReadOnly() const;
            bool CanFlush() const;

            Result Read(u64 offset, void *buffer, u32 size);
            Result Write(u64 offset, const void *buffer, u32 size);
            Result Flush();
            void Disconnect();
        private:
            Result Transact(u16 type, u64 offset, u32 length, const void *out_data, void *in_data);
    };

}
