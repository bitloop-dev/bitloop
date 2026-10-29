#include <catch2/catch_test_macros.hpp>

#include <bitloop/core/main_window.h>
#include <bitloop/core/project_worker.h>
#include <bitloop/core/scene.h>

#include <chrono>
#include <semaphore>
#include <thread>

using namespace bl;

namespace
{
    struct ShutdownState
    {
        std::thread::id gui_thread = std::this_thread::get_id();
        std::binary_semaphore entered_destroy{0};
        std::binary_semaphore finish_destroy{0};
        bool ui_destroyed = false;
        bool ui_destroyed_on_gui = false;
        bool scene_destroyed_on_worker = false;
        bool project_deleted = false;
        int overlays = 0;
        int deferred_cleanup = 0;
    };

    struct ShutdownScene : Scene<ShutdownScene>
    {
        struct UI;
        struct Config { ShutdownState* state; };
        ShutdownState& state;

        explicit ShutdownScene(Config& config) : state(*config.state) {}

        void sceneDestroy() override
        {
            state.scene_destroyed_on_worker = std::this_thread::get_id() != state.gui_thread;
            state.entered_destroy.release();
            state.finish_destroy.acquire();
            renderThreadQueue().post([this]()
            {
                state.deferred_cleanup++;
                renderThreadQueue().post([this]() { state.deferred_cleanup++; });
            });
        }

        void viewportDraw(Viewport*) const override {}
    };

    struct ShutdownScene::UI : ShutdownScene::BufferedInterfaceModel
    {
        ShutdownState& state;

        explicit UI(const ShutdownScene* scene)
            : ShutdownScene::BufferedInterfaceModel(scene), state(scene->state) {}

        void overlay() override { state.overlays++; }

        void destroy() override
        {
            state.ui_destroyed = true;
            state.ui_destroyed_on_gui = std::this_thread::get_id() == state.gui_thread;
        }
    };

    struct ShutdownProject : BasicProject
    {
        ShutdownState& state;

        explicit ShutdownProject(ShutdownState& state) : state(state)
        {
            setFullscreenButtonVisibile(false);
        }

        ~ShutdownProject() override { state.project_deleted = true; }

        void projectPrepare(Layout&) override
        {
            create<ShutdownScene>(ShutdownScene::Config{&state});
        }
    };

    struct GuiWorker : ProjectWorker
    {
        using ProjectWorker::ProjectWorker;
        using ProjectWorker::populateOverlay;
    };
}

TEST_CASE("Worker exit excludes UI population throughout project teardown", "[project_worker][shutdown]")
{
    using namespace std::chrono_literals;

    ShutdownState state;
    auto info = std::make_shared<ProjectInfo>(std::vector<std::string>{"ShutdownTest"}, "ShutdownTest",
        [&]() -> ProjectBase* { return new ShutdownProject(state); }, -123456);
    info->dev_root = nullptr;
    auto& factories = ProjectBase::projectInfoList();
    factories.push_back(info);

    SharedSync sync;
    CaptureContext capture;
    MainWindow window(sync, capture);
    bool reached_teardown = false;
    bool gui_excluded = false;

    {
        GuiWorker worker(sync, capture);
        // Establish a real project/Scene UI before isolating the worker-exit path.
        {
            std::unique_lock lock(sync.shadow_buffer_mutex);
            ProjectCommandEvent select{ProjectCommandType::PROJECT_SET, info->sim_uid};
            ProjectCommandEvent play{ProjectCommandType::PROJECT_PLAY};
            worker.handleProjectCommands(select);
            worker.handleProjectCommands(play);
            worker.populateOverlay();
        }

        sync.quit();
        worker.startWorker();
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!reached_teardown && std::chrono::steady_clock::now() < deadline)
        {
            window.threadQueue().pump();
            reached_teardown = state.entered_destroy.try_acquire();
            std::this_thread::yield();
        }

        // The UI has been deleted, but the project/Scene still exist while the
        // worker is in sceneDestroy. The GUI must not enter population here.
        if (reached_teardown)
        {
            std::unique_lock lock(sync.shadow_buffer_mutex, std::try_to_lock);
            gui_excluded = !lock.owns_lock();
        }
        state.finish_destroy.release();
        // Worker destruction pumps deferred GUI cleanup and joins the owner.
    }
    factories.pop_back();

    CHECK(reached_teardown);
    CHECK(gui_excluded);
    CHECK(state.overlays == 1);
    CHECK(state.ui_destroyed);
    CHECK(state.ui_destroyed_on_gui);
    CHECK(state.scene_destroyed_on_worker);
    CHECK(state.project_deleted);
    CHECK(state.deferred_cleanup == 2);
}
