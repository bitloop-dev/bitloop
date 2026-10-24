#include <bitloop/core/sim_snapshot.h>

#ifdef BITLOOP_DEV_MODE

BL_BEGIN_NS

namespace
{
    JSON::json rectToJson(const IRect& rect)
    {
        return {
            {"x1", rect.x1},
            {"y1", rect.y1},
            {"x2", rect.x2},
            {"y2", rect.y2},
            {"width", rect.width()},
            {"height", rect.height()}
        };
    }

    JSON::json rectToJson(const DRect& rect)
    {
        return {
            {"x1", rect.x1},
            {"y1", rect.y1},
            {"x2", rect.x2},
            {"y2", rect.y2},
            {"width", rect.width()},
            {"height", rect.height()}
        };
    }

    JSON::json valueMapToJson(const std::map<std::string, SimTraceValue>& values)
    {
        JSON::json out = JSON::json::object();
        for (const auto& [key, value] : values)
            out[key] = simTraceValueToJson(value);
        return out;
    }
}

SimSnapshotRequest& SimSnapshotRequest::attribute(std::string name, double value)
{
    attributes[std::move(name)] = value;
    return *this;
}

SimSnapshotRequest& SimSnapshotRequest::attribute(std::string name, std::string value)
{
    attributes[std::move(name)] = std::move(value);
    return *this;
}

SimSnapshotRequest& SimSnapshotRequest::attribute(std::string name, const char* value)
{
    attributes[std::move(name)] = std::string(value ? value : "");
    return *this;
}

JSON::json SimSnapshotRequest::toJson() const
{
    JSON::json out = {
        {"id", id},
        {"reason", reason},
        {"route", route},
        {"viewport_id", viewport_id},
        {"attributes", valueMapToJson(attributes)}
    };

    out["pixel_rect"] = pixel_rect ? rectToJson(*pixel_rect) : JSON::json(nullptr);
    out["world_rect"] = world_rect ? rectToJson(*world_rect) : JSON::json(nullptr);
    return out;
}

JSON::json SimSnapshotRecord::toJson() const
{
    JSON::json out = {
        {"frame", frame},
        {"time", time},
        {"dt", dt},
        {"project", project},
        {"scene", scene},
        {"scene_id", scene_id},
        {"viewport_id", viewport_id},
        {"id", id},
        {"reason", reason},
        {"route", route},
        {"surface", {
            {"width", surface.x},
            {"height", surface.y}
        }},
        {"resolution", {
            {"width", resolution.x},
            {"height", resolution.y}
        }},
        {"attributes", valueMapToJson(attributes)},
        {"filename", filename},
        {"rendered", rendered},
        {"error", error}
    };

    out["pixel_rect"] = pixel_rect ? rectToJson(*pixel_rect) : JSON::json(nullptr);
    out["world_rect"] = world_rect ? rectToJson(*world_rect) : JSON::json(nullptr);
    return out;
}

BL_END_NS

#endif
