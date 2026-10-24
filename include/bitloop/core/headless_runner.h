#pragma once

#ifdef BITLOOP_DEV_MODE

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <bitloop/core/sim_trace.h>
#include <bitloop/core/types.h>

BL_BEGIN_NS

enum class HeadlessTraceFormat
{
    CompactDelta,
    Full
};

struct HeadlessSnapshotRoute
{
    std::string scene_id;
    std::string route;
};

struct HeadlessRunOptions
{
    std::string project;
    std::vector<std::string> scene_ids;
    std::filesystem::path scene_config_path;
    bool list_scenes = false;
    std::filesystem::path trace_dir;
    int frames = 0;
    double dt = 1.0 / 60.0;
    IVec2 surface{ 800, 600 };
    std::size_t max_objects = SimTraceFrame::default_max_objects;
    HeadlessTraceFormat trace_format = HeadlessTraceFormat::CompactDelta;
    std::optional<int> trace_every_frames;
    std::vector<std::int64_t> trace_at_frames;
    int trace_keyframe_interval = 120;
    bool enable_snapshots = false;
    int snapshot_every_frames = 0;
    double snapshot_every_seconds = 0.0;
    std::vector<HeadlessSnapshotRoute> snapshot_routes;
    int checkpoint_every_frames = 0;
    std::vector<std::int64_t> checkpoint_at_frames;
    std::filesystem::path checkpoint_dir;
    std::filesystem::path restore_checkpoint;
    std::filesystem::path input_path;
};

struct HeadlessRunResult
{
    int exit_code = 0;
    std::string error;
    std::filesystem::path trace_dir;
    std::int64_t processed_frames = 0;
    std::int64_t trace_frames = 0;
    std::int64_t scene_frames = 0;
    std::int64_t total_objects = 0;
    std::int64_t dropped_objects = 0;
    std::int64_t snapshots = 0;
    std::int64_t checkpoints = 0;
    std::int64_t input_events_queued = 0;
    std::vector<std::string> selected_scene_ids;
};

[[nodiscard]] bool isHeadlessCommand(int argc, char* argv[]);
[[nodiscard]] int runHeadlessFromCommandLine(int argc, char* argv[]);
[[nodiscard]] HeadlessRunResult runHeadlessTrace(const HeadlessRunOptions& options);

BL_END_NS

#endif
