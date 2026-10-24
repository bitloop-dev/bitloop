#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <bitloop/platform/platform_macros.h>

BL_BEGIN_NS

// Preserve the numeric identifiers used by existing event handlers and input recordings.
enum EventType : std::uint32_t
{
    KEY_DOWN = 0x300,
    KEY_UP = 0x301,
    TEXT_EDITING = 0x302,
    TEXT_INPUT = 0x303,
    TEXT_EDITING_CANDIDATES = 0x307,
    MOUSE_MOTION = 0x400,
    MOUSE_BUTTON_DOWN = 0x401,
    MOUSE_BUTTON_UP = 0x402,
    MOUSE_WHEEL = 0x403,
    FINGER_DOWN = 0x700,
    FINGER_UP = 0x701,
    FINGER_MOTION = 0x702,
    FINGER_CANCELED = 0x703,
    DROP_FILE = 0x1000,
    DROP_TEXT = 0x1001,
    DROP_BEGIN = 0x1002,
    DROP_COMPLETE = 0x1003,
    DROP_POSITION = 0x1004
};

using KeyCode = std::uint32_t;
using KeyMod = std::uint16_t;
using ScanCode = std::int32_t;

struct EventData
{
    std::uint32_t type = 0;
    std::uint64_t timestamp = 0;
    std::uint32_t windowID = 0;

    struct
    {
        std::uint32_t which = 0, state = 0;
        float x = 0, y = 0, xrel = 0, yrel = 0;
    } motion;

    struct
    {
        std::uint32_t which = 0;
        std::uint8_t button = 0, clicks = 0;
        bool down = false;
        float x = 0, y = 0;
    } button;

    struct
    {
        std::uint32_t which = 0, direction = 0;
        float x = 0, y = 0, mouse_x = 0, mouse_y = 0;
        std::int32_t integer_x = 0, integer_y = 0;
    } wheel;

    struct
    {
        std::uint64_t touchID = 0, fingerID = 0;
        float x = 0, y = 0, dx = 0, dy = 0, pressure = 0;
    } tfinger;

    struct
    {
        std::uint32_t which = 0;
        KeyCode key = 0;
        ScanCode scancode = 0;
        KeyMod mod = 0;
        std::uint16_t raw = 0;
        bool down = false, repeat = false;
        std::string name;
    } key;

    struct
    {
        std::string text;
    } text;

    struct
    {
        std::string text;
        std::int32_t start = 0, length = 0;
    } edit;

    struct
    {
        std::vector<std::string> candidates;
        std::int32_t selected_candidate = -1;
        bool horizontal = false;
    } edit_candidates;

    struct
    {
        float x = 0, y = 0;
        std::string source, data;
    } drop;

    struct
    {
        std::int32_t data1 = 0, data2 = 0;
    } window;
};

BL_END_NS
