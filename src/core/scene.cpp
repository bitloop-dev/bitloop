#include <bitloop/core/scene.h>
#include <bitloop/core/project.h>
#include <bitloop/core/viewport.h>
#include <bitloop/core/capture_manager.h>
#include <bitloop/core/capture_context.h>
#include <bitloop/imguix/imguix.h>

#include <stdexcept>

BL_BEGIN_NS

void SceneBase::registerMount(Viewport* viewport)
{
    if (std::find(mounted_to_viewports.begin(), mounted_to_viewports.end(), viewport) == mounted_to_viewports.end())
        mounted_to_viewports.push_back(viewport);
}

void SceneBase::registerUnmount(Viewport* viewport)
{
    auto viewport_it = std::find(mounted_to_viewports.begin(), mounted_to_viewports.end(), viewport);
    if (viewport_it != mounted_to_viewports.end())
        mounted_to_viewports.erase(viewport_it);
}

void SceneBase::requestImmediateUpdate()
{
    project->requestImmediateUpdate();
}

int SceneBase::activeSceneIndex() const noexcept
{
    if (!project) return -1;

    const auto& scenes = project->scenes();
    const auto found = std::find(scenes.begin(), scenes.end(), this);

    return found == scenes.end() ? -1 : static_cast<int>(found - scenes.begin());
}

double SceneBase::frame_dt() const
{
    return project->frame_dt;
}

double SceneBase::project_dt() const
{
    return project->project_dt;
}

double SceneBase::fpsFactor() const
{
    const CaptureContext& capture = project->captureContext();
    if (!capture.isFixedFrameTimeDelta())
    {
        double actual_fps = fps();
        double target_fps = capture.fps();

        return (target_fps / actual_fps);
    }
    else
    {
        return 1.0;
    }
}

bool SceneBase::isSnapshotting() const
{
    bool ret = project->captureContext().isSnapshotting();
    if (ret) initiating_snapshot = false;
    return ret || initiating_snapshot;
}

bool SceneBase::isRecording() const
{
    bool ret = project->captureContext().isRecording();
    if (ret) initiating_recording = false;
    return ret || initiating_recording;
}

bool SceneBase::isCapturing() const
{
    return isSnapshotting() || isRecording();
}

void SceneBase::_onEncodeFrame(EncodeFrame& data, int request_id, const CapturePreset& preset)
{
    onEncodeFrame(data, preset);

    for (size_t i = 0; i < snapshot_callbacks.size(); i++)
    {
        const auto& [callback_id, batch_callbacks] = snapshot_callbacks[i];
        const auto& [on_snapshot_complete, on_batch_complete] = batch_callbacks;

        if (callback_id == request_id)
        {
            if (on_snapshot_complete)
                on_snapshot_complete(data, preset);

            if (on_batch_complete && i == snapshot_callbacks.size() - 1)
                on_batch_complete();

            snapshot_callbacks.erase(snapshot_callbacks.begin() + i);
            return;
        }
    }
}

void SceneBase::beginSnapshotList(
    const SnapshotPresetList& presets,
    std::string_view rel_path_fmt,
    SnapshotCompleteCallback on_snapshot_complete,
    SnapshotBatchCompleteCallback on_batch_complete)
{
    snapshot_callbacks.push_back(std::make_pair(capture_request_id, SnapshotBatchCallbacks{ on_snapshot_complete, on_batch_complete }));

    project->captureContext().queueBeginSnapshot(presets, rel_path_fmt, capture_request_id);

    capture_request_id++;
    initiating_snapshot = true;
}

void SceneBase::beginRecording()
{
    project->captureContext().queueBeginRecording();
    initiating_recording = true;
}

void SceneBase::endRecording()
{
    project->captureContext().queueEndRecording();
    initiating_recording = false;
}

void SceneBase::permitCaptureFrame(bool b)
{
    project->captureContext().captureFrame(b);
}

#ifdef BITLOOP_DEV_MODE
void SceneBase::traceSnapshot(std::string route, std::string reason) const
{
    if (route.empty())
        throw std::invalid_argument("Snapshot route must not be empty");

    SimSnapshotRequest request;
    request.id = route;
    request.route = std::move(route);
    request.reason = std::move(reason);
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}

void SceneBase::traceCanvas(std::string id, std::string reason) const
{
    SimSnapshotRequest request;
    request.id = std::move(id);
    request.reason = std::move(reason);
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}

void SceneBase::traceCanvas(std::string id, IRect pixel_rect, std::string reason) const
{
    SimSnapshotRequest request;
    request.id = std::move(id);
    request.reason = std::move(reason);
    request.pixel_rect = pixel_rect;
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}

void SceneBase::traceCanvasWorld(std::string id, DRect world_rect, std::string reason) const
{
    SimSnapshotRequest request;
    request.id = std::move(id);
    request.reason = std::move(reason);
    request.world_rect = world_rect;
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}

void SceneBase::traceCanvas(const Viewport& viewport, std::string id, std::string reason) const
{
    if (viewport.mountedScene() != this)
        throw std::invalid_argument("Snapshot Viewport is not mounted to this Scene");

    SimSnapshotRequest request;
    request.id = std::move(id);
    request.reason = std::move(reason);
    request.viewport_id = viewport.id();
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}

void SceneBase::traceCanvas(const Viewport& viewport, std::string id, IRect pixel_rect, std::string reason) const
{
    if (viewport.mountedScene() != this)
        throw std::invalid_argument("Snapshot Viewport is not mounted to this Scene");

    SimSnapshotRequest request;
    request.id = std::move(id);
    request.reason = std::move(reason);
    request.viewport_id = viewport.id();
    request.pixel_rect = pixel_rect;
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}

void SceneBase::traceCanvasWorld(const Viewport& viewport, std::string id, DRect world_rect, std::string reason) const
{
    if (viewport.mountedScene() != this)
        throw std::invalid_argument("Snapshot Viewport is not mounted to this Scene");

    SimSnapshotRequest request;
    request.id = std::move(id);
    request.reason = std::move(reason);
    request.viewport_id = viewport.id();
    request.world_rect = world_rect;
    if (project)
        project->requestTraceSnapshot(this, std::move(request));
}
#endif

bool SceneBase::capturedLastFrame() const
{
    return project->captureContext().capturedLastFrame();
}

int SceneBase::capturedFrameCount() const
{
    return project->captureContext().capturedFrameCount();
}

CaptureContext& SceneBase::captureContext()
{
    return project->captureContext();
}

const CaptureContext& SceneBase::captureContext() const
{
    return project->captureContext();
}

SettingsConfig& SceneBase::captureSettings()
{
    return project->captureSettings();
}

const SettingsConfig& SceneBase::captureSettings() const
{
    return project->captureSettings();
}

SnapshotPresetManager& SceneBase::snapshotPresetManager()
{
    return project->snapshotPresetManager();
}

const SnapshotPresetManager& SceneBase::snapshotPresetManager() const
{
    return project->snapshotPresetManager();
}

bool SceneBase::isHeadless() const
{
    return project && project->isHeadless();
}

ThreadQueue& SceneBase::renderThreadQueue()
{
    return project->renderThreadQueue();
}


void SceneBase::logClear()
{
    project->project_log->clear();
}

void SceneBase::logMessage(std::string_view fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    project->project_log->vlog(fmt.data(), ap);
    va_end(ap);
}

BL_END_NS
