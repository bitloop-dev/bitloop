#include <bitloop/core/project.h>

#include <bitloop/core/capture_context.h>
#include <bitloop/core/capture_manager.h>
#include <bitloop/core/project_worker.h>
#include <bitloop/core/main_window.h>
#include <bitloop/core/viewport.h>
#include <bitloop/core/scene.h>
#include <bitloop/platform/platform.h>
#include <bitloop/util/hashable.h>
#include <bitloop/imguix/imguix.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

BL_BEGIN_NS

namespace
{
    bool validIdentifier(std::string_view id)
    {
        if (id.empty())
            return false;

        return std::all_of(id.begin(), id.end(), [](unsigned char value)
        {
            return std::isalnum(value) || value == '-' || value == '_' || value == '.';
        });
    }

    std::string projectDisplayName(const ProjectBase& project)
    {
        const std::shared_ptr<ProjectInfo> info = project.getProjectInfo();
        if (!info)
            return {};
        return !info->name.empty() ? info->name : info->path.back();
    }

    std::string sceneDisplayName(const SceneBase& scene)
    {
        return scene.name() + " " + std::to_string(scene.sceneDeclarationIndex());
    }

#ifdef BITLOOP_DEV_MODE
    std::string launchConfigHash(const JSON::json& values)
    {
        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0')
            << StableHasher::hash_string(values.dump());
        return out.str();
    }

    void writeJsonFile(const std::filesystem::path& path, const JSON::json& json)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
            throw std::runtime_error("Could not write JSON file: " + path.string());
        out << json.dump(2) << '\n';
    }

    class ViewportGeometryScope
    {
        Viewport& viewport;
        DRect surface_rect;
        DRect client_rect;

    public:

        ViewportGeometryScope(Viewport& viewport, IVec2 resolution)
            : viewport(viewport), surface_rect(viewport.surfaceRect()), client_rect(viewport.clientRect())
        {
            viewport.setSurfacePos(0.0, 0.0);
            viewport.setSurfaceSize(static_cast<double>(resolution.x), static_cast<double>(resolution.y));
            viewport.setClientRect(0.0, 0.0, static_cast<double>(resolution.x), static_cast<double>(resolution.y));
        }

        ~ViewportGeometryScope()
        {
            viewport.setSurfacePos(surface_rect.x1, surface_rect.y1);
            viewport.setSurfaceSize(surface_rect.width(), surface_rect.height());
            viewport.setClientRect(client_rect.x1, client_rect.y1, client_rect.width(), client_rect.height());
        }
    };
#endif
}

struct ProjectBase::SceneRecord
{
    enum class Lifecycle
    {
        Declared,
        Materialized,
        Started,
        Destroyed
    };

    std::string id;
    std::uint64_t token = 0;
    std::uint64_t generation = 0;
    std::size_t ordinal = 0;
    bool named = false;
    bool selected = false;
    std::shared_ptr<void> config;
    std::function<std::unique_ptr<SceneBase>()> factory;
    std::function<void(const JSON::json&)> launch_config_parser;
    std::unique_ptr<SceneBase> scene;
    Lifecycle lifecycle = Lifecycle::Declared;
};

bool SceneHandleBase::ready() const noexcept
{
    return get() != nullptr;
}

SceneBase* SceneHandleBase::get() const noexcept
{
    const std::shared_ptr<SceneHandleState> locked = state.lock();
    if (!locked || !locked->project || locked->generation != generation)
        return nullptr;
    return locked->project->resolveSceneHandle(*this);
}

std::string SceneHandleBase::id() const
{
    const std::shared_ptr<SceneHandleState> locked = state.lock();
    if (!locked || !locked->project || locked->generation != generation)
        return {};
    return locked->project->sceneHandleId(*this);
}

void SceneHandleBase::setLaunchConfigParserImpl(std::function<void(const JSON::json&)> parser) const
{
    const std::shared_ptr<SceneHandleState> locked = state.lock();
    if (!locked || !locked->project || locked->generation != generation)
        throw std::logic_error("Cannot configure an invalid Scene handle");
    locked->project->registerSceneLaunchConfigParser(*this, std::move(parser));
}

SceneHandleBase::operator bool() const noexcept
{
    const std::shared_ptr<SceneHandleState> locked = state.lock();
    return locked && locked->project && locked->generation == generation && locked->project->findSceneRecord(token, generation);
}

/*TouchState::TouchState(const Event& e, const std::vector<FingerInfo>& _fingers) //: event(e)
{
    for (const FingerInfo& f : _fingers)
        fingers.push_back(f);

    Viewport* viewport = e.ctx_owner();
    Camera& cam = viewport->camera;

    for (SceneFingerInfo& f : fingers)
    {
        double viewport_mx = f.x - viewport->posX();
        double viewport_my = f.y - viewport->posY();

        if (cam.isPanning() ||
            (viewport_mx >= 0 &&
            viewport_my >= 0 &&
            viewport_mx <= viewport->width() &&
            viewport_my <= viewport->height()))
        {

            DVec2 world_mouse = cam.toWorld(viewport_mx, viewport_my);
            f.viewport = viewport;
            f.stage_x = viewport_mx;
            f.stage_y = viewport_my;
            f.world_x = world_mouse.x;
            f.world_y = world_mouse.y;
        }
    }
}*/

ProjectBase* ProjectBase::activeProject()
{
    return project_worker()->current_project;
}

ProjectBase::ProjectBase() = default;

std::string ProjectBase::rootPath(std::string_view virtual_path) const
{
    #ifdef BITLOOP_DEV_MODE
    return projectPath(virtual_path);
    #else
    return platform()->path(virtual_path);
    #endif
}

ProjectBase::~ProjectBase()
{
    discardStructuralMutations();
    viewports.clearImmediate(true);
    viewports.project = nullptr;
    clearSceneRecords();

    if (scene_handle_state)
        scene_handle_state->project = nullptr;
}

QDVec2 MouseInfo::worldPosition(const CameraInfo& camera) const
{
    assert(viewport);
    return camera.transformFor(*viewport).toWorld<fqd>(stage_x, stage_y);
}

ProjectBase::SceneRecord* ProjectBase::findSceneRecord(std::uint64_t token, std::uint64_t generation) noexcept
{
    if (generation != scene_generation)
        return nullptr;

    auto found = std::find_if(scene_records.begin(), scene_records.end(), [&](const std::unique_ptr<SceneRecord>& record)
    {
        return record->token == token && record->generation == generation;
    });
    return found != scene_records.end() ? found->get() : nullptr;
}

const ProjectBase::SceneRecord* ProjectBase::findSceneRecord(std::uint64_t token, std::uint64_t generation) const noexcept
{
    if (generation != scene_generation)
        return nullptr;

    auto found = std::find_if(scene_records.begin(), scene_records.end(), [&](const std::unique_ptr<SceneRecord>& record)
    {
        return record->token == token && record->generation == generation;
    });
    return found != scene_records.end() ? found->get() : nullptr;
}

bool ProjectBase::ownsSceneHandle(const SceneHandleBase& handle) const noexcept
{
    const std::shared_ptr<SceneHandleState> locked = handle.state.lock();
    return locked && locked.get() == scene_handle_state.get() && findSceneRecord(handle.token, handle.generation);
}

SceneBase* ProjectBase::resolveSceneHandle(const SceneHandleBase& handle) const noexcept
{
    if (!ownsSceneHandle(handle))
        return nullptr;

    const SceneRecord* record = findSceneRecord(handle.token, handle.generation);
    return record ? record->scene.get() : nullptr;
}

std::string ProjectBase::sceneHandleId(const SceneHandleBase& handle) const
{
    if (!ownsSceneHandle(handle))
        return {};

    const SceneRecord* record = findSceneRecord(handle.token, handle.generation);
    return record ? record->id : std::string{};
}

void ProjectBase::registerSceneLaunchConfigParser(
    const SceneHandleBase& handle,
    std::function<void(const JSON::json&)> parser)
{
    if (!declaring_scenes)
        throw std::logic_error("Scene launch-config parsers may be registered only during projectPrepare()");
    if (!parser)
        throw std::invalid_argument("Scene launch-config parser cannot be empty");

    SceneRecord* record = findSceneRecord(handle.token, handle.generation);
    if (!ownsSceneHandle(handle) || !record)
        throw std::invalid_argument("Scene handle does not belong to this Project generation");
    if (record->launch_config_parser)
        throw std::logic_error("Scene launch-config parser is already registered for Scene: " + record->id);
    record->launch_config_parser = std::move(parser);
}

SceneHandleBase ProjectBase::declareScene(
    std::string id,
    std::shared_ptr<void> config,
    std::function<std::unique_ptr<SceneBase>()> factory)
{
    if (tearing_down)
        throw std::logic_error("Scenes cannot be created while the Project is tearing down");
    if (!declaring_scenes && !started && !iterating_lifecycle)
        throw std::logic_error("Scenes may be created only during projectPrepare() or while the Project is running");

    const bool named = !id.empty();
    const std::size_t ordinal = next_scene_ordinal++;
    if (!named)
        id = "scene-" + std::to_string(ordinal);
    else if (!validIdentifier(id))
        throw std::invalid_argument("Scene ID must contain only letters, digits, '.', '_' or '-'");

    for (const std::unique_ptr<SceneRecord>& record : scene_records)
    {
        if (record->id == id)
            throw std::invalid_argument("Duplicate Scene ID: " + id);
    }

    auto record = std::make_unique<SceneRecord>();
    record->id = std::move(id);
    record->token = next_scene_token++;
    record->generation = scene_generation;
    record->ordinal = ordinal;
    record->named = named;
    record->config = std::move(config);
    record->factory = std::move(factory);

    SceneHandleBase handle(scene_handle_state, record->token, record->generation);
    scene_records.push_back(std::move(record));

    if (!declaring_scenes)
    {
        scene_records.back()->selected = true;
        const std::uint64_t token = scene_records.back()->token;
        enqueueStructuralMutation([this, token]() { materializeRuntimeScene(token); });
    }

    return handle;
}

void ProjectBase::setDefaultScenes(std::initializer_list<SceneHandleBase> scenes)
{
    if (!declaring_scenes)
        throw std::logic_error("Default Scenes may be selected only from projectPrepare()");

    defaults_specified = true;
    default_scene_tokens.clear();

    for (const SceneHandleBase& scene : scenes)
    {
        if (!ownsSceneHandle(scene))
            throw std::invalid_argument("Default Scene handle does not belong to this Project generation");
        if (std::find(default_scene_tokens.begin(), default_scene_tokens.end(), scene.token) != default_scene_tokens.end())
            throw std::invalid_argument("A default Scene was selected more than once");
        default_scene_tokens.push_back(scene.token);
    }
}

void ProjectBase::applySceneSelection()
{
    for (std::unique_ptr<SceneRecord>& record : scene_records)
        record->selected = false;

#ifdef BITLOOP_DEV_MODE
    if (headless_mode && headless_scene_selection)
    {
        std::unordered_set<std::string> requested;
        for (const std::string& id : *headless_scene_selection)
        {
            if (!validIdentifier(id))
                throw std::invalid_argument("Invalid Scene ID in headless selection: " + id);
            if (!requested.insert(id).second)
                throw std::invalid_argument("Scene selected more than once: " + id);

            auto found = std::find_if(scene_records.begin(), scene_records.end(), [&](const std::unique_ptr<SceneRecord>& record)
            {
                return record->named && record->id == id;
            });
            if (found == scene_records.end())
            {
                std::string available;
                for (const std::unique_ptr<SceneRecord>& record : scene_records)
                {
                    if (!record->named)
                        continue;
                    if (!available.empty())
                        available += ", ";
                    available += record->id;
                }
                throw std::invalid_argument(
                    "Unknown Scene ID: " + id +
                    (available.empty() ? std::string{} : ". Available Scenes: " + available));
            }
        }

        for (std::unique_ptr<SceneRecord>& record : scene_records)
            record->selected = requested.contains(record->id);
        return;
    }
#endif

    const std::unordered_set<std::uint64_t> defaults(default_scene_tokens.begin(), default_scene_tokens.end());
    for (std::unique_ptr<SceneRecord>& record : scene_records)
        record->selected = !defaults_specified || defaults.contains(record->token);
}

#ifdef BITLOOP_DEV_MODE
void ProjectBase::applyHeadlessSceneConfig()
{
    if (!headless_mode || !headless_scene_config)
        return;
    if (!headless_scene_selection || headless_scene_selection->size() != 1)
        throw std::invalid_argument("A Scene launch config requires exactly one explicitly selected Scene");

    auto found = std::find_if(scene_records.begin(), scene_records.end(), [](const std::unique_ptr<SceneRecord>& record)
    {
        return record->selected;
    });
    if (found == scene_records.end())
        throw std::logic_error("The selected Scene could not be resolved for its launch config");

    SceneRecord& record = **found;
    if (!record.launch_config_parser)
        throw std::invalid_argument("Scene '" + record.id + "' does not support --scene-config");

    try
    {
        record.launch_config_parser(*headless_scene_config);
    }
    catch (const std::exception& error)
    {
        throw std::invalid_argument("Invalid launch config for Scene '" + record.id + "': " + error.what());
    }
    catch (...)
    {
        throw std::invalid_argument("Invalid launch config for Scene '" + record.id + "'");
    }
}
#endif

void ProjectBase::materializeSelectedScenes()
{
    applySceneSelection();
#ifdef BITLOOP_DEV_MODE
    applyHeadlessSceneConfig();
#endif

    active_scenes.clear();
    for (std::unique_ptr<SceneRecord>& record : scene_records)
    {
        if (!record->selected)
            continue;

        record->scene = record->factory();
        if (!record->scene)
            throw std::runtime_error("Scene factory returned null for Scene: " + record->id);

        record->scene->project = this;
        record->scene->scene_declaration_index = static_cast<int>(record->ordinal);
        record->scene->scene_id = record->id;
        record->lifecycle = SceneRecord::Lifecycle::Materialized;
        active_scenes.push_back(record->scene.get());
    }

    viewports.resolveScenes();
}

void ProjectBase::materializeRuntimeScene(std::uint64_t token)
{
    SceneRecord* record = findSceneRecord(token, scene_generation);
    if (!record || record->scene)
        return;

    record->scene = record->factory();
    if (!record->scene)
        throw std::runtime_error("Scene factory returned null for Scene: " + record->id);

    record->scene->project = this;
    record->scene->scene_declaration_index = static_cast<int>(record->ordinal);
    record->scene->scene_id = record->id;
    record->scene->mouse = &mouse;
    record->scene->clearCurrent();
    record->lifecycle = SceneRecord::Lifecycle::Materialized;

    active_scenes.push_back(record->scene.get());
    std::stable_sort(active_scenes.begin(), active_scenes.end(), [](const SceneBase* lhs, const SceneBase* rhs)
    {
        return lhs->sceneDeclarationIndex() < rhs->sceneDeclarationIndex();
    });

    record->scene->_sceneStart();
    record->lifecycle = SceneRecord::Lifecycle::Started;

#ifdef BITLOOP_DEV_MODE
    if (headless_mode)
        return;
#endif

    main_window()->threadQueue().invokeBlocking([&]()
    {
        record->scene->_initGUI();
        main_window()->threadQueue().drain();
    });
}

void ProjectBase::destroySceneUI(SceneBase& scene)
{
#ifdef BITLOOP_DEV_MODE
    if (headless_mode)
        return;
#endif

    main_window()->threadQueue().invokeBlocking([&]()
    {
        scene._destroyGUI();
        main_window()->threadQueue().drain();
    });
}

void ProjectBase::destroyScene(std::uint64_t token)
{
    auto found = std::find_if(scene_records.begin(), scene_records.end(), [&](const std::unique_ptr<SceneRecord>& record)
    {
        return record->token == token && record->generation == scene_generation;
    });
    if (found == scene_records.end())
        return;

    SceneRecord& record = **found;
    SceneHandleBase handle(scene_handle_state, record.token, record.generation);
    viewports.removeSceneViewports(handle);

    if (record.scene)
    {
        active_scenes.erase(std::remove(active_scenes.begin(), active_scenes.end(), record.scene.get()), active_scenes.end());
        destroySceneUI(*record.scene);
        record.scene->_sceneDestroy();

        ThreadQueue& queue = renderThreadQueue();
        queue.invokeBlocking([&queue]() { queue.drain(); });

        record.lifecycle = SceneRecord::Lifecycle::Destroyed;
        record.scene.reset();
    }

    default_scene_tokens.erase(std::remove(default_scene_tokens.begin(), default_scene_tokens.end(), token), default_scene_tokens.end());
    scene_records.erase(found);
}

void ProjectBase::enqueueStructuralMutation(std::function<void()> mutation)
{
    std::lock_guard<std::mutex> lock(structural_mutation_mutex);
    structural_mutations.push_back(std::move(mutation));
}

bool ProjectBase::shouldDeferStructuralMutation() const noexcept
{
    return started || iterating_lifecycle || tearing_down;
}

void ProjectBase::commitStructuralMutations(bool gui_access_excluded)
{
    std::vector<std::function<void()>> mutations;
    {
        std::lock_guard<std::mutex> lock(structural_mutation_mutex);
        mutations.swap(structural_mutations);
    }
    if (mutations.empty())
        return;

    std::unique_lock<std::mutex> gui_lock;
    bool lock_gui = !gui_access_excluded;
#ifdef BITLOOP_DEV_MODE
    lock_gui = lock_gui && !headless_mode;
#endif
    if (lock_gui)
    {
        ProjectWorker* worker = project_worker();
        assert(worker != nullptr);
        if (worker)
            gui_lock = std::unique_lock<std::mutex>(worker->shared_sync.shadow_buffer_mutex);
    }

    // ImGui traverses Scene and Viewport topology while holding this same lock;
    // releasing it publishes the complete structural batch to the GUI.
    for (std::function<void()>& mutation : mutations)
        mutation();

    updateViewportRects();
    for (Viewport* viewport : viewports)
        viewport->notifyMounted();

    for (SceneBase* scene : active_scenes)
        scene->requestRedraw(true);
    if (canvas)
        canvas->setDirty();
}

void ProjectBase::discardStructuralMutations()
{
    std::lock_guard<std::mutex> lock(structural_mutation_mutex);
    structural_mutations.clear();
}

void ProjectBase::destroy(SceneHandleBase scene)
{
    if (!ownsSceneHandle(scene))
        throw std::invalid_argument("Scene handle does not belong to this Project generation");

    const std::uint64_t token = scene.token;
    if (shouldDeferStructuralMutation())
        enqueueStructuralMutation([this, token]() { destroyScene(token); });
    else
        destroyScene(token);
}

void ProjectBase::releaseViewportReferences(Viewport* viewport)
{
    if (ctx_focused == viewport)
        ctx_focused = nullptr;
    if (ctx_hovered == viewport)
        ctx_hovered = nullptr;
    if (mouse.viewport == viewport)
        mouse.viewport = nullptr;

    pressed_fingers.erase(
        std::remove_if(pressed_fingers.begin(), pressed_fingers.end(), [&](const FingerInfo& finger)
        {
            return finger.ctx_owner == viewport;
        }),
        pressed_fingers.end());
    any_fingers_pressed = !pressed_fingers.empty();
}

Viewport* ProjectBase::hitTestViewport(DVec2 client_position) const
{
    const std::vector<Viewport*>& ordered = viewports.orderedViewports();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it)
    {
        Viewport* viewport = *it;
        if (viewport->isVisible()
            && viewport->pointerEvents() == PointerEvents::Auto
            && viewport->clientRect().hitTest(client_position.x, client_position.y))
        {
            return viewport;
        }
    }
    return nullptr;
}

void ProjectBase::clearSceneRecords()
{
    active_scenes.clear();
    scene_records.clear();
    default_scene_tokens.clear();
    defaults_specified = false;
    next_scene_token = 1;
    next_scene_ordinal = 0;
    scene_generation++;

    if (scene_handle_state)
    {
        scene_handle_state->project = this;
        scene_handle_state->generation = scene_generation;
    }
}


void ProjectBase::configure(int _sim_uid, NanoCanvas* _canvas, ImDebugLog* shared_log, CaptureContext* shared_capture_context)
{
    sim_uid = _sim_uid;
    canvas = _canvas;
    project_log = shared_log;
    capture_context = shared_capture_context;
#ifdef BITLOOP_DEV_MODE
    headless_mode = false;
#endif

    viewports.project = this;
    scene_handle_state->project = this;
    scene_handle_state->generation = scene_generation;

    started = false;
    paused = false;
}

#ifdef BITLOOP_DEV_MODE
void ProjectBase::configureHeadless(
    int _sim_uid,
    ImDebugLog* shared_log,
    IVec2 surface_size,
    CaptureContext* shared_capture_context,
    NanoCanvas* render_canvas)
{
    configure(_sim_uid, render_canvas, shared_log, shared_capture_context);
    headless_mode = true;
    headless_surface_size = surface_size;
    trace_context_frame = 0;
    trace_context_time = 0.0;
    trace_context_dt = 0.0;
    pending_trace_snapshots.clear();
    headless_input_event_queue.clear();
}

void ProjectBase::prepareHeadlessSceneCatalogue()
{
    assert(headless_mode);
    if (started)
        throw std::logic_error("Cannot prepare a Scene catalogue while the Project is running");
    declareSceneCatalogue();
}

void ProjectBase::setHeadlessSceneSelection(std::vector<std::string> scene_ids)
{
    assert(headless_mode);
    if (started)
        throw std::logic_error("Headless Scene selection must be set before the Project starts");

    std::unordered_set<std::string> unique;
    for (const std::string& id : scene_ids)
    {
        if (!validIdentifier(id))
            throw std::invalid_argument("Invalid Scene ID in headless selection: " + id);
        if (!unique.insert(id).second)
            throw std::invalid_argument("Scene selected more than once: " + id);
    }
    headless_scene_selection = std::move(scene_ids);
}

void ProjectBase::setHeadlessSceneConfig(JSON::json values)
{
    assert(headless_mode);
    if (started)
        throw std::logic_error("Headless Scene configuration must be set before the Project starts");
    if (!values.is_object())
        throw std::invalid_argument("Scene config JSON must contain one top-level object");

    headless_scene_config_hash = launchConfigHash(values);
    headless_scene_config = std::move(values);
}

std::vector<SceneCatalogueEntry> ProjectBase::sceneCatalogue() const
{
    const std::unordered_set<std::uint64_t> defaults(default_scene_tokens.begin(), default_scene_tokens.end());
    std::vector<SceneCatalogueEntry> catalogue;
    catalogue.reserve(scene_records.size());
    for (const std::unique_ptr<SceneRecord>& record : scene_records)
    {
        catalogue.push_back({
            record->id,
            record->ordinal,
            record->named,
            !defaults_specified || defaults.contains(record->token)
        });
    }
    return catalogue;
}

std::vector<std::string> ProjectBase::selectedSceneIds() const
{
    std::vector<std::string> ids;
    ids.reserve(active_scenes.size());
    for (const SceneBase* scene : active_scenes)
        ids.push_back(scene->id());
    return ids;
}
#endif

CaptureContext& ProjectBase::captureContext()
{
    assert(capture_context != nullptr);
    return *capture_context;
}

const CaptureContext& ProjectBase::captureContext() const
{
    assert(capture_context != nullptr);
    return *capture_context;
}

SettingsConfig& ProjectBase::captureSettings()
{
    return captureContext().settings();
}

const SettingsConfig& ProjectBase::captureSettings() const
{
    return captureContext().settings();
}

SnapshotPresetManager& ProjectBase::snapshotPresetManager()
{
    return captureContext().presetManager();
}

const SnapshotPresetManager& ProjectBase::snapshotPresetManager() const
{
    return captureContext().presetManager();
}

bool ProjectBase::isHeadless() const
{
#ifdef BITLOOP_DEV_MODE
    return headless_mode;
#else
    return false;
#endif
}

ThreadQueue& ProjectBase::renderThreadQueue()
{
#ifdef BITLOOP_DEV_MODE
    if (headless_mode)
        return headless_thread_queue;
#endif
    return main_window()->threadQueue();
}

void ProjectBase::_populateAllAttributes()
{
    if (getInterfaceModel() &&
        getInterfaceModel()->sidebarVisible())
    {
        bool showUI = ImGui::CollapsingHeader("Project", ImGuiTreeNodeFlags_DefaultOpen);

        if (showUI)
        {
            ImGui::PushID("project_section");
            _projectAttributes();
            ImGui::PopID();
        }

        ImGui::Dummy(scale_size(0, 8));
    }
	
    // Populate UI for focused Scene
    if (ctx_focused)
    {
        SceneBase* scene = ctx_focused->scene;

        if (scene->getInterfaceModel() &&
            scene->getInterfaceModel()->sidebarVisible())
        {
            std::string sceneName = scene->name() + " " + std::to_string(scene->sceneDeclarationIndex());
            std::string section_id = sceneName + "_section";

            // If we have only one viewport, always show its UI (and don't show collapsing header)
            bool showSceneUI =
                (viewports.count() == 1) ||
                ImGui::CollapsingHeader(sceneName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

            if (showSceneUI)
            {
                // Allow Scene to populate inputs for section
                ImGui::PushID(section_id.c_str());

                // blue
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.35f, 0.60f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.35f, 0.42f, 0.7f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));

                // red
                //ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
                //ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
                //ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));

                scene->_sceneAttributes();

                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
        }
    }
}

bool DrawFullscreenOverlayButton(bool* fullscreen, bool* was_held, ImVec2 screen_pos, float size = 30.0f, float alpha = 0.66f)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const ImRect r(screen_pos, screen_pos + ImVec2(size, size));

    // Colors
    const ImU32 col_bg = ImGui::GetColorU32(ImVec4(0, 0, 0, alpha));
    const ImU32 col_bg_h = ImGui::GetColorU32(ImVec4(0, 0, 0, alpha + 0.15f));
    const ImU32 col_bg_p = ImGui::GetColorU32(ImVec4(0, 0, 0, alpha + 0.25f));
    const ImU32 col_bd = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f));
    const ImU32 col_fg = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);

    // Hit test
    const bool hovered = r.Contains(io.MousePos);
    const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool clicked = hovered && *was_held && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    *was_held = held;

    // Background
    const float rounding = size / 6.0f;
    dl->AddRectFilled(r.Min, r.Max, held ? col_bg_p : (hovered ? col_bg_h : col_bg), rounding);
    dl->AddRect(r.Min, r.Max, col_bd, rounding);

    // Icon geometry
    const float t = scale_size(2.0f);     // stroke thickness
    const float inset = size * 0.08f;         // distance from edges
    const float len = size * 0.15f;         // target leg length

    const float max_len = ImMax(0.0f, (size - 2.0f * inset) * 0.5f);
    const float L = ImMin(len, max_len);

    auto lineL = [&](const ImVec2& c, const ImVec2& dx, const ImVec2& dy)
    {
        dl->AddLine(c, c + dx * L, col_fg, t);
        dl->AddLine(c, c + dy * L, col_fg, t);
    };

    // Outer inset corners
    const ImVec2 tl = r.Min + ImVec2(inset, inset);
    const ImVec2 tr = ImVec2(r.Max.x - inset, r.Min.y + inset);
    const ImVec2 bl = ImVec2(r.Min.x + inset, r.Max.y - inset);
    const ImVec2 br = r.Max - ImVec2(inset, inset);

    if (!*fullscreen)
    {
        // Expand: inner box corners (open outward)
        const ImVec2 ia = r.Min + ImVec2(inset + L, inset + L);
        const ImVec2 ib = r.Max - ImVec2(inset + L, inset + L);

        lineL(ImVec2(ia.x, ia.y), ImVec2(+1, 0), ImVec2(0, +1)); // top-left
        lineL(ImVec2(ib.x, ia.y), ImVec2(-1, 0), ImVec2(0, +1)); // top-right
        lineL(ImVec2(ia.x, ib.y), ImVec2(+1, 0), ImVec2(0, -1)); // bottom-left
        lineL(ImVec2(ib.x, ib.y), ImVec2(-1, 0), ImVec2(0, -1)); // bottom-right
    }
    else
    {
        // Collapse: from outer corners inward
        const float inset2 = inset * 2.0f;
        const ImVec2 ia = r.Min + ImVec2(inset2 + L, inset2 + L);
        const ImVec2 ib = r.Max - ImVec2(inset2 + L, inset2 + L);

        lineL(ImVec2(ia.x, ia.y), ImVec2(-1, 0), ImVec2(0, -1)); // top-left inward
        lineL(ImVec2(ib.x, ia.y), ImVec2(+1, 0), ImVec2(0, -1)); // top-right inward
        lineL(ImVec2(ia.x, ib.y), ImVec2(-1, 0), ImVec2(0, +1)); // bottom-left inward
        lineL(ImVec2(ib.x, ib.y), ImVec2(+1, 0), ImVec2(0, +1)); // bottom-right inward
    }

    if (clicked)
    {
        *fullscreen = !*fullscreen;
        return true;
    }
    return false;
}

inline bool DrawSidebarOverlayButton(
    bool* sidebar_visible,
    ImVec2 screen_pos,
    float btn_w = 22.0f,
    float btn_h = 30.0f,
    float alpha = 0.66f)
{
    if (!sidebar_visible)
        return false;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const ImVec2 rmin = screen_pos;                    // top-left
    const ImVec2 rmax = screen_pos + ImVec2(btn_w, btn_h);

    const ImU32 col_bg = ImGui::GetColorU32(ImVec4(0, 0, 0, alpha));
    const ImU32 col_bg_h = ImGui::GetColorU32(ImVec4(0, 0, 0, ImClamp(alpha + 0.15f, 0.0f, 1.0f)));
    const ImU32 col_bg_p = ImGui::GetColorU32(ImVec4(0, 0, 0, ImClamp(alpha + 0.25f, 0.0f, 1.0f)));
    const ImU32 col_bd = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f));
    const ImU32 col_fg = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);

    const bool hovered = (io.MousePos.x >= rmin.x && io.MousePos.x < rmax.x &&
        io.MousePos.y >= rmin.y && io.MousePos.y < rmax.y);
    const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImGui::PushID(sidebar_visible);
    const ImGuiID hold_id = ImGui::GetID("##sidebar_overlay_hold");
    ImGui::PopID();

    ImGuiStorage* st = ImGui::GetStateStorage();
    const bool was_held = st->GetBool(hold_id, false);
    const bool clicked = hovered && was_held && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    st->SetBool(hold_id, held);

    const float rounding = btn_h / 6.0f;
    const ImDrawFlags round_left =
        ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft;

    dl->AddRectFilled(rmin, rmax, held ? col_bg_p : (hovered ? col_bg_h : col_bg), rounding, round_left);
    dl->AddRect(rmin, rmax, col_bd, rounding, round_left);

    const ImVec2 c((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);
    const float t = scale_size(2.0f);

    const float chevron_w = btn_w * 0.20f;
    const float chevron_h = btn_h * 0.13f;

    if (*sidebar_visible)
    {
        const ImVec2 p0(c.x - chevron_w, c.y - chevron_h);
        const ImVec2 p1(c.x + chevron_w, c.y);
        const ImVec2 p2(c.x - chevron_w, c.y + chevron_h);
        dl->AddLine(p0, p1, col_fg, t);
        dl->AddLine(p2, p1, col_fg, t);
    }
    else
    {
        const ImVec2 p0(c.x + chevron_w, c.y - chevron_h);
        const ImVec2 p1(c.x - chevron_w, c.y);
        const ImVec2 p2(c.x + chevron_w, c.y + chevron_h);
        dl->AddLine(p0, p1, col_fg, t);
        dl->AddLine(p2, p1, col_fg, t);
    }

    if (clicked)
    {
        *sidebar_visible = !*sidebar_visible;
        return true;
    }
    return false;
}

void ProjectBase::_populateOverlay()
{
    if (show_fullscreen_btn)
    {
        IVec2 ctx_size = main_window()->viewportSize();
        float btn_space = scale_size(6.0f);
        float fullscreen_btn_size = scale_size(56.0f);
        float sidebar_btn_size = scale_size(22.0f);

        // fullscreen button
        SDL_WindowFlags flags = SDL_GetWindowFlags(platform()->sdlWindow());
        bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN);
        static bool fullscreen_btn_held = false;
        if (DrawFullscreenOverlayButton(&fullscreen, &fullscreen_btn_held,
            ImVec2(ctx_size.x - fullscreen_btn_size - sidebar_btn_size - btn_space, btn_space), fullscreen_btn_size))
        {
            SDL_SetWindowFullscreen(platform()->sdlWindow(), fullscreen);
            main_window()->setSidebarVisible(!fullscreen);
        }

        // show/hide sidebar button
        bool sidebar_visible = main_window()->getSidebarVisible();
        if (DrawSidebarOverlayButton(&sidebar_visible, ImVec2(ctx_size.x - sidebar_btn_size, btn_space), sidebar_btn_size, fullscreen_btn_size))
        {
            main_window()->setSidebarVisible(sidebar_visible);
        }
    }

    for (SceneBase* scene : active_scenes)
        scene->_populateOverlay();
}

void ProjectBase::_initGUI()
{
    for (SceneBase* scene : active_scenes)
        scene->_initGUI();
}
void ProjectBase::_destroyGUI()
{
    for (SceneBase* scene : active_scenes)
        scene->_destroyGUI();
}

void ProjectBase::updateLiveBuffers()
{
    for (SceneBase* scene : active_scenes)
        scene->updateLiveBuffers();
}
void ProjectBase::updateShadowBuffers()
{
    for (SceneBase* scene : active_scenes)
        scene->updateShadowBuffers();
}
void ProjectBase::updateUnchangedShadowVars()
{
    for (SceneBase* scene : active_scenes)
        scene->updateUnchangedShadowVars();
}
bool ProjectBase::changedLive()
{
    for (SceneBase* scene : active_scenes) {
        if (scene->changedLive())
            return true;
    }
    return false;
}
bool ProjectBase::changedShadow()
{
    for (SceneBase* scene : active_scenes) {
        if (scene->changedShadow())
            return true;
    }
    return false;
}
void ProjectBase::markLiveValues()
{
    for (SceneBase* scene : active_scenes)
        scene->markLiveValues();
}
void ProjectBase::markShadowValues()
{
    for (SceneBase* scene : active_scenes)
        scene->markShadowValues();
}
void ProjectBase::invokeScheduledCalls()
{
    for (SceneBase* scene : active_scenes)
        scene->invokeScheduledCalls();
}

void ProjectBase::declareSceneCatalogue()
{
    assert(!started);

    discardStructuralMutations();
    viewports.clearImmediate(true);
    clearSceneRecords();

    declaring_scenes = true;
    try
    {
        projectPrepare(viewports);
        declaring_scenes = false;
    }
    catch (...)
    {
        declaring_scenes = false;
        viewports.clearImmediate(true);
        clearSceneRecords();
        throw;
    }
}

void ProjectBase::_projectPrepare()
{
    declareSceneCatalogue();
    try
    {
        materializeSelectedScenes();
    }
    catch (...)
    {
        viewports.clearImmediate(true);
        clearSceneRecords();
        throw;
    }
}
void ProjectBase::_projectStart()
{
    // Starting a project that's already been started/paused shouldn't occur
    assert(!paused);
    assert(!started);

    done_single_process = false;
    completedFrameCount.store(0, std::memory_order_relaxed);

    _projectPrepare();

    /// Determine real viewport size, and cache starting size
    /// for future "scale_adjust" calculations
    updateViewportRects();

    for (Viewport* viewport : viewports)
        viewport->setInitialSize();

    iterating_lifecycle = true;
    try
    {
        projectStart();

        //cache.init("cache.bin");

        // Start project scenes
        for (SceneBase* scene : active_scenes)
        {
            ///scene->cache = &cache;
            scene->mouse = &mouse;
            ///scene->dt_scene_ma_list.clear();
            ///scene->dt_project_ma_list.clear();
            ///scene->dt_project_draw_ma_list.clear();
            scene->clearCurrent();

            scene->_sceneStart();
        }

        for (std::unique_ptr<SceneRecord>& record : scene_records)
        {
            if (record->scene)
                record->lifecycle = SceneRecord::Lifecycle::Started;
        }

        main_window()->threadQueue().invokeBlocking([&]()
        {
            _initGUI();
            main_window()->threadQueue().drain();
        });

        for (Viewport* viewport : viewports)
            viewport->notifyMounted();
    }
    catch (...)
    {
        iterating_lifecycle = false;
        throw;
    }

    iterating_lifecycle = false;
    started = true;
    // ProjectWorker already excludes GUI access while processing project commands.
    commitStructuralMutations(true);
}
void ProjectBase::_projectResume()
{
    if (paused)
    {
        // If paused - just resume, don't restart
        paused = false;
        return;
    }
}
void ProjectBase::_projectStop()
{
    projectStop();
    completedFrameCount.store(0, std::memory_order_relaxed);
    ///cache.finalize();

    ///shaders_loaded = false;
    started = false;
    paused = false; // Ensure project can start properly next 'play' (so it doesn't try to simple 'resume')
}
void ProjectBase::_projectPause()
{
    paused = true;
}
void ProjectBase::_projectDestroy()
{
    if (!started)
        return;

    tearing_down = true;
    discardStructuralMutations();
    viewports.clearImmediate(true);

#ifdef BITLOOP_DEV_MODE
    if (!headless_mode)
#endif
    {
        main_window()->threadQueue().invokeBlocking([&]()
        {
            for (SceneBase* scene : active_scenes)
                scene->_destroyGUI();

            main_window()->threadQueue().drain();
        });
    }
#ifdef BITLOOP_DEV_MODE
    else
    {
        headless_thread_queue.drain();
    }
#endif

    for (SceneBase* scene : active_scenes)
        scene->_sceneDestroy();

    for (std::unique_ptr<SceneRecord>& record : scene_records)
    {
        if (record->scene)
            record->lifecycle = SceneRecord::Lifecycle::Destroyed;
    }

    projectDestroy();

    // Teardown callbacks may queue cleanup that still accesses scene-owned state.
    ThreadQueue& queue = renderThreadQueue();
    queue.invokeBlocking([&queue]() { queue.drain(); });

    started = false;
    paused = false;
    clearSceneRecords();
    discardStructuralMutations();
    viewports.clearImmediate(true);
    tearing_down = false;

#ifdef BITLOOP_DEV_MODE
    if (headless_mode)
        headless_thread_queue.drain();
#endif
}

bool ProjectBase::updateViewportRects()
{
    if (viewports.count() == 0)
        return false;

    DRect client_rect;
    DVec2 surface_size;

#ifdef BITLOOP_DEV_MODE
    if (headless_mode)
    {
        surface_size = static_cast<DVec2>(headless_surface_size);
        client_rect = DRect(0.0, 0.0, surface_size.x, surface_size.y);
    }
    else
#endif
    {
        assert(canvas != nullptr);
        client_rect = (DRect)canvas->clientRect();
        surface_size = canvas->fboSize();
    }

    const bool surface_resized = surface_size != old_surface_size;
    old_surface_size = surface_size;
    return viewports.resolveGeometry(surface_size, client_rect) || surface_resized;
}

#ifdef BITLOOP_DEV_MODE
void ProjectBase::startHeadless()
{
    assert(headless_mode);
    assert(!paused);
    assert(!started);

    done_single_process = false;
    completedFrameCount.store(0, std::memory_order_relaxed);

    _projectPrepare();
    updateViewportRects();

    for (Viewport* viewport : viewports)
        viewport->setInitialSize();

    iterating_lifecycle = true;
    try
    {
        projectStart();

        for (SceneBase* scene : active_scenes)
        {
            scene->mouse = &mouse;
            scene->clearCurrent();
            scene->_sceneStart();
        }

        for (std::unique_ptr<SceneRecord>& record : scene_records)
        {
            if (record->scene)
                record->lifecycle = SceneRecord::Lifecycle::Started;
        }

        for (Viewport* viewport : viewports)
            viewport->notifyMounted();
    }
    catch (...)
    {
        iterating_lifecycle = false;
        throw;
    }

    iterating_lifecycle = false;
    started = true;
    commitStructuralMutations();
}

void ProjectBase::queueHeadlessInputEvent(const EventData& event)
{
    headless_input_event_queue.push_back(event);
}

void ProjectBase::clearHeadlessInputEvents()
{
    headless_input_event_queue.clear();
}

void ProjectBase::_dispatchQueuedHeadlessInputEvents()
{
    if (headless_input_event_queue.empty())
        return;

    std::vector<EventData> events;
    events.swap(headless_input_event_queue);

    InputDispatchContext context;
    context.headless = true;
    context.viewport_hovered = true;
    context.surface_size = headless_surface_size;
    context.allow_fullscreen_hotkeys = false;

    for (const EventData& event : events)
        _dispatchInputEvent(event, context);
}

void ProjectBase::processHeadlessFrame(double dt)
{
    assert(headless_mode);
    if (!started || paused)
        return;

    commitStructuralMutations();
    frame_dt = dt * 1000.0;
    const bool viewport_rects_updated = updateViewportRects();
    _dispatchQueuedHeadlessInputEvents();

    project_timer.begin();
    invokeScheduledCalls();

    for (SceneBase* scene : active_scenes)
    {
        scene_timer.begin();
        scene->mouse = &mouse;
        scene->sceneProcess(dt);
        scene->dt_sceneProcess = scene_timer.elapsed();
    }

    for (Viewport* viewport : viewports)
    {
        viewport->scene->viewportProcess(viewport, dt);
        if (viewport_rects_updated || viewport->focused_dt > 0)
            viewport->scene->needs_redraw = true;
    }

    project_dt = project_timer.elapsed();
    completedFrameCount.fetch_add(1, std::memory_order_relaxed);
    done_single_process = true;

    _onEndFrame();
}

void ProjectBase::stopHeadless()
{
    assert(headless_mode);
    if (started)
        _projectDestroy();
    _projectStop();
}

void ProjectBase::setTraceFrameContext(std::int64_t frame, double time, double dt)
{
    trace_context_frame = frame;
    trace_context_time = time;
    trace_context_dt = dt;
}

void ProjectBase::requestTraceSnapshot(const SceneBase* scene, SimSnapshotRequest request)
{
    SimSnapshotRecord record;
    record.frame = trace_context_frame;
    record.time = trace_context_time;
    record.dt = trace_context_dt;
    record.project = projectDisplayName(*this);
    record.id = std::move(request.id);
    record.reason = std::move(request.reason);
    record.route = std::move(request.route);
    record.viewport_id = std::move(request.viewport_id);
    record.surface = headless_surface_size;
    record.pixel_rect = request.pixel_rect;
    record.world_rect = request.world_rect;
    record.attributes = std::move(request.attributes);

    if (scene)
    {
        record.scene = sceneDisplayName(*scene);
        record.scene_id = scene->id();
    }

    pending_trace_snapshots.push_back(std::move(record));
}

std::vector<SimSnapshotRecord> ProjectBase::takeTraceSnapshotRequests()
{
    std::vector<SimSnapshotRecord> out;
    out.swap(pending_trace_snapshots);
    return out;
}

bool ProjectBase::captureTraceSnapshot(SimSnapshotRecord& record, SimSnapshotImage& image) const
{
    auto scene = std::find_if(active_scenes.begin(), active_scenes.end(), [&](const SceneBase* candidate)
    {
        return candidate->id() == record.scene_id;
    });
    if (scene == active_scenes.end())
    {
        record.error = "Snapshot Scene '" + record.scene_id + "' is not active";
        return false;
    }

    record.scene = sceneDisplayName(**scene);
    if (!(*scene)->captureTraceSnapshot(record.route, image))
    {
        record.error = "Scene '" + record.scene_id + "' could not capture snapshot route '" + record.route + "'";
        return false;
    }

    // Reject malformed custom images before the encoder reads their pixel buffer.
    if (image.resolution.x <= 0 || image.resolution.y <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.resolution.x) * image.resolution.y * 4)
    {
        record.error = "Snapshot route '" + record.route + "' must supply positive dimensions and matching RGBA8 pixels";
        return false;
    }

    record.surface = image.resolution;
    return true;
}

Viewport* ProjectBase::resolveSnapshotViewport(
    std::string_view scene_id,
    std::string_view viewport_id,
    std::string& error) const
{
    auto scene_it = std::find_if(active_scenes.begin(), active_scenes.end(), [&](const SceneBase* scene)
    {
        return scene->id() == scene_id;
    });
    if (scene_it == active_scenes.end())
    {
        error = "Snapshot Scene '" + std::string(scene_id) + "' is not active";
        return nullptr;
    }

    const std::vector<Viewport*>& mounted = (*scene_it)->mountedToViewports();
    if (!viewport_id.empty())
    {
        auto viewport_it = std::find_if(mounted.begin(), mounted.end(), [&](const Viewport* viewport)
        {
            return viewport->id() == viewport_id;
        });
        if (viewport_it == mounted.end())
        {
            error = "Viewport '" + std::string(viewport_id) + "' was not found for Scene '" + std::string(scene_id) + "'";
            return nullptr;
        }
        return *viewport_it;
    }

    if (mounted.empty())
    {
        error = "Scene '" + std::string(scene_id) + "' has no Viewport; add a hidden Viewport before requesting an isolated snapshot";
        return nullptr;
    }
    if (mounted.size() > 1)
    {
        error = "Scene '" + std::string(scene_id) + "' has multiple Viewports; specify a Viewport ID";
        return nullptr;
    }

    return mounted.front();
}

bool ProjectBase::renderLayout(NanoCanvas& render_canvas, IVec2 resolution, std::string& error)
{
    assert(headless_mode);
    error.clear();
    if (!started)
    {
        error = "Project was not renderable";
        return false;
    }
    if (resolution.x <= 0 || resolution.y <= 0)
    {
        error = "Layout snapshot resolution must be positive";
        return false;
    }
    if ((render_canvas.fboWidth() != resolution.x || render_canvas.fboHeight() != resolution.y)
        && !render_canvas.resize(resolution.x, resolution.y))
    {
        error = "Could not allocate the Layout snapshot canvas";
        return false;
    }

    NanoCanvas* const previous_canvas = canvas;
    const IVec2 previous_surface_size = headless_surface_size;
    canvas = &render_canvas;
    headless_surface_size = resolution;
    updateViewportRects();

    try
    {
        render_canvas.begin(0.05f, 0.05f, 0.1f, 1.0f);
        _projectDraw();
        render_canvas.end();
    }
    catch (...)
    {
        headless_surface_size = previous_surface_size;
        updateViewportRects();
        canvas = previous_canvas;
        throw;
    }

    headless_surface_size = previous_surface_size;
    updateViewportRects();
    canvas = previous_canvas;
    return true;
}

bool ProjectBase::renderViewport(NanoCanvas& render_canvas, SimSnapshotRecord& record)
{
    assert(headless_mode);
    record.error.clear();
    if (!started)
    {
        record.error = "Project was not renderable";
        return false;
    }
    if (record.pixel_rect && record.world_rect)
    {
        record.error = "Snapshot requests cannot specify both pixel_rect and world_rect";
        return false;
    }

    Viewport* viewport = resolveSnapshotViewport(record.scene_id, record.viewport_id, record.error);
    if (!viewport)
        return false;

    const DVec2 viewport_size = viewport->size();
    const IVec2 resolution(
        static_cast<int>(std::lround(viewport_size.x)),
        static_cast<int>(std::lround(viewport_size.y)));
    if (resolution.x <= 0 || resolution.y <= 0)
    {
        record.error = "Viewport '" + viewport->id() + "' has a non-positive render size";
        return false;
    }
    if ((render_canvas.fboWidth() != resolution.x || render_canvas.fboHeight() != resolution.y)
        && !render_canvas.resize(resolution.x, resolution.y))
    {
        record.error = "Could not allocate the Viewport snapshot canvas";
        return false;
    }

    record.viewport_id = viewport->id();
    record.surface = resolution;
    ViewportGeometryScope geometry(*viewport, resolution);
    NanoCanvas* const previous_canvas = canvas;
    canvas = &render_canvas;

    try
    {
        render_canvas.begin(0.05f, 0.05f, 0.1f, 1.0f);
        render_canvas.setFillStyle(10, 10, 15);
        render_canvas.fillRect(0, 0, resolution.x, resolution.y);
        render_canvas.setFillStyle(255, 255, 255);
        render_canvas.setStrokeStyle(255, 255, 255);
        render_canvas.setFontSize(16.0f);
        drawViewport(*viewport, render_canvas);
        render_canvas.end();

        if (record.world_rect)
        {
            const DQuad stage_quad = viewport->worldStageTransform().toStageQuad(DQuad(*record.world_rect));
            if (!stage_quad.a.isFinite() || !stage_quad.b.isFinite() || !stage_quad.c.isFinite() || !stage_quad.d.isFinite())
            {
                record.error = "Snapshot world_rect transformed to non-finite pixel coordinates";
                canvas = previous_canvas;
                return false;
            }

            const DRect bounds = stage_quad.boundingRect();
            record.pixel_rect = IRect(
                static_cast<int>(std::floor(bounds.x1)),
                static_cast<int>(std::floor(bounds.y1)),
                static_cast<int>(std::ceil(bounds.x2)),
                static_cast<int>(std::ceil(bounds.y2)));
        }
    }
    catch (...)
    {
        canvas = previous_canvas;
        throw;
    }

    canvas = previous_canvas;
    return true;
}

void ProjectBase::onHeadlessEncodeFrame(EncodeFrame& data, const CapturePreset& preset)
{
    assert(headless_mode);
    _onEncodeFrame(data, 0, preset);
}

bool ProjectBase::saveHeadlessCheckpoint(
    const std::filesystem::path& checkpoint_dir,
    std::int64_t frame,
    double time,
    double dt) const
{
    assert(headless_mode);

    std::filesystem::create_directories(checkpoint_dir);
    JSON::json metadata = {
        {"schema_version", 3},
        {"project", projectDisplayName(*this)},
        {"frame", frame},
        {"time", time},
        {"dt", dt},
        {"scene_count", active_scenes.size()},
        {"selected_scene_ids", selectedSceneIds()},
        {"scene_config_hash", headless_scene_config_hash}
    };

    SimCheckpointWriter project_writer(checkpoint_dir / "project");
    const bool project_saved = saveCheckpoint(project_writer);
    project_writer.finalize({
        {"kind", "project"},
        {"saved", project_saved}
    });
    metadata["project_saved"] = project_saved;

    JSON::json scenes = JSON::json::array();
    bool any_scene_saved = false;
    for (const SceneBase* scene : active_scenes)
    {
        const std::string scene_dir = "scene_" + scene->id();
        SimCheckpointWriter writer(checkpoint_dir / scene_dir);
        const bool saved = scene->saveCheckpoint(writer);
        any_scene_saved = any_scene_saved || saved;
        writer.finalize({
            {"kind", "scene"},
            {"saved", saved},
            {"scene", sceneDisplayName(*scene)},
            {"scene_id", scene->id()}
        });

        scenes.push_back({
            {"scene", sceneDisplayName(*scene)},
            {"scene_id", scene->id()},
            {"dir", scene_dir},
            {"saved", saved}
        });
    }

    metadata["scenes"] = std::move(scenes);
    writeJsonFile(checkpoint_dir / "metadata.json", metadata);
    return project_saved || any_scene_saved;
}

bool ProjectBase::loadHeadlessCheckpoint(const std::filesystem::path& checkpoint_dir)
{
    assert(headless_mode);

    const std::filesystem::path metadata_path = checkpoint_dir / "metadata.json";
    std::ifstream metadata_in(metadata_path, std::ios::binary);
    if (!metadata_in)
        throw std::runtime_error("Could not read checkpoint metadata: " + metadata_path.string());
    const JSON::json metadata = JSON::json::parse(metadata_in);

    if (!metadata.contains("selected_scene_ids") || !metadata["selected_scene_ids"].is_array())
        throw std::runtime_error("Checkpoint does not contain a stable selected_scene_ids set");
    const std::vector<std::string> checkpoint_scene_ids = metadata["selected_scene_ids"].get<std::vector<std::string>>();
    const std::vector<std::string> active_scene_ids = selectedSceneIds();
    if (checkpoint_scene_ids != active_scene_ids)
    {
        throw std::runtime_error(
            "Checkpoint Scene selection mismatch: active=" + JSON::json(active_scene_ids).dump() +
            ", checkpoint=" + JSON::json(checkpoint_scene_ids).dump());
    }

    if (!metadata.contains("scene_config_hash") || !metadata["scene_config_hash"].is_string())
        throw std::runtime_error("Checkpoint does not contain a stable scene_config_hash");
    const std::string checkpoint_config_hash = metadata["scene_config_hash"].get<std::string>();
    if (checkpoint_config_hash != headless_scene_config_hash)
    {
        throw std::runtime_error(
            "Checkpoint Scene config mismatch: active=" + headless_scene_config_hash +
            ", checkpoint=" + checkpoint_config_hash);
    }

    bool any_loaded = false;
    if (std::filesystem::exists(checkpoint_dir / "project" / "manifest.json"))
    {
        SimCheckpointReader reader(checkpoint_dir / "project");
        any_loaded = loadCheckpoint(reader) || any_loaded;
    }

    if (metadata.contains("scenes"))
    {
        for (const JSON::json& scene_meta : metadata["scenes"])
        {
            if (!scene_meta.contains("scene_id") || !scene_meta.contains("dir"))
                continue;

            const std::string scene_id = scene_meta["scene_id"].get<std::string>();
            SceneBase* target = nullptr;
            for (SceneBase* scene : active_scenes)
            {
                if (scene->id() == scene_id)
                {
                    target = scene;
                    break;
                }
            }

            if (!target)
                continue;

            const std::filesystem::path scene_dir = checkpoint_dir / scene_meta["dir"].get<std::string>();
            if (!std::filesystem::exists(scene_dir / "manifest.json"))
                continue;

            SimCheckpointReader reader(scene_dir);
            any_loaded = target->loadCheckpoint(reader) || any_loaded;
        }
    }

    return any_loaded;
}

std::vector<SimTraceFrame> ProjectBase::collectTraceFrames(
    std::int64_t frame,
    double time,
    double dt,
    std::size_t max_objects) const
{
    std::vector<SimTraceFrame> frames;
    frames.reserve(active_scenes.size());

    const std::string project_name = projectDisplayName(*this);

    for (const SceneBase* scene : active_scenes)
    {
        SimTraceFrame trace(max_objects);
        trace.frame = frame;
        trace.time = time;
        trace.dt = dt;
        trace.project = project_name;
        trace.scene = scene->id();

        scene->traceFrame(trace);
        frames.push_back(std::move(trace));
    }

    return frames;
}
#endif

void ProjectBase::_projectProcess()
{
    commitStructuralMutations();

    frame_dt = frame_timer.tick();

    bool canvas_dirty = false;
    bool viewport_rects_updated = updateViewportRects();

    // 'did_first_process' is only true for the FIRST frame we process (when unpaused) so that we
    // don't do any drawing until this sets:  done_single_process=true
    // (this is to prevent recording the anything until the first real frame completes after starting record)
    bool did_first_process = false;

    Viewport* coordinate_viewport = nullptr;
    for (const FingerInfo& finger : pressed_fingers)
    {
        if (finger.ctx_owner)
        {
            coordinate_viewport = finger.ctx_owner;
            break;
        }
    }
    if (!coordinate_viewport)
        coordinate_viewport = hitTestViewport(DVec2(mouse.client_x, mouse.client_y));

    mouse.viewport = coordinate_viewport;
    if (coordinate_viewport)
    {
        const double viewport_x = coordinate_viewport->toSurfaceX(mouse.client_x);
        const double viewport_y = coordinate_viewport->toSurfaceY(mouse.client_y);

        mouse.stage_x = viewport_x;
        mouse.stage_y = viewport_y;
    }

    if (!paused)
    {
        project_timer.begin();

        double dt;
        if (capture_context && capture_context->manager().isRecording())
            dt = 1.0 / capture_context->manager().fps();
        else
            dt = frame_dt / 1000.0;

        // Process each scene
        for (SceneBase* scene : active_scenes)
        {
            scene_timer.begin();

            scene->mouse = &mouse;
            scene->sceneProcess(dt);

            scene->dt_sceneProcess = scene_timer.elapsed();
        }


        // Allow project to handle process on each Viewport
        for (Viewport* viewport : viewports)
        {
            viewport->scene->viewportProcess(viewport, dt);
        }

        /// --- Post-Process each scene ---

        // Measure time taken to process whole project
        project_dt = project_timer.elapsed();

        completedFrameCount.fetch_add(1, std::memory_order_relaxed);

        // Prepare to encode the next frame
        if (!done_single_process)
            did_first_process = true;
    }

    // Paused edits and restart requests still need a fresh view of the committed state.
    for (Viewport* viewport : viewports)
    {
        if (viewport_rects_updated || viewport->focused_dt > 0)
            viewport->scene->needs_redraw = true;
        if (viewport->scene->needs_redraw)
            canvas_dirty = true;
    }

    if (did_first_process)
        done_single_process = true;

    if (canvas_dirty)
        canvas->setDirty();
}
void ProjectBase::_projectDraw()
{
    // note: called from main GUI thread

    if (!done_single_process) return;
    if (!started) return;

    DVec2 surface_size = canvas->fboSize();

    /// todo: No per-viewport redrawing for now. Would require:
    /// - or give each viewport a separate Canvas (ideal)
    ///   - let ImGui draw viewport splitters
    ///   - blit each viewport (when dirty) to merged "project_image" so you can record all in one frame
    ///   - optional [hard]: add Scene-specific logic to capture only that scene.
    ///     - queue captures, see if FFmpeg can handle multiple recordings at once (multiple capture managers?)
    /// 
    /// - additional surfaces to target (switch same nanovg instance)
    
    // Draw background
    canvas->setFillStyle(10, 10, 15);
    canvas->fillRect(0, 0, surface_size.x, surface_size.y);

    canvas->setFillStyle(255, 255, 255);
    canvas->setStrokeStyle(255, 255, 255);

    canvas->setFontSize(16.0f);

    for (Viewport* viewport : viewports.orderedViewports())
    {
        if (viewport->isVisible())
            drawViewport(*viewport, *canvas);
    }

    // Draw focus decoration.
    for (Viewport* viewport : viewports.orderedViewports())
    {
        if (!viewport->isVisible())
            continue;

        if (ctx_focused == viewport)
        {
            int flash_lines_extra = 0;
            float focus_flash_brightness = viewport->focused_dt / Viewport::focus_flash_frames;
            flash_lines_extra = (int)(focus_flash_brightness * 155.0f);

            if (viewport->focused_dt > 0)
            {
                viewport->focused_dt -= 1.0f;

                int flash_fill_extra = (int)(focus_flash_brightness * 50.0f);
                canvas->setFillStyle(255, 255, 255, flash_fill_extra);
                canvas->fillRect(viewport->left() - 0.5, viewport->top() - 0.5, viewport->width() + 1, viewport->height() + 1);
            }

            canvas->setLineWidth(1);
            canvas->setStrokeStyle(75 + flash_lines_extra, 75, 100 + flash_lines_extra);
            canvas->strokeRect(viewport->left() - 0.5, viewport->top() - 0.5, viewport->width() + 1, viewport->height() + 1);
        }
    }

    for (Viewport* viewport : viewports)
        viewport->setOldSize();
}

void ProjectBase::drawViewport(Viewport& viewport, NanoCanvas& target_canvas)
{
    viewport.setTargetPainterContext(target_canvas.getPainterContext());
    viewport.resetTransform();

    target_canvas.setClipRect(viewport.left(), viewport.top(), viewport.width(), viewport.height());
    target_canvas.save();
    target_canvas.translate(std::floor(viewport.left()), std::floor(viewport.top()));

    viewport.worldMode();
    viewport.draw();

    target_canvas.restore();
    target_canvas.resetClipping();
}
void ProjectBase::_onEndFrame()
{
    for (SceneBase* scene : active_scenes)
        scene->onEndFrame();

    // Update 'live' values of all ChangeTracker variables (to compare against next frame)
    for (SceneBase* scene : active_scenes)
    {
        scene->needs_redraw = false;
        scene->ChangeTracker::updateCurrent();
    }

    mouse.clearStaleButtonStates();
    commitStructuralMutations();
}

void ProjectBase::_clearEventQueue()
{
}
void ProjectBase::_onEvent(const EventData& e)
{
    InputDispatchContext context;
    context.headless = false;
    context.viewport_hovered = main_window()->viewportHovered();
    context.surface_size = IVec2(platform()->fboWidth(), platform()->fboHeight());
    context.allow_fullscreen_hotkeys = true;

    _dispatchInputEvent(e, context);
}

void ProjectBase::_dispatchInputEvent(const EventData& e, const InputDispatchContext& context)
{
    // If viewport not hovered (e.g. Mouse over a popup window),
    // ignore mouse/finger down events to avoid stealing focus
    switch (e.type)
    {
    case EventType::MOUSE_BUTTON_DOWN:
    case EventType::FINGER_DOWN:
    {
        if (!context.viewport_hovered)
            return;
    } break;
    }

    /// TODO: Is "hovered" viewport really necessary? Stick with focused viewport?

    // Assign routing metadata to this event
    Event event(e);
    event.setPointerSurfaceSize(context.surface_size);

    const bool mobile_input = !context.headless && platform()->isMobile();

    //TouchInput s;

    // Track "cursor" position
    switch (e.type)
    {
        // Mouse
        case EventType::MOUSE_BUTTON_DOWN:
        {
            if (mobile_input) return;
            const auto& b = e.button;

            mouse.buttonState((MouseButton)b.button).is_down = true;
            mouse.buttonState((MouseButton)b.button).clicked = true;

            mouse.client_x = e.button.x;
            mouse.client_y = e.button.y;
        } break;
        case EventType::MOUSE_BUTTON_UP:
        {
            if (mobile_input) return;
            const auto& b = e.button;

            mouse.buttonState((MouseButton)b.button).is_down = false;
            mouse.client_x = b.x;
            mouse.client_y = b.y;
        } break;
        case EventType::MOUSE_MOTION:
        {
            if (mobile_input) return;
            const auto& motion = e.motion;

            mouse.client_x = motion.x;
            mouse.client_y = motion.y;
        } break;

        // Touch
        case EventType::FINGER_DOWN:
        case EventType::FINGER_UP:
        case EventType::FINGER_MOTION:
        {
            const int surface_width = context.surface_size.x > 0 ? context.surface_size.x : platform()->fboWidth();
            const int surface_height = context.surface_size.y > 0 ? context.surface_size.y : platform()->fboHeight();
            mouse.client_x = (double)(e.tfinger.x * surface_width);
            mouse.client_y = (double)(e.tfinger.y * surface_height);
        } break;
    }

    const DVec2 client_position(mouse.client_x, mouse.client_y);
    bool captured = false;
    switch (e.type)
    {
        case EventType::MOUSE_BUTTON_DOWN:
        case EventType::FINGER_DOWN:
        {
            Viewport* viewport = hitTestViewport(client_position);
            if (viewport)
            {
                if (viewport != ctx_focused)
                {
                    ctx_focused = viewport;
                    viewport->focused_dt = Viewport::focus_flash_frames;
                }
                ctx_hovered = viewport;
                captured = true;
            }
        } break;
        case EventType::MOUSE_MOTION:
        case EventType::FINGER_MOTION:
        {
            ctx_hovered = hitTestViewport(client_position);
            captured = ctx_hovered != nullptr;
        } break;
    }

    // Don't process mouse-down/finger-down event if we don't click on a viewport
    if (event.isPressEvent() && !captured)
        return;

    if (!ctx_focused && event.isPointerEvent())
        return; 

    // Update pressed_fingers
    {
        // Does this finger already have an "owner" viewport?
        // If so, loop over fingers and restore it for 
        Viewport* ctx_owner = nullptr;

        // Support both single-finger pan & 2 finger transform
        switch (e.type)
        {
            // ----- MOBILE -----
            case EventType::FINGER_DOWN:
            {
                ctx_owner = ctx_focused;

                // Remember touched-down ctx for touch release/motion event to use same owner
                event.setOwnerViewport(ctx_owner);
                PointerEvent pointer_event(event);

                // Add pressed finger
                FingerInfo info;
                info.ctx_owner = ctx_focused;
                info.fingerId = e.tfinger.fingerID;
                info.x = pointer_event.x();
                info.y = pointer_event.y();
                pressed_fingers.push_back(info);

            } break;
            case EventType::FINGER_MOTION:
            {
                for (size_t i = 0; i < pressed_fingers.size(); i++)
                {
                    FingerInfo& info = pressed_fingers[i];
                    if (info.fingerId == e.tfinger.fingerID)
                    {
                        ctx_owner = info.ctx_owner;
                        break;
                    }
                }

                event.setOwnerViewport(ctx_owner);
            } break;
            case EventType::FINGER_UP:
            {
                for (size_t i = 0; i < pressed_fingers.size(); i++)
                {
                    FingerInfo& info = pressed_fingers[i];
                    if (info.fingerId == e.tfinger.fingerID)
                    {
                        ctx_owner = info.ctx_owner;
                        break;
                    }
                }

                // Erase lifted finger
                for (size_t i = 0; i < pressed_fingers.size(); i++)
                {
                    FingerInfo& info = pressed_fingers[i];
                    if (info.fingerId == e.tfinger.fingerID)
                    {
                        pressed_fingers.erase(pressed_fingers.begin() + i);
                        break;
                    }
                }

                event.setOwnerViewport(ctx_owner);
            } break;

            // ----- DESKTOP -----
            case EventType::MOUSE_BUTTON_DOWN:
            {
                ctx_owner = ctx_focused;

                // Remember touched-down ctx for touch release/motion event to use same owner
                event.setOwnerViewport(ctx_owner);
                PointerEvent pointer_event(event);

                // Add pressed finger
                FingerInfo info;
                info.ctx_owner = ctx_focused;
                info.fingerId = 0;
                info.x = pointer_event.x();
                info.y = pointer_event.y();
                pressed_fingers.push_back(info);

            } break;
            case EventType::MOUSE_BUTTON_UP:
            {
                for (size_t i = 0; i < pressed_fingers.size(); i++)
                {
                    FingerInfo& info = pressed_fingers[i];
                    if (info.fingerId == 0)
                    {
                        ctx_owner = info.ctx_owner;
                        break;
                    }
                }

                pressed_fingers.clear();
                event.setOwnerViewport(ctx_owner);
            } break;
            case EventType::MOUSE_MOTION:
            {
                for (size_t i = 0; i < pressed_fingers.size(); i++)
                {
                    FingerInfo& info = pressed_fingers[i];
                    if (info.fingerId == 0)
                    {
                        ctx_owner = info.ctx_owner;
                        break;
                    }
                }

                event.setOwnerViewport(ctx_owner);
            } break;
            case EventType::MOUSE_WHEEL:
            {
                event.setOwnerViewport(ctx_hovered);
            } break;

            case EventType::KEY_DOWN:
            case EventType::KEY_UP:
            {
                ctx_owner = ctx_focused;
                event.setOwnerViewport(ctx_owner);
            }
            break;
        }
    }

    if (mobile_input)
    {
        // simulate left-mouse button on mobile
        bool any_pressed = pressed_fingers.size() > 0;
        mouse.buttonState(MouseButton::LEFT).is_down = any_pressed;
        mouse.buttonState(MouseButton::LEFT).clicked = any_fingers_pressed && !any_pressed;

        // store old state
        any_fingers_pressed = any_pressed;
    }
    
    event.setFocusedViewport(ctx_focused);
    event.setHoveredViewport(ctx_hovered);

    //if (event.ctx_owner())
    //{
    //    event.ctx_owner()->scene->input.addEvent(event, pressed_fingers);
    //}

    // enter/exit fullscreen hotkeys (F11 / Escape)
    if (context.allow_fullscreen_hotkeys && event.type() == EventType::KEY_DOWN)
    {
        if (event.data().key.key == SDLK_F11)
        {
            SDL_WindowFlags flags = SDL_GetWindowFlags(platform()->sdlWindow());
            bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN);

            SDL_SetWindowFullscreen(platform()->sdlWindow(), !fullscreen);
        }
        else if (event.data().key.key == SDLK_ESCAPE)
        {
            SDL_SetWindowFullscreen(platform()->sdlWindow(), false);
        }
    }

    onEvent(event);

    for (SceneBase* scene : active_scenes)
    {
        // Keep specialized handlers onMouseDown/onKeyDown(etc) pre-filtered, 
        // but keep onEvent generic so users can retain full control.
        scene->_onEvent(event);
    }
}

bool ProjectBase::captureSnapshot(SnapshotFrame& output) const
{
    if (active_scenes.size() != 1 || !active_scenes.front()->captureSnapshot(output))
        return false;
    // Custom dimensions must describe the supplied buffer before the encoder can use it.
    if (output.resolution.x <= 0 || output.resolution.y <= 0 ||
        output.frame.size() != size_t(output.resolution.x) * output.resolution.y * 4)
        throw std::runtime_error("Custom snapshot dimensions must match its RGBA8 pixels");
    return true;
}

void ProjectBase::_onEncodeFrame(EncodeFrame& data, int request_id, const CapturePreset& preset)
{
    for (SceneBase* scene : active_scenes)
        scene->_onEncodeFrame(data, request_id, preset);
}


void ProjectBase::logMessage(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    project_log->vlog(fmt, ap);
    va_end(ap);
}
void ProjectBase::logClear()
{
    project_log->clear();
}

BL_END_NS
