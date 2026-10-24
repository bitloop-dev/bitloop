#pragma once

#include <string>
#include <vector>

#include <bitloop/core/threads.h>
#include <bitloop/core/project.h>
#include <bitloop/core/capture_context.h>

#include <bitloop/imguix/imguix.h>
#include <bitloop/core/settings.h>
#include <bitloop/nanovgx/nano_canvas.h>
#include <bitloop/util/thread_queue.h>
#include <bitloop/util/timer.h>

BL_BEGIN_NS;

extern ImDebugLog project_log;
extern ImDebugLog debug_log;

struct ToolbarButtonState 
{
    ImVec4 bgColor;
    ImVec4 bgColorToggled;
    ImVec4 symbolColor;

    bool enabled = false;
    bool toggled = false;

    int  blink_timer = 0;
    bool blinking = false;
};

enum struct MainWindowCommandType
{
    ON_SELECT_PROJECT,
    ON_PLAY_PROJECT,
    ON_STOPPED_PROJECT,
    ON_PAUSED_PROJECT,

    BEGIN_SNAPSHOT_ACTIVE_PRESET
};

struct MainWindowCommandEvent
{
    MainWindowCommandType type;
    std::any payload;
    MainWindowCommandEvent(MainWindowCommandType t) : type(t) {}

    template<typename T>
    MainWindowCommandEvent(MainWindowCommandType t, T&& d) : type(t), payload(d) {}
};

class MainWindow
{
    static MainWindow* singleton;

    std::mutex command_mutex;
    std::vector<MainWindowCommandEvent> command_queue;

    // timers
    using time_point   = std::chrono::steady_clock::time_point;
    using steady_clock = std::chrono::steady_clock;

    FPSTimer             worker_fps_timer;
    time_point           last_frame_time = steady_clock::now();

    // fonts
    ImFont*              main_font = nullptr;
    ImFont*              mono_font = nullptr;

    // flags
    bool                 initialized = false;
    bool                 done_first_size = false;
    bool                 done_first_focus = false;
    bool                 update_docking_layout = false;
    bool                 vertical_layout = false;
    bool                 sidebar_visible = true;
    bool                 need_draw = false; // set true by worker after each process frame

    // viewport states
    bool                 is_editing_ui = false;
    ImVec2               client_size{};
    bool                 viewport_hovered = false;

    struct TimelineFrame {};
    struct TimelineLayer
    {
        std::string name;
        std::vector<TimelineFrame> frames;
    };

    std::vector<TimelineLayer> timelineLayers{ { "Layer 1", {} } };
    ImS64 timelineScrollFrame = 0;
    std::int64_t timelinePreviousFrame = -1;

    // toolbar
    ToolbarButtonState   play     = { ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.1f, 0.6f, 0.1f, 1.0f), ImVec4(0.4f, 1.0f, 0.4f, 1.0f), true };
    ToolbarButtonState   pause    = { ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f), false };
    ToolbarButtonState   stop     = { ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.6f, 0.1f, 0.1f, 1.0f), ImVec4(1.0f, 0.0f, 0.0f, 1.0f), false };
    ToolbarButtonState   record   = { ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.8f, 0.0f, 0.0f, 1.0f), ImVec4(1.0f, 0.2f, 0.0f, 1.0f), true };
    ToolbarButtonState   snapshot = { ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.8f, 0.0f, 0.0f, 1.0f), ImVec4(0.8f, 0.8f, 0.8f, 1.0f), true };
                         

    // settings/config
    CaptureContext&      capture_context;
    SettingsPanel        settings_panel;
    CaptureConfig        config;

    // render/capture pipeline components
    NanoCanvas           canvas;

    // thread communication
    SharedSync&          shared_sync;
    ThreadQueue          thread_queue;

    const int window_flags = 0;
        // ImGuiWindowFlags_NoTitleBar |
        // ImGuiWindowFlags_NoDecoration;// |
        //ImGuiWindowFlags_NoMove;

    // returns *which* preset to start using next frame
    const CapturePreset* determineRenderedPreset() const;

    // updates 'active_' preset values to start using next frame
    bool updateActivePreset();

    void handleCaptureCommand(CaptureCommandEvent e);

public:

    static constexpr MainWindow* instance() { 
        return singleton; 
    }

    MainWindow(SharedSync& _shared_sync, CaptureContext& _capture_context) :
        capture_context(_capture_context),
        settings_panel(this, capture_context.settings()),
        shared_sync(_shared_sync)
    {
        singleton = this;
    }
    ~MainWindow() { thread_queue.pump(); }

    [[nodiscard]] NanoCanvas*                  getCanvas() { return &canvas; }
    [[nodiscard]] const NanoCanvas*            getCanvas() const { return &canvas; }
    [[nodiscard]] CaptureContext&              captureContext() { return capture_context; }
    [[nodiscard]] const CaptureContext&        captureContext() const { return capture_context; }

    void queueBeginSnapshot(
        const SnapshotPresetList& presets,
        std::string_view rel_path_fmt = {},
        int request_id = 0
    );

    void queueBeginRecording();
    void queueEndRecording();

    void beginRecording(const CapturePreset& preset, const char* rel_path_fmt);

    #ifndef BL_WEB_BUILD
    std::filesystem::path getProjectSnapshotsDir();
    std::filesystem::path getProjectVideosDir();
    std::filesystem::path getProjectAnimationsDir();
    #endif
    
    // e.g. for a given preset 'backgrounds/seahorse_valley' may create missing dirs and return:
    //     'captures/Mandelbrot/backgrounds/seahorse_valley_1920x1080.webp'
    std::string prepareFullCapturePath(
        const CapturePreset& preset,
        std::filesystem::path base_dir,
        const char* rel_path_fmt,
        int file_idx,
        const char* fallback_extension);

private:
    // uses provided args and exact path (lowest level, no awareness of presets)
    void _beginSnapshot(const char* filepath, IVec2 res);
public:
    // begins snapshot using preset (uses default global ssaa/sharpen unless explicitly set in preset)
    void beginSnapshot(const CapturePreset& preset, const char* rel_path_fmt, int file_idx);

    // relative to project capture dir - if nullptr, name chosen automatically, e.g. For a given preset:
    void beginSnapshotList(const SnapshotPresetList& presets, const char* rel_path_fmt, int request_id);

    void endRecording();
    void checkCaptureComplete();

    bool isEditingUI();

    void init();
    void checkChangedDPR();

    void initStyles();
    void initFonts();

    ImFont* mainFont() { return main_font; }
    ImFont* monoFont() { return mono_font; }


    //ImRect viewportRect() { return viewport_rect; }
    bool viewportHovered() const { return viewport_hovered; }
    IVec2 viewportSize() const { return IVec2((int)client_size.x, (int)client_size.y); }

    void setSidebarVisible(bool b) { sidebar_visible = b; }
    bool getSidebarVisible() { return sidebar_visible; }

    /// Thread-safe message queue
    void queueMainWindowCommand(MainWindowCommandEvent e) {
        std::lock_guard<std::mutex> lock(command_mutex);
        command_queue.push_back(MainWindowCommandEvent(e));
    }

    template<typename T>
    void queueMainWindowCommand(MainWindowCommandEvent e, T&& data) {
        std::lock_guard<std::mutex> lock(command_mutex);
        command_queue.push_back(MainWindowCommandEvent(e, std::make_any(data)));
    }

    private: void handleCommand(MainWindowCommandEvent e);

public:

    /// Toolbar
    void drawToolbarButton(ImDrawList* drawList, ImVec2 pos, ImVec2 size, const char* symbol, ImU32 color);
    bool toolbarButton(const char* id, const char* symbol, const ToolbarButtonState& state, ImVec2 size, float inactive_alpha = 0.25f);
    void populateToolbar();

    /// Project Tree
    void populateProjectTreeNodeRecursive(ProjectInfoNode& node, int& i, int depth);
    void populateProjectTree(bool expand_vertical);
    void populateProjectUI();
    void populateOverlay();
    
    /// Main Window populate
    bool manageDockingLayout();
    bool focusWindow(const char* id);

    void populateCollapsedLayout();
    void populateTimeline(std::int64_t currentFrame, bool playing);
    void populateViewport(bool& projectDrawn);
    
    void populateExpandedLayout();
    void populateUI();

    ThreadQueue& threadQueue() { return thread_queue; }
    ///template <typename F> void invokeBlocking(F&& fn) {
    ///    thread_queue.invokeBlocking(std::forward<F>(fn));
    ///}

};

[[nodiscard]] constexpr MainWindow* main_window()
{
    return MainWindow::instance();
}

BL_END_NS;
