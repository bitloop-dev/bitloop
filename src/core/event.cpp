#include <bitloop/core/event.h>
#include <bitloop/platform/platform.h>
#include <bitloop/core/viewport.h>
#include <bitloop/core/scene.h>

BL_BEGIN_NS

namespace
{
    IVec2 pointerSurfaceSizeOrPlatform(const Event& event)
    {
        IVec2 size = event.pointerSurfaceSize();
        if (size.x <= 0)
            size.x = platform()->fboWidth();
        if (size.y <= 0)
            size.y = platform()->fboHeight();
        return size;
    }
}

std::string Event::toString()
{
    std::string focused = _focused_ctx ? std::to_string(_focused_ctx->viewportIndex()) : "_";
    std::string hovered = _hovered_ctx ? std::to_string(_hovered_ctx->viewportIndex()) : "_";
    std::string type;

    char attribs[256]{};
    
    switch (event_data.type)
    {
    case EventType::KEY_DOWN:           type = "SDL_KEYDOWN";          goto key_attribs;
    case EventType::KEY_UP:             type = "SDL_KEYUP";            goto key_attribs;
        key_attribs:
        snprintf(attribs, sizeof(attribs), "{%s}", event_data.key.name.c_str());

        break;

    case EventType::MOUSE_MOTION:       type = "SDL_MOUSEMOTION";
        snprintf(attribs, sizeof(attribs), "{%.1f, %.1f}",
            event_data.motion.x,
            event_data.motion.y);

        break;

    case EventType::MOUSE_BUTTON_DOWN:  type = "SDL_MOUSEBUTTONDOWN";  goto mouse_attribs;
    case EventType::MOUSE_BUTTON_UP:    type = "SDL_MOUSEBUTTONUP";    goto mouse_attribs;
        mouse_attribs:
        snprintf(attribs, sizeof(attribs), "{%.1f, %.1f}",
            event_data.button.x,
            event_data.button.y);

        break;

    case EventType::FINGER_DOWN:        type = "SDL_FINGERDOWN";    goto finger_attribs;
    case EventType::FINGER_UP:          type = "SDL_FINGERUP";      goto finger_attribs;
    case EventType::FINGER_MOTION:      type = "SDL_FINGERMOTION";
        finger_attribs:
        {
        const IVec2 surface_size = pointerSurfaceSizeOrPlatform(*this);
        snprintf(attribs, sizeof(attribs), "{F_%llu, %d, %d}",
            (unsigned long long) event_data.tfinger.fingerID,
            (int)(event_data.tfinger.x * (float)surface_size.x),
            (int)(event_data.tfinger.y * (float)surface_size.y));
        }

        break;

    case EventType::MOUSE_WHEEL:        type = "SDL_MOUSEWHEEL";       break;
    case EventType::TEXT_INPUT:         type = "SDL_TEXTINPUT";        break;

    default:                           type = "UNKNOWN_EVENT";        break;
    }

    std::string info;
    info += "[focused: " + focused + "] ";
    info += "[hovered: " + hovered + "] ";
    info += "[" + type + "] ";
    info += attribs;

    return info;
}

double PointerEvent::x()
{
    switch (event_data.type)
    {
    case EventType::FINGER_DOWN:
    case EventType::FINGER_UP:
    case EventType::FINGER_MOTION:
    {
        const IVec2 surface_size = pointerSurfaceSizeOrPlatform(*this);
        double client_x = (double)(event_data.tfinger.x * (float)surface_size.x);
        return ctx_owner()->toSurfaceX(client_x);
    }

    case EventType::MOUSE_BUTTON_DOWN:
    case EventType::MOUSE_BUTTON_UP:
        return ctx_owner()->toSurfaceX((double)event_data.button.x);

    case EventType::MOUSE_MOTION:
        return ctx_owner()->toSurfaceX((double)event_data.motion.x);
    }
    return 0;
}

double PointerEvent::y()
{
    switch (event_data.type)
    {
    case EventType::FINGER_DOWN:
    case EventType::FINGER_UP:
    case EventType::FINGER_MOTION:
    {
        const IVec2 surface_size = pointerSurfaceSizeOrPlatform(*this);
        double client_y = (double)(event_data.tfinger.y * (float)surface_size.y);
        return ctx_owner()->toSurfaceY(client_y);
    }

    case EventType::MOUSE_BUTTON_DOWN:
    case EventType::MOUSE_BUTTON_UP:
        return ctx_owner()->toSurfaceY((double)event_data.button.y);

    case EventType::MOUSE_MOTION:
        return ctx_owner()->toSurfaceY((double)event_data.motion.y);
    }
    return 0;
}

DVec2 PointerEvent::worldPosition(const CameraInfo& camera)
{
    assert(ctx_owner());
    return camera.transformFor(*ctx_owner()).toWorld<f64>(x(), y());
}

bool Event::ownedBy(const SceneBase* scene)
{
    return (ctx_owner() && ctx_owner()->mountedScene() == scene);
}

bool PointerEvent::hoveredOver(const SceneBase* scene)
{
    return (ctx_hovered() && ctx_hovered()->mountedScene() == scene);
    //auto& viewports = scene->mountedToViewports();
    //auto it = std::find(viewports.begin(), viewports.end(), ctx_hovered());
    //return (it != viewports.end());
}

BL_END_NS
