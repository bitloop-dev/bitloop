#include <catch2/catch_test_macros.hpp>
#include <bitloop.h>
#include <webp/decode.h>
#include <chrono>
#include <thread>

using namespace bl;

#ifdef BITLOOP_DEV_MODE
namespace
{
    struct SnapshotScene : BasicScene
    {
        struct Config { bool custom = true; bool malformed = false; };
        Config config;
        int tick = 0;
        mutable std::thread::id capture_thread;

        explicit SnapshotScene(Config& config) : config(config) {}
        void sceneProcess(double) override { ++tick; }
        void viewportDraw(Viewport*) const override {}
        bool captureSnapshot(SnapshotFrame& output) const override
        {
            capture_thread = std::this_thread::get_id();
            if (!config.custom) return false;
            output.resolution = {3, 2};
            output.frame.data.assign(config.malformed ? 4 : 24, uint8_t(tick));
            output.frame.payload = std::to_string(tick);
            return true;
        }
    };

    struct SnapshotProject : BasicProject
    {
        int scenes = 1;
        SnapshotScene::Config config;
        SceneHandle<SnapshotScene> source;
        void projectPrepare(Layout&) override
        {
            for (int i = 0; i < scenes; ++i) source = create<SnapshotScene>(config);
        }
    };
}

TEST_CASE("Snapshot source freezes native dimensions pixels and state on its caller", "[snapshot]")
{
    ImDebugLog log;
    CaptureContext capture;
    SnapshotProject project;
    project.configureHeadless(0, &log, {800, 600}, &capture);
    SnapshotFrame image;
    std::exception_ptr failure;
    std::thread::id owner;
    std::thread thread([&]
    {
        try
        {
            owner = std::this_thread::get_id();
            project.startHeadless();
            project.processHeadlessFrame(.02);
            project.processHeadlessFrame(.02);
            if (!project.captureSnapshot(image)) throw std::runtime_error("No custom image");
            project.processHeadlessFrame(.02);
        }
        catch (...) { failure = std::current_exception(); }
    });
    thread.join();
    if (failure) std::rethrow_exception(failure);
    CHECK(project.source->capture_thread == owner);
    CHECK(image.resolution == IVec2(3, 2));
    CHECK(image.frame.data == bytebuf(24, 2));
    CHECK(image.frame.payload == "2");
    CHECK(project.source->tick == 3);
    project.stopHeadless();
}

TEST_CASE("Snapshot sources preserve viewport fallback and reject malformed images", "[snapshot]")
{
    for (int mode : {0, 1, 2})
    {
        ImDebugLog log;
        CaptureContext capture;
        SnapshotProject project;
        project.config.custom = mode != 0;
        project.config.malformed = mode == 1;
        project.scenes = mode == 2 ? 2 : 1;
        project.configureHeadless(0, &log, {800, 600}, &capture);
        project.startHeadless();
        SnapshotFrame image;
        if (mode == 1) CHECK_THROWS(project.captureSnapshot(image));
        else CHECK_FALSE(project.captureSnapshot(image));
        project.stopHeadless();
    }
}
#endif

TEST_CASE("Native snapshot frame reaches the WebP encoder with its XMP payload", "[snapshot]")
{
    CaptureManager encoder;
    CaptureConfig config{};
    config.format = CaptureFormat::WEBP_SNAPSHOT;
    config.resolution = {3, 2};
    config.lossless = true;
    EncodeFrame frame(bytebuf(24, 255), "FluidSimSnapshot/1:123:YWJj");
    REQUIRE(encoder.startCapture(config));
    REQUIRE(encoder.encodeFrame(frame));
    bool complete = false, stored = false, error = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!(complete = encoder.handleCaptureComplete(&stored, &error)) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    REQUIRE(complete);
    REQUIRE(stored);
    REQUIRE_FALSE(error);
    bytebuf webp;
    encoder.takeCompletedCaptureFromMemory(webp);
    int width = 0, height = 0;
    REQUIRE(WebPGetInfo(webp.data(), webp.size(), &width, &height));
    CHECK(width == 3);
    CHECK(height == 2);
    std::string payload;
    REQUIRE(webp_extract_save_from_xmp(webp, payload));
    CHECK(payload == frame.payload);
}
