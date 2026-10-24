#pragma once

#include <bitloop/platform/platform_macros.h>

#include <bit>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

BL_BEGIN_NS;

using hash_t = std::uint64_t;

struct StableHasher
{
private:

    template<class>
    static inline constexpr bool dependent_false_v = false;

    hash_t seed = 0;
    hash_t count = 0;

public:

    static constexpr hash_t mix64(hash_t x) noexcept {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebull;
        x ^= x >> 31;
        return x;
    }

    static constexpr void hash_combine(hash_t& s, hash_t v) noexcept {
        s = mix64(s ^ mix64(v + 0x9e3779b97f4a7c15ull));
    }

    static inline constexpr hash_t kFnvOffset = 14695981039346656037ull;
    static inline constexpr hash_t kFnvPrime = 1099511628211ull;

    static constexpr hash_t hash_bytes_u8(const std::uint8_t* p, std::size_t n) noexcept {
        hash_t h = kFnvOffset;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= static_cast<hash_t>(p[i]);
            h *= kFnvPrime;
        }
        return mix64(h ^ static_cast<hash_t>(n));
    }

    static constexpr hash_t hash_bytes(const void* data, std::size_t n) noexcept {
        auto p = static_cast<const std::uint8_t*>(data);
        return hash_bytes_u8(p, n);
    }

    // ---- string hashing ----

    static constexpr hash_t hash_string(std::string_view s) noexcept {
        hash_t h = kFnvOffset;
        for (std::size_t i = 0; i < s.size(); ++i) {
            h ^= static_cast<hash_t>(static_cast<unsigned char>(s[i]));
            h *= kFnvPrime;
        }
        return mix64(h ^ static_cast<hash_t>(s.size()));
    }

    template <std::size_t N>
    static constexpr hash_t hash_string(const char(&s)[N]) noexcept {
        const std::size_t n = (N > 0) ? (N - 1) : 0;

        hash_t h = kFnvOffset;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= static_cast<hash_t>(static_cast<unsigned char>(s[i]));
            h *= kFnvPrime;
        }
        return mix64(h ^ static_cast<hash_t>(n));
    }

    // Guaranteed compile-time (forces constant evaluation).
    template <std::size_t N>
    static consteval hash_t hash_lit(const char(&s)[N]) noexcept {
        return hash_string(s);
    }

    template <class T>
    static inline hash_t hash_one(const T& v) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            using I = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
            I bits = std::bit_cast<I>(v);

            if constexpr (sizeof(T) == 4) {
                if ((bits & 0x7fffffffu) == 0) bits = 0;
                if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu))
                    bits = 0x7fc00000u;
            }
            else {
                if ((bits & 0x7fffffffffffffffull) == 0) bits = 0;
                if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (bits & 0x000fffffffffffffull))
                    bits = 0x7ff8000000000000ull;
            }

            return mix64(static_cast<hash_t>(bits));
        }
        else if constexpr (std::is_enum_v<T>) {
            using U = std::underlying_type_t<T>;
            return hash_one(static_cast<U>(v));
        }
        else if constexpr (std::is_integral_v<T>) {
            return mix64(static_cast<hash_t>(v));
        }
        else if constexpr (std::is_pointer_v<T>) {
            static_assert(dependent_false_v<T>,
                "StableHasher cannot hash pointer addresses because they are not stable across processes.");
        }
        else {
            static_assert(dependent_false_v<T>,
                "StableHasher::hash_one(T): Hash the type's logical fields explicitly; raw object representations are not portable.");
        }
    }

    // ---- streaming ----

    template <class T>
    StableHasher& add(const T& v) noexcept {
        hash_combine(seed, hash_one(v));
        ++count;
        return *this;
    }

    StableHasher& add_bytes(const void* data, std::size_t n) noexcept {
        hash_combine(seed, hash_bytes(data, n));
        ++count;
        return *this;
    }

    StableHasher& add_string(std::string_view s) noexcept {
        hash_combine(seed, hash_string(s));
        ++count;
        return *this;
    }

    template <class It>
    StableHasher& add_range(It first, It last) noexcept {
        for (; first != last; ++first) add(*first);
        return *this;
    }

    template <class T>
    StableHasher& add_span(std::span<const T> s) noexcept {
        return add_range(s.begin(), s.end());
    }

    hash_t finish() const noexcept {
        hash_t out = seed;
        hash_combine(out, count);
        return out;
    }

    static inline hash_t of() noexcept { return StableHasher{}.finish(); }

    template <class... Ts>
    static inline hash_t of(const Ts&... xs) noexcept {
        StableHasher h;
        (h.add(xs), ...);
        return h.finish();
    }
};

struct Hashable
{
    using hash_t = bl::hash_t;

    virtual ~Hashable() = default;

    // The public hash contract is always the full stable 64-bit value, even on
    // platforms where size_t is 32-bit. Native-size folding belongs in an STL
    // hash adapter, never in change detection or persistence code.
    hash_t hash() const noexcept {
        if (!cache_valid) {
            cache = compute_hash();
            cache_valid = true;
        }
        return cache;
    }

    // Native-width result for std::hash adapters and STL containers only.
    // Folding is safe there because unordered containers resolve collisions
    // with their equality predicate.
    std::size_t stl_hash() const noexcept {
        const hash_t h = hash();
        if constexpr (sizeof(std::size_t) >= sizeof(hash_t))
            return static_cast<std::size_t>(h);
        else
            return static_cast<std::size_t>(h ^ (h >> 32));
    }

    void invalidate_hash() const noexcept { cache_valid = false; }

protected:
    virtual hash_t compute_hash() const noexcept = 0;

    // expose StableHasher as a helper for derived types
    using Hasher = StableHasher;

private:
    mutable hash_t cache{ 0 };
    mutable bool cache_valid{ false };
};

consteval hash_t operator""_h(const char* s, size_t n) noexcept
{
    return StableHasher::hash_string(s);
}

BL_END_NS;
