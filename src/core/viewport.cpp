#include <bitloop/core/viewport.h>

#include <stdexcept>
#include <utility>

#include <bitloop/core/scene.h>
#include <bitloop/core/project.h>
#include <bitloop/core/capture_context.h>

BL_BEGIN_NS

Viewport::Viewport(Layout* layout, int viewport_index, std::string id, SceneHandleBase scene_handle) : Painter(this),
    layout(layout),
    viewport_index(viewport_index),
    viewport_id(std::move(id)),
    insertion_order(static_cast<std::uint64_t>(viewport_index)),
    scene_handle(std::move(scene_handle))
{
}

Viewport::~Viewport()
{
    detachScene();
}

void Viewport::resolveScene()
{
    SceneBase* resolved = scene_handle.get();
    if (!resolved)
        throw std::logic_error("Cannot mount a Viewport before its Scene is materialized");

    if (scene == resolved)
        return;

    detachScene();
    scene = resolved;
    scene->registerMount(this);
}

void Viewport::notifyMounted()
{
    if (!scene || mount_notified)
        return;

    scene->sceneMounted(this);
    scene->requestRedraw(true);
    mount_notified = true;
}

void Viewport::detachScene()
{
    if (!scene)
        return;

    if (mount_notified)
        scene->sceneUnmounted(this);

    scene->registerUnmount(this);
    scene = nullptr;
    mount_notified = false;
}

void Viewport::setPlacement(ViewportPlacement placement)
{
    if (layout->project->shouldDeferStructuralMutation())
    {
        Layout* owner = layout;
        const std::uint64_t ordinal = insertion_order;
        layout->project->enqueueStructuralMutation([owner, ordinal, placement = std::move(placement)]() mutable
        {
            if (Viewport* viewport = owner->findViewport(ordinal))
                viewport->setPlacementImmediate(std::move(placement));
        });
        return;
    }

    setPlacementImmediate(std::move(placement));
}

void Viewport::setVisible(bool value)
{
    if (layout->project->shouldDeferStructuralMutation())
    {
        Layout* owner = layout;
        const std::uint64_t ordinal = insertion_order;
        layout->project->enqueueStructuralMutation([owner, ordinal, value]()
        {
            if (Viewport* viewport = owner->findViewport(ordinal))
                viewport->setVisibleImmediate(value);
        });
        return;
    }

    setVisibleImmediate(value);
}

void Viewport::setZIndex(int value)
{
    if (layout->project->shouldDeferStructuralMutation())
    {
        Layout* owner = layout;
        const std::uint64_t ordinal = insertion_order;
        layout->project->enqueueStructuralMutation([owner, ordinal, value]()
        {
            if (Viewport* viewport = owner->findViewport(ordinal))
                viewport->setZIndexImmediate(value);
        });
        return;
    }

    setZIndexImmediate(value);
}

void Viewport::setPointerEvents(PointerEvents value)
{
    if (layout->project->shouldDeferStructuralMutation())
    {
        Layout* owner = layout;
        const std::uint64_t ordinal = insertion_order;
        layout->project->enqueueStructuralMutation([owner, ordinal, value]()
        {
            if (Viewport* viewport = owner->findViewport(ordinal))
                viewport->setPointerEventsImmediate(value);
        });
        return;
    }

    setPointerEventsImmediate(value);
}

void Viewport::setRenderSize(IVec2 value)
{
    if (value.x <= 0 || value.y <= 0)
        throw std::invalid_argument("Viewport render size must be positive");

    if (layout->project->shouldDeferStructuralMutation())
    {
        Layout* owner = layout;
        const std::uint64_t ordinal = insertion_order;
        layout->project->enqueueStructuralMutation([owner, ordinal, value]()
        {
            if (Viewport* viewport = owner->findViewport(ordinal))
                viewport->setRenderSizeImmediate(value);
        });
        return;
    }

    setRenderSizeImmediate(value);
}

void Viewport::setPlacementImmediate(ViewportPlacement placement)
{
    placement_policy = std::move(placement);
    layout->markGeometryDirty();
}

void Viewport::setVisibleImmediate(bool value)
{
    if (visible == value)
        return;
    visible = value;
    if (!visible)
        layout->project->releaseViewportReferences(this);
    layout->markGeometryDirty();
}

void Viewport::setZIndexImmediate(int value)
{
    if (z_index == value)
        return;
    z_index = value;
    layout->markOrderingDirty();
}

void Viewport::setPointerEventsImmediate(PointerEvents value)
{
    if (pointer_events == value)
        return;
    pointer_events = value;
    if (pointer_events == PointerEvents::None)
        layout->project->releaseViewportReferences(this);
}

void Viewport::setRenderSizeImmediate(IVec2 value)
{
    render_size = value;
    layout->markGeometryDirty();
}

void Viewport::draw()
{
    if (!scene)
        return;

    // Set defaults
    setFont(getDefaultFont());
    setTextAlign(TextAlign::ALIGN_LEFT);
    setTextBaseline(TextBaseline::BASELINE_TOP);

    // Snapshot default transformation (unscaled unrotated top-left viewport)
    default_viewport_transform = SimplePainter::currentTransform();

    print_stream.str("");
    print_stream.clear();

    // Draw mounted project to this viewport
    scene->viewportDraw(this);

    // Draw print() lines at top-left of viewport
    save();
    saveCameraTransform();
    stageMode();
    setTextAlign(TextAlign::ALIGN_LEFT);
    setTextBaseline(TextBaseline::BASELINE_TOP);
    setFillStyle(255, 255, 255);

    print_stream.seekg(0);
    int line_index = 0;
    std::string line;
    while (std::getline(print_stream, line, '\n'))
        fillText(line, 5.0, 5.0 + (line_index++ * getGlobalScale() * 16.0f));

    restoreCameraTransform();
    restore();
}

[[nodiscard]] IVec2 bl::Viewport::outputSize() const
{
    IVec2 ret = size();
    if (!scene)
        return ret;

    const CaptureManager& capture_manager = scene->project->captureContext().manager();
    if (capture_manager.isRecording() || capture_manager.isSnapshotting())
        ret = capture_manager.resolution();

    return ret;
}

BL_END_NS
