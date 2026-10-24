#include <bitloop/platform/sdl_event.h>
#include <SDL3/SDL.h>

BL_BEGIN_NS

EventData copySDLEvent(const SDL_Event& source)
{
    EventData event;
    event.type = source.type;
    event.timestamp = source.common.timestamp;

    switch (source.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            const auto& e = source.motion;
            event.windowID = e.windowID;
            event.motion = { e.which, e.state, e.x, e.y, e.xrel, e.yrel };
        } break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const auto& e = source.button;
            event.windowID = e.windowID;
            event.button = { e.which, e.button, e.clicks, e.down, e.x, e.y };
        } break;

        case SDL_EVENT_MOUSE_WHEEL:
        {
            const auto& e = source.wheel;
            event.windowID = e.windowID;
            event.wheel = { e.which, static_cast<std::uint32_t>(e.direction), e.x, e.y, e.mouse_x, e.mouse_y, e.integer_x, e.integer_y };
        } break;

        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
        {
            const auto& e = source.tfinger;
            event.windowID = e.windowID;
            event.tfinger = { e.touchID, e.fingerID, e.x, e.y, e.dx, e.dy, e.pressure };
        } break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const auto& e = source.key;
            event.windowID = e.windowID;
            event.key = { e.which, e.key, static_cast<ScanCode>(e.scancode), e.mod, e.raw, e.down, e.repeat, SDL_GetKeyName(e.key) };
        } break;

        case SDL_EVENT_TEXT_INPUT:
            event.windowID = source.text.windowID;
            event.text.text = source.text.text ? source.text.text : "";
            break;

        case SDL_EVENT_TEXT_EDITING:
            event.windowID = source.edit.windowID;
            event.edit = { source.edit.text ? source.edit.text : "", source.edit.start, source.edit.length };
            break;

        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        {
            const auto& e = source.edit_candidates;
            event.windowID = e.windowID;
            event.edit_candidates.selected_candidate = e.selected_candidate;
            event.edit_candidates.horizontal = e.horizontal;
            for (int i = 0; e.candidates && i < e.num_candidates; i++)
                event.edit_candidates.candidates.emplace_back(e.candidates[i] ? e.candidates[i] : "");
        } break;

        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION:
        {
            const auto& e = source.drop;
            event.windowID = e.windowID;
            event.drop = { e.x, e.y, e.source ? e.source : "", e.data ? e.data : "" };
        } break;

        default:
            if (source.type >= SDL_EVENT_WINDOW_FIRST && source.type <= SDL_EVENT_WINDOW_LAST)
            {
                event.windowID = source.window.windowID;
                event.window = { source.window.data1, source.window.data2 };
            }
            break;
    }

    return event;
}

BL_END_NS
