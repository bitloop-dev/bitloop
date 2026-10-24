#include <catch2/catch_test_macros.hpp>

#include <bitloop.h>

using namespace bl;

namespace
{
    SurfaceInfo makeSurface(double width, double height)
    {
        SurfaceInfo surface;
        surface.setSurfaceSize(width, height);
        surface.setInitialSize();
        return surface;
    }

    void requireNear(DVec2 actual, DVec2 expected, double tolerance = 0.00001)
    {
        REQUIRE(std::abs(actual.x - expected.x) <= tolerance);
        REQUIRE(std::abs(actual.y - expected.y) <= tolerance);
    }
}

TEST_CASE("Camera projection follows the selected Surface dimensions")
{
    SurfaceInfo large = makeSurface(300.0, 200.0);
    SurfaceInfo inset = makeSurface(100.0, 80.0);
    CameraInfo camera;
    camera.setOriginViewportAnchor(Anchor::CENTER);

    requireNear(camera.transformFor(large).toStage<f64>(DVec2(0.0, 0.0)), DVec2(150.0, 100.0));

    requireNear(camera.transformFor(inset).toStage<f64>(DVec2(0.0, 0.0)), DVec2(50.0, 40.0));
    requireNear(camera.pos(), DVec2(0.0, 0.0));
    REQUIRE(camera.zoom() == 1.0);
}

TEST_CASE("Camera projection preserves responsive Surface scaling")
{
    SurfaceInfo surface = makeSurface(100.0, 80.0);
    CameraInfo camera;
    camera.setOriginViewportAnchor(Anchor::CENTER);

    surface.setSurfaceSize(200.0, 160.0);
    const WorldStageTransform transform = camera.transformFor(surface);

    requireNear(transform.toStage<f64>(DVec2(0.0, 0.0)), DVec2(100.0, 80.0));
    requireNear(transform.toStage<f64>(DVec2(1.0, 0.0)), DVec2(102.0, 80.0));
}

TEST_CASE("Painter applies Camera projection using its own Surface")
{
    SurfaceInfo surface = makeSurface(300.0, 200.0);
    CameraInfo camera;
    camera.setOriginViewportAnchor(Anchor::CENTER);
    Painter painter(&surface);

    painter.transform(camera);

    requireNear(painter.worldStageTransform().toStage<f64>(DVec2(0.0, 0.0)), DVec2(150.0, 100.0));
}

TEST_CASE("Camera focus fits a world rectangle using its Surface")
{
    SurfaceInfo surface = makeSurface(300.0, 200.0);
    CameraInfo camera;
    camera.setOriginViewportAnchor(Anchor::CENTER);
    camera.focusWorldRect(surface, -2.0, -1.0, 2.0, 1.0);

    const WorldStageTransform transform = camera.transformFor(surface);
    requireNear(camera.pos(), DVec2(0.0, 0.0));
    requireNear(transform.toStage<f64>(DVec2(-2.0, -1.0)), DVec2(0.0, 25.0));
    requireNear(transform.toStage<f64>(DVec2(2.0, 1.0)), DVec2(300.0, 175.0));
}
