#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <bitloop.h>
#include <bitloop/core/image_loader.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    struct TraceSmokeScene : public BasicScene
    {
        static inline int process_calls = 0;
        static inline int trace_calls = 0;

        double x = 0.0;
        int steps = 0;

        static void resetCounters()
        {
            process_calls = 0;
            trace_calls = 0;
        }

        std::string name() const override { return "TraceSmokeScene"; }

        void sceneProcess(double) override
        {
            process_calls++;
            steps++;
        }

        void viewportProcess(Viewport*, double dt) override
        {
            x += 10.0 * dt;
        }

        void viewportDraw(Viewport*) const override
        {
        }

        void traceFrame(SimTraceFrame& frame) const override
        {
            trace_calls++;
            frame.metric("steps", static_cast<double>(steps));
            if (SimTraceObject* object = frame.addObject("dot", "particle", x, 2.0))
            {
                object->vx = 10.0;
                object->vy = 0.0;
                object->radius = 1.0;
            }
        }

        bool saveCheckpoint(SimCheckpointWriter& writer) const override
        {
            writer.writePod("x", x);
            writer.writePod("steps", steps);
            return true;
        }

        bool loadCheckpoint(const SimCheckpointReader& reader) override
        {
            reader.readPod("x", x);
            reader.readPod("steps", steps);
            return true;
        }
    };

    struct TraceSmokeProject : public BasicProject
    {
        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "TraceSmoke" });
        }

        void projectPrepare(Layout& layout) override
        {
            layout << create<TraceSmokeScene>("trace-smoke");
        }
    };

    struct ThrowingTraceScene : public BasicScene
    {
        std::string name() const override { return "ThrowingTraceScene"; }

        void sceneProcess(double) override
        {
            throw std::runtime_error("trace smoke failure");
        }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct ThrowingTraceProject : public BasicProject
    {
        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "TraceThrows" });
        }

        void projectPrepare(Layout& layout) override
        {
            layout << create<ThrowingTraceScene>();
        }
    };

    struct SnapshotRequestScene : public BasicScene
    {
        int steps = 0;

        std::string name() const override { return "SnapshotRequestScene"; }

        void sceneProcess(double) override
        {
            steps++;
            if (steps == 1)
                traceCanvas("first_frame", IRect(0, 0, 10, 10), "unit test request");
        }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct SnapshotRequestProject : public BasicProject
    {
        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "TraceSnapshotRequest" });
        }

        void projectPrepare(Layout& layout) override
        {
            layout << create<SnapshotRequestScene>();
        }
    };

    struct IsolatedSnapshotScene : public BasicScene
    {
        int steps = 0;

        void sceneProcess(double) override
        {
            steps++;
            if (steps != 1)
                return;

            traceCanvas("ambiguous", "multiple Viewports require a target");
            traceCanvas(*mountedToViewports().front(), "isolated", "explicit hidden Viewport");
            traceCanvasWorld(*mountedToViewports().front(), "world-crop", DRect(10.0, 8.0, 30.0, 28.0), "world crop");
        }

        void viewportDraw(Viewport* viewport) const override
        {
            viewport->stageMode();
            viewport->setFillStyle(220, 30, 40);
            viewport->fillRect(0.0, 0.0, viewport->width(), viewport->height());
        }
    };

    struct NoViewportSnapshotScene : public BasicScene
    {
        int steps = 0;

        void sceneProcess(double) override
        {
            steps++;
            if (steps == 1)
                traceCanvas("missing", "no Viewport should fail clearly");
        }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct SnapshotNeighbourScene : public BasicScene
    {
        void viewportDraw(Viewport* viewport) const override
        {
            viewport->stageMode();
            viewport->setFillStyle(20, 60, 220);
            viewport->fillRect(0.0, 0.0, viewport->width(), viewport->height());
        }
    };

    struct IsolatedSnapshotProject : public BasicProject
    {
        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "IsolatedSnapshot" });
        }

        void projectPrepare(Layout& layout) override
        {
            const SceneHandle<IsolatedSnapshotScene> isolated = create<IsolatedSnapshotScene>("isolated");
            Viewport& main = layout.add(isolated, "isolated-main");
            main.setVisible(false);
            main.setRenderSize(IVec2(96, 64));
            Viewport& alternate = layout.add(isolated, "isolated-alternate");
            alternate.setVisible(false);
            alternate.setRenderSize(IVec2(48, 32));

            [[maybe_unused]] const SceneHandle<NoViewportSnapshotScene> no_viewport = create<NoViewportSnapshotScene>("no-viewport");
            layout.add(create<SnapshotNeighbourScene>("neighbour"), "neighbour-main");
        }
    };

    struct CustomSnapshotScene : public BasicScene
    {
        static inline int process_calls = 0;
        static inline int capture_calls = 0;
        static inline bool request_snapshot = false;
        int steps = 0;

        void sceneProcess(double) override
        {
            process_calls++;
            steps++;
            if (request_snapshot && steps == 1)
                traceSnapshot("grid", "event capture");
        }

        bool captureTraceSnapshot(std::string_view route, SimSnapshotImage& image) const override
        {
            capture_calls++;
            if (route != "grid" && route != "alternate" && route != "short" && route != "empty")
                return false;

            image.resolution = IVec2(3, 2);
            image.pixels = {
                static_cast<uint8_t>(steps), 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
                255, 255, 0, 255, 255, 0, 255, 255, 0, 255, 255, 93
            };
            if (route == "alternate")
            {
                image.resolution = IVec2(2, 1);
                image.pixels.resize(8);
            }
            if (route == "short")
                image.pixels.pop_back();
            if (route == "empty")
                image.resolution = IVec2(0, 0);
            return true;
        }

        void viewportDraw(Viewport*) const override {}
    };

    struct CustomSnapshotProject : public BasicProject
    {
        static ProjectInfo info() { return ProjectInfo({ "Tests", "CustomSnapshot" }); }

        void projectPrepare(Layout&) override
        {
            // Custom pixel routes also work for Scenes without a Viewport.
            [[maybe_unused]] auto scene = create<CustomSnapshotScene>("fluid");
        }
    };

    struct InputSmokeScene : public BasicScene
    {
        int generic_events = 0;
        int process_calls = 0;
        int events_seen_at_process = 0;
        std::vector<Uint32> event_types;
        std::vector<std::pair<Uint32, DVec2>> pointer_downs;
        std::vector<std::pair<Uint32, DVec2>> pointer_moves;
        std::vector<std::pair<Uint32, DVec2>> pointer_ups;
        std::vector<KeyCode> key_downs;
        std::vector<KeyCode> key_ups;

        std::string name() const override { return "InputSmokeScene"; }

        void sceneProcess(double) override
        {
            process_calls++;
            events_seen_at_process = generic_events;
        }

        void viewportDraw(Viewport*) const override
        {
        }

        void onEvent(Event e) override
        {
            generic_events++;
            event_types.push_back(e.type());
        }

        void onPointerDown(PointerEvent e) override
        {
            pointer_downs.push_back({ e.type(), DVec2(e.x(), e.y()) });
        }

        void onPointerMove(PointerEvent e) override
        {
            pointer_moves.push_back({ e.type(), DVec2(e.x(), e.y()) });
        }

        void onPointerUp(PointerEvent e) override
        {
            pointer_ups.push_back({ e.type(), DVec2(e.x(), e.y()) });
        }

        void onKeyDown(KeyEvent e) override
        {
            key_downs.push_back(e.keyCode());
        }

        void onKeyUp(KeyEvent e) override
        {
            key_ups.push_back(e.keyCode());
        }

        void traceFrame(SimTraceFrame& frame) const override
        {
            frame.metric("generic_events", static_cast<double>(generic_events));
            frame.metric("events_seen_at_process", static_cast<double>(events_seen_at_process));
            frame.metric("pointer_downs", static_cast<double>(pointer_downs.size()));
            frame.metric("pointer_moves", static_cast<double>(pointer_moves.size()));
            frame.metric("pointer_ups", static_cast<double>(pointer_ups.size()));
            frame.metric("key_downs", static_cast<double>(key_downs.size()));
            frame.metric("key_ups", static_cast<double>(key_ups.size()));
        }
    };

    struct InputSmokeProject : public BasicProject
    {
        SceneHandle<InputSmokeScene> scene;

        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "InputSmoke" });
        }

        void projectPrepare(Layout& layout) override
        {
            scene = create<InputSmokeScene>();
            layout << scene;
        }
    };

    void requireNear(DVec2 actual, double expected_x, double expected_y)
    {
        REQUIRE(std::abs(actual.x - expected_x) < 0.00001);
        REQUIRE(std::abs(actual.y - expected_y) < 0.00001);
    }

    void registerTraceSmokeProject()
    {
        static bool registered = false;
        if (registered)
            return;

        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<TraceSmokeProject>("TraceSmoke"));
        registered = true;
    }

    void registerThrowingTraceProject()
    {
        static bool registered = false;
        if (registered)
            return;

        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<ThrowingTraceProject>("TraceThrows"));
        registered = true;
    }

    void registerSnapshotRequestProject()
    {
        static bool registered = false;
        if (registered)
            return;

        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<SnapshotRequestProject>("TraceSnapshotRequest"));
        registered = true;
    }

    void registerIsolatedSnapshotProject()
    {
        static bool registered = false;
        if (registered)
            return;

        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<IsolatedSnapshotProject>("IsolatedSnapshot"));
        registered = true;
    }

    void registerCustomSnapshotProject()
    {
        static bool registered = false;
        if (!registered)
        {
            ProjectBase::addProjectFactoryInfo(ProjectBase::createProjectFactoryInfo<CustomSnapshotProject>("CustomSnapshot"));
            registered = true;
        }
        CustomSnapshotScene::process_calls = 0;
        CustomSnapshotScene::capture_calls = 0;
        CustomSnapshotScene::request_snapshot = false;
    }

    void registerInputSmokeProject()
    {
        static bool registered = false;
        if (registered)
            return;

        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<InputSmokeProject>("InputSmoke"));
        registered = true;
    }

    std::filesystem::path makeTraceDir(const char* prefix)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(stamp));
    }
}

TEST_CASE("InputEmulation builds deterministic frame schedules")
{
    InputEmulation input(IVec2(320, 200));
    input.mouseDrag(2, 4, DVec2(10.0, 20.0), DVec2(70.0, 80.0));
    input.fingerDown(3, 42, DVec2(160.0, 50.0));
    input.keyTap(5, SDLK_A, SDL_SCANCODE_A, 2);

    REQUIRE(input.size() == 7);
    REQUIRE(input.firstFrame() == 2);
    REQUIRE(input.lastFrame() == 7);

    const std::vector<EventData> frame2 = input.eventsForFrame(2);
    REQUIRE(frame2.size() == 1);
    REQUIRE(frame2[0].type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    REQUIRE(frame2[0].button.down == true);
    REQUIRE(frame2[0].button.x == 10.0f);
    REQUIRE(frame2[0].button.y == 20.0f);

    const std::vector<EventData> frame3 = input.eventsForFrame(3);
    REQUIRE(frame3.size() == 2);
    REQUIRE(frame3[0].type == SDL_EVENT_MOUSE_MOTION);
    REQUIRE(frame3[0].motion.state == SDL_BUTTON_LMASK);
    REQUIRE(frame3[0].motion.x == 40.0f);
    REQUIRE(frame3[0].motion.y == 50.0f);
    REQUIRE(frame3[1].type == SDL_EVENT_FINGER_DOWN);
    REQUIRE(frame3[1].tfinger.fingerID == 42);
    REQUIRE(frame3[1].tfinger.x == 0.5f);
    REQUIRE(frame3[1].tfinger.y == 0.25f);

    const std::vector<EventData> frame4 = input.eventsForFrame(4);
    REQUIRE(frame4.size() == 2);
    REQUIRE(frame4[0].type == SDL_EVENT_MOUSE_MOTION);
    REQUIRE(frame4[0].motion.x == 70.0f);
    REQUIRE(frame4[0].motion.y == 80.0f);
    REQUIRE(frame4[1].type == SDL_EVENT_MOUSE_BUTTON_UP);

    const std::vector<EventData> frame7 = input.eventsForFrame(7);
    REQUIRE(frame7.size() == 1);
    REQUIRE(frame7[0].type == SDL_EVENT_KEY_UP);
    REQUIRE(frame7[0].key.key == SDLK_A);
    REQUIRE(frame7[0].key.scancode == SDL_SCANCODE_A);

    input.clearFrame(4);
    REQUIRE(input.size() == 5);
    REQUIRE(input.eventsForFrame(4).empty());
}

TEST_CASE("InputEmulation expands eased drags deterministically")
{
    InputEmulation input(IVec2(320, 200));
    input.mouseDrag(
        0,
        4,
        DVec2(0.0, 0.0),
        DVec2(100.0, 0.0),
        SDL_BUTTON_LEFT,
        InputEasing::Smoothstep);
    input.fingerDrag(
        10,
        12,
        5,
        DVec2(0.0, 0.0),
        DVec2(0.0, 100.0),
        1,
        InputEasing::EaseOutQuad,
        0.75f,
        0.25f);

    REQUIRE(input.size() == 10);

    const std::vector<EventData> frame1 = input.eventsForFrame(1);
    REQUIRE(frame1.size() == 1);
    REQUIRE(frame1[0].type == SDL_EVENT_MOUSE_MOTION);
    requireNear(DVec2(frame1[0].motion.x, frame1[0].motion.y), 15.625, 0.0);
    requireNear(DVec2(frame1[0].motion.xrel, frame1[0].motion.yrel), 15.625, 0.0);

    const std::vector<EventData> frame2 = input.eventsForFrame(2);
    REQUIRE(frame2.size() == 1);
    REQUIRE(frame2[0].type == SDL_EVENT_MOUSE_MOTION);
    requireNear(DVec2(frame2[0].motion.x, frame2[0].motion.y), 50.0, 0.0);
    requireNear(DVec2(frame2[0].motion.xrel, frame2[0].motion.yrel), 34.375, 0.0);

    const std::vector<EventData> frame4 = input.eventsForFrame(4);
    REQUIRE(frame4.size() == 2);
    REQUIRE(frame4[0].type == SDL_EVENT_MOUSE_MOTION);
    REQUIRE(frame4[1].type == SDL_EVENT_MOUSE_BUTTON_UP);
    requireNear(DVec2(frame4[0].motion.x, frame4[0].motion.y), 100.0, 0.0);

    const std::vector<EventData> frame11 = input.eventsForFrame(11);
    REQUIRE(frame11.size() == 1);
    REQUIRE(frame11[0].type == SDL_EVENT_FINGER_MOTION);
    REQUIRE(std::abs(frame11[0].tfinger.y - static_cast<float>(75.0 / 200.0)) < 0.00001f);
    REQUIRE(frame11[0].tfinger.pressure == 0.75f);

    const std::vector<EventData> frame12 = input.eventsForFrame(12);
    REQUIRE(frame12.size() == 2);
    REQUIRE(frame12[0].type == SDL_EVENT_FINGER_MOTION);
    REQUIRE(frame12[1].type == SDL_EVENT_FINGER_UP);
    REQUIRE(frame12[1].tfinger.pressure == 0.25f);
}

TEST_CASE("InputCoordinateSpace maps world targets to deterministic client events")
{
    WorldStageTransform transform;
    transform.translate(20.0, 30.0);
    transform.scale(2.0, 3.0);
    transform.rotate(0.25);

    const IVec2 input_surface_size(800, 600);
    const DRect viewport_client_rect(100.0, 50.0, 500.0, 350.0);
    const DVec2 viewport_stage_size(200.0, 150.0);
    const InputCoordinateSpace space(
        input_surface_size,
        viewport_client_rect,
        viewport_stage_size,
        transform);

    const DVec2 world_pos(12.0, -4.0);
    const DVec2 stage_pos = transform.toStage<f64>(world_pos);
    const DVec2 expected_client(
        viewport_client_rect.x1 + (stage_pos.x / viewport_stage_size.x) * viewport_client_rect.width(),
        viewport_client_rect.y1 + (stage_pos.y / viewport_stage_size.y) * viewport_client_rect.height());

    requireNear(space.worldToClient(world_pos), expected_client.x, expected_client.y);
    requireNear(space.clientToWorld(expected_client), world_pos.x, world_pos.y);

    const DRect world_rect(10.0, 20.0, 30.0, 45.0);
    const DQuad client_quad = space.worldRectToClientQuad(world_rect);
    requireNear(client_quad.a, space.worldToClient(DVec2(10.0, 20.0)).x, space.worldToClient(DVec2(10.0, 20.0)).y);
    requireNear(client_quad.b, space.worldToClient(DVec2(30.0, 20.0)).x, space.worldToClient(DVec2(30.0, 20.0)).y);
    requireNear(client_quad.c, space.worldToClient(DVec2(30.0, 45.0)).x, space.worldToClient(DVec2(30.0, 45.0)).y);
    requireNear(client_quad.d, space.worldToClient(DVec2(10.0, 45.0)).x, space.worldToClient(DVec2(10.0, 45.0)).y);
    requireNear(space.worldRectCenterToClient(world_rect), space.worldToClient(world_rect.center()).x, space.worldToClient(world_rect.center()).y);

    InputEmulation input(space);
    const DVec2 from_world(1.0, 2.0);
    const DVec2 to_world(9.0, 10.0);
    const DVec2 from_client = space.worldToClient(from_world);
    const DVec2 to_client = space.worldToClient(to_world);
    const DVec2 mid_client = (from_client + to_client) * 0.5;

    input.mouseDragWorld(1, 3, space, from_world, to_world);
    input.fingerDownWorld(4, 99, space, world_pos);

    const std::vector<EventData> frame1 = input.eventsForFrame(1);
    REQUIRE(frame1.size() == 1);
    REQUIRE(frame1[0].type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    requireNear(DVec2(frame1[0].button.x, frame1[0].button.y), from_client.x, from_client.y);

    const std::vector<EventData> frame2 = input.eventsForFrame(2);
    REQUIRE(frame2.size() == 1);
    REQUIRE(frame2[0].type == SDL_EVENT_MOUSE_MOTION);
    requireNear(DVec2(frame2[0].motion.x, frame2[0].motion.y), mid_client.x, mid_client.y);

    const std::vector<EventData> frame3 = input.eventsForFrame(3);
    REQUIRE(frame3.size() == 2);
    REQUIRE(frame3[0].type == SDL_EVENT_MOUSE_MOTION);
    REQUIRE(frame3[1].type == SDL_EVENT_MOUSE_BUTTON_UP);
    requireNear(DVec2(frame3[0].motion.x, frame3[0].motion.y), to_client.x, to_client.y);
    requireNear(DVec2(frame3[1].button.x, frame3[1].button.y), to_client.x, to_client.y);

    const std::vector<EventData> frame4 = input.eventsForFrame(4);
    REQUIRE(frame4.size() == 1);
    REQUIRE(frame4[0].type == SDL_EVENT_FINGER_DOWN);
    REQUIRE(std::abs(frame4[0].tfinger.x - static_cast<float>(expected_client.x / input_surface_size.x)) < 0.00001f);
    REQUIRE(std::abs(frame4[0].tfinger.y - static_cast<float>(expected_client.y / input_surface_size.y)) < 0.00001f);

    InputEmulation wrong_surface(IVec2(400, 300));
    REQUIRE_THROWS_AS(wrong_surface.clickWorld(0, space, world_pos), std::invalid_argument);
}

TEST_CASE("headless projects dispatch queued input events before simulation processing")
{
    ImDebugLog log;
    CaptureContext capture_context;
    InputSmokeProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.startHeadless();

    REQUIRE(project.scene.ready());

    InputEmulation input(IVec2(320, 200));
    input.mouseDown(0, DVec2(100.0, 80.0));
    input.mouseMove(0, DVec2(120.0, 90.0));
    input.mouseUp(0, DVec2(120.0, 90.0));
    input.fingerDown(0, 7, DVec2(160.0, 50.0));
    input.fingerMove(0, 7, DVec2(176.0, 60.0));
    input.fingerUp(0, 7, DVec2(176.0, 60.0));
    input.keyDown(0, SDLK_SPACE, SDL_SCANCODE_SPACE);
    input.keyUp(0, SDLK_SPACE, SDL_SCANCODE_SPACE);

    REQUIRE(input.queueFrame(project, 0) == 8);

    project.processHeadlessFrame(1.0 / 60.0);

    const InputSmokeScene& scene = *project.scene;
    REQUIRE(scene.process_calls == 1);
    REQUIRE(scene.generic_events == 8);
    REQUIRE(scene.events_seen_at_process == scene.generic_events);

    REQUIRE(scene.event_types == std::vector<Uint32>{
        SDL_EVENT_MOUSE_BUTTON_DOWN,
        SDL_EVENT_MOUSE_MOTION,
        SDL_EVENT_MOUSE_BUTTON_UP,
        SDL_EVENT_FINGER_DOWN,
        SDL_EVENT_FINGER_MOTION,
        SDL_EVENT_FINGER_UP,
        SDL_EVENT_KEY_DOWN,
        SDL_EVENT_KEY_UP
    });

    REQUIRE(scene.pointer_downs.size() == 2);
    REQUIRE(scene.pointer_moves.size() == 2);
    REQUIRE(scene.pointer_ups.size() == 2);
    REQUIRE(scene.key_downs == std::vector<KeyCode>{ SDLK_SPACE });
    REQUIRE(scene.key_ups == std::vector<KeyCode>{ SDLK_SPACE });

    REQUIRE(scene.pointer_downs[0].first == SDL_EVENT_MOUSE_BUTTON_DOWN);
    requireNear(scene.pointer_downs[0].second, 100.0, 80.0);
    REQUIRE(scene.pointer_moves[0].first == SDL_EVENT_MOUSE_MOTION);
    requireNear(scene.pointer_moves[0].second, 120.0, 90.0);
    REQUIRE(scene.pointer_ups[0].first == SDL_EVENT_MOUSE_BUTTON_UP);
    requireNear(scene.pointer_ups[0].second, 120.0, 90.0);

    REQUIRE(scene.pointer_downs[1].first == SDL_EVENT_FINGER_DOWN);
    requireNear(scene.pointer_downs[1].second, 160.0, 50.0);
    REQUIRE(scene.pointer_moves[1].first == SDL_EVENT_FINGER_MOTION);
    requireNear(scene.pointer_moves[1].second, 176.0, 60.0);
    REQUIRE(scene.pointer_ups[1].first == SDL_EVENT_FINGER_UP);
    requireNear(scene.pointer_ups[1].second, 176.0, 60.0);

    project.stopHeadless();
}

TEST_CASE("headless runner replays low-level input JSONL")
{
    registerInputSmokeProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_input_replay_");
    const std::filesystem::path input_path = makeTraceDir("bitloop_input_replay_");
    std::filesystem::remove_all(trace_dir);
    std::filesystem::remove(input_path);

    {
        std::ofstream input(input_path, std::ios::binary);
        REQUIRE(input.good());
        input << R"({"frame":0,"type":"mouse_down","x":100,"y":80})" << '\n';
        input << R"({"frame":0,"type":"mouse_move","x":120,"y":90})" << '\n';
        input << R"({"frame":0,"type":"mouse_up","x":120,"y":90})" << '\n';
        input << R"({"frame":0,"type":"finger_down","finger_id":7,"x":160,"y":50})" << '\n';
        input << R"({"frame":0,"type":"finger_move","finger_id":7,"x":176,"y":60})" << '\n';
        input << R"({"frame":0,"type":"finger_up","finger_id":7,"x":176,"y":60})" << '\n';
        input << R"({"frame":0,"type":"key_down","key":"space","scancode":"space"})" << '\n';
        input << R"({"frame":0,"type":"key_up","key":"space","scancode":"space"})" << '\n';
    }

    HeadlessRunOptions options;
    options.project = "InputSmoke";
    options.trace_dir = trace_dir;
    options.input_path = input_path;
    options.frames = 1;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.input_events_queued == 8);

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    REQUIRE(metadata_in.good());
    const JSON::json metadata = JSON::json::parse(metadata_in);
    metadata_in.close();
    REQUIRE(metadata["input"]["enabled"] == true);
    REQUIRE(metadata["input"]["format"] == "jsonl_input_replay_v1");
    REQUIRE(metadata["input"]["events_loaded"] == 8);
    REQUIRE(metadata["input"]["first_frame"] == 0);
    REQUIRE(metadata["input"]["last_frame"] == 0);

    std::ifstream summary_in(trace_dir / "summary.json", std::ios::binary);
    REQUIRE(summary_in.good());
    const JSON::json summary = JSON::json::parse(summary_in);
    summary_in.close();
    REQUIRE(summary["ok"] == true);
    REQUIRE(summary["input"]["events_loaded"] == 8);
    REQUIRE(summary["input"]["events_queued"] == 8);

    std::ifstream frames_in(trace_dir / "frames.jsonl", std::ios::binary);
    REQUIRE(frames_in.good());
    std::string line;
    REQUIRE(std::getline(frames_in, line));
    frames_in.close();
    const JSON::json frame = JSON::json::parse(line);
    REQUIRE(frame["m"]["generic_events"] == 8.0);
    REQUIRE(frame["m"]["events_seen_at_process"] == 8.0);
    REQUIRE(frame["m"]["pointer_downs"] == 2.0);
    REQUIRE(frame["m"]["pointer_moves"] == 2.0);
    REQUIRE(frame["m"]["pointer_ups"] == 2.0);
    REQUIRE(frame["m"]["key_downs"] == 1.0);
    REQUIRE(frame["m"]["key_ups"] == 1.0);

    std::filesystem::remove(input_path);
    std::filesystem::remove_all(trace_dir);
}

#endif

TEST_CASE("headless runner expands gesture input JSONL")
{
    registerInputSmokeProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_input_gestures_");
    const std::filesystem::path input_path = makeTraceDir("bitloop_input_gestures_");
    std::filesystem::remove_all(trace_dir);
    std::filesystem::remove(input_path);

    {
        std::ofstream input(input_path, std::ios::binary);
        REQUIRE(input.good());
        input << R"({"frame":0,"type":"mouse_drag","from":[100,80],"to":{"x":120,"y":90},"duration":2,"easing":"smoothstep"})" << '\n';
        input << R"({"frame":3,"type":"finger_drag","finger_id":7,"from_x":160,"from_y":50,"to_x":176,"to_y":60,"end_frame":5,"pressure":0.75,"release_pressure":0.25,"easing":"ease-out"})" << '\n';
        input << R"({"frame":6,"type":"key_tap","key":"space","scancode":"space","duration":1})" << '\n';
    }

    HeadlessRunOptions options;
    options.project = "InputSmoke";
    options.trace_dir = trace_dir;
    options.input_path = input_path;
    options.frames = 8;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);
    options.trace_format = HeadlessTraceFormat::Full;

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.input_events_queued == 10);

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    REQUIRE(metadata_in.good());
    const JSON::json metadata = JSON::json::parse(metadata_in);
    metadata_in.close();
    REQUIRE(metadata["input"]["enabled"] == true);
    REQUIRE(metadata["input"]["format"] == "jsonl_input_replay_v1");
    REQUIRE(metadata["input"]["events_loaded"] == 10);
    REQUIRE(metadata["input"]["first_frame"] == 0);
    REQUIRE(metadata["input"]["last_frame"] == 7);

    std::ifstream frames_in(trace_dir / "frames.jsonl", std::ios::binary);
    REQUIRE(frames_in.good());
    std::string line;
    JSON::json last_frame;
    while (std::getline(frames_in, line))
        last_frame = JSON::json::parse(line);
    frames_in.close();

    REQUIRE(last_frame["metrics"]["generic_events"] == 10.0);
    REQUIRE(last_frame["metrics"]["events_seen_at_process"] == 10.0);
    REQUIRE(last_frame["metrics"]["pointer_downs"] == 2.0);
    REQUIRE(last_frame["metrics"]["pointer_moves"] == 4.0);
    REQUIRE(last_frame["metrics"]["pointer_ups"] == 2.0);
    REQUIRE(last_frame["metrics"]["key_downs"] == 1.0);
    REQUIRE(last_frame["metrics"]["key_ups"] == 1.0);

    std::filesystem::remove(input_path);
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner reports malformed input replay")
{
    registerInputSmokeProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_bad_input_");
    const std::filesystem::path input_path = makeTraceDir("bitloop_bad_input_");
    std::filesystem::remove_all(trace_dir);
    std::filesystem::remove(input_path);

    {
        std::ofstream input(input_path, std::ios::binary);
        REQUIRE(input.good());
        input << R"({"frame":0,"type":"mouse_down","x":100})" << '\n';
    }

    HeadlessRunOptions options;
    options.project = "InputSmoke";
    options.trace_dir = trace_dir;
    options.input_path = input_path;
    options.frames = 1;
    options.surface = IVec2(320, 200);

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code != 0);
    REQUIRE(result.error.find("Input replay JSONL line 1") != std::string::npos);
    REQUIRE(result.error.find("missing required field 'y'") != std::string::npos);

    std::ifstream summary_in(trace_dir / "summary.json", std::ios::binary);
    REQUIRE(summary_in.good());
    const JSON::json summary = JSON::json::parse(summary_in);
    summary_in.close();
    REQUIRE(summary["ok"] == false);
    REQUIRE(summary["error"].get<std::string>().find("Input replay JSONL line 1") != std::string::npos);

    std::filesystem::remove(input_path);
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner writes semantic trace files")
{
    registerTraceSmokeProject();
    TraceSmokeScene::resetCounters();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_smoke_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "TraceSmoke";
    options.trace_dir = trace_dir;
    options.frames = 3;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.processed_frames == 3);
    REQUIRE(result.trace_frames == 3);
    REQUIRE(result.scene_frames == 3);
    REQUIRE(result.total_objects == 3);
    REQUIRE(TraceSmokeScene::process_calls == 3);
    REQUIRE(TraceSmokeScene::trace_calls == 3);

    REQUIRE(std::filesystem::exists(trace_dir / "metadata.json"));
    REQUIRE(std::filesystem::exists(trace_dir / "frames.jsonl"));
    REQUIRE(std::filesystem::exists(trace_dir / "summary.json"));

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    REQUIRE(metadata_in.good());
    const JSON::json metadata = JSON::json::parse(metadata_in);
    metadata_in.close();

    REQUIRE(metadata["trace"]["format"] == "compact_delta");
    REQUIRE(metadata["schema_version"] == 4);
    REQUIRE(metadata["trace"]["schedule"]["mode"] == "all");
    REQUIRE(metadata["trace"]["schedule"]["every_frames"] == 1);
    REQUIRE(metadata["trace"]["schedule"]["at_frames"].empty());
    REQUIRE(metadata["trace"]["frames_observed"] == 3);
    REQUIRE(metadata["trace"]["scene_frames_observed"] == 3);
    REQUIRE(metadata["trace"]["scenes"].contains("0"));
    REQUIRE(metadata["trace"]["scenes"]["0"]["name"] == "trace-smoke");

    std::ifstream frames_in(trace_dir / "frames.jsonl", std::ios::binary);
    REQUIRE(frames_in.good());

    std::string line;
    int line_count = 0;
    JSON::json first_frame;
    JSON::json last_frame;
    while (std::getline(frames_in, line))
    {
        REQUIRE_FALSE(line.empty());
        last_frame = JSON::json::parse(line);
        if (line_count == 0)
            first_frame = last_frame;
        line_count++;
    }

    REQUIRE(line_count == 3);
    REQUIRE_FALSE(last_frame.contains("project"));
    REQUIRE(first_frame["k"] == true);
    REQUIRE(first_frame["o"]["dot"]["kind"] == "particle");
    REQUIRE(first_frame["o"]["dot"]["y"] == 2.0);
    REQUIRE(last_frame["f"] == 2);
    REQUIRE(last_frame["o"]["dot"]["x"] == 3.0);
    REQUIRE(last_frame["m"]["steps"] == 3.0);

    frames_in.close();
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner traces the union of periodic and exact frames")
{
    registerTraceSmokeProject();
    TraceSmokeScene::resetCounters();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_sparse_union_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "TraceSmoke";
    options.trace_dir = trace_dir;
    options.frames = 7;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);
    options.trace_every_frames = 3;
    options.trace_at_frames = {6, 4, 6};
    options.trace_keyframe_interval = 2;

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.processed_frames == 7);
    REQUIRE(result.trace_frames == 4);
    REQUIRE(result.scene_frames == 4);
    REQUIRE(result.total_objects == 4);
    REQUIRE(TraceSmokeScene::process_calls == 7);
    REQUIRE(TraceSmokeScene::trace_calls == 4);

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    REQUIRE(metadata_in.good());
    const JSON::json metadata = JSON::json::parse(metadata_in);
    metadata_in.close();
    REQUIRE(metadata["trace"]["schedule"]["mode"] == "combined");
    REQUIRE(metadata["trace"]["schedule"]["every_frames"] == 3);
    REQUIRE(metadata["trace"]["schedule"]["at_frames"] == JSON::json::array({4, 6}));
    REQUIRE(metadata["trace"]["frames_observed"] == 4);

    std::ifstream frames_in(trace_dir / "frames.jsonl", std::ios::binary);
    REQUIRE(frames_in.good());
    std::vector<JSON::json> frames;
    std::string line;
    while (std::getline(frames_in, line))
        frames.push_back(JSON::json::parse(line));
    frames_in.close();

    REQUIRE(frames.size() == 4);
    REQUIRE(frames[0]["f"] == 0);
    REQUIRE(frames[1]["f"] == 3);
    REQUIRE(frames[2]["f"] == 4);
    REQUIRE(frames[3]["f"] == 6);
    REQUIRE(frames[0]["k"] == true);
    REQUIRE(frames[1]["k"] == true);
    REQUIRE_FALSE(frames[2].contains("k"));
    REQUIRE(frames[3]["k"] == true);
    REQUIRE(frames[3]["m"]["steps"] == 7.0);
    REQUIRE(frames[3]["o"]["dot"]["x"] == 7.0);

    std::ifstream summary_in(trace_dir / "summary.json", std::ios::binary);
    REQUIRE(summary_in.good());
    const JSON::json summary = JSON::json::parse(summary_in);
    summary_in.close();
    REQUIRE(summary["processed_frames"] == 7);
    REQUIRE(summary["trace_frames"] == 4);
    REQUIRE(summary["trace_summary_scope"] == "selected_frames");

    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner supports selected-only and disabled semantic tracing")
{
    registerTraceSmokeProject();

    SECTION("exact frames alone select only those frames")
    {
        TraceSmokeScene::resetCounters();
        const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_selected_only_");
        std::filesystem::remove_all(trace_dir);

        HeadlessRunOptions options;
        options.project = "TraceSmoke";
        options.trace_dir = trace_dir;
        options.frames = 6;
        options.dt = 0.1;
        options.surface = IVec2(320, 200);
        options.trace_at_frames = {2, 5};

        const HeadlessRunResult result = runHeadlessTrace(options);
        REQUIRE(result.exit_code == 0);
        REQUIRE(result.processed_frames == 6);
        REQUIRE(result.trace_frames == 2);
        REQUIRE(TraceSmokeScene::process_calls == 6);
        REQUIRE(TraceSmokeScene::trace_calls == 2);

        std::ifstream frames_in(trace_dir / "frames.jsonl", std::ios::binary);
        REQUIRE(frames_in.good());
        std::vector<JSON::json> frames;
        std::string line;
        while (std::getline(frames_in, line))
            frames.push_back(JSON::json::parse(line));
        frames_in.close();
        REQUIRE(frames.size() == 2);
        REQUIRE(frames[0]["f"] == 2);
        REQUIRE(frames[1]["f"] == 5);
        REQUIRE(frames[1]["m"]["steps"] == 6.0);

        std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
        REQUIRE(metadata_in.good());
        const JSON::json metadata = JSON::json::parse(metadata_in);
        metadata_in.close();
        REQUIRE(metadata["trace"]["schedule"]["mode"] == "selected");
        REQUIRE(metadata["trace"]["schedule"]["every_frames"] == 0);

        std::filesystem::remove_all(trace_dir);
    }

    SECTION("zero periodic interval disables semantic tracing")
    {
        TraceSmokeScene::resetCounters();
        const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_disabled_");
        std::filesystem::remove_all(trace_dir);

        HeadlessRunOptions options;
        options.project = "TraceSmoke";
        options.trace_dir = trace_dir;
        options.frames = 4;
        options.dt = 0.1;
        options.surface = IVec2(320, 200);
        options.trace_every_frames = 0;

        const HeadlessRunResult result = runHeadlessTrace(options);
        REQUIRE(result.exit_code == 0);
        REQUIRE(result.processed_frames == 4);
        REQUIRE(result.trace_frames == 0);
        REQUIRE(result.scene_frames == 0);
        REQUIRE(TraceSmokeScene::process_calls == 4);
        REQUIRE(TraceSmokeScene::trace_calls == 0);
        REQUIRE(std::filesystem::file_size(trace_dir / "frames.jsonl") == 0);

        std::ifstream summary_in(trace_dir / "summary.json", std::ios::binary);
        REQUIRE(summary_in.good());
        const JSON::json summary = JSON::json::parse(summary_in);
        summary_in.close();
        REQUIRE(summary["processed_frames"] == 4);
        REQUIRE(summary["trace_frames"] == 0);
        REQUIRE(summary["trace_summary_scope"] == "no_semantic_frames");

        std::filesystem::remove_all(trace_dir);
    }
}

TEST_CASE("headless runner parses sparse semantic trace command-line options")
{
    registerTraceSmokeProject();
    TraceSmokeScene::resetCounters();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_sparse_cli_");
    std::filesystem::remove_all(trace_dir);

    std::vector<std::string> arguments = {
        "app",
        "--headless",
        "--project", "TraceSmoke",
        "--frames", "5",
        "--dt", "0.1",
        "--surface", "320x200",
        "--trace", trace_dir.string(),
        "--trace-every-frames", "3",
        "--trace-at", "1,4"
    };
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments)
        argv.push_back(argument.data());

    REQUIRE(runHeadlessFromCommandLine(static_cast<int>(argv.size()), argv.data()) == 0);
    REQUIRE(TraceSmokeScene::process_calls == 5);
    REQUIRE(TraceSmokeScene::trace_calls == 4);

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    REQUIRE(metadata_in.good());
    const JSON::json metadata = JSON::json::parse(metadata_in);
    metadata_in.close();
    REQUIRE(metadata["trace"]["schedule"]["mode"] == "combined");
    REQUIRE(metadata["trace"]["schedule"]["every_frames"] == 3);
    REQUIRE(metadata["trace"]["schedule"]["at_frames"] == JSON::json::array({1, 4}));
    REQUIRE(metadata["trace"]["frames_observed"] == 4);

    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner can write full trace frames")
{
    registerTraceSmokeProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_full_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "TraceSmoke";
    options.trace_dir = trace_dir;
    options.frames = 1;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);
    options.trace_format = HeadlessTraceFormat::Full;

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    REQUIRE(metadata_in.good());
    const JSON::json metadata = JSON::json::parse(metadata_in);
    metadata_in.close();
    REQUIRE(metadata["trace"]["format"] == "full");

    std::ifstream frames_in(trace_dir / "frames.jsonl", std::ios::binary);
    REQUIRE(frames_in.good());
    std::string line;
    REQUIRE(std::getline(frames_in, line));
    frames_in.close();

    const JSON::json frame = JSON::json::parse(line);
    REQUIRE(frame["project"] == "TraceSmoke");
    REQUIRE(frame["objects"][0]["id"] == "dot");
    REQUIRE(frame["metrics"]["steps"] == 1.0);

    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner reports invalid requests")
{
    registerTraceSmokeProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_invalid_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "TraceSmoke";
    options.trace_dir = trace_dir;
    options.frames = 0;

    const HeadlessRunResult bad_frames = runHeadlessTrace(options);
    REQUIRE(bad_frames.exit_code != 0);
    REQUIRE(bad_frames.error.find("Frame count") != std::string::npos);

    options.frames = 1;
    options.trace_every_frames = -1;
    const HeadlessRunResult bad_trace_interval = runHeadlessTrace(options);
    REQUIRE(bad_trace_interval.exit_code != 0);
    REQUIRE(bad_trace_interval.error.find("Trace frame interval") != std::string::npos);

    options.trace_every_frames.reset();
    options.trace_at_frames = {1};
    const HeadlessRunResult trace_frame_outside_run = runHeadlessTrace(options);
    REQUIRE(trace_frame_outside_run.exit_code != 0);
    REQUIRE(trace_frame_outside_run.error.find("outside run range") != std::string::npos);

    options.trace_at_frames.clear();
    options.project = "MissingTraceProject";
    const HeadlessRunResult unknown_project = runHeadlessTrace(options);
    REQUIRE(unknown_project.exit_code != 0);
    REQUIRE(unknown_project.error.find("Unknown project") != std::string::npos);

    const std::filesystem::path file_path = makeTraceDir("bitloop_trace_file_");
    std::filesystem::remove_all(file_path);
    {
        std::ofstream file(file_path, std::ios::binary);
        REQUIRE(file.good());
        file << "not a directory";
    }

    options.project = "TraceSmoke";
    options.trace_dir = file_path;
    const HeadlessRunResult bad_path = runHeadlessTrace(options);
    REQUIRE(bad_path.exit_code != 0);
    REQUIRE(bad_path.error.find("not a directory") != std::string::npos);

    std::filesystem::remove(file_path);
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner records simulation exceptions")
{
    registerThrowingTraceProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_exception_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "TraceThrows";
    options.trace_dir = trace_dir;
    options.frames = 2;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code != 0);
    REQUIRE(result.error.find("trace smoke failure") != std::string::npos);
    REQUIRE(std::filesystem::exists(trace_dir / "summary.json"));

    std::ifstream summary_in(trace_dir / "summary.json", std::ios::binary);
    REQUIRE(summary_in.good());
    const JSON::json summary = JSON::json::parse(summary_in);
    summary_in.close();

    REQUIRE(summary["ok"] == false);
    REQUIRE(summary["error"].get<std::string>().find("trace smoke failure") != std::string::npos);

    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless runner saves and restores checkpoints")
{
    registerTraceSmokeProject();

    const std::filesystem::path first_trace_dir = makeTraceDir("bitloop_trace_checkpoint_a_");
    const std::filesystem::path second_trace_dir = makeTraceDir("bitloop_trace_checkpoint_b_");
    std::filesystem::remove_all(first_trace_dir);
    std::filesystem::remove_all(second_trace_dir);

    HeadlessRunOptions first;
    first.project = "TraceSmoke";
    first.trace_dir = first_trace_dir;
    first.frames = 2;
    first.dt = 0.1;
    first.surface = IVec2(320, 200);
    first.checkpoint_at_frames = {1};

    const HeadlessRunResult first_result = runHeadlessTrace(first);
    REQUIRE(first_result.exit_code == 0);
    REQUIRE(first_result.checkpoints == 1);

    const std::filesystem::path checkpoint_path =
        first_trace_dir / "checkpoints" / "frame_00000001";
    REQUIRE(std::filesystem::exists(checkpoint_path / "metadata.json"));

    HeadlessRunOptions second;
    second.project = "TraceSmoke";
    second.trace_dir = second_trace_dir;
    second.frames = 2;
    second.dt = 0.1;
    second.surface = IVec2(320, 200);
    second.restore_checkpoint = checkpoint_path;
    second.trace_at_frames = {3};

    const HeadlessRunResult second_result = runHeadlessTrace(second);
    REQUIRE(second_result.exit_code == 0);
    REQUIRE(second_result.processed_frames == 2);
    REQUIRE(second_result.trace_frames == 1);

    std::ifstream frames_in(second_trace_dir / "frames.jsonl", std::ios::binary);
    REQUIRE(frames_in.good());

    std::string line;
    JSON::json last_frame;
    while (std::getline(frames_in, line))
        last_frame = JSON::json::parse(line);
    frames_in.close();

    REQUIRE(last_frame["f"] == 3);
    REQUIRE(last_frame["m"]["steps"] == 4.0);
    REQUIRE(last_frame["o"]["dot"]["x"] == 4.0);

    std::filesystem::remove_all(first_trace_dir);
    std::filesystem::remove_all(second_trace_dir);
}

TEST_CASE("headless runner records traceCanvas requests without rendering")
{
    registerSnapshotRequestProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_trace_canvas_request_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "TraceSnapshotRequest";
    options.trace_dir = trace_dir;
    options.frames = 1;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);
    options.trace_every_frames = 0;

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.processed_frames == 1);
    REQUIRE(result.trace_frames == 0);
    REQUIRE(result.snapshots == 0);

    std::ifstream snapshots_in(trace_dir / "snapshots.jsonl", std::ios::binary);
    REQUIRE(snapshots_in.good());

    std::string line;
    REQUIRE(std::getline(snapshots_in, line));
    const JSON::json snapshot = JSON::json::parse(line);
    snapshots_in.close();

    REQUIRE(snapshot["id"] == "first_frame");
    REQUIRE(snapshot["rendered"] == false);
    REQUIRE(snapshot["pixel_rect"]["width"] == 10.0);

    std::ifstream summary_in(trace_dir / "summary.json", std::ios::binary);
    REQUIRE(summary_in.good());
    const JSON::json summary = JSON::json::parse(summary_in);
    summary_in.close();
    REQUIRE(summary["snapshot_requests"] == 1);
    REQUIRE(summary["snapshots_failed"] == 1);

    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless snapshot routes preserve native pixels and capture cadence")
{
    registerCustomSnapshotProject();
    const auto trace_dir = makeTraceDir("bitloop_snapshot_routes_");
    HeadlessRunOptions options;
    options.project = "CustomSnapshot";
    options.trace_dir = trace_dir;
    options.frames = 5;
    options.dt = 0.1;
    options.surface = IVec2(17, 11);
    options.trace_every_frames = 0;
    options.enable_snapshots = true;
    options.snapshot_every_frames = 2;
    options.snapshot_routes = { {"fluid", "grid"}, {"fluid", "alternate"} };
    SECTION("frame cadence") {}
    SECTION("time cadence")
    {
        options.snapshot_every_frames = 0;
        options.snapshot_every_seconds = 0.2;
    }

    const auto result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.snapshots == 4);
    REQUIRE(result.trace_frames == 0);
    REQUIRE(CustomSnapshotScene::process_calls == 5);
    REQUIRE(CustomSnapshotScene::capture_calls == 4);

    std::ifstream snapshots(trace_dir / "snapshots.jsonl");
    std::string line;
    int count = 0;
    while (std::getline(snapshots, line))
    {
        const auto record = JSON::json::parse(line);
        REQUIRE(record["rendered"] == true);
        REQUIRE(record["scene_id"] == "fluid");
        REQUIRE(record["viewport_id"] == "");
        REQUIRE(record["frame"] == (count < 2 ? 1 : 3));
        const bool grid = record["route"] == "grid";
        REQUIRE(record["resolution"]["width"] == (grid ? 3 : 2));
        REQUIRE(record["resolution"]["height"] == (grid ? 2 : 1));

        ImageRGBA8 image;
        const auto path = trace_dir / record["filename"].get<std::string>();
        REQUIRE(loadImageRGBA8(path.string().c_str(), image));
        REQUIRE(image.w == (grid ? 3 : 2));
        REQUIRE(image.h == (grid ? 2 : 1));
        const bytebuf expected = {
            static_cast<uint8_t>(count < 2 ? 2 : 4), 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 255, 0, 255, 255, 0, 255, 255, 0, 255, 255, 93
        };
        REQUIRE(image.pixels == bytebuf(expected.begin(), expected.begin() + (grid ? 24 : 8)));
        count++;
    }
    REQUIRE(count == 4);
    snapshots.close();
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless custom snapshot requests run only when enabled")
{
    registerCustomSnapshotProject();
    CustomSnapshotScene::request_snapshot = true;
    const auto trace_dir = makeTraceDir("bitloop_snapshot_request_");
    HeadlessRunOptions options;
    options.project = "CustomSnapshot";
    options.trace_dir = trace_dir;
    options.frames = 1;
    options.trace_every_frames = 0;
    SECTION("disabled") { options.enable_snapshots = false; }
    SECTION("enabled") { options.enable_snapshots = true; }

    const auto result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.snapshots == (options.enable_snapshots ? 1 : 0));
    REQUIRE(CustomSnapshotScene::capture_calls == (options.enable_snapshots ? 1 : 0));
    REQUIRE(CustomSnapshotScene::process_calls == 1);
    std::ifstream snapshots(trace_dir / "snapshots.jsonl");
    std::string line;
    REQUIRE(std::getline(snapshots, line));
    const auto record = JSON::json::parse(line);
    REQUIRE(record["route"] == "grid");
    REQUIRE(record["reason"] == "event capture");
    REQUIRE(record["rendered"] == options.enable_snapshots);
    snapshots.close();
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless snapshot routes report unknown sources and malformed images")
{
    registerCustomSnapshotProject();
    const auto trace_dir = makeTraceDir("bitloop_snapshot_errors_");
    HeadlessRunOptions options;
    options.project = "CustomSnapshot";
    options.trace_dir = trace_dir;
    options.frames = 1;
    options.enable_snapshots = true;
    options.snapshot_every_frames = 1;
    options.snapshot_routes = { {"fluid", "unknown"}, {"fluid", "short"}, {"fluid", "empty"} };
    SECTION("invalid images")
    {
        const auto result = runHeadlessTrace(options);
        REQUIRE(result.exit_code == 0);
        REQUIRE(result.snapshots == 0);
        std::ifstream snapshots(trace_dir / "snapshots.jsonl");
        std::string line;
        int count = 0;
        while (std::getline(snapshots, line))
        {
            const auto record = JSON::json::parse(line);
            REQUIRE(record["rendered"] == false);
            REQUIRE_FALSE(record["error"].get<std::string>().empty());
            REQUIRE(record["filename"] == "");
            count++;
        }
        REQUIRE(count == 3);
    }
    SECTION("inactive Scene")
    {
        options.snapshot_routes = { {"missing", "grid"} };
        const auto result = runHeadlessTrace(options);
        REQUIRE(result.exit_code != 0);
        REQUIRE(result.error.find("active Scene ID") != std::string::npos);
        REQUIRE(CustomSnapshotScene::capture_calls == 0);
    }
    std::filesystem::remove_all(trace_dir);
}

TEST_CASE("headless snapshots isolate hidden Viewports and report invalid targets")
{
    registerIsolatedSnapshotProject();

    const std::filesystem::path trace_dir = makeTraceDir("bitloop_isolated_snapshot_");
    std::filesystem::remove_all(trace_dir);

    HeadlessRunOptions options;
    options.project = "IsolatedSnapshot";
    options.trace_dir = trace_dir;
    options.frames = 1;
    options.dt = 0.1;
    options.surface = IVec2(320, 200);
    options.enable_snapshots = true;
    options.snapshot_every_frames = 1;

    const HeadlessRunResult result = runHeadlessTrace(options);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.snapshots == 3);

    std::ifstream snapshots_in(trace_dir / "snapshots.jsonl", std::ios::binary);
    REQUIRE(snapshots_in.good());

    std::vector<JSON::json> snapshots;
    std::string line;
    while (std::getline(snapshots_in, line))
        snapshots.push_back(JSON::json::parse(line));
    snapshots_in.close();
    REQUIRE(snapshots.size() == 5);

    auto find_snapshot = [&](std::string_view id) -> const JSON::json&
    {
        auto found = std::find_if(snapshots.begin(), snapshots.end(), [&](const JSON::json& snapshot)
        {
            return snapshot["id"].get<std::string>() == id;
        });
        REQUIRE(found != snapshots.end());
        return *found;
    };

    const JSON::json& isolated = find_snapshot("isolated");
    REQUIRE(isolated["rendered"] == true);
    REQUIRE(isolated["scene_id"] == "isolated");
    REQUIRE(isolated["viewport_id"] == "isolated-main");
    REQUIRE(isolated["resolution"]["width"] == 96);
    REQUIRE(isolated["resolution"]["height"] == 64);

    const JSON::json& world_crop = find_snapshot("world-crop");
    REQUIRE(world_crop["rendered"] == true);
    REQUIRE(world_crop["viewport_id"] == "isolated-main");
    REQUIRE(world_crop["pixel_rect"]["width"] == 20);
    REQUIRE(world_crop["pixel_rect"]["height"] == 20);
    REQUIRE(world_crop["resolution"]["width"] == 20);
    REQUIRE(world_crop["resolution"]["height"] == 20);

    const JSON::json& ambiguous = find_snapshot("ambiguous");
    REQUIRE(ambiguous["rendered"] == false);
    REQUIRE(ambiguous["error"].get<std::string>().find("multiple Viewports") != std::string::npos);

    const JSON::json& missing = find_snapshot("missing");
    REQUIRE(missing["rendered"] == false);
    REQUIRE(missing["error"].get<std::string>().find("add a hidden Viewport") != std::string::npos);

    const JSON::json& layout_snapshot = find_snapshot("periodic_frame");
    REQUIRE(layout_snapshot["rendered"] == true);
    REQUIRE(layout_snapshot["scene_id"] == "");
    REQUIRE(layout_snapshot["viewport_id"] == "");
    REQUIRE(layout_snapshot["resolution"]["width"] == 320);
    REQUIRE(layout_snapshot["resolution"]["height"] == 200);

    ImageRGBA8 image;
    std::string image_error;
    const std::filesystem::path image_path = trace_dir / isolated["filename"].get<std::string>();
    REQUIRE(loadImageRGBA8(image_path.string().c_str(), image, 0, 0, &image_error));
    REQUIRE(image.w == 96);
    REQUIRE(image.h == 64);
    const std::size_t center = (static_cast<std::size_t>(image.h / 2) * image.w + image.w / 2) * 4;
    REQUIRE(image.pixels[center + 0] == 220);
    REQUIRE(image.pixels[center + 1] == 30);
    REQUIRE(image.pixels[center + 2] == 40);

    ImageRGBA8 layout_image;
    const std::filesystem::path layout_path = trace_dir / layout_snapshot["filename"].get<std::string>();
    REQUIRE(loadImageRGBA8(layout_path.string().c_str(), layout_image, 0, 0, &image_error));
    REQUIRE(layout_image.w == 320);
    REQUIRE(layout_image.h == 200);
    const std::size_t layout_center = (static_cast<std::size_t>(layout_image.h / 2) * layout_image.w + layout_image.w / 2) * 4;
    REQUIRE(layout_image.pixels[layout_center + 0] == 20);
    REQUIRE(layout_image.pixels[layout_center + 1] == 60);
    REQUIRE(layout_image.pixels[layout_center + 2] == 220);

    std::filesystem::remove_all(trace_dir);
}
