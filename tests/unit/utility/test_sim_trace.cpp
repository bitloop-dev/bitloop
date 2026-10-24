#include <catch2/catch_test_macros.hpp>

#include <bitloop/core/sim_trace.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

TEST_CASE("SimTraceFrame serializes objects, metrics, and events")
{
    SimTraceFrame frame;
    frame.frame = 7;
    frame.time = 0.5;
    frame.dt = 0.25;
    frame.project = "TraceProject";
    frame.scene = "TraceScene";

    SimTraceObject* object = frame.addObject("ball", "particle", 1.0, 2.0);
    REQUIRE(object != nullptr);
    object->vx = 3.0;
    object->vy = 4.0;
    object->radius = 5.0;
    object->tag("primary").attribute("state", "falling").attribute("energy", 42.0);

    frame.metric("total_energy", 42.0);
    frame.metric("phase", "startup");
    frame.addEvent("collision").attribute("with", "floor").attribute("impulse", 12.0);

    JSON::json json = frame.toJson();

    REQUIRE(json["frame"] == 7);
    REQUIRE(json["project"] == "TraceProject");
    REQUIRE(json["scene"] == "TraceScene");
    REQUIRE(json["objects"].size() == 1);
    REQUIRE(json["objects"][0]["id"] == "ball");
    REQUIRE(json["objects"][0]["kind"] == "particle");
    REQUIRE(json["objects"][0]["vx"] == 3.0);
    REQUIRE(json["objects"][0]["attributes"]["state"] == "falling");
    REQUIRE(json["metrics"]["phase"] == "startup");
    REQUIRE(json["events"][0]["name"] == "collision");
}

TEST_CASE("SimTraceFrame enforces object caps")
{
    SimTraceFrame frame(1);

    REQUIRE(frame.addObject("a", "particle", 0.0, 0.0) != nullptr);
    REQUIRE(frame.addObject("b", "particle", 1.0, 1.0) == nullptr);

    JSON::json json = frame.toJson();
    REQUIRE(json["objects"].size() == 1);
    REQUIRE(json["dropped_objects"] == 1);
    REQUIRE(frame.hasTelemetry());
}

#endif
