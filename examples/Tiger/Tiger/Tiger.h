#pragma once
#include <bitloop.h>

SIM_BEG;

using namespace bl;

struct Tiger_Scene : public Scene<Tiger_Scene>
{
    bool transform_coordinates = true;
    bool scale_lines = true;
    bool scale_sizes = true;
    bool rotate_text = true;

    CameraInfo       camera;
    CameraNavigator  navigator;

    /// ─────── Provide default Scene launch config ─────── 
    struct Config { 
        // double speed = 10;
    };

    Tiger_Scene(Config& info [[maybe_unused]])
        // : speed(info.speed)  /// Initialize your Scene variables below from config
    {}

    struct UI : BufferedInterfaceModel
    {
        using BufferedInterfaceModel::BufferedInterfaceModel;
        void sidebar();
    };

    /// ─────── Scene variables ─────── 
    // double speed;

    /// ─────── Your custom methods ───────

    /// ─────── Scene methods ─────── 
    void sceneStart() override;
    void sceneMounted(Viewport* viewport) override;
    void sceneDestroy() override;
    void sceneProcess(double dt) override;

    /// ─────── Viewport methods ─────── 
    void viewportProcess(Viewport* ctx, double dt) override;
    void viewportDraw(Viewport* ctx) const override;

    /// ─────── Input handling ─────── 
    void onEvent(Event e) override;
};


struct Tiger_Project : public Project<Tiger_Project>
{
    static ProjectInfo info() {
        return ProjectInfo({ "Tests", "Draw Tiger (vector graphics)" });
    }

    struct UI : BufferedInterfaceModel
    {
        using BufferedInterfaceModel::BufferedInterfaceModel;
        void sidebar();
    };

    int viewport_count = 1;

    void projectPrepare(Layout& layout) override;
};

SIM_END;
