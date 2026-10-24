#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <bitloop/core/scene_handle.h>
#include <bitloop/core/layout_solver.h>
#include <bitloop/platform/platform_macros.h>

BL_BEGIN_NS

class ProjectBase;
class Viewport;

template<typename Iterator, typename Pointer>
class ViewportIterator
{
    Iterator iterator;

public:

    explicit ViewportIterator(Iterator iterator) : iterator(iterator)
    {
    }

    [[nodiscard]] Pointer operator*() const { return iterator->get(); }
    ViewportIterator& operator++() { ++iterator; return *this; }

    friend bool operator==(const ViewportIterator&, const ViewportIterator&) = default;
};

class Layout
{
    std::vector<std::unique_ptr<Viewport>> viewports;
    std::vector<std::unique_ptr<Viewport>> pending_viewports;
    ProjectBase* project = nullptr;
    std::uint64_t next_viewport_ordinal = 0;
    GridLayout automatic_layout;
    bool geometry_dirty = true;
    mutable std::vector<Viewport*> ordered_viewports;
    mutable bool ordering_dirty = true;

    friend class ProjectBase;
    friend class Viewport;

    void resolveScenes();
    bool resolveGeometry(DVec2 surface_size, DRect client_rect);
    void activatePendingViewport(std::uint64_t insertion_order);
    void removeImmediate(std::uint64_t insertion_order);
    void clearImmediate(bool reset_ordinal);
    void removeSceneViewports(const SceneHandleBase& scene);
    void setAutoLayoutImmediate(GridLayout layout);
    [[nodiscard]] Viewport* findViewport(std::uint64_t insertion_order);
    [[nodiscard]] const Viewport* findViewport(std::uint64_t insertion_order) const;
    void markGeometryDirty() { geometry_dirty = true; }
    void markOrderingDirty() { ordering_dirty = true; }
    [[nodiscard]] const std::vector<Viewport*>& orderedViewports() const;

public:

    using iterator = ViewportIterator<std::vector<std::unique_ptr<Viewport>>::iterator, Viewport*>;
    using const_iterator = ViewportIterator<std::vector<std::unique_ptr<Viewport>>::const_iterator, const Viewport*>;

    Layout() = default;
    ~Layout();

    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;

    Viewport& add(SceneHandleBase scene, std::string id = {});
    void remove(Viewport& viewport);
    void clear();
    void setAutoLayout(GridLayout layout);

    [[nodiscard]] const GridLayout& autoLayout() const { return automatic_layout; }

    Layout& operator<<(SceneHandleBase scene);

    [[nodiscard]] Viewport* operator[](std::size_t index) { return viewports.at(index).get(); }
    [[nodiscard]] const Viewport* operator[](std::size_t index) const { return viewports.at(index).get(); }

    [[nodiscard]] iterator begin() { return iterator(viewports.begin()); }
    [[nodiscard]] iterator end() { return iterator(viewports.end()); }
    [[nodiscard]] const_iterator begin() const { return const_iterator(viewports.begin()); }
    [[nodiscard]] const_iterator end() const { return const_iterator(viewports.end()); }

    [[nodiscard]] int count() const { return static_cast<int>(viewports.size()); }
};

BL_END_NS
