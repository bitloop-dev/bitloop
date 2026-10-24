#pragma once

#ifdef BITLOOP_DEV_MODE

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <bitloop/platform/sdl_event.h>

#include <bitloop/core/camera.h>
#include <bitloop/core/project.h>
#include <bitloop/core/types.h>
#include <bitloop/core/viewport.h>

BL_BEGIN_NS

enum class InputEasing
{
    Linear,
    Smoothstep,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad
};

[[nodiscard]] inline double inputEase(InputEasing easing, double t)
{
    t = std::clamp(t, 0.0, 1.0);

    switch (easing)
    {
        case InputEasing::Linear:
            return t;
        case InputEasing::Smoothstep:
            return t * t * (3.0 - (2.0 * t));
        case InputEasing::EaseInQuad:
            return t * t;
        case InputEasing::EaseOutQuad:
        {
            const double inverse = 1.0 - t;
            return 1.0 - (inverse * inverse);
        }
        case InputEasing::EaseInOutQuad:
            if (t < 0.5)
                return 2.0 * t * t;
            return 1.0 - (((-2.0 * t) + 2.0) * ((-2.0 * t) + 2.0) * 0.5);
    }

    return t;
}

class InputCoordinateSpace
{
    IVec2 _input_surface_size{ 800, 600 };
    DRect _client_rect{ 0.0, 0.0, 800.0, 600.0 };
    DVec2 _stage_size{ 800.0, 600.0 };
    WorldStageTransform _world_stage;

    static void requireInputSurfaceSize(IVec2 size)
    {
        if (size.x <= 0 || size.y <= 0)
            throw std::invalid_argument("InputCoordinateSpace input surface size must be positive");
    }

    static void requireClientRect(DRect rect)
    {
        if (rect.width() <= 0.0 || rect.height() <= 0.0)
            throw std::invalid_argument("InputCoordinateSpace client rect must have positive size");
    }

    static void requireStageSize(DVec2 size)
    {
        if (size.x <= 0.0 || size.y <= 0.0)
            throw std::invalid_argument("InputCoordinateSpace stage size must be positive");
    }

public:
    InputCoordinateSpace() = default;

    InputCoordinateSpace(
        IVec2 input_surface_size,
        DRect client_rect,
        DVec2 stage_size,
        const WorldStageTransform& world_stage = WorldStageTransform())
    {
        set(input_surface_size, client_rect, stage_size, world_stage);
    }

    InputCoordinateSpace(
        const Viewport& viewport,
        IVec2 input_surface_size,
        const WorldStageTransform& world_stage)
    {
        set(input_surface_size, viewport.clientRect(), viewport.size(), world_stage);
    }

    InputCoordinateSpace(
        const Viewport& viewport,
        IVec2 input_surface_size,
        const CameraInfo& camera)
    {
        set(input_surface_size, viewport.clientRect(), viewport.size(), camera.transformFor(viewport));
    }

    static InputCoordinateSpace fullSurface(
        IVec2 input_surface_size,
        const WorldStageTransform& world_stage = WorldStageTransform())
    {
        return InputCoordinateSpace(
            input_surface_size,
            DRect(0.0, 0.0, static_cast<double>(input_surface_size.x), static_cast<double>(input_surface_size.y)),
            static_cast<DVec2>(input_surface_size),
            world_stage);
    }

    InputCoordinateSpace& set(
        IVec2 input_surface_size,
        DRect client_rect,
        DVec2 stage_size,
        const WorldStageTransform& world_stage)
    {
        requireInputSurfaceSize(input_surface_size);
        requireClientRect(client_rect);
        requireStageSize(stage_size);

        _input_surface_size = input_surface_size;
        _client_rect = client_rect;
        _stage_size = stage_size;
        _world_stage = world_stage;
        return *this;
    }

    [[nodiscard]] IVec2 inputSurfaceSize() const
    {
        return _input_surface_size;
    }

    [[nodiscard]] DRect clientRect() const
    {
        return _client_rect;
    }

    [[nodiscard]] DVec2 stageSize() const
    {
        return _stage_size;
    }

    [[nodiscard]] const WorldStageTransform& worldStageTransform() const
    {
        return _world_stage;
    }

    [[nodiscard]] DVec2 stageToClient(DVec2 stage_pos) const
    {
        return DVec2(
            _client_rect.x1 + (stage_pos.x / _stage_size.x) * _client_rect.width(),
            _client_rect.y1 + (stage_pos.y / _stage_size.y) * _client_rect.height());
    }

    [[nodiscard]] DVec2 clientToStage(DVec2 client_pos) const
    {
        return DVec2(
            ((client_pos.x - _client_rect.x1) / _client_rect.width()) * _stage_size.x,
            ((client_pos.y - _client_rect.y1) / _client_rect.height()) * _stage_size.y);
    }

    [[nodiscard]] DVec2 worldToStage(DVec2 world_pos) const
    {
        return _world_stage.toStage<f64>(world_pos);
    }

    [[nodiscard]] DVec2 stageToWorld(DVec2 stage_pos) const
    {
        return _world_stage.toWorld<f64>(stage_pos);
    }

    [[nodiscard]] DVec2 worldToClient(DVec2 world_pos) const
    {
        return stageToClient(worldToStage(world_pos));
    }

    [[nodiscard]] DVec2 clientToWorld(DVec2 client_pos) const
    {
        return stageToWorld(clientToStage(client_pos));
    }

    [[nodiscard]] DVec2 stageOffsetToClientOffset(DVec2 stage_offset) const
    {
        return DVec2(
            (stage_offset.x / _stage_size.x) * _client_rect.width(),
            (stage_offset.y / _stage_size.y) * _client_rect.height());
    }

    [[nodiscard]] DVec2 worldOffsetToClientOffset(DVec2 world_offset) const
    {
        return stageOffsetToClientOffset(_world_stage.toStageOffset<f64>(world_offset));
    }

    [[nodiscard]] DQuad stageQuadToClientQuad(const DQuad& stage_quad) const
    {
        return DQuad(
            stageToClient(stage_quad.a),
            stageToClient(stage_quad.b),
            stageToClient(stage_quad.c),
            stageToClient(stage_quad.d));
    }

    [[nodiscard]] DQuad worldQuadToClientQuad(const DQuad& world_quad) const
    {
        return DQuad(
            worldToClient(world_quad.a),
            worldToClient(world_quad.b),
            worldToClient(world_quad.c),
            worldToClient(world_quad.d));
    }

    [[nodiscard]] DQuad worldRectToClientQuad(const DRect& world_rect) const
    {
        return worldQuadToClientQuad(static_cast<DQuad>(world_rect));
    }

    [[nodiscard]] DVec2 worldRectCenterToClient(const DRect& world_rect) const
    {
        return worldToClient(world_rect.center());
    }

    [[nodiscard]] DVec2 worldQuadCenterToClient(const DQuad& world_quad) const
    {
        return worldToClient(world_quad.center());
    }
};

class InputEmulation
{
public:
    using Frame = std::int64_t;

    struct ScheduledEvent
    {
        Frame frame = 0;
        std::uint64_t order = 0;
        EventData event{};
    };

private:
    IVec2 _surface_size{ 800, 600 };
    std::vector<ScheduledEvent> _events;
    std::uint64_t _next_order = 0;

    static void requireFrame(Frame frame)
    {
        if (frame < 0)
            throw std::invalid_argument("InputEmulation frame must be non-negative");
    }

    static void requireFrameRange(Frame start_frame, Frame end_frame)
    {
        requireFrame(start_frame);
        requireFrame(end_frame);
        if (end_frame < start_frame)
            throw std::invalid_argument("InputEmulation end frame must be >= start frame");
    }

    static void requireSurfaceSize(IVec2 size)
    {
        if (size.x <= 0 || size.y <= 0)
            throw std::invalid_argument("InputEmulation surface size must be positive");
    }

    [[nodiscard]] DVec2 normalized(DVec2 pixel_pos) const
    {
        return DVec2(
            pixel_pos.x / static_cast<double>(_surface_size.x),
            pixel_pos.y / static_cast<double>(_surface_size.y));
    }

    [[nodiscard]] Uint64 timestamp(Frame frame, std::uint64_t order) const
    {
        return (static_cast<Uint64>(frame) * 1'000'000ull) + static_cast<Uint64>(order);
    }

    static DVec2 lerp(DVec2 a, DVec2 b, double t)
    {
        return a + ((b - a) * t);
    }

    void requireCompatibleSurface(const InputCoordinateSpace& space) const
    {
        if (_surface_size != space.inputSurfaceSize())
            throw std::invalid_argument("InputEmulation surface size does not match InputCoordinateSpace");
    }

public:
    explicit InputEmulation(IVec2 surface_size = IVec2(800, 600))
    {
        setSurfaceSize(surface_size);
    }

    explicit InputEmulation(const InputCoordinateSpace& space)
    {
        setSurfaceSize(space.inputSurfaceSize());
    }

    InputEmulation& setSurfaceSize(IVec2 surface_size)
    {
        requireSurfaceSize(surface_size);
        _surface_size = surface_size;
        return *this;
    }

    [[nodiscard]] IVec2 surfaceSize() const
    {
        return _surface_size;
    }

    InputEmulation& clear()
    {
        _events.clear();
        _next_order = 0;
        return *this;
    }

    InputEmulation& clearFrame(Frame frame)
    {
        requireFrame(frame);
        std::erase_if(_events, [frame](const ScheduledEvent& scheduled)
        {
            return scheduled.frame == frame;
        });
        return *this;
    }

    [[nodiscard]] bool empty() const
    {
        return _events.empty();
    }

    [[nodiscard]] std::size_t size() const
    {
        return _events.size();
    }

    [[nodiscard]] const std::vector<ScheduledEvent>& scheduledEvents() const
    {
        return _events;
    }

    [[nodiscard]] std::vector<EventData> eventsForFrame(Frame frame) const
    {
        requireFrame(frame);

        std::vector<EventData> out;
        for (const ScheduledEvent& scheduled : _events)
        {
            if (scheduled.frame == frame)
                out.push_back(scheduled.event);
        }
        return out;
    }

    [[nodiscard]] Frame firstFrame() const
    {
        if (_events.empty())
            return 0;

        Frame frame = _events.front().frame;
        for (const ScheduledEvent& scheduled : _events)
            frame = std::min(frame, scheduled.frame);
        return frame;
    }

    [[nodiscard]] Frame lastFrame() const
    {
        if (_events.empty())
            return 0;

        Frame frame = _events.front().frame;
        for (const ScheduledEvent& scheduled : _events)
            frame = std::max(frame, scheduled.frame);
        return frame;
    }

    InputEmulation& event(Frame frame, const SDL_Event& event)
    {
        requireFrame(frame);

        const std::uint64_t order = _next_order++;
        EventData copy = copySDLEvent(event);
        copy.timestamp = timestamp(frame, order);
        _events.push_back({ frame, order, std::move(copy) });
        return *this;
    }

    std::size_t queueFrame(ProjectBase& project, Frame frame) const
    {
        requireFrame(frame);

        std::size_t count = 0;
        for (const ScheduledEvent& scheduled : _events)
        {
            if (scheduled.frame == frame)
            {
                project.queueHeadlessInputEvent(scheduled.event);
                count++;
            }
        }
        return count;
    }

    std::size_t queueFrames(ProjectBase& project, Frame start_frame, Frame end_frame) const
    {
        requireFrameRange(start_frame, end_frame);

        std::size_t count = 0;
        for (Frame frame = start_frame; frame <= end_frame; frame++)
            count += queueFrame(project, frame);
        return count;
    }

    InputEmulation& mouseDown(
        Frame frame,
        DVec2 pos,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.button = button;
        e.button.down = true;
        e.button.clicks = clicks;
        e.button.x = static_cast<float>(pos.x);
        e.button.y = static_cast<float>(pos.y);
        return event(frame, e);
    }

    InputEmulation& mouseUp(
        Frame frame,
        DVec2 pos,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = button;
        e.button.down = false;
        e.button.clicks = clicks;
        e.button.x = static_cast<float>(pos.x);
        e.button.y = static_cast<float>(pos.y);
        return event(frame, e);
    }

    InputEmulation& mouseMove(
        Frame frame,
        DVec2 pos,
        SDL_MouseButtonFlags state = 0,
        DVec2 rel = DVec2(0.0, 0.0))
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.state = state;
        e.motion.x = static_cast<float>(pos.x);
        e.motion.y = static_cast<float>(pos.y);
        e.motion.xrel = static_cast<float>(rel.x);
        e.motion.yrel = static_cast<float>(rel.y);
        return event(frame, e);
    }

    InputEmulation& mouseWheel(
        Frame frame,
        DVec2 pos,
        float y,
        float x = 0.0f,
        SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.x = x;
        e.wheel.y = y;
        e.wheel.direction = direction;
        e.wheel.mouse_x = static_cast<float>(pos.x);
        e.wheel.mouse_y = static_cast<float>(pos.y);
        return event(frame, e);
    }

    InputEmulation& mouseDrag(
        Frame start_frame,
        Frame end_frame,
        DVec2 from,
        DVec2 to,
        Uint8 button = SDL_BUTTON_LEFT,
        InputEasing easing = InputEasing::Linear)
    {
        requireFrameRange(start_frame, end_frame);

        mouseDown(start_frame, from, button);

        const SDL_MouseButtonFlags state = SDL_BUTTON_MASK(button);
        if (end_frame == start_frame)
        {
            mouseUp(end_frame, to, button);
            return *this;
        }

        DVec2 prev = from;
        for (Frame frame = start_frame + 1; frame <= end_frame; frame++)
        {
            const double t = static_cast<double>(frame - start_frame) /
                static_cast<double>(end_frame - start_frame);
            const DVec2 pos = lerp(from, to, inputEase(easing, t));
            mouseMove(frame, pos, state, pos - prev);
            prev = pos;
        }

        mouseUp(end_frame, to, button);
        return *this;
    }

    InputEmulation& mouseDownWorld(
        Frame frame,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        requireCompatibleSurface(space);
        return mouseDown(frame, space.worldToClient(world_pos), button, clicks);
    }

    InputEmulation& mouseUpWorld(
        Frame frame,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        requireCompatibleSurface(space);
        return mouseUp(frame, space.worldToClient(world_pos), button, clicks);
    }

    InputEmulation& mouseMoveWorld(
        Frame frame,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        SDL_MouseButtonFlags state = 0,
        DVec2 world_rel = DVec2(0.0, 0.0))
    {
        requireCompatibleSurface(space);
        return mouseMove(
            frame,
            space.worldToClient(world_pos),
            state,
            space.worldOffsetToClientOffset(world_rel));
    }

    InputEmulation& mouseWheelWorld(
        Frame frame,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        float y,
        float x = 0.0f,
        SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL)
    {
        requireCompatibleSurface(space);
        return mouseWheel(frame, space.worldToClient(world_pos), y, x, direction);
    }

    InputEmulation& mouseDragWorld(
        Frame start_frame,
        Frame end_frame,
        const InputCoordinateSpace& space,
        DVec2 from_world,
        DVec2 to_world,
        Uint8 button = SDL_BUTTON_LEFT,
        InputEasing easing = InputEasing::Linear)
    {
        requireCompatibleSurface(space);
        return mouseDrag(
            start_frame,
            end_frame,
            space.worldToClient(from_world),
            space.worldToClient(to_world),
            button,
            easing);
    }

    InputEmulation& mouseDragWorldRect(
        Frame start_frame,
        Frame end_frame,
        const InputCoordinateSpace& space,
        const DRect& from_world_rect,
        const DRect& to_world_rect,
        Uint8 button = SDL_BUTTON_LEFT,
        InputEasing easing = InputEasing::Linear)
    {
        return mouseDragWorld(
            start_frame,
            end_frame,
            space,
            from_world_rect.center(),
            to_world_rect.center(),
            button,
            easing);
    }

    InputEmulation& click(
        Frame frame,
        DVec2 pos,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        mouseDown(frame, pos, button, clicks);
        mouseUp(frame, pos, button, clicks);
        return *this;
    }

    InputEmulation& clickWorld(
        Frame frame,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        requireCompatibleSurface(space);
        return click(frame, space.worldToClient(world_pos), button, clicks);
    }

    InputEmulation& clickWorldRect(
        Frame frame,
        const InputCoordinateSpace& space,
        const DRect& world_rect,
        Uint8 button = SDL_BUTTON_LEFT,
        Uint8 clicks = 1)
    {
        return clickWorld(frame, space, world_rect.center(), button, clicks);
    }

    InputEmulation& fingerDown(
        Frame frame,
        SDL_FingerID finger_id,
        DVec2 pixel_pos,
        float pressure = 1.0f,
        SDL_TouchID touch_id = 1)
    {
        return fingerEvent(frame, SDL_EVENT_FINGER_DOWN, touch_id, finger_id, pixel_pos, DVec2(0.0), pressure);
    }

    InputEmulation& fingerMove(
        Frame frame,
        SDL_FingerID finger_id,
        DVec2 pixel_pos,
        DVec2 pixel_delta = DVec2(0.0),
        float pressure = 1.0f,
        SDL_TouchID touch_id = 1)
    {
        return fingerEvent(frame, SDL_EVENT_FINGER_MOTION, touch_id, finger_id, pixel_pos, pixel_delta, pressure);
    }

    InputEmulation& fingerUp(
        Frame frame,
        SDL_FingerID finger_id,
        DVec2 pixel_pos,
        float pressure = 0.0f,
        SDL_TouchID touch_id = 1)
    {
        return fingerEvent(frame, SDL_EVENT_FINGER_UP, touch_id, finger_id, pixel_pos, DVec2(0.0), pressure);
    }

    InputEmulation& fingerDrag(
        Frame start_frame,
        Frame end_frame,
        SDL_FingerID finger_id,
        DVec2 from,
        DVec2 to,
        SDL_TouchID touch_id = 1,
        InputEasing easing = InputEasing::Linear,
        float pressure = 1.0f,
        float release_pressure = 0.0f)
    {
        requireFrameRange(start_frame, end_frame);

        fingerDown(start_frame, finger_id, from, pressure, touch_id);
        if (end_frame == start_frame)
        {
            fingerUp(end_frame, finger_id, to, release_pressure, touch_id);
            return *this;
        }

        DVec2 prev = from;
        for (Frame frame = start_frame + 1; frame <= end_frame; frame++)
        {
            const double t = static_cast<double>(frame - start_frame) /
                static_cast<double>(end_frame - start_frame);
            const DVec2 pos = lerp(from, to, inputEase(easing, t));
            fingerMove(frame, finger_id, pos, pos - prev, pressure, touch_id);
            prev = pos;
        }

        fingerUp(end_frame, finger_id, to, release_pressure, touch_id);
        return *this;
    }

    InputEmulation& fingerDownWorld(
        Frame frame,
        SDL_FingerID finger_id,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        float pressure = 1.0f,
        SDL_TouchID touch_id = 1)
    {
        requireCompatibleSurface(space);
        return fingerDown(frame, finger_id, space.worldToClient(world_pos), pressure, touch_id);
    }

    InputEmulation& fingerMoveWorld(
        Frame frame,
        SDL_FingerID finger_id,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        DVec2 world_delta = DVec2(0.0),
        float pressure = 1.0f,
        SDL_TouchID touch_id = 1)
    {
        requireCompatibleSurface(space);
        return fingerMove(
            frame,
            finger_id,
            space.worldToClient(world_pos),
            space.worldOffsetToClientOffset(world_delta),
            pressure,
            touch_id);
    }

    InputEmulation& fingerUpWorld(
        Frame frame,
        SDL_FingerID finger_id,
        const InputCoordinateSpace& space,
        DVec2 world_pos,
        float pressure = 0.0f,
        SDL_TouchID touch_id = 1)
    {
        requireCompatibleSurface(space);
        return fingerUp(frame, finger_id, space.worldToClient(world_pos), pressure, touch_id);
    }

    InputEmulation& fingerDragWorld(
        Frame start_frame,
        Frame end_frame,
        SDL_FingerID finger_id,
        const InputCoordinateSpace& space,
        DVec2 from_world,
        DVec2 to_world,
        SDL_TouchID touch_id = 1,
        InputEasing easing = InputEasing::Linear,
        float pressure = 1.0f,
        float release_pressure = 0.0f)
    {
        requireCompatibleSurface(space);
        return fingerDrag(
            start_frame,
            end_frame,
            finger_id,
            space.worldToClient(from_world),
            space.worldToClient(to_world),
            touch_id,
            easing,
            pressure,
            release_pressure);
    }

    InputEmulation& fingerDragWorldRect(
        Frame start_frame,
        Frame end_frame,
        SDL_FingerID finger_id,
        const InputCoordinateSpace& space,
        const DRect& from_world_rect,
        const DRect& to_world_rect,
        SDL_TouchID touch_id = 1,
        InputEasing easing = InputEasing::Linear,
        float pressure = 1.0f,
        float release_pressure = 0.0f)
    {
        return fingerDragWorld(
            start_frame,
            end_frame,
            finger_id,
            space,
            from_world_rect.center(),
            to_world_rect.center(),
            touch_id,
            easing,
            pressure,
            release_pressure);
    }

    InputEmulation& keyDown(
        Frame frame,
        SDL_Keycode key,
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN,
        SDL_Keymod mod = SDL_KMOD_NONE,
        bool repeat = false)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        e.key.scancode = scancode;
        e.key.mod = mod;
        e.key.down = true;
        e.key.repeat = repeat;
        return event(frame, e);
    }

    InputEmulation& keyUp(
        Frame frame,
        SDL_Keycode key,
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN,
        SDL_Keymod mod = SDL_KMOD_NONE)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_UP;
        e.key.key = key;
        e.key.scancode = scancode;
        e.key.mod = mod;
        e.key.down = false;
        return event(frame, e);
    }

    InputEmulation& keyTap(
        Frame start_frame,
        SDL_Keycode key,
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN,
        Frame hold_frames = 1,
        SDL_Keymod mod = SDL_KMOD_NONE)
    {
        if (hold_frames < 0)
            throw std::invalid_argument("InputEmulation hold_frames must be non-negative");

        keyDown(start_frame, key, scancode, mod);
        keyUp(start_frame + hold_frames, key, scancode, mod);
        return *this;
    }

private:
    InputEmulation& fingerEvent(
        Frame frame,
        Uint32 type,
        SDL_TouchID touch_id,
        SDL_FingerID finger_id,
        DVec2 pixel_pos,
        DVec2 pixel_delta,
        float pressure)
    {
        const DVec2 pos = normalized(pixel_pos);
        const DVec2 delta = normalized(pixel_delta);

        SDL_Event e{};
        e.type = type;
        e.tfinger.touchID = touch_id;
        e.tfinger.fingerID = finger_id;
        e.tfinger.x = static_cast<float>(pos.x);
        e.tfinger.y = static_cast<float>(pos.y);
        e.tfinger.dx = static_cast<float>(delta.x);
        e.tfinger.dy = static_cast<float>(delta.y);
        e.tfinger.pressure = pressure;
        return event(frame, e);
    }
};

BL_END_NS

#endif
