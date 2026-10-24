#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    struct Options
    {
        fs::path frames_file;
        fs::path metadata_file;
        fs::path index_file;
        std::int64_t frame = -1;
        std::string scene_filter;
        bool no_index = false;
        bool pretty = false;
    };

    struct SceneState
    {
        std::string id;
        std::string name;
        json objects = json::object();
        json metrics = json::object();
        json events = json::array();
        bool initialized = false;
    };

    [[noreturn]] void usage(int exit_code)
    {
        std::ostream& out = exit_code == 0 ? std::cout : std::cerr;
        out
            << "Usage: reconstruct_full_frame --file frames.jsonl --frame N [options]\n"
            << "Options:\n"
            << "  --metadata PATH   Default: sibling metadata.json\n"
            << "  --index PATH      Default: sibling frames.jsonl.idx.json\n"
            << "  --scene ID_OR_NAME\n"
            << "  --no-index        Scan from start instead of using/creating an index\n"
            << "  --pretty          Pretty-print JSON output\n";
        std::exit(exit_code);
    }

    bool parseInt64(std::string_view text, std::int64_t& out)
    {
        try
        {
            std::size_t consumed = 0;
            const std::string tmp(text);
            out = std::stoll(tmp, &consumed);
            return consumed == tmp.size();
        }
        catch (...)
        {
            return false;
        }
    }

    Options parseArgs(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; i++)
        {
            const std::string_view arg(argv[i]);
            auto requireValue = [&](std::string_view flag) -> const char*
            {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing value for " + std::string(flag));
                return argv[++i];
            };

            if (arg == "--file")
            {
                options.frames_file = requireValue(arg);
            }
            else if (arg == "--metadata")
            {
                options.metadata_file = requireValue(arg);
            }
            else if (arg == "--index")
            {
                options.index_file = requireValue(arg);
            }
            else if (arg == "--frame")
            {
                const char* value = requireValue(arg);
                if (!parseInt64(value, options.frame))
                    throw std::runtime_error("Invalid --frame value");
            }
            else if (arg == "--scene")
            {
                options.scene_filter = requireValue(arg);
            }
            else if (arg == "--no-index")
            {
                options.no_index = true;
            }
            else if (arg == "--pretty")
            {
                options.pretty = true;
            }
            else if (arg == "--help" || arg == "-h")
            {
                usage(0);
            }
            else
            {
                throw std::runtime_error("Unknown option: " + std::string(arg));
            }
        }

        if (options.frames_file.empty())
            throw std::runtime_error("Missing required --file");
        if (options.frame < 0)
            throw std::runtime_error("Missing or invalid required --frame");
        if (options.metadata_file.empty())
            options.metadata_file = options.frames_file.parent_path() / "metadata.json";
        if (options.index_file.empty())
            options.index_file = options.frames_file.parent_path() /
                (options.frames_file.filename().string() + ".idx.json");

        return options;
    }

    json readJsonFile(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("Could not open JSON file: " + path.string());
        return json::parse(in);
    }

    std::int64_t fileMtimeNs(const fs::path& path)
    {
        const auto time = fs::last_write_time(path).time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(time).count();
    }

    std::string sceneNameFromMetadata(const json& metadata, const std::string& scene_id)
    {
        if (metadata.contains("trace") &&
            metadata["trace"].contains("scenes") &&
            metadata["trace"]["scenes"].contains(scene_id) &&
            metadata["trace"]["scenes"][scene_id].contains("name"))
        {
            return metadata["trace"]["scenes"][scene_id]["name"].get<std::string>();
        }
        return scene_id;
    }

    std::string lineSceneId(const json& line)
    {
        if (!line.contains("s"))
            return "0";
        if (line["s"].is_string())
            return line["s"].get<std::string>();
        return std::to_string(line["s"].get<int>());
    }

    std::int64_t lineFrame(const json& line)
    {
        if (line.contains("f"))
            return line["f"].get<std::int64_t>();
        return line["frame"].get<std::int64_t>();
    }

    bool lineIsCompact(const json& line)
    {
        return line.contains("f") && line.contains("s");
    }

    json buildIndex(const fs::path& frames_file)
    {
        std::ifstream in(frames_file, std::ios::binary);
        if (!in)
            throw std::runtime_error("Could not open frames file: " + frames_file.string());

        json compact_keyframes = json::object();
        json full_frames = json::object();
        std::string detected_format;

        while (true)
        {
            const std::streampos offset = in.tellg();
            std::string raw;
            if (!std::getline(in, raw))
                break;
            if (raw.empty())
                continue;

            const json line = json::parse(raw);
            const std::int64_t frame = lineFrame(line);
            if (lineIsCompact(line))
            {
                detected_format = "compact_delta";
                if (line.value("k", false))
                {
                    const std::string scene_id = lineSceneId(line);
                    compact_keyframes[scene_id].push_back({
                        {"frame", frame},
                        {"offset", static_cast<std::uint64_t>(offset)}
                    });
                }
            }
            else
            {
                detected_format = "full";
                full_frames[std::to_string(frame)].push_back(static_cast<std::uint64_t>(offset));
            }
        }

        return {
            {"schema_version", 1},
            {"source", fs::absolute(frames_file).string()},
            {"source_size", fs::file_size(frames_file)},
            {"source_mtime_ns", fileMtimeNs(frames_file)},
            {"detected_format", detected_format.empty() ? "unknown" : detected_format},
            {"compact_keyframes", std::move(compact_keyframes)},
            {"full_frames", std::move(full_frames)}
        };
    }

    bool indexMatches(const json& index, const fs::path& frames_file)
    {
        return index.is_object() &&
            index.value("schema_version", 0) == 1 &&
            index.value("source_size", static_cast<std::uintmax_t>(0)) == fs::file_size(frames_file) &&
            index.value("source_mtime_ns", std::int64_t{0}) == fileMtimeNs(frames_file);
    }

    json loadOrBuildIndex(const Options& options)
    {
        if (!options.no_index && fs::exists(options.index_file))
        {
            try
            {
                json index = readJsonFile(options.index_file);
                if (indexMatches(index, options.frames_file))
                    return index;
            }
            catch (...)
            {
            }
        }

        json index = buildIndex(options.frames_file);
        if (!options.no_index)
        {
            std::ofstream out(options.index_file, std::ios::binary);
            if (out)
                out << index.dump(2) << '\n';
        }
        return index;
    }

    bool sceneSelected(const Options& options, const SceneState& scene)
    {
        return options.scene_filter.empty() ||
            options.scene_filter == scene.id ||
            options.scene_filter == scene.name;
    }

    std::uint64_t compactStartOffset(const json& metadata, const json& index, std::int64_t target_frame)
    {
        std::vector<std::string> scene_ids;
        if (metadata.contains("trace") && metadata["trace"].contains("scenes"))
        {
            for (auto it = metadata["trace"]["scenes"].begin(); it != metadata["trace"]["scenes"].end(); ++it)
                scene_ids.push_back(it.key());
        }
        if (scene_ids.empty() && index.contains("compact_keyframes"))
        {
            for (auto it = index["compact_keyframes"].begin(); it != index["compact_keyframes"].end(); ++it)
                scene_ids.push_back(it.key());
        }

        std::uint64_t start = std::numeric_limits<std::uint64_t>::max();
        for (const std::string& scene_id : scene_ids)
        {
            if (!index.contains("compact_keyframes") || !index["compact_keyframes"].contains(scene_id))
            {
                start = 0;
                continue;
            }

            std::uint64_t scene_start = 0;
            for (const json& entry : index["compact_keyframes"][scene_id])
            {
                if (entry["frame"].get<std::int64_t>() <= target_frame)
                    scene_start = entry["offset"].get<std::uint64_t>();
                else
                    break;
            }
            start = std::min(start, scene_start);
        }

        return start == std::numeric_limits<std::uint64_t>::max() ? 0 : start;
    }

    void applyCompactLine(
        const json& line,
        const json& metadata,
        std::int64_t target_frame,
        std::map<std::string, SceneState>& scenes)
    {
        const std::string scene_id = lineSceneId(line);
        SceneState& scene = scenes[scene_id];
        scene.id = scene_id;
        scene.name = sceneNameFromMetadata(metadata, scene_id);
        scene.events = json::array();

        const bool keyframe = line.value("k", false);
        if (keyframe)
        {
            scene.objects = json::object();
            scene.metrics = json::object();
        }

        if (line.contains("m"))
        {
            for (auto it = line["m"].begin(); it != line["m"].end(); ++it)
                scene.metrics[it.key()] = it.value();
        }

        if (line.contains("mr"))
        {
            for (const json& name : line["mr"])
                scene.metrics.erase(name.get<std::string>());
        }

        if (line.contains("o"))
        {
            for (auto it = line["o"].begin(); it != line["o"].end(); ++it)
            {
                const std::string object_id = it.key();
                const json& delta = it.value();
                if (delta.is_null())
                {
                    scene.objects.erase(object_id);
                    continue;
                }

                json& object = scene.objects[object_id];
                if (!object.is_object())
                    object = json::object();

                if (delta.contains("unset"))
                {
                    for (const json& field : delta["unset"])
                        object.erase(field.get<std::string>());
                }

                for (auto field = delta.begin(); field != delta.end(); ++field)
                {
                    if (field.key() != "unset")
                        object[field.key()] = field.value();
                }
            }
        }

        if (lineFrame(line) == target_frame && line.contains("e"))
        {
            for (const json& event : line["e"])
            {
                json out_event = {{"name", event.value("n", "")}};
                if (event.contains("a"))
                    out_event["attributes"] = event["a"];
                scene.events.push_back(std::move(out_event));
            }
        }

        scene.initialized = true;
    }

    json sceneToJson(const SceneState& scene)
    {
        json objects = json::array();
        for (auto it = scene.objects.begin(); it != scene.objects.end(); ++it)
        {
            json object = it.value();
            object["id"] = it.key();
            objects.push_back(std::move(object));
        }

        return {
            {"scene_id", scene.id},
            {"scene", scene.name},
            {"objects", std::move(objects)},
            {"metrics", scene.metrics},
            {"events", scene.events}
        };
    }

    json reconstructCompact(const Options& options, const json& metadata, const json& index)
    {
        const std::uint64_t start_offset = options.no_index
            ? 0
            : compactStartOffset(metadata, index, options.frame);

        std::ifstream in(options.frames_file, std::ios::binary);
        if (!in)
            throw std::runtime_error("Could not open frames file: " + options.frames_file.string());
        in.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);

        std::map<std::string, SceneState> scenes;
        std::string raw;
        while (std::getline(in, raw))
        {
            if (raw.empty())
                continue;
            const json line = json::parse(raw);
            const std::int64_t frame = lineFrame(line);
            if (frame > options.frame)
                break;
            applyCompactLine(line, metadata, options.frame, scenes);
        }

        json scene_array = json::array();
        for (const auto& [id, scene] : scenes)
        {
            (void)id;
            if (scene.initialized && sceneSelected(options, scene))
                scene_array.push_back(sceneToJson(scene));
        }

        if (!options.scene_filter.empty() && scene_array.empty())
            throw std::runtime_error("Scene not found at requested frame: " + options.scene_filter);

        return {
            {"frame", options.frame},
            {"time", metadata.value("start_time", 0.0) +
                (static_cast<double>(options.frame - metadata.value("start_frame", 0)) + 1.0) *
                metadata.value("dt", 0.0)},
            {"dt", metadata.value("dt", 0.0)},
            {"project", metadata.value("project", "")},
            {"source_trace_format", "compact_delta"},
            {"source_offset", start_offset},
            {"scenes", std::move(scene_array)}
        };
    }

    json reconstructFull(const Options& options, const json& metadata, const json& index)
    {
        std::vector<std::uint64_t> offsets;
        const std::string frame_key = std::to_string(options.frame);
        if (!options.no_index &&
            index.contains("full_frames") &&
            index["full_frames"].contains(frame_key))
        {
            for (const json& offset : index["full_frames"][frame_key])
                offsets.push_back(offset.get<std::uint64_t>());
        }

        std::ifstream in(options.frames_file, std::ios::binary);
        if (!in)
            throw std::runtime_error("Could not open frames file: " + options.frames_file.string());

        json scene_array = json::array();
        auto consumeLine = [&](const std::string& raw)
        {
            if (raw.empty())
                return;
            const json frame = json::parse(raw);
            if (frame["frame"].get<std::int64_t>() != options.frame)
                return;

            SceneState scene;
            scene.id = frame.value("scene", "");
            scene.name = frame.value("scene", "");
            if (!options.scene_filter.empty() &&
                options.scene_filter != scene.id &&
                options.scene_filter != scene.name)
            {
                return;
            }

            scene_array.push_back({
                {"scene_id", scene.id},
                {"scene", scene.name},
                {"objects", frame.value("objects", json::array())},
                {"metrics", frame.value("metrics", json::object())},
                {"events", frame.value("events", json::array())}
            });
        };

        if (!offsets.empty())
        {
            for (std::uint64_t offset : offsets)
            {
                in.clear();
                in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
                std::string raw;
                std::getline(in, raw);
                consumeLine(raw);
            }
        }
        else
        {
            std::string raw;
            while (std::getline(in, raw))
            {
                if (raw.empty())
                    continue;
                const json line = json::parse(raw);
                const std::int64_t frame = lineFrame(line);
                if (frame > options.frame)
                    break;
                if (frame == options.frame)
                    consumeLine(raw);
            }
        }

        if (scene_array.empty())
            throw std::runtime_error("Frame not found: " + std::to_string(options.frame));

        return {
            {"frame", options.frame},
            {"project", metadata.value("project", "")},
            {"source_trace_format", "full"},
            {"scenes", std::move(scene_array)}
        };
    }

    void validateRequestedFrame(const Options& options, const json& metadata)
    {
        if (!metadata.contains("frames_requested") || !metadata.contains("start_frame"))
            return;

        const std::int64_t start_frame = metadata.value("start_frame", 0);
        const std::int64_t frames_requested = metadata.value("frames_requested", 0);
        const std::int64_t end_frame = start_frame + frames_requested - 1;
        if (options.frame < start_frame || options.frame > end_frame)
        {
            throw std::runtime_error(
                "Requested frame " + std::to_string(options.frame) +
                " is outside trace range [" + std::to_string(start_frame) +
                ", " + std::to_string(end_frame) + "]");
        }
    }

    void validateObservedFrame(const Options& options, const json& metadata)
    {
        if (!metadata.contains("trace") || !metadata["trace"].contains("schedule"))
            return;

        const json& schedule = metadata["trace"]["schedule"];
        const std::int64_t start_frame = metadata.value("start_frame", 0);
        const std::int64_t every_frames = schedule.value("every_frames", 1);
        bool observed = every_frames > 0 && ((options.frame - start_frame) % every_frames) == 0;
        if (!observed && schedule.contains("at_frames"))
        {
            for (const json& frame : schedule["at_frames"])
            {
                if (frame.get<std::int64_t>() == options.frame)
                {
                    observed = true;
                    break;
                }
            }
        }

        if (!observed)
        {
            throw std::runtime_error(
                "Requested frame " + std::to_string(options.frame) +
                " was not observed by the semantic trace schedule");
        }
    }

    std::string traceFormat(const json& metadata, const json& index)
    {
        if (metadata.contains("trace") && metadata["trace"].contains("format"))
            return metadata["trace"]["format"].get<std::string>();
        return index.value("detected_format", "unknown");
    }
}

int main(int argc, char** argv)
{
    try
    {
        const Options options = parseArgs(argc, argv);
        if (!fs::exists(options.frames_file))
            throw std::runtime_error("Frames file does not exist: " + options.frames_file.string());

        json metadata = json::object();
        if (fs::exists(options.metadata_file))
            metadata = readJsonFile(options.metadata_file);

        validateRequestedFrame(options, metadata);
        validateObservedFrame(options, metadata);
        const json index = loadOrBuildIndex(options);
        const std::string format = traceFormat(metadata, index);

        json out;
        if (format == "compact" || format == "compact_delta" || format == "delta")
            out = reconstructCompact(options, metadata, index);
        else if (format == "full")
            out = reconstructFull(options, metadata, index);
        else
            throw std::runtime_error("Could not determine trace format");

        std::cout << (options.pretty ? out.dump(2) : out.dump()) << '\n';
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "reconstruct_full_frame: " << e.what() << '\n';
        return 1;
    }
}
