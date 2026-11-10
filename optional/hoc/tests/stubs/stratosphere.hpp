// Minimal deterministic host fixture for compiling the unmodified client TU.
// This does not implement Horizon IPC or cryptographic SHA-256.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
using u8 = std::uint8_t; using u32 = std::uint32_t; using u64 = std::uint64_t;
using Handle = int;
inline constexpr Handle INVALID_HANDLE = 0, CUR_PROCESS_HANDLE = -1;
struct Result {
    unsigned value = 0;
    constexpr Result(unsigned v = 0) : value(v) {}
    constexpr unsigned GetValue() const { return value; }
    friend constexpr bool operator==(Result, Result) = default;
};
inline constexpr unsigned SfBufferAttr_In = 1, SfBufferAttr_Out = 2, SfBufferAttr_HipcMapAlias = 4;
inline constexpr unsigned SfOutHandleAttr_HipcCopy = 1;
struct Service { Handle session = 0; };
struct DispatchOptions {
    unsigned buffer_attrs[2]{};
    struct Buffer { void *pointer{}; std::size_t size{}; } buffers[2]{};
    bool in_send_pid{};
    unsigned in_num_handles{};
    Handle in_handles[1]{};
    unsigned out_handle_attrs[1]{};
    Handle *out_handles{};
};
namespace fixture {
inline long long tick_ms;
inline unsigned connect_calls, close_calls, service_closes, dispatch_calls;
inline Handle last_closed_handle;
inline Result connect_error{};
inline unsigned unavailable_attempts;
inline std::uint32_t firmware = 0x17000000;
inline std::uint64_t helper_id = 0x4200000000484F43ULL;
struct ScopeExit { std::function<void()> f; ~ScopeExit() { f(); } };
struct ScopeExitBuilder {
    template<class F> ScopeExit operator+(F &&f) { return {std::forward<F>(f)}; }
};
template<class Input, class Output>
Result Dispatch(Service *, unsigned, const Input &, Output &, const DispatchOptions &);
}
inline bool serviceIsActive(const Service *s) { return s->session != 0; }
inline void serviceClose(Service *s) { ++fixture::service_closes; s->session = 0; }
inline void serviceCreate(Service *s, Handle h) { s->session = h; }
#define R_SUCCEEDED(r) ((r).value == 0)
#define R_FAILED(r) ((r).value != 0)
#define R_RETURN(r) return (r)
#define R_SUCCEED() return Result{}
#define R_UNLESS(condition, r) do { if (!(condition)) return (r); } while (false)
#define R_TRY(expression) do { const Result local_result = (expression); if (R_FAILED(local_result)) return local_result; } while (false)
#define R_DISCARD(expression) static_cast<void>(expression)
#define FIXTURE_CONCAT_INNER(a,b) a##b
#define FIXTURE_CONCAT(a,b) FIXTURE_CONCAT_INNER(a,b)
#define ON_SCOPE_EXIT auto FIXTURE_CONCAT(scope_exit_, __LINE__) = fixture::ScopeExitBuilder{} + [&]()
#define serviceDispatchInOut(service, command, input, output, ...) fixture::Dispatch(service, command, input, output, DispatchOptions{__VA_ARGS__})
namespace ams {
using ::Result;
struct TimeSpan {
    long long ms;
    static TimeSpan FromMilliSeconds(long long n) { return {n}; }
    long long GetMilliSeconds() const { return ms; }
};
namespace os {
using NativeHandle = Handle;
inline constexpr NativeHandle InvalidNativeHandle = INVALID_HANDLE;
struct Tick {
    long long value;
    TimeSpan ToTimeSpan() const { return {value}; }
    friend Tick operator-(Tick a, Tick b) { return {a.value - b.value}; }
};
inline Tick GetSystemTick() { return {fixture::tick_ms}; }
inline void SleepThread(TimeSpan duration) { fixture::tick_ms += duration.ms; }
}
namespace hos { inline std::uint32_t GetVersion() { return fixture::firmware; } }
namespace svc {
inline constexpr unsigned InfoType_ProgramId = 1;
struct ResultNotFound : Result {
    constexpr ResultNotFound() : Result(11) {}
    static constexpr bool Includes(Result result) { return result.value == 11; }
};
inline Result ResultTimedOut() { return {12}; }
inline Result ConnectToNamedPort(Handle *out, const char *) {
    ++fixture::connect_calls;
    if (fixture::unavailable_attempts) { --fixture::unavailable_attempts; return ResultNotFound(); }
    if (R_FAILED(fixture::connect_error)) return fixture::connect_error;
    *out = 91; return {};
}
inline Result CloseHandle(Handle h) { ++fixture::close_calls; fixture::last_closed_handle = h; return {}; }
inline Result GetInfo(u64 *out, unsigned, Handle, unsigned) { *out = fixture::helper_id; return {}; }
}
namespace ldr {
inline Result ResultInternalError() { return {21}; }
inline Result ResultInvalidNso() { return {22}; }
}
namespace crypto {
// A deterministic digest fixture verifies that the production client compares
// all returned bytes and hashes the destination. SHA correctness is not claimed.
inline void GenerateSha256(void *out, std::size_t out_size, const void *data, std::size_t size) {
    auto *bytes = static_cast<u8 *>(out); const auto *input = static_cast<const u8 *>(data);
    u64 state = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) state = (state ^ input[i]) * 1099511628211ULL;
    for (std::size_t i = 0; i < out_size; ++i) { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; bytes[i] = static_cast<u8>(state); }
}
inline void GenerateSha256Hash(void *out, std::size_t n, const void *p, std::size_t s) { GenerateSha256(out, n, p, s); }
}
}
