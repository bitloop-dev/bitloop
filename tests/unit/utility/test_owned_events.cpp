#include <catch2/catch_test_macros.hpp>

#ifdef BITLOOP_DEV_MODE
#include <bitloop.h>
#else
#include <bitloop/core/event.h>
#endif
#include <bitloop/platform/sdl_event.h>
#include <SDL3/SDL.h>

using namespace bl;

static_assert(std::uint32_t(EventType::KEY_DOWN) == SDL_EVENT_KEY_DOWN);
static_assert(std::uint32_t(EventType::KEY_UP) == SDL_EVENT_KEY_UP);
static_assert(std::uint32_t(EventType::TEXT_EDITING) == SDL_EVENT_TEXT_EDITING);
static_assert(std::uint32_t(EventType::TEXT_INPUT) == SDL_EVENT_TEXT_INPUT);
static_assert(std::uint32_t(EventType::TEXT_EDITING_CANDIDATES) == SDL_EVENT_TEXT_EDITING_CANDIDATES);
static_assert(std::uint32_t(EventType::MOUSE_MOTION) == SDL_EVENT_MOUSE_MOTION);
static_assert(std::uint32_t(EventType::MOUSE_BUTTON_DOWN) == SDL_EVENT_MOUSE_BUTTON_DOWN);
static_assert(std::uint32_t(EventType::MOUSE_BUTTON_UP) == SDL_EVENT_MOUSE_BUTTON_UP);
static_assert(std::uint32_t(EventType::MOUSE_WHEEL) == SDL_EVENT_MOUSE_WHEEL);
static_assert(std::uint32_t(EventType::FINGER_DOWN) == SDL_EVENT_FINGER_DOWN);
static_assert(std::uint32_t(EventType::FINGER_UP) == SDL_EVENT_FINGER_UP);
static_assert(std::uint32_t(EventType::FINGER_MOTION) == SDL_EVENT_FINGER_MOTION);
static_assert(std::uint32_t(EventType::FINGER_CANCELED) == SDL_EVENT_FINGER_CANCELED);
static_assert(std::uint32_t(EventType::DROP_FILE) == SDL_EVENT_DROP_FILE);
static_assert(std::uint32_t(EventType::DROP_TEXT) == SDL_EVENT_DROP_TEXT);
static_assert(std::uint32_t(EventType::DROP_BEGIN) == SDL_EVENT_DROP_BEGIN);
static_assert(std::uint32_t(EventType::DROP_COMPLETE) == SDL_EVENT_DROP_COMPLETE);
static_assert(std::uint32_t(EventType::DROP_POSITION) == SDL_EVENT_DROP_POSITION);

TEST_CASE("owned events preserve wheel and keyboard metadata", "[owned-input]")
{
    SDL_Event source{};
    source.type = SDL_EVENT_MOUSE_WHEEL;
    source.wheel.timestamp = 123456789;
    source.wheel.windowID = 7;
    source.wheel.which = 12;
    source.wheel.x = -0.25f;
    source.wheel.y = 0.75f;
    source.wheel.mouse_x = 42.5f;
    source.wheel.mouse_y = 70.25f;
    source.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    source.wheel.integer_x = -1;
    source.wheel.integer_y = 2;

    Event wheel(copySDLEvent(source));
    source = {};
    REQUIRE(wheel.type() == EventType::MOUSE_WHEEL);
    REQUIRE(wheel.data().timestamp == 123456789);
    REQUIRE(wheel.data().windowID == 7);
    REQUIRE(wheel.data().wheel.which == 12);
    REQUIRE(wheel.data().wheel.x == -0.25f);
    REQUIRE(PointerEvent(wheel).wheelY() == 0.75);
    REQUIRE(wheel.data().wheel.mouse_x == 42.5f);
    REQUIRE(wheel.data().wheel.mouse_y == 70.25f);
    REQUIRE(wheel.data().wheel.direction == SDL_MOUSEWHEEL_FLIPPED);
    REQUIRE(wheel.data().wheel.integer_x == -1);
    REQUIRE(wheel.data().wheel.integer_y == 2);

    source.type = SDL_EVENT_KEY_DOWN;
    source.key.key = SDLK_A;
    source.key.scancode = SDL_SCANCODE_Q;
    source.key.mod = SDL_KMOD_LCTRL | SDL_KMOD_RSHIFT;
    source.key.which = 19;
    source.key.raw = 30;
    source.key.down = true;
    source.key.repeat = true;
    KeyEvent key(Event(copySDLEvent(source)));
    source = {};
    (void)SDL_GetKeyName(SDLK_F11);

    REQUIRE(key.keyCode() == SDLK_A);
    REQUIRE(key.scanCode() == SDL_SCANCODE_Q);
    REQUIRE(key.keyMod() == (SDL_KMOD_LCTRL | SDL_KMOD_RSHIFT));
    REQUIRE(key.data().key.which == 19);
    REQUIRE(key.data().key.raw == 30);
    REQUIRE(key.data().key.down);
    REQUIRE(key.data().key.repeat);
    REQUIRE(std::string(key.keyName()) == "A");
}

TEST_CASE("owned events retain IME strings after SDL buffers are replaced", "[owned-input]")
{
    EventData editing;
    EventData candidates;
    {
        std::string composition(256, 'x');
        std::string candidate(256, 'y');
        const char* values[] = { candidate.c_str(), "second" };
        SDL_Event source{};
        source.type = SDL_EVENT_TEXT_EDITING;
        source.edit.text = composition.c_str();
        source.edit.start = 3;
        source.edit.length = 7;
        editing = copySDLEvent(source);

        source = {};
        source.type = SDL_EVENT_TEXT_EDITING_CANDIDATES;
        source.edit_candidates.candidates = values;
        source.edit_candidates.num_candidates = 2;
        source.edit_candidates.selected_candidate = 1;
        source.edit_candidates.horizontal = true;
        candidates = copySDLEvent(source);
        composition.assign(256, '!');
        candidate.assign(256, '!');
    }

    Event copy(editing);
    editing = {};
    REQUIRE(copy.data().edit.text == std::string(256, 'x'));
    REQUIRE(copy.data().edit.start == 3);
    REQUIRE(copy.data().edit.length == 7);
    REQUIRE(candidates.edit_candidates.candidates == std::vector<std::string>{ std::string(256, 'y'), "second" });
    REQUIRE(candidates.edit_candidates.selected_candidate == 1);
    REQUIRE(candidates.edit_candidates.horizontal);

    SDL_Event empty{};
    empty.type = SDL_EVENT_DROP_BEGIN;
    REQUIRE(copySDLEvent(empty).drop.data.empty());
    REQUIRE(copySDLEvent(empty).drop.source.empty());
}

#ifdef BITLOOP_DEV_MODE
namespace
{
    struct OwnedInputScene : BasicScene
    {
        std::vector<Event> events;
        std::vector<KeyEvent> keys;

        void onEvent(Event e) override { events.push_back(e); }
        void onKeyDown(KeyEvent e) override { keys.push_back(e); }
        void onKeyUp(KeyEvent e) override { keys.push_back(e); }
        void viewportDraw(Viewport*) const override {}
    };

    struct OwnedInputProject : BasicProject
    {
        SceneHandle<OwnedInputScene> scene;
        Viewport* viewport = nullptr;

        void projectPrepare(Layout& layout) override
        {
            scene = create<OwnedInputScene>();
            viewport = &layout.add(scene, "main");
        }
    };
}

TEST_CASE("headless queues and retained callbacks own text and drop payloads", "[owned-input]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    OwnedInputProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    InputEmulation input(IVec2(300, 200));
    {
        std::string text(256, 'a');
        std::string path = "C:/scripts/" + std::string(256, 'b') + ".cpp";
        std::string source_name(256, 'c');
        SDL_Event source{};
        source.type = SDL_EVENT_TEXT_INPUT;
        source.text.text = text.c_str();
        input.event(0, source);

        source = {};
        source.type = SDL_EVENT_DROP_FILE;
        source.drop.data = path.c_str();
        source.drop.source = source_name.c_str();
        source.drop.x = 25.5f;
        source.drop.y = 40.25f;
        input.event(0, source);
        text.assign(256, '!');
        path.assign(256, '!');
        source_name.assign(256, '!');
    }

    REQUIRE(input.queueFrame(project, 0) == 2);
    input.clear();
    project.processHeadlessFrame(1.0 / 60.0);

    const auto& events = project.scene->events;
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type() == EventType::TEXT_INPUT);
    REQUIRE(events[0].data().text.text == std::string(256, 'a'));
    REQUIRE(events[1].type() == EventType::DROP_FILE);
    REQUIRE(events[1].data().drop.data == "C:/scripts/" + std::string(256, 'b') + ".cpp");
    REQUIRE(events[1].data().drop.source == std::string(256, 'c'));
    REQUIRE(events[1].data().drop.x == 25.5f);
    REQUIRE(events[1].data().drop.y == 40.25f);

    Event retained = events[1];
    project.stopHeadless();
    REQUIRE(retained.data().drop.data == "C:/scripts/" + std::string(256, 'b') + ".cpp");
}

TEST_CASE("copied keyboard callbacks retain focus and viewport ownership", "[owned-input]")
{
    ImDebugLog log;
    CaptureContext capture_context;
    OwnedInputProject project;
    project.configureHeadless(-1, &log, IVec2(300, 200), &capture_context);
    project.startHeadless();

    InputEmulation input(IVec2(300, 200));
    input.click(0, DVec2(100.0, 80.0));
    input.keyDown(0, SDLK_A, SDL_SCANCODE_A, SDL_KMOD_LSHIFT, true);
    input.keyUp(1, SDLK_A, SDL_SCANCODE_A);
    input.queueFrame(project, 0);
    project.processHeadlessFrame(1.0 / 60.0);
    REQUIRE(project.scene->keys.size() == 1);
    input.queueFrame(project, 1);
    input.clear();
    project.processHeadlessFrame(1.0 / 60.0);

    REQUIRE(project.scene->keys.size() == 2);
    for (KeyEvent key : project.scene->keys)
    {
        REQUIRE(key.ownedBy(project.scene.get()));
        REQUIRE(key.ctx_owner() == project.viewport);
        REQUIRE(key.ctx_focused() == project.viewport);
        REQUIRE(key.ctx_hovered() == project.viewport);
        REQUIRE(key.pointerSurfaceSize() == IVec2(300, 200));
        REQUIRE(key.keyCode() == SDLK_A);
        REQUIRE(std::string(key.keyName()) == "A");
    }
    REQUIRE(project.scene->keys[0].data().key.repeat);
    REQUIRE(project.scene->keys[0].keyMod() == SDL_KMOD_LSHIFT);
    REQUIRE(project.scene->keys[1].type() == EventType::KEY_UP);

    project.stopHeadless();
}
#endif
