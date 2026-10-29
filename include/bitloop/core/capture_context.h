#pragma once

#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <bitloop/core/capture_manager.h>
#include <bitloop/core/settings_config.h>

BL_BEGIN_NS;

class MainWindow;

enum struct CaptureCommandType
{
    BEGIN_SNAPSHOT_PRESET_LIST,
    BEGIN_RECORDING,
    END_RECORDING
};

struct SnapshotPresetsArgs
{
    std::string rel_path_fmt;
    SnapshotPresetList presets;
    int request_id = 0;
};

struct CaptureCommandEvent
{
    CaptureCommandType type;
    SnapshotPresetsArgs snapshot_args;
};

class CaptureContext
{
    friend class MainWindow;

    std::mutex command_mutex;
    std::vector<CaptureCommandEvent> command_queue;

    SettingsConfig settings_config;
    CaptureManager capture_manager;

    SnapshotPresetList enabled_capture_presets;
    int enabled_capture_presets_current_idx = 0;

    bool is_snapshotting_list = false;
    int active_snapshot_preset_request_id = 0;
    std::string active_capture_rel_path_fmt;
    int shared_batch_fileindex = 0;

    bool new_frame_prepared = false;
    bool permit_frame_capture = false;
    bool captured_last_frame = false;

    const CapturePreset* active_preset = nullptr;
    int active_ssaa = 1;
    float active_sharpen = 0.0f;
    IVec2 active_canvas_size{};
    IVec2 active_target_size{};

    bool changed_size = true;
    bool changed_scale = true;
    bool changed_sharpening = false;
    float old_sharpen = 0.0f;

    CapturePreprocessor preprocessor;
    EncodeFrame preprocessed_frame;
    std::optional<SnapshotFrame> prepared_snapshot; // GUI-owned; delivered from the worker queue.
    bool use_preprocessor_texture = false;

public:
    [[nodiscard]] SettingsConfig& settings() { return settings_config; }
    [[nodiscard]] const SettingsConfig& settings() const { return settings_config; }

    [[nodiscard]] SnapshotPresetManager& presetManager() { return settings_config.snapshot_preset_manager; }
    [[nodiscard]] const SnapshotPresetManager& presetManager() const { return settings_config.snapshot_preset_manager; }

    [[nodiscard]] CaptureManager& manager() { return capture_manager; }
    [[nodiscard]] const CaptureManager& manager() const { return capture_manager; }

    [[nodiscard]] bool isSnapshotting() const { return is_snapshotting_list; }
    [[nodiscard]] bool isRecording() const { return capture_manager.isRecording(); }
    [[nodiscard]] bool isCapturing() const { return capture_manager.isCapturing(); }

    void captureFrame(bool b) { permit_frame_capture = b; }
    [[nodiscard]] bool capturingNextFrame() const { return permit_frame_capture; }
    [[nodiscard]] bool capturedLastFrame() const { return captured_last_frame; }
    [[nodiscard]] int capturedFrameCount() const { return capture_manager.frameCount(); }

    [[nodiscard]] int fps() const { return settings_config.record_fps; }
    void setFixedFrameTimeDelta(bool b) { settings_config.fixed_time_delta = b; }
    [[nodiscard]] bool isFixedFrameTimeDelta() const { return settings_config.fixed_time_delta; }

    void queueBeginSnapshot(
        const SnapshotPresetList& presets,
        std::string_view rel_path_fmt = {},
        int request_id = 0)
    {
        SnapshotPresetsArgs args;
        args.rel_path_fmt = rel_path_fmt;
        args.presets = presets;
        args.request_id = request_id;

        std::lock_guard<std::mutex> lock(command_mutex);
        command_queue.push_back({ CaptureCommandType::BEGIN_SNAPSHOT_PRESET_LIST, std::move(args) });
    }

    void queueBeginRecording()
    {
        std::lock_guard<std::mutex> lock(command_mutex);
        command_queue.push_back({ CaptureCommandType::BEGIN_RECORDING, {} });
    }

    void queueEndRecording()
    {
        std::lock_guard<std::mutex> lock(command_mutex);
        command_queue.push_back({ CaptureCommandType::END_RECORDING, {} });
    }

    [[nodiscard]] std::vector<CaptureCommandEvent> takeQueuedCommands()
    {
        std::lock_guard<std::mutex> lock(command_mutex);
        return std::move(command_queue);
    }
};

BL_END_NS
