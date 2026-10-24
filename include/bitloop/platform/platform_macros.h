#pragma once

#ifndef BL_BEGIN_NS
#define BL_BEGIN_NS namespace bl {
#endif
#ifndef BL_END_NS
#define BL_END_NS   }
#endif

/// ======== compiler message ========

#define BL_STR1(x) #x
#define BL_STR(x)  BL_STR1(x)

// ------------------ Pragmas ------------------
#if defined(_MSC_VER)
#define BL_MESSAGE(msg) __pragma(message(msg))
#define BL_SHOW(name)   __pragma(message(#name "=" BL_STR(name)))
#else
  // Clang/GCC/Emscripten
#define BL_DO_PRAGMA(x) _Pragma(BL_STR(x))
#define BL_MESSAGE(msg) BL_DO_PRAGMA(message msg)
// shows: name=VALUE for a macro VALUE
#define BL_SHOW(name)   BL_DO_PRAGMA(message BL_NAME(name) "=" BL_STR(name))
#define BL_NAME(x)      #x
#endif

/// ======== FORCE_INLINE ========

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif

#ifndef NO_INLINE
#if defined(_MSC_VER)
#define NO_INLINE [[msvc::noinline]]
#elif defined(__clang__)
#define NO_INLINE [[clang::noinline]]
#elif defined(__GNUC__)
#define NO_INLINE __attribute__((noinline))
#else
#define NO_INLINE
#endif
#endif

/// ======== Fast-math ========

#if defined(__FAST_MATH__) // GCC/Clang: -ffast-math
#define BL_FAST_MATH
#elif defined(_MSC_VER) && defined(_M_FP_FAST)  // MSVC: /fp:fast
#define BL_FAST_MATH
#endif

#if defined(_MSC_VER)
#define BL_PUSH_PRECISE  __pragma(float_control(precise, on, push)) \
                         __pragma(fp_contract(off))
#define BL_POP_PRECISE   __pragma(float_control(pop))


#elif defined(__EMSCRIPTEN__)
#define BL_PUSH_PRECISE  _Pragma("clang fp reassociate(off)") \
                            _Pragma("clang fp contract(off)")
#define BL_POP_PRECISE   _Pragma("clang fp reassociate(on)")  \
                            _Pragma("clang fp contract(fast)")

// Other Clang (desktop) that supports the richer set
#elif defined(__clang__)
#define BL_PUSH_PRECISE  _Pragma("clang fp reassociate(off)") \
                            _Pragma("clang fp contract(off)")
#define BL_POP_PRECISE   _Pragma("clang fp reassociate(on)")  \
                            _Pragma("clang fp contract(fast)")

// GCC fallback
#elif defined(__GNUC__)
#define BL_PUSH_PRECISE  _Pragma("GCC push_options")                \
                            _Pragma("GCC optimize(\"no-fast-math\")") \
                            _Pragma("STDC FP_CONTRACT OFF")
#define BL_POP_PRECISE   _Pragma("GCC pop_options")

#else
#define BL_PUSH_PRECISE
#define BL_POP_PRECISE
#endif

// Returns true if the compiler enabled a "fast math" mode for this TU
constexpr bool fast_math_enabled()
{
    #ifdef BL_FAST_MATH
    return true;
    #else
    return false;
    #endif
}

/// ======== blBreak ========

#if defined(_MSC_VER)
    #define blBreak() __debugbreak()
#elif defined(__clang__)
    #define blBreak() __builtin_debugtrap()
#elif defined(__GNUC__)
    #define blBreak() __builtin_trap()
#else
    #include <signal.h>
    #define blBreak() raise(SIGTRAP)
#endif

/// ======== for-each macro ========

// --- utils ---
#define BL_CAT(a,b) BL_CAT_I(a,b)
#define BL_CAT_I(a,b) a##b

// --- count args ---
#define BL_ARG_N( \
     _1,_2,_3,_4,_5,_6,_7,_8,_9,_10, \
     _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
     _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
     _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
     _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
     _51,_52,_53,_54,_55,_56,_57,_58,_59, N, ...) N

#define BL_RSEQ_N() \
     59,58,57,56,55,54,53,52,51,50, \
     49,48,47,46,45,44,43,42,41,40, \
     39,38,37,36,35,34,33,32,31,30, \
     29,28,27,26,25,24,23,22,21,20, \
     19,18,17,16,15,14,13,12,11,10, \
     9,8,7,6,5,4,3,2,1,0

#define BL_EXPAND(...) __VA_ARGS__
#define BL_NARG_(...)   BL_EXPAND(BL_ARG_N(__VA_ARGS__))
#define BL_NARG(...)    BL_NARG_(__VA_ARGS__, BL_RSEQ_N())

// --- foreach unrollers ---
#define BL_FOREACH_0(M, ...)
#define BL_FOREACH_1(M,a1) \
    M(a1);
#define BL_FOREACH_2(M,a1,a2) \
    M(a1); M(a2);
#define BL_FOREACH_3(M,a1,a2,a3) \
    M(a1); M(a2); M(a3);
#define BL_FOREACH_4(M,a1,a2,a3,a4) \
    M(a1); M(a2); M(a3); M(a4);
#define BL_FOREACH_5(M,a1,a2,a3,a4,a5) \
    M(a1); M(a2); M(a3); M(a4); M(a5);
#define BL_FOREACH_6(M,a1,a2,a3,a4,a5,a6) \
    M(a1); M(a2); M(a3); M(a4); M(a5); M(a6);
#define BL_FOREACH_7(M,a1,a2,a3,a4,a5,a6,a7) \
    M(a1); M(a2); M(a3); M(a4); M(a5); M(a6); M(a7);
#define BL_FOREACH_8(M,a1,a2,a3,a4,a5,a6,a7,a8) \
    M(a1); M(a2); M(a3); M(a4); M(a5); M(a6); M(a7); M(a8);
#define BL_FOREACH_9(M,a1,a2,a3,a4,a5,a6,a7,a8,a9) \
    M(a1); M(a2); M(a3); M(a4); M(a5); M(a6); M(a7); M(a8); M(a9);
#define BL_FOREACH_10(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) \
    M(a1); M(a2); M(a3); M(a4); M(a5); M(a6); M(a7); M(a8); M(a9); M(a10);

#define BL_FOREACH_11(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) \
    BL_FOREACH_10(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) M(a11);
#define BL_FOREACH_12(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12) \
    BL_FOREACH_11(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) M(a12);
#define BL_FOREACH_13(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13) \
    BL_FOREACH_12(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12) M(a13);
#define BL_FOREACH_14(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14) \
    BL_FOREACH_13(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13) M(a14);
#define BL_FOREACH_15(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15) \
    BL_FOREACH_14(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14) M(a15);
#define BL_FOREACH_16(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16) \
    BL_FOREACH_15(M,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15) M(a16);

#define BL_FOREACH(M, ...)       BL_FOREACH_I(M, __VA_ARGS__)
#define BL_FOREACH_I(M, ...)     BL_FOREACH_II(M, BL_NARG(__VA_ARGS__), __VA_ARGS__)
#define BL_FOREACH_II(M, N, ...) BL_EXPAND( BL_CAT(BL_FOREACH_, N)(M, __VA_ARGS__) )
