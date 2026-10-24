#pragma once

#include <memory>

#include <bitloop/platform/platform.h>

#include <bitloop/core/debug.h>
#include <bitloop/core/camera.h>

#include <bitloop/nanovgx/nano_bitmap.h>
#include <bitloop/nanovgx/nano_shader_surface.h>

#include <atomic>

BL_BEGIN_NS

class Viewport;

enum struct PathWinding
{
    WINDING_CCW  = NVGwinding::NVG_CCW,
    WINDING_CW   = NVGwinding::NVG_CW
};
enum struct LineCap
{
    CAP_BUTT     = NVGlineCap::NVG_BUTT,
    CAP_ROUND    = NVGlineCap::NVG_ROUND,
    CAP_SQUARE   = NVGlineCap::NVG_SQUARE,
};
enum struct LineJoin
{
    JOIN_BEVEL   = NVGlineCap::NVG_BEVEL,
    JOIN_MITER   = NVGlineCap::NVG_MITER
};
enum struct TextAlign
{
    ALIGN_LEFT    = NVGalign::NVG_ALIGN_LEFT,
    ALIGN_CENTER  = NVGalign::NVG_ALIGN_CENTER,
    ALIGN_RIGHT   = NVGalign::NVG_ALIGN_RIGHT
};
enum struct TextBaseline
{
    BASELINE_TOP     = NVGalign::NVG_ALIGN_TOP,
    BASELINE_MIDDLE  = NVGalign::NVG_ALIGN_MIDDLE,
    BASELINE_BOTTOM  = NVGalign::NVG_ALIGN_BOTTOM
};
enum struct CompositeOperation
{
    SOURCE_OVER       = NVGcompositeOperation::NVG_SOURCE_OVER,
    SOURCE_IN         = NVGcompositeOperation::NVG_SOURCE_IN,
    SOURCE_OUT        = NVGcompositeOperation::NVG_SOURCE_OUT,
    ATOP              = NVGcompositeOperation::NVG_ATOP,
    DESTINATION_OVER  = NVGcompositeOperation::NVG_DESTINATION_OVER,
    DESTINATION_IN    = NVGcompositeOperation::NVG_DESTINATION_IN,
    DESTINATION_OUT   = NVGcompositeOperation::NVG_DESTINATION_OUT,
    DESTINATION_ATOP  = NVGcompositeOperation::NVG_DESTINATION_ATOP,
    LIGHTER           = NVGcompositeOperation::NVG_LIGHTER,
    COPY              = NVGcompositeOperation::NVG_COPY,
    XOR               = NVGcompositeOperation::NVG_XOR
};
enum struct BlendFactor
{
    ZERO                  = NVGblendFactor::NVG_ZERO,
    ONE                   = NVGblendFactor::NVG_ONE,
    SRC_COLOR             = NVGblendFactor::NVG_SRC_COLOR,
    ONE_MINUS_SRC_COLOR   = NVGblendFactor::NVG_ONE_MINUS_SRC_COLOR,
    DST_COLOR             = NVGblendFactor::NVG_DST_COLOR,
    ONE_MINUS_DST_COLOR   = NVGblendFactor::NVG_ONE_MINUS_DST_COLOR,
    SRC_ALPHA             = NVGblendFactor::NVG_SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA   = NVGblendFactor::NVG_ONE_MINUS_SRC_ALPHA,
    DST_ALPHA             = NVGblendFactor::NVG_DST_ALPHA,
    ONE_MINUS_DST_ALPHA   = NVGblendFactor::NVG_ONE_MINUS_DST_ALPHA,
    SRC_ALPHA_SATURATE    = NVGblendFactor::NVG_SRC_ALPHA_SATURATE
};

typedef NVGpaint PaintStyle;

struct CompositeScope
{
    NVGcontext* vg;
    CompositeScope(NVGcontext* vg, CompositeOperation op) : vg(vg) { nvgSave(vg); nvgGlobalCompositeOperation(vg, (int)op); }
    CompositeScope(NVGcontext* vg, BlendFactor src, BlendFactor dst) : vg(vg) { nvgSave(vg); nvgGlobalCompositeBlendFunc(vg, (int)src, (int)dst); }
    CompositeScope(NVGcontext* vg, BlendFactor sRGB, BlendFactor dRGB, BlendFactor sA, BlendFactor dA) : vg(vg) { nvgSave(vg); nvgGlobalCompositeBlendFuncSeparate(vg, (int)sRGB, (int)dRGB, (int)sA, (int)dA); }
    ~CompositeScope() { nvgRestore(vg); }
};

class NanoFontInternal
{
    friend class SimplePainter;

protected:

    std::string path;
    int id = 0;
    bool created = false;

public:

    NanoFontInternal(const char* virtual_path)
    {
        path = platform()->path(virtual_path);
    }
};

struct NanoFont : std::shared_ptr<NanoFontInternal>
{
    static NanoFont create(const char* virtual_path)
    {
        return std::make_shared<NanoFontInternal>(virtual_path);
    }

    NanoFont() = default;
    NanoFont(const NanoFont& rhs) = default;
    NanoFont(std::shared_ptr<NanoFontInternal> f)
        : std::shared_ptr<NanoFontInternal>(f)
    {}
};

struct PainterContext
{
    NVGcontext* vg = nullptr;
    f64 global_scale = 1.0;

    // Text
    NanoFont default_font;
    NanoFont active_font;

    TextAlign text_align = TextAlign::ALIGN_LEFT;
    TextBaseline text_baseline = TextBaseline::BASELINE_TOP;

    f32 font_size_px = 16.0f;

    f64 adjust_scale_x = 1.0;
    f64 adjust_scale_y = 1.0;
};

class Painter;

// ===========================================
// ======== Simple nanovg C++ wrapper ========
// ===========================================

static inline NVGcolor toNVG(const Color& c) { return nvgRGBA(c.r, c.g, c.b, c.a); }
static inline NVGcolor toNVG(const f32(&rgb)[3], f32 a) { return NVGcolor{ rgb[0], rgb[1], rgb[2], a }; }
static inline NVGcolor toNVG(const f32(&rgba)[4]) { return NVGcolor{ rgba[0], rgba[1], rgba[2], rgba[3] }; }

class SimplePainter
{
    PainterContext* paint_ctx = nullptr;
    NVGcontext* vg = nullptr;

protected:

    friend class Viewport;
    friend class CameraInfo;
    friend class Painter;

public:


    void setTargetPainterContext(PainterContext* target)
    {
        paint_ctx = target;
        vg = target->vg;

        // Necessary? Project should be resetting size each frame anyway
        ///setFontSize(target->font_size);
    }

    // ======== Context getter/setters ========

    [[nodiscard]] NanoFont getDefaultFont() { return paint_ctx->default_font; }
    [[nodiscard]] f64 getGlobalScale() { return paint_ctx->global_scale; }
    bool setGlobalScale(f64 scale) {
        bool changed = scale != paint_ctx->global_scale;
        paint_ctx->global_scale = scale;
        return changed;
    }
    void setGlobalAlpha(f64 alpha) { nvgGlobalAlpha(vg, (f32)alpha); }

    // ======== Transforms ========

    void save() const { nvgSave(vg); }
    void restore() { nvgRestore(vg); }
    
    void resetTransform()
    {
        nvgResetTransform(vg);
    }
    void transform(float a, float b, float c, float d, float e, float f)
    {
        nvgTransform(vg, a, b, c, d, e, f);
    }
    void transform(const FMat3& m)
    {
        const f32 a = m(0, 0);
        const f32 b = m(1, 0);
        const f32 c = m(0, 1);
        const f32 d = m(1, 1);
        const f32 e = m(0, 2);
        const f32 f = m(1, 2);

        nvgTransform(vg, a, b, c, d, e, f);
    }

    FMat3 currentTransform() const
    {
        f32 x[6];
        nvgCurrentTransform(vg, x);

        FMat3 m = FMat3::identity(1.0f);
        m(0, 0) = x[0];  m(1, 0) = x[1];
        m(0, 1) = x[2];  m(1, 1) = x[3];
        m(0, 2) = x[4];  m(1, 2) = x[5];
        m(2, 0) = 0.0f; m(2, 1) = 0.0f; m(2, 2) = 1.0f;
        return m;
    }

    void translate(f64 x, f64 y)                  { nvgTranslate(vg, (f32)(x), (f32)(y)); }
    void translate(DVec2 p)                       { nvgTranslate(vg, (f32)(p.x), (f32)(p.y)); }
    void rotate(f64 angle)                        { nvgRotate(vg, (f32)(angle)); }
    void scale(f64 scale)                         { nvgScale(vg, (f32)(scale), (f32)(scale)); }
    void scale(f64 scale_x, f64 scale_y)          { nvgScale(vg, (f32)(scale_x), (f32)(scale_y)); }
    void skewX(f64 angle)                         { nvgSkewX(vg, (f32)(angle)); }
    void skewY(f64 angle)                         { nvgSkewY(vg, (f32)(angle)); }
    void setClipRect(f64 x, f64 y, f64 w, f64 h)  { nvgScissor(vg, (f32)(x), (f32)(y), (f32)(w), (f32)(h)); }
    void resetClipping()                          { nvgResetScissor(vg); }

    // ======== Styles ========

    void setFillStyle(const Color& color)                  { nvgFillColor(vg, nvgRGBA(color.r, color.g, color.b, color.a)); }
    void setFillStyle(const Color& color, int a)           { nvgFillColor(vg, nvgRGBA(color.r, color.g, color.b, a)); }
    void setFillStyle(const f32(&color)[3])                { nvgFillColor(vg, {color[0], color[1], color[2], 1.0f}); }
    void setFillStyle(const f32(&color)[4])                { nvgFillColor(vg, {color[0], color[1], color[2], color[3]}); }
    void setFillStyle(int r, int g, int b, int a = 255)    { nvgFillColor(vg, nvgRGBA(r, g, b, a)); }
    void setFillStyle(NVGpaint paint)                      { nvgFillPaint(vg, paint); }
                                                           
    void setStrokeStyle(const Color& color)                { nvgStrokeColor(vg, nvgRGBA(color.r, color.g, color.b, color.a)); }
    void setStrokeStyle(const Color& color, int a)         { nvgStrokeColor(vg, nvgRGBA(color.r, color.g, color.b, a)); }
    void setStrokeStyle(const f32(&color)[3])              { nvgStrokeColor(vg, {color[0], color[1], color[2], 1.0f}); }
    void setStrokeStyle(const f32(&color)[4])              { nvgStrokeColor(vg, {color[0], color[1], color[2], color[3]}); }
    void setStrokeStyle(int r, int g, int b, int a = 255)  { nvgStrokeColor(vg, nvgRGBA(r, g, b, a)); }
                                                           
    void setLineWidth(f64 w)                               { nvgStrokeWidth(vg, (f32)w); }
    void setLineCap(LineCap cap)                           { nvgLineCap(vg, (int)cap); }
    void setLineJoin(LineJoin join)                        { nvgLineJoin(vg, (int)join); }
    void setMiterLimit(f64 limit)                          { nvgMiterLimit(vg, (f32)limit); }

    // ======== patterns ========
    PaintStyle imagePattern(f32 ox, f32 oy, f32 ex, f32 ey, f32 angle, int image, f32 alpha)
    {
        return nvgImagePattern(vg, ox, oy, ex, ey, angle, image, alpha);
    }

    // ======== composite ========

    void setCompositeSourceOver()             { nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER); }
    void setCompositeSourceIn()               { nvgGlobalCompositeOperation(vg, NVG_SOURCE_IN); }
    void setCompositeSourceOut()              { nvgGlobalCompositeOperation(vg, NVG_SOURCE_OUT); }
    void setCompositeSourceAtop()             { nvgGlobalCompositeOperation(vg, NVG_ATOP); }
    void setCompositeDestOver()               { nvgGlobalCompositeOperation(vg, NVG_DESTINATION_OVER); }
    void setCompositeDestIn()                 { nvgGlobalCompositeOperation(vg, NVG_DESTINATION_IN); }
    void setCompositeDestOut()                { nvgGlobalCompositeOperation(vg, NVG_DESTINATION_OUT); }
    void setCompositeDestAtop()               { nvgGlobalCompositeOperation(vg, NVG_DESTINATION_ATOP); }
    void setCompositeLighter()                { nvgGlobalCompositeOperation(vg, NVG_LIGHTER); }
    void setCompositeCopy()                   { nvgGlobalCompositeOperation(vg, NVG_COPY); }
    void setCompositeXor()                    { nvgGlobalCompositeOperation(vg, NVG_XOR); }
    void setComposite(CompositeOperation op)  { nvgGlobalCompositeOperation(vg, (int)op); }

    // ======== blending ========

    void setBlendFunc(BlendFactor src, BlendFactor dst) {
        nvgGlobalCompositeBlendFunc(vg, (int)src, (int)dst);
    }
    void setBlendFuncSeparate(BlendFactor srcRGB, BlendFactor dstRGB, BlendFactor srcA, BlendFactor dstA) {
        nvgGlobalCompositeBlendFuncSeparate(vg, (int)srcRGB, (int)dstRGB, (int)srcA, (int)dstA);
    }

    // blend presets
    void setBlendAdditive() { nvgGlobalCompositeOperation(vg, NVG_LIGHTER); }
    void setBlendAlphaPremult() { nvgGlobalCompositeBlendFunc(vg, NVG_ONE, NVG_ONE_MINUS_SRC_ALPHA); }
    void setBlendAlphaStraight() { nvgGlobalCompositeBlendFunc(vg, NVG_SRC_ALPHA, NVG_ONE_MINUS_SRC_ALPHA); }
    void setBlendMultiply() { nvgGlobalCompositeBlendFuncSeparate(vg, NVG_DST_COLOR, NVG_ONE_MINUS_SRC_ALPHA, NVG_ONE, NVG_ONE_MINUS_SRC_ALPHA); }
    void setBlendScreen() { nvgGlobalCompositeBlendFuncSeparate(vg, NVG_ONE, NVG_ONE_MINUS_DST_COLOR, NVG_ONE, NVG_ONE_MINUS_DST_ALPHA); }

    CompositeScope scopedComposite(CompositeOperation op) { return CompositeScope(vg, op); }
    CompositeScope scopedBlend(BlendFactor src, BlendFactor dst) { return CompositeScope(vg, src, dst); }
    CompositeScope scopedBlendSeparate(BlendFactor sRGB, BlendFactor dRGB, BlendFactor sA, BlendFactor dA) { return CompositeScope(vg, sRGB, dRGB, sA, dA); }

    // ======== Linear Gradient ========

    void setFillLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const Color& inner, const Color& outer) {
        nvgFillPaint(vg, nvgLinearGradient(vg, f32(x0), f32(y0), f32(x1), f32(y1), toNVG(inner), toNVG(outer)));
    }
    void setFillLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) {
        nvgFillPaint(vg, nvgLinearGradient(vg, f32(x0), f32(y0), f32(x1), f32(y1), toNVG(inner, a), toNVG(outer, a)));
    }
    void setFillLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[4], const f32(&outer)[4]) {
        nvgFillPaint(vg, nvgLinearGradient(vg, f32(x0), f32(y0), f32(x1), f32(y1), toNVG(inner), toNVG(outer)));
    }

    // ======== Radial Gradient ========

    void setFillRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const Color& inner, const Color& outer) {
        nvgFillPaint(vg, nvgRadialGradient(vg, f32(cx), f32(cy), f32(r0), f32(r1), toNVG(inner), toNVG(outer)));
    }
    void setFillRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) {
        nvgFillPaint(vg, nvgRadialGradient(vg, f32(cx), f32(cy), f32(r0), f32(r1), toNVG(inner, a), toNVG(outer, a)));
    }
    void setFillRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4]) {
        nvgFillPaint(vg, nvgRadialGradient(vg, f32(cx), f32(cy), f32(r0), f32(r1), toNVG(inner), toNVG(outer)));
    }

    // ======== Box Gradient ========

    void setFillBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const Color& inner, const Color& outer) {
        nvgFillPaint(vg, nvgBoxGradient(vg, f32(x), f32(y), f32(w), f32(h), f32(r), f32(f), toNVG(inner), toNVG(outer)));
    }
    void setFillBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) {
        nvgFillPaint(vg, nvgBoxGradient(vg, f32(x), f32(y), f32(w), f32(h), f32(r), f32(f), toNVG(inner, a), toNVG(outer, a)));
    }
    void setFillBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4]) {
        nvgFillPaint(vg, nvgBoxGradient(vg, f32(x), f32(y), f32(w), f32(h), f32(r), f32(f), toNVG(inner), toNVG(outer)));
    }

    // ======== Linear Gradient (Stroke) ========

    void setStrokeLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const Color& inner, const Color& outer) {
        nvgStrokePaint(vg, nvgLinearGradient(vg, f32(x0), f32(y0), f32(x1), f32(y1), toNVG(inner), toNVG(outer)));
    }
    void setStrokeLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) {
        nvgStrokePaint(vg, nvgLinearGradient(vg, f32(x0), f32(y0), f32(x1), f32(y1), toNVG(inner, a), toNVG(outer, a)));
    }
    void setStrokeLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[4], const f32(&outer)[4]) {
        nvgStrokePaint(vg, nvgLinearGradient(vg, f32(x0), f32(y0), f32(x1), f32(y1), toNVG(inner), toNVG(outer)));
    }

    // ======== Radial Gradient (Stroke) ========

    void setStrokeRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const Color& inner, const Color& outer) {
        nvgStrokePaint(vg, nvgRadialGradient(vg, f32(cx), f32(cy), f32(r0), f32(r1), toNVG(inner), toNVG(outer)));
    }
    void setStrokeRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) {
        nvgStrokePaint(vg, nvgRadialGradient(vg, f32(cx), f32(cy), f32(r0), f32(r1), toNVG(inner, a), toNVG(outer, a)));
    }
    void setStrokeRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4]) {
        nvgStrokePaint(vg, nvgRadialGradient(vg, f32(cx), f32(cy), f32(r0), f32(r1), toNVG(inner), toNVG(outer)));
    }

    // ======== Box Gradient (Stroke) ========

    void setStrokeBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const Color& inner, const Color& outer) {
        nvgStrokePaint(vg, nvgBoxGradient(vg, f32(x), f32(y), f32(w), f32(h), f32(r), f32(f), toNVG(inner), toNVG(outer)));
    }
    void setStrokeBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) {
        nvgStrokePaint(vg, nvgBoxGradient(vg, f32(x), f32(y), f32(w), f32(h), f32(r), f32(f), toNVG(inner, a), toNVG(outer, a)));
    }
    void setStrokeBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4]) {
        nvgStrokePaint(vg, nvgBoxGradient(vg, f32(x), f32(y), f32(w), f32(h), f32(r), f32(f), toNVG(inner), toNVG(outer)));
    }

    // Linear (Fill)
    void setFillLinearGradient(DVec2 p0, DVec2 p1, const Color& inner, const Color& outer) { setFillLinearGradient(p0.x, p0.y, p1.x, p1.y, inner, outer); }
    void setFillLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) { setFillLinearGradient(p0.x, p0.y, p1.x, p1.y, inner, outer, a); }
    void setFillLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[4], const f32(&outer)[4]) { setFillLinearGradient(p0.x, p0.y, p1.x, p1.y, inner, outer); }

    // Linear (Stroke)
    void setStrokeLinearGradient(DVec2 p0, DVec2 p1, const Color& inner, const Color& outer) { setStrokeLinearGradient(p0.x, p0.y, p1.x, p1.y, inner, outer); }
    void setStrokeLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) { setStrokeLinearGradient(p0.x, p0.y, p1.x, p1.y, inner, outer, a); }
    void setStrokeLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[4], const f32(&outer)[4]) { setStrokeLinearGradient(p0.x, p0.y, p1.x, p1.y, inner, outer); }

    // Radial (Fill)
    void setFillRadialGradient(DVec2 c, f64 r0, f64 r1, const Color& inner, const Color& outer) { setFillRadialGradient(c.x, c.y, r0, r1, inner, outer); }
    void setFillRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) { setFillRadialGradient(c.x, c.y, r0, r1, inner, outer, a); }
    void setFillRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4]) { setFillRadialGradient(c.x, c.y, r0, r1, inner, outer); }

    // Radial (Stroke)
    void setStrokeRadialGradient(DVec2 c, f64 r0, f64 r1, const Color& inner, const Color& outer) { setStrokeRadialGradient(c.x, c.y, r0, r1, inner, outer); }
    void setStrokeRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) { setStrokeRadialGradient(c.x, c.y, r0, r1, inner, outer, a); }
    void setStrokeRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4]) { setStrokeRadialGradient(c.x, c.y, r0, r1, inner, outer); }

    // Box (Fill)
    void setFillBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const Color& inner, const Color& outer) { setFillBoxGradient(pos.x, pos.y, size.x, size.y, r, f, inner, outer); }
    void setFillBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) { setFillBoxGradient(pos.x, pos.y, size.x, size.y, r, f, inner, outer, a); }
    void setFillBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4]) { setFillBoxGradient(pos.x, pos.y, size.x, size.y, r, f, inner, outer); }

    // Box (Stroke)
    void setStrokeBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const Color& inner, const Color& outer) { setStrokeBoxGradient(pos.x, pos.y, size.x, size.y, r, f, inner, outer); }
    void setStrokeBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f) { setStrokeBoxGradient(pos.x, pos.y, size.x, size.y, r, f, inner, outer, a); }
    void setStrokeBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4]) { setStrokeBoxGradient(pos.x, pos.y, size.x, size.y, r, f, inner, outer); }

    // ======== Paths ========                               
                                                             
    void beginPath()           { nvgBeginPath(vg); }
    void moveTo(f64 x, f64 y)  { nvgMoveTo(vg, (f32)(x), (f32)(y)); }
    void lineTo(f64 x, f64 y)  { nvgLineTo(vg, (f32)(x), (f32)(y)); }
    void moveTo(DVec2 p)       { nvgMoveTo(vg, (f32)(p.x), (f32)(p.y)); }
    void lineTo(DVec2 p)       { nvgLineTo(vg, (f32)(p.x), (f32)(p.y)); }
    void stroke()              { nvgStroke(vg); }
    void fill()                { nvgFill(vg); }
    void closePath()           { nvgClosePath(vg); }

    void bezierTo(f64 x1, f64 y1, f64 x2, f64 y2, f64 x, f64 y) {
        nvgBezierTo(vg, (f32)(x1), (f32)(y1), (f32)(x2), (f32)(y2), (f32)(x), (f32)(y));
    }
    void bezierTo(DVec2 p1, DVec2 p2, DVec2 p) {
        nvgBezierTo(vg, (f32)p1.x, (f32)p1.y, (f32)p2.x, (f32)p2.y, (f32)p.x, (f32)p.y);
    }
    void quadraticTo(f64 cx, f64 cy, f64 x, f64 y) {
        nvgQuadTo(vg, (f32)(cx), (f32)(cy), (f32)(x), (f32)(y));
    }
    void quadraticTo(DVec2 c, DVec2 p) {
        nvgQuadTo(vg, (f32)c.x, (f32)c.y, (f32)p.x, (f32)p.y);
    }

    void arc(f64 cx, f64 cy, f64 r, f64 a0, f64 a1, PathWinding winding = PathWinding::WINDING_CCW) {
        nvgArc(vg, (f32)(cx), (f32)(cy), (f32)(r), (f32)(a0), (f32)(a1), (int)winding);
    }
    void arc(DVec2 cen, f64 r, f64 a0, f64 a1, PathWinding winding = PathWinding::WINDING_CCW) {
        nvgArc(vg, (f32)(cen.x), (f32)(cen.y), (f32)(r), (f32)(a0), (f32)(a1), (int)winding);
    }
    void arcTo(f64 x0, f64 y0, f64 x1, f64 y1, f64 r) {
        nvgArcTo(vg, (f32)(x0), (f32)(y0), (f32)(x1), (f32)(y1), (f32)(r));
    }
    void arcTo(DVec2 p0, DVec2 p1, f64 r) {
        nvgArcTo(vg, (f32)(p0.x), (f32)(p0.y), (f32)(p1.x), (f32)(p1.y), (f32)(r));
    }

    template<typename PointT> void drawPath(const std::vector<PointT>& path) {
        if (path.size() >= 2) {
            moveTo(path[0]); 
            for (size_t i = 1; i < path.size(); i++) 
                lineTo(path[i]); 
        }
    }

    // ======== Shapes ========

    void circle(f64 cx, f64 cy, f64 r) { nvgCircle(vg, (f32)(cx), (f32)(cy), (f32)(r)); }
    void circle(DVec2 p, f64 r) { nvgCircle(vg, (f32)(p.x), (f32)(p.y), (f32)(r)); }
    void rect(f64 x, f64 y, f64 w, f64 h) { nvgRect(vg, (f32)x, (f32)y, (f32)w, (f32)h); }
    void rect(DVec2 pos, DVec2 size) { nvgRect(vg, (f32)pos.x, (f32)pos.y, (f32)size.x, (f32)size.y); }
    void ellipse(f64 cx, f64 cy, f64 rx, f64 ry) { nvgEllipse(vg, (f32)(cx), (f32)(cy), (f32)(rx), (f32)(ry)); }
    void ellipse(DVec2 cen, DVec2 size) { nvgEllipse(vg, (f32)(cen.x), (f32)(cen.y), (f32)(size.x), (f32)(size.y)); }
    void fillRect(f64 x, f64 y, f64 w, f64 h) { nvgBeginPath(vg); nvgRect(vg, (f32)(x), (f32)(y), (f32)(w), (f32)(h)); nvgFill(vg); }
    void strokeRect(f64 x, f64 y, f64 w, f64 h) { nvgBeginPath(vg); nvgRect(vg, (f32)(x), (f32)(y), (f32)(w), (f32)(h)); nvgStroke(vg); }

    void strokeRoundedRect(f64 x, f64 y, f64 w, f64 h, f64 r)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (f32)(x), (f32)(y), (f32)(w), (f32)(h), (f32)(r));
        nvgStroke(vg);
    }
    void fillRoundedRect(f64 x, f64 y, f64 w, f64 h, f64 r)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (f32)(x), (f32)(y), (f32)(w), (f32)(h), (f32)(r));
        nvgFill(vg);
    }

    // ======== Image ========

    //void drawImage(Image& bmp, f64 x, f64 y, f64 w = 0, f64 h = 0) { 
    //    bmp.draw(vg, x, y, w <= 0 ? bmp.bmp_width : w, h <= 0 ? bmp.bmp_height : h); 
    //}

    //void drawImage(Image& bmp, DQuad quad) {
    //    bmp.drawSheared(vg, quad);
    //}

    // ======== Text ========
    void setTextAlign(TextAlign align)          { paint_ctx->text_align = align;       nvgTextAlign(vg, (int)(paint_ctx->text_align) | (int)(paint_ctx->text_baseline)); }
    void setTextBaseline(TextBaseline baseline) { paint_ctx->text_baseline = baseline; nvgTextAlign(vg, (int)(paint_ctx->text_align) | (int)(paint_ctx->text_baseline)); }
    virtual void setFontSize(f64 size_pts)      { paint_ctx->font_size_px = (f32)(paint_ctx->global_scale * size_pts); nvgFontSize(vg, paint_ctx->font_size_px); }
    void setFontSizePx(f64 size_px)             { paint_ctx->font_size_px = (f32)size_px; nvgFontSize(vg, (f32)size_px); }
    void setFont(NanoFont font)
    {
        if (font == paint_ctx->active_font)
            return;

        if (!font->created)
        {
            font->id = nvgCreateFont(vg, font->path.c_str(), font->path.c_str());
            font->created = true;

            nvgFontSize(vg, (f32)paint_ctx->global_scale * paint_ctx->font_size_px);
        }

        nvgFontFaceId(vg, font->id);
        paint_ctx->active_font = font;
    }


    [[nodiscard]] DRect boundingBox(std::string_view txt) const
    {
        f32 bounds[4];
        nvgTextBounds(vg, 0, 0, txt.data(), txt.data()+txt.size(), bounds);
        return DRect((f64)(bounds[0]), (f64)(bounds[1]), (f64)(bounds[2] - bounds[0]), (f64)(bounds[3] - bounds[1]));
    }

    void fillText(std::string_view txt, DVec2 pos) { fillText(txt, pos.x, pos.y); }
    void fillText(std::string_view txt, f64 x, f64 y)
    {
        if (!paint_ctx->active_font) setFont(paint_ctx->default_font);
        nvgText(vg, (f32)(x), (f32)(y), txt.data(), txt.data() + txt.size());
    }
};

class SurfaceInfo
{
    // ----- "local" rect info (not necessarily client-space) -----
    f64 x = 0, y = 0;
    f64 w = 0, h = 0;

    f64 start_w = 0;
    f64 start_h = 0;
    f64 old_w = 0;
    f64 old_h = 0;

    // If surface resized, keep track of the scale factor from the "start" size
    f64 scale_adjust = 1;

    // ----- "client" rect info (for SDL event coordinate conversions) -----
    f64 client_x = 0, client_y = 0;
    f64 client_w = 0, client_h = 0;

public:

    void setInitialSize()
    {
        start_w = w;
        start_h = h;
        scale_adjust = 1;
    }

    void setOldSize()
    {
        old_w = w;
        old_h = h;
    }

    void setSurfacePos(f64 _x, f64 _y)
    {
        x = _x;
        y = _y;
    }

    void setSurfaceSize(f64 _w, f64 _h)
    {
        w = _w;
        h = _h;

        DVec2 start_size = { start_w, start_h };
        DVec2 new_size = { w, h };

        if (start_size.x > 0.0 && start_size.y > 0.0)
            scale_adjust = std::min(new_size.x / start_size.x, new_size.y / start_size.y);
        else
            scale_adjust = 1.0;
    }

    void setClientRect(f64 _x, f64 _y, f64 _w, f64 _h)
    {
        client_x = _x;
        client_y = _y;
        client_w = _w;
        client_h = _h;
    }

    [[nodiscard]] constexpr f64   left()             const { return x; }
    [[nodiscard]] constexpr f64   top()              const { return y; }
    [[nodiscard]] constexpr f64   right()            const { return x + w; }
    [[nodiscard]] constexpr f64   bottom()           const { return y + h; }
                                                     
    [[nodiscard]] constexpr f64   width()            const { return w; }
    [[nodiscard]] constexpr f64   height()           const { return h; }
    [[nodiscard]] constexpr DVec2 size()             const { return DVec2(w, h); }
    [[nodiscard]] constexpr DRect surfaceRect()      const { return DRect(x, y, x + w, y + h); }
    [[nodiscard]] constexpr DRect clientRect()       const { return DRect(client_x, client_y, client_x + client_w, client_y + client_h); }

    [[nodiscard]] constexpr f64   initialSizeScale() const { return scale_adjust; }
    [[nodiscard]] constexpr bool  resized()          const { return (w != old_w) || (h != old_h); }

    [[nodiscard]] f64 toSurfaceX(f64 client_posX)    const { return ((client_posX - client_x) / client_w) * w; }
    [[nodiscard]] f64 toSurfaceY(f64 client_posY)    const { return ((client_posY - client_y) / client_h) * h; }
};

// ==================================
// ======== Advanced Painter ========
// ==================================
//
// > Pretransforms world coordinates (f64 precision) before rendering in screen-space with nanovg 
// > Supports toggling between screen-space / world-space
//

class Painter : private SimplePainter
{
    //friend class CameraInfo;
    friend class Viewport;

    DVec2 align_full(DVec2 p)         { return DVec2{ std::floor(p.x),       std::floor(p.y) };       }
    DVec2 align_full(f64 px, f64 py)  { return DVec2{ std::floor(px),        std::floor(py) };        }
    DVec2 align_half(DVec2 p)         { return DVec2{ std::floor(p.x) + 0.5, std::floor(p.y) + 0.5 }; }
    DVec2 align_half(f64 px, f64 py)  { return DVec2{ std::floor(px)  + 0.5, std::floor(py)  + 0.5 }; }

    FMat3 default_viewport_transform;
    f64 line_width = 1;

    bool transform_coordinates = true;
    bool scale_lines = true;
    bool scale_sizes = true;
    bool scale_text = true;
    bool rotate_text = true;

    bool saved_transform_coordinates = transform_coordinates;
    bool saved_scale_lines = scale_lines;
    bool saved_scale_sizes = scale_sizes;
    bool saved_scale_text = scale_text;
    bool saved_rotate_text = rotate_text;

    SurfaceInfo* surface = nullptr;
    WorldStageTransform m;

public:

    Painter(SurfaceInfo* s) : surface(s)
    {}

    f64 _avgAdjustedZoom() const {
        return m.avgZoomScaleFactor();
    }

    void worldCoordinates(bool b) { transform_coordinates = b; }
    void scalingLines(bool b)     { scale_lines = b; }
    void scalingSizes(bool b)     { scale_sizes = b; }
    void scalingText(bool b)      { scale_text = b; }
    void rotatingText(bool b)     { rotate_text = b; }

    void worldMode()
    {
        transform_coordinates = true;
        scale_lines = true;
        scale_sizes = true;
        scale_text = true;
        rotate_text = true;

        setLineWidth(line_width);
    }
    void stageMode()
    {
        transform_coordinates = false;
        scale_lines = false;
        scale_sizes = false;
        scale_text = false;
        rotate_text = false;

        // Don't preserve NanoVG transforms, start fresh
        SimplePainter::resetTransform();
        SimplePainter::transform(default_viewport_transform);
        setLineWidth(line_width);
    }
    void worldHudMode()
    {
        transform_coordinates = true;
        scale_lines = false;
        scale_sizes = false;
        scale_text = false;
        rotate_text = false;

        setLineWidth(line_width);
    }

    // todo: integrate with save()/load() stack?
    void saveCameraTransform()
    {
        saved_transform_coordinates = transform_coordinates;
        saved_scale_lines = scale_lines;
        saved_scale_sizes = scale_sizes;
        saved_scale_text = scale_text;
        saved_rotate_text = rotate_text;
    }
    void restoreCameraTransform()
    {
        transform_coordinates = saved_transform_coordinates;
        scale_lines = saved_scale_lines;
        scale_sizes = saved_scale_sizes;
        scale_text = saved_scale_text;
        rotate_text = saved_rotate_text;

        setLineWidth(line_width);
    }

    [[nodiscard]] WorldStageTransform worldStageTransform() const { return m; }
    [[nodiscard]] WorldStageTransform cameraTransform(const CameraInfo& camera) const { return camera.transformFor(*surface); }
    template<typename T> [[nodiscard]] const Mat3<T>& currentTransform() const { return m.stageTransform<T>(); }
    template<typename T> [[nodiscard]] const Mat3<T>& inverseTransform() const { return m.worldTransform<T>(); }

    void resetTransform()
    {
        m.reset();
        SimplePainter::resetTransform();
    }

    template<typename T>
    void transform(const Mat3<T>& _m) {
        if (transform_coordinates)
        {
            constexpr FloatType new_precision = float_index_v<T>;
            if (new_precision > m.maxPrecision())
                m.setMaxPrecision(new_precision); // todo: Allow downgrading? (especially when changing scene)

            m.transform(_m);
        }
        else
            SimplePainter::transform(static_cast<FMat3>(_m));
    }

    template<typename T>
    void setTransform(const Mat3<T>& _m)
    {
        if (transform_coordinates)
        {
            constexpr FloatType new_precision = float_index_v<T>;
            if (new_precision > m.maxPrecision())
                m.setMaxPrecision(new_precision); // todo: Allow downgrading? (especially when changing scene)

            m.reset();
            m.transform(_m);
        }
        else {
            SimplePainter::resetTransform();
            SimplePainter::transform(static_cast<FMat3>(_m));
        }
    }

    void transform(const WorldStageTransform& _m)
    {
        switch (_m.maxPrecision())
        {
            case FloatType::F32:
            case FloatType::F64:  transform<f64>((DMat3)_m);     break;
            case FloatType::FDD: transform<fdd>((DDMat3)_m); break;
            case FloatType::FQD: transform<fqd>((QDMat3)_m); break;
            default: break;
        }
    }

    void setTransform(const WorldStageTransform& _m)
    {
        switch (_m.maxPrecision())
        {
            case FloatType::F32:
            case FloatType::F64:  setTransform<f64>((DMat3)_m);     break;
            case FloatType::FDD: setTransform<fdd>((DDMat3)_m); break;
            case FloatType::FQD: setTransform<fqd>((QDMat3)_m); break;
            default: break;
        }
    }

    template<typename T> void translate(T x, T y) { if (transform_coordinates) m.translate(x, y); else SimplePainter::translate(x, y); }
    template<typename T> void scale(T s)          { if (transform_coordinates) m.scale(s);        else SimplePainter::scale(s); }
    void rotate(f64 r)                            { if (transform_coordinates) m.rotate(r);       else SimplePainter::rotate(r); }

    [[nodiscard]] DVec2 Offset(f64 stage_offX, f64 stage_offY) const
    {
        return m.toWorldOffset<f64>(stage_offX, stage_offY);
    }

    f64 lineScale() {
        return scale_lines ?
            1.0                         // Scaling already handled by world projection matrix
            : paint_ctx->global_scale;  // Stage-transform, fall back to DPR scaling
    }
    f64 sizeScale() {
        return scale_sizes ?
            1.0                         // Scaling already handled by world projection matrix
            : paint_ctx->global_scale;  // Stage-transform, fall back to DPR scaling
    }

private:

    struct ScopedResetTransform
    {
        Painter* painter;
        ScopedResetTransform(Painter* p) : painter(p) {
            p->SimplePainter::save();
            p->SimplePainter::resetTransform();
            p->SimplePainter::transform(painter->default_viewport_transform);
        }
        ~ScopedResetTransform() { painter->SimplePainter::restore(); }
    };

    // ======== Position/Size wrappers (applies only enabled camera transforms) ========

    // IF (transform_coordinates)    Input = World (f64/fdd/fqd),   Output = Stage (f64)
    // IF (!transform_coordinates)   Input = Stage (f64/fdd/fqd),   Output = Stage (f64)

    template<typename T> DVec2    PT(T x, T y)    const { return transform_coordinates ? m.toStage<T>(x, y) : Vec2{ (f64)x, (f64)y }; }
    template<typename T> DVec2    PT(Vec2<T> p)   const { return transform_coordinates ? m.toStage<T>(p) : static_cast<DVec2>(p); }
                                  
    template<typename T> DVec2    SIZE(T w, T h)  const { return scale_sizes ? m.toStageSideLengths<T>({w, h}) : DVec2{(f64)w, (f64)h}; }
    template<typename T> DVec2    SIZE(Vec2<T> s) const { return scale_sizes ? m.toStageSideLengths<T>(s) : s; }
    template<typename T> f64      SIZE(T radius)  const { return scale_sizes ? (_avgAdjustedZoom() * radius) : radius; }

    template<typename T> DQuad    QUAD(const Quad<T>& q) const { return { PT(q.a), PT(q.b), PT(q.c), PT(q.d) }; }

    template<typename T> void     ROTATE(T r)           { if (r != 0.0) SimplePainter::rotate(r); }
    template<typename T> void     TRANSLATE(T x, T y)   { SimplePainter::translate(PT(x, y)); }
    template<typename T> DVec2    TRANSFORMED(T x, T y, T w, T h, f64 rotation) {
        SimplePainter::translate(PT(x, y));
        if (rotation != 0.0)
            SimplePainter::rotate(rotation);
        return SIZE(w, h);
    }

public:

    // ======== Styles ========

    void setLineWidth(f64 w)
    {
        this->line_width = w;
        if (scale_lines)
            SimplePainter::setLineWidth(w * lineScale() * _avgAdjustedZoom());
        else
            SimplePainter::setLineWidth(w * lineScale());
    }

    // ======== Paths (overrides) ========

    template<typename T> void moveTo(T px, T py)                 { SimplePainter::moveTo(PT(px, py)); }
    template<typename T> void lineTo(T px, T py)                 { SimplePainter::lineTo(PT(px, py)); }
    template<typename T> void moveTo(Vec2<T> p)                  { SimplePainter::moveTo(PT(p)); }
    template<typename T> void lineTo(Vec2<T> p)                  { SimplePainter::lineTo(PT(p)); }

    template<typename T> void circle(T cx, T cy, T r)            { SimplePainter::circle(PT(cx, cy), SIZE(r)); }
    template<typename T> void circle(Vec2<T> cen, T r)           { SimplePainter::circle(PT(cen),    SIZE(r)); }
    template<typename T> void ellipse(T x, T y, T rx, T ry)      { SimplePainter::ellipse(PT(x, y),  SIZE(rx, ry)); }
    template<typename T> void ellipse(Vec2<T> cen, Vec2<T> size) { SimplePainter::ellipse(PT(cen),   SIZE(size)); }

    template<typename T> void arc(T cx, T cy, T r, T a0, T a1, PathWinding winding = PathWinding::WINDING_CCW) {
        SimplePainter::arc(PT(cx, cy), SIZE(r), a0, a1, winding);
    }
    template<typename T> void arc(DVec2 cen,  T r, T a0, T a1, PathWinding winding = PathWinding::WINDING_CCW) {
        SimplePainter::arc(PT(cen), SIZE(r), a0, a1, winding);
    }
    template<typename T> void arcTo(T x0, T y0, T x1, T y1, T r) {
        SimplePainter::arcTo(PT(x0, y0), PT(x1, y1), SIZE(r));
    }
    template<typename T> void arcTo(Vec2<T> p0, Vec2<T> p1, T r) {
        SimplePainter::arcTo(PT(p0), PT(p1), SIZE(r));
    }

    template<typename T> void bezierTo(T x1, T y1, T x2, T y2, T x3, T y3) {
        SimplePainter::bezierTo(PT(x1, y1), PT(x2, y2), PT(x3, y3));
    }
    template<typename T> void quadraticTo(T cx, T cy, T x, T y) {
        SimplePainter::quadraticTo(PT(cx, cy), PT(x, y));
    }

    template<typename PointT> void drawPath(const std::vector<PointT>& path)
    {
        if (path.size() < 2) return;
        size_t len = path.size();
        moveTo(path[0]);
        for (size_t i = 1; i < len; i++)
            lineTo(path[i]);
    }
    template<typename PointT, size_t N> void drawPath(const PointT(&path)[N])
    {
        if constexpr (N < 2) return;
        moveTo(path[0]);
        for (size_t i = 1; i < N; ++i)
            lineTo(path[i]);
    }
    template<typename PointT, size_t N> void drawClosedPath(const PointT(&path)[N])
    {
        if constexpr (N < 2) return;
        moveTo(path[0]);
        for (size_t i = 1; i < N; ++i)
            lineTo(path[i]);
        lineTo(path[0]);
    }

    template<typename PointT> void strokePath(const std::vector<PointT>& path)
    {
        if (path.size() < 2) return;
        beginPath();
        size_t len = path.size();
        moveTo(path[0]);
        for (size_t i = 1; i < len; i++)
            lineTo(path[i]);
        stroke();
    }
    template<typename PointT> void strokePath(const std::vector<PointT>& path, size_t i0, size_t i1)
    {
        size_t len = i1 - i0;
        if (len < 2) return;
        beginPath();
        moveTo(path[i0]);
        for (size_t i = i0+1; i < i1; i++)
            lineTo(path[i]);
        stroke();
    }

    // Linear (Fill)
    void setFillLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f)        { SimplePainter::setFillLinearGradient(PT(x0, y0), PT(x1, y1), inner, outer, a); }
    void setFillLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[4], const f32(&outer)[4])                      { SimplePainter::setFillLinearGradient(PT(x0, y0), PT(x1, y1), inner, outer); }
    void setFillLinearGradient(DVec2 p0, DVec2 p1, const Color& inner, const Color& outer)                                      { SimplePainter::setFillLinearGradient(PT(p0), PT(p1), inner, outer); }
    void setFillLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f)                    { SimplePainter::setFillLinearGradient(PT(p0), PT(p1), inner, outer, a); }
    void setFillLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[4], const f32(&outer)[4])                                  { SimplePainter::setFillLinearGradient(PT(p0), PT(p1), inner, outer); }
                                                                                                                                
    // Linear (Stroke)                                                                                                          
    void setStrokeLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const Color& inner, const Color& outer)                        { SimplePainter::setStrokeLinearGradient(PT(x0, y0), PT(x1, y1), inner, outer); }
    void setStrokeLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f)        { SimplePainter::setStrokeLinearGradient(PT(x0, y0), PT(x1, y1), inner, outer, a); }
    void setStrokeLinearGradient(f64 x0, f64 y0, f64 x1, f64 y1, const f32(&inner)[4], const f32(&outer)[4])                    { SimplePainter::setStrokeLinearGradient(PT(x0, y0), PT(x1, y1), inner, outer); }
    void setStrokeLinearGradient(DVec2 p0, DVec2 p1, const Color& inner, const Color& outer)                                    { SimplePainter::setStrokeLinearGradient(PT(p0), PT(p1), inner, outer); }
    void setStrokeLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f)                  { SimplePainter::setStrokeLinearGradient(PT(p0), PT(p1), inner, outer, a); }
    void setStrokeLinearGradient(DVec2 p0, DVec2 p1, const f32(&inner)[4], const f32(&outer)[4])                                { SimplePainter::setStrokeLinearGradient(PT(p0), PT(p1), inner, outer); }
                                                                                                                                
    // Radial (Fill)                                                                                                            
    void setFillRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f)          { SimplePainter::setFillRadialGradient(PT(cx, cy), SIZE(r0), SIZE(r1), inner, outer, a); }
    void setFillRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4])                      { SimplePainter::setFillRadialGradient(PT(cx, cy), SIZE(r0), SIZE(r1), inner, outer); }
    void setFillRadialGradient(DVec2 c, f64 r0, f64 r1, const Color& inner, const Color& outer)                                 { SimplePainter::setFillRadialGradient(PT(c), SIZE(r0), SIZE(r1), inner, outer); }
    void setFillRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f)               { SimplePainter::setFillRadialGradient(PT(c), SIZE(r0), SIZE(r1), inner, outer, a); }
    void setFillRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4])                             { SimplePainter::setFillRadialGradient(PT(c), SIZE(r0), SIZE(r1), inner, outer); }
                                                                                                                                
    // Radial (Stroke)                                                                                                          
    void setStrokeRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const Color& inner, const Color& outer)                        { SimplePainter::setStrokeRadialGradient(PT(cx, cy), SIZE(r0), SIZE(r1), inner, outer); }
    void setStrokeRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f)        { SimplePainter::setStrokeRadialGradient(PT(cx, cy), SIZE(r0), SIZE(r1), inner, outer, a); }
    void setStrokeRadialGradient(f64 cx, f64 cy, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4])                    { SimplePainter::setStrokeRadialGradient(PT(cx, cy), SIZE(r0), SIZE(r1), inner, outer); }
    void setStrokeRadialGradient(DVec2 c, f64 r0, f64 r1, const Color& inner, const Color& outer)                               { SimplePainter::setStrokeRadialGradient(PT(c), SIZE(r0), SIZE(r1), inner, outer); }
    void setStrokeRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[3], const f32(&outer)[3], f32 a = 1.0f)             { SimplePainter::setStrokeRadialGradient(PT(c), SIZE(r0), SIZE(r1), inner, outer, a); }
    void setStrokeRadialGradient(DVec2 c, f64 r0, f64 r1, const f32(&inner)[4], const f32(&outer)[4])                           { SimplePainter::setStrokeRadialGradient(PT(c), SIZE(r0), SIZE(r1), inner, outer); }
                                                                                                                                
    // Box (Fill)                                                                                                               
    void setFillBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const Color& inner, const Color& outer)                   { SimplePainter::setFillBoxGradient(PT(x, y), SIZE(w, h), SIZE(r), SIZE(f), inner, outer); }
    void setFillBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f)   { SimplePainter::setFillBoxGradient(PT(x, y), SIZE(w, h), SIZE(r), SIZE(f), inner, outer, a); }
    void setFillBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4])               { SimplePainter::setFillBoxGradient(PT(x, y), SIZE(w, h), SIZE(r), SIZE(f), inner, outer); }
    void setFillBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const Color& inner, const Color& outer)                        { SimplePainter::setFillBoxGradient(PT(pos), SIZE(size), SIZE(r), SIZE(f), inner, outer); }
    void setFillBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f)        { SimplePainter::setFillBoxGradient(PT(pos), SIZE(size), SIZE(r), SIZE(f), inner, outer, a); }
    void setFillBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4])                    { SimplePainter::setFillBoxGradient(PT(pos), SIZE(size), SIZE(r), SIZE(f), inner, outer); }
                                                                                                                                
    // Box (Stroke)                                                                                                             
    void setStrokeBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const Color& inner, const Color& outer)                 { SimplePainter::setStrokeBoxGradient(PT(x, y), SIZE(w, h), SIZE(r), SIZE(f), inner, outer); }
    void setStrokeBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f) { SimplePainter::setStrokeBoxGradient(PT(x, y), SIZE(w, h), SIZE(r), SIZE(f), inner, outer, a); }
    void setStrokeBoxGradient(f64 x, f64 y, f64 w, f64 h, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4])             { SimplePainter::setStrokeBoxGradient(PT(x, y), SIZE(w, h), SIZE(r), SIZE(f), inner, outer); }
    void setStrokeBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const Color& inner, const Color& outer)                      { SimplePainter::setStrokeBoxGradient(PT(pos), SIZE(size), SIZE(r), SIZE(f), inner, outer); }
    void setStrokeBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[3], const f32(&outer)[3], f32 a=1.0f)      { SimplePainter::setStrokeBoxGradient(PT(pos), SIZE(size), SIZE(r), SIZE(f), inner, outer, a); }
    void setStrokeBoxGradient(DVec2 pos, DVec2 size, f64 r, f64 f, const f32(&inner)[4], const f32(&outer)[4])                  { SimplePainter::setStrokeBoxGradient(PT(pos), SIZE(size), SIZE(r), SIZE(f), inner, outer); }


    // ======== Shapes ========

    template<typename T> void strokeRect(T x, T y, T w, T h)
    {
        ScopedResetTransform t(this);
        DVec2 s = TRANSFORMED(x, y, w, h, m._angle<T>());
        SimplePainter::strokeRect(0, 0, s.x, s.y);
    }
    template<typename T> void fillRect(T x, T y, T w, T h)
    {
        ScopedResetTransform t(this);
        DVec2 s = TRANSFORMED(x, y, w, h, m._angle<T>());
        SimplePainter::fillRect(0, 0, s.x, s.y);
    }
    template<typename T> void strokeRoundedRect(T x, T y, T w, T h, T r) {
        ScopedResetTransform t(this);
        DVec2 s = TRANSFORMED(x, y, w, h, m._angle<T>());
        SimplePainter::strokeRoundedRect(0, 0, s.x, s.y, SIZE(r));
    }
    template<typename T> void fillRoundedRect(T x, T y, T w, T h, T r)
    {
        ScopedResetTransform t(this);
        DVec2 s = TRANSFORMED(x, y, w, h, m._angle<T>());
        SimplePainter::fillRoundedRect(0, 0, s.x, s.y, SIZE(r));
    }
    template<typename T> void strokeEllipse(T cx, T cy, T rx, T ry)
    {
        ScopedResetTransform t(this);
        DVec2 s = TRANSFORMED(cx, cy, rx, ry, m._angle<T>());
        SimplePainter::beginPath();
        SimplePainter::ellipse(0, 0, s.x, s.y);
        SimplePainter::stroke();
    }
    template<typename T> void fillEllipse(T cx, T cy, T rx, T ry)
    {
        ScopedResetTransform t(this);
        DVec2 s = TRANSFORMED(cx, cy, rx, ry, m._angle<T>());
        SimplePainter::beginPath();
        SimplePainter::ellipse(0, 0, s.x, s.y);
        SimplePainter::fill();
    }

    // Overloads
    template<typename T> void strokeQuad(const Quad<T>& q)                             { beginPath(); drawClosedPath<Vec2<T>>({q.a, q.b, q.c, q.d}); stroke(); }
    template<typename T> void strokeQuad(const Quad<T>& q, Color col)                  { setStrokeStyle(col); beginPath(); drawClosedPath<Vec2<T>>({ q.a, q.b, q.c, q.d }); stroke(); }
    template<typename T> void fillQuad(const Quad<T>& q)                               { beginPath(); drawClosedPath<Vec2<T>>({ q.a, q.b, q.c, q.d }); fill(); }
    template<typename T> void fillQuad(const Quad<T>& q, Color col)                    { setFillStyle(col); beginPath(); drawClosedPath<Vec2<T>>({ q.a, q.b, q.c, q.d }); fill(); }
    template<typename T> void strokeRect(const Rect<T>& r)                             { strokeRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1); }
    template<typename T> void strokeRect(const Rect<T>& r, Color col)                  { setStrokeStyle(col); strokeRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1); }
    template<typename T> void fillRect(const Rect<T>& r)                               { fillRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1); }
    template<typename T> void fillRect(T x, T y, T w, T h, Color col)                  { setFillStyle(col); fillRect<T>(x, y, w, h); }
    template<typename T> void fillRect(const Rect<T>& r, Color col)                    { setFillStyle(col); fillRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1); }
    template<typename T> void strokeRoundedRect(const Rect<T>& r, T radius)            { strokeRoundedRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1, radius); }
    template<typename T> void strokeRoundedRect(const Rect<T>& r, T radius, Color col) { setStrokeStyle(col); strokeRoundedRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1, radius); }
    template<typename T> void fillRoundedRect(const Rect<T>& r, T radius)              { fillRoundedRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1, radius); }
    template<typename T> void fillRoundedRect(const Rect<T>& r, T radius, Color col)   { setFillStyle(col); fillRoundedRect<T>(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1, radius); }
    template<typename T> void strokeEllipse(T cx, T cy, T r)                           { strokeEllipse<T>(cx, cy, r, r); }
    template<typename T> void strokeEllipse(T cx, T cy, T r, Color col)                { setStrokeStyle(col); strokeEllipse<T>(cx, cy, r, r); }
    template<typename T> void fillEllipse(T cx, T cy, T r)                             { fillEllipse<T>(cx, cy, r, r); }
    template<typename T> void fillEllipse(T cx, T cy, T r, Color col)                  { setFillStyle(col); fillEllipse<T>(cx, cy, r, r); }
    template<typename T> void strokeEllipse(Vec2<T> cen, T r)                          { strokeEllipse<T>(cen.x, cen.y, r, r); }
    template<typename T> void strokeEllipse(Vec2<T> cen, T r, Color col)               { setStrokeStyle(col); strokeEllipse<T>(cen.x, cen.y, r, r); }
    template<typename T> void fillEllipse(Vec2<T> cen, T r)                            { fillEllipse<T>(cen.x, cen.y, r, r); }
    template<typename T> void fillEllipse(Vec2<T> cen, T r, Color col)                 { setFillStyle(col); fillEllipse<T>(cen.x, cen.y, r, r); }

    template<typename T> void drawArrow(Vec2<T> a, Vec2<T> b, Color color=Color(255,255,255), f64 tip_angle=35, f64 tip_scale=1.0, bool fill_tip=true)
    {
        f64 dx = b.x - a.x;
        f64 dy = b.y - a.y;
        f64 angle = std::atan2(dy, dx);
        const f64 tip_sharp_angle = math::toRadians(180.0 - tip_angle);
        f64 arrow_size;

        if (transform_coordinates)
        {
            arrow_size = (line_width * 4 * tip_scale) / _avgAdjustedZoom();
        }
        else
        {
            arrow_size = (line_width * 4 * tip_scale) / (scale_lines ? _avgAdjustedZoom() : 1);
        }

        DVec2 c = b - (b - a).normalized() * arrow_size * 0.7;

        f64 rx1 = b.x + std::cos(angle + tip_sharp_angle) * arrow_size;
        f64 ry1 = b.y + std::sin(angle + tip_sharp_angle) * arrow_size;
        f64 rx2 = b.x + std::cos(angle - tip_sharp_angle) * arrow_size;
        f64 ry2 = b.y + std::sin(angle - tip_sharp_angle) * arrow_size;

        if (fill_tip)
        {
            setLineCap(LineCap::CAP_ROUND);
            setFillStyle(color);
            setStrokeStyle(color);
            beginPath();
            moveTo(a);
            lineTo(c);
            stroke();

            beginPath();
            moveTo(b);
            lineTo(rx1, ry1);
            lineTo(rx2, ry2);
            fill();
        }
        else // Stroke tip
        {
            setLineCap(LineCap::CAP_ROUND);
            setFillStyle(color);
            setStrokeStyle(color);
            beginPath();
            moveTo(a);
            lineTo(b);
            stroke();

            strokeLine(b, {rx1, ry1});
            strokeLine(b, {rx2, ry2});
        }
    }

    void transform(const CameraInfo& camera)
    {
        transform(cameraTransform(camera));
    }

    void setTransform(const CameraInfo& camera)
    {
        setTransform(cameraTransform(camera));
    }

    // ======== Image ========

    //void drawImage(Image& bmp, f64 x, f64 y, f64 w = 0, f64 h = 0) {
    //    SimplePainter::drawImage(bmp, x, y, w <= 0 ? bmp.bmp_width : w, h <= 0 ? bmp.bmp_height : h);
    //}

    template<typename T>
    void drawImage(const Image& bmp, Quad<T> q)
    {
        DQuad quad = { PT(q.a), PT(q.b), PT(q.c), PT(q.d) };

        DVec2 a = quad.a;
        DVec2 u = quad.b - quad.a;
        DVec2 v = quad.d - quad.a;

        bmp.refreshData(vg);

        nvgSave(vg);
        nvgResetTransform(vg);
        nvgTransform(vg, (f32)u.x, (f32)u.y, (f32)v.x, (f32)v.y, (f32)a.x, (f32)a.y);
        NVGpaint paint = nvgImagePattern(vg, 0, 0, 1, 1, 0.0f, bmp.imageId(), 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, 1, 1);
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        nvgRestore(vg);
    }
    template<typename T>
    void drawImage(const WorldImageT<T>& bmp)
    {
        drawImage(bmp, bmp.worldQuad());
    }

    template<typename T>
    void drawShaderSurface(const ShaderSurface& surf, Quad<T> q, f32 alpha = 1.0f)
    {
        int img = surf.nvgImageId(vg);
        if (!img)
            return;

        DQuad quad = { PT(q.a), PT(q.b), PT(q.c), PT(q.d) };

        DVec2 a = quad.a;
        DVec2 u = quad.b - quad.a;
        DVec2 v = quad.d - quad.a;

        SimplePainter::save();
        SimplePainter::resetTransform();
        SimplePainter::transform(default_viewport_transform);
        SimplePainter::transform((f32)u.x, (f32)u.y, (f32)v.x, (f32)v.y, (f32)a.x, (f32)a.y);

        PaintStyle paint = SimplePainter::imagePattern(0, 0, 1, 1, 0.0f, img, alpha);

        SimplePainter::beginPath();
        SimplePainter::rect(0, 0, 1, 1);
        SimplePainter::setFillStyle(paint);
        SimplePainter::fill();
        SimplePainter::restore();
    }

    //void drawShaderSurface(const ShaderSurface& surf, f64 x, f64 y, f64 w, f64 h, f64 alpha = 1.0) const
    //{
    //    int img = surf.nvgImageId(vg);
    //    if (!img)
    //        return;
    //
    //    NVGpaint paint = nvgImagePattern(vg, x, y, w, h, 0.0f, img, alpha);
    //
    //    nvgBeginPath(vg);
    //    nvgRect(vg, x, y, w, h);
    //    nvgFillPaint(vg, paint);
    //    nvgFill(vg);
    //}

    // ======== Text ========

    void setFontSize(f64 size_pts) {
        SimplePainter::setFontSizePx((f32)(sizeScale() * size_pts));

    }

    template<typename T> void fillText(std::string_view txt, T px, T py)
    {
        ScopedResetTransform t(this);
        TRANSLATE(px, py);

        if (rotate_text)
            ROTATE(m._angle<T>());

        if (scale_text)
        {
            DVec2 z = m._zoom<T>();
            SimplePainter::scale(z.x, z.y);
        }

        SimplePainter::fillText(txt, 0, 0);
    }
    template<typename T> void fillText(std::string_view txt, const Vec2<T>& p) {
        fillText(txt, p.x, p.y);
    }

    template<typename T=f64> [[nodiscard]] Rect<T> boundingBox(std::string_view txt)
    {
        SimplePainter::save();
        SimplePainter::resetTransform();
        SimplePainter::transform(default_viewport_transform);
        auto r = SimplePainter::boundingBox(txt);
        SimplePainter::restore();
        return r;
    }

private:

    [[nodiscard]] std::string formatNumberScientific(f64 v, int decimals, f64 fixed_min = 0.001, f64 fixed_max = 100000);
    [[nodiscard]] std::string formatNumberScientific(fdd v, int decimals, f64 fixed_min = 0.001, f64 fixed_max = 100000);
    [[nodiscard]] std::string formatNumberScientific(fqd v, int decimals, f64 fixed_min = 0.001, f64 fixed_max = 100000);

    const f64 exponent_font_scale = 0.85;
    const f64 exponent_spacing_x = 0.06;
    const f64 exponent_spacing_y = -0.3;

public:

    // convert value to string first with ctx->prepareNumberScientific(n) or bl::to_string()
    template<typename PosT=f64, typename ValT> void fillNumberScientific(std::string& txt, Vec2<PosT> pos, f64 fontSize = 12.0)
    {
        size_t ePos = txt.find("e");
        if (ePos != std::string::npos)
        {
            const int exponent = std::stoi(txt.substr(ePos + 1));
            std::string mantissa_txt = txt.substr(0, ePos) + "e";
            std::string exponent_txt = std::to_string(exponent);

            f64 mantissaWidth = boundingBox<PosT>(mantissa_txt).x2 + exponent_spacing_x;

            /// todo: Take whatever alignment you're given and adjust right bound
            setTextAlign(TextAlign::ALIGN_CENTER);
            setFontSize(fontSize);

            pos = pos.floored();
            fillTextSharp(mantissa_txt.c_str(), pos);

            pos.x += PosT{ mantissaWidth } / PosT{ 2 } + PosT{ fontSize * exponent_spacing_x };
            pos.y -= PosT{ fontSize * (exponent_font_scale + exponent_spacing_y) };

            setTextAlign(TextAlign::ALIGN_LEFT);
            setFontSize(fontSize * exponent_font_scale);

            fillTextSharp(exponent_txt.c_str(), pos);

            setFontSize(fontSize);
            setTextAlign(TextAlign::ALIGN_CENTER);
        }
        else
        {
            setFontSize(fontSize);
            fillTextSharp(txt.c_str(), pos);
        }
    }
    
	template<typename PosT=f64, typename ValT> [[nodiscard]] Rect<PosT> boundingBoxScientific(std::string& txt, f64 fontSize = 12.0)
    {
        size_t ePos = txt.find("e");
        if (ePos != std::string::npos)
        {
            const int  exponent = std::stoi(txt.substr(ePos + 1));
            std::string mantissa_txt = txt.substr(0, ePos) + "e";
            std::string exponent_txt = std::to_string(exponent);

            Rect<PosT> mantissaRect = boundingBox<PosT>(mantissa_txt);
            Rect<PosT> exponentRect = boundingBox<PosT>(exponent_txt);
            Rect<PosT> ret = mantissaRect;

            ret.x2 += exponentRect.width() + PosT{ fontSize * exponent_spacing_x };
            ret.y1 -= PosT{ fontSize * (exponent_font_scale + exponent_spacing_y) };

            return ret;
        }
        else
        {
            return boundingBox(txt);
        }
    }

    // ======== Sharp variants ========

    template<typename T> void moveToSharp(T px, T py)  { SimplePainter::moveTo(align_half(PT(px, py))); }
    template<typename T> void moveToSharp(Vec2<T> p)   { SimplePainter::moveTo(align_half(PT(p))); }
    template<typename T> void lineToSharp(T px, T py)  { SimplePainter::lineTo(align_half(PT(px, py))); }
    template<typename T> void lineToSharp(Vec2<T> p)   { SimplePainter::lineTo(align_half(PT(p.x, p.y))); }

    template<typename T> void strokeLine(Vec2<T> p1, Vec2<T> p2)
    {
        beginPath();
        moveTo(p1);
        lineTo(p2);
        stroke();
    }
    template<typename T> void strokeLineSharp(Vec2<T> p1, Vec2<T> p2)
    {
        beginPath();
        moveToSharp(p1);
        lineToSharp(p2);
        stroke();
    }

    template<typename T> void fillTextSharp(std::string_view txt, Vec2<T> pos)
    {
        SimplePainter::fillText(txt, align_full(PT(pos)));
    }

    // ======== Axis ========

    void drawWorldAxis(
        f64 axis_opacity = 0.3,
        f64 grid_opacity = 0.075,
        f64 text_opacity = 0.45
    );

    // ======== Extras ========

    void drawCursor(f64 x, f64 y, f64 size = 24.0);


    // ======== Expose unchanged methods ========

    using SimplePainter::setTargetPainterContext;
    using SimplePainter::setGlobalScale;
    using SimplePainter::getGlobalScale;
    using SimplePainter::setGlobalAlpha;
    //
    using SimplePainter::getDefaultFont;
    //
    using SimplePainter::save;
    using SimplePainter::restore;
    using SimplePainter::skewX; // todo: untested
    using SimplePainter::skewY; // todo: untested
    //
    using SimplePainter::setClipRect;   // todo: untested
    using SimplePainter::resetClipping; // todo: untested
    //
    using SimplePainter::setFont;
    using SimplePainter::setTextAlign;
    using SimplePainter::setTextBaseline;
    //
    // ======== composite ========

    using SimplePainter::setCompositeSourceOver;
    using SimplePainter::setCompositeSourceIn;
    using SimplePainter::setCompositeSourceOut;
    using SimplePainter::setCompositeSourceAtop;
    using SimplePainter::setCompositeDestOver;
    using SimplePainter::setCompositeDestIn;
    using SimplePainter::setCompositeDestOut;
    using SimplePainter::setCompositeDestAtop;
    using SimplePainter::setCompositeLighter;
    using SimplePainter::setCompositeCopy;
    using SimplePainter::setCompositeXor;
    using SimplePainter::setComposite;

    // ======== blending ========

    using SimplePainter::setBlendFunc;
    using SimplePainter::setBlendFuncSeparate;
    using SimplePainter::setBlendAdditive;
    using SimplePainter::setBlendAlphaPremult;
    using SimplePainter::setBlendAlphaStraight;
    using SimplePainter::setBlendMultiply;
    using SimplePainter::setBlendScreen;

    using SimplePainter::scopedComposite;
    using SimplePainter::scopedBlend;
    using SimplePainter::scopedBlendSeparate;

    // ======== line cap/join/miter ========

    using SimplePainter::setLineCap;
    using SimplePainter::setLineJoin;
    using SimplePainter::setMiterLimit; // todo: Use world size?

    //
    using SimplePainter::setStrokeStyle;
    using SimplePainter::setFillStyle;
    using SimplePainter::beginPath;
    using SimplePainter::stroke;
    using SimplePainter::fill;
    using SimplePainter::closePath;
};

// todo: currently unused, but lots of places could do with using this instead (e.g. preprocessor, shader surfaces, Canvas, etc)
struct GLSurface
{
    GLuint fbo = 0;
    GLuint tex = 0;
    GLuint rbo = 0;
    int width = 0;
    int height = 0;
    bool has_depth_stencil = false;

    void destroy()
    {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (tex) glDeleteTextures(1, &tex);
        if (rbo) glDeleteRenderbuffers(1, &rbo);
        fbo = tex = rbo = 0;
        width = height = 0;
        has_depth_stencil = false;
    }

    bool resize(int w, int h, bool want_depth_stencil)
    {
        if (w <= 0 || h <= 0) return false;
        if (w == width && h == height && want_depth_stencil == has_depth_stencil) return false;

        destroy();

        width = w;
        height = h;
        has_depth_stencil = want_depth_stencil;

        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);

        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

        if (want_depth_stencil)
        {
            glGenRenderbuffers(1, &rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }
};


class NanoCanvas : public SimplePainter
{
    GLuint fbo = 0, tex = 0, rbo = 0;
    bool has_fbo = false;
    int fbo_w = 0, fbo_h = 0;  // Local Canvas dimensions (FBO size)

    // actual size Canvas gets drawn on to screen
    IRect client_rect{};

    // persistent states shared with any painters that target this canvas
    PainterContext context;

    std::atomic<bool> dirty = false;

public:

    void create(f64 global_scale);
    bool resize(int w, int h);

    ~NanoCanvas();

    IRect clientRect() const { return client_rect; }
    void setClientRect(IRect r) { client_rect = r; }

    void begin(f32 r, f32 g, f32 b, f32 a = 1.0);
    void end();

    void setDirty(bool b=true) { dirty.store(b, std::memory_order_relaxed); }
    bool isDirty() const { return dirty.load(std::memory_order_acquire); }

    PainterContext* getPainterContext() { return &context; }
    GLuint texture() const { return tex; }
    [[nodiscard]] int fboWidth()  const { return fbo_w; }
    [[nodiscard]] int fboHeight() const { return fbo_h; }
    [[nodiscard]] IVec2 fboSize() const { return {fbo_w, fbo_h}; }
    [[nodiscard]] int fboExists() const { return has_fbo; }

    bool readPixels(std::vector<uint8_t>& out_rgba);
};

BL_END_NS
