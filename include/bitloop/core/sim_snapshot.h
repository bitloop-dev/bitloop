#pragma once

#ifdef BITLOOP_DEV_MODE

#include <bitloop/core/sim_trace.h>
#include <bitloop/core/types.h>

#include <map>
#include <optional>
#include <string>

BL_BEGIN_NS

struct SimSnapshotImage
{
    IVec2 resolution{0, 0};
    bytebuf pixels; // tightly packed RGBA8, top row first
    std::string payload; // Optional application XMP, captured with these pixels on the owner.
};

struct SimSnapshotRequest
{
    std::string id;
    std::string reason;
    std::string route;
    std::string viewport_id;
    std::optional<IRect> pixel_rect;
    std::optional<DRect> world_rect;
    std::map<std::string, SimTraceValue> attributes;

    SimSnapshotRequest& attribute(std::string name, double value);
    SimSnapshotRequest& attribute(std::string name, std::string value);
    SimSnapshotRequest& attribute(std::string name, const char* value);
    [[nodiscard]] JSON::json toJson() const;
};

struct SimSnapshotRecord
{
    std::int64_t frame = 0;
    double time = 0.0;
    double dt = 0.0;
    std::string project;
    std::string scene;
    std::string scene_id;
    std::string viewport_id;
    std::string id;
    std::string reason;
    std::string route;
    IVec2 surface{0, 0};
    IVec2 resolution{0, 0};
    std::optional<IRect> pixel_rect;
    std::optional<DRect> world_rect;
    std::map<std::string, SimTraceValue> attributes;
    std::string filename;
    bool rendered = false;
    std::string error;

    [[nodiscard]] JSON::json toJson() const;
};

BL_END_NS

#endif
