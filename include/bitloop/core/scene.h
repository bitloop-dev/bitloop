#pragma once

#include <random>
#include <chrono>
#include <string>

#include <bitloop/core/interface_model.h>

#include <bitloop/core/snapshot_presets.h>
#include <bitloop/util/math_util.h>
#include <bitloop/util/change_tracker.h>
#include <bitloop/util/timer.h>
#include <bitloop/util/thread_queue.h>

#ifdef BITLOOP_DEV_MODE
    #include <bitloop/core/sim_checkpoint.h>
    #include <bitloop/core/sim_snapshot.h>
    #include <bitloop/core/sim_trace.h>
#endif

#include "types.h"
#include "event.h"

#include "camera.h"
#include "input.h"

BL_BEGIN_NS


class Viewport;
class ProjectBase;

class CaptureContext;
class CaptureManager;
struct SnapshotFrame;
struct SettingsConfig;

class SceneBase : public ChangeTracker
{
    friend class ProjectBase;
    friend class Viewport;

private:

    ProjectBase* project = nullptr;

    mutable std::mt19937 gen;
    double dt_sceneProcess = 0;
    SimpleTimer running_time;

    bool needs_redraw = false;

public:

    [[nodiscard]] ProjectBase& getProject() noexcept
    {
        return *project;
    }

    [[nodiscard]] const ProjectBase& getProject() const noexcept
    {
        return *project;
    }

    template<typename T>
    T& getProject() const
    {
        return static_cast<T&>(*project);
    }
    
    // mutable so we return true the moment we begin capturing,
    // even if CaptureManager hasn't registered it yet.
    mutable bool initiating_snapshot = false;
    mutable bool initiating_recording = false;

    int capture_request_id = 1; // 0 reserved for "no callback"
    std::vector<std::pair<int, SnapshotBatchCallbacks>> snapshot_callbacks;


    virtual void _initGUI() {}
    virtual void _destroyGUI() {}

    int scene_declaration_index = -1;
    std::string scene_id;
    std::vector<Viewport*> mounted_to_viewports;

    bool started = false;
    bool destroyed = false;

    void _sceneStart()
    {
        started = true;
        running_time.begin();
        sceneStart();
    }

    void _sceneDestroy()
    {
        if (!destroyed && started)
        {
            sceneStop();
            sceneDestroy();
            destroyed = true;
            started = false;
        }
    }

    // Mounting to/from viewport
    void registerMount(Viewport* viewport);
    void registerUnmount(Viewport* viewport);

    //
    virtual void _sceneAttributes() {}
    virtual void _populateOverlay() {}
    

    // In case Scene uses double buffer
    virtual void updateLiveBuffers() {}
    virtual void updateShadowBuffers() {}
    virtual bool changedLive() { return false; }
    virtual bool changedShadow() { return false; }
    virtual void markLiveValues() {}
    virtual void markShadowValues() {}
    virtual void updateUnchangedShadowVars() {}
    virtual void invokeScheduledCalls() {}

    //

    void _onEncodeFrame(EncodeFrame& data, int request_id, const CapturePreset& preset);

    //

    void _onEvent(Event e) 
    {
        onEvent(e);

        if (e.ownedBy(this))
        {
            switch (e.type())
            {
            case EventType::FINGER_DOWN:       onPointerDown(PointerEvent(e));  break;
            case EventType::MOUSE_BUTTON_DOWN: onPointerDown(PointerEvent(e));  break;
            case EventType::FINGER_UP:         onPointerUp(PointerEvent(e));    break;
            case EventType::MOUSE_BUTTON_UP:   onPointerUp(PointerEvent(e));    break;
            case EventType::FINGER_MOTION:     onPointerMove(PointerEvent(e));  break;
            case EventType::MOUSE_MOTION:      onPointerMove(PointerEvent(e));  break;
            case EventType::MOUSE_WHEEL:       onWheel(PointerEvent(e));        break;

            case EventType::KEY_DOWN:          onKeyDown(KeyEvent(e));          break;
            case EventType::KEY_UP:            onKeyUp(KeyEvent(e));            break;
            }
        }
    }

public:

    // todo: Give each viewport it's own mouse, don't spill position between viewports
    MouseInfo* mouse = nullptr;

    //Input input;

    struct Config {};
    struct UI {};

    SceneBase() : gen(std::random_device{}()) {}
    virtual ~SceneBase() = default;

    virtual InterfaceModel* getInterfaceModel() = 0;

    void requestRedraw(bool b) { needs_redraw = b; }
    void requestImmediateUpdate();

    virtual void sceneStart() {}
    virtual void sceneMounted(Viewport*) {}
    virtual void sceneUnmounted(Viewport*) {}
    virtual void sceneStop() {}
    virtual void sceneDestroy() {}
    virtual void sceneProcess(double) {}
    virtual void viewportProcess(Viewport*, double) {}
    virtual void viewportDraw(Viewport*) const = 0;
    virtual void onEndFrame() {}

#ifdef BITLOOP_DEV_MODE
    virtual void traceFrame(SimTraceFrame&) const {}
    virtual bool captureTraceSnapshot(std::string_view, SimSnapshotImage&) const { return false; }
    virtual bool saveCheckpoint(SimCheckpointWriter&) const { return false; }
    virtual bool loadCheckpoint(const SimCheckpointReader&) { return false; }
#endif

    virtual void onEvent(Event) {}

    virtual void onPointerEvent(PointerEvent) {}
    virtual void onPointerDown(PointerEvent) {}
    virtual void onPointerUp(PointerEvent) {}
    virtual void onPointerMove(PointerEvent) {}
    virtual void onWheel(PointerEvent) {}
    virtual void onKeyDown(KeyEvent) {}
    virtual void onKeyUp(KeyEvent) {}

    virtual std::string name() const { return "Scene"; }
    [[nodiscard]] const std::string& id() const { return scene_id; }
    [[nodiscard]] int sceneDeclarationIndex() const { return scene_declaration_index; }
    [[nodiscard]] int activeSceneIndex() const noexcept;
    [[nodiscard]] const std::vector<Viewport*>& mountedToViewports() const {
        return mounted_to_viewports;
    }

    [[nodiscard]] double running_dt() const { return running_time.elapsed(); }
    [[nodiscard]] double scene_dt() const { return dt_sceneProcess; } // scene only (not including frame delay)
    [[nodiscard]] double project_dt() const; // project + all scenes (not including frame delay)
    [[nodiscard]] double frame_dt() const; // whole frame time elapsed (including sleep)

    [[nodiscard]] double fps() const { return 1000.0 / frame_dt(); }
    [[nodiscard]] double fpsFactor() const; // rate multier:  1x is real FPS = target FPS

    [[nodiscard]] bool isSnapshotting() const;
    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool isCapturing() const;
    [[nodiscard]] bool capturedLastFrame() const;
    [[nodiscard]] int  capturedFrameCount() const;

    [[nodiscard]] CaptureContext& captureContext();
    [[nodiscard]] const CaptureContext& captureContext() const;
    [[nodiscard]] SettingsConfig& captureSettings();
    [[nodiscard]] const SettingsConfig& captureSettings() const;
    [[nodiscard]] SnapshotPresetManager& snapshotPresetManager();
    [[nodiscard]] const SnapshotPresetManager& snapshotPresetManager() const;
    [[nodiscard]] bool isHeadless() const;
    [[nodiscard]] ThreadQueue& renderThreadQueue();

    void permitCaptureFrame(bool b);
#ifdef BITLOOP_DEV_MODE
    void traceSnapshot(std::string route, std::string reason = {}) const;
    void traceCanvas(std::string id, std::string reason = {}) const;
    void traceCanvas(std::string id, IRect pixel_rect, std::string reason = {}) const;
    void traceCanvasWorld(std::string id, DRect world_rect, std::string reason = {}) const;
    void traceCanvas(const Viewport& viewport, std::string id, std::string reason = {}) const;
    void traceCanvas(const Viewport& viewport, std::string id, IRect pixel_rect, std::string reason = {}) const;
    void traceCanvasWorld(const Viewport& viewport, std::string id, DRect world_rect, std::string reason = {}) const;
#endif

    virtual void onBeginSnapshot() {}
    // Called on the simulation owner after a completed update, never from drawing.
    // False preserves viewport capture; throw to report a failed custom snapshot.
    virtual bool captureSnapshot(SnapshotFrame&) const { return false; }
    virtual void onEncodeFrame(EncodeFrame& data [[maybe_unused]], const CapturePreset& preset [[maybe_unused]]) {}

    void beginSnapshotList(
        const SnapshotPresetList& presets,
        std::string_view relative_filepath = {},
        SnapshotCompleteCallback on_snapshot_complete = nullptr,
        SnapshotBatchCompleteCallback on_batch_complete = nullptr);

    void beginSnapshot(
        const CapturePreset& preset,
        std::string_view relative_filepath = {},
        SnapshotCompleteCallback on_snapshot_complete = nullptr)
    {
        beginSnapshotList(SnapshotPresetList(preset), relative_filepath, on_snapshot_complete, nullptr);
    }

    void beginRecording();
    void endRecording();

    [[nodiscard]] double random(double min = 0, double max = 1) const
    {
        std::uniform_real_distribution<> dist(min, max);
        return dist(gen);
    }

    void logMessage(std::string_view, ...);
    void logClear();
};

class BasicScene : public SceneBase, public DirectInterfaceModel
{
    // no BufferedInterfaceModel* ui, all data is on single thread (todo: test)

    void _sceneAttributes() override final
    {
        sidebar();
    }
    void _populateOverlay() override final
    {
        overlay();
    }

    InterfaceModel* getInterfaceModel() override final
    {
        return static_cast<InterfaceModel*>(this);
    }
};


template<typename SceneType>
class Scene : public SceneBase, public VarBuffer<SceneType>
{
    friend class ProjectBase;

public:

    using BufferedInterfaceModel = DoubleBufferedInterfaceModel<SceneType>;
    BufferedInterfaceModel* ui = nullptr;
    
    Scene() : SceneBase()
    {
    }

    ~Scene()
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
        return static_cast<const SceneType::UI*>(ui);
    }

    void _initGUI() override final
    {
        if (!ui)
        {
            // safer to initialize UI on same thread as UI populate
            ui = new SceneType::UI(static_cast<const SceneType*>(this));
            ui->init();
        }
    }

    void _destroyGUI() override final
    {
        if (ui)
        {
            ui->destroy();
            delete ui;
            ui = nullptr;
        }
    }

    void _sceneAttributes() override final
    {
        ui->sidebar();
    }

    void _populateOverlay() override final
    {
        ui->overlay();
    }

private:

    void updateLiveBuffers() override final          { VarBuffer<SceneType>::updateLive(); }
    void updateShadowBuffers() override final        { VarBuffer<SceneType>::updateShadow(); }
    void markLiveValues() override final             { VarBuffer<SceneType>::markLiveValue(); }
    void markShadowValues() override final           { VarBuffer<SceneType>::markShadowValue(); }
    bool changedLive() override final                { return VarBuffer<SceneType>::liveChanged(); }
    bool changedShadow() override final              { return VarBuffer<SceneType>::shadowChanged(); }
    void updateUnchangedShadowVars() override final  { VarBuffer<SceneType>::updateUnchangedShadowVars(); }
    void invokeScheduledCalls() override final       { VarBuffer<SceneType>::invokeScheduledCalls(); }
};


BL_END_NS
