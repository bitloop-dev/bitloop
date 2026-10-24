#include <bitloop/core/image_loader.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>

#include "stb_image.h"
#include <webp/decode.h>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

BL_BEGIN_NS;

static std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string file_ext_lower(const std::string& path)
{
    std::filesystem::path p(path);
    return to_lower(p.extension().string());
}

static std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::string& path, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "Failed to open file: " + path;
        return std::nullopt;
    }

    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size <= 0) {
        if (err) *err = "File is empty or size could not be determined: " + path;
        return std::nullopt;
    }
    f.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes((size_t)size);
    if (!f.read((char*)bytes.data(), size)) {
        if (err) *err = "Failed to read file bytes: " + path;
        return std::nullopt;
    }
    return bytes;
}

static bool load_webp_rgba8(const std::string& path, ImageRGBA8& out, std::string* err)
{
    auto bytes_opt = read_file_bytes(path, err);
    if (!bytes_opt) return false;
    const auto& bytes = *bytes_opt;

    int w = 0, h = 0;
    if (!WebPGetInfo(bytes.data(), bytes.size(), &w, &h)) {
        if (err) *err = "WebPGetInfo failed (invalid or truncated WebP): " + path;
        return false;
    }

    const size_t stride = (size_t)w * 4u;
    const size_t buf_sz = (size_t)h * stride;

    out.w = w;
    out.h = h;
    out.pixels.resize(buf_sz);

    if (WebPDecodeRGBAInto(bytes.data(), bytes.size(),
        out.pixels.data(), out.pixels.size(),
        (int)stride) == nullptr)
    {
        if (err) *err = "WebPDecodeRGBAInto failed: " + path;
        return false;
    }

    return true;
}

static bool load_svg_rgba8(const std::string& path, ImageRGBA8& out, int targetW, int targetH, std::string* err)
{
    NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
    if (!image) {
        if (err) *err = "nsvgParseFromFile failed: " + path;
        return false;
    }

    const int outW = (targetW > 0) ? targetW : (int)std::max(1.0f, image->width);
    const int outH = (targetH > 0) ? targetH : (int)std::max(1.0f, image->height);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        if (err) *err = "nsvgCreateRasterizer failed.";
        return false;
    }

    out.w = outW;
    out.h = outH;
    out.pixels.assign((size_t)outW * (size_t)outH * 4u, 0);

    // Preserve your current “fit while keeping aspect ratio” behavior.
    const float scaleX = (float)outW / image->width;
    const float scaleY = (float)outH / image->height;
    const float scale = (scaleX < scaleY) ? scaleX : scaleY;

    const int stride = outW * 4;
    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, out.pixels.data(), outW, outH, stride);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return true;
}

static bool load_stb_rgba8(const std::string& path, ImageRGBA8& out, std::string* err)
{
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4); // force RGBA
    if (!data) {
        if (err) *err = std::string("stbi_load failed: ") + stbi_failure_reason();
        return false;
    }

    out.w = w;
    out.h = h;
    out.pixels.assign(data, data + (size_t)w * (size_t)h * 4u);
    stbi_image_free(data);
    return true;
}

bool loadImageRGBA8(const char* path, ImageRGBA8& out, int svgTargetW, int svgTargetH, std::string* err)
{
    out = {};

    const std::string full = bl::platform()->path(path);
    const std::string ext = file_ext_lower(full);

    if (ext == ".svg")
        return load_svg_rgba8(full, out, svgTargetW, svgTargetH, err);

    // Prefer libwebp for .webp to avoid relying on stb’s build flags/capability.
    if (ext == ".webp")
        return load_webp_rgba8(full, out, err);

    // Otherwise: try stb for the common formats.
    if (load_stb_rgba8(full, out, err))
        return true;

    return false;
}


GLuint createGLTextureRGBA8(const void* pixels, int w, int h)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Prefer a sized internal format for clarity.
    glTexImage2D(GL_TEXTURE_2D, 0,
        GL_RGBA8, // internal format
        w, h, 0,
        GL_RGBA,  // incoming pixel format
        GL_UNSIGNED_BYTE,
        pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void destroyGLTexture(GLuint tex)
{
    glDeleteTextures(1, &tex);
}

// GL load-image wrapper
GLuint loadGLTextureRGBA8(
    const char* path,
    int* outW,
    int* outH,
    int svgTargetW,
    int svgTargetH,
    std::string* err)
{
    ImageRGBA8 img;
    if (!loadImageRGBA8(path, img, svgTargetW, svgTargetH, err))
        return 0;

    if (outW) *outW = img.w;
    if (outH) *outH = img.h;

    if (img.pixels.empty() || img.w <= 0 || img.h <= 0) {
        if (err) *err = "Decoded image is empty/invalid.";
        return 0;
    }

    return createGLTextureRGBA8(img.pixels.data(), img.w, img.h);
}

BL_END_NS;
