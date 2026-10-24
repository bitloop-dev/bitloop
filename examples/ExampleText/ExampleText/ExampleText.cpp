#include "ExampleText.h"

SIM_BEG;
using namespace bl;

/// ─────────────────────── Project ───────────────────────

void ExampleText_Project::UI::sidebar()
{
    bl_scoped(viewport_count);
    ImGui::SliderInt("Viewport Count", &viewport_count, 1, 8);
}

void ExampleText_Project::projectPrepare(Layout& layout)
{
    /// Create multiple instance of a single Scene, mount to separate viewports
    for (int i = 0; i < viewport_count; i++)
        layout << create<ExampleText_Scene>();

    /// Or create a single Scene instance and view on multiple Viewports
    // SceneHandle<ExampleText_Scene> scene = create<ExampleText_Scene>();
    //for (int i = 0; i < viewport_count; ++i)
    //    layout << scene;
}

/// ─────────────────────── Scene ───────────────────────

void ExampleText_Scene::UI::sidebar()
{
    if (ImGui::Section("View", true))
    {
        bl_scoped(camera);
        camera.populateUI(); // imgui camera controls
    }
    
    if (ImGui::Section("Options", true))
    {
        bl_scoped(opts);

        ImGui::Checkbox("Transform coordinates", &opts.transform_coordinates);
        ImGui::Checkbox("Scale Lines", &opts.scale_lines);
        ImGui::Checkbox("Scale Sizes", &opts.scale_sizes);
        ImGui::Checkbox("Scale Text",  &opts.scale_text);
        ImGui::Checkbox("Rotate Text", &opts.rotate_text);
        ImGui::SliderDouble("Font Size", &opts.font_size, 1.0, 100.0);
    }
}

void ExampleText_Scene::sceneStart()
{
    // Initialize scene
    fonts.push_back(NanoFont::create("/data/fonts/NK57 Monospace Sc Bk.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Baileys Car.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Barbatrick.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Baveuse 3d.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Dacquoise.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Deftone Stylus.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Degrassi.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Deluxe Ducks.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Die Nasty.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Iomanoid.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Jandles.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Metal Lord.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Paint Boy.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Waker.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Robokoz.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Zeroes One.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Lunasol Aurora.otf"));
    fonts.push_back(NanoFont::create("/data/fonts/Street Cred.otf"));

}

void ExampleText_Scene::sceneMounted([[maybe_unused]] Viewport* ctx)
{
    // Initialize viewport
    camera.setOriginViewportAnchor(Anchor::CENTER);
    //camera.focusWorldRect(*ctx, -100, -100, 1000, 700);

    navigator.setTarget(camera);
    navigator.setDirectCameraPanning(true);

}

void ExampleText_Scene::sceneDestroy()
{
    // Destroy scene (dynamically allocated resources, etc.)
}

void ExampleText_Scene::sceneProcess([[maybe_unused]] double dt)
{
    // Process scene update
}

void ExampleText_Scene::viewportProcess(
    [[maybe_unused]] Viewport* ctx,
    [[maybe_unused]] double dt)
{
    // Process viewport update
    if (Changed(camera, opts))
        requestRedraw(true);
}

void ExampleText_Scene::viewportDraw(Viewport* ctx) const
{
    // Draw Scene on this viewport (don't update simulation state here)
    ctx->transform(camera);
    ctx->drawWorldAxis();

    ctx->worldCoordinates(opts.transform_coordinates);
    ctx->scalingLines(opts.scale_lines);
    ctx->scalingSizes(opts.scale_sizes);
    ctx->scalingText(opts.scale_text);
    ctx->rotatingText(opts.rotate_text);
    ctx->setFontSize(opts.font_size);

    ctx->setFillStyle(255, 255, 255);
    ctx->setTextAlign(TextAlign::ALIGN_LEFT);
    for (size_t i = 0; i < fonts.size(); i++)
    {
        double y = (double)i * opts.font_size + 50.0;
        ctx->setFont(fonts[i]);
        ctx->fillText("The quick brown fox jumps over the lazy dog", 50.0, y);

    }
}

void ExampleText_Scene::onEvent(Event e)
{
    if (!e.ownedBy(this))
        return;

    navigator.handleWorldNavigation(e, true);
}

//void ExampleText_Scene::onPointerDown(PointerEvent e) {{}}
//void ExampleText_Scene::onPointerUp(PointerEvent e) {{}}
//void ExampleText_Scene::onPointerMove(PointerEvent e) {{}}
//void ExampleText_Scene::onWheel(PointerEvent e) {{}}
//void ExampleText_Scene::onKeyDown(KeyEvent e) {{}}
//void ExampleText_Scene::onKeyUp(KeyEvent e) {{}}

SIM_END;
