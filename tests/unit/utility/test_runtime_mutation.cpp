#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <thread>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    struct MutationCounts
    {
        int constructed = 0;
        int started = 0;
        int mounted = 0;
        int unmounted = 0;
        int processed = 0;
        int viewport_processed = 0;
        int destroyed = 0;
        int destructed = 0;
        std::thread::id start_thread;
        std::thread::id destroy_thread;
    };

    struct MutationScene : BasicScene
    {
        struct Config
        {
            MutationCounts* counts = nullptr;
            std::function<void()>* process_action = nullptr;
        };

        MutationCounts* counts = nullptr;
        std::function<void()>* process_action = nullptr;

        explicit MutationScene(Config& config) : counts(config.counts), process_action(config.process_action)
        {
            counts->constructed++;
        }

        ~MutationScene() override
        {
            counts->destructed++;
        }

        void sceneStart() override
        {
            counts->started++;
            counts->start_thread = std::this_thread::get_id();
        }

        void sceneMounted(Viewport*) override
        {
            counts->mounted++;
        }

        void sceneUnmounted(Viewport*) override
        {
            counts->unmounted++;
        }

        void sceneProcess(double) override
        {
            counts->processed++;
            if (process_action && *process_action)
            {
                std::function<void()> action = std::move(*process_action);
                *process_action = {};
                action();
            }
        }

        void viewportProcess(Viewport*, double) override
        {
            counts->viewport_processed++;
        }

        void sceneDestroy() override
        {
            counts->destroyed++;
            counts->destroy_thread = std::this_thread::get_id();
        }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct MutationProject : BasicProject
    {
        MutationCounts driver_counts;
        MutationCounts runtime_counts;
        std::function<void()> process_action;
        SceneHandle<MutationScene> driver;
        SceneHandle<MutationScene> runtime;
        Viewport* first = nullptr;
        Viewport* second = nullptr;

        void projectPrepare(Layout&) override
        {
            driver = create<MutationScene>("driver", MutationScene::Config{ &driver_counts, &process_action });
        }

        void spawnRuntimeScene()
        {
            process_action = [this]()
            {
                runtime = create<MutationScene>("runtime", MutationScene::Config{ &runtime_counts });
                first = &currentLayout().add(runtime, "runtime-main");
                second = &currentLayout().add(runtime, "runtime-overlay");
                second->setPlacement(ViewportPlacement::anchored(Anchor::BOTTOM_RIGHT, DVec2(80.0, 60.0)));
                second->setZIndex(10);
            };
        }

        void destroyRuntimeScene()
        {
            process_action = [this]() { destroy(runtime); };
        }
    };
}

TEST_CASE("runtime Scene and Viewport mutations commit after active iteration")
{
    ImDebugLog log;
    CaptureContext capture_context;
    MutationProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    const std::thread::id worker_thread = std::this_thread::get_id();
    project.spawnRuntimeScene();
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(project.runtime.ready());
    REQUIRE(project.currentLayout().count() == 2);
    REQUIRE(project.runtime_counts.constructed == 1);
    REQUIRE(project.runtime_counts.started == 1);
    REQUIRE(project.runtime_counts.mounted == 2);
    REQUIRE(project.runtime_counts.processed == 0);
    REQUIRE(project.runtime_counts.viewport_processed == 0);
    REQUIRE(project.runtime_counts.start_thread == worker_thread);
    REQUIRE(project.second->zIndex() == 10);

    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.runtime_counts.processed == 1);
    REQUIRE(project.runtime_counts.viewport_processed == 2);

    project.destroyRuntimeScene();
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE_FALSE(project.runtime.ready());
    REQUIRE(project.currentLayout().count() == 0);
    REQUIRE(project.runtime_counts.processed == 2);
    REQUIRE(project.runtime_counts.viewport_processed == 4);
    REQUIRE(project.runtime_counts.unmounted == 2);
    REQUIRE(project.runtime_counts.destroyed == 1);
    REQUIRE(project.runtime_counts.destructed == 1);
    REQUIRE(project.runtime_counts.destroy_thread == worker_thread);

    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.runtime_counts.processed == 2);
    REQUIRE(project.runtime_counts.viewport_processed == 4);

    project.stopHeadless();
}

TEST_CASE("runtime Scene handles remain stable through repeated spawn and removal")
{
    ImDebugLog log;
    CaptureContext capture_context;
    MutationProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    for (int cycle = 1; cycle <= 4; ++cycle)
    {
        project.spawnRuntimeScene();
        project.processHeadlessFrame(1.0 / 60.0);
        const SceneHandle<MutationScene> stale = project.runtime;
        REQUIRE(stale.ready());
        REQUIRE(project.currentLayout().count() == 2);

        project.destroyRuntimeScene();
        project.processHeadlessFrame(1.0 / 60.0);
        REQUIRE_FALSE(stale.ready());
        REQUIRE(stale.get() == nullptr);
        REQUIRE(project.currentLayout().count() == 0);
        REQUIRE(project.runtime_counts.destroyed == cycle);
        REQUIRE(project.runtime_counts.destructed == cycle);
    }

    project.stopHeadless();
}

TEST_CASE("runtime Viewport changes are deferred and cancel pointer ownership safely")
{
    ImDebugLog log;
    CaptureContext capture_context;
    MutationProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();
    project.spawnRuntimeScene();
    project.processHeadlessFrame(1.0 / 60.0);

    project.first->setVisible(false);
    project.second->setPointerEvents(PointerEvents::None);
    project.second->setRenderSize(IVec2(640, 480));
    REQUIRE(project.first->isVisible());
    REQUIRE(project.second->pointerEvents() == PointerEvents::Auto);

    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE_FALSE(project.first->isVisible());
    REQUIRE(project.second->pointerEvents() == PointerEvents::None);
    REQUIRE(project.second->renderSize() == IVec2(640, 480));

    project.currentLayout().remove(*project.second);
    REQUIRE(project.currentLayout().count() == 2);
    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.currentLayout().count() == 1);
    REQUIRE(project.runtime.ready());

    project.stopHeadless();
}

#endif
