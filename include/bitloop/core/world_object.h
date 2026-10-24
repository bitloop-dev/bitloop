#pragma once

#include <bitloop/util/math_util.h>
#include <bitloop/core/camera.h>

BL_BEGIN_NS

template<typename T>
class WorldObjectT
{
    WorldStageTransform world_stage;

    DVec2   toStage(Vec2<T> p)                  const { return world_stage.toStage<T>(p); }
    DVec2   toStageOffset(const Vec2<T>& o)     const { return world_stage.toStageOffset<T>(o); }
    Vec2<T> toWorld(double sx, double sy)       const { return world_stage.toWorld<T>(sx, sy); }
    Vec2<T> toWorldOffset(double sx, double sy) const { return world_stage.toWorldOffset<T>(sx, sy); }

    // local basis vectors after all transforms
    Vec2<T> u{ T{1}, T{0} };   // width direction
    Vec2<T> v{ T{0}, T{1} };   // height direction

public:

    union {
        Vec2<T> pos = { T{0}, T{0} };
        struct { T x, y; };
    };

    union {
        DVec2 align = { -1, -1 };
        struct { double align_x, align_y; };
    };

    T rotation = T{ 0 };

    WorldObjectT() {}
    ~WorldObjectT() {}

    void setWorldStageTransform(const WorldStageTransform& transform) { world_stage = transform; }
    [[nodiscard]] const WorldStageTransform& worldStageTransform() const { return world_stage; }

    void setAlign(int ax, int ay) { align_x = ax; align_y = ay; }
    void setAlign(const DVec2& _align) { align = _align; }

    // ======== Stage Methods ========

    [[nodiscard]] DVec2  stagePos()      const { return toStage(pos); }
    [[nodiscard]] double stageWidth()    const { return toStageOffset(u).mag(); }
    [[nodiscard]] double stageHeight()   const { return toStageOffset(v).mag(); }
    [[nodiscard]] DVec2  stageSize()     const { return { stageWidth(), stageHeight() }; }
    [[nodiscard]] double stageRotation() const {
        T cos_r = cos(rotation);
        T sin_r = sin(rotation);
        Vec2<T> local_u = Vec2<T>{ worldSize().x * cos_r, worldSize().x * sin_r };
        DVec2 stage_origin = toStage(pos);
        DVec2 stage_u_end = toStage(pos + local_u);
        DVec2 u_stage = stage_u_end - stage_origin;

        return std::atan2(u_stage.y, u_stage.x);
    }
    [[nodiscard]] DQuad stageQuad() const {
        return world_stage.toStageQuad<T>(worldQuad());
    }

    void setStagePos(double sx, double sy) { pos = toWorld(sx, sy); }
    void setStageRect(double sx, double sy, double sw, double sh)
    {
        pos = toWorld(sx, sy) - worldAlignOffset();
        u = toWorld(sx + sw, sy) - pos;
        v = toWorld(sx, sy + sh) - pos;
    }

    void setStageSize(double sw, double sh)
    {
        u = toWorldOffset(sw, 0);
        v = toWorldOffset(0, sh);
    }

    // ======== World Methods ========

    [[nodiscard]] T worldWidth()  const { return u.mag(); }
    [[nodiscard]] T worldHeight() const { return v.mag(); }
    [[nodiscard]] Vec2<T> worldSize() const { return { u.mag(), v.mag() }; }
    [[nodiscard]] Quad<T> worldQuad() const {
        Vec2<T> offset = T(0.5) * (T(-align_x - 1.0) * u + T(-align_y - 1.0) * v);
        Vec2<T> p = pos + offset;
        return { p, p + u, p + u + v, p + v };
    }
    [[nodiscard]] Vec2<T> topLeft() const {
        Vec2<T> offset = T(0.5) * (T(-align_x - 1.0) * u + T(-align_y - 1.0) * v);
        return pos + offset;
    }

    [[nodiscard]] Vec2<T> worldAlignOffset() const { return Vec2<T>{-(align + 1.0) * 0.5} * worldSize(); }
    [[nodiscard]] T worldAlignOffsetX() const { return T(-(align_x + 1) * 0.5) * worldWidth(); }
    [[nodiscard]] T worldAlignOffsetY() const { return T(-(align_y + 1) * 0.5) * worldHeight(); }

    [[nodiscard]] Vec2<T> worldToUVRatio(const Vec2<T>& p) const
    {
        // p = origin + [a] * u + [b] * v
        Vec2<T> origin = pos + T(0.5) * (T(-align_x - 1.0) * u + T(-align_y - 1.0) * v);
        Vec2<T> delta = p - origin;

        // Make sure uv vectors span 2D area
        T det = u.x * v.y - u.y * v.x;
        if (det == T{ 0.0 }) return  Vec2<T>{ T{ 0 }, T{ 0 } };

        // solve for a,b
        T inv_det = 1.0 / det;
        T a = (delta.x * v.y - delta.y * v.x) * inv_det;
        T b = (u.x * delta.y - u.y * delta.x) * inv_det;
        return { a, b };
    }

    void setWorldRect(T _x, T _y, T _w, T _h)
    {
        rotation = 0;
        x = _x - worldAlignOffsetX();
        y = _y - worldAlignOffsetY();
        u = { _w, T{0} };
        v = { T{0}, _h };
    }
    void setWorldRect(Vec2<T> pos, Vec2<T> size)
    {
        rotation = 0;
        x = pos.x - worldAlignOffsetX();
        y = pos.y - worldAlignOffsetY();
        u = { size.x, T{0} };
        v = { T{0}, size.y };
    }
};

typedef WorldObjectT<f64> WorldObject;
typedef WorldObjectT<fdd> WorldObject128;
typedef WorldObjectT<fqd> WorldObject256;


BL_END_NS;
