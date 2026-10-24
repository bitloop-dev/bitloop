#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    struct ConfigurableScene : BasicScene
    {
        struct Config
        {
            int count = 1;
            double speed = 1.0;
        };

        static inline int constructions = 0;
        Config config;

        explicit ConfigurableScene(Config& config) : config(config)
        {
            constructions++;
        }

        void viewportDraw(Viewport*) const override
        {
        }

        void traceFrame(SimTraceFrame& frame) const override
        {
            frame.metric("count", static_cast<double>(config.count));
            frame.metric("speed", config.speed);
        }
    };

    struct UnsupportedConfigScene : BasicScene
    {
        static inline int constructions = 0;

        UnsupportedConfigScene()
        {
            constructions++;
        }

        void viewportDraw(Viewport*) const override
        {
        }
    };

    struct SceneConfigProject : BasicProject
    {
        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "SceneConfig" });
        }

        void projectPrepare(Layout& layout) override
        {
            auto configurable = create<ConfigurableScene>("configurable");
            configurable.setLaunchConfigParser(
                [](ConfigurableScene::Config& config, const JSON::json& values)
                {
                    for (auto field = values.begin(); field != values.end(); ++field)
                    {
                        if (field.key() != "count" && field.key() != "speed")
                            throw std::invalid_argument("Unknown field: " + field.key());
                    }

                    if (values.contains("count"))
                    {
                        if (!values["count"].is_number_integer())
                            throw std::invalid_argument("count must be an integer");
                        config.count = values["count"].get<int>();
                    }
                    if (values.contains("speed"))
                    {
                        if (!values["speed"].is_number())
                            throw std::invalid_argument("speed must be numeric");
                        config.speed = values["speed"].get<double>();
                    }
                    if (config.count < 1 || config.count > 100)
                        throw std::invalid_argument("count must be between 1 and 100");
                    if (!(config.speed > 0.0) || config.speed > 100.0)
                        throw std::invalid_argument("speed must be between 0 and 100");
                    if (static_cast<double>(config.count) * config.speed > 200.0)
                        throw std::invalid_argument("count and speed exceed the work budget");
                });

            auto unsupported = create<UnsupportedConfigScene>("unsupported");
            layout << configurable << unsupported;
            setDefaultScenes({ configurable });
        }
    };

    void registerSceneConfigProject()
    {
        static bool registered = false;
        if (registered)
            return;
        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<SceneConfigProject>("SceneConfig"));
        registered = true;
    }

    std::filesystem::path temporaryDirectory(std::string_view prefix)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(stamp));
    }

    void writeText(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out << text;
    }

    HeadlessRunOptions configurableRun(
        const std::filesystem::path& trace_dir,
        const std::filesystem::path& config_path)
    {
        HeadlessRunOptions options;
        options.project = "SceneConfig";
        options.scene_ids = { "configurable" };
        options.scene_config_path = config_path;
        options.trace_dir = trace_dir;
        options.frames = 1;
        options.trace_format = HeadlessTraceFormat::Full;
        return options;
    }
}

TEST_CASE("typed Scene config overrides alter traces and metadata before construction")
{
    registerSceneConfigProject();
    ConfigurableScene::constructions = 0;
    const std::filesystem::path root = temporaryDirectory("bitloop_scene_config_valid_");
    const std::filesystem::path trace_dir = root / "trace";
    const std::filesystem::path config_path = root / "config.json";
    std::filesystem::create_directories(root);
    writeText(config_path, R"({"count":7,"speed":2.5})");

    const HeadlessRunResult result = runHeadlessTrace(configurableRun(trace_dir, config_path));
    REQUIRE(result.exit_code == 0);
    REQUIRE(ConfigurableScene::constructions == 1);

    std::ifstream frame_in(trace_dir / "frames.jsonl", std::ios::binary);
    std::string line;
    REQUIRE(std::getline(frame_in, line));
    const JSON::json frame = JSON::json::parse(line);
    REQUIRE(frame["scene"] == "configurable");
    REQUIRE(frame["metrics"]["count"] == 7.0);
    REQUIRE(frame["metrics"]["speed"] == 2.5);
    frame_in.close();

    std::ifstream metadata_in(trace_dir / "metadata.json", std::ios::binary);
    const JSON::json metadata = JSON::json::parse(metadata_in);
    REQUIRE(metadata["selected_scene_ids"] == JSON::json::array({ "configurable" }));
    REQUIRE(metadata["scene_config"]["scene_id"] == "configurable");
    REQUIRE(metadata["scene_config"]["values"]["count"] == 7);
    REQUIRE(metadata["scene_config"]["values"]["speed"] == 2.5);
    REQUIRE(metadata["scene_config"]["hash"].get<std::string>().size() == 16);
    metadata_in.close();

    std::filesystem::remove_all(root);
}

TEST_CASE("malformed and invalid Scene config files fail before Scene construction")
{
    registerSceneConfigProject();
    const std::filesystem::path root = temporaryDirectory("bitloop_scene_config_invalid_");
    std::filesystem::create_directories(root);

    const std::pair<std::string, std::string> cases[] = {
        { R"({"count":)", "Malformed Scene config JSON" },
        { R"({"unknown":1})", "Unknown field" },
        { R"({"count":"seven"})", "count must be an integer" },
        { R"({"count":101})", "count must be between" },
        { R"({"count":100,"speed":3})", "work budget" }
    };

    for (std::size_t index = 0; index < std::size(cases); ++index)
    {
        ConfigurableScene::constructions = 0;
        const std::filesystem::path config_path = root / ("config_" + std::to_string(index) + ".json");
        writeText(config_path, cases[index].first);
        const HeadlessRunResult result = runHeadlessTrace(configurableRun(root / ("trace_" + std::to_string(index)), config_path));
        REQUIRE(result.exit_code == 1);
        REQUIRE(result.error.find(cases[index].second) != std::string::npos);
        REQUIRE(ConfigurableScene::constructions == 0);
    }

    std::filesystem::remove_all(root);
}

TEST_CASE("Scene config rejects unsupported Scenes and ambiguous selections")
{
    registerSceneConfigProject();
    ConfigurableScene::constructions = 0;
    UnsupportedConfigScene::constructions = 0;
    const std::filesystem::path root = temporaryDirectory("bitloop_scene_config_unsupported_");
    const std::filesystem::path config_path = root / "config.json";
    std::filesystem::create_directories(root);
    writeText(config_path, R"({})");

    HeadlessRunOptions unsupported = configurableRun(root / "unsupported_trace", config_path);
    unsupported.scene_ids = { "unsupported" };
    const HeadlessRunResult unsupported_result = runHeadlessTrace(unsupported);
    REQUIRE(unsupported_result.exit_code == 1);
    REQUIRE(unsupported_result.error.find("does not support --scene-config") != std::string::npos);
    REQUIRE(UnsupportedConfigScene::constructions == 0);

    HeadlessRunOptions ambiguous = configurableRun(root / "ambiguous_trace", config_path);
    ambiguous.scene_ids = { "configurable", "unsupported" };
    const HeadlessRunResult ambiguous_result = runHeadlessTrace(ambiguous);
    REQUIRE(ambiguous_result.exit_code == 1);
    REQUIRE(ambiguous_result.error.find("exactly one selected Scene") != std::string::npos);
    REQUIRE(ConfigurableScene::constructions == 0);
    REQUIRE(UnsupportedConfigScene::constructions == 0);

    std::filesystem::remove_all(root);
}

TEST_CASE("Scene config hash participates in checkpoint compatibility")
{
    registerSceneConfigProject();
    const std::filesystem::path root = temporaryDirectory("bitloop_scene_config_checkpoint_");
    const std::filesystem::path first_config = root / "first.json";
    const std::filesystem::path second_config = root / "second.json";
    std::filesystem::create_directories(root);
    writeText(first_config, R"({"count":4})");
    writeText(second_config, R"({"count":5})");

    HeadlessRunOptions first = configurableRun(root / "first_trace", first_config);
    first.checkpoint_at_frames = { 0 };
    const HeadlessRunResult first_result = runHeadlessTrace(first);
    REQUIRE(first_result.exit_code == 0);

    HeadlessRunOptions second = configurableRun(root / "second_trace", second_config);
    second.restore_checkpoint = root / "first_trace" / "checkpoints" / "frame_00000000";
    const HeadlessRunResult second_result = runHeadlessTrace(second);
    REQUIRE(second_result.exit_code == 1);
    REQUIRE(second_result.error.find("Scene config mismatch") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("--scene-config requires exactly one --scene")
{
    ConfigurableScene::constructions = 0;
    char executable[] = "app";
    char headless[] = "--headless";
    char project_flag[] = "--project";
    char project_name[] = "SceneConfig";
    char config_flag[] = "--scene-config";
    char config_path[] = "config.json";
    char frames_flag[] = "--frames";
    char frames[] = "1";
    char trace_flag[] = "--trace";
    char trace_path[] = "unused";
    char* arguments[] = {
        executable,
        headless,
        project_flag,
        project_name,
        config_flag,
        config_path,
        frames_flag,
        frames,
        trace_flag,
        trace_path
    };

    std::ostringstream error;
    std::streambuf* previous = std::cerr.rdbuf(error.rdbuf());
    const int exit_code = runHeadlessFromCommandLine(static_cast<int>(std::size(arguments)), arguments);
    std::cerr.rdbuf(previous);

    REQUIRE(exit_code == 2);
    REQUIRE(error.str().find("requires exactly one --scene ID") != std::string::npos);
    REQUIRE(ConfigurableScene::constructions == 0);
}

#endif
