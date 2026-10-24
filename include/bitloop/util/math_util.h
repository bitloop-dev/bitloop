#pragma once
#include <numbers>
#include <sstream>
#include <iomanip>
#include <algorithm>

// todo: Make types a util, or only import fdd.h (and f64, etc)
#include <bitloop/core/types.h>

//#define SIMD_FORCE_SCALER
//#include <bitloop/util/fltx/simd.h>

BL_BEGIN_NS

namespace math
{
    // Numbers

    template<class T> inline constexpr T pi_v = std::numbers::pi_v<T>;
    template<> inline constexpr fdd pi_v<fdd> = fdd(3.141592653589793116, 1.2246467991473532072e-16);
    template<> inline constexpr fqd pi_v<fqd> = fqd{ 0x1.921fb54442d18p+1,  0x1.1a62633145c07p-53, -0x1.f1976b7ed8fbcp-109,  0x1.4cf98e804177dp-163 };

    inline constexpr float  pi_f       = (float)std::numbers::pi;
    inline constexpr float  tau_f      = (float)std::numbers::pi * 2.0f;
    inline constexpr float  half_pi_f  = (float)std::numbers::pi / 2.0f;
    inline constexpr float  inv_pi_f   = (float)std::numbers::inv_pi;
    inline constexpr float  inv_tau_f  = 1.0f / tau_f;
                                
    inline constexpr double pi         = std::numbers::pi;
    inline constexpr double tau        = std::numbers::pi * 2.0;
    inline constexpr double half_pi    = std::numbers::pi / 2.0;
    inline constexpr double inv_pi     = std::numbers::inv_pi;
    inline constexpr double inv_tau    = 1.0 / tau;

    // Functions

    template<std::integral T>
    [[nodiscard]] inline T roundDown(T v, T step) noexcept
    {
        return (v / step) * step;
    }

    template<std::integral T>
    [[nodiscard]] inline T roundUp(T v, T step) noexcept
    {
        return ((v + step - 1) / step) * step;
    }

    template<std::floating_point T>
    [[nodiscard]] inline T roundDown(T v, T step) noexcept
    {
        return std::floor(v / step) * step;
    }

    template<std::floating_point T>
    [[nodiscard]] inline T roundUp(T v, T step) noexcept
    {
        return std::ceil(v / step) * step;
    }

    template<typename T> [[nodiscard]] inline bool divisible(T _big, T _small, f64 tol_pct=0.0001)
    {
        static_assert(bl::is_arithmetic_v<T>, "divisible() only supports arithmetic types");
        if constexpr (bl::is_fltx_float_v<T> || std::is_floating_point_v<T>) // todo: was is_floating_point_v. Bring back for convenience?
        {
            T d = abs(fmod(_big, _small) - abs(_small));
            T t = abs(_small) * tol_pct;
            return d <= t;
        }
        else if constexpr (bl::is_integral_v<T>)
            return _small != 0 && (_big % _small == 0);
        return false;
    }

    [[nodiscard]] inline int countDecimalPlaces(double num)
    {
        // todo: Optimize
        std::ostringstream out;
        out << std::fixed << std::setprecision(10) << num;
        std::string str = out.str();

        size_t pos = str.find('.');
        if (pos == std::string::npos) return 0;
        return int(str.size() - pos - 1);
    }

    template<typename T>
    [[nodiscard]] inline T precisionFromDecimals(int count)
    {
        return pow(T{10.0}, (T)(-count));
    }

    [[nodiscard]] inline int countDigits(int n)
    {
        if (n == 0) return 1;

        // unsigned to avoid overflow on INT_MIN
        unsigned v = (n < 0)
            ? static_cast<unsigned>(-(static_cast<long long>(n)))
            : static_cast<unsigned>(n);

        return static_cast<int>(std::log10(static_cast<double>(v))) + 1;
    }
    
    template <typename T>
    [[nodiscard]] int countWholeDigits(T x)
    {
        static_assert(bl::is_fltx_float_v<T> || std::is_floating_point_v<T>, "Float must be a floating-point type");
        if (!bl::isfinite(x)) return 0;
        x = abs(x);
        if (x < T{ 1 }) return 1;
        return static_cast<int>(floor(log10(x))) + 1;
    }

    template<typename T, typename... Rest>
    [[nodiscard]] constexpr T avg(T first, Rest... rest)
    {
        T sum = (first + ... + rest);
        constexpr std::size_t count = 1 + sizeof...(rest);
        return sum / static_cast<T>(count);
    }

    template<typename T>
    [[nodiscard]] T wrap(T value, T min, T max)
    {
        T range = max - min;
        value = std::fmod(value - min, range);
        if (value < 0) value += range;
        return value + min;
    }

    template<typename T>
    [[nodiscard]] T wrap01(T value)
    {
        if (value >= T{0} && value < T{1}) return value;
        T y = value - std::floor(value);
        return y == T{0} ? T{0} : y;
    }

    // lerp from input a to log(a)
    template<typename T>
    [[nodiscard]] inline T linearLogLerp(T a, T lerp_factor)
    {
        return a + (log(a) - a) * lerp_factor;
    }

    // lerp from input a to log1p(a)
    template<typename T>
    [[nodiscard]] inline T linearLog1pLerp(T a, T lerp_factor)
    {
        return a + (log(1 + a) - a) * lerp_factor;
    }

    template<typename T>
    [[nodiscard]] inline T linearLogRangeLerp(T x, T a, T b, T lerp_factor)
    {
        return x + (log(((x - a) / (b - a))) - x) * lerp_factor;
    }

    // Ratios (a->b,  0->1)
    template<typename T> [[nodiscard]] constexpr T ratio(T a, T b) { return ((b-a)/a); }
    template<typename T> [[nodiscard]] constexpr T absRatio(T a, T b) { return abs((b-a)/a); }
    template<typename T> [[nodiscard]] constexpr T avgRatio(T a, T b) { return ((a-b)/((a+b)/T{2})); }
    template<typename T> [[nodiscard]] constexpr T absAvgRatio(T a, T b) { return (abs(a-b)/((a+b)/T{2})); }

    // Percentages (a->b,  0->100)
    template<typename T> [[nodiscard]] constexpr T pct(T a, T b) { return ((b-a)/a)*T{100}; }
    template<typename T> [[nodiscard]] constexpr T absPct(T a, T b) { return abs((b-a)/a)*T{100}; }
    template<typename T> [[nodiscard]] constexpr T avgPct(T a, T b) { return (((a-b)/((a+b)/T{2}))*T{100}); }
    template<typename T> [[nodiscard]] constexpr T absAvgPct(T a, T b) { return (abs(a-b)/((a+b)/T{2}))*T{100}; }

    // Coordinate offset rotation
    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> rotateOffset(T dx, T dy, U rotation)
    {
        T _cos = T(bl::cos(rotation));
        T _sin = T(bl::sin(rotation));
        return {
            (dx * _cos - dy * _sin),
            (dy * _cos + dx * _sin)
        };
    }

    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> rotateOffset(T dx, T dy, U _cos, U _sin)
    {
        return {
            (dx * _cos - dy * _sin),
            (dy * _cos + dx * _sin)
        };
    }
    
    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> rotateOffset(const Vec2<T>& offset, U rotation)
    {
        T _cos = T(bl::cos(rotation));
        T _sin = T(bl::sin(rotation));
        return {
            (offset.x * _cos - offset.y * _sin),
            (offset.y * _cos + offset.x * _sin)
        };
    }

    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> rotateOffset(const Vec2<T>& offset, U _cos, U _sin)
    {
        return {
            (offset.x * _cos - offset.y * _sin),
            (offset.y * _cos + offset.x * _sin)
        };
    }

    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> reverseRotateOffset(T dx, T dy, U rotation)
    {
        T _cos = T(bl::cos(rotation));
        T _sin = T(bl::sin(rotation));
        return {
            (dx * _cos + dy * _sin),
            (dy * _cos - dx * _sin)
        };
    }

    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> reverseRotateOffset(T dx, T dy, U _cos, U _sin)
    {
        return {
            (dx * _cos + dy * _sin),
            (dy * _cos - dx * _sin)
        };
    }

    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> reverseRotateOffset(const Vec2<T>& offset, U rotation)
    {
        T _cos = T(bl::cos(rotation));
        T _sin = T(bl::sin(rotation));
        return {
            (offset.x * _cos + offset.y * _sin),
            (offset.y * _cos - offset.x * _sin)
        };
    }

    template<typename T, typename U>
    [[nodiscard]] inline Vec2<T> reverseRotateOffset(const Vec2<T>& offset, U _cos, U _sin)
    {
        return {
            (offset.x * _cos + offset.y * _sin),
            (offset.y * _cos - offset.x * _sin)
        };
    }

    // Angles
    template<typename T> [[nodiscard]] constexpr T toRadians(T degrees) { return degrees * pi_v<T> / T{ 180 }; }
    template<typename T> [[nodiscard]] constexpr T toDegrees(T radians) { return radians * T{ 180 } / pi_v<T>; }

    template<typename T> [[nodiscard]] inline T closestAngleDifference(T angle, T target_angle)
    {
        T diff = fmod((target_angle - angle) + pi_v<T>, pi_v<T>*T{2});
        if (diff < T{0}) diff += pi_v<T>*T{2};
        return diff - pi_v<T>;
    }

    template<typename T>
    [[nodiscard]] inline T wrapRadians2PI(T a) noexcept
    {
        const T two_pi = pi_v<T> *T{ 2 };
        return a - two_pi * floor(a / two_pi);
    }

    template<typename T>
    [[nodiscard]] inline T wrapRadians(T a) noexcept
    {
        const T pi = pi_v<T>;
        const T two_pi = pi * T{ 2 };
        return a - two_pi * floor((a + pi) / two_pi);
    }

    template<typename T, typename S> [[nodiscard]] inline T lerpAngle(T a, T b, S f) { return wrapRadians(a + T{f}*closestAngleDifference(a, b)); }
    template<typename T> [[nodiscard]] inline T avgAngle(T a, T b) { return lerpAngle(a, b, T{ 0.5 }); }
    template<typename T> constexpr T avgAngle(const std::vector<T>& angles)
    {
        T s = 0, c = 0;
        for (T a : angles) {
            s += bl::sin(a);
            c += bl::cos(a);
        }

        // If angles are very spread out, resultant length can be ~0 and mean is undefined/unstable.
        const T r2 = s * s + c * c;
        if (r2 < 1e-24) return 0;

        return bl::atan2(s, c);
    }

    // todo: Add support for "Segment" class, perhaps move to those classes for:
    // >  Ray     --> Ray      intersection
    // >  Segment --> Segment  intersection
    // 
    // >  Ray <-> Segment      intersection
    // >  Ray <-> Quad         intersection
    // >  Ray <-> Rect         intersection
    // >  Ray <-> AngledRect   intersection

    // Intersections
    inline bool lineEqIntersect(DVec2* targ, const DRay& ray1, const DRay& ray2, bool bidirectional)
    {
        // Unit direction vectors for each ray
        double d1x = std::cos(ray1.angle), d1y = std::sin(ray1.angle);
        double d2x = std::cos(ray2.angle), d2y = std::sin(ray2.angle);

        // Origins
        double x1 = ray1.x, y1 = ray1.y;
        double x2 = ray2.x, y2 = ray2.y;

        // Denom of the 2x2 system
        double denom = d1x * d2y - d1y * d2x;

        // Are rays parallel?
        if (std::fabs(denom) < 1e-9)
            return false;

        // Compute t and u such that:  ray1.origin + t*d1 = ray2.origin + u*d2
        double dx = x2 - x1;
        double dy = y2 - y1;
        double t = (dx * d2y - dy * d2x) / denom;
        double u = (dx * d1y - dy * d1x) / denom;

        // If restricting to the forward direction, both t and u must be nonnegative.
        if (!bidirectional)
        {
            if (t < 0 || u < 0)
                return false;
        }

        // Compute intersection point.
        *targ = Vec2(x1 + t * d1x, y1 + t * d1y);
        return true;
    }
    inline bool rayRectIntersection(DVec2* back_intersect, DVec2* foward_intersect, const DRect& r, const DRay& ray)
    {
        // Normalize rect boundaries
        double minX = std::min(r.x1, r.x2);
        double maxX = std::max(r.x1, r.x2);
        double minY = std::min(r.y1, r.y2);
        double maxY = std::max(r.y1, r.y2);

        // DRay origin and direction
        double rx = ray.x;
        double ry = ray.y;
        double dx = std::cos(ray.angle);
        double dy = std::sin(ray.angle);

        // Aaccumulate candidate intersections as (t, point) pairs
        struct IntersectionCandidate
        {
            double t;
            DVec2 pt;
        };

        std::vector<IntersectionCandidate> candidates;
        const double eps = 1e-9;

        // --- Check intersection with vertical edges ---

        // Left edge: x = minX
        if (std::fabs(dx) > eps) {
            double t = (minX - rx) / dx;
            double y_int = ry + t * dy;
            if (y_int >= minY - eps && y_int <= maxY + eps)
                candidates.push_back({ t, Vec2(minX, y_int) });
        }

        // Right edge: x = maxX
        if (std::fabs(dx) > eps) {
            double t = (maxX - rx) / dx;
            double y_int = ry + t * dy;
            if (y_int >= minY - eps && y_int <= maxY + eps)
                candidates.push_back({ t, Vec2(maxX, y_int) });
        }

        // --- Check intersection with horizontal edges ---

        // Bottom edge: y = minY
        if (std::fabs(dy) > eps) {
            double t = (minY - ry) / dy;
            double x_int = rx + t * dx;
            if (x_int >= minX - eps && x_int <= maxX + eps) {
                candidates.push_back({ t, Vec2(x_int, minY) });
            }
        }

        // Top edge: y = maxY
        if (std::fabs(dy) > eps)
        {
            double t = (maxY - ry) / dy;
            double x_int = rx + t * dx;
            if (x_int >= minX - eps && x_int <= maxX + eps)
                candidates.push_back({ t, Vec2(x_int, maxY) });
        }

        // We expect exactly 2 intersection points
        if (candidates.size() < 2)
            return false;

        // Sort candidates by their t-value.
        std::sort(candidates.begin(), candidates.end(), [](
            const IntersectionCandidate& a,
            const IntersectionCandidate& b)
        {
            return a.t < b.t;
        });

        // Remove duplicates: if two candidate intersections have nearly the same t, keep only one.
        std::vector<IntersectionCandidate> unique;
        unique.push_back(candidates.front());
        for (size_t i = 1; i < candidates.size(); ++i)
        {
            if (std::fabs(candidates[i].t - unique.back().t) > eps)
                unique.push_back(candidates[i]);
        }

        if (unique.size() != 2)
        {
            // In degenerate cases (e.g. ray exactly tangent to the rectangle)
            return false;
        }

        // Order the two intersections:
        // The one with the smaller t is the "back" intersection (behind the ray’s origin)
        // and the one with the larger t is the "foward" intersection.
        *back_intersect = unique[0].pt;
        *foward_intersect = unique[1].pt;
        return true;
    }

    // Lerp
    template<typename ValueT, typename Scalar>
    [[nodiscard]] constexpr ValueT lerp(const ValueT& a, const ValueT& b, Scalar factor)
    {
        return (ValueT)(a + (b - a) * factor);
    }
    
    template<typename ValueT, typename Scalar>
    [[nodiscard]] inline Rect<ValueT> lerp(const Rect<ValueT>& src, const Rect<ValueT>& targ, Scalar factor)
    {
        Rect<ValueT> ret = src;
        ret.x1 += (targ.x1 - src.x1) * factor;
        ret.y1 += (targ.y1 - src.y1) * factor;
        ret.x2 += (targ.x2 - src.x2) * factor;
        ret.y2 += (targ.y2 - src.y2) * factor;
        return ret;
    }

    template<typename ValueT, typename Scalar>
    [[nodiscard]] inline AngledRect<ValueT> lerp(const AngledRect<ValueT>& src, const AngledRect<ValueT>& targ, Scalar factor)
    {
        AngledRect<ValueT> ret;
        ret.cen = lerp(src.cen, targ.cen, factor);
        ret.size = lerp(src.size, targ.size, factor);
        ret.angle = lerpAngle(src.angle, targ.angle, factor);
        return ret;
    }

    // inverse lerp: get factor from value and range
    template<typename T>
    [[nodiscard]] inline T lerpFactor(T value, T min, T max) {
        return ((value - min) / (max - min));
    }

    template<typename T>
    [[nodiscard]] inline T lerpFactorClamped(T value, T min, T max) {
        T f = ((value - min) / (max - min));
        if (f < 0) return 0;
        if (f > 1) return 1;
        return f;
    }

    // Catmull–Rom spline lerp
    template<class T>
    inline Vec2<T> arcLerp(float t, const Vec2<T>& a, const Vec2<T>& b, const Vec2<T>& c)
    {
        // todo: use newton steps to improve performance

        using Vec2 = Vec2<T>;
        if (t <= 0.0f) { return a; }
        if (t >= 1.0f) { return c; }

        auto eval_seg = [&](int seg, T u) -> Vec2 {
            const Vec2 P0 = (seg == 0) ? (a + (a - b)) : a;
            const Vec2 P1 = (seg == 0) ? a : b;
            const Vec2 P2 = (seg == 0) ? b : c;
            const Vec2 P3 = (seg == 0) ? c : (c + (c - b));
            const T u2 = u * u, u3 = u2 * u;
            return (P1 * T(2)
                + (P2 - P0) * u
                + (P0 * T(2) - P1 * T(5) + P2 * T(4) - P3) * u2
                + (-P0 + P1 * T(3) - P2 * T(3) + P3) * u3) * T(0.5);
        };

        constexpr int S = 64;
        auto seg_length = [&](int seg, T cum[S + 1], Vec2 pts[S + 1]) -> T {
            T L = 0;
            pts[0] = eval_seg(seg, 0);
            cum[0] = 0.0f;
            for (int i = 1; i <= S; ++i) {
                T u = T(i) / T(S);
                pts[i] = eval_seg(seg, u);
                L += len(pts[i] - pts[i - 1]);
                cum[i] = L;
            }
            return L;
        };

        T cum0[S + 1], cum1[S + 1];
        Vec2  pts0[S + 1], pts1[S + 1];

        const T L0 = seg_length(0, cum0, pts0);
        const T L1 = seg_length(1, cum1, pts1);
        const T L = L0 + L1;

        if (L <= 0.0f) { return b; }

        const T target = t * L;

        // choose segment and invert the sampled cumulative length
        int   seg = (target <= L0) ? 0 : 1;
        T want = (seg == 0) ? target : (target - L0);

        T* cum = (seg == 0) ? cum0 : cum1;
        Vec2* pts = (seg == 0) ? pts0 : pts1;

        int i = 1;
        while (i <= S && cum[i] < want) ++i;
        if (i > S) { return pts[S]; }

        const T c0 = cum[i - 1], c1 = cum[i];
        const T w = (c1 > c0) ? ((want - c0) / (c1 - c0)) : 0;

        // linearly blend between the two sampled points for final position
        Vec2 d = pts[i - 1] + (pts[i] - pts[i - 1]) * w;
        return d;
    }

    template<typename T>
    std::vector<Vec2<T>> delaunayMeshEllipse(
        T cx, T cy,
        T ellipse_r,
        T spacing_r,
        T dist_pow
    )
    {
        static_assert(std::is_floating_point_v<T>, "T must be a floating type");

        const T dx = T(2) * spacing_r;
        const T dy = std::sqrt(T(3)) * spacing_r;

        // Separate bounds for rows/cols so the hex grid fully covers the disk
        const int max_row = int(std::ceil(ellipse_r / dy)) + 2;
        const int max_col = int(std::ceil(ellipse_r / dx)) + 2;

        std::vector<Vec2<T>> ret;
        ret.reserve((2 * max_row + 1) * (2 * max_col + 1));

        for (int row = -max_row; row <= max_row; ++row)
        {
            const T y = T(row) * dy;
            const T ox = (row & 1) ? spacing_r : T(0);

            for (int col = -max_col; col <= max_col; ++col)
            {
                const T x = ox + T(col) * dx;

                const T r = std::hypot(x, y);
                if (r > ellipse_r) continue;

                if (r <= std::numeric_limits<T>::epsilon())
                {
                    ret.push_back({ cx, cy });
                    continue;
                }

                // u in [0,1]; warp u -> u^{dist_pow}; keep angle the same
                const T u = r / ellipse_r;
                const T scale = std::pow(u, dist_pow - T(1)); // r' = scale * r = ellipse_r * u^{dist_pow}
                ret.push_back({ cx + x * scale, cy + y * scale });
            }
        }
        return ret;
    }


    template<typename T>
    class SMA
    {
        int ma_length = 0;
        std::vector<T> samples;   // ring buffer storage
        std::size_t head = 0;     // index of the oldest sample to overwrite next
        std::size_t count = 0;    // number of valid samples in buffer (<= ma_length)
        T sum{};

    public:

        SMA(int length=1) : ma_length(length)
        {
            setLength(length);
        }

        void setLength(int length)
        {
            ma_length = length;

            if (ma_length < 1) ma_length = 1;
            samples.resize(static_cast<std::size_t>(ma_length)); // fixed size
        }

        T push(T v)
        {
            if (count < static_cast<std::size_t>(ma_length))
            {
                // warming up: append into the next slot
                const std::size_t idx = (head + count) % static_cast<std::size_t>(ma_length);
                samples[idx] = v;
                sum += v;
                ++count;
            }
            else
            {
                // full: overwrite oldest (head)
                sum -= samples[head];
                samples[head] = v;
                sum += v;

                head = (head + 1) % static_cast<std::size_t>(ma_length);
            }

            return average();
        }

        void clear()
        {
            sum = T{};
            head = 0;
            count = 0;
        }

        [[nodiscard]] T average() const
        {
            if (count == 0) return T{};
            return sum / (double)(count);
        }
    };

    template<typename T>
    class EMA
    {
        int ma_length = 0;   // smoothing length
        T alpha{};           // smoothing factor
        T value{};           // current EMA
        bool has_value = false;

    public:
        EMA(int length) : ma_length(length)
        {
            if (ma_length < 1) ma_length = 1;
            alpha = T(2) / (T(ma_length) + T(1));
        }

        T push(T v)
        {
            if (!has_value)
            {
                value = v; // first sample
                has_value = true;
                return value;
            }

            value = value + alpha * (v - value);
            return value;
        }

        void clear()
        {
            value = T{};
            has_value = false;
        }

        [[nodiscard]] T average() const
        {
            return has_value ? value : T{};
        }

        [[nodiscard]] bool ready() const
        {
            return has_value;
        }

        [[nodiscard]] int length() const
        {
            return ma_length;
        }

        [[nodiscard]] T smoothing() const
        {
            return alpha;
        }
    };

}

BL_END_NS
