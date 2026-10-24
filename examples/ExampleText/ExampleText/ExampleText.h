#pragma once
#include <bitloop.h>

SIM_BEG;

using namespace bl;

struct ExampleText_Scene : public Scene<ExampleText_Scene>
{
    /// ─────── Default Scene launch config ─────── 
    struct Config {};
    ExampleText_Scene(Config& info [[maybe_unused]]) {}

    struct UI : BufferedInterfaceModel {
        using BufferedInterfaceModel::BufferedInterfaceModel;
        void sidebar();
    };

    /// ─────── Scene variables ───────
    std::vector<NanoFont> fonts;

    CameraInfo      camera;
    CameraNavigator navigator;

    struct Opts
    {
        bool transform_coordinates = true;
        bool scale_lines = true;
        bool scale_sizes = true;
        bool scale_text = true;
        bool rotate_text = true;
        double font_size = 32;
    } opts;

    /// ─────── Scene methods ───────
    void sceneStart() override;
    void sceneMounted(Viewport* viewport) override;
    void sceneDestroy() override;
    void sceneProcess(double dt) override;

    /// ─────── Viewport methods ─────── 
    void viewportProcess(Viewport* ctx, double dt) override;
    void viewportDraw(Viewport* ctx) const override;

    /// ─────── Input handling ─────── 
    void onEvent(Event e) override; // All event types

    /// Filters for specific input events
    //void onPointerDown(PointerEvent e) override;
    //void onPointerUp(PointerEvent e) override;
    //void onPointerMove(PointerEvent e) override;
    //void onWheel(PointerEvent e) override;
    //void onKeyDown(KeyEvent e) override;
    //void onKeyUp(KeyEvent e) override;

    /// ─────── Your custom methods ───────
    // void customMethod();
};

struct ExampleText_Project : public Project<ExampleText_Project>
{
    static ProjectInfo info() {
        return ProjectInfo({ "Tests", "Draw Text" });
    }

    struct UI : BufferedInterfaceModel {
        using BufferedInterfaceModel::BufferedInterfaceModel;
        void sidebar();
    };

    int viewport_count = 1;

    void projectPrepare(Layout& layout) override;
};

SIM_END;
