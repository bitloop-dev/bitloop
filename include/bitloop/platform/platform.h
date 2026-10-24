#pragma once

/// SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_touch.h>

/// emscripten
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

/// Windows
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/// OpenGL
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include "glad/glad.h"
#endif

#include <filesystem>
#include <functional>

#include <bitloop/core/debug.h>
#include <bitloop/core/types.h>
#include "platform_macros.h"

BL_BEGIN_NS

struct GLCaps
{
    int max_texture_size = 0;
    int max_renderbuffer_size = 0;
    int max_samples = 0;
    int max_viewport_w = 0;
    int max_viewport_h = 0;
};

class PlatformManager
{
    static PlatformManager *singleton;

    SDL_Window* window = nullptr;

    double css_w = 0, css_h = 0;
    int gl_w=0, gl_h = 0;
    int win_w = 0, win_h = 0;
    int fb_w = 0, fb_h = 0;
    float win_dpr = 1.0f;

    bool is_mobile_device = false;

    GLuint offscreen_fbo = 0;
    GLuint offscreen_color = 0;
    GLuint offscreen_depth = 0;
    int    offscreen_w = 0;
    int    offscreen_h = 0;

    GLCaps gl_caps;
    GLCaps query_gl_caps();

public:

    static constexpr PlatformManager* instance()
    {
        return singleton;
    }

    PlatformManager(SDL_Window* _window)
    {
        singleton = this;
        window = _window;
    }

    ~PlatformManager()
    {
        if (singleton == this)
            singleton = nullptr;
    }

    // Required function calls
    void init();
    void update();
    void resized();

    [[nodiscard]] SDL_Window* sdlWindow() const { return window; }

    [[nodiscard]] int glWidth()      const { return gl_w; }
    [[nodiscard]] int glHeight()     const { return gl_h; }
    [[nodiscard]] int fboWidth()     const { return fboSize().x; }
    [[nodiscard]] int fboHeight()    const { return fboSize().y; }
    [[nodiscard]] int windowWidth()  const { return win_w; }
    [[nodiscard]] int windowHeight() const { return win_h; }

    [[nodiscard]] IVec2 glSize()     const { return {gl_w, gl_h}; }
    [[nodiscard]] IVec2 windowSize() const { return {win_w, win_h}; }

    // Device Info

    [[nodiscard]] float dpr() const { return win_dpr; }

    [[nodiscard]] bool offscreenActive() const
    {
        #ifdef BL_SIMULATED_DEVICE
        return true;
        #else
        return false;
        #endif
    }

    // what the app should render to (offscreen, or direct)
    [[nodiscard]] IVec2 fboSize() const
    {
        #ifdef BL_SIMULATED_DEVICE
        return {offscreen_w, offscreen_h};
        #else
        return {fb_w, fb_h};
        #endif
    }

    GLCaps glCaps() const { return gl_caps; }
    bool glValidTextureSize(int internal_w, int internal_h);

    // input scaling to fbo-space
    [[nodiscard]] float inputScaleX() const { return (gl_w > 0) ? (float)fboSize().x / (float)gl_w : 1.0f; }
    [[nodiscard]] float inputScaleY() const { return (gl_h > 0) ? (float)fboSize().y / (float)gl_h : 1.0f; }

    void imguiFixOffscreenMousePosition();
    void upscaleMouseEventToOffscreen(SDL_Event& e);
    void convertMouseToTouch(SDL_Event& e);

    // begin gl (for offscreen, or direct)
    void glBeginFrame();
    void glEndFrame();

    [[nodiscard]] bool deviceVertical();
    void deviceOrientation(int* orientation_angle, int* orientation_index = nullptr);
    bool deviceOrientationChanged(std::function<void(int, int)> onChanged);
    
    // Platform detection
    [[nodiscard]] bool isMobile() const;
    [[nodiscard]] bool isDesktopNative() const;
    [[nodiscard]] bool isDesktopBrowser() const;

    // Scale
    [[nodiscard]] float fontScale() const;
    [[nodiscard]] float thumbScale(float extra_mobile_mult=1.0f) const;

    // Layout helpers
    [[nodiscard]] float lineHeight() const;
    [[nodiscard]] float inputHeight() const;
    [[nodiscard]] float maxCharRows() const;
    [[nodiscard]] float maxCharCols() const;

    // File system helpers
    [[nodiscard]] std::filesystem::path executableDir() const;
    [[nodiscard]] std::filesystem::path resourceDir() const;
    [[nodiscard]] std::string path(std::string_view virtual_path) const;

    // Opens an existing directory in the native file manager; failures set SDL_GetError().
    [[nodiscard]] bool openDirectory(const std::filesystem::path& directory) const;


    // URL helpers
    #ifdef __EMSCRIPTEN__
private:
    char*        _url_get_base();
    char*        _url_get_string(const char* k);
                 
public:          
    int          url_has(const char* k);
                 
    std::string  url_get_base();
    std::string  url_get_string(const char* k);
    double       url_get_number(const char* k, double fallback);
                 
    void         url_set_string(const char* key, const char* value, int use_hash = 0, int replace = 1);
    void         url_set_number(const char* key, double value, int use_hash = 0, int replace = 1);
    void         url_unset(const char* key, int use_hash = 0, int replace = 1);

    void         download_blob(const void* data, size_t size, const char* filename, const char* mime);
    void         download_blob(const bytebuf& buf, const char* filename, const char* mime);
    void         download_snapshot_webp(const bytebuf& buf, const char* filename);
    #endif
};

[[nodiscard]] constexpr PlatformManager* platform()
{
    return PlatformManager::instance();
}

[[nodiscard]] inline float scale_size(float length)
{
    return PlatformManager::instance()->dpr() * length;
}

[[nodiscard]] inline double scale_size(double length)
{
    return static_cast<double>(PlatformManager::instance()->dpr()) * length;
}

[[nodiscard]] inline int scale_size(int length)
{
    return static_cast<int>(PlatformManager::instance()->dpr() * static_cast<float>(length));
}

BL_END_NS
