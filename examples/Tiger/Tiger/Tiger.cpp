#include "Tiger.h"
#include "draw_tiger.h"

SIM_BEG;

using namespace bl;

/// ─────────────────────── Project ───────────────────────

void Tiger_Project::UI::sidebar()
{
    bl_scoped(viewport_count);

    ImGui::SliderInt("Viewport Count", &viewport_count, 1, 8);
}

void Tiger_Project::projectPrepare(Layout& layout)
{
    for (int i = 0; i < viewport_count; i++)
        layout << create<Tiger_Scene>();
}

/// ─────────────────────── Scene ───────────────────────

void Tiger_Scene::UI::sidebar()
{
    bl_scoped(transform_coordinates);
    bl_scoped(scale_lines);
    bl_scoped(scale_sizes);
    bl_scoped(rotate_text);

    ImGui::Checkbox("Transform coordinates", &transform_coordinates);
    ImGui::Checkbox("Scale Lines", &scale_lines);
    ImGui::Checkbox("Scale Sizes", &scale_sizes);
    ImGui::Checkbox("Rotate Text", &rotate_text);

    if (ImGui::Section("View", true)) 
    {
        // imgui camera controls
        bl_scoped(camera);
        camera.populateUI();
    }
}

void Tiger_Scene::sceneStart()
{}

void Tiger_Scene::sceneMounted(Viewport* ctx)
{
    // initialize viewport
    camera.setOriginViewportAnchor(Anchor::CENTER);
    camera.focusWorldRect(*ctx, -100, -100, 1000, 1000);

    navigator.setTarget(camera);
    navigator.setDirectCameraPanning(true);
}

void Tiger_Scene::sceneDestroy()
{}

void Tiger_Scene::sceneProcess([[maybe_unused]] double dt)
{
}

void Tiger_Scene::viewportProcess(Viewport*, double)
{
    // Process your scene...
    ///camera.setRotation(camera.rotation() + 0.01);
    if (Changed(camera, transform_coordinates, scale_lines, scale_sizes))
        requestRedraw(true);
}

void Tiger_Scene::viewportDraw(Viewport* ctx) const
{
    ctx->transform(camera);
    ctx->drawWorldAxis();

    ctx->worldCoordinates(transform_coordinates);
    ctx->scalingLines(scale_lines);
    ctx->scalingSizes(scale_sizes);

    draw_tiger(ctx);
}

void Tiger_Scene::onEvent(Event e)
{
    if (e.ownedBy(this))
        navigator.handleWorldNavigation(e, true);
}

SIM_END;
