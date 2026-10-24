#include <catch2/catch_test_macros.hpp>
#include <bitloop.h>

#ifndef __EMSCRIPTEN__
namespace
{
    struct ImageContext
    {
        bool ownsVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
        SDL_Window* window = nullptr;
        SDL_GLContext context = nullptr;
        NVGcontext* canvas = nullptr;
        std::string error;

        ImageContext()
        {
            if (ownsVideo && !SDL_InitSubSystem(SDL_INIT_VIDEO)) { error = SDL_GetError(); return; }
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
            window = SDL_CreateWindow("Image mipmap regression", 32, 32, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!window) { error = SDL_GetError(); return; }
            context = SDL_GL_CreateContext(window);
            if (!context) { error = SDL_GetError(); return; }
            if (!SDL_GL_MakeCurrent(window, context)) { error = SDL_GetError(); return; }
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) { error = "GLAD initialization failed"; return; }
            canvas = nvgCreate(0);
            if (!canvas) error = "NanoVG initialization failed";
        }

        ~ImageContext()
        {
            if (canvas) nvgDelete(canvas);
            if (context) SDL_GL_DestroyContext(context);
            if (window) SDL_DestroyWindow(window);
            if (ownsVideo) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };

    struct TestImage : bl::Image { using Image::refreshData; };

    void requireMipColors(NVGcontext* canvas, const TestImage& image, int width, int height, bl::Color expected)
    {
        glBindTexture(GL_TEXTURE_2D, nvglImageHandle(canvas, image.imageId()));
        for (int level = 0;; ++level)
        {
            CAPTURE(level, width, height);
            GLint actualWidth = 0, actualHeight = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &actualWidth);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &actualHeight);
            REQUIRE(actualWidth == width);
            REQUIRE(actualHeight == height);
            std::vector<uint32_t> pixels(size_t(width) * height);
            glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            REQUIRE(std::all_of(pixels.begin(), pixels.end(), [expected](uint32_t color) { return color == expected.rgba; }));
            if (width == 1 && height == 1) break;
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        REQUIRE(glGetError() == GL_NO_ERROR);
    }
}

TEST_CASE("Image updates refresh every mipmap level", "[image][graphics]")
{
    ImageContext graphics;
    if (!graphics.context) SKIP("OpenGL context unavailable: " << graphics.error);
    INFO(graphics.error);
    REQUIRE(graphics.canvas != nullptr);

    TestImage image;
    image.create(32, 16);
    const bl::Color pending(66, 71, 82), finished(31, 184, 169), changed(230, 80, 30);
    image.clear(pending);
    image.refreshData(graphics.canvas);
    REQUIRE(image.imageId() != 0);
    requireMipColors(graphics.canvas, image, 32, 16, pending);

    const int imageId = image.imageId();
    for (auto color : {finished, changed})
    {
        image.clear(color);
        image.refreshData(graphics.canvas);
        REQUIRE(image.imageId() == imageId);
        GLint binding = -1;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        REQUIRE(binding == 0);
        requireMipColors(graphics.canvas, image, 32, 16, color);
    }

    image.create(16, 16);
    image.clear(finished);
    image.refreshData(graphics.canvas);
    requireMipColors(graphics.canvas, image, 16, 16, finished);
}
#endif
