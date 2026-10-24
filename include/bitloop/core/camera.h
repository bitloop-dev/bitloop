#pragma once
#include "types.h"
#include "debug.h"
#include "input.h"
#include <bitloop/util/math_util.h>

BL_BEGIN_NS

/*inline DDMat3 dd_translate(bl::fdd tx, bl::fdd ty)
{
    DDMat3 m = DDMat3::identity(1.0_dd);
    m(0, 2) = tx;
    m(1, 2) = ty;
    return m;
}

inline DDMat3 dd_scale(fdd sx, fdd sy)
{
    DDMat3 m = DDMat3::identity(1.0_dd);
    m(0, 0) = sx;
    m(1, 1) = sy;
    return m;
}

inline DDMat3 dd_rotate(fdd r)
{
    fdd c = cos(r);
    fdd s = sin(r);

    DDMat3 m = DDMat3::identity(1.0_dd);
    m(0, 0) = c;  m(0, 1) = -s;
    m(1, 0) = s;  m(1, 1) = c;
    return m;
}*/

template<typename T>
inline Mat3<T> mat_translate(T tx, T ty)
{
    Mat3<T> m = Mat3<T>::identity(T{ 1.0 });
    m(0, 2) = tx;
    m(1, 2) = ty;
    return m;
}

template<typename T>
inline Mat3<T> mat_scale(T tx, T ty)
{
    Mat3<T> m = Mat3<T>::identity(T{ 1.0 });
    m(0, 0) = (T)tx;
    m(1, 1) = (T)ty;
    return m;
}

template<typename T>
inline Mat3<T> mat_rotate(f64 r)
{
    const f64 c = std::cos(r);
    const f64 s = std::sin(r);

    Mat3<T> m = Mat3<T>::identity(T{ 1.0 });
    m(0, 0) = (T)c;  m(0, 1) = (T)-s;
    m(1, 0) = (T)s;  m(1, 1) = (T)c;
    return m;
}

class SurfaceInfo;
class CameraInfo;
class Viewport;
class ProjectBase;
class Event;

/*
class WorldStageTransform
{
    friend class CameraInfo;

    // ────── world <-> stage ──────
    QDMat3 m256     = QDMat3(1_qd);
    QDMat3 inv_m256 = QDMat3(1_qd);

    DDMat3 m128     = DDMat3(1_dd);
    DDMat3 inv_m128 = DDMat3(1_dd);

    DMat3 m64     = DMat3(1.0);
    DMat3 inv_m64 = DMat3(1.0);

    FloatType max_precision = FloatType::F64;

    void updateCache()
    {
        if (max_precision >= FloatType::FQD)
        {
            inv_m256 = m256.inversed();

            // downcast chain: fqd -> fdd -> f64
            m128     = static_cast<DDMat3>(m256);
            inv_m128 = static_cast<DDMat3>(inv_m256);
            m64      = static_cast<DMat3>(m128);
            inv_m64  = static_cast<DMat3>(inv_m128);
            return;
        }

        if (max_precision >= FloatType::FDD)
        {
            inv_m128 = m128.inversed();

            m64     = static_cast<DMat3>(m128);
            inv_m64 = static_cast<DMat3>(inv_m128);

            // keep higher caches coherent even when using fdd as max precision
            m256     = static_cast<QDMat3>(m128);
            inv_m256 = static_cast<QDMat3>(inv_m128);
            return;
        }

        inv_m64 = m64.inversed();

        // keep higher caches coherent even when using f64 as max precision
        m128     = static_cast<DDMat3>(m64);
        inv_m128 = static_cast<DDMat3>(inv_m64);
        m256     = static_cast<QDMat3>(m128);
        inv_m256 = static_cast<QDMat3>(inv_m128);
    }

public:

    // precision
    [[nodiscard]] FloatType maxPrecision() const { return max_precision; }
    void setMaxPrecision(FloatType p) { max_precision = p; }

    // transforms
    void reset()
    {
        if (max_precision >= FloatType::FQD)
            m256 = QDMat3(1.0_qd);
        else if (max_precision >= FloatType::FDD)
            m128 = DDMat3(1.0_dd);
        else
            m64 = DMat3(1.0);

        updateCache();
    }

    template<typename T>
    void transform(const Mat3<T>& m)
    {
        stageTransform<T>() *= static_cast<Mat3<T>>(m);
        updateCache();
    }

    template<typename T>
    void translate(T x, T y)
    {
        if (max_precision >= FloatType::FQD)
            m256 *= mat_translate<fqd>((fqd)x, (fqd)y);
        else if (max_precision >= FloatType::FDD)
            m128 *= mat_translate<fdd>(x, y);
        else
            m64 *= mat_translate<f64>((f64)x, (f64)y);
    
        updateCache();
    }

    template<typename T>
    void scale(T s)
    {
        if (max_precision >= FloatType::FQD)
            m256 *= mat_scale<fqd>((fqd)s, (fqd)s);
        else if (max_precision >= FloatType::FDD)
            m128 *= mat_scale<fdd>((fdd)s, (fdd)s);
        else
            m64 *= mat_scale<f64>((f64)s, (f64)s);
    
        updateCache();
    }

    template<typename T>
    void scale(T sx, T sy)
    {
        if (max_precision >= FloatType::FQD)
            m256 *= mat_scale<fqd>((fqd)sx, (fqd)sy);
        else if (max_precision >= FloatType::FDD)
            m128 *= mat_scale<fdd>((fdd)sx, (fdd)sy);
        else
            m64 *= mat_scale<f64>((f64)sx, (f64)sy);
    
        updateCache();
    }

    void rotate(f64 r)
    {
        if (max_precision >= FloatType::FQD)
            m256 *= mat_rotate<fqd>(r);
        else if (max_precision >= FloatType::FDD)
            m128 *= mat_rotate<fdd>(r);
        else
            m64 *= mat_rotate<f64>(r);

        updateCache();
    }

    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr DMat3&  stageTransform() { return m64;  }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr DDMat3& stageTransform() { return m128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr QDMat3& stageTransform() { return m256; }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr DMat3&  worldTransform() { return inv_m64;  }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr DDMat3& worldTransform() { return inv_m128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr QDMat3& worldTransform() { return inv_m256; }

    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr const DMat3&  stageTransform() const { return m64;  }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr const DDMat3& stageTransform() const { return m128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr const QDMat3& stageTransform() const { return m256; }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr const DMat3&  worldTransform() const { return inv_m64;  }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr const DDMat3& worldTransform() const { return inv_m128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr const QDMat3& worldTransform() const { return inv_m256; }

    constexpr operator const FMat3()   const { return (FMat3)m64; }
    constexpr operator const DMat3&()  const { return m64; }
    constexpr operator const DDMat3&() const { return m128; }
    constexpr operator const QDMat3&() const { return m256; }
    
    f64 avgZoomScaleFactor() const
    {
        const f64 a = m64(0, 0);
        const f64 b = m64(0, 1);
        const f64 c = m64(1, 0);
        const f64 d = m64(1, 1);

        return std::sqrt(0.5 * (a * a + b * b + c * c + d * d));
    }

    template<class T>
    Vec2<T> _zoom() const
    {
        const auto& m = stageTransform<T>();

        const T m00 = m(0, 0);
        const T m01 = m(0, 1);
        const T m10 = m(1, 0);
        const T m11 = m(1, 1);

        // column lengths give the local (pre-rotation)
        const T sx = sqrt(m00 * m00 + m10 * m10);
        if (sx == T{ 0 }) return { T{0}, T{0} };

        // preserve reflection sign on Y via determinant
        const T det = m00 * m11 - m01 * m10;
        const T sy = det / sx;

        return { sx, sy };
    }

    template<class T>
    f64 _angle() const
    {
        const auto& m = stageTransform<T>();
        return std::atan2((f64)m(1, 0), (f64)m(0, 0));
    }

    // ────── toStage ──────
    template<class T>       requires is_f32_v<T>  [[nodiscard]] DVec2 toStage(f32 wx, f32 wy)    const { return m64.mulPoint(DVec2{ (f64)wx, (f64)wy }); }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] DVec2 toStage(f64 wx, f64 wy)    const { return m64.mulPoint({ wx, wy }); }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] DVec2 toStage(fdd wx, fdd wy)  const { return m128.mulPoint({ wx, wy }); }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] DVec2 toStage(fqd wx, fqd wy)  const { return m256.mulPoint({ wx, wy }); }
    
    template<class T>       requires is_f32_v<T>  [[nodiscard]] DVec2 toStage(FVec2 p)           const { return m64.mulPoint(DVec2{ (f64)p.x, (f64)p.y }); }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] DVec2 toStage(DVec2 p)           const { return m64.mulPoint(p); }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] DVec2 toStage(DDVec2 p)          const { return static_cast<DVec2>(m128.mulPoint(p)); }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] DVec2 toStage(QDVec2 p)          const { return static_cast<DVec2>(m256.mulPoint(p)); }
    
    // ────── toWorld ──────                                                                            
    template<class T>       requires is_f32_v<T>  [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy)  const { return static_cast<FVec2>(  inv_m64.mulPoint({ sx, sy }) ); }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy)  const { return static_cast<DVec2>(  inv_m64.mulPoint({ sx, sy }) ); }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy)  const { return static_cast<DDVec2>( inv_m128.mulPoint(DDVec2{fdd{sx}, fdd{sy}}) ); }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy)  const { return static_cast<QDVec2>( inv_m256.mulPoint(QDVec2{fqd{sx}, fqd{sy}}) ); }
    
    template<class T>       requires is_f32_v<T>  [[nodiscard]] Vec2<T> toWorld(DVec2 p)         const { return static_cast<FVec2>(  inv_m64.mulPoint(p) ); }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] Vec2<T> toWorld(DVec2 p)         const { return static_cast<DVec2>(  inv_m64.mulPoint(p) ); }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] Vec2<T> toWorld(DVec2 p)         const { return static_cast<DDVec2>( inv_m128.mulPoint(DDVec2(p)) ); }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] Vec2<T> toWorld(DVec2 p)         const { return static_cast<QDVec2>( inv_m256.mulPoint(QDVec2(p)) ); }
                                                                                               
    // ────── worldToStageOffset ──────                                                                   
    template<class T>       requires is_f32_v<T>  [[nodiscard]] DVec2 toStageOffset(FVec2 o)     const { return static_cast<DVec2>( m64.mulVector(DVec2{(f64)o.x, (f64)o.y})); }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] DVec2 toStageOffset(DVec2 o)     const { return static_cast<DVec2>( m64.mulVector(o)); }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] DVec2 toStageOffset(DDVec2 o)    const { return static_cast<DVec2>( m128.mulVector(o)); }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] DVec2 toStageOffset(QDVec2 o)    const { return static_cast<DVec2>( m256.mulVector(o)); }
                                                                                               
    // ────── stageToWorldOffset ──────                                                                   
    template<class T>       requires is_f32_v<T>  [[nodiscard]] FVec2  toWorldOffset(DVec2 o)    const { return static_cast<FVec2>(  inv_m64.mulVector(o) ); }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] DVec2  toWorldOffset(DVec2 o)    const { return static_cast<DVec2>(  inv_m64.mulVector(o) ); }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] DDVec2 toWorldOffset(DVec2 o)    const { return static_cast<DDVec2>( inv_m128.mulVector(DDVec2{fdd{o.x}, fdd{o.y}}) ); }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] QDVec2 toWorldOffset(DVec2 o)    const { return static_cast<QDVec2>( inv_m256.mulVector(QDVec2{fqd{o.x}, fqd{o.y}}) ); }

    template<typename T=f64>
    [[nodiscard]] Vec2<T> toWorldOffset(f64 sx, f64 sy) const {
        return toWorldOffset<T>({ sx, sy });
    }

    // ────── toWorldRect ──────
    [[nodiscard]] DRect toWorldRect(const DRect& r) const {
        DVec2 tl = toWorld(r.x1, r.y1);
        DVec2 br = toWorld(r.x2, r.y2);
        return { tl.x, tl.y, br.x, br.y };
    }

    template<typename T = f64>
    [[nodiscard]] Rect<T> toWorldRect(f64 x1, f64 y1, f64 x2, f64 y2) const {
        Vec2<T> tl = toWorld<T>(x1, y1);
        Vec2<T> br = toWorld<T>(x2, y2);
        return { tl.x, tl.y, br.x, br.y };
    }

    // ────── toWorldQuad ──────
    template<class T = f64> Quad<T> toWorldQuad(DVec2 a, DVec2 b, DVec2 c, DVec2 d)  const { return Quad<T>(toWorld<T>(a), toWorld<T>(b), toWorld<T>(c), toWorld<T>(d)); }
    template<class T = f64> Quad<T> toWorldQuad(const DQuad& quad)                   const { return toWorldQuad<T>(quad.a, quad.b, quad.c, quad.d); }
    template<class T = f64> Quad<T> toWorldQuad(f64 x1, f64 y1, f64 x2, f64 y2)      const { return toWorldQuad<T>(DQuad(x1, y1, x2, y2)); }
    template<class T = f64> Quad<T> toWorldQuad(const DRect& r)                      const { return toWorldQuad<T>(static_cast<DQuad>(r)); }

    // ────── DVec2 toStageSize<INPUT_WORLD_PRECISION>(...) ──────
    template<class T> requires is_fqd_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        const auto a = m256(0, 0);
        const auto b = m256(0, 1);
        const auto c = m256(1, 0);
        const auto d = m256(1, 1);

        const auto nw = abs(a) * wh.x + abs(b) * wh.y;
        const auto nh = abs(c) * wh.x + abs(d) * wh.y;

        return { (f64)nw, (f64)nh };
    }

    template<class T> requires is_fdd_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        const auto a = m128(0, 0);
        const auto b = m128(0, 1);
        const auto c = m128(1, 0);
        const auto d = m128(1, 1);

        const auto nw = abs(a) * wh.x + abs(b) * wh.y;
        const auto nh = abs(c) * wh.x + abs(d) * wh.y;

        return { (f64)nw, (f64)nh };
    }

    template<class T = f64> requires is_f64_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        const f64 a = m64(0, 0);
        const f64 b = m64(0, 1);
        const f64 c = m64(1, 0);
        const f64 d = m64(1, 1);

        const f64 nw = std::abs(a) * wh.x + std::abs(b) * wh.y;
        const f64 nh = std::abs(c) * wh.x + std::abs(d) * wh.y;

        return { nw, nh };
    }

    template<class T> requires is_f32_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        return toStageSizeAABB((DVec2)wh);
    }

    template<class T> requires is_fqd_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        const auto a = m256(0, 0);
        const auto b = m256(0, 1);
        const auto c = m256(1, 0);
        const auto d = m256(1, 1);

        const auto sx = sqrt(a * a + b * b);
        const auto sy = sqrt(c * c + d * d);

        return { (f64)(sx * wh.x), (f64)(sy * wh.y) };
    }

    template<class T> requires is_fdd_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        const auto a = m128(0, 0);
        const auto b = m128(0, 1);
        const auto c = m128(1, 0);
        const auto d = m128(1, 1);

        const auto sx = sqrt(a * a + b * b);
        const auto sy = sqrt(c * c + d * d);

        return { (f64)(sx * wh.x), (f64)(sy * wh.y) };
    }

    template<class T = f64> requires is_f64_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        const f64 a = m64(0, 0);
        const f64 b = m64(0, 1);
        const f64 c = m64(1, 0);
        const f64 d = m64(1, 1);

        const f64 sx = sqrt(a * a + b * b);
        const f64 sy = sqrt(c * c + d * d);

        return { sx * wh.x, sy * wh.y };
    }

    template<class T> requires is_f32_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const {
        return toStageSideLengths((DVec2)wh);
    }

    // ────── DRect toStageRect<INPUT_WORLD_PRECISION>(...) ──────
    template<class T = f64> [[nodiscard]] DRect toStageRect(T x0, T y0, T x1, T y1)   const { return { toStage<T>(x0, y0), toStage<T>(x1, y1) }; }
    template<class T = f64> [[nodiscard]] DRect toStageRect(Vec2<T> pt1, Vec2<T> pt2) const { return { toStage<T>(pt1), toStage<T>(pt2) }; }

    // ────── toStageQuad ──────
    template<class T> [[nodiscard]] DQuad toStageQuad(Vec2<T> a, Vec2<T> b, Vec2<T> c, Vec2<T> d) const { return { toStage<T>(a),   toStage<T>(b),   toStage<T>(c),   toStage<T>(d)   }; }
    template<class T> [[nodiscard]] DQuad toStageQuad(const Quad<T> &q)                           const { return { toStage<T>(q.a), toStage<T>(q.b), toStage<T>(q.c), toStage<T>(q.d) }; }

    // ────── axis ──────
    [[nodiscard]] DVec2 axisStageDirection(bool isX) const
    {
        const f64 m00 = m64(0, 0);
        const f64 m01 = m64(1, 0);
        const f64 m10 = m64(0, 1);
        const f64 m11 = m64(1, 1);

        if (isX)
        {
            const f64 sx = std::hypot(m00, m10);
            if (sx == 0) return { 0, 0 };
            return { m00 / sx, -m10 / sx };
        }
        else
        {
            const f64 sy = std::hypot(m01, m11);
            if (sy == 0) return { 0, 0 };
            return { m01 / sy, -m11 / sy };
        }
    }
    [[nodiscard]] DVec2 axisStagePerpDirection(bool isX) const
    {
        DVec2 d = axisStageDirection(isX).normalized();
        return { -d.y, d.x };
    }
};
*/

class WorldStageTransform
{
    friend class CameraInfo;

    QDMat3 m256     = QDMat3(1_qd);
    QDMat3 inv_m256 = QDMat3(1_qd);

    DDMat3 m128     = DDMat3(1_dd);
    DDMat3 inv_m128 = DDMat3(1_dd);

    DMat3 m64       = DMat3(1.0);
    DMat3 inv_m64   = DMat3(1.0);

    FloatType max_precision = FloatType::F64;

    template<typename MatT>
    [[nodiscard]] static constexpr bool isAffine2D(const MatT& m)
    {
        using T = typename MatT::value_type;
        return m(2, 0) == T{}
            && m(2, 1) == T{}
            && m(2, 2) == T{ 1 };
    }

    template<typename MatT>
    [[nodiscard]] static constexpr MatT affineInverse2D(const MatT& m)
    {
        using T = typename MatT::value_type;

        const T& a  = m(0, 0);
        const T& b  = m(0, 1);
        const T& tx = m(0, 2);

        const T& c  = m(1, 0);
        const T& d  = m(1, 1);
        const T& ty = m(1, 2);

        const T det = a * d - b * c;

        MatT inv;
        inv(0, 0) =  d / det;
        inv(0, 1) = -b / det;
        inv(0, 2) = (b * ty - d * tx) / det;

        inv(1, 0) = -c / det;
        inv(1, 1) =  a / det;
        inv(1, 2) = (c * tx - a * ty) / det;

        inv(2, 0) = T{};
        inv(2, 1) = T{};
        inv(2, 2) = T{ 1 };

        return inv;
    }

    template<typename MatT>
    [[nodiscard]] static constexpr MatT inverseStep(const MatT& m)
    {
        if (isAffine2D(m))
            return affineInverse2D(m);
        return m.inversed();
    }

    constexpr void updateCache()
    {
        if (max_precision >= FloatType::FQD)
        {
            m128     = static_cast<DDMat3>(m256);
            inv_m128 = static_cast<DDMat3>(inv_m256);
            m64      = static_cast<DMat3>(m128);
            inv_m64  = static_cast<DMat3>(inv_m128);
            return;
        }

        if (max_precision >= FloatType::FDD)
        {
            m64      = static_cast<DMat3>(m128);
            inv_m64  = static_cast<DMat3>(inv_m128);
            m256     = static_cast<QDMat3>(m128);
            inv_m256 = static_cast<QDMat3>(inv_m128);
            return;
        }

        m128     = static_cast<DDMat3>(m64);
        inv_m128 = static_cast<DDMat3>(inv_m64);
        m256     = static_cast<QDMat3>(m128);
        inv_m256 = static_cast<QDMat3>(inv_m128);
    }

    constexpr void applyStep(const QDMat3& step, const QDMat3& inv_step)
    {
        m256 *= step;
        inv_m256 = inv_step * inv_m256;
        updateCache();
    }

    constexpr void applyStep(const DDMat3& step, const DDMat3& inv_step)
    {
        m128 *= step;
        inv_m128 = inv_step * inv_m128;
        updateCache();
    }

    constexpr void applyStep(const DMat3& step, const DMat3& inv_step)
    {
        m64 *= step;
        inv_m64 = inv_step * inv_m64;
        updateCache();
    }

public:
    [[nodiscard]] FloatType maxPrecision() const { return max_precision; }

    void setMaxPrecision(FloatType p)
    {
        if (p == max_precision)
            return;

        if (p >= FloatType::FQD)
        {
            if (max_precision >= FloatType::FDD)
            {
                m256     = static_cast<QDMat3>(m128);
                inv_m256 = static_cast<QDMat3>(inv_m128);
            }
            else
            {
                m256     = static_cast<QDMat3>(m64);
                inv_m256 = static_cast<QDMat3>(inv_m64);
            }
        }
        else if (p >= FloatType::FDD)
        {
            if (max_precision >= FloatType::FQD)
            {
                m128     = static_cast<DDMat3>(m256);
                inv_m128 = static_cast<DDMat3>(inv_m256);
            }
            else
            {
                m128     = static_cast<DDMat3>(m64);
                inv_m128 = static_cast<DDMat3>(inv_m64);
            }
        }
        else
        {
            if (max_precision >= FloatType::FQD)
            {
                m64     = static_cast<DMat3>(m256);
                inv_m64 = static_cast<DMat3>(inv_m256);
            }
            else
            {
                m64     = static_cast<DMat3>(m128);
                inv_m64 = static_cast<DMat3>(inv_m128);
            }
        }

        max_precision = p;
        updateCache();
    }

    void reset()
    {
        if (max_precision >= FloatType::FQD)
        {
            m256     = QDMat3(1.0_qd);
            inv_m256 = QDMat3(1.0_qd);
        }
        else if (max_precision >= FloatType::FDD)
        {
            m128     = DDMat3(1.0_dd);
            inv_m128 = DDMat3(1.0_dd);
        }
        else
        {
            m64      = DMat3(1.0);
            inv_m64  = DMat3(1.0);
        }

        updateCache();
    }

    template<typename T>
    void transform(const Mat3<T>& m)
    {
        if (max_precision >= FloatType::FQD)
        {
            const QDMat3 step = static_cast<QDMat3>(m);
            applyStep(step, inverseStep(step));
        }
        else if (max_precision >= FloatType::FDD)
        {
            const DDMat3 step = static_cast<DDMat3>(m);
            applyStep(step, inverseStep(step));
        }
        else
        {
            const DMat3 step = static_cast<DMat3>(m);
            applyStep(step, inverseStep(step));
        }
    }

    template<typename T>
    void translate(T x, T y)
    {
        if (max_precision >= FloatType::FQD)
        {
            const fqd_s tx = static_cast<fqd_s>(x);
            const fqd_s ty = static_cast<fqd_s>(y);
            applyStep(
                mat_translate<fqd>(tx, ty),
                mat_translate<fqd>(-tx, -ty)
            );
        }
        else if (max_precision >= FloatType::FDD)
        {
            const fdd_s tx = static_cast<fdd_s>(x);
            const fdd_s ty = static_cast<fdd_s>(y);
            applyStep(
                mat_translate<fdd>(tx, ty),
                mat_translate<fdd>(-tx, -ty)
            );
        }
        else
        {
            const f64 tx = static_cast<f64>(x);
            const f64 ty = static_cast<f64>(y);
            applyStep(
                mat_translate<f64>(tx, ty),
                mat_translate<f64>(-tx, -ty)
            );
        }
    }

    template<typename T>
    void scale(T s)
    {
        scale(s, s);
    }

    template<typename T>
    void scale(T sx, T sy)
    {
        if (max_precision >= FloatType::FQD)
        {
            const fqd_s x = static_cast<fqd_s>(sx);
            const fqd_s y = static_cast<fqd_s>(sy);
            applyStep(
                mat_scale<fqd>(x, y),
                mat_scale<fqd>(fqd_s{ 1.0 } / x, fqd_s{ 1.0 } / y)
            );
        }
        else if (max_precision >= FloatType::FDD)
        {
            const fdd_s x = static_cast<fdd_s>(sx);
            const fdd_s y = static_cast<fdd_s>(sy);
            applyStep(
                mat_scale<fdd>(x, y),
                mat_scale<fdd>(fdd_s{ 1.0 } / x, fdd_s{ 1.0 } / y)
            );
        }
        else
        {
            const f64 x = static_cast<f64>(sx);
            const f64 y = static_cast<f64>(sy);
            applyStep(
                mat_scale<f64>(x, y),
                mat_scale<f64>(1.0 / x, 1.0 / y)
            );
        }
    }

    void rotate(f64 r)
    {
        if (max_precision >= FloatType::FQD)
        {
            applyStep(
                mat_rotate<fqd>(r),
                mat_rotate<fqd>(-r)
            );
        }
        else if (max_precision >= FloatType::FDD)
        {
            applyStep(
                mat_rotate<fdd>(r),
                mat_rotate<fdd>(-r)
            );
        }
        else
        {
            applyStep(
                mat_rotate<f64>(r),
                mat_rotate<f64>(-r)
            );
        }
    }

    template<class T > requires is_f64_v<T> [[nodiscard]] constexpr DMat3&  stageTransform() { return m64; }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] constexpr DDMat3& stageTransform() { return m128; }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] constexpr QDMat3& stageTransform() { return m256; }
    template<class T > requires is_f64_v<T> [[nodiscard]] constexpr DMat3&  worldTransform() { return inv_m64; }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] constexpr DDMat3& worldTransform() { return inv_m128; }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] constexpr QDMat3& worldTransform() { return inv_m256; }

    template<class T > requires is_f64_v<T> [[nodiscard]] constexpr const DMat3&  stageTransform() const { return m64; }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] constexpr const DDMat3& stageTransform() const { return m128; }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] constexpr const QDMat3& stageTransform() const { return m256; }
    template<class T > requires is_f64_v<T> [[nodiscard]] constexpr const DMat3&  worldTransform() const { return inv_m64; }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] constexpr const DDMat3& worldTransform() const { return inv_m128; }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] constexpr const QDMat3& worldTransform() const { return inv_m256; }

    constexpr operator const FMat3()   const { return (FMat3)m64; }
    constexpr operator const DMat3&()  const { return m64; }
    constexpr operator const DDMat3&() const { return m128; }
    constexpr operator const QDMat3&() const { return m256; }

    f64 avgZoomScaleFactor() const
    {
        const f64 a = m64(0, 0);
        const f64 b = m64(0, 1);
        const f64 c = m64(1, 0);
        const f64 d = m64(1, 1);

        return std::sqrt(0.5 * (a * a + b * b + c * c + d * d));
    }

    template<class T>
    Vec2<T> _zoom() const
    {
        const auto& m = stageTransform<T>();

        const T m00 = m(0, 0);
        const T m01 = m(0, 1);
        const T m10 = m(1, 0);
        const T m11 = m(1, 1);

        const T sx = sqrt(m00 * m00 + m10 * m10);
        if (sx == T{ 0 }) return { T{0}, T{0} };

        const T det = m00 * m11 - m01 * m10;
        const T sy = det / sx;

        return { sx, sy };
    }

    template<class T>
    f64 _angle() const
    {
        const auto& m = stageTransform<T>();
        return std::atan2((f64)m(1, 0), (f64)m(0, 0));
    }

    template<class T>  requires is_f32_v<T>  [[nodiscard]] DVec2 toStage(f32 wx, f32 wy)   const { return m64.mulPoint(DVec2{ (f64)wx, (f64)wy }); }
    template<class T > requires is_f64_v<T>  [[nodiscard]] DVec2 toStage(f64 wx, f64 wy)   const { return m64.mulPoint({ wx, wy }); }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] DVec2 toStage(fdd wx, fdd wy) const { return m128.mulPoint({ wx, wy }); }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] DVec2 toStage(fqd wx, fqd wy) const { return m256.mulPoint({ wx, wy }); }

    template<class T>  requires is_f32_v<T>  [[nodiscard]] DVec2 toStage(FVec2 p)  const { return m64.mulPoint(DVec2{ (f64)p.x, (f64)p.y }); }
    template<class T > requires is_f64_v<T>  [[nodiscard]] DVec2 toStage(DVec2 p)  const { return m64.mulPoint(p); }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] DVec2 toStage(DDVec2 p) const { return static_cast<DVec2>(m128.mulPoint(p)); }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] DVec2 toStage(QDVec2 p) const { return static_cast<DVec2>(m256.mulPoint(p)); }

    template<class T>  requires is_f32_v<T>  [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy) const { return static_cast<FVec2>(inv_m64.mulPoint({ sx, sy })); }
    template<class T > requires is_f64_v<T>  [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy) const { return static_cast<DVec2>(inv_m64.mulPoint({ sx, sy })); }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy) const { return static_cast<DDVec2>(inv_m128.mulPoint(DDVec2{ fdd{sx}, fdd{sy} })); }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] Vec2<T> toWorld(f64 sx, f64 sy) const { return static_cast<QDVec2>(inv_m256.mulPoint(QDVec2{ fqd{sx}, fqd{sy} })); }

    template<class T>  requires is_f32_v<T>  [[nodiscard]] Vec2<T> toWorld(DVec2 p) const { return static_cast<FVec2>(inv_m64.mulPoint(p)); }
    template<class T > requires is_f64_v<T>  [[nodiscard]] Vec2<T> toWorld(DVec2 p) const { return static_cast<DVec2>(inv_m64.mulPoint(p)); }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] Vec2<T> toWorld(DVec2 p) const { return static_cast<DDVec2>(inv_m128.mulPoint(DDVec2(p))); }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] Vec2<T> toWorld(DVec2 p) const { return static_cast<QDVec2>(inv_m256.mulPoint(QDVec2(p))); }

    template<class T>  requires is_f32_v<T>  [[nodiscard]] DVec2 toStageOffset(FVec2 o)  const { return static_cast<DVec2>(m64.mulVector(DVec2{ (f64)o.x, (f64)o.y })); }
    template<class T > requires is_f64_v<T>  [[nodiscard]] DVec2 toStageOffset(DVec2 o)  const { return static_cast<DVec2>(m64.mulVector(o)); }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] DVec2 toStageOffset(DDVec2 o) const { return static_cast<DVec2>(m128.mulVector(o)); }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] DVec2 toStageOffset(QDVec2 o) const { return static_cast<DVec2>(m256.mulVector(o)); }

    template<class T>  requires is_f32_v<T>  [[nodiscard]] FVec2  toWorldOffset(DVec2 o) const { return static_cast<FVec2>(inv_m64.mulVector(o)); }
    template<class T > requires is_f64_v<T>  [[nodiscard]] DVec2  toWorldOffset(DVec2 o) const { return static_cast<DVec2>(inv_m64.mulVector(o)); }
    template<class T>  requires is_fdd_v<T> [[nodiscard]] DDVec2 toWorldOffset(DVec2 o) const { return static_cast<DDVec2>(inv_m128.mulVector(DDVec2{ fdd{o.x}, fdd{o.y} })); }
    template<class T>  requires is_fqd_v<T> [[nodiscard]] QDVec2 toWorldOffset(DVec2 o) const { return static_cast<QDVec2>(inv_m256.mulVector(QDVec2{ fqd{o.x}, fqd{o.y} })); }

    template<typename T /*= f64*/>
    [[nodiscard]] Vec2<T> toWorldOffset(f64 sx, f64 sy) const
    {
        return toWorldOffset<T>({ sx, sy });
    }

    template<typename T /*= f64*/>
    [[nodiscard]] DRect toWorldRect(const DRect& r) const
    {
        DVec2 tl = toWorld<T>(r.x1, r.y1);
        DVec2 br = toWorld<T>(r.x2, r.y2);
        return { tl.x, tl.y, br.x, br.y };
    }

    template<typename T /*= f64*/>
    [[nodiscard]] Rect<T> toWorldRect(f64 x1, f64 y1, f64 x2, f64 y2) const
    {
        Vec2<T> tl = toWorld<T>(x1, y1);
        Vec2<T> br = toWorld<T>(x2, y2);
        return { tl.x, tl.y, br.x, br.y };
    }

    template<class T /*= f64*/> Quad<T> toWorldQuad(DVec2 a, DVec2 b, DVec2 c, DVec2 d) const { return Quad<T>(toWorld<T>(a), toWorld<T>(b), toWorld<T>(c), toWorld<T>(d)); }
    template<class T /*= f64*/> Quad<T> toWorldQuad(const DQuad& quad)                    const { return toWorldQuad<T>(quad.a, quad.b, quad.c, quad.d); }
    template<class T /*= f64*/> Quad<T> toWorldQuad(f64 x1, f64 y1, f64 x2, f64 y2)       const { return toWorldQuad<T>(DQuad(x1, y1, x2, y2)); }
    template<class T /*= f64*/> Quad<T> toWorldQuad(const DRect& r)                       const { return toWorldQuad<T>(static_cast<DQuad>(r)); }

    template<class T> requires is_fqd_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        const auto a = m256(0, 0);
        const auto b = m256(0, 1);
        const auto c = m256(1, 0);
        const auto d = m256(1, 1);

        const auto nw = abs(a) * wh.x + abs(b) * wh.y;
        const auto nh = abs(c) * wh.x + abs(d) * wh.y;

        return { (f64)nw, (f64)nh };
    }

    template<class T> requires is_fdd_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        const auto a = m128(0, 0);
        const auto b = m128(0, 1);
        const auto c = m128(1, 0);
        const auto d = m128(1, 1);

        const auto nw = abs(a) * wh.x + abs(b) * wh.y;
        const auto nh = abs(c) * wh.x + abs(d) * wh.y;

        return { (f64)nw, (f64)nh };
    }

    template<class T /*= f64*/> requires is_f64_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        const f64 a = m64(0, 0);
        const f64 b = m64(0, 1);
        const f64 c = m64(1, 0);
        const f64 d = m64(1, 1);

        const f64 nw = std::abs(a) * wh.x + std::abs(b) * wh.y;
        const f64 nh = std::abs(c) * wh.x + std::abs(d) * wh.y;

        return { nw, nh };
    }

    template<class T> requires is_f32_v<T>
    [[nodiscard]] DVec2 toStageSizeAABB(Vec2<T> wh) const
    {
        return toStageSizeAABB((DVec2)wh);
    }

    template<class T> requires is_fqd_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        const auto a = m256(0, 0);
        const auto b = m256(0, 1);
        const auto c = m256(1, 0);
        const auto d = m256(1, 1);

        const auto sx = sqrt(a * a + b * b);
        const auto sy = sqrt(c * c + d * d);

        return { (f64)(sx * wh.x), (f64)(sy * wh.y) };
    }

    template<class T> requires is_fdd_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        const auto a = m128(0, 0);
        const auto b = m128(0, 1);
        const auto c = m128(1, 0);
        const auto d = m128(1, 1);

        const auto sx = sqrt(a * a + b * b);
        const auto sy = sqrt(c * c + d * d);

        return { (f64)(sx * wh.x), (f64)(sy * wh.y) };
    }

    template<class T /*= f64*/> requires is_f64_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        const f64 a = m64(0, 0);
        const f64 b = m64(0, 1);
        const f64 c = m64(1, 0);
        const f64 d = m64(1, 1);

        const f64 sx = sqrt(a * a + b * b);
        const f64 sy = sqrt(c * c + d * d);

        return { sx * wh.x, sy * wh.y };
    }

    template<class T> requires is_f32_v<T>
    [[nodiscard]] DVec2 toStageSideLengths(Vec2<T> wh) const
    {
        return toStageSideLengths((DVec2)wh);
    }

    template<class T /*= f64*/> [[nodiscard]] DRect toStageRect(T x0, T y0, T x1, T y1)   const { return { toStage<T>(x0, y0), toStage<T>(x1, y1) }; }
    template<class T /*= f64*/> [[nodiscard]] DRect toStageRect(Vec2<T> pt1, Vec2<T> pt2) const { return { toStage<T>(pt1), toStage<T>(pt2) }; }

    template<class T> [[nodiscard]] DQuad toStageQuad(Vec2<T> a, Vec2<T> b, Vec2<T> c, Vec2<T> d) const { return { toStage<T>(a), toStage<T>(b), toStage<T>(c), toStage<T>(d) }; }
    template<class T> [[nodiscard]] DQuad toStageQuad(const Quad<T>& q)                           const { return { toStage<T>(q.a), toStage<T>(q.b), toStage<T>(q.c), toStage<T>(q.d) }; }

    [[nodiscard]] DVec2 axisStageDirection(bool isX) const
    {
        const f64 m00 = m64(0, 0);
        const f64 m01 = m64(1, 0);
        const f64 m10 = m64(0, 1);
        const f64 m11 = m64(1, 1);

        if (isX)
        {
            const f64 sx = std::hypot(m00, m10);
            if (sx == 0) return { 0, 0 };
            return { m00 / sx, -m10 / sx };
        }
        else
        {
            const f64 sy = std::hypot(m01, m11);
            if (sy == 0) return { 0, 0 };
            return { m01 / sy, -m11 / sy };
        }
    }

    [[nodiscard]] DVec2 axisStagePerpDirection(bool isX) const
    {
        DVec2 d = axisStageDirection(isX).normalized();
        return { -d.y, d.x };
    }
};

class CameraInfo
{
    // ─────── camera data ────────────────────────────────────────────────────────────────────────────────────────────────
    union { struct { fqd x_256;     fqd y_256; };     QDVec2 pos_256    { 0_qd, 0_qd }; };
    union { struct { fdd x_128;     fdd y_128; };     DDVec2 pos_128    { 0_dd, 0_dd }; };
    union { struct { f64  x_64;      f64  y_64;  };     DVec2  pos_64     { 0.0,  0.0  }; };
    union { struct { f64  pan_x;     f64  pan_y; };     DVec2  cam_pan    { 0.0,  0.0  }; };
    union { struct { f64  sx_64;     f64  sy_64; };     DVec2  stretch_64 { 1.0,  1.0  }; };
    union { struct { f64  zoom_x;    f64  zoom_y; };    DVec2  zoom_xy    { 1.0,  1.0  }; };
    //union { struct { f64  zoom_x_64; f64  zoom_y_64; }; DVec2  zoom_xy_64 { 1.0,  1.0  }; };

    //fdd  zoom_128 = 1.0_dd;
    f64   zoom_64 = 1.0;
    f64   rotation_64 = 0;

    DVec2 viewport_anchor{ 0.5, 0.5 };
    f64  ref_zoom = 1.0;

    mutable FloatType max_precision = FloatType::F64;

    // UI
    QDVec2 init_pos{ 0_qd, 0_qd };
    f64    init_zoom = zoom_64;
    DVec2  init_stretch = stretch_64;
    f64    init_rotation = rotation_64;
    bool   ui_using_relative_zoom = false;

    template<typename T>
    FORCE_INLINE void posChanged()
    {
        if constexpr (std::is_same_v<T, fqd>)
        {
            pos_128 = (DDVec2)pos_256;
            pos_64 = (DVec2)pos_128;
        }
        else if constexpr (std::is_same_v<T, fdd>)
        {
            pos_256 = (QDVec2)pos_128;
            pos_64 = (DVec2)pos_128;
        }
        else // if (std::is_same_v<T, f64>)
        {
            pos_256 = (QDVec2)pos_64;
            pos_128 = (DDVec2)pos_64;
        }
    }

    FORCE_INLINE void zoomChanged()
    {
        zoom_xy = zoom_64 * stretch_64;
    }


public:

    CameraInfo() = default;
    CameraInfo(const CameraInfo& rhs) { *this = rhs; }
    ~CameraInfo() = default;

    bool operator ==(const CameraInfo& rhs) const;
    CameraInfo& operator =(const CameraInfo& rhs);

    [[nodiscard]] WorldStageTransform transformFor(const SurfaceInfo& surface) const;

    void setMaxPrecision(FloatType new_precision)
    {
        if (max_precision == new_precision) return;
        
        /// if (new_precision > max_precision)
        /// {
        ///     // upcast
        ///     if (max_precision <= FloatType::F64 && new_precision >= FloatType::FDD)
        ///         pos_128 = (DDVec2)pos_64;
        /// 
        ///     if (max_precision <= FloatType::FDD && new_precision >= FloatType::FQD)
        ///         pos_256 = (QDVec2)pos_128;
        /// }
        /// else
        /// {
        ///     // downcast
        /// }
        
        max_precision = new_precision;
        
        if (max_precision == FloatType::F64)       posChanged<f64>();
        else if (max_precision == FloatType::FDD) posChanged<fdd>();
        else if (max_precision == FloatType::FQD) posChanged<fqd>();
    }

    // ─────── f64 getters ────────────────────────────────────────────────────────────────────────────────────────────────
    [[nodiscard]] constexpr f64 panX()       const  { return pan_x; }
    [[nodiscard]] constexpr f64 panY()       const  { return pan_y; }
    [[nodiscard]] constexpr f64 rotation()   const  { return rotation_64; }
    [[nodiscard]] constexpr f64 zoom()       const  { return zoom_64; }
    [[nodiscard]] constexpr f64 stretchX()   const  { return sx_64; }
    [[nodiscard]] constexpr f64 stretchY()   const  { return sy_64; }
    [[nodiscard]] constexpr DVec2 stretch()  const  { return stretch_64; }
    [[nodiscard]] constexpr f64 zoomX()      const  { return zoom_x; } /// includes stretch
    [[nodiscard]] constexpr f64 zoomY()      const  { return zoom_y; } /// includes stretch

    // ─────── f32 / f64 / fdd / fqd getters ─────────────────────────────────────────────────────────────────────────────────────────
    template<class T>       requires is_f32_v<T>  [[nodiscard]] constexpr f32    x() const { return (f32)x_64; }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr f64    x() const { return x_64; }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr fdd   x() const { return x_128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr fqd   x() const { return x_256; }

    template<class T>       requires is_f32_v<T>  [[nodiscard]] constexpr f32    y() const { return (f32)y_64; }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr f64    y() const { return y_64; }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr fdd   y() const { return y_128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr fqd   y() const { return y_256; }

    template<class T>       requires is_f32_v<T>  [[nodiscard]] constexpr FVec2  pos() const { return (FVec2)pos_64; }
    template<class T = f64> requires is_f64_v<T>  [[nodiscard]] constexpr DVec2  pos() const { return pos_64; }
    template<class T>       requires is_fdd_v<T> [[nodiscard]] constexpr DDVec2 pos() const { return pos_128; }
    template<class T>       requires is_fqd_v<T> [[nodiscard]] constexpr QDVec2 pos() const { return pos_256; }

    // ─────── fdd setters ───────────────────────────────────────────────────────────────────────────────────────────────
    bool setX(fqd x)           { if (x_256 != x)        { x_256 = x;         posChanged<fqd>();  return true; }  return false; }
    bool setY(fqd y)           { if (y_256 != y)        { y_256 = y;         posChanged<fqd>();  return true; }  return false; }
    bool setPos(fqd x, fqd y) { if (!pos_256.eq(x, y)) { pos_256 = {x, y};  posChanged<fqd>();  return true; }  return false; }
    bool setPos(QDVec2 p)       { if (pos_256 != p)      { pos_256 = p;       posChanged<fqd>();  return true; }  return false; }

    // ─────── fdd setters ───────────────────────────────────────────────────────────────────────────────────────────────
    bool setX(fdd x)           { if (x_128 != x)        { x_128 = x;         posChanged<fdd>();  return true; }  return false; }
    bool setY(fdd y)           { if (y_128 != y)        { y_128 = y;         posChanged<fdd>();  return true; }  return false; }                                                                                               
    bool setPos(fdd x, fdd y) { if (!pos_128.eq(x, y)) { pos_128 = {x, y};  posChanged<fdd>();  return true; }  return false; }
    bool setPos(DDVec2 p)       { if (pos_128 != p)      { pos_128 = p;       posChanged<fdd>();  return true; }  return false; }
                                                
    // ─────── f64 setters ────────────────────────────────────────────────────────────────────────────────────────────────
    bool setX(f64 x)            { if (x_64  != x)        { x_64  = x;         posChanged<f64>();  return true; }  return false; }
    bool setY(f64 y)            { if (y_64  != y)        { y_64  = y;         posChanged<f64>();  return true; }  return false; }
    bool setPos(f64 x, f64 y)   { if (!pos_64.eq(x, y))  { pos_64 = {x, y};   posChanged<f64>();  return true; }  return false; }
    bool setPos(DVec2 p)        { if (pos_64 != p)       { pos_64 = p;        posChanged<f64>();  return true; }  return false; }

    bool setZoom(f64 z)         { if (zoom_64 != z)      { zoom_64 = z;       zoomChanged(); return true; }  return false; }
    bool setStretchX(f64 x)     { if (sx_64 != x)        { sx_64 = x;         zoomChanged(); return true; }  return false; }
    bool setStretchY(f64 y)     { if (sy_64 != y)        { sy_64 = y;         zoomChanged(); return true; }  return false; }
    bool setStretch(DVec2 s)    { if (stretch_64 != s)   { stretch_64 = s;    zoomChanged(); return true; }  return false; }                                                                                              
    bool setPan(f64 x, f64 y)   { if (!cam_pan.eq(x, y)) { cam_pan = {x, y};  return true; }  return false; }
    bool setRotation(f64 r)     { if (rotation_64 != r)  { rotation_64 = r;   return true; }  return false; }

    // ────── world rect focusing ─────────────────────────────────────────────────────────────────────────────────────────
    void focusWorldRect(const SurfaceInfo& surface, fqd x0, fqd y0, fqd x1, fqd y1, bool stretch = false);

    template<typename T>
    void focusWorldRect(const SurfaceInfo& surface, T x0, T y0, T x1, T y1, bool stretch = false)
       { focusWorldRect(surface, (fqd)x0, (fqd)y0, (fqd)x1, (fqd)y1, stretch); }

    template<typename T>
    void focusWorldRect(const SurfaceInfo& surface, const Rect<T>& r, bool stretch = false)
       { focusWorldRect<T>(surface, r.x1, r.y1, r.x2, r.y2, stretch); }

    //void focusWorldRect();  // todo: Use current view as the focus rect

    // ────── relative zoom ───────────────────────────────────────────────────────────────────────────────────────────────
    /// Once focusWorldRect called, use it as a "reference" for future scaling relative to that reference zoom
    /// e.g. setRelativeZoom(2)  will zoom 2x relative to focused rect
    void setReferenceZoom(f64 _ref_zoom) { ref_zoom = _ref_zoom; }
    [[nodiscard]] f64 getReferenceZoom() const { return ref_zoom ; }
    [[nodiscard]] f64 relativeZoom() const noexcept { return zoom() / ref_zoom; }

    [[nodiscard]] DVec2 viewportStageSize(const SurfaceInfo& surface) const;
    template<typename T = f64> [[nodiscard]] Vec2<T> viewportWorldSize(const SurfaceInfo& surface) const {
        return Vec2<T>(viewportStageSize(surface)) / getReferenceZoom();
    }
    template<typename T = f64> [[nodiscard]] AngledRect<T> worldAngledRect(const SurfaceInfo& surface) const {
        return AngledRect<T>(pos<T>(), viewportWorldSize<T>(surface), (T)rotation());
    }
    template<typename T> [[nodiscard]] Quad<T> worldQuad(const SurfaceInfo& surface) const {
        return static_cast<Quad<T>>(AngledRect<T>(pos<T>(), viewportWorldSize<T>(surface), (T)rotation()));
    }

    // Set zoom (relative to the reference)
    void setRelativeZoom(f64 rel_zoom) { zoom_64 = ref_zoom * rel_zoom; zoomChanged(); }
    //void setRelativeZoom(fdd rel_zoom) { zoom_128 = ref_zoom * rel_zoom; zoomDirty(); }
    
    // ────── viewport origin ─────────────────────────────────────────────────────────────────────────────────────────────
    void setOriginViewportAnchor(f64 ax, f64 ay);
    void setOriginViewportAnchor(Anchor anchor);
    void cameraToViewport(const SurfaceInfo& surface, f64 left, f64 top, f64 right, f64 bottom);
    void originToCenterViewport();

    [[nodiscard]] DVec2 originPixelOffset(const SurfaceInfo& surface) const;
    [[nodiscard]] DVec2 originWorldOffset(const SurfaceInfo& surface) const;
    [[nodiscard]] DVec2 panPixelOffset() const { return DVec2(pan_x, pan_y); }


    // ────── UI ──────────────────────────────────────────────────────────────────────────────────────────────────────────

public:

    void populateUI(DRect restrict_world_rect = DRect::max_extent());
    int  getPositionDecimals()  const { return 1 + math::countWholeDigits(zoom_64 * 5); }
    fqd getPositionPrecision() const { return math::precisionFromDecimals<fqd>(getPositionDecimals()); }

    void uiSetUsingActualZoom() { ui_using_relative_zoom = false; }
    void uiSetUsingRelativeZoom() { ui_using_relative_zoom = true; }
    void uiSetCurrentAsDefault()
    {
        init_pos.set(x_256, y_256);
        init_stretch = stretch_64;
        init_rotation = rotation_64;
        init_zoom = relativeZoom();
    }

    // ────── tween ───────────────────────────────────────────────────────────────────────────────────────────────────────
    ///static void lerp(CameraInfo& dst, const CameraInfo& a, const CameraInfo& b, f64 lerp_factor)
    ///{
    ///    dst.setX(math::lerp(a.x<fdd>(), b.x<fdd>(), lerp_factor));
    ///    dst.setY(math::lerp(a.y<fdd>(), b.y<fdd>(), lerp_factor));
    ///    dst.setRotation(math::lerpAngle(a.rotation(), b.rotation(), lerp_factor));
    ///    dst.setZoom(math::lerp(a.zoom(), b.zoom(), lerp_factor));
    ///    dst.setStretch(math::lerp(a.stretch(), b.stretch(), lerp_factor));
    ///}
};

class CameraNavigator
{
public:

    CameraInfo* camera = nullptr;

    void setTarget(CameraInfo& cam)
    {
        camera = &cam;
    }

    // ────── Pan attributes ──────
    int pan_down_touch_x = 0;
    int pan_down_touch_y = 0;
    f64 pan_down_touch_dist = 0;
    f64 pan_down_touch_angle = 0;

    fqd pan_beg_cam_x = 0_qd;
    fqd pan_beg_cam_y = 0_qd;
    f64  pan_beg_cam_zoom = 0.0;
    f64  pan_beg_cam_angle = 0.0;

    f64 min_zoom = -1e+300; // Relative to "focused" rect
    f64 max_zoom = 1e+300; // Relative to "focused" rect

    bool panning = false;
    bool direct_cam_panning = true;

    std::vector<FingerInfo> fingers;

    // Clamp the relative zoom range (relative to the reference)
    void restrictRelativeZoomRange(f64 min, f64 max);

    void setDirectCameraPanning(bool b) { direct_cam_panning = b; }
    void panBegin(int _x, int _y, f64 touch_dist, f64 touch_angle);
    bool panDrag(const SurfaceInfo& surface, int _x, int _y, f64 touch_dist, f64 touch_angle);
    void panEnd();
    bool panZoomProcess(const SurfaceInfo& surface);


    [[nodiscard]] std::vector<FingerInfo> pressedFingers() const {
        return fingers;
    }

    [[nodiscard]] f64 touchAngle() const {
        if (fingers.size() >= 2) return std::atan2(fingers[1].y - fingers[0].y, fingers[1].x - fingers[0].x);
        return 0.0;
    }
    [[nodiscard]] f64 touchDist() const {
        if (fingers.size() >= 2) {
            f64 dx = fingers[1].x - fingers[0].x;
            f64 dy = fingers[1].y - fingers[0].y;
            return std::sqrt(dx * dx + dy * dy);
        }
        return 0.0;
    }

    [[nodiscard]] constexpr bool isPanning()         const { return panning; }
    [[nodiscard]] constexpr f64  panDownTouchDist()  const { return pan_down_touch_dist; }
    [[nodiscard]] constexpr f64  panDownTouchAngle() const { return pan_down_touch_angle; }
    [[nodiscard]] constexpr f64  panDownTouchX()     const { return pan_down_touch_x; }
    [[nodiscard]] constexpr f64  panDownTouchY()     const { return pan_down_touch_y; }
    [[nodiscard]] constexpr fqd panBegCamX()        const { return pan_beg_cam_x; }
    [[nodiscard]] constexpr fqd panBegCamY()        const { return pan_beg_cam_y; }
    [[nodiscard]] constexpr f64  panBegCamZoom()     const { return pan_beg_cam_zoom; }
    [[nodiscard]] constexpr f64  panBegCamAngle()    const { return pan_beg_cam_angle; }

    bool handleWorldNavigation(
        Event e,
        bool single_touch_pan,
        bool zoom_anchor_mouse = false,
        double wheel_zoom_rate = 1.0);

    void debugPrint(Viewport* ctx) const;
};


BL_END_NS
