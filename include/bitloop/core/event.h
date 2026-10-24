#pragma once
#include <string>
#include <utility>

#include <bitloop/core/event_data.h>
#include <bitloop/core/types.h>

BL_BEGIN_NS

class ProjectBase;
class SceneBase;
class CameraInfo;
class Viewport;

class PointerEvent;

class Event
{
    friend class ProjectBase;

    friend class PointerEvent;
    friend class KeyEvent;

protected:
    Viewport* _focused_ctx = nullptr;
    Viewport* _hovered_ctx = nullptr;
    Viewport* _owner_ctx = nullptr;
    IVec2 _pointer_surface_size{ 0, 0 };

    EventData event_data;

    void setFocusedViewport(Viewport* ctx) { _focused_ctx = ctx; }
    void setHoveredViewport(Viewport* ctx) { _hovered_ctx = ctx; }
    void setOwnerViewport(Viewport* ctx)   { _owner_ctx = ctx; }
    void setPointerSurfaceSize(IVec2 size) { _pointer_surface_size = size; }

public:

    // Pointer = mouse/touch
    [[nodiscard]] bool isPointerEvent() const
    {
        switch (event_data.type)
        {
        case EventType::FINGER_DOWN:
        case EventType::FINGER_UP:
        case EventType::FINGER_MOTION:

        case EventType::MOUSE_BUTTON_DOWN:
        case EventType::MOUSE_BUTTON_UP:
        case EventType::MOUSE_MOTION:
        case EventType::MOUSE_WHEEL:
            return true;
        }
        return false;
    }

    [[nodiscard]] bool isFingerEvent() const
    {
        switch (event_data.type)
        {
        case EventType::FINGER_DOWN:
        case EventType::FINGER_UP:
        case EventType::FINGER_MOTION:
            return true;
        }
        return false;
    }

    [[nodiscard]] bool isPressEvent() const
    {
        switch (event_data.type)
        {
        case EventType::FINGER_DOWN:
        case EventType::MOUSE_BUTTON_DOWN:
            return true;
        }
        return false;
    }

    Event(EventData data) : event_data(std::move(data)) {}
    Event(const Event& e) = default;

    [[nodiscard]] auto type() const             { return event_data.type; }
    [[nodiscard]] Viewport* ctx_focused() const { return _focused_ctx; }
    [[nodiscard]] Viewport* ctx_hovered() const { return _hovered_ctx; }
    [[nodiscard]] Viewport* ctx_owner()   const { return _owner_ctx; }
    [[nodiscard]] IVec2 pointerSurfaceSize() const { return _pointer_surface_size; }
    [[nodiscard]] const EventData& data() const { return event_data; }
    [[nodiscard]] std::string toString();

    bool ownedBy(const SceneBase* scene);
};

class PointerEvent : public Event
{
public:
    PointerEvent(const Event& e) : Event(e) {}

    // Mouse
    [[nodiscard]] std::uint8_t button() { return event_data.button.button; }
    [[nodiscard]] double wheelY() { return (double)event_data.wheel.y; }

    // Touch
    [[nodiscard]] constexpr std::uint64_t fingerID() { return event_data.tfinger.fingerID; }
    [[nodiscard]] double x();
    [[nodiscard]] double y();
    [[nodiscard]] DVec2 worldPosition(const CameraInfo& camera);

    [[nodiscard]] bool hoveredOver(const SceneBase* scene);
};

class KeyEvent : public Event
{
public:
    KeyEvent(const Event& e) : Event(e) {}

    [[nodiscard]] KeyCode keyCode()   { return event_data.key.key; }
    [[nodiscard]] ScanCode scanCode() { return event_data.key.scancode; }
    [[nodiscard]] KeyMod keyMod()     { return event_data.key.mod; }

    [[nodiscard]] const char* keyName() { return event_data.key.name.c_str(); }
};

BL_END_NS
