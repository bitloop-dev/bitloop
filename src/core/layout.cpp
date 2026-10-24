#include <bitloop/core/layout.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include <bitloop/core/project.h>
#include <bitloop/core/viewport.h>

BL_BEGIN_NS

namespace
{
    bool validIdentifier(std::string_view id)
    {
        if (id.empty())
            return false;

        return std::all_of(id.begin(), id.end(), [](unsigned char value)
        {
            return std::isalnum(value) || value == '-' || value == '_' || value == '.';
        });
    }
}

Layout::~Layout()
{
    clearImmediate(true);
}

Viewport& Layout::add(SceneHandleBase scene, std::string id)
{
    if (project && project->tearing_down)
        throw std::logic_error("Viewports cannot be added while the Project is tearing down");
    if (!project || !project->ownsSceneHandle(scene))
        throw std::invalid_argument("Viewport Scene handle does not belong to this Project generation");

    const std::uint64_t ordinal = next_viewport_ordinal++;
    if (id.empty())
        id = "viewport-" + std::to_string(ordinal);
    else if (!validIdentifier(id))
        throw std::invalid_argument("Viewport ID must contain only letters, digits, '.', '_' or '-'");

    auto has_id = [&](const std::vector<std::unique_ptr<Viewport>>& collection)
    {
        return std::any_of(collection.begin(), collection.end(), [&](const std::unique_ptr<Viewport>& viewport)
        {
            return viewport->id() == id;
        });
    };
    if (has_id(viewports) || has_id(pending_viewports))
        throw std::invalid_argument("Duplicate Viewport ID: " + id);

    auto viewport = std::make_unique<Viewport>(this, static_cast<int>(ordinal), std::move(id), std::move(scene));
    Viewport& result = *viewport;

    if (project->shouldDeferStructuralMutation())
    {
        pending_viewports.push_back(std::move(viewport));
        project->enqueueStructuralMutation([this, ordinal]() { activatePendingViewport(ordinal); });
    }
    else
    {
        viewports.push_back(std::move(viewport));
        geometry_dirty = true;
        ordering_dirty = true;
    }

    return result;
}

void Layout::remove(Viewport& viewport)
{
    const bool owned = std::any_of(viewports.begin(), viewports.end(), [&](const std::unique_ptr<Viewport>& candidate)
    {
        return candidate.get() == &viewport;
    }) || std::any_of(pending_viewports.begin(), pending_viewports.end(), [&](const std::unique_ptr<Viewport>& candidate)
    {
        return candidate.get() == &viewport;
    });

    if (!owned)
        return;

    const std::uint64_t ordinal = viewport.insertionOrder();
    if (project->shouldDeferStructuralMutation())
        project->enqueueStructuralMutation([this, ordinal]() { removeImmediate(ordinal); });
    else
        removeImmediate(ordinal);
}

void Layout::clear()
{
    if (!project || !project->shouldDeferStructuralMutation())
    {
        clearImmediate(true);
        return;
    }

    std::vector<std::uint64_t> ordinals;
    ordinals.reserve(viewports.size() + pending_viewports.size());
    for (const std::unique_ptr<Viewport>& viewport : viewports)
        ordinals.push_back(viewport->insertionOrder());
    for (const std::unique_ptr<Viewport>& viewport : pending_viewports)
        ordinals.push_back(viewport->insertionOrder());

    project->enqueueStructuralMutation([this, ordinals = std::move(ordinals)]()
    {
        for (std::uint64_t ordinal : ordinals)
            removeImmediate(ordinal);
    });
}

void Layout::setAutoLayout(GridLayout layout)
{
    if (layout.columns && *layout.columns <= 0)
        throw std::invalid_argument("GridLayout columns must be positive");
    if (layout.rows && *layout.rows <= 0)
        throw std::invalid_argument("GridLayout rows must be positive");
    if (!std::isfinite(layout.gap) || layout.gap < 0.0)
        throw std::invalid_argument("GridLayout gap must be finite and non-negative");

    if (project && project->shouldDeferStructuralMutation())
        project->enqueueStructuralMutation([this, layout = std::move(layout)]() mutable { setAutoLayoutImmediate(std::move(layout)); });
    else
        setAutoLayoutImmediate(std::move(layout));
}

Layout& Layout::operator<<(SceneHandleBase scene)
{
    add(std::move(scene));
    return *this;
}

void Layout::resolveScenes()
{
    for (auto it = viewports.begin(); it != viewports.end();)
    {
        if (!(*it)->scene_handle.ready())
        {
            project->releaseViewportReferences(it->get());
            it = viewports.erase(it);
            ordering_dirty = true;
            continue;
        }

        (*it)->resolveScene();
        ++it;
    }

    if (!project->ctx_focused)
    {
        const std::vector<Viewport*>& ordered = orderedViewports();
        auto focused = std::find_if(ordered.rbegin(), ordered.rend(), [](const Viewport* viewport)
        {
            return viewport->isVisible() && viewport->pointerEvents() == PointerEvents::Auto;
        });
        if (focused != ordered.rend())
            project->ctx_focused = *focused;
    }
}

Viewport* Layout::findViewport(std::uint64_t insertion_order)
{
    const auto find = [&](auto& collection) -> Viewport*
    {
        auto found = std::find_if(collection.begin(), collection.end(), [&](const std::unique_ptr<Viewport>& viewport)
        {
            return viewport->insertionOrder() == insertion_order;
        });
        return found != collection.end() ? found->get() : nullptr;
    };

    if (Viewport* viewport = find(viewports))
        return viewport;
    return find(pending_viewports);
}

const Viewport* Layout::findViewport(std::uint64_t insertion_order) const
{
    return const_cast<Layout*>(this)->findViewport(insertion_order);
}

void Layout::activatePendingViewport(std::uint64_t insertion_order)
{
    auto found = std::find_if(pending_viewports.begin(), pending_viewports.end(), [&](const std::unique_ptr<Viewport>& viewport)
    {
        return viewport->insertionOrder() == insertion_order;
    });
    if (found == pending_viewports.end())
        return;

    if (!(*found)->scene_handle.ready())
    {
        pending_viewports.erase(found);
        return;
    }

    std::unique_ptr<Viewport> viewport = std::move(*found);
    pending_viewports.erase(found);
    viewport->resolveScene();
    viewports.push_back(std::move(viewport));
    geometry_dirty = true;
    ordering_dirty = true;
}

void Layout::removeImmediate(std::uint64_t insertion_order)
{
    auto active = std::find_if(viewports.begin(), viewports.end(), [&](const std::unique_ptr<Viewport>& viewport)
    {
        return viewport->insertionOrder() == insertion_order;
    });
    if (active != viewports.end())
    {
        if (project)
            project->releaseViewportReferences(active->get());
        viewports.erase(active);
        geometry_dirty = true;
        ordering_dirty = true;
        return;
    }

    auto pending = std::find_if(pending_viewports.begin(), pending_viewports.end(), [&](const std::unique_ptr<Viewport>& viewport)
    {
        return viewport->insertionOrder() == insertion_order;
    });
    if (pending != pending_viewports.end())
        pending_viewports.erase(pending);
}

void Layout::clearImmediate(bool reset_ordinal)
{
    if (project)
    {
        for (const std::unique_ptr<Viewport>& viewport : viewports)
            project->releaseViewportReferences(viewport.get());
    }

    viewports.clear();
    pending_viewports.clear();
    if (reset_ordinal)
        next_viewport_ordinal = 0;
    geometry_dirty = true;
    ordered_viewports.clear();
    ordering_dirty = true;
}

void Layout::removeSceneViewports(const SceneHandleBase& scene)
{
    auto matches = [&](const std::unique_ptr<Viewport>& viewport)
    {
        return viewport->scene_handle.token == scene.token
            && viewport->scene_handle.generation == scene.generation
            && viewport->scene_handle.state.lock() == scene.state.lock();
    };

    for (auto it = viewports.begin(); it != viewports.end();)
    {
        if (!matches(*it))
        {
            ++it;
            continue;
        }

        if (project)
            project->releaseViewportReferences(it->get());
        it = viewports.erase(it);
    }

    std::erase_if(pending_viewports, matches);
    geometry_dirty = true;
    ordering_dirty = true;
}

void Layout::setAutoLayoutImmediate(GridLayout layout)
{
    automatic_layout = std::move(layout);
    geometry_dirty = true;
}

const std::vector<Viewport*>& Layout::orderedViewports() const
{
    if (!ordering_dirty)
        return ordered_viewports;

    ordered_viewports.clear();
    ordered_viewports.reserve(viewports.size());
    for (const std::unique_ptr<Viewport>& viewport : viewports)
        ordered_viewports.push_back(viewport.get());

    std::stable_sort(ordered_viewports.begin(), ordered_viewports.end(), [](const Viewport* lhs, const Viewport* rhs)
    {
        if (lhs->zIndex() != rhs->zIndex())
            return lhs->zIndex() < rhs->zIndex();
        return lhs->insertionOrder() < rhs->insertionOrder();
    });

    ordering_dirty = false;
    return ordered_viewports;
}

bool Layout::resolveGeometry(DVec2 surface_size, DRect client_rect)
{
    std::vector<ViewportLayoutInput> inputs;
    inputs.reserve(viewports.size());
    for (const std::unique_ptr<Viewport>& viewport : viewports)
        inputs.push_back({ viewport->placement_policy, viewport->visible });

    const std::vector<DRect> rectangles = LayoutSolver::solve(surface_size, automatic_layout, inputs);
    bool changed = geometry_dirty;

    for (std::size_t index = 0; index < viewports.size(); ++index)
    {
        Viewport& viewport = *viewports[index];
        const DRect old_surface = viewport.surfaceRect();
        const DRect old_client = viewport.clientRect();

        DRect surface_rect;
        DRect resolved_client;
        if (viewport.visible)
        {
            surface_rect = rectangles[index];
            resolved_client = LayoutSolver::mapSurfaceToClient(surface_rect, surface_size, client_rect);
        }
        else
        {
            const DVec2 hidden_size = viewport.render_size.x > 0 && viewport.render_size.y > 0
                ? static_cast<DVec2>(viewport.render_size)
                : surface_size;
            surface_rect = DRect(0.0, 0.0, hidden_size.x, hidden_size.y);
            resolved_client = surface_rect;
        }

        viewport.setSurfacePos(surface_rect.x1, surface_rect.y1);
        viewport.setSurfaceSize(surface_rect.width(), surface_rect.height());
        viewport.setClientRect(resolved_client.x1, resolved_client.y1, resolved_client.width(), resolved_client.height());

        changed = changed
            || old_surface.x1 != surface_rect.x1 || old_surface.y1 != surface_rect.y1
            || old_surface.x2 != surface_rect.x2 || old_surface.y2 != surface_rect.y2
            || old_client.x1 != resolved_client.x1 || old_client.y1 != resolved_client.y1
            || old_client.x2 != resolved_client.x2 || old_client.y2 != resolved_client.y2;
    }

    geometry_dirty = false;
    return changed;
}

BL_END_NS
