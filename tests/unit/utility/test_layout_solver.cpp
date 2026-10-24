#include <catch2/catch_test_macros.hpp>

#include <bitloop.h>

using namespace bl;

namespace
{
    void requireRect(DRect actual, DRect expected)
    {
        REQUIRE(actual.x1 == expected.x1);
        REQUIRE(actual.y1 == expected.y1);
        REQUIRE(actual.x2 == expected.x2);
        REQUIRE(actual.y2 == expected.y2);
    }

#ifdef BITLOOP_DEV_MODE
    struct LayoutScene : BasicScene
    {
        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct LayoutProject : BasicProject
    {
        Viewport* automatic = nullptr;
        Viewport* hidden = nullptr;
        Viewport* overlay = nullptr;

        void projectPrepare(Layout& layout) override
        {
            const SceneHandle<LayoutScene> scene = create<LayoutScene>();
            automatic = &layout.add(scene, "automatic");
            hidden = &layout.add(scene, "hidden");
            hidden->setVisible(false);
            hidden->setRenderSize(IVec2(640, 480));
            overlay = &layout.add(scene, "overlay");
            overlay->setPlacement(ViewportPlacement::anchored(Anchor::BOTTOM_RIGHT, DVec2(80.0, 40.0), DVec2(10.0, 20.0)));
            overlay->setZIndex(5);
            overlay->setPointerEvents(PointerEvents::None);
        }
    };
#endif
}

TEST_CASE("LayoutSolver preserves the default automatic grid")
{
    const std::vector<ViewportLayoutInput> inputs(3);

    std::vector<DRect> rectangles = LayoutSolver::solve(DVec2(300.0, 200.0), GridLayout{}, inputs);
    requireRect(rectangles[0], DRect(0.0, 0.0, 96.0, 200.0));
    requireRect(rectangles[1], DRect(102.0, 0.0, 198.0, 200.0));
    requireRect(rectangles[2], DRect(204.0, 0.0, 300.0, 200.0));

    rectangles = LayoutSolver::solve(DVec2(600.0, 400.0), GridLayout{}, inputs);
    requireRect(rectangles[0], DRect(0.0, 0.0, 196.0, 400.0));
    requireRect(rectangles[1], DRect(202.0, 0.0, 398.0, 400.0));
    requireRect(rectangles[2], DRect(404.0, 0.0, 600.0, 400.0));
}

TEST_CASE("hidden and manually placed Viewports consume no automatic grid cells")
{
    std::vector<ViewportLayoutInput> inputs{
        { ViewportPlacement::automatic(), true },
        { ViewportPlacement::automatic(), false },
        { ViewportPlacement::anchored(Anchor::BOTTOM_RIGHT, DVec2(80.0, 40.0), DVec2(10.0, 20.0)), true },
        { ViewportPlacement::automatic(), true },
    };

    const std::vector<DRect> rectangles = LayoutSolver::solve(DVec2(300.0, 200.0), GridLayout{}, inputs);
    requireRect(rectangles[0], DRect(0.0, 0.0, 147.0, 200.0));
    requireRect(rectangles[1], DRect());
    requireRect(rectangles[2], DRect(210.0, 140.0, 290.0, 180.0));
    requireRect(rectangles[3], DRect(153.0, 0.0, 300.0, 200.0));
}

TEST_CASE("normalized anchored and fill placements respond to resizing")
{
    const std::vector<ViewportLayoutInput> inputs{
        { ViewportPlacement::normalized(DRect(0.25, 0.25, 0.75, 0.75)), true },
        { ViewportPlacement::anchored(Anchor::BOTTOM_RIGHT, DVec2(80.0, 40.0), DVec2(10.0, 20.0)), true },
        { ViewportPlacement::fill(), true },
    };

    std::vector<DRect> rectangles = LayoutSolver::solve(DVec2(300.0, 200.0), GridLayout{}, inputs);
    requireRect(rectangles[0], DRect(75.0, 50.0, 225.0, 150.0));
    requireRect(rectangles[1], DRect(210.0, 140.0, 290.0, 180.0));
    requireRect(rectangles[2], DRect(0.0, 0.0, 300.0, 200.0));

    rectangles = LayoutSolver::solve(DVec2(600.0, 400.0), GridLayout{}, inputs);
    requireRect(rectangles[0], DRect(150.0, 100.0, 450.0, 300.0));
    requireRect(rectangles[1], DRect(510.0, 340.0, 590.0, 380.0));
    requireRect(rectangles[2], DRect(0.0, 0.0, 600.0, 400.0));
}

TEST_CASE("LayoutSolver maps presentation rectangles into client coordinates")
{
    const DRect mapped = LayoutSolver::mapSurfaceToClient(
        DRect(100.0, 50.0, 300.0, 150.0),
        DVec2(400.0, 200.0),
        DRect(20.0, 30.0, 820.0, 430.0));

    requireRect(mapped, DRect(220.0, 130.0, 620.0, 330.0));
}

TEST_CASE("Viewport placement and grid inputs reject invalid geometry")
{
    REQUIRE_THROWS_AS(ViewportPlacement::normalized(DRect(0.0, 0.0, 0.0, 1.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(ViewportPlacement::anchored(Anchor::TOP_LEFT, DVec2(0.0, 10.0)), std::invalid_argument);
    REQUIRE_THROWS_AS(ViewportPlacement::anchored(Anchor::TOP_LEFT, DVec2(10.0, 10.0), DVec2(INFINITY, 0.0)), std::invalid_argument);

    const std::vector<ViewportLayoutInput> inputs(2);
    REQUIRE_THROWS_AS(LayoutSolver::solve(DVec2(0.0, 200.0), GridLayout{}, inputs), std::invalid_argument);
    REQUIRE_THROWS_AS(LayoutSolver::solve(DVec2(300.0, 200.0), GridLayout{ .columns = 0 }, inputs), std::invalid_argument);
    REQUIRE_THROWS_AS(LayoutSolver::solve(DVec2(300.0, 200.0), GridLayout{ .columns = 1, .rows = 1 }, inputs), std::invalid_argument);
    REQUIRE_THROWS_AS(LayoutSolver::solve(DVec2(300.0, 200.0), GridLayout{ .gap = INFINITY }, inputs), std::invalid_argument);
}

#ifdef BITLOOP_DEV_MODE
TEST_CASE("Layout applies persistent Viewport properties to live geometry")
{
    ImDebugLog log;
    CaptureContext capture_context;
    LayoutProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    requireRect(project.automatic->surfaceRect(), DRect(0.0, 0.0, 300.0, 200.0));
    requireRect(project.hidden->surfaceRect(), DRect(0.0, 0.0, 640.0, 480.0));
    requireRect(project.overlay->surfaceRect(), DRect(210.0, 140.0, 290.0, 180.0));
    REQUIRE(project.overlay->zIndex() == 5);
    REQUIRE(project.overlay->pointerEvents() == PointerEvents::None);

    project.hidden->setVisible(true);
    project.processHeadlessFrame(1.0 / 60.0);
    requireRect(project.automatic->surfaceRect(), DRect(0.0, 0.0, 147.0, 200.0));
    requireRect(project.hidden->surfaceRect(), DRect(153.0, 0.0, 300.0, 200.0));
    requireRect(project.overlay->surfaceRect(), DRect(210.0, 140.0, 290.0, 180.0));

    REQUIRE_THROWS_AS(project.hidden->setRenderSize(IVec2(0, 480)), std::invalid_argument);
    project.stopHeadless();
}
#endif
