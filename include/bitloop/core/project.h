#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include <bitloop/util/json.h>
#include <bitloop/util/timer.h>
#include <bitloop/util/thread_queue.h>
#include <bitloop/core/interface_model.h>

#ifdef BITLOOP_DEV_MODE
    #include <filesystem>
    #include <bitloop/core/sim_checkpoint.h>
    #include <bitloop/core/sim_snapshot.h>
    #include <bitloop/core/sim_trace.h>
#endif

#include "input.h"
#include "layout.h"

#include "types.h"
#include "event.h"
#include "var_buffer.h"

#ifdef SIM_BEG
#undef SIM_BEG
#endif
#ifdef SIM_END
#undef SIM_END
#endif
#define SIM_BEG  namespace SIM_NAME {
#define SIM_END  }
                             
BL_BEGIN_NS

class Viewport;
class ProjectBase;
class ProjectManager;
class NanoCanvas;
class SceneBase;

class CaptureContext;
class CapturePreset;
class SnapshotPresetManager;
struct EncodeFrame;
struct SnapshotFrame;
struct SettingsConfig;

struct ImDebugLog;

// Switch to "Input"
// Input.mouse
// Input.touch
// Input.touch.fingers
// Input.pointer (composite of mouse on finger 0)
// Or just use single:
// > Input.pointers[0].x
// > Input.pointers[0].y
// > Input.pointers[0].pressed
// Then use in viewport::process directly (always for consistency/predictability?)



/// ======= Input ==========
/*
struct SceneFingerInfo : FingerInfo
{
    Viewport* viewport = nullptr;
    double client_x = 0;
    double client_y = 0;
    double stage_x = 0;
    double stage_y = 0;
    double world_x = 0;
    double world_y = 0;
    int scroll_delta = 0;
    bool pressed = false;

    SceneFingerInfo(const FingerInfo& f) : FingerInfo(f)
    {}
};

struct TouchState
{
    //PointerEvent event; // The actual event which updated this state
    std::vector<SceneFingerInfo> fingers; // "Current" state of all fingers during this state

    //TouchState(const Event& e, const std::vector<FingerInfo>& fingers);
    //TouchState(const std::vector<FingerInfo>& fingers);

    const SceneFingerInfo* finger(int i=0) const
    {
        if (i < (int)fingers.size())
            return &fingers[i];
        return nullptr;
    }
};

struct ScrollState
{
    PointerEvent event; // The event which updated this state

    int scroll_delta = 0;
};

struct KeyboardState
{
    KeyEvent event; // The event which updated this state
};

struct TouchInput : TouchState
{
    std::vector<TouchState> updates;
    //TouchState present;

    //TouchState

    //const TouchState& now()const  {
    //    if (updates.size())
    //        return updates.back();
    //
    //    static SDL_Event dummy_sdl;
    //    static Event dummy(dummy_sdl);
    //    static std::vector<FingerInfo> dummy_fingers;
    //    static TouchState dummy_state(dummy, dummy_fingers);
    //    return dummy_state;
    //}

    void push(const TouchState& s) {
        updates.push_back(s);
        static_cast<TouchState&>(*this) = s;
    }

    void clear() {
        updates.clear();
    }
};

struct Input
{
    TouchInput touch;
    //InputStateQueue<KeyboardState>  keyboard;

    void clear()
    {
        touch.clear();
        //keyboard.clear();
    }

    void addEvent(const Event& e, const std::vector<FingerInfo>& fingers)
    {
        if (e.isPointerEvent())
        {
            UNUSED(fingers);
            //TouchState s(fingers);
            //touch.push(s);
        }
    }
};
*/
/// =================

class ProjectBase;
using ProjectCreatorFunc = std::function<ProjectBase*()>;

struct ProjectInfo
{
    std::string name;
    std::vector<std::string> path;
    const char* dev_root; // nullptr if not BITLOOP_DEV_MODE

    ProjectCreatorFunc creator;
    int sim_uid;

    ProjectInfo(
        std::vector<std::string> path,
        std::string name="",
        ProjectCreatorFunc creator = nullptr,
        int sim_uid = -100
    )
        : name(name), path(path), creator(creator), sim_uid(sim_uid)
    {}
};

struct ProjectInfoNode
{
    std::shared_ptr<ProjectInfo> project_info = nullptr;
    std::vector<ProjectInfoNode> children;
    std::string name;
    int uid = -1;

    ProjectInfoNode(std::string node_name) : name(node_name) {}
    ProjectInfoNode(std::shared_ptr<ProjectInfo> project)
    {
        project_info = project;
        name = project->path.back();
    }

    int count_nodes() const
    {
        int count = 1u;
        for (const auto& child : children)
            count += child.count_nodes();
        return count;
    }
};

#ifdef BITLOOP_DEV_MODE
struct SceneCatalogueEntry
{
    std::string id;
    std::size_t ordinal = 0;
    bool selectable = false;
    bool selected_by_default = false;
};
#endif

class ProjectBase
{
    struct SceneRecord;

    Layout viewports;
    std::vector<std::unique_ptr<SceneRecord>> scene_records;
    std::vector<SceneBase*> active_scenes;
    std::shared_ptr<SceneHandleState> scene_handle_state = std::make_shared<SceneHandleState>();
    std::vector<std::uint64_t> default_scene_tokens;
    std::uint64_t next_scene_token = 1;
    std::size_t next_scene_ordinal = 0;
    std::uint64_t scene_generation = 0;
    bool declaring_scenes = false;
    bool defaults_specified = false;
    std::mutex structural_mutation_mutex;
    std::vector<std::function<void()>> structural_mutations;
    bool iterating_lifecycle = false;
    bool tearing_down = false;

#ifdef BITLOOP_DEV_MODE
    std::optional<std::vector<std::string>> headless_scene_selection;
    std::optional<JSON::json> headless_scene_config;
    std::string headless_scene_config_hash;
#endif

    int sim_uid = -1;

    SimpleTimer project_timer;
    SimpleTimer scene_timer;
    SimpleTimer frame_timer;

    double      project_dt = 0;
    double      frame_dt = 0;
    std::atomic<std::int64_t> completedFrameCount{ 0 };

    // Keep track of focused/hovered project viewports
    Viewport* ctx_focused = nullptr;
    Viewport* ctx_hovered = nullptr;


    friend class ProjectWorker;
    friend class Layout;
    friend class SceneBase;
    friend class SceneHandleBase;
    friend class Viewport;

    NanoCanvas* canvas = nullptr;
    ImDebugLog* project_log = nullptr;
    CaptureContext* capture_context = nullptr;

    // ----- states -----
    bool started = false;
    bool paused = false;
    bool done_single_process = false;
    DVec2 old_surface_size{};
#ifdef BITLOOP_DEV_MODE
    bool headless_mode = false;
    ThreadQueue headless_thread_queue;
    IVec2 headless_surface_size{ 800, 600 };
    std::int64_t trace_context_frame = 0;
    double trace_context_time = 0.0;
    double trace_context_dt = 0.0;
    std::vector<SimSnapshotRecord> pending_trace_snapshots;
    std::vector<EventData> headless_input_event_queue;
#endif
    bool show_fullscreen_btn = true;
    bool immediate_update_requested = false;

    void configure(int sim_uid, NanoCanvas* canvas, ImDebugLog* project_log, CaptureContext* capture_context);

    SceneHandleBase declareScene(
        std::string id,
        std::shared_ptr<void> config,
        std::function<std::unique_ptr<SceneBase>()> factory);
    [[nodiscard]] SceneRecord* findSceneRecord(std::uint64_t token, std::uint64_t generation) noexcept;
    [[nodiscard]] const SceneRecord* findSceneRecord(std::uint64_t token, std::uint64_t generation) const noexcept;
    [[nodiscard]] bool ownsSceneHandle(const SceneHandleBase& handle) const noexcept;
    [[nodiscard]] SceneBase* resolveSceneHandle(const SceneHandleBase& handle) const noexcept;
    [[nodiscard]] std::string sceneHandleId(const SceneHandleBase& handle) const;
	
    void registerSceneLaunchConfigParser(
        const SceneHandleBase& handle,
        std::function<void(const JSON::json&)> parser);
    void declareSceneCatalogue();
    void applySceneSelection();

#ifdef BITLOOP_DEV_MODE
    void applyHeadlessSceneConfig();
#endif

    void materializeSelectedScenes();
    void materializeRuntimeScene(std::uint64_t token);
    void destroyScene(std::uint64_t token);
    void initializeScene(SceneRecord& record);
    void destroySceneUI(SceneBase& scene);
    void enqueueStructuralMutation(std::function<void()> mutation);
    void commitStructuralMutations(bool gui_access_excluded = false);
    void discardStructuralMutations();
    [[nodiscard]] bool shouldDeferStructuralMutation() const noexcept;
    void releaseViewportReferences(Viewport* viewport);
    void clearSceneRecords();

#ifdef BITLOOP_DEV_MODE
    [[nodiscard]] Viewport* resolveSnapshotViewport(
        std::string_view scene_id,
        std::string_view viewport_id,
        std::string& error) const;
#endif

    bool updateViewportRects();

    struct InputDispatchContext
    {
        bool headless = false;
        bool viewport_hovered = true;
        IVec2 surface_size{ 0, 0 };
        bool allow_fullscreen_hotkeys = true;
    };

    void _dispatchInputEvent(const EventData& e, const InputDispatchContext& context);
    [[nodiscard]] Viewport* hitTestViewport(DVec2 client_position) const;

#ifdef BITLOOP_DEV_MODE
    void _dispatchQueuedHeadlessInputEvents();
#endif

protected:

    // ----- populating Project/Scene ImGui attributes -----
    void _populateAllAttributes();
    virtual void _populateOverlay();
    virtual void _projectAttributes() {}

    virtual void _initGUI();
    virtual void _destroyGUI();
    
    // ----- Project management (internal, invokes public virtual methods) -----
    void _projectPrepare();
    void _projectStart();
    void _projectStop();
    void _projectPause();
    void _projectResume();
    void _projectDestroy();
    void _projectProcess();
    void _projectDraw();
    void drawViewport(Viewport& viewport, NanoCanvas& target_canvas);
    void _onEndFrame(); // called after worker process & after gui render, just before next project process

    // ----- capturing -----

    void _onEncodeFrame(EncodeFrame& data, int request_id, const CapturePreset& preset);

    // ----- input -----
    std::vector<FingerInfo> pressed_fingers;
    bool any_fingers_pressed = false;
    void _clearEventQueue();
    void _onEvent(const EventData& e);


    // ----- data buffers -----
    virtual void updateProjectLiveBuffer() {}
    virtual void updateProjectShadowBuffer() {}

    virtual void updateLiveBuffers();
    virtual void updateShadowBuffers();
    virtual void updateUnchangedShadowVars();
    virtual bool changedLive();
    virtual bool changedShadow();
    virtual void markLiveValues();
    virtual void markShadowValues();
    virtual void invokeScheduledCalls();

public:

    static ProjectBase* activeProject();

    ProjectBase();

#ifdef BITLOOP_DEV_MODE
    void configureHeadless(int sim_uid, ImDebugLog* project_log, IVec2 surface_size, CaptureContext* capture_context, NanoCanvas* render_canvas = nullptr);
    void startHeadless();
    void prepareHeadlessSceneCatalogue();
    void setHeadlessSceneSelection(std::vector<std::string> scene_ids);
    void setHeadlessSceneConfig(JSON::json values);
    [[nodiscard]] std::vector<SceneCatalogueEntry> sceneCatalogue() const;
    [[nodiscard]] std::vector<std::string> selectedSceneIds() const;
    [[nodiscard]] const std::string& headlessSceneConfigHash() const noexcept { return headless_scene_config_hash; }
    void queueHeadlessInputEvent(const EventData& event);
    void clearHeadlessInputEvents();
    void processHeadlessFrame(double dt);
    void stopHeadless();
    void setTraceFrameContext(std::int64_t frame, double time, double dt);
    void requestTraceSnapshot(const SceneBase* scene, SimSnapshotRequest request);
    [[nodiscard]] std::vector<SimSnapshotRecord> takeTraceSnapshotRequests();
    bool captureTraceSnapshot(SimSnapshotRecord& record, SimSnapshotImage& image) const;
    bool renderLayout(NanoCanvas& render_canvas, IVec2 resolution, std::string& error);
    bool renderViewport(NanoCanvas& render_canvas, SimSnapshotRecord& record);
    void onHeadlessEncodeFrame(EncodeFrame& data, const CapturePreset& preset);
    bool saveHeadlessCheckpoint(
        const std::filesystem::path& checkpoint_dir,
        std::int64_t frame,
        double time,
        double dt) const;
    bool loadHeadlessCheckpoint(const std::filesystem::path& checkpoint_dir);
    [[nodiscard]] std::vector<SimTraceFrame> collectTraceFrames(
        std::int64_t frame,
        double time,
        double dt,
        std::size_t max_objects = SimTraceFrame::default_max_objects) const;
#endif

    // todo: improve access
    MouseInfo mouse;

    virtual ~ProjectBase();

    // A single Scene may override snapshots. Composite projects can select their own source.
    virtual bool captureSnapshot(SnapshotFrame& output) const;

    virtual InterfaceModel* getInterfaceModel() = 0;

    [[nodiscard]] CaptureContext& captureContext();
    [[nodiscard]] const CaptureContext& captureContext() const;
    [[nodiscard]] SettingsConfig& captureSettings();
    [[nodiscard]] const SettingsConfig& captureSettings() const;
    [[nodiscard]] SnapshotPresetManager& snapshotPresetManager();
    [[nodiscard]] const SnapshotPresetManager& snapshotPresetManager() const;
    [[nodiscard]] bool isHeadless() const;
    [[nodiscard]] ThreadQueue& renderThreadQueue();
    
    /// ----- Project factory -----

    [[nodiscard]] static std::vector<std::shared_ptr<ProjectInfo>>& projectInfoList()
    {
        static std::vector<std::shared_ptr<ProjectInfo>> info_list;
        return info_list;
    }
    [[nodiscard]] static ProjectInfoNode& projectTreeRootInfo()
    {
        static ProjectInfoNode root("root");
        return root;
    }
    [[nodiscard]] static std::shared_ptr<ProjectInfo> findProjectInfo(int sim_uid)
    {
        for (auto& info : projectInfoList())
        {
            if (info->sim_uid == sim_uid)
                return info;
        }
        return nullptr;
    }
    [[nodiscard]] static std::shared_ptr<ProjectInfo> findProjectInfo(const char *name)
    {
        for (auto& info : projectInfoList())
        {
            if (info->path.back() == name)
                return info;
        }
        return nullptr;
    }

    static inline int factory_sim_index = 0;

    template<typename T>
    static std::shared_ptr<ProjectInfo> createProjectFactoryInfo(std::string name, const char* dev_root = nullptr)
    {
        auto project_info = std::make_shared<ProjectInfo>(T::info());
        project_info->creator = []() -> ProjectBase* { return new T(); };
        project_info->name = name;
        project_info->sim_uid = ProjectBase::factory_sim_index++;
        project_info->dev_root = dev_root;
        return project_info;
    }

    static void addProjectFactoryInfo(std::shared_ptr<ProjectInfo> project_info)
    {
        std::vector<std::shared_ptr<ProjectInfo>>& project_list = projectInfoList();

        project_list.push_back(project_info);

        ProjectInfoNode* current = &projectTreeRootInfo();

        // Traverse tree and insert info in correct nested category
        for (size_t i = 0; i < project_info->path.size(); i++)
        {
            const std::string& insert_name = project_info->path[i];
            bool is_leaf = (i == (project_info->path.size() - 1));

            if (is_leaf)
            {
                current->children.push_back(ProjectInfoNode(project_info));
                current->children.back().uid = current->count_nodes();
                break;
            }
            else
            {
                // Does category already exist?
                ProjectInfoNode* found_existing = nullptr;
                for (ProjectInfoNode& existing_node : current->children)
                {
                    if (existing_node.name == insert_name)
                    {
                        // Yes
                        found_existing = &existing_node;
                        break;
                    }
                }
                if (found_existing)
                {
                    current = found_existing;
                }
                else
                {
                    current->children.push_back(ProjectInfoNode(insert_name));
                    current->children.back().uid = current->count_nodes();

                    current = &current->children.back();
                }
            }
        }
    }

    [[nodiscard]] std::shared_ptr<ProjectInfo> getProjectInfo() const
    {
        return findProjectInfo(sim_uid);
    }

    /// ----- Scene factory -----

    template<typename SceneType>
    [[nodiscard]] SceneHandle<SceneType> create()
    {
        auto config = std::make_shared<typename SceneType::Config>();
        auto factory = [config]() -> std::unique_ptr<SceneBase>
        {
            if constexpr (std::is_constructible_v<SceneType, typename SceneType::Config&>)
                return std::make_unique<SceneType>(*config);
            else
                return std::make_unique<SceneType>();
        };

        return SceneHandle<SceneType>(declareScene({}, config, std::move(factory)), config);
    }

    template<typename SceneType>
    [[nodiscard]] SceneHandle<SceneType> create(typename SceneType::Config config)
    {
        auto config_ptr = std::make_shared<typename SceneType::Config>(std::move(config));
        auto factory = [config_ptr]() -> std::unique_ptr<SceneBase>
        {
            return std::make_unique<SceneType>(*config_ptr);
        };

        return SceneHandle<SceneType>(declareScene({}, config_ptr, std::move(factory)), config_ptr);
    }

    template<typename SceneType>
    [[nodiscard]] SceneHandle<SceneType> create(std::string id)
    {
        auto config = std::make_shared<typename SceneType::Config>();
        auto factory = [config]() -> std::unique_ptr<SceneBase>
        {
            if constexpr (std::is_constructible_v<SceneType, typename SceneType::Config&>)
                return std::make_unique<SceneType>(*config);
            else
                return std::make_unique<SceneType>();
        };

        return SceneHandle<SceneType>(declareScene(std::move(id), config, std::move(factory)), config);
    }

    template<typename SceneType>
    [[nodiscard]] SceneHandle<SceneType> create(std::string id, typename SceneType::Config config)
    {
        auto config_ptr = std::make_shared<typename SceneType::Config>(std::move(config));
        auto factory = [config_ptr]() -> std::unique_ptr<SceneBase>
        {
            return std::make_unique<SceneType>(*config_ptr);
        };

        return SceneHandle<SceneType>(declareScene(std::move(id), config_ptr, std::move(factory)), config_ptr);
    }

    void setDefaultScenes(std::initializer_list<SceneHandleBase> scenes);
    void destroy(SceneHandleBase scene);

    [[nodiscard]] const std::vector<SceneBase*>& scenes() const
    {
        return active_scenes;
    }

    template<typename T>
    [[nodiscard]] T& getActiveScene(int index) const
    {
        return static_cast<T&>(*active_scenes[index]);
    }

    [[nodiscard]] Layout& currentLayout()
    {
        return viewports;
    }

    /// ----- states -----

    void requestImmediateUpdate()
    {
        immediate_update_requested = true;
    }

    void setFullscreenButtonVisibile(bool b)
    {
        show_fullscreen_btn = b;
    }

    [[nodiscard]] bool isPaused() const { return paused; }

    // Zero-based completed frame within this run; zero before the first step.
    [[nodiscard]] std::int64_t currentFrameIndex() const
    {
        const std::int64_t count = completedFrameCount.load(std::memory_order_relaxed);
        return count > 0 ? count - 1 : 0;
    }
    [[nodiscard]] bool isActive() const { return started; } // in "started/paused" state (but not stopped)

    #ifdef BITLOOP_DEV_MODE
    private:
    [[nodiscard]] std::string projectPath(std::string_view virtual_path) const
    {
        std::filesystem::path p = getProjectInfo()->dev_root;

        if (!virtual_path.empty() && virtual_path.front() == '/')
            virtual_path.remove_prefix(1); // trim leading '/'

        p /= virtual_path; // join
        p = p.lexically_normal(); // clean up
        return p.string();
    }
    public:
    #endif

    /// todo: add IDBFS support for virtual file system on web?
    
    // rootPath("/path/to/file")
    // - useful to modify source data during development, but still behaves in release builds where no project folder exist
    // - for web builds, files are read-only
    // 
    // If DEFINED     (BITLOOP_DEV_MODE):  "root" = project folder
    // If NOT DEFINED (BITLOOP_DEV_MODE):  "root" = exe folder
    
    [[nodiscard]] std::string rootPath(std::string_view virtual_path) const;

    /// ----- overridable virtual methods -----

    virtual std::vector<std::string> categorize()
    {
        return { "New Projects", "Project" };
    }

    virtual void projectAttributes() {}
    virtual void projectPrepare(Layout& layout) = 0;
    virtual void projectStart() {}
    virtual void projectStop() {}
    virtual void projectDestroy() {}
	
#ifdef BITLOOP_DEV_MODE
    virtual bool saveCheckpoint(SimCheckpointWriter&) const { return false; }
    virtual bool loadCheckpoint(const SimCheckpointReader&) { return false; }
#endif

    virtual void onEvent(Event&) {}

    /// ----- logging -----

    void logMessage(const char* fmt, ...);
    void logClear();
};

class BasicProject : public ProjectBase, public DirectInterfaceModel
{
    // todo: UI Not thread-safe, only for simple projects without buffered variables
    void _projectAttributes() override final
    {
        ProjectBase::_projectAttributes();
        sidebar();
    }
    void _populateOverlay() override final
    {
        ProjectBase::_populateOverlay();
        overlay();
    }

    InterfaceModel* getInterfaceModel() override final
    {
        return static_cast<InterfaceModel*>(this);
    }

public:

    using ProjectBase::destroy;
};

template<typename ProjectType>
class Project : public ProjectBase, public VarBuffer<ProjectType>
{
    friend class ProjectBase;

public:

    using ProjectBase::destroy;
    using BufferedInterfaceModel = DoubleBufferedInterfaceModel<ProjectType>;
    BufferedInterfaceModel* ui = nullptr;

    Project() : ProjectBase() {}
    ~Project()
    {
        // should already be destroyed in _destroyGUI on GUI thread
        assert(ui == nullptr);
        if (ui) delete ui;
    }

protected:

    InterfaceModel* getInterfaceModel() override final
    {
        return static_cast<InterfaceModel*>(ui);
    }

    auto getUI() const noexcept
    {
        return static_cast<const ProjectType::UI*>(ui);
    }

    void _initGUI() override final
    {
        if (!ui)
        {
            ui = new ProjectType::UI(static_cast<const ProjectType*>(this));
            ui->init();
        }

        ProjectBase::_initGUI();
    }
    void _destroyGUI() override final
    {
        if (ui)
        {
            ui->destroy();
            delete ui;
            ui = nullptr;
        }

        ProjectBase::_destroyGUI();
    }
    void _projectAttributes() override final
    {
        // todo: Project 'ui' should exist before project has started? (but not scenes ui)
        //if (!ui) return;
        assert(ui != nullptr);
        ProjectBase::_projectAttributes();
        ui->sidebar();
    }
    void _populateOverlay() override final
    {
        // todo: Project 'ui' should exist before project has started? (but not scenes ui)
        assert(ui != nullptr);
        ProjectBase::_populateOverlay();
        ui->overlay();
    }

    void updateLiveBuffers() override final
    {
        ProjectBase::updateLiveBuffers(); // updates scenes
        VarBuffer<ProjectType>::updateLive();
    }
    void updateShadowBuffers() override final
    {
        ProjectBase::updateShadowBuffers(); // updates scenes
        VarBuffer<ProjectType>::updateShadow();
    }
    void markLiveValues() override final
    {
        ProjectBase::markLiveValues(); // marks scenes
        VarBuffer<ProjectType>::markLiveValue();
    }
    void markShadowValues() override final
    {
        ProjectBase::markShadowValues(); // marks scenes
        VarBuffer<ProjectType>::markShadowValue();
    }
    bool changedLive() override final
    {
        if (ProjectBase::changedLive()) return true; // checks scenes
        return VarBuffer<ProjectType>::liveChanged();
    }
    bool changedShadow() override final
    {
        if (ProjectBase::changedShadow()) return true; // checks scenes
        return VarBuffer<ProjectType>::shadowChanged();
    }

    void updateUnchangedShadowVars() override final
    {
        ProjectBase::updateUnchangedShadowVars(); // updates scenes
        VarBuffer<ProjectType>::updateUnchangedShadowVars();
    }

    void invokeScheduledCalls() override final
    {
        ProjectBase::invokeScheduledCalls(); // invoke scheduled calls for all scenes
        VarBuffer<ProjectType>::invokeScheduledCalls();
    }
};



BL_END_NS
