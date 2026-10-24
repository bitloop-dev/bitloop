#include <catch2/catch_test_macros.hpp>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    void requireRect(DRect actual, DRect expected)
    {
        REQUIRE(actual.x1 == expected.x1);
        REQUIRE(actual.y1 == expected.y1);
        REQUIRE(actual.x2 == expected.x2);
        REQUIRE(actual.y2 == expected.y2);
    }

    struct LifecycleCounts
    {
        int constructed = 0;
        int started = 0;
        int mounted = 0;
        int unmounted = 0;
        int processed = 0;
        int viewport_processed = 0;
        int destroyed = 0;
        int destructed = 0;
    };

    struct LifecycleScene : public BasicScene
    {
        struct Config
        {
            LifecycleCounts* counts = nullptr;
        };

        LifecycleCounts* counts = nullptr;

        explicit LifecycleScene(Config& config) : counts(config.counts)
        {
            counts->constructed++;
        }

        ~LifecycleScene() override
        {
            counts->destructed++;
        }

        void sceneStart() override
        {
            counts->started++;
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
        }

        void viewportProcess(Viewport*, double) override
        {
            counts->viewport_processed++;
        }

        void sceneDestroy() override
        {
            counts->destroyed++;
        }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct LifecycleProject : public BasicProject
    {
        LifecycleCounts* counts = nullptr;
        SceneHandle<LifecycleScene> scene;
        int viewport_count = 1;

        LifecycleProject(LifecycleCounts& counts, int viewport_count)
            : counts(&counts), viewport_count(viewport_count)
        {
        }

        void projectPrepare(Layout& layout) override
        {
            scene = create<LifecycleScene>(LifecycleScene::Config{ counts });
            for (int index = 0; index < viewport_count; ++index)
                layout << scene;
        }
    };
}

TEST_CASE("headless Scene lifecycle is deterministic across restart")
{
    ImDebugLog log;
    CaptureContext capture_context;
    LifecycleCounts counts;
    LifecycleProject project(counts, 2);
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);

    project.startHeadless();
    REQUIRE(counts.constructed == 1);
    REQUIRE(counts.started == 1);
    REQUIRE(counts.mounted == 2);
    REQUIRE(project.scenes().size() == 1);
    REQUIRE(project.scene->mountedToViewports().size() == 2);

    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(counts.processed == 1);
    REQUIRE(counts.viewport_processed == 2);

    project.stopHeadless();
    REQUIRE_FALSE(project.scene.ready());
    REQUIRE(counts.unmounted == 2);
    REQUIRE(counts.destroyed == 1);
    REQUIRE(counts.destructed == 1);

    project.startHeadless();
    project.processHeadlessFrame(1.0 / 60.0);
    project.stopHeadless();

    REQUIRE(counts.constructed == 2);
    REQUIRE(counts.started == 2);
    REQUIRE(counts.mounted == 4);
    REQUIRE(counts.unmounted == 4);
    REQUIRE(counts.processed == 2);
    REQUIRE(counts.viewport_processed == 4);
    REQUIRE(counts.destroyed == 2);
    REQUIRE(counts.destructed == 2);
}

TEST_CASE("automatic Layout assigns deterministic grid rectangles")
{
    ImDebugLog log;
    CaptureContext capture_context;
    LifecycleCounts counts;
    LifecycleProject project(counts, 3);
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    Layout& layout = project.currentLayout();
    REQUIRE(layout.count() == 3);
    requireRect(layout[0]->surfaceRect(), DRect(0.0, 0.0, 96.0, 200.0));
    requireRect(layout[1]->surfaceRect(), DRect(102.0, 0.0, 198.0, 200.0));
    requireRect(layout[2]->surfaceRect(), DRect(204.0, 0.0, 300.0, 200.0));

    project.stopHeadless();
}

TEST_CASE("removing Viewports never owns or destroys their Scene")
{
    ImDebugLog log;
    CaptureContext capture_context;
    LifecycleCounts counts;
    LifecycleProject project(counts, 2);
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    Layout& layout = project.currentLayout();
    layout.remove(*layout[0]);
    REQUIRE(layout.count() == 2);
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(layout.count() == 1);
    REQUIRE(project.scene.ready());
    REQUIRE(project.scene->mountedToViewports().size() == 1);
    REQUIRE(counts.unmounted == 1);
    REQUIRE(counts.destroyed == 0);
    REQUIRE(counts.destructed == 0);

    layout.clear();
    REQUIRE(layout.count() == 1);
    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.scene.ready());
    REQUIRE(project.scene->mountedToViewports().empty());
    REQUIRE(counts.unmounted == 2);
    REQUIRE(counts.destroyed == 0);

    REQUIRE(counts.processed == 2);
    REQUIRE(counts.viewport_processed == 1);

    project.stopHeadless();
    REQUIRE(counts.destroyed == 1);
    REQUIRE(counts.destructed == 1);
}

TEST_CASE("a Scene with no Viewport still runs its complete lifecycle")
{
    ImDebugLog log;
    CaptureContext capture_context;
    LifecycleCounts counts;
    LifecycleProject project(counts, 0);
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    REQUIRE(project.currentLayout().count() == 0);
    REQUIRE(project.scene.ready());
    REQUIRE(project.scenes().size() == 1);
    REQUIRE(counts.constructed == 1);
    REQUIRE(counts.started == 1);
    REQUIRE(counts.mounted == 0);

    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(counts.processed == 1);
    REQUIRE(counts.viewport_processed == 0);

    project.stopHeadless();
    REQUIRE(counts.destroyed == 1);
    REQUIRE(counts.destructed == 1);
}

TEST_CASE("default Scene selection materializes only selected declarations")
{
    struct DefaultProject : BasicProject
    {
        LifecycleCounts* first_counts = nullptr;
        LifecycleCounts* second_counts = nullptr;
        SceneHandle<LifecycleScene> first;
        SceneHandle<LifecycleScene> second;

        void projectPrepare(Layout& layout) override
        {
            first = create<LifecycleScene>("first", LifecycleScene::Config{ first_counts });
            second = create<LifecycleScene>("second", LifecycleScene::Config{ second_counts });
            layout << first << second;
            setDefaultScenes({ second });
        }
    };

    ImDebugLog log;
    CaptureContext capture_context;
    LifecycleCounts first_counts;
    LifecycleCounts second_counts;
    DefaultProject project;
    project.first_counts = &first_counts;
    project.second_counts = &second_counts;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    REQUIRE_FALSE(project.first.ready());
    REQUIRE(project.second.ready());
    REQUIRE(project.second.id() == "second");
    REQUIRE(project.scenes().size() == 1);
    REQUIRE(project.currentLayout().count() == 1);
    REQUIRE(first_counts.constructed == 0);
    REQUIRE(second_counts.constructed == 1);

    project.stopHeadless();
}

TEST_CASE("Scene handles fail safely after Project teardown and destruction")
{
    SceneHandle<LifecycleScene> stale;
    {
        ImDebugLog log;
        CaptureContext capture_context;
        LifecycleCounts counts;
        LifecycleProject project(counts, 0);
        project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
        project.startHeadless();
        stale = project.scene;
        REQUIRE(stale.ready());
        project.stopHeadless();
        REQUIRE_FALSE(stale.ready());
        REQUIRE(stale.get() == nullptr);
        REQUIRE_THROWS_AS(stale.operator->(), std::logic_error);
    }

    REQUIRE_FALSE(stale.ready());
    REQUIRE(stale.get() == nullptr);
}

#endif
