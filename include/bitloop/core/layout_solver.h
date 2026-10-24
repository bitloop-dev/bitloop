#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <bitloop/core/types.h>

BL_BEGIN_NS

enum class ViewportPlacementMode
{
    Auto,
    Fill,
    Normalized,
    Anchored
};

enum class PointerEvents
{
    Auto,
    None
};

struct GridLayout
{
    std::optional<int> columns;
    std::optional<int> rows;
    double gap = 6.0;
};

class ViewportPlacement
{
    ViewportPlacementMode placement_mode = ViewportPlacementMode::Auto;
    DRect rect{};
    Anchor anchor_value = Anchor::TOP_LEFT;
    DVec2 anchored_size{};
    DVec2 anchored_margin{};

public:

    [[nodiscard]] static ViewportPlacement automatic();
    [[nodiscard]] static ViewportPlacement fill();
    [[nodiscard]] static ViewportPlacement normalized(DRect rect);
    [[nodiscard]] static ViewportPlacement anchored(Anchor anchor, DVec2 size, DVec2 margin = {});

    [[nodiscard]] ViewportPlacementMode mode() const { return placement_mode; }
    [[nodiscard]] DRect normalizedRect() const { return rect; }
    [[nodiscard]] Anchor anchor() const { return anchor_value; }
    [[nodiscard]] DVec2 size() const { return anchored_size; }
    [[nodiscard]] DVec2 margin() const { return anchored_margin; }
};

struct ViewportLayoutInput
{
    ViewportPlacement placement;
    bool visible = true;
};

class LayoutSolver
{
public:

    [[nodiscard]] static std::vector<DRect> solve(
        DVec2 surface_size,
        const GridLayout& grid,
        std::span<const ViewportLayoutInput> viewports);

    [[nodiscard]] static DRect mapSurfaceToClient(DRect surface_rect, DVec2 surface_size, DRect client_rect);
};

BL_END_NS
