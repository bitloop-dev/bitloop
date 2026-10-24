#include "{SIM_NAME}.h"

SIM_BEG;

using namespace bl;

/// ─────────────────────── project setup ───────────────────────

void {SIM_NAME}_Project::UI::sidebar()
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Use bl_pull(..) / bl_push(..) on all used {SIM_NAME}_Project variables,
    /// or bl_scoped(..) to pull and push automatically in the current scope.
    /// Custom types should provide:
    ///   - operator==(const T&) to avoid unnecessary syncing (use hashing if possible)
    ///   - an assignment operator and copy constructor

    // BITLOOP_TEMPLATE_GUIDE_END
    bl_scoped(viewport_count);

    ImGui::SliderInt("Viewport Count", &viewport_count, 1, 8);
}

void {SIM_NAME}_Project::projectPrepare(Layout& layout)
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Create multiple Scene instances with one automatic-grid Viewport each.
    // BITLOOP_TEMPLATE_GUIDE_END
    for (int i = 0; i < viewport_count; i++)
        layout << create<{SIM_NAME}_Scene>();
    // BITLOOP_TEMPLATE_GUIDE_BEGIN

    /// Alternatively, create one Scene instance and show it in multiple Viewports.
    // SceneHandle<{SIM_NAME}_Scene> scene = create<{SIM_NAME}_Scene>();
    // for (int i = 0; i < viewport_count; ++i)
    //     layout << scene;
    /// Give catalogue Scenes stable IDs and select defaults when only a subset should launch.
    // auto primary = create<{SIM_NAME}_Scene>("primary");
    // layout.add(primary, "main");
    // setDefaultScenes({ primary });
    // BITLOOP_TEMPLATE_GUIDE_END
}

/// ────────────────────────── scene ──────────────────────────

void {SIM_NAME}_Scene::UI::sidebar()
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Use bl_pull(..) / bl_push(..) on all used {SIM_NAME}_Scene variables,
    /// or bl_scoped(..) to pull and push automatically in the current scope.
    /// Custom types should provide:
    ///   - operator==(const T&) to avoid unnecessary syncing (use hashing if possible)
    ///   - an assignment operator and copy constructor

    // BITLOOP_TEMPLATE_GUIDE_END
    if (ImGui::CollapsingHeaderBox("View", true))
    {
        // BITLOOP_TEMPLATE_GUIDE_BEGIN
        // ImGui camera controls.
        // BITLOOP_TEMPLATE_GUIDE_END
        bl_scoped(camera);

        camera.populateUI();
        ImGui::EndCollapsingHeaderBox();
    }

    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Example: Scene variable input.
    // bl_pull(gravity);
    // ImGui::SliderDouble("gravity", &gravity, 0.0, 10.0);
    // bl_push(gravity);

    /// Example: Queue a {SIM_NAME}_Scene::foo() call.
    // if (ImGui::Button("FOO"))
    // {
    //     bl_schedule([]({SIM_NAME}_Scene& scene) {
    //         scene.foo("BAR");
    //     });
    // }

    /// Example: UI-only state needs no synchronization.
    // ImGui::Checkbox("option", &test_popup_open);
    // BITLOOP_TEMPLATE_GUIDE_END
}

void {SIM_NAME}_Scene::sceneStart()
{
    /// Initialize the Scene.
}

void {SIM_NAME}_Scene::sceneMounted([[maybe_unused]] Viewport* ctx)
{
    /// Initialize viewport.
    camera.setOriginViewportAnchor(Anchor::CENTER);
    // camera.focusWorldRect(*ctx, -100, -100, 100, 100);

    /// Enable reverting to the current camera view.
    camera.uiSetCurrentAsDefault();

    /// Configure default navigator.
    navigator.setTarget(camera);
    navigator.setDirectCameraPanning(true);
}

void {SIM_NAME}_Scene::sceneUnmounted([[maybe_unused]] Viewport* ctx)
{
    /// Release state owned specifically for this Viewport.
}

void {SIM_NAME}_Scene::sceneDestroy()
{
    /// Release dynamically allocated Scene resources.
}

void {SIM_NAME}_Scene::sceneProcess([[maybe_unused]] double dt)
{
    /// Process the Scene once per frame, not once per viewport.

    // redraw the scene every frame
    requestRedraw(true);
}

void {SIM_NAME}_Scene::viewportProcess(
    [[maybe_unused]] Viewport* ctx,
    [[maybe_unused]] double dt)
{
    /// Process one update for each mounted viewport every frame.
}

void {SIM_NAME}_Scene::viewportDraw(Viewport* ctx) const
{
    /// Draw the Scene on this viewport without modifying simulation state.
    ctx->transform(camera);
    ctx->drawWorldAxis();
}

void {SIM_NAME}_Scene::onEvent(Event e)
{
    if (e.ownedBy(this))
        navigator.handleWorldNavigation(e, true);
}

// void {SIM_NAME}_Scene::onPointerDown(PointerEvent e) {}
// void {SIM_NAME}_Scene::onPointerUp(PointerEvent e) {}
// void {SIM_NAME}_Scene::onPointerMove(PointerEvent e) {}
// void {SIM_NAME}_Scene::onWheel(PointerEvent e) {}
// void {SIM_NAME}_Scene::onKeyDown(KeyEvent e) {}
// void {SIM_NAME}_Scene::onKeyUp(KeyEvent e) {}

// BITLOOP_TEMPLATE_AI_BEGIN
#ifdef BITLOOP_DEV_MODE
void {SIM_NAME}_Scene::traceFrame([[maybe_unused]] SimTraceFrame& frame) const
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Record semantic state for headless Codex feedback.
    /// For dense simulations, prefer aggregate metrics, sampled objects, and events
    /// instead of every particle.
    /// Example:
    /// if (auto* object = frame.addObject("primary", "particle", x, y))
    /// {
    ///     object->vx = vx;
    ///     object->vy = vy;
    ///     object->radius = radius;
    ///     object->attribute("energy", energy);
    /// }
    /// frame.metric("object_count", static_cast<double>(objects.size()));
    ///
    /// Request visual snapshots only when they help diagnose behavior:
    /// traceCanvas("overview", "after initialization");
    /// traceCanvas("collision_crop", IRect(100, 100, 220, 220), "local collision region");
    // BITLOOP_TEMPLATE_GUIDE_END
}

bool {SIM_NAME}_Scene::captureTraceSnapshot([[maybe_unused]] std::string_view route, [[maybe_unused]] SimSnapshotImage& image) const
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Supply native dimensions and tightly packed RGBA8 pixels, top row first.
    /// Keep simulation state unchanged; return false for unsupported routes.
    // BITLOOP_TEMPLATE_GUIDE_END
    return false;
}

bool {SIM_NAME}_Scene::saveCheckpoint([[maybe_unused]] SimCheckpointWriter& writer) const
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Opt in to resumable headless runs by writing simulation-owned state.
    /// Use named chunks so layout changes fail explicitly instead of restoring
    /// arbitrary process memory.
    /// Example:
    /// writer.writePod("rng_seed", rng_seed);
    /// writer.writeTrivialVector("particles", particles);
    // BITLOOP_TEMPLATE_GUIDE_END
    return false;
}

bool {SIM_NAME}_Scene::loadCheckpoint([[maybe_unused]] const SimCheckpointReader& reader)
{
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Restore the same chunks written by saveCheckpoint(...).
    /// Example:
    /// reader.readPod("rng_seed", rng_seed);
    /// particles = reader.readTrivialVector<Particle>("particles");
    // BITLOOP_TEMPLATE_GUIDE_END
    return false;
}
#endif
// BITLOOP_TEMPLATE_AI_END

SIM_END;
