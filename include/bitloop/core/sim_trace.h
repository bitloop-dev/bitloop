#pragma once

#ifdef BITLOOP_DEV_MODE

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <bitloop/platform/platform_macros.h>
#include <bitloop/util/json.h>

BL_BEGIN_NS

using SimTraceValue = std::variant<double, std::string>;

struct SimTraceObject
{
    std::string id;
    std::string kind;
    double x = 0.0;
    double y = 0.0;

    std::optional<double> vx;
    std::optional<double> vy;
    std::optional<double> radius;
    std::optional<double> angle;
    std::optional<double> mass;

    std::vector<std::string> tags;
    std::map<std::string, SimTraceValue> attributes;

    SimTraceObject& tag(std::string value);
    SimTraceObject& attribute(std::string name, double value);
    SimTraceObject& attribute(std::string name, std::string value);
    SimTraceObject& attribute(std::string name, const char* value);

    [[nodiscard]] JSON::json toJson() const;
};

struct SimTraceEvent
{
    std::string name;
    std::map<std::string, SimTraceValue> attributes;

    SimTraceEvent& attribute(std::string key, double value);
    SimTraceEvent& attribute(std::string key, std::string value);
    SimTraceEvent& attribute(std::string key, const char* value);

    [[nodiscard]] JSON::json toJson() const;
};

struct SimTraceFrame
{
    static constexpr std::size_t default_max_objects = 100000;

    std::int64_t frame = 0;
    double time = 0.0;
    double dt = 0.0;
    std::string project;
    std::string scene;

    std::vector<SimTraceObject> objects;
    std::map<std::string, SimTraceValue> metrics;
    std::vector<SimTraceEvent> events;

    std::size_t max_objects = default_max_objects;
    std::size_t dropped_objects = 0;

    explicit SimTraceFrame(std::size_t object_limit = default_max_objects);

    SimTraceObject* addObject(std::string id, std::string kind, double x, double y);
    SimTraceEvent& addEvent(std::string name);
    SimTraceFrame& metric(std::string name, double value);
    SimTraceFrame& metric(std::string name, std::string value);
    SimTraceFrame& metric(std::string name, const char* value);

    [[nodiscard]] bool hasTelemetry() const;
    [[nodiscard]] JSON::json toJson() const;
    [[nodiscard]] std::string toJsonLine() const;
};

[[nodiscard]] JSON::json simTraceValueToJson(const SimTraceValue& value);

BL_END_NS

#endif
