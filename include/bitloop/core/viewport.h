#pragma once

#include <sstream>
#include <string>

#include <bitloop/core/layout_solver.h>
#include <bitloop/core/scene_handle.h>
#include <bitloop/nanovgx/nano_canvas.h>

BL_BEGIN_NS

class ProjectBase;
class SceneBase;
class Layout;

class Viewport : public Painter, public SurfaceInfo
{
    std::string print_text;
    std::stringstream print_stream;

protected:

    friend class ProjectBase;
    friend class SceneBase;
    friend class Layout;

    int viewport_index;
    std::string viewport_id;
    std::uint64_t insertion_order = 0;
    ViewportPlacement placement_policy = ViewportPlacement::automatic();
    IVec2 render_size{};
    int z_index = 0;
    PointerEvents pointer_events = PointerEvents::Auto;
    bool visible = true;

    Layout* layout = nullptr; // owner
    SceneHandleBase scene_handle;
    SceneBase* scene = nullptr;
    bool mount_notified = false;

    static inline float focus_flash_frames = 20.0f;
    float focused_dt = 0;

    void resolveScene();
    void notifyMounted();
    void detachScene();
    void setPlacementImmediate(ViewportPlacement placement);
    void setVisibleImmediate(bool value);
    void setZIndexImmediate(int value);
    void setPointerEventsImmediate(PointerEvents value);
    void setRenderSizeImmediate(IVec2 value);

public:
    
    Viewport(Layout* layout, int viewport_index, std::string id, SceneHandleBase scene);

    ~Viewport();

    void draw();

    [[nodiscard]] const std::string& id() const { return viewport_id; }
    [[nodiscard]] int viewportIndex() const { return viewport_index; }
    [[nodiscard]] std::uint64_t insertionOrder() const { return insertion_order; }

    void setPlacement(ViewportPlacement placement);
    void setVisible(bool value);
    void setZIndex(int value);
    void setPointerEvents(PointerEvents value);
    void setRenderSize(IVec2 value);

    [[nodiscard]] const ViewportPlacement& placement() const { return placement_policy; }
    [[nodiscard]] bool isVisible() const { return visible; }
    [[nodiscard]] int zIndex() const { return z_index; }
    [[nodiscard]] PointerEvents pointerEvents() const { return pointer_events; }
    [[nodiscard]] IVec2 renderSize() const { return render_size; }

    //[[nodiscard]] double posX() const { return x; }
    //[[nodiscard]] double posY() const { return y; }

    [[nodiscard]] IVec2 outputSize() const;

    [[nodiscard]] WorldStageTransform worldStageTransform() const
    {
        return Painter::worldStageTransform();
    }

    template<typename T=double> [[nodiscard]] Vec2<T> worldSize() const { return worldStageTransform().toWorldOffset<T>(width(), height()); }
    template<typename T=double> [[nodiscard]] Quad<T> worldQuad() const {
        Rect<T> r = Rect<T>(Vec2<T>(T{0},T{0}), Vec2<T>(size()));
        return worldStageTransform().toWorldQuad(r);
    }
    //[[nodiscard]] DAngledRect worldRect() { return camera.toWorldRect(DAngledRect(width/2, height/2, width, height, 0.0)); }



    [[nodiscard]] SceneBase* mountedScene() { return scene; }
    [[nodiscard]] const SceneBase* mountedScene() const { return scene; }

    // Viewport-specific draw helpers (i.e. size of viewport needed)
    ///void printTouchInfo();

    std::stringstream& print() {
        return print_stream;
    }
};

BL_END_NS
