#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    struct CatalogueCounters
    {
        static inline std::array<int, 3> constructed{};

        static void reset()
        {
            constructed.fill(0);
        }
    };

    template<int Index>
    struct CatalogueScene : BasicScene
    {
        int steps = 0;

        CatalogueScene()
        {
            CatalogueCounters::constructed[Index]++;
        }

        void sceneProcess(double) override
        {
            steps++;
        }

        void viewportDraw(Viewport*) const override
        {
        }

        void traceFrame(SimTraceFrame& frame) const override
        {
            frame.metric("steps", static_cast<double>(steps));
        }

        bool saveCheckpoint(SimCheckpointWriter& writer) const override
        {
            writer.writePod("steps", steps);
            return true;
        }

        bool loadCheckpoint(const SimCheckpointReader& reader) override
        {
            reader.readPod("steps", steps);
            return true;
        }
    };

    struct CatalogueProject : BasicProject
    {
        SceneHandle<CatalogueScene<0>> first;
        SceneHandle<CatalogueScene<1>> second;
        SceneHandle<CatalogueScene<2>> unnamed;

        static ProjectInfo info()
        {
            return ProjectInfo({ "Tests", "SceneCatalogue" });
        }

        void projectPrepare(Layout& layout) override
        {
            first = create<CatalogueScene<0>>("first");
            second = create<CatalogueScene<1>>("second");
            unnamed = create<CatalogueScene<2>>();
            layout << first << second << unnamed;
            setDefaultScenes({ second });
        }
    };

    void registerCatalogueProject()
    {
        static bool registered = false;
        if (registered)
            return;
        ProjectBase::addProjectFactoryInfo(
            ProjectBase::createProjectFactoryInfo<CatalogueProject>("SceneCatalogue"));
        registered = true;
    }

    std::filesystem::path temporaryTraceDirectory(std::string_view prefix)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(stamp));
    }
}

TEST_CASE("Scene catalogue declaration constructs no Scenes and exposes defaults")
{
    CatalogueCounters::reset();
    ImDebugLog log;
    CaptureContext capture_context;
    CatalogueProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.prepareHeadlessSceneCatalogue();

    const std::vector<SceneCatalogueEntry> catalogue = project.sceneCatalogue();
    REQUIRE(catalogue.size() == 3);
    REQUIRE(catalogue[0].id == "first");
    REQUIRE(catalogue[0].selectable);
    REQUIRE_FALSE(catalogue[0].selected_by_default);
    REQUIRE(catalogue[1].id == "second");
    REQUIRE(catalogue[1].selected_by_default);
    REQUIRE(catalogue[2].id == "scene-2");
    REQUIRE_FALSE(catalogue[2].selectable);
    REQUIRE((CatalogueCounters::constructed == std::array<int, 3>{ 0, 0, 0 }));
}

TEST_CASE("headless Scene selection materializes exact IDs in declaration order")
{
    CatalogueCounters::reset();
    ImDebugLog log;
    CaptureContext capture_context;
    CatalogueProject project;
    project.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    project.setHeadlessSceneSelection({ "second", "first" });
    project.startHeadless();

    REQUIRE((project.selectedSceneIds() == std::vector<std::string>{ "first", "second" }));
    REQUIRE(project.currentLayout().count() == 2);
    REQUIRE((CatalogueCounters::constructed == std::array<int, 3>{ 1, 1, 0 }));

    project.stopHeadless();
}

TEST_CASE("invalid headless Scene selections fail before construction")
{
    CatalogueCounters::reset();
    ImDebugLog log;
    CaptureContext capture_context;
    CatalogueProject unknown;
    unknown.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    unknown.setHeadlessSceneSelection({ "missing" });
    REQUIRE_THROWS_WITH(unknown.startHeadless(), Catch::Matchers::ContainsSubstring("Unknown Scene ID"));
    REQUIRE((CatalogueCounters::constructed == std::array<int, 3>{ 0, 0, 0 }));

    CatalogueProject duplicate;
    duplicate.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    REQUIRE_THROWS_WITH(
        duplicate.setHeadlessSceneSelection({ "first", "first" }),
        Catch::Matchers::ContainsSubstring("selected more than once"));
    REQUIRE((CatalogueCounters::constructed == std::array<int, 3>{ 0, 0, 0 }));

    CatalogueProject unnamed;
    unnamed.configureHeadless(-1, &log, IVec2(320, 200), &capture_context);
    unnamed.setHeadlessSceneSelection({ "scene-2" });
    REQUIRE_THROWS_WITH(unnamed.startHeadless(), Catch::Matchers::ContainsSubstring("Unknown Scene ID"));
    REQUIRE((CatalogueCounters::constructed == std::array<int, 3>{ 0, 0, 0 }));
}

TEST_CASE("--list-scenes needs neither frames nor trace output and constructs nothing")
{
    registerCatalogueProject();
    CatalogueCounters::reset();

    char executable[] = "app";
    char headless[] = "--headless";
    char project_flag[] = "--project";
    char project_name[] = "SceneCatalogue";
    char list_flag[] = "--list-scenes";
    char* arguments[] = { executable, headless, project_flag, project_name, list_flag };

    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());
    const int exit_code = runHeadlessFromCommandLine(static_cast<int>(std::size(arguments)), arguments);
    std::cout.rdbuf(previous);

    REQUIRE(exit_code == 0);
    const JSON::json catalogue = JSON::json::parse(output.str());
    REQUIRE(catalogue["project"] == "SceneCatalogue");
    REQUIRE(catalogue["scenes"].size() == 3);
    REQUIRE(catalogue["scenes"][1]["id"] == "second");
    REQUIRE(catalogue["scenes"][1]["default"] == true);
    REQUIRE((CatalogueCounters::constructed == std::array<int, 3>{ 0, 0, 0 }));
}

TEST_CASE("checkpoint restore rejects a different selected Scene set")
{
    registerCatalogueProject();
    CatalogueCounters::reset();
    const std::filesystem::path first_trace = temporaryTraceDirectory("bitloop_scene_selection_a_");
    const std::filesystem::path second_trace = temporaryTraceDirectory("bitloop_scene_selection_b_");
    std::filesystem::remove_all(first_trace);
    std::filesystem::remove_all(second_trace);

    HeadlessRunOptions first;
    first.project = "SceneCatalogue";
    first.scene_ids = { "first" };
    first.trace_dir = first_trace;
    first.frames = 1;
    first.checkpoint_at_frames = { 0 };
    const HeadlessRunResult first_result = runHeadlessTrace(first);
    REQUIRE(first_result.exit_code == 0);
    REQUIRE((first_result.selected_scene_ids == std::vector<std::string>{ "first" }));

    HeadlessRunOptions second;
    second.project = "SceneCatalogue";
    second.scene_ids = { "second" };
    second.trace_dir = second_trace;
    second.frames = 1;
    second.restore_checkpoint = first_trace / "checkpoints" / "frame_00000000";
    const HeadlessRunResult second_result = runHeadlessTrace(second);
    REQUIRE(second_result.exit_code == 1);
    REQUIRE(second_result.error.find("Scene selection mismatch") != std::string::npos);

    std::filesystem::remove_all(first_trace);
    std::filesystem::remove_all(second_trace);
}

#endif
