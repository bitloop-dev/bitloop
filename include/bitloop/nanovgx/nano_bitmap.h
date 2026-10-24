#pragma once

#include "nanovgx.h"

#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>

#include <bitloop/util/color.h>
#include <bitloop/core/world_object.h>
#include <bitloop/core/raster_grid.h>

BL_BEGIN_NS

class Image : public virtual RasterGrid
{
    friend class SimplePainter;
    friend class Painter;

protected:

    // mutable: silently refresh nanovg data when necessary
    mutable int nano_img = 0;
    mutable bool pending_resize = false;

    // non-mutable
    std::vector<uint8_t> pixels;

    uint32_t* colors;

public:

    Image() : RasterGrid(0, 0), colors(nullptr) {}

    [[nodiscard]] int imageId() const { return nano_img; }
    [[nodiscard]] const uint8_t* data() const { return pixels.data(); }

    void create(int w, int h) 
    {
        //raster_w = w; raster_h = h;
        RasterGrid::setRasterSize(w, h);
        pixels.assign(size_t(w) * h * 4, 0);
        colors = reinterpret_cast<uint32_t*>(&pixels.front());
        pending_resize = true;
    }
    void clear(Color c)
    {
        if (pixels.size() == 0)
            return;
        uint32_t u32 = c.rgba;
        uint32_t* pixel = colors;
        uint32_t count = rasterCount();
        //uint32_t count = raster_w * raster_h;
        for (uint32_t i=0; i< count; i++)
            *pixel++ = u32;
    }
    void clear(int r, int g, int b, int a)
    {
        clear(Color(r, g, b, a));
    }

    void setPixel(int x, int y, uint32_t rgba)
    {
        size_t i = (size_t(y) * raster_w + x);
        colors[i] = rgba;
    }
    void setPixel(int x, int y, int r, int g, int b, int a=255)
    {
        size_t i = (size_t(y) * raster_w + x) * 4;
        pixels[i++] = r;
        pixels[i++] = g;
        pixels[i++] = b;
        pixels[i++] = a;
    }
    void setPixelSafe(int x, int y, uint32_t rgba)
    {
        if ((unsigned)x >= (unsigned)raster_w ||
            (unsigned)y >= (unsigned)raster_h)
        {
            return;
        }
        size_t i = (size_t(y) * raster_w + x) * 4;
        pixels[i + 0] = rgba & 0xFF;
        pixels[i + 1] = (rgba >> 8) & 0xFF;
        pixels[i + 2] = (rgba >> 16) & 0xFF;
        pixels[i + 3] = (rgba >> 24) & 0xFF;
    }
    void setPixelSafe(int x, int y, int r, int g, int b, int a = 255)
    {
        if ((unsigned)x >= (unsigned)raster_w ||
            (unsigned)y >= (unsigned)raster_h)
        {
            return;
        }
        size_t i = (size_t(y) * raster_w + x) * 4;
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }

    [[nodiscard]] Color getPixel(int x, int y) const
    {
        size_t i = (size_t(y) * raster_w + x) * 4;
        return 
            pixels[i] |
            pixels[i + 1] << 8 | 
            pixels[i + 2] << 16 | 
            pixels[i + 3] << 24;
    }
    [[nodiscard]] Color getPixelSafe(int x, int y) const
    {
        if ((unsigned)x >= (unsigned)raster_w ||
            (unsigned)y >= (unsigned)raster_h)
        {
            return 0;
        }

        size_t i = (size_t(y) * raster_w + x) * 4;
        return
            pixels[i] | 
            pixels[i + 1] << 8 | 
            pixels[i + 2] << 16 | 
            pixels[i + 3] << 24;
    }
    [[nodiscard]] uint32_t* getU32PtrSafe(int x, int y)
    {
        if ((unsigned)x >= (unsigned)raster_w ||
            (unsigned)y >= (unsigned)raster_h)
        {
            return nullptr;
        }
        return colors + (y * raster_w + x);
    }

protected:

    void refreshData(NVGcontext* vg) const
    {
        if (raster_w <= 0 || raster_h <= 0)
            return;

        if (pending_resize)
        {
            if (nano_img) nvgDeleteImage(vg, nano_img);
            //nano_img = nvgCreateImageRGBA(vg, raster_w, raster_h, NVG_IMAGE_NEAREST, pixels.data());
            nano_img = nvgCreateImageRGBA(vg, raster_w, raster_h, NVG_IMAGE_GENERATE_MIPMAPS, pixels.data());
            pending_resize = false;
        }
        else
        {
            nvgUpdateImage(vg, nano_img, pixels.data());
            if (nano_img)
            {
                // NanoVG updates level zero only; minification must sample the latest pixels too.
                GLint previousTexture = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
                glBindTexture(GL_TEXTURE_2D, nvglImageHandle(vg, nano_img));
                glGenerateMipmap(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
            }
        }
    }

    /*void draw(NVGcontext* vg, double x, double y, double w, double h)
    {
        if (raster_w <= 0 || raster_h <= 0)
            return;

        if (pending_resize)
        {
            if (nano_img) nvgDeleteImage(vg, nano_img);
            nano_img = nvgCreateImageRGBA(vg, raster_w, raster_h, NVG_IMAGE_NEAREST, pixels.data());
            pending_resize = false;
        }
        else
        {
            nvgUpdateImage(vg, nano_img, pixels.data());
        }

        float _x = static_cast<float>(x);
        float _y = static_cast<float>(y);
        float _w = static_cast<float>(w);
        float _h = static_cast<float>(h);


        NVGpaint p = nvgImagePattern(vg, _x, _y, _w, _h, 0.0f, nano_img, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, _x, _y, _w, _h);
        nvgFillPaint(vg, p);
        nvgFill(vg);
    }

    void drawSheared(NVGcontext* vg, DQuad quad)
    {
        if (raster_w <= 0 || raster_h <= 0)
            return;

        if (pending_resize)
        {
            if (nano_img) nvgDeleteImage(vg, nano_img);
            nano_img = nvgCreateImageRGBA(vg, raster_w, raster_h, NVG_IMAGE_NEAREST, pixels.data());
            pending_resize = false;
        }
        else
        {
            nvgUpdateImage(vg, nano_img, pixels.data());
        }

        DVec2 a = quad.a;
        DVec2 u = quad.b - quad.a;
        DVec2 v = quad.d - quad.a;

        nvgSave(vg);
        nvgTransform(vg,
            (float)u.x, (float)u.y,
            (float)v.x, (float)v.y,
            (float)a.x, (float)a.y);

        NVGpaint paint = nvgImagePattern(vg, 0, 0, 1, 1, 0.0f, nano_img, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, 1, 1);
        nvgFillPaint(vg, paint);
        nvgFill(vg);

        nvgRestore(vg);
    }*/
};

template<typename T=f64>
class WorldImageT : public Image, public WorldRasterGridT<T>
{
    bool needs_reshading = false;
    Quad<T> prev_world_quad;

    friend class PaintContext;

public:

    void setNeedsReshading(bool b = true)
    {
        needs_reshading = b;
    }

    [[nodiscard]] bool needsReshading()
    {
        Quad<T> world_quad = WorldObjectT<T>::worldQuad();
        if (needs_reshading || (world_quad != prev_world_quad))
        {
            needs_reshading = false; // todo: Move to when drawn?
            prev_world_quad = world_quad;
            return true;
        }
        prev_world_quad = world_quad;
        return false;
    }

    void setRasterSize(int bmp_w, int bmp_h) override
    {
        // resizing raster grid should force reshade flag dirty
        if (bmp_w < 0) bmp_w = 0;
        if (bmp_h < 0) bmp_h = 0;

        if (raster_w != bmp_w || raster_h != bmp_h)
        {
            Image::create(bmp_w, bmp_h);
            needs_reshading = true;
        }
    }
};

typedef WorldImageT<f64> WorldImage;
typedef WorldImageT<fdd> WorldImage128;
typedef WorldImageT<fqd> WorldImage256;

BL_END_NS
