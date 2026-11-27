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
#include <stratosphere.hpp>
#include <stratosphere/usb/usb_nbd.hpp>

#if defined(ATMOSPHERE_OS_HORIZON)
extern "C" {
#include <switch/services/bsd.h>
}
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>

namespace ams::usb::nbd {

    namespace {

        constexpr u64 NbdMagic         = 0x4E42444D41474943ULL; /* "NBDMAGIC" */
        constexpr u64 IHaveOpt         = 0x49484156454F5054ULL; /* "IHAVEOPT" */
        constexpr u32 FlagFixedNewstyle = 1u << 0;
        constexpr u32 FlagNoZeroes      = 1u << 1;
        constexpr u32 OptExportName     = 1;
        constexpr u32 RequestMagic      = 0x25609513;
        constexpr u32 ReplyMagic        = 0x67446698;
        constexpr u16 CmdRead           = 0;
        constexpr u16 CmdWrite          = 1;
        constexpr u16 CmdDisconnect     = 2;
        constexpr u16 CmdFlush          = 3;
        constexpr u16 TransmitHasFlags  = 1u << 0;
        constexpr u16 TransmitReadOnly  = 1u << 1;
        constexpr u16 TransmitSendFlush = 1u << 2;

        /* Socket receive/send timeout: a dead link is reported instead of hanging I/O. */
        constexpr int SocketTimeoutSeconds = 5;

        /* bsd:s transfer memory, sized for one TCP socket with a 256 KiB receive
         * window. Allocated from the process heap on first use, so the USB-only
         * mode costs no memory at all. */
        constexpr u32 TcpTxSize    = 0x8000;
        constexpr u32 TcpTxMaxSize = 0x20000;
        constexpr u32 TcpRxSize    = 0x20000;
        constexpr u32 TcpRxMaxSize = 0x40000;
        constexpr u32 UdpTxSize    = 0x2400;
        constexpr u32 UdpRxSize    = 0xA500;
        constexpr u32 SbEfficiency = 2;
        constexpr size_t TransferMemorySize =
            util::AlignUp<size_t>(TcpTxMaxSize + TcpRxMaxSize + UdpTxSize + UdpRxSize, os::MemoryPageSize) * SbEfficiency;

        constinit os::SdkMutex g_bsd_mutex;
        constinit bool g_bsd_initialized = false;
        constinit Config g_config{};

        u64 NowNs() { return os::GetSystemTick().ToTimeSpan().GetNanoSeconds(); }

        void PutBe16(u8 *p, u16 v) { p[0] = static_cast<u8>(v >> 8); p[1] = static_cast<u8>(v); }
        void PutBe32(u8 *p, u32 v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<u8>(v >> (24 - 8 * i)); }
        void PutBe64(u8 *p, u64 v) { for (int i = 0; i < 8; ++i) p[i] = static_cast<u8>(v >> (56 - 8 * i)); }
        u16 GetBe16(const u8 *p) { return static_cast<u16>(p[0] << 8 | p[1]); }
        u32 GetBe32(const u8 *p) { return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3]; }
        u64 GetBe64(const u8 *p) { return static_cast<u64>(GetBe32(p)) << 32 | GetBe32(p + 4); }

        Result EnsureBsd() {
            std::scoped_lock lk(g_bsd_mutex);
            if (g_bsd_initialized) R_SUCCEED();
            static void *transfer_memory = nullptr;
            if (transfer_memory == nullptr) {
                transfer_memory = std::aligned_alloc(os::MemoryPageSize, TransferMemorySize);
                R_UNLESS(transfer_memory != nullptr, usb::ResultResourceBusy());
            }
            const ::BsdInitConfig config = {
                .version             = 1,
                .tmem_buffer         = transfer_memory,
                .tmem_buffer_size    = TransferMemorySize,
                .tcp_tx_buf_size     = TcpTxSize,
                .tcp_rx_buf_size     = TcpRxSize,
                .tcp_tx_buf_max_size = TcpTxMaxSize,
                .tcp_rx_buf_max_size = TcpRxMaxSize,
                .udp_tx_buf_size     = UdpTxSize,
                .udp_rx_buf_size     = UdpRxSize,
                .sb_efficiency       = SbEfficiency,
            };
            R_TRY(sm::Initialize());
            /* bsd:s (system client). One session: the storage worker serializes all I/O. */
            R_TRY(::bsdInitialize(std::addressof(config), 1, 1u << 1));
            g_bsd_initialized = true;
            R_SUCCEED();
        }

        bool ParseIpv4(const char *text, u32 *out) {
            u32 parts[4]{};
            int index = 0;
            bool digit = false;
            for (const char *p = text; ; ++p) {
                if (*p >= '0' && *p <= '9') {
                    parts[index] = parts[index] * 10 + static_cast<u32>(*p - '0');
                    if (parts[index] > 255) return false;
                    digit = true;
                } else if (*p == '.' || *p == '\0') {
                    if (!digit) return false;
                    digit = false;
                    if (*p == '\0') break;
                    if (++index > 3) return false;
                } else {
                    return false;
                }
            }
            if (index != 3) return false;
            *out = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
            return true;
        }

        void CopyValue(char *dst, size_t dst_size, const char *src, size_t len) {
            const size_t n = std::min(len, dst_size - 1);
            std::memcpy(dst, src, n);
            dst[n] = '\0';
        }

        u32 ParseU32(const char *src, size_t len) {
            u32 v = 0;
            for (size_t i = 0; i < len && src[i] >= '0' && src[i] <= '9'; ++i) v = v * 10 + static_cast<u32>(src[i] - '0');
            return v;
        }

    }

    void Configure(const Config &config) { g_config = config; }
    bool IsEnabled() { return g_config.enabled; }
    const Config &GetConfig() { return g_config; }

    bool ParseConfig(Config *out, const char *text, size_t size) {
        Config config{};
        config.enabled = true;
        config.port = 10809;
        config.reconnect_ms = 20000;
        std::strcpy(config.export_name, "nexus");
        size_t pos = 0;
        while (pos < size) {
            size_t end = pos;
            while (end < size && text[end] != '\n') ++end;
            size_t line_end = end;
            while (line_end > pos && (text[line_end - 1] == '\r' || text[line_end - 1] == ' ' || text[line_end - 1] == '\t')) --line_end;
            while (pos < line_end && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
            if (pos < line_end && text[pos] != '#' && text[pos] != ';' && text[pos] != '[') {
                size_t eq = pos;
                while (eq < line_end && text[eq] != '=') ++eq;
                if (eq < line_end) {
                    size_t key_end = eq;
                    while (key_end > pos && (text[key_end - 1] == ' ' || text[key_end - 1] == '\t')) --key_end;
                    size_t value = eq + 1;
                    while (value < line_end && (text[value] == ' ' || text[value] == '\t')) ++value;
                    const size_t key_len = key_end - pos, value_len = line_end - value;
                    const auto key_is = [&](const char *k) { return std::strlen(k) == key_len && std::memcmp(text + pos, k, key_len) == 0; };
                    if (key_is("host")) CopyValue(config.host, sizeof(config.host), text + value, value_len);
                    else if (key_is("port")) config.port = static_cast<u16>(ParseU32(text + value, value_len));
                    else if (key_is("export")) CopyValue(config.export_name, sizeof(config.export_name), text + value, value_len);
                    else if (key_is("reconnect_ms")) config.reconnect_ms = ParseU32(text + value, value_len);
                    else if (key_is("enabled")) config.enabled = value_len > 0 && (text[value] == '1' || text[value] == 't' || text[value] == 'y');
                }
            }
            pos = end + 1;
        }
        u32 ip = 0;
        if (!ParseIpv4(config.host, std::addressof(ip)) || config.port == 0 || config.export_name[0] == '\0') return false;
        if (config.reconnect_ms < 1000) config.reconnect_ms = 1000;
        if (config.reconnect_ms > 120000) config.reconnect_ms = 120000;
        *out = config;
        return true;
    }

    namespace {

        Result SendAll(s32 fd, const void *data, size_t size) {
            const u8 *p = static_cast<const u8 *>(data);
            while (size > 0) {
                const ssize_t n = ::bsdSend(fd, p, size, 0);
                if (n <= 0) {
                    diagnostics::last_errno.store(static_cast<u32>(g_bsdErrno));
                    R_THROW(usb::ResultTransactionError());
                }
                p += n;
                size -= static_cast<size_t>(n);
            }
            R_SUCCEED();
        }

        Result RecvAll(s32 fd, void *data, size_t size) {
            u8 *p = static_cast<u8 *>(data);
            while (size > 0) {
                const ssize_t n = ::bsdRecv(fd, p, size, 0);
                if (n <= 0) {
                    diagnostics::last_errno.store(n == 0 ? 0 : static_cast<u32>(g_bsdErrno));
                    R_THROW(usb::ResultTransactionError());
                }
                p += n;
                size -= static_cast<size_t>(n);
            }
            R_SUCCEED();
        }

    }

    bool Connection::IsReadOnly() const { return (m_flags & TransmitHasFlags) && (m_flags & TransmitReadOnly); }
    bool Connection::CanFlush() const { return (m_flags & TransmitHasFlags) && (m_flags & TransmitSendFlush); }

    void Connection::Close() {
        if (m_fd >= 0) {
            ::bsdClose(m_fd);
            m_fd = -1;
        }
    }

    void Connection::Disconnect() {
        if (m_fd >= 0) {
            u8 request[28]{};
            PutBe32(request, RequestMagic);
            PutBe16(request + 6, CmdDisconnect);
            static_cast<void>(SendAll(m_fd, request, sizeof(request)));
        }
        this->Close();
    }

    Result Connection::Open(const Config &config, u64 deadline_ns) {
        this->Close();
        u32 ip = 0;
        R_UNLESS(ParseIpv4(config.host, std::addressof(ip)), usb::ResultInvalidParameter());
        diagnostics::last_stage.store(1);
        R_TRY(EnsureBsd());

        for (;;) {
            diagnostics::last_stage.store(2);
            const s32 fd = ::bsdSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fd >= 0) {
                const int one = 1;
                struct timeval tv{};
                tv.tv_sec = SocketTimeoutSeconds;
                ::bsdSetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                ::bsdSetSockOpt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
                ::bsdSetSockOpt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                ::bsdSetSockOpt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

                struct sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = static_cast<u16>((config.port >> 8) | (config.port << 8));
                addr.sin_addr.s_addr = __builtin_bswap32(ip);
                if (::bsdConnect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
                    m_fd = fd;
                    const Result rc = [&]() -> Result {
                        diagnostics::last_stage.store(3);
                        u8 hello[18];
                        R_TRY(RecvAll(m_fd, hello, sizeof(hello)));
                        R_UNLESS(GetBe64(hello) == NbdMagic && GetBe64(hello + 8) == IHaveOpt, usb::ResultOperationDenied());
                        const u16 handshake_flags = GetBe16(hello + 16);
                        const u32 client_flags = FlagFixedNewstyle | ((handshake_flags & FlagNoZeroes) ? FlagNoZeroes : 0);
                        const size_t name_len = std::strlen(config.export_name);
                        u8 option[20 + sizeof(config.export_name)];
                        PutBe32(option, client_flags);
                        PutBe64(option + 4, IHaveOpt);
                        PutBe32(option + 12, OptExportName);
                        PutBe32(option + 16, static_cast<u32>(name_len));
                        std::memcpy(option + 20, config.export_name, name_len);
                        R_TRY(SendAll(m_fd, option, 20 + name_len));
                        diagnostics::last_stage.store(4);
                        /* An unknown export or a denied client makes the server close here. */
                        u8 info[10 + 124];
                        R_TRY(RecvAll(m_fd, info, (client_flags & FlagNoZeroes) ? 10 : sizeof(info)));
                        m_size = GetBe64(info);
                        m_flags = GetBe16(info + 8);
                        R_UNLESS(m_size >= BlockSize && (m_size % BlockSize) == 0, usb::ResultOperationDenied());
                        diagnostics::last_stage.store(5);
                        R_SUCCEED();
                    }();
                    if (R_SUCCEEDED(rc)) {
                        diagnostics::connects.fetch_add(1);
                        R_SUCCEED();
                    }
                    this->Close();
                    diagnostics::connect_failures.fetch_add(1);
                    /* A protocol refusal will not fix itself by retrying quickly. */
                    if (usb::ResultOperationDenied::Includes(rc)) R_RETURN(rc);
                } else {
                    diagnostics::last_errno.store(static_cast<u32>(g_bsdErrno));
                    diagnostics::connect_failures.fetch_add(1);
                    ::bsdClose(fd);
                }
            } else {
                diagnostics::last_errno.store(static_cast<u32>(g_bsdErrno));
                diagnostics::connect_failures.fetch_add(1);
            }
            /* Network not up yet (boot, wake, Wi-Fi roaming): retry until the deadline. */
            R_UNLESS(NowNs() + 1'000'000'000ULL < deadline_ns, usb::ResultTransactionError());
            os::SleepThread(TimeSpan::FromSeconds(1));
        }
    }

    Result Connection::Transact(u16 type, u64 offset, u32 length, const void *out_data, void *in_data) {
        R_UNLESS(m_fd >= 0, usb::ResultNotInitialized());
        u8 request[28];
        const u64 cookie = ++m_cookie;
        PutBe32(request, RequestMagic);
        PutBe16(request + 4, 0);
        PutBe16(request + 6, type);
        PutBe64(request + 8, cookie);
        PutBe64(request + 16, offset);
        PutBe32(request + 24, length);
        const auto failed = [&](Result rc) -> Result {
            diagnostics::io_failures.fetch_add(1);
            this->Close();
            return rc;
        };
        Result rc = SendAll(m_fd, request, sizeof(request));
        if (R_SUCCEEDED(rc) && out_data != nullptr) rc = SendAll(m_fd, out_data, length);
        if (R_FAILED(rc)) return failed(rc);
        u8 reply[16];
        rc = RecvAll(m_fd, reply, sizeof(reply));
        if (R_FAILED(rc)) return failed(rc);
        /* Requests are never pipelined, so the reply must be for this cookie. */
        if (GetBe32(reply) != ReplyMagic || GetBe64(reply + 8) != cookie) return failed(usb::ResultTransactionError());
        const u32 error = GetBe32(reply + 4);
        if (error != 0) {
            /* The server reported an I/O error (EIO, EPERM on a read-only export...).
             * The connection stays usable; the data payload is not sent. */
            diagnostics::last_errno.store(error);
            diagnostics::io_failures.fetch_add(1);
            R_THROW(usb::ResultScsiCommandFailed());
        }
        if (in_data != nullptr) {
            rc = RecvAll(m_fd, in_data, length);
            if (R_FAILED(rc)) return failed(rc);
        }
        R_SUCCEED();
    }

    Result Connection::Read(u64 offset, void *buffer, u32 size) {
        R_RETURN(this->Transact(CmdRead, offset, size, nullptr, buffer));
    }

    Result Connection::Write(u64 offset, const void *buffer, u32 size) {
        R_UNLESS(!this->IsReadOnly(), usb::ResultOperationDenied());
        R_RETURN(this->Transact(CmdWrite, offset, size, buffer, nullptr));
    }

    Result Connection::Flush() {
        if (!this->CanFlush()) R_SUCCEED();
        R_RETURN(this->Transact(CmdFlush, 0, 0, nullptr, nullptr));
    }

}

#else

namespace ams::usb::nbd {

    namespace { constinit Config g_config{}; }

    void Configure(const Config &config) { g_config = config; }
    bool IsEnabled() { return false; }
    const Config &GetConfig() { return g_config; }
    bool ParseConfig(Config *, const char *, size_t) { return false; }

}

#endif
