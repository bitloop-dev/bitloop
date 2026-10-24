#include <catch2/catch_test_macros.hpp>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    struct CleanupState
    {
        bool alive = false;
        bool cleanup_after_destruction = false;
        int completed = 0;
        int completed_at_destruction = 0;
        int project_cleanup = 0;

        void recordCleanup()
        {
            cleanup_after_destruction |= !alive;
            completed++;
        }
    };

    void postCleanup(ThreadQueue& queue, CleanupState& state, int remaining)
    {
        queue.post([&queue, &state, remaining]()
        {
            state.recordCleanup();
            if (remaining > 1)
                postCleanup(queue, state, remaining - 1);
        });
    }

    struct CleanupScene : BasicScene
    {
        struct Config
        {
            CleanupState* state = nullptr;
        };

        CleanupState& state;

        explicit CleanupScene(Config& config) : state(*config.state)
        {
            state.alive = true;
        }

        ~CleanupScene() override
        {
            state.completed_at_destruction = state.completed;
            state.alive = false;
        }

        void sceneStop() override
        {
            postCleanup(renderThreadQueue(), state, 1);
        }

        void sceneDestroy() override
        {
            postCleanup(renderThreadQueue(), state, 80);
        }

        void viewportDraw(Viewport*) const override {}
    };

    struct CleanupProject : BasicProject
    {
        CleanupState state;
        SceneHandle<CleanupScene> scene;

        void projectPrepare(Layout&) override
        {
            state = {};
            scene = create<CleanupScene>(CleanupScene::Config{ &state });
        }

        void projectDestroy() override
        {
            if (scene.ready())
            {
                renderThreadQueue().post([this]()
                {
                    state.recordCleanup();
                    state.project_cleanup++;
                });
            }
        }
    };
}

TEST_CASE("Scene removal finishes queued cleanup before deleting the Scene", "[scene_cleanup]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    CleanupProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    project.destroy(project.scene);
    project.processHeadlessFrame(1.0 / 60.0);

    CHECK_FALSE(project.scene.ready());
    CHECK(project.state.completed_at_destruction == 81);
    CHECK(project.state.completed == 81);
    CHECK_FALSE(project.state.cleanup_after_destruction);
    project.stopHeadless();
}

TEST_CASE("Project teardown finishes queued cleanup before deleting its Scenes", "[scene_cleanup]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    CleanupProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);

    for (int cycle = 0; cycle < 2; ++cycle)
    {
        project.startHeadless();
        project.stopHeadless();

        CHECK_FALSE(project.scene.ready());
        CHECK(project.state.completed_at_destruction == 82);
        CHECK(project.state.completed == 82);
        CHECK(project.state.project_cleanup == 1);
        CHECK_FALSE(project.state.cleanup_after_destruction);
    }
}

#endif
