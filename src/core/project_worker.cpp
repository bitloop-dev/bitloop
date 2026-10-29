#include <bitloop/core/project_worker.h>
#include <bitloop/core/main_window.h>
#include <bitloop/core/project.h>
#include <bitloop/core/capture_manager.h>

BL_BEGIN_NS

ProjectWorker* ProjectWorker::singleton = nullptr;

void ProjectWorker::startWorker()
{
    worker_thread = std::thread(&ProjectWorker::worker_loop, singleton);
    shared_sync.project_thread_started = true;
}

ProjectWorker::~ProjectWorker()
{
    if (shared_sync.project_thread_started)
    {
        // Owner-thread teardown can wait for deferred GL cleanup on the GUI thread.
        while (!worker_finished.load(std::memory_order_acquire))
        {
            main_window()->threadQueue().pump();
            std::this_thread::yield();
        }
        worker_thread.join();
    }
}


void ProjectWorker::_destroyActiveProject()
{
    BL_TAKE_OWNERSHIP("live");

    if (current_project)
    {
        current_project->_projectDestroy();

        // Finish project UI cleanup and remaining deferred deletions before deleting the project.
        // current_project->ui==nullptr after this
        main_window()->threadQueue().invokeBlocking([&]()
        {
            current_project->_destroyGUI();
            main_window()->threadQueue().drain();
        });

        delete current_project;
        current_project = nullptr;
    }
}


void ProjectWorker::handleProjectCommands(ProjectCommandEvent& e)
{
    BL_TAKE_OWNERSHIP("live");

    switch (e.type)
    {
    case ProjectCommandType::PROJECT_SET:
    {
        project_log.clear();

        // delete the actual project, then create new
        _destroyActiveProject();

        current_project = ProjectBase::findProjectInfo(e.project_uid)->creator();
        current_project->configure(
            e.project_uid,
            main_window()->getCanvas(),
            &project_log,
            capture_context);

        main_window()->queueMainWindowCommand({
            MainWindowCommandType::ON_SELECT_PROJECT,
            current_project->getProjectInfo()->name });
    }
    break;

    case ProjectCommandType::PROJECT_PLAY:
        if (current_project)
        {
            if (current_project->isPaused())
            {
                // don't destroy if simply resuming project
                current_project->_projectResume();
            }
            else
            {
                current_project->_projectDestroy();

                // todo: delete here to make inline class variable delcarations the same each restart?
                
                current_project->_projectStart();
            }

            current_project->updateShadowBuffers();

            main_window()->queueMainWindowCommand({ MainWindowCommandType::ON_PLAY_PROJECT });
        }
        break;

    case ProjectCommandType::PROJECT_STOP:
        if (current_project)
        {
            current_project->_projectDestroy();
            current_project->_projectStop();

            main_window()->queueMainWindowCommand({ MainWindowCommandType::ON_STOPPED_PROJECT });
        }
        break;

    case ProjectCommandType::PROJECT_PAUSE:
        if (current_project)
        {
            current_project->_projectPause();
            main_window()->queueMainWindowCommand({ MainWindowCommandType::ON_PAUSED_PROJECT });
        }
        break;
    }
}

void ProjectWorker::worker_loop()
{
    //bool shadow_changed = false;

    bool immediate_update_requested = false;
    auto last_frame_time = std::chrono::steady_clock::now();

    while (!shared_sync.quitting.load())
    {
        /// ────── Safe place to control project changes ──────
        // We don't call populateAttributes() if holding shadow_buffer_mutex,
        // meaning we won't loop over scenes here while processing project commands
        {
            std::vector<ProjectCommandEvent> commands;
            {
                std::lock_guard<std::mutex> lock(command_mutex);
                commands = std::move(project_command_queue);
            }

            if (!commands.empty())
            {
                std::unique_lock<std::mutex> shadow_lock(shared_sync.shadow_buffer_mutex);
                for (auto& e : commands) handleProjectCommands(e);
            }
        }

        // Wait for GUI to consume the freshly-rendered previous frame
        shared_sync.wait_until_gui_consumes_frame();

        /// ────── Do heavy work (while GUI thread redraws cached frame) ──────
        if (current_project && current_project->started) 
        {
            /// ────── Update live values ──────
            {
                //blPrint() << "──────────────────────────────";
                //blPrint() << "────── NEW WORKER FRAME ──────";

                /// ────── Event polling ──────
                {
                    current_project->markLiveValues();

                    pollEvents();

                    // If handled SDL event changed live buffer, immediately push those changes to shadow buffer
                    if (current_project->changedLive())
                        pushDataToShadow();
                }
                
                /// ────── Update Live Buffer ──────
                pullDataFromShadow();

                /// ────── Mark buffer states prior to process ──────
                {
                    std::unique_lock<std::mutex> shadow_lock(shared_sync.shadow_buffer_mutex);
                    current_project->markLiveValues();
                    current_project->markShadowValues();
                }

                // Capture by default unless sim overrides flag (only has an affect when recording)
                if (!current_project->paused || capture_context->isSnapshotting())
                    capture_context->captureFrame(true);

                current_project->invokeScheduledCalls();

                /// ────── Process simulation (potentially heavy work) ──────
                current_project->_projectProcess();

                #ifndef BL_WEB_BUILD // does nothing on web
                if (current_project->immediate_update_requested)
                {
                    immediate_update_requested = true;
                    current_project->immediate_update_requested = false;
                }
                #endif

                /// ────── Update shadow buffer with *changed* live variables ──────
                {
                    // we push live changes to shadow IF shadow itself wasn't changed by UI
                    pushDataToUnchangedShadowVars();
                }


                //blPrint() << "────── END WORKER FRAME ──────";
                //blPrint() << "──────────────────────────────";
                //blPrint() << "";
            }
        }

        prepareSnapshots();

        // If recording, don't wake GUI until capture_manager is ready to encode a new frame,
        // otherwise the main GUI thread would need to block while it waits for the last frame to encode.
        // We intentionally avoid a frame queue so video encoding stays in sync with simulation.
        if (capture_context->capturingNextFrame())
        {
            CaptureManager& capture_manager = capture_context->manager();
            if ((capture_manager.isRecording() || capture_manager.isSnapshotting()))
                capture_manager.waitUntilReadyForNewFrame();
        }

        // delay to achieve target fps (minus time taken to draw the frame)
        if (!immediate_update_requested)
        {
            auto dt = std::chrono::steady_clock::now() - last_frame_time;
            uint64_t target_ns = 1000000000 / capture_context->fps();
            uint64_t frame_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count());
            uint64_t delay_ns = std::max((uint64_t)0, target_ns - frame_ns);
            if (delay_ns > 0) SDL_DelayPrecise(delay_ns);
        }
        else
        {
            shared_sync.setImmediateUpdate(true);
            immediate_update_requested = false;
        }

        last_frame_time = std::chrono::steady_clock::now();

        /// ────── Flag ready to draw ──────
        shared_sync.flag_ready_to_draw();

        /// ────── Wake GUI ──────
        shared_sync.cv.notify_one();

        /// ────── Wait for GUI to draw the freshly prepared data ──────
        shared_sync.wait_until_gui_consumes_frame();

        if (current_project)
            current_project->_onEndFrame();
    }

    {
        // The GUI may still be finishing its last frame while pumping cleanup.
        // Exclude UI population throughout teardown, as project commands do.
        std::lock_guard<std::mutex> shadow_lock(shared_sync.shadow_buffer_mutex);
        _destroyActiveProject();
    }
    worker_finished.store(true, std::memory_order_release);
}

void ProjectWorker::pushDataToShadow()
{
    // While we update the shadow buffer, we should NOT 
    // permit ui updates (force a small stall)
    std::unique_lock<std::mutex> shadow_lock(shared_sync.shadow_buffer_mutex);
    current_project->updateShadowBuffers();
}

void ProjectWorker::pushDataToUnchangedShadowVars()
{
    std::unique_lock<std::mutex> shadow_lock(shared_sync.shadow_buffer_mutex);
    current_project->updateUnchangedShadowVars();
}

void ProjectWorker::pullDataFromShadow()
{
    //if (shared_sync.editing_ui.load())
    {
        /// Make sure we can't alter the shadow buffer while we copy them to live buffer
        std::lock_guard<std::mutex> live_buffer_lock(shared_sync.live_buffer_mutex);
        shared_sync.updating_live_buffer.store(true);

        current_project->updateLiveBuffers();

        shared_sync.updating_live_buffer.store(false, std::memory_order_release);

        // Inform GUI thread that 'updating_live_buffer' changed
        shared_sync.cv_updating_live_buffer.notify_one();
    }
}

void ProjectWorker::queueEvent(EventData event)
{
    // Queue events which should only be processed once the current worker frame completes
    {
        std::lock_guard<std::mutex> lock(event_queue_mutex);
        input_event_queue.push_back(std::move(event));
    }

    // Immediately process events for the overlay
}

void ProjectWorker::pollEvents()
{
    // Grab event queue data (and clear via swap with empty queue)
    std::vector<EventData> local;
    {
        std::lock_guard<std::mutex> lock(event_queue_mutex);
        local.swap(input_event_queue);
    }

    for (const EventData& e : local)
        _onEvent(e);
}

bool ProjectWorker::hasCurrentProject()
{
    return (current_project != nullptr);
}

bool ProjectWorker::hasRunningProject()
{
    return (current_project != nullptr && !current_project->isPaused());
}

void ProjectWorker::populateAttributes()
{
    if (current_project)
        current_project->_populateAllAttributes();
}

void ProjectWorker::populateOverlay()
{
    if (current_project)
        current_project->_populateOverlay();
}

void ProjectWorker::onEncodeFrame(EncodeFrame& data, int request_id, const CapturePreset& preset)
{
    if (current_project)
        current_project->_onEncodeFrame(data, request_id, preset);
}

void ProjectWorker::requestSnapshot(SnapshotCallback callback)
{
    std::lock_guard lock(command_mutex);
    snapshot_requests.push_back(std::move(callback));
}

void ProjectWorker::prepareSnapshots()
{
    std::vector<SnapshotCallback> requests;
    {
        std::lock_guard lock(command_mutex);
        requests.swap(snapshot_requests);
    }
    for (auto& callback : requests)
    {
        std::optional<SnapshotFrame> snapshot;
        std::string error;
        try
        {
            if (!current_project || !current_project->started)
                throw std::runtime_error("The project stopped before its snapshot was captured");
            SnapshotFrame output;
            if (current_project->captureSnapshot(output)) snapshot = std::move(output);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
        main_window()->threadQueue().post(
            [callback = std::move(callback), snapshot = std::move(snapshot), error = std::move(error)]() mutable
            {
                callback(std::move(snapshot), std::move(error));
            });
    }
}

void ProjectWorker::_onEvent(const EventData& e)
{
    BL_TAKE_OWNERSHIP("live");

    if (current_project)
        current_project->_onEvent(e);
}

void ProjectWorker::draw()
{
    BL_TAKE_OWNERSHIP("live");

    if (current_project)
        current_project->_projectDraw();
}

BL_END_NS
