#include <bitloop/core/layout_solver.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

BL_BEGIN_NS

namespace
{
    bool finiteRect(DRect rect)
    {
        return std::isfinite(rect.x1) && std::isfinite(rect.y1) && std::isfinite(rect.x2) && std::isfinite(rect.y2);
    }

    void requirePositiveRect(DRect rect, std::string_view label)
    {
        if (!finiteRect(rect) || rect.x2 <= rect.x1 || rect.y2 <= rect.y1)
            throw std::invalid_argument(std::string(label) + " must be finite with positive width and height");
    }

    DVec2 anchorRatio(Anchor anchor)
    {
        switch (anchor)
        {
            case Anchor::TOP_LEFT:     return { 0.0, 0.0 };
            case Anchor::TOP:          return { 0.5, 0.0 };
            case Anchor::TOP_RIGHT:    return { 1.0, 0.0 };
            case Anchor::LEFT:         return { 0.0, 0.5 };
            case Anchor::CENTER:       return { 0.5, 0.5 };
            case Anchor::RIGHT:        return { 1.0, 0.5 };
            case Anchor::BOTTOM_LEFT:  return { 0.0, 1.0 };
            case Anchor::BOTTOM:       return { 0.5, 1.0 };
            case Anchor::BOTTOM_RIGHT: return { 1.0, 1.0 };
        }
        throw std::invalid_argument("Unknown Viewport anchor");
    }

    DRect solveAnchored(DVec2 surface_size, const ViewportPlacement& placement)
    {
        const DVec2 size = placement.size();
        const DVec2 margin = placement.margin();
        const DVec2 ratio = anchorRatio(placement.anchor());

        DVec2 position;
        position.x = ratio.x == 0.0 ? margin.x : ratio.x == 1.0 ? surface_size.x - size.x - margin.x : (surface_size.x - size.x) * 0.5 + margin.x;
        position.y = ratio.y == 0.0 ? margin.y : ratio.y == 1.0 ? surface_size.y - size.y - margin.y : (surface_size.y - size.y) * 0.5 + margin.y;
        return DRect(position, position + size);
    }

    std::pair<int, int> gridDimensions(int count, const GridLayout& grid)
    {
        if (count <= 0)
            return { 0, 0 };

        if (grid.columns && *grid.columns <= 0)
            throw std::invalid_argument("GridLayout columns must be positive");
        if (grid.rows && *grid.rows <= 0)
            throw std::invalid_argument("GridLayout rows must be positive");

        if (grid.columns && grid.rows)
        {
            if (*grid.columns * *grid.rows < count)
                throw std::invalid_argument("GridLayout does not have enough cells for its automatic Viewports");
            return { *grid.columns, *grid.rows };
        }

        if (grid.columns)
            return { *grid.columns, static_cast<int>(std::ceil(static_cast<double>(count) / *grid.columns)) };
        if (grid.rows)
            return { static_cast<int>(std::ceil(static_cast<double>(count) / *grid.rows)), *grid.rows };

        int rows = static_cast<int>(std::sqrt(count));
        int columns = count / rows;
        if (count > rows * columns)
            rows++;
        return { columns, rows };
    }
}

ViewportPlacement ViewportPlacement::automatic()
{
    return {};
}

ViewportPlacement ViewportPlacement::fill()
{
    ViewportPlacement placement;
    placement.placement_mode = ViewportPlacementMode::Fill;
    return placement;
}

ViewportPlacement ViewportPlacement::normalized(DRect rect)
{
    requirePositiveRect(rect, "Normalized Viewport rectangle");

    ViewportPlacement placement;
    placement.placement_mode = ViewportPlacementMode::Normalized;
    placement.rect = rect;
    return placement;
}

ViewportPlacement ViewportPlacement::anchored(Anchor anchor, DVec2 size, DVec2 margin)
{
    if (!size.isFinite() || size.x <= 0.0 || size.y <= 0.0)
        throw std::invalid_argument("Anchored Viewport size must be finite and positive");
    if (!margin.isFinite())
        throw std::invalid_argument("Anchored Viewport margin must be finite");

    ViewportPlacement placement;
    placement.placement_mode = ViewportPlacementMode::Anchored;
    placement.anchor_value = anchor;
    placement.anchored_size = size;
    placement.anchored_margin = margin;
    return placement;
}

std::vector<DRect> LayoutSolver::solve(DVec2 surface_size, const GridLayout& grid, std::span<const ViewportLayoutInput> viewports)
{
    if (!surface_size.isFinite() || surface_size.x <= 0.0 || surface_size.y <= 0.0)
        throw std::invalid_argument("Layout surface size must be finite and positive");
    if (!std::isfinite(grid.gap) || grid.gap < 0.0)
        throw std::invalid_argument("GridLayout gap must be finite and non-negative");

    const int automatic_count = static_cast<int>(std::count_if(viewports.begin(), viewports.end(), [](const ViewportLayoutInput& viewport)
    {
        return viewport.visible && viewport.placement.mode() == ViewportPlacementMode::Auto;
    }));
    const auto [columns, rows] = gridDimensions(automatic_count, grid);

    double cell_width = 0.0;
    double cell_height = 0.0;
    if (automatic_count > 0)
    {
        const double usable_width = surface_size.x - (columns - 1) * grid.gap;
        const double usable_height = surface_size.y - (rows - 1) * grid.gap;
        if (usable_width <= 0.0 || usable_height <= 0.0)
            throw std::invalid_argument("GridLayout gaps leave no positive space for automatic Viewports");
        cell_width = usable_width / columns;
        cell_height = usable_height / rows;
    }

    std::vector<DRect> result(viewports.size());
    int automatic_index = 0;
    for (std::size_t index = 0; index < viewports.size(); ++index)
    {
        const ViewportLayoutInput& viewport = viewports[index];
        if (!viewport.visible)
            continue;

        switch (viewport.placement.mode())
        {
            case ViewportPlacementMode::Auto:
            {
                const int column = automatic_index % columns;
                const int row = automatic_index / columns;
                const double x = std::floor(column * (cell_width + grid.gap));
                const double y = std::floor(row * (cell_height + grid.gap));
                result[index] = DRect(x, y, x + std::ceil(cell_width), y + std::ceil(cell_height));
                automatic_index++;
                break;
            }

            case ViewportPlacementMode::Fill:
                result[index] = DRect(0.0, 0.0, surface_size.x, surface_size.y);
                break;

            case ViewportPlacementMode::Normalized:
            {
                const DRect normalized = viewport.placement.normalizedRect();
                result[index] = DRect(
                    normalized.x1 * surface_size.x,
                    normalized.y1 * surface_size.y,
                    normalized.x2 * surface_size.x,
                    normalized.y2 * surface_size.y);
                break;
            }

            case ViewportPlacementMode::Anchored:
                result[index] = solveAnchored(surface_size, viewport.placement);
                break;
        }
    }

    return result;
}

DRect LayoutSolver::mapSurfaceToClient(DRect surface_rect, DVec2 surface_size, DRect client_rect)
{
    requirePositiveRect(surface_rect, "Viewport surface rectangle");
    requirePositiveRect(client_rect, "Layout client rectangle");
    if (!surface_size.isFinite() || surface_size.x <= 0.0 || surface_size.y <= 0.0)
        throw std::invalid_argument("Layout surface size must be finite and positive");

    const DVec2 scale = client_rect.size() / surface_size;
    return DRect(
        client_rect.x1 + surface_rect.x1 * scale.x,
        client_rect.y1 + surface_rect.y1 * scale.y,
        client_rect.x1 + surface_rect.x2 * scale.x,
        client_rect.y1 + surface_rect.y2 * scale.y);
}

BL_END_NS
