#include <bitloop/core/sim_trace.h>

#ifdef BITLOOP_DEV_MODE

#include <utility>

BL_BEGIN_NS

namespace
{
    JSON::json valueMapToJson(const std::map<std::string, SimTraceValue>& values)
    {
        JSON::json out = JSON::json::object();
        for (const auto& [key, value] : values)
            out[key] = simTraceValueToJson(value);
        return out;
    }
}

JSON::json simTraceValueToJson(const SimTraceValue& value)
{
    return std::visit([](const auto& v) -> JSON::json
    {
        return v;
    }, value);
}

SimTraceObject& SimTraceObject::tag(std::string value)
{
    tags.push_back(std::move(value));
    return *this;
}

SimTraceObject& SimTraceObject::attribute(std::string name, double value)
{
    attributes[std::move(name)] = value;
    return *this;
}

SimTraceObject& SimTraceObject::attribute(std::string name, std::string value)
{
    attributes[std::move(name)] = std::move(value);
    return *this;
}

SimTraceObject& SimTraceObject::attribute(std::string name, const char* value)
{
    attributes[std::move(name)] = std::string(value ? value : "");
    return *this;
}

JSON::json SimTraceObject::toJson() const
{
    JSON::json out = {
        {"id", id},
        {"kind", kind},
        {"x", x},
        {"y", y}
    };

    if (vx) out["vx"] = *vx;
    if (vy) out["vy"] = *vy;
    if (radius) out["radius"] = *radius;
    if (angle) out["angle"] = *angle;
    if (mass) out["mass"] = *mass;
    if (!tags.empty()) out["tags"] = tags;
    if (!attributes.empty()) out["attributes"] = valueMapToJson(attributes);

    return out;
}

SimTraceEvent& SimTraceEvent::attribute(std::string key, double value)
{
    attributes[std::move(key)] = value;
    return *this;
}

SimTraceEvent& SimTraceEvent::attribute(std::string key, std::string value)
{
    attributes[std::move(key)] = std::move(value);
    return *this;
}

SimTraceEvent& SimTraceEvent::attribute(std::string key, const char* value)
{
    attributes[std::move(key)] = std::string(value ? value : "");
    return *this;
}

JSON::json SimTraceEvent::toJson() const
{
    JSON::json out = {{"name", name}};
    if (!attributes.empty())
        out["attributes"] = valueMapToJson(attributes);
    return out;
}

SimTraceFrame::SimTraceFrame(std::size_t object_limit)
    : max_objects(object_limit)
{
}

SimTraceObject* SimTraceFrame::addObject(std::string id, std::string kind, double x, double y)
{
    if (objects.size() >= max_objects)
    {
        dropped_objects++;
        return nullptr;
    }

    objects.push_back(SimTraceObject{
        .id = std::move(id),
        .kind = std::move(kind),
        .x = x,
        .y = y
    });
    return &objects.back();
}

SimTraceEvent& SimTraceFrame::addEvent(std::string name)
{
    events.push_back(SimTraceEvent{std::move(name)});
    return events.back();
}

SimTraceFrame& SimTraceFrame::metric(std::string name, double value)
{
    metrics[std::move(name)] = value;
    return *this;
}

SimTraceFrame& SimTraceFrame::metric(std::string name, std::string value)
{
    metrics[std::move(name)] = std::move(value);
    return *this;
}

SimTraceFrame& SimTraceFrame::metric(std::string name, const char* value)
{
    metrics[std::move(name)] = std::string(value ? value : "");
    return *this;
}

bool SimTraceFrame::hasTelemetry() const
{
    return !objects.empty() || !metrics.empty() || !events.empty() || dropped_objects > 0;
}

JSON::json SimTraceFrame::toJson() const
{
    JSON::json object_json = JSON::json::array();
    for (const SimTraceObject& object : objects)
        object_json.push_back(object.toJson());

    JSON::json event_json = JSON::json::array();
    for (const SimTraceEvent& event : events)
        event_json.push_back(event.toJson());

    JSON::json out = {
        {"frame", frame},
        {"time", time},
        {"dt", dt},
        {"project", project},
        {"scene", scene},
        {"objects", std::move(object_json)},
        {"metrics", valueMapToJson(metrics)},
        {"events", std::move(event_json)}
    };

    if (dropped_objects > 0)
        out["dropped_objects"] = dropped_objects;

    return out;
}

std::string SimTraceFrame::toJsonLine() const
{
    return toJson().dump();
}

BL_END_NS

#endif
