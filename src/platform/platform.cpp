#include <bitloop/core/config.h>
#include <bitloop/core/project.h>
#include <bitloop/imguix/imguix.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif


BL_BEGIN_NS

PlatformManager* PlatformManager::singleton = nullptr;



GLCaps PlatformManager::query_gl_caps()
{
    GLCaps caps{};

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.max_texture_size);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &caps.max_renderbuffer_size);

    GLint vp[2] = {};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, vp);
    caps.max_viewport_w = vp[0];
    caps.max_viewport_h = vp[1];

    caps.max_samples = 0;
    if (glGetError() == GL_NO_ERROR) {
        glGetIntegerv(GL_MAX_SAMPLES, &caps.max_samples);
        glGetError(); // avoid stale error affecting later checks
    }

    return caps;
}

bool PlatformManager::glValidTextureSize(int internal_w, int internal_h)
{
    const int max_w = std::min(gl_caps.max_texture_size, gl_caps.max_renderbuffer_size);
    const int max_h = std::min(gl_caps.max_texture_size, gl_caps.max_renderbuffer_size);

    if (internal_w <= 0 || internal_h <= 0) return false;
    if (internal_w > max_w || internal_h > max_h) return false;
    if (internal_w > gl_caps.max_viewport_w || internal_h > gl_caps.max_viewport_h) return false;
    return true;
}

#ifdef __EMSCRIPTEN__
EM_JS(int, _is_mobile_device, (), {
  // 1. Prefer UA-Client-Hints when they exist (Chromium >= 89)
  if (navigator.userAgentData && navigator.userAgentData.mobile !== undefined) {
    return navigator.userAgentData.mobile ? 1 : 0;
  }

  // 2. Pointer/hover media-queries – quick and cheap
  if (window.matchMedia('(pointer: coarse)').matches &&
      !window.matchMedia('(hover: hover)').matches) {
    return 1;
  }

  // 3. Fallback to a light regex on the UA string
  return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini|Windows Phone/i
           .test(navigator.userAgent) ? 1 : 0;
});
#else
int _is_mobile_device()
{
    return 0;
}
#endif

void PlatformManager::init()
{
    is_mobile_device = _is_mobile_device();
    gl_caps = query_gl_caps();

    #ifdef __EMSCRIPTEN__
    EM_ASM({
        window.addEventListener('keydown', function(event)
        {
            if (event.ctrlKey && event.key == 'v')
                event.stopImmediatePropagation();
        }, true);
    });
    #endif
}

void PlatformManager::update()
{
    SDL_GetWindowSizeInPixels(window, &gl_w, &gl_h);
    SDL_GetWindowSize(window, &win_w, &win_h);

    #ifdef BL_SIMULATED_DEVICE
    win_dpr = BL_SIMULATED_DEVICE.dpr;
    #else
    win_dpr = SDL_GetWindowDisplayScale(window);
    #endif
}

void PlatformManager::resized()
{
    #ifdef __EMSCRIPTEN__
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    float device_dpr = emscripten_get_device_pixel_ratio();
    fb_w = css_w * device_dpr;
    fb_h = css_h * device_dpr;
    SDL_SetWindowSize(window, fb_w, fb_h);
    emscripten_set_canvas_element_size("#canvas", fb_w, fb_h);
    #else
    SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);
    #endif

    platform()->update();
}

static void destroy_offscreen_fbo(GLuint& fbo, GLuint& color, GLuint& depth)
{
    if (depth) { glDeleteRenderbuffers(1, &depth); depth = 0; }
    if (color) { glDeleteTextures(1, &color);      color = 0; }
    if (fbo)   { glDeleteFramebuffers(1, &fbo);    fbo = 0; }
}

static void ensure_offscreen_fbo(GLuint& fbo, GLuint& color, GLuint& depth, int w, int h)
{
    destroy_offscreen_fbo(fbo, color, depth);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PlatformManager::glBeginFrame()
{
    #ifdef BL_SIMULATED_DEVICE
    offscreen_w = BL_SIMULATED_DEVICE.w;
    offscreen_h = BL_SIMULATED_DEVICE.h;

    // ensure offscreen FBO exists
    if (!offscreen_fbo)
        ensure_offscreen_fbo(offscreen_fbo, offscreen_color, offscreen_depth, offscreen_w, offscreen_h);

    glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fbo);
    glViewport(0, 0, offscreen_w, offscreen_h);
    #else
    //glBindFramebuffer(GL_FRAMEBUFFER, 0);
    //glViewport(0, 0, gl_w, gl_h);
    #endif
}

void PlatformManager::glEndFrame()
{
    #ifdef BL_SIMULATED_DEVICE
    // downscale into the real window backbuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreen_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(
        0, 0, offscreen_w, offscreen_h,
        0, 0, gl_w, gl_h,
        GL_COLOR_BUFFER_BIT, GL_LINEAR
    );
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    #endif
}

void PlatformManager::imguiFixOffscreenMousePosition()
{
    #ifdef BL_SIMULATED_DEVICE
    // imgui_impl_sdl3 calls UpdateMouseData() in NewFrame and queries global mouse state,
    // which can overwrite scaled coordinates
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(mx * inputScaleX(), my * inputScaleY());
    #endif
}

void PlatformManager::upscaleMouseEventToOffscreen(SDL_Event& e)
{
    // upscale mouse position from screen space to offscreen
    const float sx = platform()->inputScaleX();
    const float sy = platform()->inputScaleY();

    switch (e.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        e.motion.x *= sx;
        e.motion.y *= sy;
        e.motion.xrel *= sx;
        e.motion.yrel *= sy;
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        e.button.x *= sx;
        e.button.y *= sy;
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        e.wheel.mouse_x *= sx;
        e.wheel.mouse_y *= sy;
        break;

    default:
        break;
    }
}

void PlatformManager::convertMouseToTouch(SDL_Event& e)
{
    // emulate touch (from mouse)
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        SDL_Event me = e;
        e.type = SDL_EVENT_FINGER_DOWN;
        e.tfinger.fingerID = 0;
        e.tfinger.x = (me.button.x / (float)platform()->fboWidth());
        e.tfinger.y = (me.button.y / (float)platform()->fboHeight());
    }
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        SDL_Event me = e;
        e.type = SDL_EVENT_FINGER_UP;
        e.tfinger.fingerID = 0;
        e.tfinger.x = (me.button.x / (float)platform()->fboWidth());
        e.tfinger.y = (me.button.y / (float)platform()->fboHeight());
    }
    else if (e.type == SDL_EVENT_MOUSE_MOTION)
    {
        SDL_Event me = e;
        e.type = SDL_EVENT_FINGER_MOTION;
        e.tfinger.fingerID = 0;
        e.tfinger.x = (me.button.x / (float)platform()->fboWidth());
        e.tfinger.y = (me.button.y / (float)platform()->fboHeight());
    }
}

bool PlatformManager::deviceVertical()
{
    #ifdef __EMSCRIPTEN__
    EmscriptenOrientationChangeEvent orientation;
    if (emscripten_get_orientation_status(&orientation) == EMSCRIPTEN_RESULT_SUCCESS)
        return std::abs(orientation.orientationAngle % 180) < 90;
    #endif

    // todo: Add do DebugDevicePreset
    #ifdef BL_SIMULATE_MOBILE
    return true;
    #else
    return false;
    #endif
}

void PlatformManager::deviceOrientation(int* orientation_angle, int* orientation_index)
{
    #ifdef __EMSCRIPTEN__
    EmscriptenOrientationChangeEvent orientation;
    if (emscripten_get_orientation_status(&orientation) == EMSCRIPTEN_RESULT_SUCCESS)
    {
        if (orientation_angle) *orientation_angle = orientation.orientationAngle;
        if (orientation_index) *orientation_index = orientation.orientationIndex;
    }
    #else
    if (orientation_angle) *orientation_angle = 0;
    if (orientation_index) *orientation_index = 0;
    #endif
}

#ifdef __EMSCRIPTEN__
std::function<void(int, int)> _onOrientationChangedCB;
EM_BOOL _orientationChanged(int type, const EmscriptenOrientationChangeEvent* e, void* data)
{
    if (_onOrientationChangedCB)
        _onOrientationChangedCB(e->orientationIndex, e->orientationAngle);
    return EM_TRUE;
}
#endif

bool PlatformManager::deviceOrientationChanged(std::function<void(int, int)> onChanged [[maybe_unused]])
{
    #ifdef __EMSCRIPTEN__
    _onOrientationChangedCB = onChanged;
    EMSCRIPTEN_RESULT res = emscripten_set_orientationchange_callback(NULL, EM_FALSE, _orientationChanged);
    return (res == EMSCRIPTEN_RESULT_SUCCESS);
    #else
    return false;
    #endif
}

bool PlatformManager::isMobile() const
{
    #ifdef BL_SIMULATE_MOBILE
    return true;
    #else
    return is_mobile_device;
    #endif
}

bool PlatformManager::isDesktopNative() const
{
    #if defined BL_WEB_BUILD
    return false;
    #else
    return !isMobile();
    #endif
}

bool PlatformManager::isDesktopBrowser() const
{
    #ifdef BL_SIMULATE_BROWSER
        // simulating browser
        #ifdef BL_SIMULATE_MOBILE
        return false;
        #else
        return true;
        #endif
    #else
        // detect real environment
        #ifdef __EMSCRIPTEN__
        return !isMobile(); // Assumed desktop browser if mobile screen not detected
        #else
        return false; // Native desktop application
        #endif
    #endif
}

float PlatformManager::fontScale() const
{
    return isMobile() ? 1.3f : 1.0f;
}


float PlatformManager::thumbScale(float extra_mobile_mult) const
{
    return isMobile() ? (2.0f * extra_mobile_mult) : 1.0f;
}

float PlatformManager::lineHeight() const
{
    return ImGui::GetFontSize();
}

float PlatformManager::inputHeight() const
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& s = ImGui::GetStyle();
    return io.DisplaySize.y / ImGui::GetFontSize() + s.FramePadding.y * 2.0f;
}

float PlatformManager::maxCharRows() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.DisplaySize.y / ImGui::GetFontSize();
}

float PlatformManager::maxCharCols() const
{
    ImGuiIO& io = ImGui::GetIO();
    return io.DisplaySize.x / ImGui::GetFontSize();
}

std::filesystem::path PlatformManager::executableDir() const
{
    #if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    if (len == 0)
        throw std::runtime_error("GetModuleFileNameW failed");
    buf.resize(len);
    return std::filesystem::path(buf).parent_path();

    #elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        throw std::runtime_error("_NSGetExecutablePath failed");
    return std::filesystem::canonical(buf).parent_path();

    #elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len == -1)
        throw std::runtime_error("readlink failed");
    buf[len] = '\0';
    return std::filesystem::canonical(buf).parent_path();

    #else // fallback
    return std::filesystem::current_path();
    #endif
}

bool PlatformManager::openDirectory(const std::filesystem::path& directory) const
{
    #ifdef BL_WEB_BUILD
    (void)directory;
    return SDL_SetError("Opening folders requires a native application");
    #else
    if (directory.empty()) return SDL_SetError("No folder was specified");

    std::error_code error;
    const auto absolutePath = std::filesystem::absolute(directory, error).lexically_normal();
    if (error) return SDL_SetError("Cannot resolve folder: %s", error.message().c_str());
    const bool isDirectory = std::filesystem::is_directory(absolutePath, error);
    if (error) return SDL_SetError("Cannot access folder: %s", error.message().c_str());
    if (!isDirectory) return SDL_SetError("The path does not exist or is not a directory");

    #ifdef _WIN32
    // SDL passes this directly to ShellExecuteW; a native path preserves Unicode and literal URL characters.
    const auto utf8Path = absolutePath.u8string();
    return SDL_OpenURL(reinterpret_cast<const char*>(utf8Path.c_str()));
    #else
    const auto utf8Path = absolutePath.generic_u8string();
    std::string url = "file://";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : utf8Path)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') url += char(c);
        else
        {
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 15];
        }
    }
    return SDL_OpenURL(url.c_str());
    #endif
    #endif
}

std::filesystem::path PlatformManager::resourceDir() const
{
    #ifdef __EMSCRIPTEN__
    return "/";
    #else
    return executableDir();
    #endif
}

std::string PlatformManager::path(std::string_view virtual_path) const
{
    std::filesystem::path p = resourceDir();

    if (!virtual_path.empty() && virtual_path.front() == '/')
        virtual_path.remove_prefix(1); // trim leading '/'

    p /= virtual_path; // join
    p = p.lexically_normal(); // clean up
    return p.string();
}

#ifdef __EMSCRIPTEN__
char* PlatformManager::_url_get_base() {
    return (char*)MAIN_THREAD_EM_ASM_PTR({
      const u = new URL(location.href);
      let path = u.pathname;
      if (path.endsWith('index.html')) path = path.slice(0, -'index.html'.length);
      if (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
      const s = u.origin + path;
      const n = lengthBytesUTF8(s) + 1;
      const p = _malloc(n);
      stringToUTF8(s, p, n);
      return p;
    });
}

std::string PlatformManager::url_get_base()
{
    char* str = _url_get_base();
    std::string ret = str;
    free(str);
    return ret;
}

int PlatformManager::url_has(const char* k)
{
    return MAIN_THREAD_EM_ASM_INT({
      const key = UTF8ToString($0);
      const u = new URL(location.href);
      if (u.searchParams.has(key)) return 1;
      if (u.hash && u.hash.length > 1) {
        const h = new URLSearchParams(u.hash.substring(1));
        if (h.has(key)) return 1;
      }
      return 0;
    }, k);
}

// Gets a number param; returns fallback if missing/NaN
double PlatformManager::url_get_number(const char* k, double fallback)
{
    return MAIN_THREAD_EM_ASM_DOUBLE({
      const key = UTF8ToString($0);
      const fb = $1;
      const u = new URL(location.href);
      let v = u.searchParams.get(key);
      if (v === null && u.hash && u.hash.length > 1) {
        v = new URLSearchParams(u.hash.substring(1)).get(key);
      }
      const n = Number(v);
      return Number.isFinite(n) ? n : fb;
    }, k, fallback);
}

// Gets a string param; returns newly-allocated C string (caller free()), or 0 if missing
char* PlatformManager::_url_get_string(const char* k)
{
    return (char*)MAIN_THREAD_EM_ASM_PTR({
      const key = UTF8ToString($0);
      const u = new URL(location.href);
      let v = u.searchParams.get(key);
      if (v === null && u.hash && u.hash.length > 1) {
        v = new URLSearchParams(u.hash.substring(1)).get(key);
      }
      if (v === null) return 0;
      const len = lengthBytesUTF8(v) + 1;
      const p = _malloc(len);
      stringToUTF8(v, p, len);
      return p;
    }, k);
}


std::string PlatformManager::url_get_string(const char* k)
{
    char* str = _url_get_string(k);
    std::string ret = str;
    free(str);
    return ret;
}

// Set a string param in ?query (use_hash=0) or #hash (use_hash=1).
// replace=1 uses history.replaceState (good for live updates).
void PlatformManager::url_set_string(const char* key, const char* value, int use_hash, int replace)
{
  MAIN_THREAD_EM_ASM({
    const key = UTF8ToString($0);
    const val = UTF8ToString($1);
    const useHash = $2|0;
    const doReplace = $3|0;

    const u = new URL(location.href);
    const base = u.origin + u.pathname;

    const qs = u.searchParams;                     // ?query
    const hs = new URLSearchParams(u.hash ? u.hash.slice(1) : ""); // #hash

    if (useHash) hs.set(key, val); else qs.set(key, val);

    const q = qs.toString();
    const h = hs.toString();
    const newUrl = base + (q ? "?" + q : "") + (h ? "#" + h : "");
    if (doReplace) history.replaceState(null, "", newUrl);
    else           history.pushState(null, "", newUrl);
  }, key, value, use_hash, replace);
}

void PlatformManager::url_set_number(const char* key, double value, int use_hash, int replace) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", value);
    url_set_string(key, buf, use_hash, replace);
}

// Remove a param
void PlatformManager::url_unset(const char* key, int use_hash, int replace)
{
    MAIN_THREAD_EM_ASM({
      const key = UTF8ToString($0);
      const useHash = $1 | 0;
      const doReplace = $2 | 0;

      const u = new URL(location.href);
      const base = u.origin + u.pathname;

      const qs = u.searchParams;
      const hs = new URLSearchParams(u.hash ? u.hash.slice(1) : "");

      if (useHash) hs.delete(key); else qs.delete(key);

      const q = qs.toString();
      const h = hs.toString();
      const newUrl = base + (q ? "?" + q : "") + (h ? "#" + h : "");
      if (doReplace) history.replaceState(null, "", newUrl);
      else           history.pushState(null, "", newUrl);
    }, key, use_hash, replace);
}


// Downloading files to browser
EM_JS(void, js_download_bytes, (const uint8_t* ptr, size_t size, const char* filename, const char* mime),
{
    const name = UTF8ToString(filename);
    const type = UTF8ToString(mime);

    // Copy bytes out of WASM memory
    const bytes = HEAPU8.slice(ptr, ptr + size);
    const blob = new Blob([bytes], { type });
    const url = URL.createObjectURL(blob);

    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
});

void PlatformManager::download_blob(const void* data, size_t size, const char* filename, const char* mime)
{
    js_download_bytes(static_cast<const uint8_t*>(data), size, filename, mime);
}

void PlatformManager::download_blob(const bytebuf& buf, const char* filename, const char* mime)
{
    js_download_bytes(buf.data(), buf.size(), filename, mime);
}

void PlatformManager::download_snapshot_webp(const bytebuf& buf, const char* filename)
{
    download_blob(buf.data(), buf.size(), filename, "image/webp");
}

#endif


BL_END_NS
