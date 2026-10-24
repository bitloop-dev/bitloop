#pragma once

#include <bitloop/core/capture_manager.h>
#include <bitloop/core/snapshot_presets.h>

BL_BEGIN_NS;

enum class WebPCompressionMode
{
    Lossless,
    NearLossless,
    Lossy
};

// Saveable settings
struct SettingsConfig
{
    SnapshotPresetManager snapshot_preset_manager;

    std::string capture_dir;

    bool    fixed_time_delta = false;

    int     default_ssaa = 1;
    float   default_sharpen = 0.0f;

    CaptureFormatSnapshot snapshot_format = CaptureFormatSnapshot::WEBP_SNAPSHOT; // CaptureFormat
    CaptureFormatVideo    record_format =
    #if BITLOOP_FFMPEG_ENABLED
        CaptureFormatVideo::x264;
    #else
        CaptureFormatVideo::WEBP_VIDEO;
    #endif

    int     record_fps = 60;
    int     record_frame_count = 0;
    int     record_quality = 100;
    WebPCompressionMode record_webp_mode = WebPCompressionMode::Lossless;
    int     record_webp_effort = 100;
    int     record_near_lossless = 80;

    bool    show_fps = false;
    bool    fill_viewport = false;

    SnapshotPresetHashMap target_image_presets;
    int target_video_preset = 0; // use the selected video preset as the target preset
    
    #if BITLOOP_FFMPEG_ENABLED
    int64_t        record_bitrate = 128000000ll;
    BitrateRange   record_bitrate_mbps_range{ 1, 1000 };
    #endif

    SettingsConfig()
    {
        const SnapshotPresetList& capture_presets = snapshot_preset_manager.allPresets();
        target_image_presets[capture_presets["viewport"]->hashedAlias()] = true;
        //target_image_presets[capture_presets["fhd"]->hashedAlias()] = true;
    }

    SnapshotPresetList enabledImagePresets() const
    {
        const SnapshotPresetList& capture_presets = snapshot_preset_manager.allPresets();
        SnapshotPresetList ret;
        for (const auto& p : capture_presets)
        {
            if (target_image_presets.count(p.hashedAlias()) > 0)
                ret.add(p);
        }
        ret.updateLookup();
        return ret;
    }

    CapturePreset enabledVideoPreset() const
    {
        const SnapshotPresetList& capture_presets = snapshot_preset_manager.allPresets();
        return capture_presets.at(target_video_preset);
    }

    CaptureFormat getRecordFormat() const { return (CaptureFormat)record_format; }
    CaptureFormat getSnapshotFormat() const { return (CaptureFormat)snapshot_format; }
    IVec2         getRecordResolution() const { return enabledVideoPreset().getResolution(); }

    void updateRecordBitrate();
};

BL_END_NS
