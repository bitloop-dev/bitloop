#pragma once
#define BL_BEGIN_NS namespace bl {
#define BL_END_NS   }

// dependencies
// - math_util

#include <cmath>
#include <limits>
#include <type_traits>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <fltx.h>

using namespace bl::literals;
using namespace bl::types;

// Forward declare math helpers for access

BL_BEGIN_NS

namespace math {
    template<typename T>
    T avgAngle(T a, T b);
}

/// ================= Enums =================

enum struct Anchor
{
    TOP_LEFT,     TOP,     TOP_RIGHT,
    LEFT,         CENTER,  RIGHT,
    BOTTOM_LEFT,  BOTTOM,  BOTTOM_RIGHT
};


// ----- Vec2 -----

template<typename T>
struct Vec2
{
    storage_t<T> x, y;

    // constructors
    Vec2() = default;
    constexpr Vec2(T v) : x{ v }, y{ v } {}
    constexpr Vec2(T _x, T _y) : x{ _x }, y{ _y } {}

    // convenience
    constexpr void set(T _x, T _y) { x = _x; y = _y; }
    constexpr void set(const Vec2<T>& rhs) { x = rhs.x; y = rhs.y; }
    [[nodiscard]] constexpr bool eq(T _x, T _y) const { return (x == _x && y == _y); }

    // conversions
    template<typename T2> [[nodiscard]] constexpr operator Vec2<T2>()    const { return Vec2<T2>{(T2)x, (T2)y}; }

    // access
    [[nodiscard]] constexpr const storage_t<T>& operator[](int i) const { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>& operator[](int i) { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>* data() { return &x; }

    // operator
    [[nodiscard]] constexpr Vec2 operator-() const { return Vec2(-x, -y); }
    [[nodiscard]] constexpr bool operator==(const Vec2& rhs) const { return x == rhs.x && y == rhs.y; }
    [[nodiscard]] constexpr bool operator!=(const Vec2& rhs) const { return x != rhs.x || y != rhs.y; }
    [[nodiscard]] constexpr Vec2 operator+(const Vec2& rhs) const { return { x + rhs.x, y + rhs.y }; }
    [[nodiscard]] constexpr Vec2 operator-(const Vec2& rhs) const { return { x - rhs.x, y - rhs.y }; }
    [[nodiscard]] constexpr Vec2 operator*(const Vec2& rhs) const { return { x * rhs.x, y * rhs.y }; }
    [[nodiscard]] constexpr Vec2 operator/(const Vec2& rhs) const { return { x / rhs.x, y / rhs.y }; }

    template<typename U> requires requires(T a, U b) { a + b; } constexpr void operator+=(Vec2<U> rhs) { x = x + rhs.x; y = y + rhs.y; }
    template<typename U> requires requires(T a, U b) { a - b; } constexpr void operator-=(Vec2<U> rhs) { x = x - rhs.x; y = y - rhs.y; }
    template<typename U> requires requires(T a, U b) { a * b; } constexpr void operator*=(Vec2<U> rhs) { x = x * rhs.x; y = y * rhs.y; }
    template<typename U> requires requires(T a, U b) { a / b; } constexpr void operator/=(Vec2<U> rhs) { x = x / rhs.x; y = y / rhs.y; }

    template<typename U> requires requires(T a, U b) { a + b; } constexpr void operator+=(U v) { x = x + v; y = y + v; }
    template<typename U> requires requires(T a, U b) { a - b; } constexpr void operator-=(U v) { x = x - v; y = y - v; }
    template<typename U> requires requires(T a, U b) { a * b; } constexpr void operator*=(U v) { x = x * v; y = y * v; }
    template<typename U> requires requires(T a, U b) { a / b; } constexpr void operator/=(U v) { x = x / v; y = y / v; }

    // properties
    [[nodiscard]] constexpr T angle() const { return atan2(y, x); }
    [[nodiscard]] constexpr T average() const { return (x + y) / T{ 2 }; }
    [[nodiscard]] constexpr T mag() const { return sqrt(x * x + y * y); }
    [[nodiscard]] constexpr T mag2() const { return x * x + y * y; }
    [[nodiscard]] constexpr T dot(const Vec2& other) const { return x * other.x + y * other.y; }
    [[nodiscard]] constexpr T angleTo(const Vec2& b) const { return atan2(b.y - y, b.x - x); }
    [[nodiscard]] constexpr bool isZero() const { return x == (T)0 && y == (T)0; }
    [[nodiscard]] constexpr bool isFinite() const { return std::isfinite(x) && std::isfinite(y); }

    // operators
    [[nodiscard]] constexpr Vec2 snapped(T step) const { return { round(x/step)*step, round(y/step)*step }; }
    [[nodiscard]] constexpr Vec2 floored() const { return { floor(x), floor(y) }; }
    [[nodiscard]] constexpr Vec2 rounded() const { return { round(x), round(y) }; }
    [[nodiscard]] constexpr Vec2 floored(f64 offset) const { return { floor(x) + offset, floor(y) + offset }; }
    [[nodiscard]] constexpr Vec2 rounded(f64 offset) const { return { round(x) + offset, round(y) + offset }; }
    [[nodiscard]] constexpr Vec2 normalized() const { return (*this) / mag(); }

    // static
    [[nodiscard]] static constexpr Vec2 lerp(const Vec2& a, const Vec2& b, T ratio) { return a + (b - a) * ratio; }
    [[nodiscard]] static constexpr Vec2 lowest() { return Vec2{ std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest()}; }
    [[nodiscard]] static constexpr Vec2 highest() { return Vec2{ std::numeric_limits<T>::max(), std::numeric_limits<T>::max() }; }

    friend std::ostream& operator<<(std::ostream& os, const Vec2& v) {
        return os << "(x: " << v.x << ", y: " << v.y << ")";
    }
};

// ----- Vec3 -----

template<typename T>
struct Vec3
{
    storage_t<T> x, y, z;

    // constructors
    Vec3() = default;
    constexpr Vec3(T _x, T _y, T _z) : x(_x), y(_y), z(_z) {}

    // conversions
    template<typename T2> constexpr operator Vec3<T2>()    const { return Vec3<T2>{(T2)x, (T2)y, (T2)z}; }

    // access
    [[nodiscard]] constexpr const storage_t<T>& operator[](int i) const { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>& operator[](int i) { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>* data() { return &x; }

    // arithmetic
    [[nodiscard]] constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }
    [[nodiscard]] constexpr bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z != other.z; }
    [[nodiscard]] constexpr bool operator!=(const Vec3& other) const { return x != other.x || y != other.y || z != other.z; }
    [[nodiscard]] constexpr Vec3 operator+(const Vec3& rhs) const { return { x + rhs.x, y + rhs.y, z + rhs.z }; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& rhs) const { return { x - rhs.x, y - rhs.y, z - rhs.z }; }
    [[nodiscard]] constexpr Vec3 operator*(const Vec3& rhs) const { return { x * rhs.x, y * rhs.y, z * rhs.z }; }
    [[nodiscard]] constexpr Vec3 operator/(const Vec3& rhs) const { return { x / rhs.x, y / rhs.y, z / rhs.z }; }
    [[nodiscard]] constexpr Vec3 operator+(T v) const { return { x + v, y + v, z + v }; }
    [[nodiscard]] constexpr Vec3 operator-(T v) const { return { x - v, y - v, z - v }; }
    [[nodiscard]] constexpr Vec3 operator*(T v) const { return { x * v, y * v, z * v }; }
    [[nodiscard]] constexpr Vec3 operator/(T v) const { return { x / v, y / v, z / v }; }

    constexpr void operator+=(Vec3 rhs) { x += rhs.x; y += rhs.y; z += rhs.z; }
    constexpr void operator-=(Vec3 rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; }
    constexpr void operator*=(Vec3 rhs) { x *= rhs.x; y *= rhs.y; z *= rhs.z; }
    constexpr void operator/=(Vec3 rhs) { x /= rhs.x; y /= rhs.y; z /= rhs.z; }
    constexpr void operator+=(T v) { x += v; y += v; z += v; }
    constexpr void operator-=(T v) { x -= v; y -= v; z -= v; }
    constexpr void operator*=(T v) { x *= v; y *= v; z *= v; }
    constexpr void operator/=(T v) { x /= v; y /= v; z /= v; }

    // properties
    [[nodiscard]] constexpr T average() const { return (x + y + z) / T{ 3 }; }
    [[nodiscard]] constexpr T mag() const { return sqrt(x * x + y * y + z * z); }
    [[nodiscard]] constexpr T dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

    // operators
    [[nodiscard]] constexpr Vec3 floored() const { return { floor(x), floor(y), floor(z)}; }
    [[nodiscard]] constexpr Vec3 rounded() const { return { round(x), round(y), round(z)}; }
    [[nodiscard]] constexpr Vec3 floored(f64 offset) const { return { floor(x) + offset, floor(y) + offset, floor(z) + offset}; }
    [[nodiscard]] constexpr Vec3 rounded(f64 offset) const { return { round(x) + offset, round(y) + offset, round(z) + offset }; }
    [[nodiscard]] constexpr Vec3 normalized() const { return (*this) / mag(); }

    // static
    [[nodiscard]] static constexpr Vec3 lerp(const Vec3& a, const Vec3& b, T ratio) { return a + (b - a) * ratio; }
};

// ----- Vec4 -----

template<typename T>
struct Vec4
{
    storage_t<T> x, y, z, w;

    // constructors
    Vec4() = default;
    constexpr Vec4(T _x, T _y, T _z, T _w) : x(_x), y(_y), z(_z), w(_w) {}

    // conversions
    template<typename T2> constexpr operator Vec4<T2>()    const { return Vec4<T2>{(T2)x, (T2)y, (T2)z, (T2)w}; }

    // access
    [[nodiscard]] constexpr const storage_t<T>& operator[](int i) const { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>& operator[](int i) { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>* data() { return &x; }

    // arithmetic
    [[nodiscard]] constexpr Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }
    [[nodiscard]] constexpr bool operator==(const Vec4& other) const { return x == other.x && y == other.y && z != other.z && w != other.w; }
    [[nodiscard]] constexpr bool operator!=(const Vec4& other) const { return x != other.x || y != other.y || z != other.z || w != other.w; }
    [[nodiscard]] constexpr Vec4 operator+(const Vec4& rhs) const { return { x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w }; }
    [[nodiscard]] constexpr Vec4 operator-(const Vec4& rhs) const { return { x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w }; }
    [[nodiscard]] constexpr Vec4 operator*(const Vec4& rhs) const { return { x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w }; }
    [[nodiscard]] constexpr Vec4 operator/(const Vec4& rhs) const { return { x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w }; }
    [[nodiscard]] constexpr Vec4 operator+(T v) const { return { x + v, y + v, z + v, w + v }; }
    [[nodiscard]] constexpr Vec4 operator-(T v) const { return { x - v, y - v, z - v, w - v }; }
    [[nodiscard]] constexpr Vec4 operator*(T v) const { return { x * v, y * v, z * v, w * v }; }
    [[nodiscard]] constexpr Vec4 operator/(T v) const { return { x / v, y / v, z / v, w / v }; }

    constexpr void operator+=(Vec4 rhs) { x += rhs.x; y += rhs.y; z += rhs.z; w += rhs.w; }
    constexpr void operator-=(Vec4 rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; w -= rhs.w; }
    constexpr void operator*=(Vec4 rhs) { x *= rhs.x; y *= rhs.y; z *= rhs.z; w *= rhs.w; }
    constexpr void operator/=(Vec4 rhs) { x /= rhs.x; y /= rhs.y; z /= rhs.z; w /= rhs.w; }
    constexpr void operator+=(T v) { x += v; y += v; z += v; w += v; }
    constexpr void operator-=(T v) { x -= v; y -= v; z -= v; w -= v; }
    constexpr void operator*=(T v) { x *= v; y *= v; z *= v; w *= v; }
    constexpr void operator/=(T v) { x /= v; y /= v; z /= v; w /= v; }

    // properties
    [[nodiscard]] constexpr T average() const { return (x + y + z + w) / T{ 4 }; }
    [[nodiscard]] constexpr T mag() const { return sqrt(x * x + y * y + z * z + w * w); }
    [[nodiscard]] constexpr T dot(const Vec4& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }

    // operators
    [[nodiscard]] constexpr Vec4 floored() const { return { floor(x), floor(y), floor(z), floor(w) }; }
    [[nodiscard]] constexpr Vec4 rounded() const { return { round(x), round(y), round(z), round(w) }; }
    [[nodiscard]] constexpr Vec4 floored(f64 offset) const { return { floor(x) + offset, floor(y) + offset, floor(z) + offset, floor(w) + offset }; }
    [[nodiscard]] constexpr Vec4 rounded(f64 offset) const { return { round(x) + offset, round(y) + offset, round(z) + offset, round(w) + offset }; }
    [[nodiscard]] constexpr Vec4 normalized() const { return (*this) / mag(); }

    // static
    [[nodiscard]] static constexpr Vec4 lerp(const Vec4& a, const Vec4& b, T ratio) { return a + (b - a) * ratio; }
};

// ----- Complex -----

template<typename T>
struct Complex
{
    storage_t<T> x, y; // x = real, y = imag

    // constructors
    Complex() = default;
    constexpr Complex(T v) : x{ v }, y{ v } {}
    constexpr Complex(T _x, T _y) : x{ _x }, y{ _y } {}
    constexpr Complex(const Vec2<T>& rhs) : x{ rhs.x }, y{ rhs.y } {}

    // convenience
    constexpr void set(T _x, T _y) { x = _x; y = _y; }
    constexpr void set(const Complex<T>& rhs) { x = rhs.x; y = rhs.y; }
    [[nodiscard]] constexpr bool eq(T _x, T _y) const { return (x == _x && y == _y); }

    // conversions
    template<typename T2> [[nodiscard]] constexpr explicit operator Vec2<T2>()  const { return Vec2<T2>(x, y); }
    template<typename T2> [[nodiscard]] constexpr operator Complex<T2>()        const { return Complex<T2>{ (T2)x, (T2)y }; }

    // access
    [[nodiscard]] constexpr const storage_t<T>& operator[](int i) const { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>& operator[](int i) { return (&x)[i]; }
    [[nodiscard]] constexpr storage_t<T>* data() { return &x; }

    // operator (complex arithmetic)
    [[nodiscard]] constexpr Complex operator-() const { return Complex(-x, -y); }
    [[nodiscard]] constexpr bool operator==(const Complex& rhs) const { return x == rhs.x && y == rhs.y; }
    [[nodiscard]] constexpr bool operator!=(const Complex& rhs) const { return x != rhs.x || y != rhs.y; }

    [[nodiscard]] constexpr Complex operator+(const Complex& rhs) const { return { x + rhs.x, y + rhs.y }; }
    [[nodiscard]] constexpr Complex operator-(const Complex& rhs) const { return { x - rhs.x, y - rhs.y }; }

    // (a+bi)(c+di) = (ac-bd) + (ad+bc)i
    [[nodiscard]] constexpr Complex operator*(const Complex& rhs) const {
        return { x * rhs.x - y * rhs.y, x * rhs.y + y * rhs.x };
    }

    // (a+bi)/(c+di) = ((a+bi)(c-di)) / (c^2+d^2)
    [[nodiscard]] constexpr Complex operator/(const Complex& rhs) const {
        const T denom = rhs.x * rhs.x + rhs.y * rhs.y;
        return { (x * rhs.x + y * rhs.y) / denom, (y * rhs.x - x * rhs.y) / denom };
    }

    // compound ops
    constexpr void operator+=(Complex rhs) { x += rhs.x; y += rhs.y; }
    constexpr void operator-=(Complex rhs) { x -= rhs.x; y -= rhs.y; }

    constexpr void operator*=(Complex rhs)
    {
        const T rx = x * rhs.x - y * rhs.y;
        const T ry = x * rhs.y + y * rhs.x;
        x = rx; y = ry;
    }

    constexpr void operator/=(Complex rhs)
    {
        const T denom = rhs.x * rhs.x + rhs.y * rhs.y;
        const T rx = (x * rhs.x + y * rhs.y) / denom;
        const T ry = (y * rhs.x - x * rhs.y) / denom;
        x = rx; y = ry;
    }

    // scalar ops (treat scalar as real number)
    constexpr void operator+=(T v) { x += v; }
    constexpr void operator-=(T v) { x -= v; }
    constexpr void operator*=(T v) { x *= v; y *= v; }
    constexpr void operator/=(T v) { x /= v; y /= v; }

    // properties
    [[nodiscard]] constexpr T real() const { return x; }
    [[nodiscard]] constexpr T imag() const { return y; }

    [[nodiscard]] constexpr T mag() const { return sqrt(x * x + y * y); }
    [[nodiscard]] constexpr T mag2() const { return x * x + y * y; }
    [[nodiscard]] constexpr T arg() const { return atan2(y, x); }
    [[nodiscard]] constexpr T dot(const Complex& other) const { return x * other.x + y * other.y; }

    [[nodiscard]] constexpr Complex conj() const { return { x, -y }; }
    [[nodiscard]] constexpr bool isZero() const { return x == (T)0 && y == (T)0; }

    // helpers
    [[nodiscard]] constexpr Complex normalized() const { return (*this) / mag(); }
    [[nodiscard]] constexpr Complex squared() const {
        return { x * x - y * y, (T)2 * x * y }; // (a+bi)^2 = (a^2-b^2) + (2ab)i
    }

    // static
    [[nodiscard]] static constexpr Complex lerp(const Complex& a, const Complex& b, T ratio) { return a + (b - a) * ratio; }
    [[nodiscard]] static constexpr Complex lowest()  { return Complex{ std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest() }; }
    [[nodiscard]] static constexpr Complex highest() { return Complex{ std::numeric_limits<T>::max(), std::numeric_limits<T>::max() }; }

    friend std::ostream& operator<<(std::ostream& os, const Complex& z)
    {
        return os << "(re: " << z.x << ", im: " << z.y << ")";
    }
};


// ----- Matrix -----

template<typename T, int R, int C>
struct Matrix
{
    static_assert(R > 0 && C > 0);

    using value_type = T;
    static constexpr int rows = R;
    static constexpr int cols = C;
    static constexpr int count = R * C;

    // column-major storage
    std::array<storage_t<T>, (size_t)count> m{};

    // constructors
    Matrix() = default;

    // fill diagonal with v (identity for square matrices)
    constexpr explicit Matrix(T v)
    {
        for (int i = 0; i < count; ++i) m[(size_t)i] = T{};
        if constexpr (R == C)
        {
            for (int i = 0; i < R; ++i) (*this)(i, i) = v;
        }
        else
        {
            // non-square: fill all entries with v
            for (int i = 0; i < count; ++i) m[(size_t)i] = v;
        }
    }

    // from flat list in column-major order
    template<typename... Ts>
    constexpr Matrix(Ts... xs) requires (sizeof...(Ts) == (size_t)count && (std::convertible_to<Ts, T> && ...))
        : m{ T(xs)... }
    {
    }

    // conversions
    template<typename U> requires (!std::is_same_v<U, T>)
    constexpr explicit Matrix(const Matrix<U, R, C>& rhs)
    {
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(rhs.m[(size_t)i]);
    }

    template<typename U> requires (!std::is_same_v<U, T>)
    constexpr Matrix& operator=(const Matrix<U, R, C>& rhs)
    {
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(rhs.m[(size_t)i]);
        return *this;
    }

    template<typename U>
    explicit constexpr operator Matrix<U, R, C>() const noexcept
    {
        Matrix<U, R, C> ret(T{ 0.0 });
        for (int i = 0; i < count; ++i)
            ret[(size_t)i] = static_cast<U>(m[(size_t)i]);
        return ret;
    }

    // access
    [[nodiscard]] constexpr storage_t<T>& operator()(int r, int c) noexcept { return m[(size_t)(c * R + r)]; }
    [[nodiscard]] constexpr const storage_t<T>& operator()(int r, int c) const noexcept { return m[(size_t)(c * R + r)]; }

    [[nodiscard]] constexpr storage_t<T>& operator[](int i) noexcept { return m[(size_t)i]; }
    [[nodiscard]] constexpr const storage_t<T>& operator[](int i) const noexcept { return m[(size_t)i]; }

    [[nodiscard]] constexpr storage_t<T>* data() { return m.data(); }
    [[nodiscard]] constexpr const storage_t<T>* data() const { return m.data(); }

    // convenience
    constexpr void setZero() { for (auto& e : m) e = T{}; }

    constexpr void setIdentity(T diag = T{ 1 }) requires (R == C)
    {
        setZero();
        for (int i = 0; i < R; ++i) (*this)(i, i) = diag;
    }

    [[nodiscard]] constexpr bool isIdentity(T diag = T{ 1 }) const requires (R == C)
    {
        for (int c = 0; c < C; ++c)
            for (int r = 0; r < R; ++r)
                if ((*this)(r, c) != ((r == c) ? diag : T{}))
                    return false;
        return true;
    }

    // operators
    [[nodiscard]] constexpr bool operator==(const Matrix& rhs) const { return m == rhs.m; }
    [[nodiscard]] constexpr bool operator!=(const Matrix& rhs) const { return m != rhs.m; }

    [[nodiscard]] constexpr Matrix operator-() const
    {
        Matrix out;
        for (int i = 0; i < count; ++i) out.m[(size_t)i] = -m[(size_t)i];
        return out;
    }

    [[nodiscard]] constexpr Matrix operator+(const Matrix& rhs) const
    {
        Matrix out;
        for (int i = 0; i < count; ++i) out.m[(size_t)i] = m[(size_t)i] + rhs.m[(size_t)i];
        return out;
    }

    [[nodiscard]] constexpr Matrix operator-(const Matrix& rhs) const
    {
        Matrix out;
        for (int i = 0; i < count; ++i) out.m[(size_t)i] = m[(size_t)i] - rhs.m[(size_t)i];
        return out;
    }

    [[nodiscard]] constexpr Matrix operator*(T s) const
    {
        Matrix out;
        for (int i = 0; i < count; ++i) out.m[(size_t)i] = m[(size_t)i] * s;
        return out;
    }

    [[nodiscard]] constexpr Matrix operator/(T s) const
    {
        Matrix out;
        for (int i = 0; i < count; ++i) out.m[(size_t)i] = m[(size_t)i] / s;
        return out;
    }

    constexpr void operator+=(const Matrix& rhs) { for (int i = 0; i < count; ++i) m[(size_t)i] += rhs.m[(size_t)i]; }
    constexpr void operator-=(const Matrix& rhs) { for (int i = 0; i < count; ++i) m[(size_t)i] -= rhs.m[(size_t)i]; }
    constexpr void operator*=(T s) { for (int i = 0; i < count; ++i) m[(size_t)i] *= s; }
    constexpr void operator/=(T s) { for (int i = 0; i < count; ++i) m[(size_t)i] /= s; }

    constexpr Matrix& operator*=(const Matrix& rhs)
    {
        *this = (*this) * rhs;
        return *this;
    }


    template<typename U>
    constexpr Matrix& operator*=(const Matrix<U, R, C>& rhs)
        requires (R == C)
    {
        const auto prod = (*this) * rhs;
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(prod[i]);
        return *this;
    }


    // scalar ops with different type, e.g. fdd *= f64
    template<typename U>
    constexpr Matrix& operator+=(const Matrix<U, R, C>& rhs)
    {
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(m[(size_t)i] + rhs.m[(size_t)i]);
        return *this;
    }
    template<typename U>
    constexpr Matrix& operator-=(const Matrix<U, R, C>& rhs)
    {
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(m[(size_t)i] - rhs.m[(size_t)i]);
        return *this;
    }
    template<typename U>
    constexpr Matrix& operator*=(U s)
    {
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(m[(size_t)i] * s);
        return *this;
    }

    template<typename U>
    constexpr Matrix& operator/=(U s)
    {
        for (int i = 0; i < count; ++i)
            m[(size_t)i] = static_cast<T>(m[(size_t)i] / s);
        return *this;
    }

    // matrix multiply
    template<int C2>
    [[nodiscard]] constexpr Matrix<T, R, C2> operator*(const Matrix<T, C, C2>& rhs) const
    {
        Matrix<T, R, C2> out;
        out.setZero();

        for (int c2 = 0; c2 < C2; ++c2)
        {
            for (int r = 0; r < R; ++r)
            {
                T acc{};
                for (int k = 0; k < C; ++k)
                    acc += (*this)(r, k) * rhs(k, c2);
                out(r, c2) = acc;
            }
        }
        return out;
    }

    // mat * vec2 (only for 2x2)
    [[nodiscard]] constexpr Vec2<T> operator*(const Vec2<T>& v) const requires (R == 2 && C == 2)
    {
        return Vec2<T>{
            (*this)(0, 0)* v.x + (*this)(0, 1) * v.y,
            (*this)(1, 0)* v.x + (*this)(1, 1) * v.y
        };
    }

    [[nodiscard]] constexpr Vec2<T> operator*(const Vec2<T>& v) const requires (R == 3 && C == 3)
    {
        return mulPoint(v);
    }

    // homogeneous 2D point: (x,y,1)
    [[nodiscard]] constexpr Vec2<T> mulPoint(const Vec2<T>& v) const requires (R == 3 && C == 3)
    {
        const T x = (*this)(0, 0) * v.x + (*this)(0, 1) * v.y + (*this)(0, 2);
        const T y = (*this)(1, 0) * v.x + (*this)(1, 1) * v.y + (*this)(1, 2);
        const T w = (*this)(2, 0) * v.x + (*this)(2, 1) * v.y + (*this)(2, 2);

        if (w == T{ 0 } || w == T{ 1 })
            return { x, y };

        return { x / w, y / w };
    }

    // homogeneous 2D direction: (x,y,0) (no translation)
    [[nodiscard]] constexpr Vec2<T> mulVector(const Vec2<T>& v) const requires (R == 3 && C == 3)
    {
        const T x = (*this)(0, 0) * v.x + (*this)(0, 1) * v.y;
        const T y = (*this)(1, 0) * v.x + (*this)(1, 1) * v.y;
        return { x, y };
    }

    // properties
    [[nodiscard]] constexpr Matrix<T, C, R> transposed() const
    {
        Matrix<T, C, R> out;
        for (int c = 0; c < C; ++c)
            for (int r = 0; r < R; ++r)
                out(c, r) = (*this)(r, c);
        return out;
    }

    // ----- determinant -----

    [[nodiscard]] constexpr T determinant() const requires (R == 2 && C == 2)
    {
        return (*this)(0, 0) * (*this)(1, 1)
             - (*this)(0, 1) * (*this)(1, 0);
    }

    [[nodiscard]] constexpr T determinant() const requires (R == 3 && C == 3)
    {
        const storage_t<T>& a = (*this)(0, 0);
        const storage_t<T>& b = (*this)(0, 1);
        const storage_t<T>& c = (*this)(0, 2);

        const storage_t<T>& d = (*this)(1, 0);
        const storage_t<T>& e = (*this)(1, 1);
        const storage_t<T>& f = (*this)(1, 2);

        const storage_t<T>& g = (*this)(2, 0);
        const storage_t<T>& h = (*this)(2, 1);
        const storage_t<T>& i = (*this)(2, 2);

        return a * (e * i - f * h)
             - b * (d * i - f * g)
             + c * (d * h - e * g);
    }

    [[nodiscard]] constexpr Matrix inversed() const requires (R == 2 && C == 2)
    {
        const T det = determinant();

        Matrix inv;
        inv(0,0) =  (*this)(1,1);
        inv(0,1) = -(*this)(0,1);
        inv(1,0) = -(*this)(1,0);
        inv(1,1) =  (*this)(0,0);

        return inv / det;
    }

    [[nodiscard]] constexpr Matrix inversed() const requires (R == 3 && C == 3)
    {
        const storage_t<T>& a = (*this)(0,0);
        const storage_t<T>& b = (*this)(0,1);
        const storage_t<T>& c = (*this)(0,2);

        const storage_t<T>& d = (*this)(1,0);
        const storage_t<T>& e = (*this)(1,1);
        const storage_t<T>& f = (*this)(1,2);

        const storage_t<T>& g = (*this)(2,0);
        const storage_t<T>& h = (*this)(2,1);
        const storage_t<T>& i = (*this)(2,2);

        Matrix inv;

        inv(0,0) =  (e*i - f*h);
        inv(0,1) = -(b*i - c*h);
        inv(0,2) =  (b*f - c*e);

        inv(1,0) = -(d*i - f*g);
        inv(1,1) =  (a*i - c*g);
        inv(1,2) = -(a*f - c*d);

        inv(2,0) =  (d*h - e*g);
        inv(2,1) = -(a*h - b*g);
        inv(2,2) =  (a*e - b*d);

        const T det = a*(e*i - f*h)
                    - b*(d*i - f*g)
                    + c*(d*h - e*g);

        return inv / det;
    }

    // static
    [[nodiscard]] static constexpr Matrix identity(T diag = T{ 1 }) requires (R == C)
    {
        Matrix out;
        out.setIdentity(diag);
        return out;
    }

    [[nodiscard]] static constexpr Matrix zero()
    {
        Matrix out;
        out.setZero();
        return out;
    }

    friend std::ostream& operator<<(std::ostream& os, const Matrix& M)
    {
        os << "{";
        for (int r = 0; r < R; ++r)
        {
            os << (r == 0 ? "" : " ");
            os << "[";
            for (int c = 0; c < C; ++c)
            {
                os << M(r, c);
                if (c + 1 < C) os << ", ";
            }
            os << "]";
            if (r + 1 < R) os << "\n";
        }
        return os << "}";
    }
};

// scalar op matrix
template<typename T, int R, int C>
[[nodiscard]] constexpr Matrix<T, R, C> operator*(const T& s, const Matrix<T, R, C>& M)
{
    return M * s;
}

template<typename X> struct is_matrix : std::false_type {};
template<typename T, int R, int C> struct is_matrix<Matrix<T, R, C>> : std::true_type {};
template<typename X> inline constexpr bool is_matrix_v = is_matrix<std::remove_cvref_t<X>>::value;

// convenience aliases
template<typename T> using Mat2 = Matrix<T, 2, 2>;
template<typename T> using Mat3 = Matrix<T, 3, 3>;
template<typename T> using Mat4 = Matrix<T, 4, 4>;

// ----- Range -----
template<typename T>
struct Range
{
    storage_t<T> lo, hi;

    static Range<T> mergeShrink(const std::vector<Range<T>>& ranges)
    {
        T lo = std::numeric_limits<T>::lowest();
        T hi = std::numeric_limits<T>::max();
        for (auto& range : ranges)
        {
            if (range.lo > lo) lo = range.lo;
            if (range.hi < hi) hi = range.hi;
        }
        return Range<T>{ lo, hi };
    }

    static Range<T> mergeExpand(const std::vector<Range<T>>& ranges)
    {
        T lo = std::numeric_limits<T>::max();
        T hi = std::numeric_limits<T>::lowest();
        for (auto& range : ranges)
        {
            if (range.lo < lo) lo = range.lo;
            if (range.hi > hi) hi = range.hi;
        }
        return Range<T>{ lo, hi };
    }
};

// ----- Segment -----

template<typename T>
struct Segment
{
    using Pt = Vec2<T>;
    Pt a, b;

    // constructors
    Segment() = default;
    constexpr Segment(T ax, T ay, T bx, T by) : a(ax, ay), b(bx, by) {}
    constexpr Segment(const Pt& A, const Pt& B) : a(A), b(B) {}

    // conversions
    template<typename T2> constexpr operator Segment<T2>() const {
        return Segment<T2>(static_cast<Vec2<T2>>(a), static_cast<Vec2<T2>>(b));
    }

    // access
    [[nodiscard]] constexpr const Pt& operator[](int i) const { return (&a)[i]; }
    [[nodiscard]] constexpr Pt& operator[](int i) { return (&a)[i]; }
    [[nodiscard]] constexpr Pt* data() { return (&a); }

    // equality
    [[nodiscard]] constexpr bool operator==(const Segment& rhs) const { return a == rhs.a && b == rhs.b; }
    [[nodiscard]] constexpr bool operator!=(const Segment& rhs) const { return !(*this == rhs); }

    // basic geometry
    [[nodiscard]] constexpr Pt   vec()       const { return b - a; }
    [[nodiscard]] constexpr T    lengthSq()  const { Pt v = vec(); return v.x * v.x + v.y * v.y; }
    [[nodiscard]] constexpr T    length()    const { return sqrt(lengthSq()); }
    [[nodiscard]] constexpr Pt   midpoint()  const { return (a + b) * T(0.5); }
    [[nodiscard]] static constexpr T cross(const Pt& u, const Pt& v) { return u.x * v.y - u.y * v.x; }

    // point at parameter t in [0,1]
    [[nodiscard]] constexpr Pt pointAt(T t) const { return a + (b - a) * t; }

    // closest parameter on the segment to point p (clamped to [0,1])
    [[nodiscard]] constexpr T closestParam(const Pt& p) const {
        Pt ab = b - a;
        T d2 = ab.x * ab.x + ab.y * ab.y;
        if (d2 == T(0)) return T(0);
        T t = (p - a).dot(ab) / d2;
        if (t < T(0)) t = T(0);
        else if (t > T(1)) t = T(1);
        return t;
    }

    // closest point on the segment to p
    [[nodiscard]] constexpr Pt closestPoint(const Pt& p) const { return pointAt(closestParam(p)); }

    // squared distance from p to the segment
    [[nodiscard]] constexpr T distSqToPoint(const Pt& p) const {
        Pt q = closestPoint(p);
        Pt d = p - q;
        return d.x * d.x + d.y * d.y;
    }

    // distance from p to the segment
    [[nodiscard]] constexpr T distToPoint(const Pt& p) const { return sqrt(distSqToPoint(p)); }

    // does the segment contain point p (collinear and within endpoints)?
    // eps == 0 means exact; pass a small eps for floats (e.g. 1e-6)
    [[nodiscard]] constexpr bool containsPoint(const Pt& p, T eps = T(0)) const {
        Pt ap = p - a, ab = b - a;
        T c = cross(ab, ap);
        if (c > eps || c < -eps) return false; // not collinear
        // check projection within [0, |ab|^2]
        T d2 = ab.x * ab.x + ab.y * ab.y;
        if (d2 == T(0)) return (p == a);
        T t = (ap.x * ab.x + ap.y * ab.y);
        return (t >= -eps) && (t <= d2 + eps);
    }

    // proper segment/segment intersection (returns true if they touch anywhere).
    // Optionally returns parameters t (on this) and u (on other) in [0,1].
    [[nodiscard]] constexpr bool intersects(const Segment& s, T* out_t = nullptr, T* out_u = nullptr) const {
        Pt r = b - a;
        Pt q = s.a;
        Pt svec = s.b - s.a;
        Pt pq = q - a;

        T rxs = cross(r, svec);
        T pqxr = cross(pq, r);

        if (rxs == T(0)) {
            // parallel
            if (pqxr != T(0)) return false; // parallel, non-collinear
            // collinear: check overlap via projections
            T r2 = r.x * r.x + r.y * r.y;
            T t0 = ((q - a).dot(r)) / (r2 == T(0) ? T(1) : r2);
            T t1 = ((s.b - a).dot(r)) / (r2 == T(0) ? T(1) : r2);
            if (t0 > t1) { T tmp = t0; t0 = t1; t1 = tmp; }
            bool overlap = !(t1 < T(0) || t0 > T(1));
            if (overlap) {
                if (out_t) *out_t = std::max(T(0), t0);
                if (out_u) *out_u = T(0); // arbitrary for collinear; caller can compute if needed
            }
            return overlap;
        }
        else {
            // general case
            T t = cross(pq, svec) / rxs;
            T u = cross(pq, r) / rxs;
            if (t >= T(0) && t <= T(1) && u >= T(0) && u <= T(1)) {
                if (out_t) *out_t = t;
                if (out_u) *out_u = u;
                return true;
            }
            return false;
        }
    }
};

// ----- Ray -----

template<typename T>
struct Ray : public Vec2<T> // todo: Some methods maybe aren't ideal for inheriting (e.g. angleTo)
{
    f64 angle;

    Ray() = default;
    constexpr Ray(f64 x, f64 y, T angle) : Vec2<T>(x, y), angle(angle) {}
    constexpr Ray(const Vec2<T>& p, T angle) : Vec2<T>(p.x, p.y), angle(angle) {}
    constexpr Ray(const Vec2<T>& a, const Vec2<T>& b) : Vec2<T>(a.x, a.y), angle(a.angleTo(b)) {}

    [[nodiscard]] constexpr Vec2<T> project(T dist) const {
        return {
            Vec2<T>::x + std::cos(angle) * dist,
            Vec2<T>::y + std::sin(angle) * dist,
        };
    }
};

// ----- Triangle -----

template <typename VecT>
struct Triangle 
{
    VecT* a, b, c;

    Triangle(VecT* p1, VecT* p2, VecT* p3)
    {
        // Calculate orientation
        f64 area = (p2->x - p1->x) * (p3->y - p1->y) - (p2->y - p1->y) * (p3->x - p1->x);

        // Store in CCW order
        if (area < 0) { a = p1; b = p3; c = p2; }
        else          { a = p1; b = p2; c = p3; }
    }

    [[nodiscard]] bool containsVertex(VecT* p) const {
        return (a == p || b == p || c == p);
    }

    // Returns true if 'p' lies in the circumcircle of the triangle
    [[nodiscard]] bool isPointInCircumcircle(const VecT& p) const
    {
        const f64 epsilon = 1e-10;
        f64 ax = a->x - p.x, ay = a->y - p.y;
        f64 bx = b->x - p.x, by = b->y - p.y;
        f64 cx = c->x - p.x, cy = c->y - p.y;

        f64 dA = ax * ax + ay * ay;
        f64 dB = bx * bx + by * by;
        f64 dC = cx * cx + cy * cy;

        f64 determinant =
            (ax * (by * dC - cy * dB)) -
            (ay * (bx * dC - cx * dB)) +
            (dA * (bx * cy - by * cx));

        return determinant >= -epsilon; // Changed to use epsilon
    }

    [[nodiscard]] bool operator==(const Triangle& other) const
    {
        // Make local copies
        VecT* p1 = a, p2 = b, p3 = c;
        VecT* q1 = other.a, q2 = other.b, q3 = other.c;

        // Sort in ascending order
        if (p2 < p1) { VecT* tmp = p1; p1 = p2; p2 = tmp; }
        if (p3 < p1) { VecT* tmp = p1; p1 = p3; p3 = tmp; }
        if (p3 < p2) { VecT* tmp = p2; p2 = p3; p3 = tmp; }

        if (q2 < q1) { VecT* tmp = q1; q1 = q2; q2 = tmp; }
        if (q3 < q1) { VecT* tmp = q1; q1 = q3; q3 = tmp; }
        if (q3 < q2) { VecT* tmp = q2; q2 = q3; q3 = tmp; }

        // Compare sorted pointers
        return (p1 == q1 && p2 == q2 && p3 == q3);
    }
};

template <typename VecT>
struct TriangleHash
{
    std::size_t operator()(const Triangle<VecT>& tri) const noexcept
    {
        size_t h1 = reinterpret_cast<size_t>(tri.a);
        size_t h2 = reinterpret_cast<size_t>(tri.b);
        size_t h3 = reinterpret_cast<size_t>(tri.c);

        // Murmur-inspired hash mixing for stronger hash distribution
        h1 ^= (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        h1 ^= (h3 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        return h1;
    }
};

template <typename VecT>
struct TriangleEqual
{
    bool operator()(const Triangle<VecT>& lhs, const Triangle<VecT>& rhs) const {
        return lhs == rhs;
    }
};

// ----- Rect -----

template<typename T> struct Quad; // forward declare for Rect->Quad conversion support
template<typename T>
struct Rect
{
    union { struct { storage_t<T> x1, y1; }; Vec2<T> a; };
    union { struct { storage_t<T> x2, y2; }; Vec2<T> b; };

    Rect() : a(0, 0), b(0, 0) {}
    constexpr Rect(T _x1, T _y1, T _x2, T _y2) { set(_x1, _y1, _x2, _y2); }
    constexpr Rect(const Vec2<T>& _a, const Vec2<T>& _b) { set(_a, _b); }

    template<typename T2>
    explicit constexpr operator Rect<T2>() const { return Rect<T2>((T2)x1, (T2)y1, (T2)x2, (T2)y2); }

    constexpr operator Quad<T>() const { return Quad<T>(x1, y1, x2, y1, x2, y2, x1, y2); }

    constexpr void set(const Rect& r) { a = r.a; b = r.b; }
    constexpr void set(const Vec2<T>& A, const Vec2<T>& B) { a = A; b = B; }
    constexpr void set(T _x1, T _y1, T _x2, T _y2) { x1 = _x1; y1 = _y1; x2 = _x2; y2 = _y2; }

    [[nodiscard]] constexpr T x() const { return x1; }
    [[nodiscard]] constexpr T y() const { return y1; }
    [[nodiscard]] constexpr T width() const { return x2 - x1; }
    [[nodiscard]] constexpr T height() const { return y2 - y1; }
    [[nodiscard]] constexpr Vec2<T> size() const { return { x2 - x1, y2 - y1 }; }

    [[nodiscard]] constexpr T cx() const { return (x1 + x2) / 2; }
    [[nodiscard]] constexpr T cy() const { return (y1 + y2) / 2; }
    [[nodiscard]] constexpr Vec2<T> center() const { return { (x1 + x2) / 2, (y1 + y2) / 2 }; }

    [[nodiscard]] constexpr Vec2<T> tl() const { return { std::min(x1, x2), std::min(y1, y2) }; }
    [[nodiscard]] constexpr Vec2<T> tr() const { return { std::max(x1, x2), std::min(y1, y2) }; }
    [[nodiscard]] constexpr Vec2<T> bl() const { return { std::min(x1, x2), std::max(y1, y2) }; }
    [[nodiscard]] constexpr Vec2<T> br() const { return { std::max(x1, x2), std::max(y1, y2) }; }

    [[nodiscard]] constexpr bool hitTest(T x, T y) const { return (x >= x1 && y >= y1 && x <= x2 && y <= y2); }
    [[nodiscard]] constexpr Rect scaled(T mult) const {
        T cx = (x1 + x2) / 2;
        T cy = (y1 + y2) / 2;
        T r_x1 = cx + (x1 - cx) * mult;
        T r_y1 = cy + (y1 - cy) * mult;
        T r_x2 = cx + (x2 - cx) * mult;
        T r_y2 = cy + (y2 - cy) * mult;
        return Rect(r_x1, r_y1, r_x2, r_y2);
    }

    void merge(const Rect& r)
    {
        if (r.x1 < x1) x1 = r.x1;
        if (r.y1 < y1) y1 = r.y1;
        if (r.x2 > x2) x2 = r.x2;
        if (r.y2 > y2) y2 = r.y2;
    }

    // the "biggest" finite rect representable (caution: size of rect becomes infinite, be careul if using fast-math)
    static constexpr Rect<T> max_extent() {
        constexpr T lo = std::numeric_limits<T>::lowest();
        constexpr T hi = std::numeric_limits<T>::max();
        return Rect<T>(lo, lo, hi, hi);
    }

    // large finite rect such that the width/height is guaranteed to be finite
    static constexpr Rect max_finite_extent() {
        const T hi = std::numeric_limits<T>::max() / T(4);
        const T lo = -hi;
        return { lo, lo, hi, hi };
    }

    // an "empty" rect for incremental expansion
    static constexpr Rect<T> empty() {
        constexpr T lo = std::numeric_limits<T>::lowest();
        constexpr T hi = std::numeric_limits<T>::max();
        return Rect<T>(hi, hi, lo, lo);
    }
};

// ----- AngledRect -----

template<typename T> struct Quad;
template<typename T> struct AngledRect;

template<typename T>
struct AngledRect
{
    union { struct { storage_t<T> cx, cy; }; Vec2<T> cen; };
    union { struct { storage_t<T> w, h; }; Vec2<T> size; };
    storage_t<T> angle;

    AngledRect() {}
    AngledRect(T _cx, T _cy, T _w, T _h, T _angle) : cx(_cx), cy(_cy), w(_w), h(_h), angle(_angle) {}
    AngledRect(Vec2<T> cen, Vec2<T> size, T _angle) : cx(cen.x), cy(cen.y), w(size.x), h(size.y), angle(_angle) {}

    [[nodiscard]] Quad<T> toQuad()   const { return Quad<T>(*this); }
    [[nodiscard]] operator Quad<T>() const { return Quad<T>(*this); }

    [[nodiscard]] T aspectRatio() const { return (w / h); }

    template<typename T2>
    static Vec2<T2> enclosingSize(const AngledRect<T2>& A, const AngledRect<T2>& B, T2 avgAngle, T2 fixedAspectRatio = 0)
    {
        // rotate world points by –avgAngle so the enclosing frame is axis-aligned
        const T2 cosC = cos(-avgAngle);
        const T2 sinC = sin(-avgAngle);

        T2 xmin = std::numeric_limits<T2>::max(),    ymin = std::numeric_limits<T2>::max();
        T2 xmax = std::numeric_limits<T2>::lowest(), ymax = std::numeric_limits<T2>::lowest();

        auto accumulate = [&](const AngledRect<T2>& R)
        {
            const T2 d = R.angle - avgAngle; // local tilt inside the frame
            const T2 cd = cos(d);
            const T2 sd = sin(d);

            // centre in the avgAngle frame
            const T2 cxp = cosC * R.cx - sinC * R.cy;
            const T2 cyp = sinC * R.cx + cosC * R.cy;
            const T2 hw = R.w * 0.5;
            const T2 hh = R.h * 0.5;

            for (int sx = -1; sx <= 1; sx += 2)
            {
                for (int sy = -1; sy <= 1; sy += 2)
                {
                    const T2 dx = sx * hw;
                    const T2 dy = sy * hh;

                    // rotate offset by d, then add to centre
                    const T2 x = cxp + dx * cd - dy * sd;
                    const T2 y = cyp + dx * sd + dy * cd;

                    xmin = std::min(xmin, x);
                    xmax = std::max(xmax, x);
                    ymin = std::min(ymin, y);
                    ymax = std::max(ymax, y);
                }
            }
        };

        accumulate(A);
        accumulate(B);

        T2 W, H;
        if (fixedAspectRatio > T2{0})
        {
            const T2 Wraw = xmax - xmin;
            const T2 Hraw = ymax - ymin;

            if (Wraw / Hraw >= fixedAspectRatio) {
                W = Wraw;
                H = W / fixedAspectRatio;
            } else {
                H = Hraw;
                W = fixedAspectRatio * H;
            }
        }
        else {
            W = xmax - xmin;
            H = ymax - ymin;
        }

        return { W, H };
    }

    void fitTo(AngledRect<T> a, AngledRect<T> b, T fixed_aspect_ratio=0)
    {
        cx = (a.cx + b.cx) * T{0.5};
        cy = (a.cy + b.cy) * T{0.5};
        angle = math::avgAngle<T>(a.angle, b.angle);
        size = AngledRect<T>::enclosingSize<T>(a, b, angle, fixed_aspect_ratio);
    }
};

// ----- Quad -----

template<typename T>
struct Quad
{
    using Pt = Vec2<T>;
    Pt a, b, c, d;

    Quad() = default;
    constexpr Quad(T ax, T ay, T bx, T by, T cx, T cy, T dx, T dy)  : a(ax, ay), b(bx, by), c(cx, cy), d(dx, dy) {}
    constexpr Quad(T x1, T y1, T x2, T y2)        { set(x1, y1, x2, y2); }
    constexpr Quad(T cx, T cy, T w, T h, T angle) { set(cx, cy, w, h, angle); }

    constexpr Quad(const Pt& A, const Pt& B, const Pt& C, const Pt& D) : a(A), b(B), c(C), d(D) {}
    constexpr Quad(const Rect<T>& r)              { set(r.x1, r.y1, r.x2, r.y2); }
    constexpr Quad(const AngledRect<T>& r)        { set(r.cx, r.cy, r.w, r.h, r.angle); }

    [[nodiscard]] constexpr Vec2<T>* data() { return &a; }

    [[nodiscard]] constexpr Segment<T> ab() const { return Segment<T>(a, b); }
    [[nodiscard]] constexpr Segment<T> bc() const { return Segment<T>(b, c); }
    [[nodiscard]] constexpr Segment<T> cd() const { return Segment<T>(c, d); }
    [[nodiscard]] constexpr Segment<T> da() const { return Segment<T>(d, a); }

    template<typename ScalarT>
    [[nodiscard]] constexpr Vec2<T> lerpPoint(ScalarT fx, ScalarT fy) const
    {
        const Vec2<T> U = b - a;
        const Vec2<T> V = d - a;
        const Vec2<T> W = a - b - d + c; // twist
        return a + U * fx + V * fy + W * (fx * fy);
    }
    template<typename ScalarT>
    [[nodiscard]] constexpr Vec2<T> lerpPoint(Vec2<ScalarT> f) const {
        return lerpPoint(f.x, f.y);
    }

    [[nodiscard]] constexpr T minX() const { return std::min({ a.x, b.x, c.x, d.x }); }
    [[nodiscard]] constexpr T maxX() const { return std::max({ a.x, b.x, c.x, d.x }); }
    [[nodiscard]] constexpr T minY() const { return std::min({ a.y, b.y, c.y, d.y }); }
    [[nodiscard]] constexpr T maxY() const { return std::max({ a.y, b.y, c.y, d.y }); }

    template<typename T2>
    [[nodiscard]] constexpr operator Quad<T2>() {
        return Quad<T2>(
            static_cast<Vec2<T2>>(a),
            static_cast<Vec2<T2>>(b),
            static_cast<Vec2<T2>>(c),
            static_cast<Vec2<T2>>(d)
        );
    }

    constexpr void set(T x1, T y1, T x2, T y2)
    {
        a = { x1, y1 };
        b = { x2, y1 };
        c = { x2, y2 };
        d = { x1, y2 };
    }

    void set(T cx, T cy, T w, T h, T angle)
    {
        T w2 = w / 2, h2 = h / 2, _cos = cos(angle), _sin = sin(angle);
        a = {cx + ((+w2) * _cos - (+h2) * _sin), cy + ((+h2) * _cos + (+w2) * _sin)};
        b = {cx + ((-w2) * _cos - (+h2) * _sin), cy + ((+h2) * _cos + (-w2) * _sin)};
        c = {cx + ((-w2) * _cos - (-h2) * _sin), cy + ((-h2) * _cos + (-w2) * _sin)};
        d = {cx + ((+w2) * _cos - (-h2) * _sin), cy + ((-h2) * _cos + (+w2) * _sin)};
    }

    [[nodiscard]] constexpr bool operator ==(const Quad& rhs) { return (a == rhs.a && b == rhs.b && c == rhs.c && d == rhs.d); }
    [[nodiscard]] constexpr bool operator !=(const Quad& rhs) { return (a != rhs.a || b != rhs.b || c != rhs.c || d != rhs.d); }

    [[nodiscard]] Vec2<T> center() const { return (a + b + c + d) / T(4); }
    [[nodiscard]] T area() const
    {
        // shoelace formula
        return std::abs((a.x * b.y + b.x * c.y + c.x * d.y + d.x * a.y) - (a.y * b.x + b.y * c.x + c.y * d.x + d.y * a.x)) / 2;
    }
    [[nodiscard]] Rect<T> boundingRect() const
    {
        T min_x = std::min({ a.x, b.x, c.x, d.x });
        T max_x = std::max({ a.x, b.x, c.x, d.x });
        T min_y = std::min({ a.y, b.y, c.y, d.y });
        T max_y = std::max({ a.y, b.y, c.y, d.y });
        return Rect<T>(min_x, min_y, max_x, max_y);
    }

    template<typename U>
    [[nodiscard]] constexpr Quad<T> scaled(U scale) const
    {
        Vec2<T> cen = center();
        return Quad<T>(
            cen + (a - cen) * scale,
            cen + (b - cen) * scale,
            cen + (c - cen) * scale,
            cen + (d - cen) * scale
        );
    }

    [[nodiscard]] static constexpr T cross(const Pt& u, const Pt& v) {
        return u.x * v.y - u.y * v.x;
    }

    [[nodiscard]] static constexpr T cross_at(const Pt& o, const Pt& u, const Pt& v) {
        // cross( u - o, v - o )
        return (u.x - o.x) * (v.y - o.y) - (u.y - o.y) * (v.x - o.x);
    }

    [[nodiscard]] constexpr bool contains(T x, T y) const noexcept {
        const Pt p{ x, y };
        const T s0 = cross_at(a, b, p);
        const T s1 = cross_at(b, c, p);
        const T s2 = cross_at(c, d, p);
        const T s3 = cross_at(d, a, p);

        // Inside if all have the same sign (allowing on-edge as inside).
        const bool all_nonneg = (s0 >= T(0)) & (s1 >= T(0)) & (s2 >= T(0)) & (s3 >= T(0));
        const bool all_nonpos = (s0 <= T(0)) & (s1 <= T(0)) & (s2 <= T(0)) & (s3 <= T(0));
        return all_nonneg || all_nonpos;
    }

    [[nodiscard]] constexpr bool contains(const Pt& p) const noexcept { return contains(p.x, p.y); }

    [[nodiscard]] constexpr bool intersects(const Segment<T>& seg) const noexcept
    {
        if (ab().intersects(seg)) return true;
        if (bc().intersects(seg)) return true;
        if (cd().intersects(seg)) return true;
        if (da().intersects(seg)) return true;
        return false;
    }
};

/// ================= free operator overloads =================

/// ----- Vec2 overloads -----

template<class X> struct is_vec2 : std::false_type {};
template<class T> struct is_vec2<Vec2<T>> : std::true_type {};
template<class X> inline constexpr bool is_vec2_v = is_vec2<std::remove_cvref_t<X>>::value;

template<class T, class U> using plus_t  = value_t<decltype(std::declval<T>() + std::declval<U>())>;
template<class T, class U> using minus_t = value_t<decltype(std::declval<T>() - std::declval<U>())>;
template<class T, class U> using mul_t   = value_t<decltype(std::declval<T>() * std::declval<U>())>;
template<class T, class U> using div_t   = value_t<decltype(std::declval<T>() / std::declval<U>())>;

// Vec2 op scalar
template<class T, class U> requires (!is_vec2_v<U>&& requires(T a, U b) { a + b; })
constexpr auto operator+(const Vec2<T>& v, const U& s) -> Vec2<plus_t<T, U>> {
    using R = plus_t<T, U>; return { R(v.x) + s, R(v.y) + s };
}
template<class T, class U> requires (!is_vec2_v<U>&& requires(T a, U b) { a - b; })
constexpr auto operator-(const Vec2<T>& v, const U& s) -> Vec2<minus_t<T, U>> {
    using R = minus_t<T, U>; return { R(v.x) - s, R(v.y) - s };
}
template<class T, class U> requires (!is_vec2_v<U>&& requires(T a, U b) { a* b; })
constexpr auto operator*(const Vec2<T>& v, const U& s) -> Vec2<mul_t<T, U>> {
    using R = mul_t<T, U>; return { R(v.x) * s, R(v.y) * s };
}
template<class T, class U> requires (!is_vec2_v<U>&& requires(T a, U b) { a / b; })
constexpr auto operator/(const Vec2<T>& v, const U& s) -> Vec2<div_t<T, U>> {
    using R = div_t<T, U>; return { R(v.x) / s, R(v.y) / s };
}

// scalar op Vec2
template<class T, class U> requires (!is_vec2_v<T>&& requires(T a, U b) { a + b; })
constexpr auto operator+(const T& s, const Vec2<U>& v) -> Vec2<plus_t<T, U>> {
    using R = plus_t<T, U>; return { s + R(v.x), s + R(v.y) };
}
template<class T, class U> requires (!is_vec2_v<T>&& requires(T a, U b) { a - b; })
constexpr auto operator-(const T& s, const Vec2<U>& v) -> Vec2<minus_t<T, U>> {
    using R = minus_t<T, U>; return { s - R(v.x), s - R(v.y) };
}
template<class T, class U> requires (!is_vec2_v<T>&& requires(T a, U b) { a* b; })
constexpr auto operator*(const T& s, const Vec2<U>& v) -> Vec2<mul_t<T, U>> {
    using R = mul_t<T, U>; return { s * R(v.x), s * R(v.y) };
}
template<class T, class U> requires (!is_vec2_v<T>&& requires(T a, U b) { a / b; })
constexpr auto operator/(const T& s, const Vec2<U>& v) -> Vec2<div_t<T, U>> {
    using R = div_t<T, U>; return { s / R(v.x), s / R(v.y) };
}

// Vec2 op Vec2 (elementwise)
template<class T, class U> requires requires(T a, U b) { a + b; }
constexpr auto operator+(const Vec2<T>& a, const Vec2<U>& b) -> Vec2<plus_t<T, U>> {
    using R = plus_t<T, U>; return { R(a.x) + b.x, R(a.y) + b.y };
}
template<class T, class U> requires requires(T a, U b) { a - b; }
constexpr auto operator-(const Vec2<T>& a, const Vec2<U>& b) -> Vec2<minus_t<T, U>> {
    using R = minus_t<T, U>; return { R(a.x) - b.x, R(a.y) - b.y };
}
template<class T, class U> requires requires(T a, U b) { a* b; }
constexpr auto operator*(const Vec2<T>& a, const Vec2<U>& b) -> Vec2<mul_t<T, U>> {
    using R = mul_t<T, U>; return { R(a.x) * b.x, R(a.y) * b.y };
}
template<class T, class U> requires requires(T a, U b) { a / b; }
constexpr auto operator/(const Vec2<T>& a, const Vec2<U>& b) -> Vec2<div_t<T, U>> {
    using R = div_t<T, U>; return { R(a.x) / b.x, R(a.y) / b.y };
}


/// ----- Vec3 overloads -----
template<typename T> inline Vec3<T> operator*(T s, const Vec3<T>& v) { return v * s; }
template<typename T> inline Vec3<T> operator*(const Vec3<T>& v, T s) { return v * s; }
template<typename T> inline Vec3<T> operator/(T s, const Vec3<T>& v) { return v / s; }
template<typename T> inline Vec3<T> operator/(const Vec3<T>& v, T s) { return v / s; }
template<typename T> inline Vec3<T> operator+(T s, const Vec3<T>& v) { return v + s; }
template<typename T> inline Vec3<T> operator+(const Vec3<T>& v, T s) { return v + s; }
template<typename T> inline Vec3<T> operator-(T s, const Vec3<T>& v) { return v - s; }
template<typename T> inline Vec3<T> operator-(const Vec3<T>& v, T s) { return v - s; }

/// ----- Vec4 overloads -----
template<typename T> inline Vec4<T> operator*(T s, const Vec4<T>& v) { return v * s; }
template<typename T> inline Vec4<T> operator*(const Vec4<T>& v, T s) { return v * s; }
template<typename T> inline Vec4<T> operator/(T s, const Vec4<T>& v) { return v / s; }
template<typename T> inline Vec4<T> operator/(const Vec4<T>& v, T s) { return v / s; }
template<typename T> inline Vec4<T> operator+(T s, const Vec4<T>& v) { return v + s; }
template<typename T> inline Vec4<T> operator+(const Vec4<T>& v, T s) { return v + s; }
template<typename T> inline Vec4<T> operator-(T s, const Vec4<T>& v) { return v - s; }
template<typename T> inline Vec4<T> operator-(const Vec4<T>& v, T s) { return v - s; }

/// ----- Complex overloads -----

template<class X> struct is_cplx : std::false_type {};
template<class T> struct is_cplx<Complex<T>> : std::true_type {};
template<class X> inline constexpr bool is_cplx_v = is_cplx<std::remove_cvref_t<X>>::value;

// Complex op scalar (scalar treated as real)
template<class T, class U> requires (!is_cplx_v<U>&& requires(T a, U b) { a + b; })
constexpr auto operator+(const Complex<T>& z, const U& s) -> Complex<plus_t<T, U>>
{
    // z + s  => (re+s, im)
    using R = plus_t<T, U>;
    return { R(z.x) + s, R(z.y) };
}
template<class T, class U> requires (!is_cplx_v<U>&& requires(T a, U b) { a - b; })
constexpr auto operator-(const Complex<T>& z, const U& s) -> Complex<minus_t<T, U>>
{
    // z - s  => (re-s, im)
    using R = minus_t<T, U>;
    return { R(z.x) - s, R(z.y) };
}
template<class T, class U> requires (!is_cplx_v<U>&& requires(T a, U b) { a* b; })
constexpr auto operator*(const Complex<T>& z, const U& s) -> Complex<mul_t<T, U>>
{
    // z * s  => (re*s, im*s)
    using R = mul_t<T, U>;
    return { R(z.x) * s, R(z.y) * s };
}
template<class T, class U> requires (!is_cplx_v<U>&& requires(T a, U b) { a / b; })
constexpr auto operator/(const Complex<T>& z, const U& s) -> Complex<div_t<T, U>>
{
    // z / s  => (re/s, im/s)
    using R = div_t<T, U>;
    return { R(z.x) / s, R(z.y) / s };
}

// scalar op Complex (scalar treated as real)
template<class T, class U> requires (!is_cplx_v<T>&& requires(T a, U b) { a + b; })
constexpr auto operator+(const T& s, const Complex<U>& z) -> Complex<plus_t<T, U>>
{
    // s + z  => (s+re, im)
    using R = plus_t<T, U>;
    return { s + R(z.x), R(z.y) };
}
template<class T, class U> requires (!is_cplx_v<T>&& requires(T a, U b) { a - b; })
constexpr auto operator-(const T& s, const Complex<U>& z) -> Complex<minus_t<T, U>>
{
    // s - z  => (s-re, -im)
    using R = minus_t<T, U>;
    return { s - R(z.x), -R(z.y) };
}
template<class T, class U> requires (!is_cplx_v<T>&& requires(T a, U b) { a* b; })
constexpr auto operator*(const T& s, const Complex<U>& z) -> Complex<mul_t<T, U>>
{
    // s * z  => (s*re, s*im)
    using R = mul_t<T, U>;
    return { s * R(z.x), s * R(z.y) };
}
template<class T, class U> requires (!is_cplx_v<T>&& requires(T a, U b) { a / b; a* b; a + b; a - b; })
constexpr auto operator/(const T& s, const Complex<U>& z) -> Complex<div_t<T, U>>
{
    // s / z  => s*(conj z)/|z|^2  => (s*re/den, -s*im/den)
    using R = div_t<T, U>;
    const auto denom = z.x * z.x + z.y * z.y;
    return { R(s) * R(z.x) / denom, -R(s) * R(z.y) / denom };
}

// true complex arithmetic
template<class T, class U> requires requires(T a, U b) { a + b; a - b; a* b; }
constexpr auto operator+(const Complex<T>& a, const Complex<U>& b) -> Complex<plus_t<T, U>>
{
    using R = plus_t<T, U>;
    return { R(a.x) + b.x, R(a.y) + b.y };
}
template<class T, class U> requires requires(T a, U b) { a - b; }
constexpr auto operator-(const Complex<T>& a, const Complex<U>& b) -> Complex<minus_t<T, U>>
{
    using R = minus_t<T, U>;
    return { R(a.x) - b.x, R(a.y) - b.y };
}
template<class T, class U> requires requires(T a, U b) { a* b; a + b; a - b; }
constexpr auto operator*(const Complex<T>& a, const Complex<U>& b) -> Complex<mul_t<T, U>>
{
    // (a+ai*i)(b+bi*i) = (ab - ai*bi) + (a*bi + ai*b)i
    using R = mul_t<T, U>;
    const R rx = R(a.x) * b.x - R(a.y) * b.y;
    const R ry = R(a.x) * b.y + R(a.y) * b.x;
    return { rx, ry };
}
template<class T, class U> requires requires(T a, U b) { a* b; a + b; a - b; a / b; }
constexpr auto operator/(const Complex<T>& a, const Complex<U>& b) -> Complex<div_t<T, U>>
{
    // (a+ai*i)/(b+bi*i) = ((a+ai*i)(b-bi*i)) / (b^2+bi^2)
    using R = div_t<T, U>;
    const auto denom = b.x * b.x + b.y * b.y;
    const R rx = (R(a.x) * b.x + R(a.y) * b.y) / denom;
    const R ry = (R(a.y) * b.x - R(a.x) * b.y) / denom;
    return { rx, ry };
}

/// ----- Matrix overloads -----

template<class T, class U> using mat_mul_t    = value_t<decltype(std::declval<T>() * std::declval<U>() + std::declval<T>() * std::declval<U>())>;
template<class T, class U> using scalar_mul_t = value_t<decltype(std::declval<T>() * std::declval<U>())>;
template<class T, class U> using scalar_div_t = value_t<decltype(std::declval<T>() / std::declval<U>())>;

// ================= typedefs =================

typedef std::vector<uint8_t>  bytebuf;

typedef Vec2<i16>          SVec2;
typedef Vec2<int>          IVec2;
typedef Vec2<f32>          FVec2;
typedef Vec2<f64>          DVec2;
typedef Vec2<fdd>         DDVec2;
typedef Vec2<fqd>         QDVec2;
                              
typedef Vec3<int>          IVec3;
typedef Vec3<f32>          FVec3;
typedef Vec3<f64>          DVec3;
typedef Vec3<fdd>         DDVec3;
typedef Vec3<fqd>         QDVec3;
                              
typedef Vec4<int>          IVec4;
typedef Vec4<f32>          FVec4;
typedef Vec4<f64>          DVec4;
typedef Vec4<fdd>         DDVec4;
typedef Vec4<fqd>         QDVec4;

typedef Mat2<f32>          FMat2;
typedef Mat2<f64>          DMat2;
typedef Mat2<fdd>         DDMat2;
typedef Mat2<fqd>         QDMat2;

typedef Mat3<f32>          FMat3;
typedef Mat3<f64>          DMat3;
typedef Mat3<fdd>         DDMat3;
typedef Mat3<fqd>         QDMat3;

typedef Mat4<f32>          FMat4;
typedef Mat4<f64>          DMat4;
typedef Mat4<fdd>         DDMat4;
typedef Mat4<fqd>         QDMat4;

typedef Range<int>         IRange;
typedef Range<f32>         FRange;
typedef Range<f64>         DRange;
typedef Range<fdd>        DDRange;
typedef Range<fqd>        QDRange;
                              
typedef Segment<int>       ISegment;
typedef Segment<f32>       FSegment;
typedef Segment<f64>       DSegment;
typedef Segment<fdd>      DDSegment;
typedef Segment<fqd>      QDSegment;
                              
typedef Rect<int>          IRect;
typedef Rect<f32>          FRect;
typedef Rect<f64>          DRect;
typedef Rect<fdd>         DDRect;
typedef Rect<fqd>         QDRect;
                              
typedef Quad<int>          IQuad;
typedef Quad<f32>          FQuad;
typedef Quad<f64>          DQuad;
typedef Quad<fdd>         DDQuad;
typedef Quad<fqd>         QDQuad;
                              
typedef Ray<f32>           FRay;
typedef Ray<f64>           DRay;
typedef Ray<fdd>          DDRay;
typedef Ray<fqd>          QDRay;
                              
typedef AngledRect<f64>    DAngledRect;
typedef AngledRect<fdd>   DDAngledRect;
typedef AngledRect<fqd>   QDAngledRect;


// ================= statitic layout tests =================

template<class T>
inline void _vec2_tests()
{
    static_assert(std::is_standard_layout_v<Vec2<T>>, "Vec2<T> must be standard layout");
    static_assert(sizeof(Vec2<T>) == 2 * sizeof(T), "Vec2<T> must be exactly two Ts");
    static_assert(offsetof(Vec2<T>, x) == 0);
    static_assert(offsetof(Vec2<T>, y) == sizeof(T));
}

inline void _type_tests() {
    static_assert(std::is_trivially_constructible_v<fdd>, "fdd must be trivially constructible");
    static_assert(std::is_trivially_constructible_v<fqd>, "fqd must be trivially constructible");

    _vec2_tests<f32>();
    _vec2_tests<f64>();
    _vec2_tests<fdd>();
    _vec2_tests<fqd>();
}

struct VecTests
{
    VecTests() { _type_tests(); }
};
static inline VecTests tests;


// ================= core utilities =================

// nth_type<N, Ts...> -> the Nth type from Ts...
template<std::size_t N, class T0, class... Ts> struct nth_type : nth_type<N - 1, Ts...> {};
template<class T0, class... Ts> struct nth_type<0, T0, Ts...> { using type = T0; };

// index_of<T, Ts...> -> first index of T, or npos
constexpr std::size_t type_npos = static_cast<std::size_t>(-1);

template<class T, class... Ts>
struct index_of_impl;

template<class T>
struct index_of_impl<T> { static constexpr std::size_t value = type_npos; };

template<class T, class U0, class... Us>
struct index_of_impl<T, U0, Us...> {
    static constexpr std::size_t tail = index_of_impl<T, Us...>::value;
    static constexpr std::size_t value =
        std::is_same_v<T, U0> ? 0 :
        (tail == type_npos ? type_npos : 1 + tail);
};

template<class T, class... Ts>
inline constexpr std::size_t index_of_v = index_of_impl<T, Ts...>::value;

// ----- lazy machinery to avoid instantiating the "discarded" branch -----
template<class T> struct type_identity { using type = T; };

template<bool, class Then, class Else> struct lazy_cond;
template<class Then, class Else>     struct lazy_cond<true,  Then, Else> { using type = typename Then::type; };
template<class Then, class Else>     struct lazy_cond<false, Then, Else> { using type = typename Else::type; };
template<std::size_t N, class... Ts> struct lazy_nth { using type = typename nth_type<N, Ts...>::type; };

// ================= upgrade / downgrade =================

template<class T, class... Ladder>
struct upgrade_type {
private:
    static constexpr std::size_t idx = index_of_v<T, Ladder...>;
    static constexpr std::size_t N = sizeof...(Ladder);
    static constexpr bool can_up = (idx != type_npos) && (idx + 1 < N);
public:
    using type = typename lazy_cond<
        can_up,
        lazy_nth<idx + 1, Ladder...>,
        type_identity<T>
    >::type;
};
template<class T, class... Ladder>
using upgrade_type_t = typename upgrade_type<T, Ladder...>::type;

template<class T, class... Ladder>
struct downgrade_type {
private:
    static constexpr std::size_t idx = index_of_v<T, Ladder...>;
    static constexpr bool        can_down = (idx != type_npos) && (idx > 0);
public:
    using type = typename lazy_cond<
        can_down,
        lazy_nth<idx - 1, Ladder...>,
        type_identity<T>
    >::type;
};
template<class T, class... Ladder>
using downgrade_type_t = typename downgrade_type<T, Ladder...>::type;

// ================= type_list + adapters =================

template<class... Ts>
struct type_list {};

template<template<class, class...> class Target, class List>
struct expand;

template<template<class, class...> class Target, class... Ts>
struct expand<Target, type_list<Ts...>> {
    template<class T>
    using into_t = typename Target<T, Ts...>::type;
};

// List-based adapters (nicer syntax)
template<class T, class List>
struct upgrade_from_list;
template<class T, class... Ts>
struct upgrade_from_list<T, type_list<Ts...>> {
    using type = upgrade_type_t<T, Ts...>;
};
template<class T, class List>
using upgrade_from_list_t = typename upgrade_from_list<T, List>::type;

template<class T, class List>
struct downgrade_from_list;
template<class T, class... Ts>
struct downgrade_from_list<T, type_list<Ts...>> {
    using type = downgrade_type_t<T, Ts...>;
};
template<class T, class List>
using downgrade_from_list_t = typename downgrade_from_list<T, List>::type;

// type_index

// type_index primary template
template<class List, class T>
struct type_index;

// recursive helper
template<class T, std::size_t I, class... Ts>
struct type_index_impl;

template<class T, std::size_t I, class U, class... Us>
struct type_index_impl<T, I, U, Us...>
    : std::conditional_t<
    std::is_same_v<T, U>,
    std::integral_constant<std::size_t, I>,
    type_index_impl<T, I + 1, Us...>
    >
{
};

template<class T, std::size_t I, class U>
struct type_index_impl<T, I, U>
{
    static_assert(std::is_same_v<T, U>, "type_index: type not found in type_list");
    using type = std::integral_constant<std::size_t, I>;
};

// type_index specialization for type_list
template<class... Ts, class T>
struct type_index<type_list<Ts...>, T>
    : type_index_impl<T, 0, Ts...>::type
{
};

// optional convenience value
template<class List, class T>
inline constexpr std::size_t type_index_v = type_index<List, T>::value;



// ================= rebinding class templates =================
// Upgrade/downgrade the FIRST type parameter of a class-template instance.

template<class Instance, class List>
struct upgrade_template;

template<template<class, class...> class C, class T, class... Args, class... Ladder>
struct upgrade_template<C<T, Args...>, type_list<Ladder...>> {
    using type = C<upgrade_type_t<T, Ladder...>, Args...>;
};
template<class Instance, class List>
using upgrade_template_t = typename upgrade_template<Instance, List>::type;

template<class Instance, class List>
struct downgrade_template;

template<template<class, class...> class C, class T, class... Args, class... Ladder>
struct downgrade_template<C<T, Args...>, type_list<Ladder...>> {
    using type = C<downgrade_type_t<T, Ladder...>, Args...>;
};
template<class Instance, class List>
using downgrade_template_t = typename downgrade_template<Instance, List>::type;

// =================  ladders =================

using float_types        = type_list<f32, f64, fdd, fqd>;
using signed_int_types   = type_list<i8, i16, i32, i64>;
using unsigned_int_types = type_list<u8, u16, u32, u64>;

using vec2_types = type_list<FVec2, DVec2, DDVec2>;
using vec3_types = type_list<FVec3, DVec3, DDVec3>;
using vec4_types = type_list<FVec4, DVec4, DDVec4>;

template<class T>
inline constexpr FloatType float_index_v = (FloatType)type_index<float_types, value_t<T>>::value;

// ================= float type comparisons =================

template<class A, class B>
struct max_float {
private:
    using list = float_types;
    static constexpr std::size_t ia = index_of_v<A, f32, f64, fdd, fqd>;
    static constexpr std::size_t ib = index_of_v<B, f32, f64, fdd, fqd>;
    static constexpr bool valid = (ia != type_npos) && (ib != type_npos);
public:
    using type = std::conditional_t<
        valid,
        typename nth_type<(ia > ib ? ia : ib), f32, f64, fdd, fqd>::type,
        void
    >;
};
template<class A, class B>
using max_float_t = typename max_float<A, B>::type;

template<class A, class B>
struct min_float {
private:
    using list = float_types;
    static constexpr std::size_t ia = index_of_v<A, f32, f64, fdd, fqd>;
    static constexpr std::size_t ib = index_of_v<B, f32, f64, fdd, fqd>;
    static constexpr bool valid = (ia != type_npos) && (ib != type_npos);
public:
    using type = std::conditional_t<
        valid,
        typename nth_type<(ia < ib ? ia : ib), f32, f64, fdd, fqd>::type,
        void
        >;
};
template<class A, class B>
using min_float_t = typename min_float<A, B>::type;

// ================= upgrade/downgrade =================


// integers
template<class T> using upgrade_int_t          = upgrade_from_list_t<T, signed_int_types>;
template<class T> using downgrade_int_t        = downgrade_from_list_t<T, signed_int_types>;
template<class T> using upgrade_uint_t         = upgrade_from_list_t<T, unsigned_int_types>;
template<class T> using downgrade_uint_t       = downgrade_from_list_t<T, unsigned_int_types>;

// floats
template<class T> using upgrade_float_t        = upgrade_from_list_t<T, float_types>;
template<class T> using downgrade_float_t      = downgrade_from_list_t<T, float_types>;

// vec
template<class T> using downgrade_vec2_float_t = downgrade_template_t<Vec2<T>, float_types>;
template<class T> using downgrade_vec3_float_t = downgrade_template_t<Vec3<T>, float_types>;
template<class T> using downgrade_vec4_float_t = downgrade_template_t<Vec4<T>, float_types>;

template<class T> using upgrade_vec2_float_t   = upgrade_template_t<Vec2<T>, float_types>;
template<class T> using upgrade_vec3_float_t   = upgrade_template_t<Vec3<T>, float_types>;
template<class T> using upgrade_vec4_float_t   = upgrade_template_t<Vec4<T>, float_types>;
                                                   
template<class T> using upgrade_vec2_int_t     = upgrade_template_t<Vec2<T>, signed_int_types>;
template<class T> using upgrade_vec3_int_t     = upgrade_template_t<Vec3<T>, signed_int_types>;
template<class T> using upgrade_vec4_int_t     = upgrade_template_t<Vec4<T>, signed_int_types>;
                                                   
template<class T> using downgrade_vec2_int_t   = upgrade_template_t<Vec2<T>, signed_int_types>;
template<class T> using downgrade_vec3_int_t   = upgrade_template_t<Vec3<T>, signed_int_types>;
template<class T> using downgrade_vec4_int_t   = upgrade_template_t<Vec4<T>, signed_int_types>;


template<class F, class... Args> using return_type = std::invoke_result_t<std::decay_t<F>, Args...>;



// Global std::ostream overloads

///template<typename T> std::ostream& operator<<(std::ostream& os, const bl::Vec2<T>& vec) {
///    return os << "(x: " << vec.x << ", y: " << vec.y << ")";
///}

template<typename T> std::ostream& operator<<(std::ostream& os, const bl::Vec3<T>& vec) {
    return os << "(x: " << vec.x << ", y: " << vec.y << ", z: " << vec.z << ")";
}

template<typename T> std::ostream& operator<<(std::ostream& os, const bl::Vec4<T>& vec) {
    return os << "(x: " << vec.x << ", y: " << vec.y << ", z: " << vec.z << ", w: " << vec.w << ")";
}

template<typename T> std::ostream& operator<<(std::ostream& os, const bl::Quad<T>& q) {
    return os << "{a: " << q.a << ", b: " << q.b << ", c: " << q.c << ", d: " << q.d << "}";
}

template<typename T> std::ostream& operator<<(std::ostream& os, const bl::AngledRect<T>& r) {
    return os << "{cx: " << r.cx << ", cy: " << r.cy << ", w: " << r.w << ", h: " << r.h << "rot: " << r.angle << "}";
}

BL_END_NS

// Convenience macro to enable bitops for one enum type
#define bl_enable_enum_bitops(E) \
    template<> struct enable_enum_bitops<E> : std::true_type {}

template<class E>
struct enable_enum_bitops : std::false_type {};

template<class E>
concept bitmask_enum = std::is_enum_v<E> && enable_enum_bitops<E>::value;

template<bitmask_enum E>
using enum_underlying_t = std::underlying_type_t<E>;

// -------- assignment ops: E op= E --------
template<bitmask_enum E>
constexpr E& operator|=(E& a, E b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
    return a;
}
template<bitmask_enum E>
constexpr E& operator&=(E& a, E b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
    return a;
}
template<bitmask_enum E>
constexpr E& operator^=(E& a, E b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));
    return a;
}

// -------- assignment ops: E op= I (integral RHS) --------
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr E& operator|=(E& a, I b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
    return a;
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr E& operator&=(E& a, I b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
    return a;
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr E& operator^=(E& a, I b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));
    return a;
}

// -------- assignment ops: I op= E (integral LHS) --------
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr I& operator|=(I& a, E b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<I>(static_cast<I>(a) | static_cast<I>(static_cast<U>(b)));
    return a;
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr I& operator&=(I& a, E b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<I>(static_cast<I>(a) & static_cast<I>(static_cast<U>(b)));
    return a;
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr I& operator^=(I& a, E b) noexcept {
    using U = enum_underlying_t<E>;
    a = static_cast<I>(static_cast<I>(a) ^ static_cast<I> (static_cast<U>(b)));
    return a;
}

// -------- non-assignment ops: E op E --------
template<bitmask_enum E>
constexpr E operator|(E a, E b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template<bitmask_enum E>
constexpr E operator&(E a, E b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template<bitmask_enum E>
constexpr E operator^(E a, E b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));
}
template<bitmask_enum E>
constexpr E operator~(E a) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(~static_cast<U>(a));
}

// -------- non-assignment mixed: E op I, I op E --------
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr E operator|(E a, I b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr E operator&(E a, I b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr E operator^(E a, I b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));
}

template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr I operator|(I a, E b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<I>(static_cast<I>(a) | static_cast<I>(static_cast<U>(b)));
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr I operator&(I a, E b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<I>(static_cast<I> (a) & static_cast<I>(static_cast<U>(b)));
}
template<bitmask_enum E, class I>
    requires std::is_integral_v<I>
constexpr I operator^(I a, E b) noexcept {
    using U = enum_underlying_t<E>;
    return static_cast<I>(static_cast<I> (a) ^ static_cast<I>(static_cast<U>(b)));
}


// handy fixed-bit enums for constexpr dispatch tables
enum struct Enum_0_2 : uint8_t { _0, _1, _2, COUNT };
enum struct Enum_0_3 : uint8_t { _0, _1, _2, _3, COUNT };
enum struct Enum_0_4 : uint8_t { _0, _1, _2, _3, _4, COUNT };
enum struct Enum_0_5 : uint8_t { _0, _1, _2, _3, _4, _5, COUNT };
enum struct Enum_0_6 : uint8_t { _0, _1, _2, _3, _4, _5, _6, COUNT };
enum struct Enum_0_7 : uint8_t { _0, _1, _2, _3, _4, _5, _6, _7, COUNT };

bl_enable_enum_bitops(Enum_0_2);
bl_enable_enum_bitops(Enum_0_3);
bl_enable_enum_bitops(Enum_0_4);
bl_enable_enum_bitops(Enum_0_5);
bl_enable_enum_bitops(Enum_0_6);
bl_enable_enum_bitops(Enum_0_7);
