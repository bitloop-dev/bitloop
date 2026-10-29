#include <bitloop/core/headless_runner.h>

#ifdef BITLOOP_DEV_MODE

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <bitloop/core/capture_context.h>
#include <bitloop/core/input_emulation.h>
#include <bitloop/core/main_window.h>
#include <bitloop/core/project.h>
#include <bitloop/nanovgx/nano_canvas.h>
#include <bitloop/platform/platform.h>

BL_BEGIN_NS

namespace
{
    struct TraceSummary
    {
        std::int64_t scene_frames = 0;
        std::int64_t frames_with_objects = 0;
        std::int64_t total_objects = 0;
        std::int64_t total_events = 0;
        std::int64_t dropped_objects = 0;
        std::int64_t snapshot_requests = 0;
        std::int64_t snapshots_rendered = 0;
        std::int64_t snapshots_failed = 0;
        std::int64_t checkpoints_written = 0;
        std::int64_t checkpoints_without_state = 0;
        double max_speed = 0.0;
        bool has_bounds = false;
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        double max_x = std::numeric_limits<double>::lowest();
        double max_y = std::numeric_limits<double>::lowest();
        std::map<std::string, std::int64_t> object_kinds;

        void update(const SimTraceFrame& frame)
        {
            scene_frames++;
            total_objects += static_cast<std::int64_t>(frame.objects.size());
            total_events += static_cast<std::int64_t>(frame.events.size());
            dropped_objects += static_cast<std::int64_t>(frame.dropped_objects);

            if (!frame.objects.empty())
                frames_with_objects++;

            for (const SimTraceObject& object : frame.objects)
            {
                object_kinds[object.kind]++;
                min_x = std::min(min_x, object.x);
                min_y = std::min(min_y, object.y);
                max_x = std::max(max_x, object.x);
                max_y = std::max(max_y, object.y);
                has_bounds = true;

                if (object.vx && object.vy)
                {
                    const double speed = std::sqrt((*object.vx * *object.vx) + (*object.vy * *object.vy));
                    max_speed = std::max(max_speed, speed);
                }
            }
        }

        [[nodiscard]] JSON::json toJson() const
        {
            JSON::json kinds = JSON::json::object();
            for (const auto& [kind, count] : object_kinds)
                kinds[kind] = count;

            JSON::json out = {
                {"scene_frames", scene_frames},
                {"frames_with_objects", frames_with_objects},
                {"total_objects", total_objects},
                {"total_events", total_events},
                {"dropped_objects", dropped_objects},
                {"snapshot_requests", snapshot_requests},
                {"snapshots_rendered", snapshots_rendered},
                {"snapshots_failed", snapshots_failed},
                {"checkpoints_written", checkpoints_written},
                {"checkpoints_without_state", checkpoints_without_state},
                {"max_speed", max_speed},
                {"object_kinds", std::move(kinds)}
            };

            if (has_bounds)
            {
                out["bounds"] = {
                    {"min_x", min_x},
                    {"min_y", min_y},
                    {"max_x", max_x},
                    {"max_y", max_y}
                };
            }
            else
            {
                out["bounds"] = nullptr;
            }

            return out;
        }
    };

    [[nodiscard]] const char* traceFormatName(HeadlessTraceFormat format)
    {
        switch (format)
        {
            case HeadlessTraceFormat::CompactDelta: return "compact_delta";
            case HeadlessTraceFormat::Full: return "full";
            default: return "unknown";
        }
    }

    [[nodiscard]] bool parseTraceFormat(std::string_view value, HeadlessTraceFormat& out)
    {
        if (value == "compact" || value == "compact_delta" || value == "delta")
        {
            out = HeadlessTraceFormat::CompactDelta;
            return true;
        }

        if (value == "full" || value == "verbose")
        {
            out = HeadlessTraceFormat::Full;
            return true;
        }

        return false;
    }

    [[nodiscard]] std::string lowerCopy(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }

    [[nodiscard]] std::string trimCopy(std::string_view text)
    {
        std::size_t first = 0;
        while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
            first++;

        std::size_t last = text.size();
        while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
            last--;

        return std::string(text.substr(first, last - first));
    }

    [[noreturn]] void throwInputLineError(std::size_t line_number, const std::string& message)
    {
        throw std::runtime_error(
            "Input replay JSONL line " + std::to_string(line_number) + ": " + message);
    }

    [[nodiscard]] const JSON::json& requireField(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field)
    {
        const auto it = event_json.find(std::string(field));
        if (it == event_json.end())
            throwInputLineError(line_number, "missing required field '" + std::string(field) + "'");
        return *it;
    }

    [[nodiscard]] double requireNumber(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field)
    {
        const JSON::json& value = requireField(event_json, line_number, field);
        if (!value.is_number())
            throwInputLineError(line_number, "field '" + std::string(field) + "' must be numeric");
        return value.get<double>();
    }

    [[nodiscard]] std::int64_t requireInt64(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field)
    {
        const JSON::json& value = requireField(event_json, line_number, field);
        if (!value.is_number_integer())
            throwInputLineError(line_number, "field '" + std::string(field) + "' must be an integer");
        return value.get<std::int64_t>();
    }

    [[nodiscard]] std::string requireString(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field)
    {
        const JSON::json& value = requireField(event_json, line_number, field);
        if (!value.is_string())
            throwInputLineError(line_number, "field '" + std::string(field) + "' must be a string");
        return value.get<std::string>();
    }

    [[nodiscard]] bool hasField(const JSON::json& event_json, std::string_view field)
    {
        return event_json.find(std::string(field)) != event_json.end();
    }

    [[nodiscard]] double optionalNumber(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field,
        double fallback)
    {
        const auto it = event_json.find(std::string(field));
        if (it == event_json.end())
            return fallback;
        if (!it->is_number())
            throwInputLineError(line_number, "field '" + std::string(field) + "' must be numeric");
        return it->get<double>();
    }

    [[nodiscard]] std::int64_t optionalInt64(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field,
        std::int64_t fallback)
    {
        const auto it = event_json.find(std::string(field));
        if (it == event_json.end())
            return fallback;
        if (!it->is_number_integer())
            throwInputLineError(line_number, "field '" + std::string(field) + "' must be an integer");
        return it->get<std::int64_t>();
    }

    [[nodiscard]] DVec2 requirePoint(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view x_field = "x",
        std::string_view y_field = "y")
    {
        return DVec2(
            requireNumber(event_json, line_number, x_field),
            requireNumber(event_json, line_number, y_field));
    }

    [[nodiscard]] DVec2 parsePointValue(
        const JSON::json& value,
        std::size_t line_number,
        std::string_view field)
    {
        if (value.is_array())
        {
            if (value.size() != 2 || !value[0].is_number() || !value[1].is_number())
                throwInputLineError(line_number, "field '" + std::string(field) + "' must be [x, y]");
            return DVec2(value[0].get<double>(), value[1].get<double>());
        }

        if (value.is_object())
        {
            const auto x_it = value.find("x");
            const auto y_it = value.find("y");
            if (x_it == value.end() || y_it == value.end() || !x_it->is_number() || !y_it->is_number())
                throwInputLineError(line_number, "field '" + std::string(field) + "' must contain numeric x/y");
            return DVec2(x_it->get<double>(), y_it->get<double>());
        }

        throwInputLineError(line_number, "field '" + std::string(field) + "' must be [x, y] or {\"x\":...,\"y\":...}");
    }

    [[nodiscard]] DVec2 requireNamedPoint(
        const JSON::json& event_json,
        std::size_t line_number,
        std::string_view field)
    {
        const auto it = event_json.find(std::string(field));
        if (it != event_json.end())
            return parsePointValue(*it, line_number, field);

        const std::string x_field = std::string(field) + "_x";
        const std::string y_field = std::string(field) + "_y";
        if (hasField(event_json, x_field) || hasField(event_json, y_field))
            return requirePoint(event_json, line_number, x_field, y_field);

        throwInputLineError(line_number, "missing required field '" + std::string(field) + "'");
    }

    [[nodiscard]] DVec2 requireGesturePoint(
        const JSON::json& event_json,
        std::size_t line_number)
    {
        const auto pos_it = event_json.find("pos");
        if (pos_it != event_json.end())
            return parsePointValue(*pos_it, line_number, "pos");

        const auto at_it = event_json.find("at");
        if (at_it != event_json.end())
            return parsePointValue(*at_it, line_number, "at");

        return requirePoint(event_json, line_number);
    }

    [[nodiscard]] std::optional<std::int64_t> optionalDurationFrames(
        const JSON::json& event_json,
        std::size_t line_number)
    {
        const char* fields[] = { "duration", "duration_frames", "frames" };
        std::optional<std::int64_t> duration;
        std::string used_field;

        for (const char* field : fields)
        {
            if (!hasField(event_json, field))
                continue;

            if (duration)
                throwInputLineError(line_number, "use only one duration field");

            duration = optionalInt64(event_json, line_number, field, 0);
            used_field = field;
        }

        if (duration && *duration < 0)
            throwInputLineError(line_number, "field '" + used_field + "' must be non-negative");

        return duration;
    }

    [[nodiscard]] InputEmulation::Frame parseGestureEndFrame(
        const JSON::json& event_json,
        std::size_t line_number,
        InputEmulation::Frame start_frame,
        InputEmulation::Frame default_duration)
    {
        const bool has_end_frame = hasField(event_json, "end_frame");
        const std::optional<std::int64_t> duration = optionalDurationFrames(event_json, line_number);

        if (has_end_frame && duration)
            throwInputLineError(line_number, "use either 'end_frame' or a duration field");

        if (has_end_frame)
        {
            const std::int64_t end_frame = requireInt64(event_json, line_number, "end_frame");
            if (end_frame < start_frame)
                throwInputLineError(line_number, "field 'end_frame' must be >= frame");
            return end_frame;
        }

        if (default_duration < 0)
            throwInputLineError(line_number, "default duration must be non-negative");

        return start_frame + duration.value_or(default_duration);
    }

    [[nodiscard]] InputEasing parseInputEasing(
        const JSON::json& event_json,
        std::size_t line_number,
        InputEasing fallback = InputEasing::Linear)
    {
        const auto it = event_json.find("easing");
        if (it == event_json.end())
            return fallback;
        if (!it->is_string())
            throwInputLineError(line_number, "field 'easing' must be a string");

        std::string name = lowerCopy(it->get<std::string>());
        std::ranges::replace(name, '-', '_');
        std::ranges::replace(name, ' ', '_');

        if (name == "linear")
            return InputEasing::Linear;
        if (name == "smoothstep" || name == "smooth_step")
            return InputEasing::Smoothstep;
        if (name == "ease_in" || name == "ease_in_quad")
            return InputEasing::EaseInQuad;
        if (name == "ease_out" || name == "ease_out_quad")
            return InputEasing::EaseOutQuad;
        if (name == "ease_in_out" || name == "ease_in_out_quad")
            return InputEasing::EaseInOutQuad;

        throwInputLineError(line_number, "unknown easing '" + name + "'");
    }

    [[nodiscard]] Uint8 parseMouseButtonName(std::string name, std::size_t line_number)
    {
        name = lowerCopy(std::move(name));
        if (name == "left" || name == "primary")
            return SDL_BUTTON_LEFT;
        if (name == "middle" || name == "wheel")
            return SDL_BUTTON_MIDDLE;
        if (name == "right" || name == "secondary")
            return SDL_BUTTON_RIGHT;
        if (name == "x1" || name == "back")
            return SDL_BUTTON_X1;
        if (name == "x2" || name == "forward")
            return SDL_BUTTON_X2;

        throwInputLineError(line_number, "unknown mouse button '" + name + "'");
    }

    [[nodiscard]] Uint8 parseMouseButton(
        const JSON::json& event_json,
        std::size_t line_number,
        Uint8 fallback = SDL_BUTTON_LEFT)
    {
        const auto it = event_json.find("button");
        if (it == event_json.end())
            return fallback;
        if (it->is_number_integer())
            return static_cast<Uint8>(it->get<int>());
        if (it->is_string())
            return parseMouseButtonName(it->get<std::string>(), line_number);

        throwInputLineError(line_number, "field 'button' must be a string or integer");
    }

    [[nodiscard]] SDL_MouseButtonFlags parseMouseButtonState(
        const JSON::json& event_json,
        std::size_t line_number)
    {
        const auto state_it = event_json.find("state");
        if (state_it != event_json.end())
        {
            if (!state_it->is_number_unsigned() && !state_it->is_number_integer())
                throwInputLineError(line_number, "field 'state' must be an integer mouse button bitmask");
            return static_cast<SDL_MouseButtonFlags>(state_it->get<Uint32>());
        }

        const auto buttons_it = event_json.find("buttons");
        if (buttons_it == event_json.end())
            return 0;

        SDL_MouseButtonFlags state = 0;
        if (buttons_it->is_string())
        {
            state |= SDL_BUTTON_MASK(parseMouseButtonName(buttons_it->get<std::string>(), line_number));
            return state;
        }

        if (!buttons_it->is_array())
            throwInputLineError(line_number, "field 'buttons' must be a string or array");

        for (const JSON::json& button : *buttons_it)
        {
            if (button.is_string())
                state |= SDL_BUTTON_MASK(parseMouseButtonName(button.get<std::string>(), line_number));
            else if (button.is_number_integer())
                state |= SDL_BUTTON_MASK(static_cast<Uint8>(button.get<int>()));
            else
                throwInputLineError(line_number, "field 'buttons' entries must be strings or integers");
        }
        return state;
    }

    [[nodiscard]] SDL_Keycode parseKeyValue(const JSON::json& value, std::size_t line_number)
    {
        if (value.is_number_integer())
            return static_cast<SDL_Keycode>(value.get<std::int64_t>());

        if (!value.is_string())
            throwInputLineError(line_number, "field 'key' must be a string or integer");

        std::string name = value.get<std::string>();
        const std::string lower = lowerCopy(name);
        if (lower == "space")
            return SDLK_SPACE;

        SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key != SDLK_UNKNOWN)
            return key;

        if (name.size() == 1)
            return static_cast<SDL_Keycode>(std::tolower(static_cast<unsigned char>(name[0])));

        throwInputLineError(line_number, "unknown key '" + name + "'");
    }

    [[nodiscard]] SDL_Keycode parseRequiredKey(
        const JSON::json& event_json,
        std::size_t line_number)
    {
        return parseKeyValue(requireField(event_json, line_number, "key"), line_number);
    }

    [[nodiscard]] SDL_Scancode parseOptionalScancode(
        const JSON::json& event_json,
        std::size_t line_number,
        SDL_Keycode key)
    {
        const auto it = event_json.find("scancode");
        if (it == event_json.end())
        {
            SDL_Keymod modstate = SDL_KMOD_NONE;
            return SDL_GetScancodeFromKey(key, &modstate);
        }

        if (it->is_number_integer())
            return static_cast<SDL_Scancode>(it->get<int>());

        if (!it->is_string())
            throwInputLineError(line_number, "field 'scancode' must be a string or integer");

        const std::string name = it->get<std::string>();
        const std::string lower = lowerCopy(name);
        if (lower == "space")
            return SDL_SCANCODE_SPACE;

        SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN)
            return scancode;

        throwInputLineError(line_number, "unknown scancode '" + name + "'");
    }

    [[nodiscard]] SDL_Keymod parseOptionalKeyMod(
        const JSON::json& event_json,
        std::size_t line_number)
    {
        const auto it = event_json.find("mod");
        if (it == event_json.end())
            return SDL_KMOD_NONE;
        if (!it->is_number_integer() && !it->is_number_unsigned())
            throwInputLineError(line_number, "field 'mod' must be an integer SDL_Keymod bitmask");
        return static_cast<SDL_Keymod>(it->get<Uint16>());
    }

    void appendReplayEvent(
        InputEmulation& input,
        const JSON::json& event_json,
        std::size_t line_number)
    {
        if (!event_json.is_object())
            throwInputLineError(line_number, "event must be a JSON object");

        const std::int64_t frame = requireInt64(event_json, line_number, "frame");
        if (frame < 0)
            throwInputLineError(line_number, "field 'frame' must be non-negative");

        const std::string type = lowerCopy(requireString(event_json, line_number, "type"));

        if (type == "mouse_drag" || type == "drag")
        {
            input.mouseDrag(
                frame,
                parseGestureEndFrame(event_json, line_number, frame, 1),
                requireNamedPoint(event_json, line_number, "from"),
                requireNamedPoint(event_json, line_number, "to"),
                parseMouseButton(event_json, line_number),
                parseInputEasing(event_json, line_number));
            return;
        }

        if (type == "finger_drag" || type == "touch_drag")
        {
            input.fingerDrag(
                frame,
                parseGestureEndFrame(event_json, line_number, frame, 1),
                optionalInt64(event_json, line_number, "finger_id", optionalInt64(event_json, line_number, "finger", 0)),
                requireNamedPoint(event_json, line_number, "from"),
                requireNamedPoint(event_json, line_number, "to"),
                optionalInt64(event_json, line_number, "touch_id", 1),
                parseInputEasing(event_json, line_number),
                static_cast<float>(optionalNumber(event_json, line_number, "pressure", 1.0)),
                static_cast<float>(optionalNumber(event_json, line_number, "release_pressure", 0.0)));
            return;
        }

        if (type == "click" || type == "mouse_click")
        {
            const DVec2 pos = requireGesturePoint(event_json, line_number);
            const InputEmulation::Frame end_frame = parseGestureEndFrame(event_json, line_number, frame, 0);
            const Uint8 button = parseMouseButton(event_json, line_number);
            const Uint8 clicks = static_cast<Uint8>(optionalInt64(event_json, line_number, "clicks", 1));
            input.mouseDown(frame, pos, button, clicks);
            input.mouseUp(end_frame, pos, button, clicks);
            return;
        }

        if (type == "key_tap" || type == "keytap" || type == "tap_key")
        {
            const SDL_Keycode key = parseRequiredKey(event_json, line_number);
            const std::int64_t hold_frames = optionalInt64(
                event_json,
                line_number,
                "hold_frames",
                optionalDurationFrames(event_json, line_number).value_or(1));
            if (hold_frames < 0)
                throwInputLineError(line_number, "field 'hold_frames' must be non-negative");

            input.keyTap(
                frame,
                key,
                parseOptionalScancode(event_json, line_number, key),
                hold_frames,
                parseOptionalKeyMod(event_json, line_number));
            return;
        }

        if (type == "mouse_down" || type == "mousedown" || type == "mouse_button_down")
        {
            input.mouseDown(
                frame,
                requirePoint(event_json, line_number),
                parseMouseButton(event_json, line_number),
                static_cast<Uint8>(optionalInt64(event_json, line_number, "clicks", 1)));
            return;
        }

        if (type == "mouse_up" || type == "mouseup" || type == "mouse_button_up")
        {
            input.mouseUp(
                frame,
                requirePoint(event_json, line_number),
                parseMouseButton(event_json, line_number),
                static_cast<Uint8>(optionalInt64(event_json, line_number, "clicks", 1)));
            return;
        }

        if (type == "mouse_move" || type == "mousemove" || type == "mouse_motion")
        {
            input.mouseMove(
                frame,
                requirePoint(event_json, line_number),
                parseMouseButtonState(event_json, line_number),
                DVec2(
                    optionalNumber(event_json, line_number, "xrel", 0.0),
                    optionalNumber(event_json, line_number, "yrel", 0.0)));
            return;
        }

        if (type == "mouse_wheel" || type == "wheel")
        {
            const DVec2 pos = hasField(event_json, "mouse_x") || hasField(event_json, "mouse_y")
                ? requirePoint(event_json, line_number, "mouse_x", "mouse_y")
                : requirePoint(event_json, line_number);
            input.mouseWheel(
                frame,
                pos,
                static_cast<float>(optionalNumber(event_json, line_number, "wheel_y", optionalNumber(event_json, line_number, "dy", 0.0))),
                static_cast<float>(optionalNumber(event_json, line_number, "wheel_x", optionalNumber(event_json, line_number, "dx", 0.0))));
            return;
        }

        if (type == "finger_down" || type == "touch_down")
        {
            input.fingerDown(
                frame,
                optionalInt64(event_json, line_number, "finger_id", optionalInt64(event_json, line_number, "finger", 0)),
                requirePoint(event_json, line_number),
                static_cast<float>(optionalNumber(event_json, line_number, "pressure", 1.0)),
                optionalInt64(event_json, line_number, "touch_id", 1));
            return;
        }

        if (type == "finger_move" || type == "touch_move" || type == "finger_motion")
        {
            input.fingerMove(
                frame,
                optionalInt64(event_json, line_number, "finger_id", optionalInt64(event_json, line_number, "finger", 0)),
                requirePoint(event_json, line_number),
                DVec2(
                    optionalNumber(event_json, line_number, "dx", 0.0),
                    optionalNumber(event_json, line_number, "dy", 0.0)),
                static_cast<float>(optionalNumber(event_json, line_number, "pressure", 1.0)),
                optionalInt64(event_json, line_number, "touch_id", 1));
            return;
        }

        if (type == "finger_up" || type == "touch_up")
        {
            input.fingerUp(
                frame,
                optionalInt64(event_json, line_number, "finger_id", optionalInt64(event_json, line_number, "finger", 0)),
                requirePoint(event_json, line_number),
                static_cast<float>(optionalNumber(event_json, line_number, "pressure", 0.0)),
                optionalInt64(event_json, line_number, "touch_id", 1));
            return;
        }

        if (type == "key_down" || type == "keydown")
        {
            const SDL_Keycode key = parseRequiredKey(event_json, line_number);
            input.keyDown(
                frame,
                key,
                parseOptionalScancode(event_json, line_number, key),
                parseOptionalKeyMod(event_json, line_number),
                optionalInt64(event_json, line_number, "repeat", 0) != 0);
            return;
        }

        if (type == "key_up" || type == "keyup")
        {
            const SDL_Keycode key = parseRequiredKey(event_json, line_number);
            input.keyUp(
                frame,
                key,
                parseOptionalScancode(event_json, line_number, key),
                parseOptionalKeyMod(event_json, line_number));
            return;
        }

        throwInputLineError(line_number, "unknown input event type '" + type + "'");
    }

    struct InputReplay
    {
        InputEmulation input;
        std::filesystem::path path;
        std::size_t events_loaded = 0;
        InputEmulation::Frame first_frame = 0;
        InputEmulation::Frame last_frame = 0;

        explicit InputReplay(IVec2 surface_size)
            : input(surface_size)
        {
        }

        [[nodiscard]] JSON::json metadataJson() const
        {
            return {
                {"enabled", true},
                {"format", "jsonl_input_replay_v1"},
                {"file", path.string()},
                {"events_loaded", events_loaded},
                {"first_frame", events_loaded == 0 ? nullptr : JSON::json(first_frame)},
                {"last_frame", events_loaded == 0 ? nullptr : JSON::json(last_frame)}
            };
        }
    };

    [[nodiscard]] InputReplay loadInputReplay(
        const std::filesystem::path& path,
        IVec2 surface_size)
    {
        if (!std::filesystem::exists(path))
            throw std::runtime_error("Input replay file does not exist: " + path.string());
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Input replay path is not a file: " + path.string());

        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("Could not open input replay file: " + path.string());

        InputReplay replay(surface_size);
        replay.path = path;

        std::string line;
        std::size_t line_number = 0;
        while (std::getline(in, line))
        {
            line_number++;
            const std::string trimmed = trimCopy(line);
            if (trimmed.empty() || trimmed.starts_with("#"))
                continue;

            JSON::json event_json;
            try
            {
                event_json = JSON::json::parse(trimmed);
            }
            catch (const std::exception& e)
            {
                throwInputLineError(line_number, e.what());
            }

            appendReplayEvent(replay.input, event_json, line_number);
        }

        replay.events_loaded = replay.input.size();
        if (replay.events_loaded > 0)
        {
            replay.first_frame = replay.input.firstFrame();
            replay.last_frame = replay.input.lastFrame();
        }

        return replay;
    }

    JSON::json traceValueMapToJson(const std::map<std::string, SimTraceValue>& values)
    {
        JSON::json out = JSON::json::object();
        for (const auto& [key, value] : values)
            out[key] = simTraceValueToJson(value);
        return out;
    }

    JSON::json compactObjectState(const SimTraceObject& object)
    {
        JSON::json out = object.toJson();
        out.erase("id");
        return out;
    }

    JSON::json compactEventJson(const SimTraceEvent& event)
    {
        JSON::json out = {{"n", event.name}};
        if (!event.attributes.empty())
            out["a"] = traceValueMapToJson(event.attributes);
        return out;
    }

    class CompactTraceWriter
    {
        struct SceneState
        {
            int id = 0;
            std::string name;
            bool has_emitted_keyframe = false;
            std::int64_t last_keyframe_frame = 0;
            std::map<std::string, JSON::json> objects;
            std::map<std::string, JSON::json> metrics;
        };

        int keyframe_interval = 120;
        std::map<std::string, int> scene_ids;
        std::vector<SceneState> scenes;

        SceneState& sceneState(std::string scene_name)
        {
            auto it = scene_ids.find(scene_name);
            if (it != scene_ids.end())
                return scenes[static_cast<std::size_t>(it->second)];

            const int id = static_cast<int>(scenes.size());
            scene_ids[scene_name] = id;
            scenes.push_back(SceneState{
                .id = id,
                .name = std::move(scene_name)
            });
            return scenes.back();
        }

        [[nodiscard]] bool shouldKeyframe(const SceneState& state, std::int64_t frame) const
        {
            if (!state.has_emitted_keyframe)
                return true;
            if (keyframe_interval <= 0)
                return false;
            return frame - state.last_keyframe_frame >= keyframe_interval;
        }

    public:
        explicit CompactTraceWriter(int keyframe_interval)
            : keyframe_interval(keyframe_interval)
        {
        }

        JSON::json writeFrame(const SimTraceFrame& frame)
        {
            SceneState& state = sceneState(frame.scene);
            const bool keyframe = shouldKeyframe(state, frame.frame);

            JSON::json out = {
                {"f", frame.frame},
                {"s", state.id}
            };
            if (keyframe)
                out["k"] = true;

            JSON::json object_delta = JSON::json::object();
            std::set<std::string> current_object_ids;

            for (const SimTraceObject& object : frame.objects)
            {
                current_object_ids.insert(object.id);
                const JSON::json current = compactObjectState(object);

                JSON::json changed = JSON::json::object();
                auto previous_it = state.objects.find(object.id);
                if (keyframe || previous_it == state.objects.end())
                {
                    changed = current;
                }
                else
                {
                    const JSON::json& previous = previous_it->second;
                    for (auto it = current.begin(); it != current.end(); ++it)
                    {
                        const auto prev_field = previous.find(it.key());
                        if (prev_field == previous.end() || *prev_field != it.value())
                            changed[it.key()] = it.value();
                    }

                    JSON::json unset = JSON::json::array();
                    for (auto it = previous.begin(); it != previous.end(); ++it)
                    {
                        if (!current.contains(it.key()))
                            unset.push_back(it.key());
                    }
                    if (!unset.empty())
                        changed["unset"] = std::move(unset);
                }

                if (!changed.empty())
                    object_delta[object.id] = std::move(changed);

                state.objects[object.id] = current;
            }

            for (auto it = state.objects.begin(); it != state.objects.end();)
            {
                if (current_object_ids.contains(it->first))
                {
                    ++it;
                    continue;
                }

                object_delta[it->first] = nullptr;
                it = state.objects.erase(it);
            }

            if (!object_delta.empty())
                out["o"] = std::move(object_delta);

            const JSON::json current_metrics = traceValueMapToJson(frame.metrics);
            JSON::json metric_delta = JSON::json::object();
            if (keyframe)
            {
                metric_delta = current_metrics;
            }
            else
            {
                for (auto it = current_metrics.begin(); it != current_metrics.end(); ++it)
                {
                    const auto prev_metric = state.metrics.find(it.key());
                    if (prev_metric == state.metrics.end() || prev_metric->second != it.value())
                        metric_delta[it.key()] = it.value();
                }

                JSON::json removed_metrics = JSON::json::array();
                for (const auto& [name, value] : state.metrics)
                {
                    (void)value;
                    if (!current_metrics.contains(name))
                        removed_metrics.push_back(name);
                }
                if (!removed_metrics.empty())
                    out["mr"] = std::move(removed_metrics);
            }

            if (!metric_delta.empty())
                out["m"] = std::move(metric_delta);

            state.metrics.clear();
            for (auto it = current_metrics.begin(); it != current_metrics.end(); ++it)
                state.metrics[it.key()] = it.value();

            if (!frame.events.empty())
            {
                JSON::json events = JSON::json::array();
                for (const SimTraceEvent& event : frame.events)
                    events.push_back(compactEventJson(event));
                out["e"] = std::move(events);
            }

            if (frame.dropped_objects > 0)
                out["d"] = frame.dropped_objects;

            state.has_emitted_keyframe = true;
            if (keyframe)
                state.last_keyframe_frame = frame.frame;

            if (out.size() <= (keyframe ? 3 : 2))
                return nullptr;

            return out;
        }

        [[nodiscard]] JSON::json metadataJson() const
        {
            JSON::json scenes_json = JSON::json::object();
            for (const SceneState& scene : scenes)
            {
                JSON::json objects = JSON::json::array();
                for (const auto& [id, state] : scene.objects)
                {
                    (void)state;
                    objects.push_back(id);
                }

                scenes_json[std::to_string(scene.id)] = {
                    {"name", scene.name},
                    {"objects_seen", std::move(objects)}
                };
            }

            return scenes_json;
        }
    };

    void writeJsonFile(const std::filesystem::path& path, const JSON::json& json)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
            throw std::runtime_error("Could not open output file: " + path.string());

        out << json.dump(2) << '\n';
    }

    JSON::json readJsonFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("Could not open JSON file: " + path.string());
        return JSON::json::parse(in);
    }

    std::string sanitizeFilename(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (char c : text)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || c == '_' || c == '-' || c == '.')
                out.push_back(c);
            else
                out.push_back('_');
        }
        if (out.empty())
            out = "snapshot";
        return out;
    }

    std::string frameDirectoryName(std::int64_t frame)
    {
        std::ostringstream out;
        out << "frame_" << std::setw(8) << std::setfill('0') << frame;
        return out.str();
    }

    IRect normalizedClampedRect(IRect rect, IVec2 bounds)
    {
        const int x1 = std::clamp(std::min(rect.x1, rect.x2), 0, bounds.x);
        const int y1 = std::clamp(std::min(rect.y1, rect.y2), 0, bounds.y);
        const int x2 = std::clamp(std::max(rect.x1, rect.x2), 0, bounds.x);
        const int y2 = std::clamp(std::max(rect.y1, rect.y2), 0, bounds.y);
        return IRect(x1, y1, x2, y2);
    }

    bytebuf flipRgbaTopDown(const bytebuf& bottom_up, int width, int height)
    {
        bytebuf out(bottom_up.size());
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        for (int y = 0; y < height; y++)
        {
            const std::size_t src_offset = static_cast<std::size_t>(height - 1 - y) * stride;
            const std::size_t dst_offset = static_cast<std::size_t>(y) * stride;
            std::memcpy(out.data() + dst_offset, bottom_up.data() + src_offset, stride);
        }
        return out;
    }

    bytebuf cropRgbaTopDown(const bytebuf& rgba, int source_w, IRect rect)
    {
        const int crop_w = static_cast<int>(rect.width());
        const int crop_h = static_cast<int>(rect.height());
        bytebuf out(static_cast<std::size_t>(crop_w) * static_cast<std::size_t>(crop_h) * 4);

        const std::size_t src_stride = static_cast<std::size_t>(source_w) * 4;
        const std::size_t dst_stride = static_cast<std::size_t>(crop_w) * 4;
        for (int y = 0; y < crop_h; y++)
        {
            const std::size_t src_offset =
                (static_cast<std::size_t>(rect.y1 + y) * src_stride) +
                (static_cast<std::size_t>(rect.x1) * 4);
            const std::size_t dst_offset = static_cast<std::size_t>(y) * dst_stride;
            std::memcpy(out.data() + dst_offset, rgba.data() + src_offset, dst_stride);
        }

        return out;
    }

    class HeadlessRenderContext
    {
        SDL_Window* window = nullptr;
        SDL_GLContext gl_context = nullptr;
        std::unique_ptr<PlatformManager> platform_manager;
        bool initialized_video = false;

    public:
        explicit HeadlessRenderContext(IVec2 surface)
        {
            if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
            {
                if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                    throw std::runtime_error(std::string("SDL_InitSubSystem failed: ") + SDL_GetError());
                initialized_video = true;
            }

            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

            window = SDL_CreateWindow(
                "bitloop headless trace",
                surface.x,
                surface.y,
                SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);

            if (!window)
                throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

            gl_context = SDL_GL_CreateContext(window);
            if (!gl_context)
                throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());

            SDL_GL_MakeCurrent(window, gl_context);
            SDL_GL_SetSwapInterval(0);

            #ifndef __EMSCRIPTEN__
            if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
                throw std::runtime_error("Failed to initialize GLAD for headless snapshots");
            glDisable(GL_FRAMEBUFFER_SRGB);
            #endif

            platform_manager = std::make_unique<PlatformManager>(window);
            platform_manager->init();
        }

        ~HeadlessRenderContext()
        {
            platform_manager.reset();
            if (gl_context)
                SDL_GL_DestroyContext(gl_context);
            if (window)
                SDL_DestroyWindow(window);
            if (initialized_video)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };

    bool encodeSnapshotRecord(
        ProjectBase& project,
        NanoCanvas& canvas,
        const std::filesystem::path& trace_dir,
        std::ofstream& snapshots_out,
        SimSnapshotRecord& record,
        int snapshot_index)
    {
        bytebuf pixels;
        std::string source_payload;
        if (!record.route.empty())
        {
            SimSnapshotImage image;
            if (!project.captureTraceSnapshot(record, image))
            {
                snapshots_out << record.toJson().dump() << '\n';
                return false;
            }
            pixels = std::move(image.pixels);
            source_payload = std::move(image.payload);
        }
        else
        {
            bool rendered = false;
            if (record.scene_id.empty())
            {
                if (record.surface.x <= 0 || record.surface.y <= 0)
                    record.surface = IVec2(canvas.fboWidth(), canvas.fboHeight());
                rendered = project.renderLayout(canvas, record.surface, record.error);
            }
            else
            {
                rendered = project.renderViewport(canvas, record);
            }

            if (!rendered)
            {
                snapshots_out << record.toJson().dump() << '\n';
                return false;
            }

            record.surface = IVec2(canvas.fboWidth(), canvas.fboHeight());
            bytebuf pixels_bottom_up;
            if (!canvas.readPixels(pixels_bottom_up))
            {
                record.error = "Could not read canvas pixels";
                snapshots_out << record.toJson().dump() << '\n';
                return false;
            }

            pixels = flipRgbaTopDown(pixels_bottom_up, record.surface.x, record.surface.y);
        }
        IVec2 output_size = record.surface;

        if (record.pixel_rect)
        {
            const IRect rect = normalizedClampedRect(*record.pixel_rect, record.surface);
            record.pixel_rect = rect;
            if (rect.width() <= 0 || rect.height() <= 0)
            {
                record.error = "Snapshot pixel_rect is empty after clamping";
                snapshots_out << record.toJson().dump() << '\n';
                return false;
            }

            pixels = cropRgbaTopDown(pixels, record.surface.x, rect);
            output_size = IVec2(static_cast<int>(rect.width()), static_cast<int>(rect.height()));
        }

        record.resolution = output_size;

        CapturePreset preset;
        if (const CapturePreset* viewport_preset = project.snapshotPresetManager().allPresets()["viewport"])
            preset = *viewport_preset;
        else
            preset = CapturePreset("Headless Snapshot", "viewport", output_size);
        preset.setResolution(output_size);
        preset.setVideo(false);

        EncodeFrame frame(pixels);
        frame.payload = std::move(source_payload);
        project.onHeadlessEncodeFrame(frame, preset);

        bytebuf encoded;
        const bool encoded_ok = webp_snapshot_encode_rgba(
            frame.frameData(),
            output_size.x,
            output_size.y,
            100.0f,
            6,
            true,
            100,
            100,
            encoded);

        if (!encoded_ok)
        {
            record.error = "WebP snapshot encoding failed";
            snapshots_out << record.toJson().dump() << '\n';
            return false;
        }

        const std::string filename =
            "snapshots/" +
            frameDirectoryName(record.frame) +
            "_" +
            std::to_string(snapshot_index) +
            "_" +
            sanitizeFilename(record.id) +
            ".webp";

        record.filename = filename;
        record.rendered = true;

        if (!webp_set_save_string_as_xmp_inplace(
            encoded,
            frame.payload.empty() ? record.toJson().dump() : frame.payload))
            throw std::runtime_error("Could not embed snapshot XMP state");

        const std::filesystem::path out_path = trace_dir / filename;
        std::filesystem::create_directories(out_path.parent_path());
        std::ofstream out(out_path, std::ios::binary);
        if (!out)
            throw std::runtime_error("Could not write snapshot file: " + out_path.string());
        out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));

        snapshots_out << record.toJson().dump() << '\n';
        return true;
    }

    bool parseInt(std::string_view text, int& out)
    {
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const auto result = std::from_chars(first, last, out);
        return result.ec == std::errc{} && result.ptr == last;
    }

    bool parseSize(std::string_view text, std::size_t& out)
    {
        unsigned long long value = 0;
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const auto result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last)
            return false;
        out = static_cast<std::size_t>(value);
        return true;
    }

    bool parseInt64(std::string_view text, std::int64_t& out)
    {
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const auto result = std::from_chars(first, last, out);
        return result.ec == std::errc{} && result.ptr == last;
    }

    bool parseFrameList(std::string_view text, std::vector<std::int64_t>& out)
    {
        out.clear();
        while (!text.empty())
        {
            const std::size_t comma = text.find(',');
            const std::string_view part =
                comma == std::string_view::npos ? text : text.substr(0, comma);

            std::int64_t value = 0;
            if (part.empty() || !parseInt64(part, value))
                return false;
            out.push_back(value);

            if (comma == std::string_view::npos)
                break;
            text.remove_prefix(comma + 1);
        }
        return true;
    }

    class TraceSchedule
    {
        int every_frames = 1;
        std::int64_t start_frame = 0;
        std::int64_t end_frame = 0;
        std::vector<std::int64_t> at_frames;

    public:
        TraceSchedule(const HeadlessRunOptions& options, std::int64_t start_frame)
            : every_frames(options.trace_every_frames.value_or(options.trace_at_frames.empty() ? 1 : 0)),
              start_frame(start_frame),
              end_frame(start_frame + static_cast<std::int64_t>(options.frames) - 1),
              at_frames(options.trace_at_frames)
        {
            std::ranges::sort(at_frames);
            at_frames.erase(std::unique(at_frames.begin(), at_frames.end()), at_frames.end());
            for (std::int64_t frame : at_frames)
            {
                if (frame < start_frame || frame > end_frame)
                {
                    throw std::runtime_error(
                        "Trace frame " + std::to_string(frame) +
                        " is outside run range [" + std::to_string(start_frame) +
                        ", " + std::to_string(end_frame) + "]");
                }
            }
        }

        [[nodiscard]] bool contains(std::int64_t frame) const
        {
            return (every_frames > 0 && ((frame - start_frame) % every_frames) == 0) ||
                std::binary_search(at_frames.begin(), at_frames.end(), frame);
        }

        [[nodiscard]] std::string_view mode() const
        {
            if (every_frames == 1)
                return "all";
            if (every_frames == 0)
                return at_frames.empty() ? "none" : "selected";
            return at_frames.empty() ? "periodic" : "combined";
        }

        [[nodiscard]] std::string_view summaryScope() const
        {
            if (mode() == "all")
                return "all_processed_frames";
            if (mode() == "none")
                return "no_semantic_frames";
            return "selected_frames";
        }

        [[nodiscard]] JSON::json metadataJson() const
        {
            return {
                {"mode", std::string(mode())},
                {"every_frames", every_frames},
                {"at_frames", at_frames},
                {"frame_indexing", "absolute_zero_based_post_process"}
            };
        }
    };

    bool parseDouble(std::string_view text, double& out)
    {
        std::string tmp(text);
        char* end = nullptr;
        out = std::strtod(tmp.c_str(), &end);
        return end == tmp.c_str() + tmp.size();
    }

    bool parseSurface(std::string_view text, IVec2& out)
    {
        const std::size_t x_pos = text.find('x');
        const std::size_t upper_x_pos = text.find('X');
        const std::size_t split = x_pos == std::string_view::npos ? upper_x_pos : x_pos;
        if (split == std::string_view::npos)
            return false;

        int w = 0;
        int h = 0;
        if (!parseInt(text.substr(0, split), w) || !parseInt(text.substr(split + 1), h))
            return false;

        out = IVec2(w, h);
        return true;
    }

    std::shared_ptr<ProjectInfo> findProjectByCliName(const std::string& name)
    {
        for (const std::shared_ptr<ProjectInfo>& info : ProjectBase::projectInfoList())
        {
            if (!info)
                continue;
            if (info->name == name || (!info->path.empty() && info->path.back() == name))
                return info;
        }

        return nullptr;
    }

    std::string availableProjectNames()
    {
        std::ostringstream out;
        bool first = true;
        for (const std::shared_ptr<ProjectInfo>& info : ProjectBase::projectInfoList())
        {
            if (!info)
                continue;
            if (!first)
                out << ", ";
            first = false;
            out << (!info->name.empty() ? info->name : info->path.back());
        }
        return out.str();
    }

    void printHeadlessUsage(std::ostream& out)
    {
        out
            << "Usage: app --headless --project NAME --frames N --trace DIR [options]\n"
            << "       app --headless --project NAME --list-scenes\n"
            << "Options:\n"
            << "  --list-scenes          List declarative Scene IDs without constructing Scenes\n"
            << "  --scene ID             Select one Scene; repeat to select several\n"
            << "  --scene-config FILE    Strict JSON override for one selected Scene\n"
            << "  --dt SECONDS           Fixed simulation dt. Default: 0.0166667\n"
            << "  --surface WIDTHxHEIGHT Virtual viewport surface. Default: 800x600\n"
            << "  --input FILE           Replay client-space input JSONL events/gestures\n"
            << "  --max-objects N        Per-scene object cap. Default: 100000\n"
            << "  --trace-format compact|full\n"
            << "  --trace-every-frames N  Trace every N processed frames from run start; 0 disables periodic tracing\n"
            << "  --trace-at A,B,C        Trace exact absolute post-process frames; alone selects only these frames\n"
            << "  --trace-keyframe-frames N  Compact full-state interval. Default: 120, 0 disables periodic keyframes\n"
            << "  --snapshots            Save traceCanvas()/traceSnapshot() requests as WebP files\n"
            << "  --snapshot-every-frames N\n"
            << "  --snapshot-every-seconds SECONDS\n"
            << "  --snapshot-route SCENE:ROUTE  Custom source for periodic snapshots; repeat for several\n"
            << "  --checkpoint-every-frames N\n"
            << "  --checkpoint-at A,B,C\n"
            << "  --checkpoint-dir DIR   Default: TRACE/checkpoints\n"
            << "  --restore-checkpoint DIR\n";
    }

    bool parseHeadlessArgs(int argc, char* argv[], HeadlessRunOptions& options, std::string& error)
    {
        for (int i = 1; i < argc; i++)
        {
            const std::string_view arg(argv[i]);
            if (arg == "--headless")
                continue;

            auto requireValue = [&](std::string_view flag) -> const char*
            {
                if (i + 1 >= argc)
                {
                    error = "Missing value for " + std::string(flag);
                    return nullptr;
                }
                return argv[++i];
            };

            if (arg == "--project")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                options.project = value;
            }
            else if (arg == "--list-scenes")
            {
                options.list_scenes = true;
            }
            else if (arg == "--scene")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                options.scene_ids.emplace_back(value);
            }
            else if (arg == "--scene-config")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!options.scene_config_path.empty())
                {
                    error = "--scene-config may be specified only once";
                    return false;
                }
                options.scene_config_path = value;
            }
            else if (arg == "--frames")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseInt(value, options.frames))
                {
                    error = "Invalid --frames value";
                    return false;
                }
            }
            else if (arg == "--dt")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseDouble(value, options.dt))
                {
                    error = "Invalid --dt value";
                    return false;
                }
            }
            else if (arg == "--surface")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseSurface(value, options.surface))
                {
                    error = "Invalid --surface value";
                    return false;
                }
            }
            else if (arg == "--trace")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                options.trace_dir = value;
            }
            else if (arg == "--input")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                options.input_path = value;
            }
            else if (arg == "--max-objects")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseSize(value, options.max_objects))
                {
                    error = "Invalid --max-objects value";
                    return false;
                }
            }
            else if (arg == "--trace-format")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseTraceFormat(value, options.trace_format))
                {
                    error = "Invalid --trace-format value";
                    return false;
                }
            }
            else if (arg == "--trace-every-frames")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                int every_frames = 0;
                if (!parseInt(value, every_frames))
                {
                    error = "Invalid --trace-every-frames value";
                    return false;
                }
                options.trace_every_frames = every_frames;
            }
            else if (arg == "--trace-at")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!options.trace_at_frames.empty())
                {
                    error = "--trace-at may be specified only once";
                    return false;
                }
                if (!parseFrameList(value, options.trace_at_frames))
                {
                    error = "Invalid --trace-at value";
                    return false;
                }
            }
            else if (arg == "--trace-keyframe-frames")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseInt(value, options.trace_keyframe_interval))
                {
                    error = "Invalid --trace-keyframe-frames value";
                    return false;
                }
            }
            else if (arg == "--snapshots")
            {
                options.enable_snapshots = true;
            }
            else if (arg == "--snapshot-every-frames")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseInt(value, options.snapshot_every_frames))
                {
                    error = "Invalid --snapshot-every-frames value";
                    return false;
                }
                options.enable_snapshots = true;
            }
            else if (arg == "--snapshot-every-seconds")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseDouble(value, options.snapshot_every_seconds))
                {
                    error = "Invalid --snapshot-every-seconds value";
                    return false;
                }
                options.enable_snapshots = true;
            }
            else if (arg == "--snapshot-route")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                const std::string_view target(value);
                const std::size_t separator = target.find(':');
                if (separator == std::string_view::npos || separator == 0 || separator + 1 == target.size())
                {
                    error = "Invalid --snapshot-route; expected SCENE:ROUTE";
                    return false;
                }
                options.snapshot_routes.push_back({ std::string(target.substr(0, separator)), std::string(target.substr(separator + 1)) });
                options.enable_snapshots = true;
            }
            else if (arg == "--checkpoint-every-frames")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseInt(value, options.checkpoint_every_frames))
                {
                    error = "Invalid --checkpoint-every-frames value";
                    return false;
                }
            }
            else if (arg == "--checkpoint-at")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                if (!parseFrameList(value, options.checkpoint_at_frames))
                {
                    error = "Invalid --checkpoint-at value";
                    return false;
                }
            }
            else if (arg == "--checkpoint-dir")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                options.checkpoint_dir = value;
            }
            else if (arg == "--restore-checkpoint")
            {
                const char* value = requireValue(arg);
                if (!value) return false;
                options.restore_checkpoint = value;
            }
            else if (arg == "--help" || arg == "-h")
            {
                error = "help";
                return false;
            }
            else
            {
                error = "Unknown headless option: " + std::string(arg);
                return false;
            }
        }

        if (options.project.empty())
        {
            error = "Missing required --project";
            return false;
        }

        std::set<std::string> unique_scenes;
        for (const std::string& scene_id : options.scene_ids)
        {
            if (!unique_scenes.insert(scene_id).second)
            {
                error = "Duplicate --scene ID: " + scene_id;
                return false;
            }
        }

        if (options.list_scenes)
        {
            if (!options.scene_ids.empty() || !options.scene_config_path.empty())
            {
                error = "--list-scenes cannot be combined with --scene or --scene-config";
                return false;
            }
            return true;
        }

        if (!options.scene_config_path.empty() && options.scene_ids.size() != 1)
        {
            error = "--scene-config requires exactly one --scene ID";
            return false;
        }

        if (options.trace_dir.empty())
        {
            error = "Missing required --trace";
            return false;
        }
        if (options.frames <= 0)
        {
            error = "--frames must be greater than zero";
            return false;
        }
        if (!(options.dt > 0.0))
        {
            error = "--dt must be greater than zero";
            return false;
        }
        if (options.surface.x <= 0 || options.surface.y <= 0)
        {
            error = "--surface dimensions must be greater than zero";
            return false;
        }
        if (options.max_objects == 0)
        {
            error = "--max-objects must be greater than zero";
            return false;
        }
        if (options.trace_keyframe_interval < 0)
        {
            error = "--trace-keyframe-frames must be zero or greater";
            return false;
        }
        if (options.trace_every_frames && *options.trace_every_frames < 0)
        {
            error = "--trace-every-frames must be zero or greater";
            return false;
        }
        for (std::int64_t frame : options.trace_at_frames)
        {
            if (frame < 0)
            {
                error = "--trace-at frames must be zero or greater";
                return false;
            }
        }
        if (options.snapshot_every_frames < 0)
        {
            error = "--snapshot-every-frames must be zero or greater";
            return false;
        }
        if (options.snapshot_every_seconds < 0.0)
        {
            error = "--snapshot-every-seconds must be zero or greater";
            return false;
        }
        if (options.checkpoint_every_frames < 0)
        {
            error = "--checkpoint-every-frames must be zero or greater";
            return false;
        }
        for (std::int64_t frame : options.checkpoint_at_frames)
        {
            if (frame < 0)
            {
                error = "--checkpoint-at frames must be zero or greater";
                return false;
            }
        }

        return true;
    }

    bool printSceneCatalogue(const HeadlessRunOptions& options, std::ostream& out, std::string& error)
    {
        try
        {
            const std::shared_ptr<ProjectInfo> info = findProjectByCliName(options.project);
            if (!info || !info->creator)
            {
                const std::string available = availableProjectNames();
                throw std::runtime_error(
                    "Unknown project: " + options.project +
                    (available.empty() ? "" : ". Available projects: " + available));
            }

            ImDebugLog project_log;
            CaptureContext capture_context;
            std::unique_ptr<ProjectBase> project(info->creator());
            project->configureHeadless(info->sim_uid, &project_log, options.surface, &capture_context);
            project->prepareHeadlessSceneCatalogue();

            JSON::json scenes = JSON::json::array();
            for (const SceneCatalogueEntry& scene : project->sceneCatalogue())
            {
                scenes.push_back({
                    {"id", scene.id},
                    {"ordinal", scene.ordinal},
                    {"selectable", scene.selectable},
                    {"default", scene.selected_by_default}
                });
            }

            out << JSON::json({
                {"project", !info->name.empty() ? info->name : info->path.back()},
                {"scenes", std::move(scenes)}
            }).dump(2) << '\n';
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}

bool isHeadlessCommand(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (std::string_view(argv[i]) == "--headless")
            return true;
    }
    return false;
}

HeadlessRunResult runHeadlessTrace(const HeadlessRunOptions& options)
{
    HeadlessRunResult result;
    result.trace_dir = options.trace_dir;
    std::unique_ptr<ProjectBase> project;
    bool project_started = false;

    try
    {
        if (options.project.empty())
            throw std::runtime_error("Missing project name");
        if (options.frames <= 0)
            throw std::runtime_error("Frame count must be greater than zero");
        if (!(options.dt > 0.0))
            throw std::runtime_error("dt must be greater than zero");
        if (options.surface.x <= 0 || options.surface.y <= 0)
            throw std::runtime_error("Surface dimensions must be greater than zero");
        if (options.trace_dir.empty())
            throw std::runtime_error("Trace output directory is required");
        if (options.max_objects == 0)
            throw std::runtime_error("Max object count must be greater than zero");
        if (options.trace_keyframe_interval < 0)
            throw std::runtime_error("Trace keyframe interval must be zero or greater");
        if (options.trace_every_frames && *options.trace_every_frames < 0)
            throw std::runtime_error("Trace frame interval must be zero or greater");
        if (std::ranges::any_of(options.trace_at_frames, [](std::int64_t frame) { return frame < 0; }))
            throw std::runtime_error("Explicit trace frames must be zero or greater");
        if (options.snapshot_every_frames < 0)
            throw std::runtime_error("Snapshot frame interval must be zero or greater");
        if (options.snapshot_every_seconds < 0.0)
            throw std::runtime_error("Snapshot time interval must be zero or greater");
        if (options.checkpoint_every_frames < 0)
            throw std::runtime_error("Checkpoint frame interval must be zero or greater");
        if (!options.scene_config_path.empty() && options.scene_ids.size() != 1)
            throw std::runtime_error("Scene config requires exactly one selected Scene ID");

        if (std::filesystem::exists(options.trace_dir) && !std::filesystem::is_directory(options.trace_dir))
            throw std::runtime_error("Trace path exists but is not a directory: " + options.trace_dir.string());
        std::filesystem::create_directories(options.trace_dir);

        std::optional<InputReplay> input_replay;
        if (!options.input_path.empty())
            input_replay = loadInputReplay(options.input_path, options.surface);

        std::optional<JSON::json> scene_config;
        if (!options.scene_config_path.empty())
        {
            try
            {
                scene_config = readJsonFile(options.scene_config_path);
            }
            catch (const JSON::json::parse_error& error)
            {
                throw std::runtime_error("Malformed Scene config JSON: " + std::string(error.what()));
            }

            if (!scene_config->is_object())
                throw std::runtime_error("Scene config JSON must contain one top-level object");
        }

        const bool checkpoint_requested =
            options.checkpoint_every_frames > 0 ||
            !options.checkpoint_at_frames.empty();
        const std::filesystem::path checkpoint_root =
            options.checkpoint_dir.empty() ? (options.trace_dir / "checkpoints") : options.checkpoint_dir;

        if (checkpoint_requested &&
            std::filesystem::exists(checkpoint_root) &&
            !std::filesystem::is_directory(checkpoint_root))
        {
            throw std::runtime_error("Checkpoint path exists but is not a directory: " + checkpoint_root.string());
        }

        JSON::json restore_metadata = nullptr;
        std::int64_t start_frame = 0;
        double start_time = 0.0;
        if (!options.restore_checkpoint.empty())
        {
            if (!std::filesystem::is_directory(options.restore_checkpoint))
                throw std::runtime_error("Restore checkpoint is not a directory: " + options.restore_checkpoint.string());
            restore_metadata = readJsonFile(options.restore_checkpoint / "metadata.json");
            if (restore_metadata.contains("frame"))
                start_frame = restore_metadata["frame"].get<std::int64_t>() + 1;
            if (restore_metadata.contains("time"))
                start_time = restore_metadata["time"].get<double>();
        }

        const std::shared_ptr<ProjectInfo> info = findProjectByCliName(options.project);
        if (!info || !info->creator)
        {
            const std::string available = availableProjectNames();
            throw std::runtime_error(
                "Unknown project: " + options.project +
                (available.empty() ? "" : ". Available projects: " + available));
        }

        const std::string project_name = !info->name.empty() ? info->name : info->path.back();

        if (restore_metadata.is_object() &&
            restore_metadata.contains("project") &&
            restore_metadata["project"].get<std::string>() != project_name)
        {
            throw std::runtime_error(
                "Checkpoint project mismatch: expected " + project_name +
                ", checkpoint is " + restore_metadata["project"].get<std::string>());
        }

        const TraceSchedule trace_schedule(options, start_frame);

        JSON::json metadata_json = {
            {"schema_version", 4},
            {"project", project_name},
            {"frames_requested", options.frames},
            {"start_frame", start_frame},
            {"start_time", start_time},
            {"dt", options.dt},
            {"surface", {
                {"width", options.surface.x},
                {"height", options.surface.y}
            }},
            {"max_objects", options.max_objects},
            {"scene_config", {
                {"enabled", scene_config.has_value()},
                {"scene_id", ""},
                {"path", options.scene_config_path.string()},
                {"values", scene_config ? *scene_config : JSON::json(nullptr)},
                {"hash", ""}
            }},
            {"input", input_replay
                ? input_replay->metadataJson()
                : JSON::json({{"enabled", false}, {"format", "jsonl_input_replay_v1"}})},
            {"snapshots", {
                {"enabled", options.enable_snapshots},
                {"every_frames", options.snapshot_every_frames},
                {"every_seconds", options.snapshot_every_seconds}
            }},
            {"checkpoints", {
                {"every_frames", options.checkpoint_every_frames},
                {"checkpoint_at_frames", options.checkpoint_at_frames},
                {"checkpoint_dir", checkpoint_root.string()},
                {"restore_checkpoint", options.restore_checkpoint.string()}
            }},
            {"trace", {
                {"format", traceFormatName(options.trace_format)},
                {"frames_file", "frames.jsonl"},
                {"keyframe_interval", options.trace_keyframe_interval},
                {"schedule", trace_schedule.metadataJson()},
                {"summary_scope", std::string(trace_schedule.summaryScope())},
                {"frames_observed", 0},
                {"scene_frames_observed", 0},
                {"line_keys", {
                    {"f", "frame"},
                    {"s", "scene id"},
                    {"k", "keyframe/full state for this scene"},
                    {"o", "object deltas keyed by object id; null removes an object"},
                    {"m", "metric deltas"},
                    {"mr", "removed metric names"},
                    {"e", "events"},
                    {"d", "dropped object count"}
                }},
                {"scenes", JSON::json::object()}
            }}
        };

        std::ofstream frames_out(options.trace_dir / "frames.jsonl", std::ios::binary);
        if (!frames_out)
            throw std::runtime_error("Could not open frames.jsonl for writing");
        std::ofstream snapshots_out(options.trace_dir / "snapshots.jsonl", std::ios::binary);
        if (!snapshots_out)
            throw std::runtime_error("Could not open snapshots.jsonl for writing");
        std::ofstream checkpoints_out(options.trace_dir / "checkpoints.jsonl", std::ios::binary);
        if (!checkpoints_out)
            throw std::runtime_error("Could not open checkpoints.jsonl for writing");

        std::optional<HeadlessRenderContext> render_context;
        std::unique_ptr<NanoCanvas> render_canvas;
        std::unique_ptr<PlatformManager> headless_platform;
        if (options.enable_snapshots)
        {
            render_context.emplace(options.surface);
            render_canvas = std::make_unique<NanoCanvas>();
            render_canvas->create(1.0);
            if (!render_canvas->resize(options.surface.x, options.surface.y))
                throw std::runtime_error("Could not allocate headless snapshot canvas");
        }
        else
        {
            headless_platform = std::make_unique<PlatformManager>(nullptr);
        }

        TraceSummary summary;
        CaptureContext capture_context;
        SnapshotPresetList& headless_presets = capture_context.presetManager().allPresets();
        if (headless_presets.size() > 0)
            headless_presets[0].setResolution(options.surface);
        capture_context.settings().updateRecordBitrate();

        project.reset(info->creator());
        project->configureHeadless(info->sim_uid, &project_log, options.surface, &capture_context, render_canvas.get());
        if (!options.scene_ids.empty())
            project->setHeadlessSceneSelection(options.scene_ids);
        if (scene_config)
            project->setHeadlessSceneConfig(*scene_config);
        project->startHeadless();
        project_started = true;

        const std::vector<std::string> selected_scene_ids = project->selectedSceneIds();
        metadata_json["snapshots"]["routes"] = JSON::json::array();
        for (const HeadlessSnapshotRoute& target : options.snapshot_routes)
        {
            if (target.route.empty() || std::find(selected_scene_ids.begin(), selected_scene_ids.end(), target.scene_id) == selected_scene_ids.end())
                throw std::runtime_error("Snapshot route requires an active Scene ID and a non-empty route: '" + target.scene_id + ":" + target.route + "'");
            metadata_json["snapshots"]["routes"].push_back({ {"scene_id", target.scene_id}, {"route", target.route} });
        }
        metadata_json["selected_scene_ids"] = selected_scene_ids;
        if (scene_config)
            metadata_json["scene_config"]["scene_id"] = selected_scene_ids.front();
        metadata_json["scene_config"]["hash"] = project->headlessSceneConfigHash();
        writeJsonFile(options.trace_dir / "metadata.json", metadata_json);

        if (!options.restore_checkpoint.empty())
            project->loadHeadlessCheckpoint(options.restore_checkpoint);

        const std::set<std::int64_t> explicit_checkpoints(
            options.checkpoint_at_frames.begin(),
            options.checkpoint_at_frames.end());

        double next_snapshot_time =
            options.snapshot_every_seconds > 0.0
                ? (std::floor(start_time / options.snapshot_every_seconds) + 1.0) * options.snapshot_every_seconds
                : std::numeric_limits<double>::infinity();
        int snapshot_index = 0;
        CompactTraceWriter compact_writer(options.trace_keyframe_interval);
        std::int64_t input_events_queued = 0;
        std::int64_t trace_frames = 0;

        for (int i = 0; i < options.frames; i++)
        {
            const std::int64_t frame_index = start_frame + i;
            const double time = start_time + static_cast<double>(i + 1) * options.dt;
            project->setTraceFrameContext(frame_index, time, options.dt);
            if (input_replay)
                input_events_queued += static_cast<std::int64_t>(input_replay->input.queueFrame(*project, frame_index));
            project->processHeadlessFrame(options.dt);
            if (trace_schedule.contains(frame_index))
            {
                trace_frames++;
                std::vector<SimTraceFrame> frames = project->collectTraceFrames(
                    frame_index,
                    time,
                    options.dt,
                    options.max_objects);

                for (const SimTraceFrame& frame : frames)
                {
                    summary.update(frame);
                    if (options.trace_format == HeadlessTraceFormat::Full)
                    {
                        frames_out << frame.toJsonLine() << '\n';
                    }
                    else
                    {
                        const JSON::json compact_frame = compact_writer.writeFrame(frame);
                        if (!compact_frame.is_null())
                            frames_out << compact_frame.dump() << '\n';
                    }
                }
            }

            std::vector<SimSnapshotRecord> snapshot_records = project->takeTraceSnapshotRequests();

            const bool periodic_frame_snapshot =
                options.snapshot_every_frames > 0 &&
                ((i + 1) % options.snapshot_every_frames) == 0;
            bool periodic_time_snapshot = false;
            if (options.snapshot_every_seconds > 0.0 && time + 1e-12 >= next_snapshot_time)
            {
                periodic_time_snapshot = true;
                while (time + 1e-12 >= next_snapshot_time)
                    next_snapshot_time += options.snapshot_every_seconds;
            }

            if (periodic_frame_snapshot || periodic_time_snapshot)
            {
                SimSnapshotRecord record;
                record.frame = frame_index;
                record.time = time;
                record.dt = options.dt;
                record.project = project_name;
                record.scene = "project";
                record.id = periodic_frame_snapshot ? "periodic_frame" : "periodic_time";
                record.reason = periodic_frame_snapshot
                    ? "snapshot-every-frames"
                    : "snapshot-every-seconds";
                record.surface = options.surface;
                if (options.snapshot_routes.empty())
                    snapshot_records.push_back(std::move(record));
                else
                {
                    for (const HeadlessSnapshotRoute& target : options.snapshot_routes)
                    {
                        record.scene_id = target.scene_id;
                        record.route = target.route;
                        record.id = target.route;
                        snapshot_records.push_back(record);
                    }
                }
            }

            for (SimSnapshotRecord& record : snapshot_records)
            {
                summary.snapshot_requests++;
                if (!options.enable_snapshots || !render_canvas)
                {
                    record.error = "Snapshots disabled; pass --snapshots or a snapshot interval to save snapshot requests";
                    snapshots_out << record.toJson().dump() << '\n';
                    summary.snapshots_failed++;
                    continue;
                }

                if (encodeSnapshotRecord(
                    *project,
                    *render_canvas,
                    options.trace_dir,
                    snapshots_out,
                    record,
                    snapshot_index++))
                {
                    summary.snapshots_rendered++;
                }
                else
                {
                    summary.snapshots_failed++;
                }
            }

            const bool checkpoint_due =
                checkpoint_requested &&
                (explicit_checkpoints.contains(frame_index) ||
                 (options.checkpoint_every_frames > 0 && ((i + 1) % options.checkpoint_every_frames) == 0));

            if (checkpoint_due)
            {
                const std::filesystem::path checkpoint_path =
                    checkpoint_root / frameDirectoryName(frame_index);
                const bool saved_state = project->saveHeadlessCheckpoint(
                    checkpoint_path,
                    frame_index,
                    time,
                    options.dt);

                summary.checkpoints_written++;
                if (!saved_state)
                    summary.checkpoints_without_state++;

                checkpoints_out << JSON::json({
                    {"frame", frame_index},
                    {"time", time},
                    {"dt", options.dt},
                    {"path", checkpoint_path.string()},
                    {"saved_state", saved_state}
                }).dump() << '\n';
            }
        }

        project->stopHeadless();
        project_started = false;
        frames_out.close();
        snapshots_out.close();
        checkpoints_out.close();

        JSON::json summary_json = summary.toJson();
        summary_json["ok"] = true;
        summary_json["frames_requested"] = options.frames;
        summary_json["processed_frames"] = options.frames;
        summary_json["trace_frames"] = trace_frames;
        summary_json["trace_summary_scope"] = std::string(trace_schedule.summaryScope());
        summary_json["start_frame"] = start_frame;
        summary_json["end_frame"] = start_frame + options.frames - 1;
        summary_json["input"] = {
            {"enabled", input_replay.has_value()},
            {"events_loaded", input_replay ? input_replay->events_loaded : 0},
            {"events_queued", input_events_queued}
        };
        writeJsonFile(options.trace_dir / "summary.json", summary_json);

        metadata_json["trace"]["frames_observed"] = trace_frames;
        metadata_json["trace"]["scene_frames_observed"] = summary.scene_frames;
        if (options.trace_format == HeadlessTraceFormat::CompactDelta)
            metadata_json["trace"]["scenes"] = compact_writer.metadataJson();
        writeJsonFile(options.trace_dir / "metadata.json", metadata_json);

        result.exit_code = 0;
        result.processed_frames = options.frames;
        result.trace_frames = trace_frames;
        result.scene_frames = summary.scene_frames;
        result.total_objects = summary.total_objects;
        result.dropped_objects = summary.dropped_objects;
        result.snapshots = summary.snapshots_rendered;
        result.checkpoints = summary.checkpoints_written;
        result.input_events_queued = input_events_queued;
        result.selected_scene_ids = selected_scene_ids;
    }
    catch (const std::exception& e)
    {
        if (project_started && project)
        {
            try
            {
                project->stopHeadless();
            }
            catch (...)
            {
            }
        }

        result.exit_code = 1;
        result.error = e.what();

        if (!options.trace_dir.empty())
        {
            try
            {
                if (!std::filesystem::exists(options.trace_dir))
                    std::filesystem::create_directories(options.trace_dir);
                writeJsonFile(options.trace_dir / "summary.json", {
                    {"ok", false},
                    {"error", result.error}
                });
            }
            catch (...)
            {
            }
        }
    }

    return result;
}

int runHeadlessFromCommandLine(int argc, char* argv[])
{
    HeadlessRunOptions options;
    std::string error;
    if (!parseHeadlessArgs(argc, argv, options, error))
    {
        if (error == "help")
        {
            printHeadlessUsage(std::cout);
            return 0;
        }

        std::cerr << "bitloop headless: " << error << "\n";
        printHeadlessUsage(std::cerr);
        return 2;
    }

    if (options.list_scenes)
    {
        if (printSceneCatalogue(options, std::cout, error))
            return 0;
        std::cerr << "bitloop headless: " << error << "\n";
        return 2;
    }

    const HeadlessRunResult result = runHeadlessTrace(options);
    if (result.exit_code != 0)
        std::cerr << "bitloop headless: " << result.error << "\n";
    return result.exit_code;
}

BL_END_NS

#endif
