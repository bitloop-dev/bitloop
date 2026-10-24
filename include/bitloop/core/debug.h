#pragma once
#include <cstdio>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <format>
#include <thread>
#include <atomic>
#include <iostream>
#include <concepts>

#include <mutex>
#include <unordered_map>
#include <array>

#include <ostream>
#include <sstream>
#include <type_traits>

#include <cstdarg> // va_list, va_start, va_end, va_copy
#include <cstdio>

#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <source_location>

#if defined (_WIN32)
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #ifdef _MSC_VER
  #include <stacktrace>
  #endif
#elif defined(__EMSCRIPTEN__)
  #include <emscripten/emscripten.h>
#endif

#include <bitloop/platform/platform_macros.h>

/// ======== Config ========
#include "config.h"


/// ========================= ///
/// ======== Private ======== ///

#ifdef NDEBUG // Relax coding style when debugging, force cleanup before release
    #pragma warning(error: 4189) // local variable is initialized but not referenced
    #pragma warning(error: 4100) // unreferenced formal parameter
    #pragma warning(error: 0415) // unreferenced formal parameter
    //#pragma warning(error: 4458) // declaration hides class member (disabled for bl_pull to work)
#endif

#define UNUSED(x) ((void)(x))

/// configure defines based on provided build config
#if defined BL_RELWITHDEBINFO || defined BL_DEBUG
#define BL_DEBUG_INFO
#endif

// don't attempt to simulate devices on web
#if defined __EMSCRIPTEN__
#undef BL_SIMULATED_DEVICE
#endif

// configure web environment (including simulated browser)
#if defined __EMSCRIPTEN__ || defined BL_SIMULATE_BROWSER
#define BL_WEB_BUILD
#undef BITLOOP_FFMPEG_ENABLED
#undef BITLOOP_FFMPEG_X265_ENABLED
#endif

#ifndef BL_WEB_BUILD
// For RelWithDebInfo, to force compiler to avoid optimizing variables
// to make them inspectable while debugging
template<class T>
NO_INLINE void dbg_keep_ref(const T& x)
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(x) : "memory");
#else
    (void)*reinterpret_cast<volatile const unsigned char*>(&x);
#endif
}
#else
#define dbg_keep_ref(x)
#endif

// Disable debug flags for Release builds
#if defined NDEBUG && !defined(BL_FORCE_DEBUG)
#undef BL_DEBUG_FINITE_DOUBLE_CHECKS
#endif

#ifndef BL_BEGIN_NS
#define BL_BEGIN_NS namespace bl {
#endif
#ifndef BL_END_NS
#define BL_END_NS   }
#endif

BL_BEGIN_NS

extern void ImDebugPrint(const char* txt, ...);

// ---------- persistent (per-thread) defaults ----------
struct FloatState {
    std::chars_format fmt;
    int precision;
};

// Persist across blPrint() calls on the same thread
inline thread_local FloatState g_state{
    std::chars_format::general,
    std::numeric_limits<double>::max_digits10
};

// ---------- manipulators ----------
struct Precision { int value; };
inline Precision precision(int p) { return { p }; }

struct FixedPrec { int n; };
inline FixedPrec to_fixed(int n) { return { n }; }
inline FixedPrec dp(int n) { return { n }; }

struct Scientific {};
struct General {};

inline constexpr Scientific scientific{};
inline constexpr General general{};

// Detect if `os << t` is valid
template<class T>
concept OstreamInsertable =
    requires(std::ostream & os, const T & v) {
        { os << v } -> std::same_as<std::ostream&>;
};

// ---------- stream ----------
class DebugStream {
public:
    // variables first
    static constexpr std::size_t kBufSize = 1024;
    char buf[kBufSize];
    std::size_t len;
    std::chars_format floatFmt;
    int prec;

    DebugStream()
        : buf{}, len(0), floatFmt(g_state.fmt), prec(g_state.precision) {
    }

    ~DebugStream() {
        // ensure line-oriented output
        if (len && buf[len - 1] != '\n') { ensure(1); buf[len++] = '\n'; }
        flush();
    }

    // text
    DebugStream& operator<<(const char* s) { if (s) append(std::string_view{ s }); return *this; }
    DebugStream& operator<<(std::string_view sv) { append(sv); return *this; }
    DebugStream& operator<<(const std::string& s) { return (*this) << std::string_view{ s }; }
    DebugStream& operator<<(char c) { ensure(1); buf[len++] = c; return *this; }
    DebugStream& operator<<(bool b) { return (*this) << (b ? "true" : "false"); }

    // integers
    template<class T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    DebugStream& operator<<(T v) {
        ensure(32);
        auto r = std::to_chars(buf + len, buf + kBufSize, v);
        if (r.ec == std::errc{}) len = static_cast<std::size_t>(r.ptr - buf);
        else { flush(); r = std::to_chars(buf, buf + kBufSize, v); if (r.ec == std::errc{}) len = static_cast<std::size_t>(r.ptr - buf); }
        return *this;
    }

    // pointers (hex)
    DebugStream& operator<<(const void* p) {
        (*this) << "0x";
        ensure(2 * sizeof(void*) + 2);
        auto r = std::to_chars(buf + len, buf + kBufSize, reinterpret_cast<uintptr_t>(p), 16);
        if (r.ec == std::errc{}) len = static_cast<std::size_t>(r.ptr - buf);
        else { flush(); r = std::to_chars(buf, buf + kBufSize, reinterpret_cast<uintptr_t>(p), 16); if (r.ec == std::errc{}) len = static_cast<std::size_t>(r.ptr - buf); }
        return *this;
    }

    // floats
    DebugStream& operator<<(float v) { return append_float(static_cast<double>(v)); }
    DebugStream& operator<<(double v) { return append_float(v); }


    // Fallback for anything with an std::ostream << overload
    template<class T> requires OstreamInsertable<T> &&
        (!std::is_integral_v<T> || std::is_same_v<T, bool>) &&
        (!std::is_same_v<std::remove_cvref_t<T>, char>) &&
        (!std::is_convertible_v<T, const char*>) &&
        (!std::is_same_v<std::remove_cvref_t<T>, std::string_view>) &&
        (!std::is_pointer_v<std::remove_reference_t<T>>)
        DebugStream& operator<<(const T& v)
    {
        std::ostringstream oss;
        oss << v;
        const std::string s = oss.str();
        return (*this) << std::string_view{ s };
    }

    void flush() {
        if (!len) return;

        buf[len] = '\0';

        #ifdef WIN32
        OutputDebugStringA(buf);
        #endif
        ImDebugPrint(buf);
        std::cout << buf;

        len = 0;
    }

private:
    void ensure(std::size_t need) {
        if (kBufSize - len < need) flush();
    }

    void append(std::string_view sv) {
        const char* p = sv.data();
        std::size_t n = sv.size();

        while (n) {
            if (kBufSize - len == 0) { flush(); }
            std::size_t take = std::min(n, kBufSize - len);
            std::memcpy(buf + len, p, take);
            len += take;

            p += take;
            n -= take;
        }
    }

    DebugStream& append_float(double v) {
        ensure(64);
        auto r = std::to_chars(buf + len, buf + kBufSize, v, floatFmt, prec);
        if (r.ec == std::errc{}) {
            len = static_cast<std::size_t>(r.ptr - buf);
        }
        else {
            flush();
            r = std::to_chars(buf, buf + kBufSize, v, floatFmt, prec);
            if (r.ec == std::errc{}) len = static_cast<std::size_t>(r.ptr - buf);
        }
        return *this;
    }
};

// ---------- manipulators wired to stream and persistent state ----------
inline DebugStream& operator<<(DebugStream& s, Precision p) {
    s.prec = p.value;
    g_state.precision = p.value; // persist for next streams
    return s;
}

inline DebugStream& operator<<(DebugStream& s, FixedPrec fp) {
    s.floatFmt = std::chars_format::fixed;
    s.prec = fp.n;
    g_state.fmt = std::chars_format::fixed;
    g_state.precision = fp.n;
    return s;
}

inline DebugStream& operator<<(DebugStream& s, Scientific) {
    s.floatFmt = std::chars_format::scientific;
    g_state.fmt = std::chars_format::scientific;
    return s;
}

inline DebugStream& operator<<(DebugStream& s, General) {
    s.floatFmt = std::chars_format::general;
    g_state.fmt = std::chars_format::general;
    return s;
}

BL_END_NS

// factory
[[nodiscard]] inline bl::DebugStream blPrint() { return bl::DebugStream{}; }
inline void blPrint(const char* fmt, ...)
{
    const size_t small_size = 1024;
    char small_buf[small_size]{};

    va_list ap;

    // First: try to format into the small stack buffer
    va_start(ap, fmt);
    int n = std::vsnprintf(small_buf, small_size, fmt, ap);
    va_end(ap);

    if (n >= 0 && static_cast<std::size_t>(n) < small_size) {
        // Fits in the stack buffer
        #ifdef WIN32
        OutputDebugStringA(small_buf);
        #endif
        bl::ImDebugPrint(small_buf);
        std::cout << small_buf;
        return;
    }

    // Second: compute required size exactly (excluding null)
    #if defined(_MSC_VER) && !defined(__clang__)
    va_start(ap, fmt);
    int needed = _vscprintf(fmt, ap);
    va_end(ap);
    #else
    va_start(ap, fmt);
    int needed = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    #endif

    if (needed < 0) {
        // Formatting error: bail
        return;
    }

    // Allocate once and format
    std::string buf;
    buf.resize(static_cast<std::size_t>(needed) + 1);

    va_start(ap, fmt);
    std::vsnprintf((char*)buf.data(), buf.size(), fmt, ap);
    va_end(ap);

    #ifdef WIN32
    OutputDebugStringA(buf.c_str());
    #endif
    bl::ImDebugPrint(buf.c_str());
    std::cout << buf.c_str();
}

#ifdef TIMERS_ENABLED
#define timer0(name)      auto _timer_##name = std::chrono::steady_clock::now();
#define timer1(name, ...) auto waited_##name = std::chrono::steady_clock::now() - _timer_##name;\
                          double dt_##name = std::chrono::duration<double, std::milli>(waited_##name).count();\
                          if (dt_##name >= TIMER_ELAPSED_LIMIT) { blPrint("Timer (%s): %.4f\n", #name, dt_##name); }
#else
#define timer0(name)     
#define timer1(name, ...)
#endif

class FiniteDouble
{
    void set(const FiniteDouble& v)
    {
        //break_on_assignment = v.break_on_assignment;
        set(v.value);
    }
    void set(double v) 
    {
        if (v == value)
            return;

        if (break_on_assignment)
        {
            #ifdef _MSC_VER
            std::string stack = std::to_string(std::stacktrace::current());
            #endif
            blBreak();
            if (break_on_assignment == 1)
                break_on_assignment = 0;
        }

        if (!std::isfinite(v))
        {
            #ifdef _MSC_VER
            std::string stack = std::to_string(std::stacktrace::current());
            #endif
            blBreak();
        }
        
        value = v;
    }

    double value = 0.0;

public:

    FiniteDouble() = default;
    FiniteDouble(const FiniteDouble& v, bool _break_on_assignment = false)
    {
        break_on_assignment = _break_on_assignment;
        set(v);
    }
    FiniteDouble(double v, bool _break_on_assignment=false)
    {
        break_on_assignment = _break_on_assignment;
        set(v);
    }
    FiniteDouble& operator=(const FiniteDouble v)
    {
        set(v);
        return *this;
    }
    FiniteDouble& operator=(double v) 
    {
        set(v);
        return *this;
    }
    operator double() const
    {
        return value;
    }
    double* operator&()
    {
        return &value;
    }
    const double* operator&() const
    {
        return &value;
    }

    bool operator==(double other) const { return value == other; }
    bool operator!=(double other) const { return value != other; }
    bool operator<(double other)  const { return value < other; }
    bool operator<=(double other) const { return value <= other; }
    bool operator>(double other)  const { return value > other; }
    bool operator>=(double other) const { return value >= other; }
    bool operator==(const FiniteDouble& other) const { return value == other.value; }
    bool operator!=(const FiniteDouble& other) const { return value != other.value; }

    FiniteDouble operator+(double other) const { return FiniteDouble(value + other); }
    FiniteDouble operator-(double other) const { return FiniteDouble(value - other); }
    FiniteDouble operator*(double other) const { return FiniteDouble(value * other); }
    FiniteDouble operator/(double other) const { return FiniteDouble(value / other); }
    FiniteDouble& operator+=(double other) { set(value + other); return *this;  }
    FiniteDouble& operator-=(double other) { set(value - other); return *this;  }
    FiniteDouble& operator*=(double other) { set(value * other); return *this;  }
    FiniteDouble& operator/=(double other) { set(value / other); return *this;  }

    friend std::ostream& operator<<(std::ostream& os, const FiniteDouble& fd);

    int break_on_assignment = 0;
    void breakOnAssignment()
    {
        break_on_assignment = 1;
    }
    void breakOnEveryAssignment()
    {
        break_on_assignment = 2;
    }
};

namespace std {
    template <>
        struct hash<FiniteDouble> {
            size_t operator()(const FiniteDouble& fd) const noexcept {
                return hash<double>()(static_cast<double>(fd));
        }
    };
}

/// --- Debug & Release ---

#if defined NDEBUG

/// --- Release Mode ---
#if defined BL_FORCE_RELEASE_FINITE_DOUBLE_CHECKS
#define finite_double FiniteDouble
#else
#define finite_double double
#endif

#else

/// --- Debug Mode ---
#if defined BL_DEBUG_FINITE_DOUBLE_CHECKS
#define finite_double FiniteDouble
#else
#define finite_double double
#endif

#endif

#include <bitloop/util/debug_ownership.h>
