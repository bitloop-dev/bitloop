#pragma once
#include <bitloop.h>

SIM_BEG;

using namespace bl;

struct {SIM_NAME}_Scene : public Scene<{SIM_NAME}_Scene>
{
    CameraInfo       camera;
    CameraNavigator  navigator;

    /// ─────── Variables ───────
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    // double gravity;
    // BITLOOP_TEMPLATE_GUIDE_ELSE

    // BITLOOP_TEMPLATE_GUIDE_END

    /// ─────── Methods ───────
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    // void customMethod();
    // BITLOOP_TEMPLATE_GUIDE_ELSE

    // BITLOOP_TEMPLATE_GUIDE_END

    /// ─────── Launch config ───────
    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// Add configurable fields here, e.g. double gravity = 9.8;
    // BITLOOP_TEMPLATE_GUIDE_END
    struct Config {};

    {SIM_NAME}_Scene(Config& info [[maybe_unused]])
        // BITLOOP_TEMPLATE_GUIDE_BEGIN
        // : gravity(info.gravity)
        // BITLOOP_TEMPLATE_GUIDE_END
    {}

    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    /// ─────── Thread-safe UI for editing Scene inputs with ImGui ───────
    // BITLOOP_TEMPLATE_GUIDE_END
    struct UI : BufferedInterfaceModel
    {
        using BufferedInterfaceModel::BufferedInterfaceModel;
        void sidebar() override;
        // void overlay() override; // ImGui viewport overlay

        /// ─────── Your UI-only variables ───────
        // BITLOOP_TEMPLATE_GUIDE_BEGIN
        // bool test_popup_open = false;
        // BITLOOP_TEMPLATE_GUIDE_ELSE

        // BITLOOP_TEMPLATE_GUIDE_END
    };

    /// ─────── Scene methods ───────
    void sceneStart() override;
    void sceneMounted(Viewport* viewport) override;
    void sceneUnmounted(Viewport* viewport) override;
    void sceneDestroy() override;
    void sceneProcess(double dt) override;

    /// ─────── Viewport methods ───────
    void viewportProcess(Viewport* ctx, double dt) override;
    void viewportDraw(Viewport* ctx) const override;

    /// ─────── Input handling ───────
    void onEvent(Event e) override; // receives all event types

    // BITLOOP_TEMPLATE_GUIDE_BEGIN
    // void onPointerDown(PointerEvent e) override;
    // void onPointerUp(PointerEvent e) override;
    // void onPointerMove(PointerEvent e) override;
    // void onWheel(PointerEvent e) override;
    // void onKeyDown(KeyEvent e) override;
    // void onKeyUp(KeyEvent e) override;
    // BITLOOP_TEMPLATE_GUIDE_END

    // BITLOOP_TEMPLATE_AI_BEGIN
    // ─────── AI hooks ───────
    #ifdef BITLOOP_DEV_MODE
    void traceFrame(SimTraceFrame& frame) const override;
    bool captureTraceSnapshot(std::string_view route, SimSnapshotImage& image) const override;
    bool saveCheckpoint(SimCheckpointWriter& writer) const override;
    bool loadCheckpoint(const SimCheckpointReader& reader) override;
    #endif
    // BITLOOP_TEMPLATE_AI_END
};

struct {SIM_NAME}_Project : public Project<{SIM_NAME}_Project>
{
    static ProjectInfo info()
    {
        // BITLOOP_TEMPLATE_GUIDE_BEGIN
        // Categorize your project.
        // BITLOOP_TEMPLATE_GUIDE_END
        return ProjectInfo({ "New Projects", "{SIM_NAME}" });
    }

    struct UI : BufferedInterfaceModel
    {
        using BufferedInterfaceModel::BufferedInterfaceModel;
        void sidebar();
        // void overlay();
    };

    int viewport_count = 1;

    void projectPrepare(Layout& layout) override;
};

SIM_END;
