#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include <bitloop.h>

using namespace bl;

#ifdef BITLOOP_DEV_MODE

namespace
{
    std::filesystem::path makeCheckpointDir()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("bitloop_checkpoint_unit_" + std::to_string(stamp));
    }
}

TEST_CASE("SimCheckpoint stores binary chunks and JSON metadata")
{
    const std::filesystem::path dir = makeCheckpointDir();
    std::filesystem::remove_all(dir);

    int value = 42;
    std::vector<double> samples = {1.0, 2.0, 3.5};

    SimCheckpointWriter writer(dir);
    writer.writePod("value", value);
    writer.writeTrivialVector("samples", samples);
    writer.writeJson("config", {{"name", "checkpoint"}, {"enabled", true}});
    writer.finalize({{"kind", "unit"}});

    SimCheckpointReader reader(dir);
    REQUIRE(reader.metadata()["kind"] == "unit");
    REQUIRE(reader.hasChunk("value"));
    REQUIRE(reader.hasChunk("samples"));

    int restored_value = 0;
    reader.readPod("value", restored_value);
    REQUIRE(restored_value == 42);

    const std::vector<double> restored_samples = reader.readTrivialVector<double>("samples");
    REQUIRE(restored_samples == samples);

    const JSON::json config = reader.readJson("config");
    REQUIRE(config["name"] == "checkpoint");
    REQUIRE(config["enabled"] == true);

    std::filesystem::remove_all(dir);
}

#endif
