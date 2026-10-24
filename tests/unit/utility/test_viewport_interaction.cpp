#include <catch2/catch_test_macros.hpp>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    struct PointerCounts
    {
        int downs = 0;
        int moves = 0;
        int ups = 0;
    };

    struct PointerScene : BasicScene
    {
        struct Config
        {
            PointerCounts* counts = nullptr;
        };

        PointerCounts* counts = nullptr;

        explicit PointerScene(Config& config) : counts(config.counts)
        {
        }

        void onPointerDown(PointerEvent) override { counts->downs++; }
        void onPointerMove(PointerEvent) override { counts->moves++; }
        void onPointerUp(PointerEvent) override { counts->ups++; }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct OverlapProject : BasicProject
    {
        PointerCounts bottom_counts;
        PointerCounts top_counts;
        Viewport* bottom = nullptr;
        Viewport* top = nullptr;

        void projectPrepare(Layout& layout) override
        {
            const SceneHandle<PointerScene> bottom_scene = create<PointerScene>(PointerScene::Config{ &bottom_counts });
            const SceneHandle<PointerScene> top_scene = create<PointerScene>(PointerScene::Config{ &top_counts });

            bottom = &layout.add(bottom_scene, "bottom");
            bottom->setPlacement(ViewportPlacement::fill());
            top = &layout.add(top_scene, "top");
            top->setPlacement(ViewportPlacement::normalized(DRect(0.25, 0.25, 0.75, 0.75)));
            top->setZIndex(10);
        }
    };

    struct CameraScene : BasicScene
    {
        CameraInfo shared_camera;
        CameraNavigator navigator;

        void sceneStart() override
        {
            shared_camera.setOriginViewportAnchor(Anchor::CENTER);
            navigator.setTarget(shared_camera);
            navigator.setDirectCameraPanning(true);
        }

        void viewportDraw(Viewport* ctx) const override
        {
            ctx->transform(shared_camera);
        }

        void onEvent(Event event) override
        {
            if (event.ownedBy(this))
                navigator.handleWorldNavigation(event, true);
        }
    };

    struct CameraProject : BasicProject
    {
        SceneHandle<CameraScene> scene;
        Viewport* large = nullptr;
        Viewport* inset = nullptr;

        void projectPrepare(Layout& layout) override
        {
            scene = create<CameraScene>();
            large = &layout.add(scene, "large");
            large->setPlacement(ViewportPlacement::fill());
            inset = &layout.add(scene, "inset");
            inset->setPlacement(ViewportPlacement::anchored(Anchor::BOTTOM_RIGHT, DVec2(100.0, 80.0)));
        }
    };

    void requireNear(DVec2 actual, DVec2 expected)
    {
        REQUIRE(std::abs(actual.x - expected.x) < 0.00001);
        REQUIRE(std::abs(actual.y - expected.y) < 0.00001);
    }
}

TEST_CASE("overlapping Viewports use reverse z-order hit-testing and pointer pass-through")
{
    ImDebugLog log;
    CaptureContext capture_context;
    OverlapProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    InputEmulation input(IVec2(300, 200));
    input.click(0, DVec2(150.0, 100.0));
    input.queueFrame(project, 0);
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(project.top_counts.downs == 1);
    REQUIRE(project.top_counts.ups == 1);
    REQUIRE(project.bottom_counts.downs == 0);

    project.top->setPointerEvents(PointerEvents::None);
    input.click(1, DVec2(150.0, 100.0));
    input.queueFrame(project, 1);
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(project.top_counts.downs == 1);
    REQUIRE(project.bottom_counts.downs == 1);
    REQUIRE(project.bottom_counts.ups == 1);

    project.stopHeadless();
}

TEST_CASE("pointer capture survives leaving a Viewport and cancels on hide")
{
    ImDebugLog log;
    CaptureContext capture_context;
    OverlapProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    InputEmulation input(IVec2(300, 200));
    input.mouseDown(0, DVec2(150.0, 100.0));
    input.mouseMove(1, DVec2(10.0, 10.0), SDL_BUTTON_LMASK);
    input.mouseUp(2, DVec2(10.0, 10.0));

    input.queueFrame(project, 0);
    project.processHeadlessFrame(1.0 / 60.0);
    input.queueFrame(project, 1);
    project.processHeadlessFrame(1.0 / 60.0);
    input.queueFrame(project, 2);
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(project.top_counts.downs == 1);
    REQUIRE(project.top_counts.moves == 1);
    REQUIRE(project.top_counts.ups == 1);
    REQUIRE(project.bottom_counts.moves == 0);

    input.mouseDown(3, DVec2(150.0, 100.0));
    input.mouseMove(4, DVec2(10.0, 10.0), SDL_BUTTON_LMASK);
    input.mouseUp(5, DVec2(10.0, 10.0));
    input.queueFrame(project, 3);
    project.processHeadlessFrame(1.0 / 60.0);
    project.top->setVisible(false);
    input.queueFrame(project, 4);
    project.processHeadlessFrame(1.0 / 60.0);
    input.queueFrame(project, 5);
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(project.top_counts.downs == 2);
    REQUIRE(project.top_counts.moves == 1);
    REQUIRE(project.top_counts.ups == 1);

    project.stopHeadless();
}

TEST_CASE("owned touch events retain separate Viewport captures across batches", "[owned-input]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    OverlapProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    for (int cycle = 0; cycle < 4; cycle++)
    {
        InputEmulation input(IVec2(300, 200));
        input.fingerDown(0, 0x1'0000'0007, DVec2(150.0, 100.0));
        input.fingerDown(0, 0x2'0000'0007, DVec2(10.0, 10.0));
        input.fingerMove(1, 0x1'0000'0007, DVec2(10.0, 10.0));
        input.fingerMove(1, 0x2'0000'0007, DVec2(150.0, 100.0));
        input.fingerUp(2, 0x2'0000'0007, DVec2(150.0, 100.0));
        input.fingerUp(2, 0x1'0000'0007, DVec2(10.0, 10.0));

        for (int frame = 0; frame < 3; frame++)
        {
            input.queueFrame(project, frame);
            project.processHeadlessFrame(1.0 / 60.0);
        }

        REQUIRE(project.top_counts.downs == cycle + 1);
        REQUIRE(project.top_counts.moves == cycle + 1);
        REQUIRE(project.top_counts.ups == cycle + 1);
        REQUIRE(project.bottom_counts.downs == cycle + 1);
        REQUIRE(project.bottom_counts.moves == cycle + 1);
        REQUIRE(project.bottom_counts.ups == cycle + 1);
    }

    project.stopHeadless();
}

TEST_CASE("removing a captured Viewport safely discards later queued motion and release", "[owned-input]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    OverlapProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    InputEmulation input(IVec2(300, 200));
    input.mouseDown(0, DVec2(150.0, 100.0));
    input.mouseMove(1, DVec2(10.0, 10.0), SDL_BUTTON_LMASK);
    input.mouseUp(1, DVec2(10.0, 10.0));
    input.click(2, DVec2(150.0, 100.0));

    input.queueFrame(project, 0);
    project.processHeadlessFrame(1.0 / 60.0);
    input.queueFrame(project, 1);
    project.currentLayout().remove(*project.top);
    project.top = nullptr;
    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.top_counts.downs == 1);
    REQUIRE(project.top_counts.moves == 0);
    REQUIRE(project.top_counts.ups == 0);
    REQUIRE(project.bottom_counts.moves == 0);
    REQUIRE(project.bottom_counts.ups == 0);

    input.queueFrame(project, 2);
    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.bottom_counts.downs == 1);
    REQUIRE(project.bottom_counts.ups == 1);

    project.stopHeadless();
}

TEST_CASE("a shared Camera projects independently through differently sized Viewports")
{
    ImDebugLog log;
    CaptureContext capture_context;
    CameraProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    const DVec2 large_origin = project.scene->shared_camera.transformFor(*project.large).toStage<f64>(DVec2(0.0, 0.0));
    const DVec2 inset_origin = project.scene->shared_camera.transformFor(*project.inset).toStage<f64>(DVec2(0.0, 0.0));
    requireNear(large_origin, DVec2(150.0, 100.0));
    requireNear(inset_origin, DVec2(50.0, 40.0));

    project.stopHeadless();
}

TEST_CASE("CameraNavigator projects input through the event's Viewport")
{
    ImDebugLog log;
    CaptureContext capture_context;
    CameraProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    project.inset->setInitialSize();
    project.inset->setSurfaceSize(200.0, 160.0);

    InputEmulation input(IVec2(300, 200));
    input.mouseDown(0, DVec2(250.0, 160.0));
    input.mouseMove(1, DVec2(260.0, 160.0), SDL_BUTTON_LMASK);
    input.mouseUp(2, DVec2(260.0, 160.0));

    input.queueFrame(project, 0);
    project.processHeadlessFrame(1.0 / 60.0);
    input.queueFrame(project, 1);
    project.processHeadlessFrame(1.0 / 60.0);
    input.queueFrame(project, 2);
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(std::abs(project.scene->shared_camera.x() + 10.0) < 0.00001);
    REQUIRE(std::abs(project.scene->shared_camera.y()) < 0.00001);

    project.stopHeadless();
}

TEST_CASE("CameraNavigator receives complete pinch sequences from owned events", "[owned-input]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    CameraProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    InputEmulation input(IVec2(300, 200));
    input.fingerDown(0, 41, DVec2(50.0, 100.0));
    input.fingerDown(0, 82, DVec2(150.0, 100.0));
    input.fingerMove(1, 41, DVec2(25.0, 100.0));
    input.fingerMove(1, 82, DVec2(175.0, 100.0));
    input.fingerUp(2, 41, DVec2(25.0, 100.0));
    input.fingerUp(2, 82, DVec2(175.0, 100.0));
    input.fingerMove(3, 82, DVec2(200.0, 100.0));

    const double initial_zoom = project.scene->shared_camera.zoom();
    for (int frame = 0; frame < 3; frame++)
    {
        input.queueFrame(project, frame);
        project.processHeadlessFrame(1.0 / 60.0);
    }

    const double final_zoom = project.scene->shared_camera.zoom();
    REQUIRE(std::abs(final_zoom - initial_zoom * 1.5) < 0.00001);
    input.queueFrame(project, 3);
    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.scene->shared_camera.zoom() == final_zoom);

    project.stopHeadless();
}

#endif
