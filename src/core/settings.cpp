#include <bitloop/core/settings.h>
#include <bitloop/core/main_window.h>

#include <SDL3/SDL_dialog.h>

BL_BEGIN_NS;

#ifndef BL_WEB_BUILD
static void on_folder_chosen([[maybe_unused]] void* userdata, const char* const* filelist, [[maybe_unused]] int filter)
{
    if (!filelist) { SDL_Log("Dialog error"); return; }
    if (!*filelist) { SDL_Log("User canceled"); return; }
    blPrint() << "Chosen folder: " << filelist[0]; // first (and only) entry for folder dialog
}

static inline double lerp_log(double a, double b, double t) {
    return std::exp(std::log(a) + t * (std::log(b) - std::log(a)));
}

static inline bool is_x265(CaptureFormat format)
{
    #if BITLOOP_FFMPEG_X265_ENABLED
    return (format == CaptureFormat::x265);
    #else
    (void)format;
    return false;
    #endif
}

// Range depends ONLY on resolution, fps, codec
static inline BitrateRange recommended_bitrate_range_mbps(IVec2 res, int fps, CaptureFormat format)
{
    int w = std::max(16, res.x);
    int h = std::max(16, res.y);
    fps = std::clamp(fps, 1, 120);

    // Broad coverage band for simple->extreme screen content
    // (bppf = bits per pixel per frame)
    const double bppf_min = 0.006; // very simple scenes
    const double bppf_max = 0.70;  // near-lossless-ish, extreme detail/motion

    // Codec efficiency: x265 typically ~30% less for same quality
    const double eff = is_x265(format) ? 0.70 : 1.00;

    const double ppf = (double)w * (double)h * (double)fps;
    double min_bps = ppf * bppf_min * eff;
    double max_bps = ppf * bppf_max * eff;

    BitrateRange r;
    r.min_mbps = std::clamp(min_bps / 1e6, 0.05, 1000.0);
    r.max_mbps = std::clamp(max_bps / 1e6, r.min_mbps, 1000.0);
    return r;
}

static inline double choose_bitrate_mbps_from_range(const BitrateRange& r, int quality_0_to_100)
{
    int q = std::clamp(quality_0_to_100, 0, 100);
    double t = q / 100.0; // 0..1
    return lerp_log(r.min_mbps, r.max_mbps, t); // same curve/endpoints as before
}
#endif

void SettingsConfig::updateRecordBitrate()
{
    #if BITLOOP_FFMPEG_ENABLED
    record_bitrate_mbps_range = recommended_bitrate_range_mbps(getRecordResolution(), record_fps, getRecordFormat());
    record_bitrate = (int64_t)(1000000.0 * choose_bitrate_mbps_from_range(record_bitrate_mbps_range, record_quality));
    #endif
}

void SettingsPanel::init()
{
    config.updateRecordBitrate();

    SnapshotPresetList& presets = main_window->captureContext().presetManager().allPresets();
    presets.updateSupportedSizes(config.default_ssaa);

    CapturePreset* selected_preset = presets.at_safe(selected_capture_preset);
    setInputPreset(selected_preset);
}

void SettingsPanel::populateCapturePresetsEditor()
{
    bool is_capturing = main_window->captureContext().manager().isCapturing();
    if (is_capturing)
        ImGui::BeginDisabled();

    SnapshotPresetList& presets = main_window->captureContext().presetManager().allPresets();

    CapturePreset* selected_preset = presets.at_safe(selected_capture_preset);

    if (ImGui::Button("New target"))
    {
        CapturePreset new_preset;
        new_preset.setName("unnamed");
        new_preset.setResolution(1920, 1080);

        presets.add(new_preset);

        selected_capture_preset = presets.size() - 1;
        selected_preset = presets.at_safe(selected_capture_preset);
        setInputPreset(selected_preset);
    }

    if (selected_preset)
    {
        ImGui::SameLine();
        if (ImGui::Button("Duplicate target"))
        {
            CapturePreset new_preset(*selected_preset);
            new_preset.setAlias(std::string(new_preset.getAlias()) + "_clone");
            presets.add(new_preset);

            selected_capture_preset = presets.size() - 1;
            selected_preset = presets.at_safe(selected_capture_preset);
            setInputPreset(selected_preset);
        }
    }

    int changed_selected_index = populateCapturePresetsList<CapturePresetsSelectMode::NONE, false>([&](int i) -> CapturePreset& {
        return presets[i];
    }, (int)presets.size(), nullptr, selected_capture_preset);

    if (changed_selected_index >= 0)
    {
        selected_preset = presets.at_safe(selected_capture_preset);
        setInputPreset(selected_preset);
    }

    if (selected_preset)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTable("##preset_table", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            // don't allow modifying default viewport preset
            bool is_viewport_preset = selected_preset->isViewportPreset();
            if (is_viewport_preset)
                ImGui::BeginDisabled();

            // Alias (only permit unique, otherwise revert)
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Alias");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);

            auto FilterLowerNumUnderscore = [](ImGuiInputTextCallbackData* data)
            {
                if (data->EventFlag != ImGuiInputTextFlags_CallbackCharFilter) return 0;
                ImWchar c = data->EventChar;
                if (c >= 'A' && c <= 'Z') { data->EventChar = (ImWchar)(c - 'A' + 'a'); return 0; } // convert to lowercase
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') return 0; // allow: a-z, 0-9, underscore
                return 1; // Reject everything else
            };

            if (ImGui::InputText("##preset_alias", input_alias, selected_preset->maxAliasSize(),
                ImGuiInputTextFlags_CallbackCharFilter, FilterLowerNumUnderscore))
            {
                if (presets.isUniqueAliasChange(selected_capture_preset, input_alias))
                {
                    selected_preset->setAlias(input_alias);
                    presets.updateLookup();
                }
                else // revert
                {
                    strcpy(input_alias, revert_alias);
                }
            }

            ImGui::SetItemTooltip("Unique alias used for ID and filenames");

            // Name
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Description");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##preset_name", input_name, selected_preset->maxNameSize()))
            {
                selected_preset->setName(input_alias);
            }

            bool updated_render_size = false;

            // Width
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Width");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (is_viewport_preset)
            {
                int w = selected_preset->width();
                ImGui::InputInt("##res_x", &w, 10);
            }
            else
            {
                if (ImGui::InputInt("##res_x", &input_resolution.x, 10))
                {
                    input_resolution.x = std::clamp(input_resolution.x, 16, 16384);
                    input_resolution.x = (input_resolution.x & ~1); // force even
                    selected_preset->setResolution(input_resolution);
                    updated_render_size = true;
                }
            }

            // Height
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Height");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (is_viewport_preset)
            {
                int h = selected_preset->height();
                ImGui::InputInt("##res_y", &h, 10);
            }
            else
            {
                if (ImGui::InputInt("##res_y", &input_resolution.y, 10))
                {
                    input_resolution.y = std::clamp(input_resolution.y, 16, 16384);
                    input_resolution.y = (input_resolution.y & ~1); // force even
                    selected_preset->setResolution(input_resolution);
                    updated_render_size = true;
                }
            }

            // Supersampling
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            bool had_specific_ssaa = input_ssaa > 0;
            bool use_specific_ssaa = had_specific_ssaa;
            ImGui::Checkbox("Supersample", &use_specific_ssaa);

            if (!had_specific_ssaa && use_specific_ssaa)
            {
                input_ssaa = 1; // enable
                selected_preset->setSSAA(input_ssaa);
                updated_render_size = true;
            }
            else if (had_specific_ssaa && !use_specific_ssaa)
            {
                input_ssaa = 0; // disable
                selected_preset->setSSAA(input_ssaa);
                updated_render_size = true;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (!use_specific_ssaa) ImGui::BeginDisabled();
            if (ImGui::SliderInt("##ssaa",
                use_specific_ssaa ? &input_ssaa : &config.default_ssaa,
                1, 8,
                use_specific_ssaa ? "%dx" : "Using global (%dx)"))
            {
                selected_preset->setSSAA(input_ssaa);
                updated_render_size = true;
            }

            if (!use_specific_ssaa) ImGui::EndDisabled();

            if (updated_render_size)
                selected_preset->updateSupportedSize(platform()->glCaps(), config.default_ssaa);

            // Sharpening
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            bool had_specific_sharpen = input_sharpen >= 0.0;
            bool use_specific_sharpen = had_specific_sharpen;
            ImGui::Checkbox("Sharpening", &use_specific_sharpen);

            if (!had_specific_sharpen && use_specific_sharpen)
            {
                input_sharpen = 0.0f; // enable (non-negative)
                selected_preset->setSharpening(input_sharpen);
            }
            else if (had_specific_sharpen && !use_specific_sharpen)
            {
                input_sharpen = -1.0f; // disable (negative)
                selected_preset->setSharpening(input_sharpen);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (!use_specific_sharpen) ImGui::BeginDisabled();

            if (ImGui::SliderFloat("##sharpen",
                use_specific_sharpen ? &input_sharpen : &config.default_sharpen,
                0.0f, 1.0f,
                use_specific_sharpen ? "%.2f" : "Using global (%.2f)"))
            {
                selected_preset->setSharpening(input_sharpen);
            }

            if (is_viewport_preset)
                ImGui::EndDisabled();

            if (!use_specific_sharpen) ImGui::EndDisabled();
            ImGui::EndTable();
        }
    }

    if (is_capturing)
        ImGui::EndDisabled();
}

inline bool CaptureFormatVideoCombo(const char* id, CaptureFormatVideo& value)
{
    const char* preview = "Unknown";
    CaptureFormatName((CaptureFormat)value, &preview);

    bool changed = false;

    if (ImGui::BeginCombo(id, preview))
    {
        for (CaptureFormatVideo opt : CaptureFormatVideoOptions())
        {
            const bool selected = (opt == value);
            const char* label = "Unknown";
            CaptureFormatName((CaptureFormat)opt, &label);

            if (ImGui::Selectable(label, selected))
            {
                value = opt;
                changed = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    return changed;
}

void SettingsPanel::populateSettings()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scale_size(6, 6));

    bool capturing = main_window->captureContext().manager().isCapturing();
    if (capturing)
        ImGui::BeginDisabled();

    // Paths
    #ifndef BL_WEB_BUILD
    ImGui::BeginLabelledBox("Paths");
    ImGui::Text("Media Output Directory:");
    ImGui::InputText("##folder", &config.capture_dir, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_ElideLeft);
    ImGui::SameLine();
    if (ImGui::Button("..."))
    {
        SDL_ShowOpenFolderDialog(on_folder_chosen, NULL, platform()->sdlWindow(), config.capture_dir.c_str(), false);
    }
    ImGui::EndLabelledBox();
    #endif

    // Global Options
    {
        ImGui::BeginLabelledBox("Global Capture Options");

        if (!platform()->isMobile())
        {
            ImGui::Checkbox("Fixed frame time-delta", &config.fixed_time_delta);
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("FPS:");
        if (ImGui::InputInt("##fps", &config.record_fps, 1))
        {
            config.record_fps = std::clamp(config.record_fps, 1, 100);
            config.updateRecordBitrate();
        }
        ImGui::Checkbox("Show FPS in toolbar", &config.show_fps);
        ImGui::Checkbox("Fill viewport", &config.fill_viewport);
        ImGui::EndLabelledBox();
    }


    // Capture Presets
    {
        SnapshotPresetList& presets = main_window->captureContext().presetManager().allPresets();

        ImGui::BeginLabelledBox("Capture Presets");

        ImGui::Text("Default SSAA (Supersampling Factor)");
        if (ImGui::SliderInt("##ssaa", &config.default_ssaa, 1, 8, "%dx"))
        {
            presets.updateSupportedSizes(config.default_ssaa);

            for (int i = 0; i < presets.size(); i++)
            {
                if (!presets[i].isSupportedSize())
                {
                    if (i == selected_image_preset)
                        selected_image_preset = 0;
                    if (i == config.target_video_preset)
                        config.target_video_preset = 0;
                }
            }
        }

        ImGui::Spacing(); ImGui::Spacing();
        ImGui::Text("Default Sharpen");
        ImGui::SliderFloat("##sharpen", &config.default_sharpen, 0.0f, 1.0f, "%.2f");

        ImGui::Spacing(); ImGui::Spacing();

        ImGui::TextUnformatted("Select a preset to preview");

        if (ImGui::BeginTabBar("capture_tabs"))
        {
            if (ImGui::TabBox("Image"))
            {
                ImGui::TextUnformatted("Check all presets to include in snapshot batch");

                selected_preset_type = SelectedPresetType::IMAGE_PRESET;

                populateCapturePresetsList<CapturePresetsSelectMode::MULTI>(
                    [&](int i) -> CapturePreset& {
                    return presets[i];
                }, (int)presets.size(), &config.target_image_presets, selected_image_preset);

                ImGui::EndTabBox();
            }

            if (ImGui::TabBox("Video"))
            {
                selected_preset_type = SelectedPresetType::VIDEO_PRESET;

                // Use target_video_preset as both selected item index and chosen radio button index
                int new_index = populateCapturePresetsList<CapturePresetsSelectMode::SINGLE>(
                    [&](int i) -> CapturePreset& {
                    return presets[i];
                }, (int)presets.size(), nullptr, config.target_video_preset);

                if (new_index >= 0)
                {
                    config.updateRecordBitrate();
                }

                ImGui::Spacing(); 

                ImGui::Text("Codec:");
                if (CaptureFormatVideoCombo("##codec", config.record_format))
                {
                    config.updateRecordBitrate();
                }

                ImGui::Spacing();
                ImGui::Spacing();
                if (config.record_format == CaptureFormatVideo::WEBP_VIDEO)
                {
                    ImGui::Text("Compression mode:");
                    int mode = (int)config.record_webp_mode;
                    if (ImGui::Combo("##webp_mode", &mode, "Lossless\0Near-lossless\0Lossy\0"))
                        config.record_webp_mode = (WebPCompressionMode)mode;

                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (config.record_webp_mode == WebPCompressionMode::Lossy)
                    {
                        ImGui::Text("Image quality:");
                        ImGui::SliderInt("##quality", &config.record_quality, 0, 100);
                        ImGui::SetItemTooltip("Higher = better image quality, usually larger files.\nEven 100 is lossy.");
                    }
                    else
                    {
                        ImGui::Text("Compression effort:");
                        ImGui::SliderInt("##webp_effort", &config.record_webp_effort, 0, 100);
                        ImGui::SetItemTooltip("Higher = slower encoding, usually smaller files.\nControls compression effort, not image fidelity.");

                        if (config.record_webp_mode == WebPCompressionMode::NearLossless)
                        {
                            ImGui::Spacing();
                            ImGui::Spacing();
                            ImGui::Text("Pixel fidelity:");
                            ImGui::SliderInt("##near_lossless", &config.record_near_lossless, 0, 100);
                            ImGui::SetItemTooltip("100 = unchanged pixels.\nLower = more pixel changes to reduce file size.\nThis is not a percentage of image quality retained.");
                        }
                        else
                        {
                            ImGui::TextDisabled("Preserves captured pixels exactly.");
                        }
                    }
                }
                else
                {
                    ImGui::Text("Quality:");
                    if (ImGui::SliderInt("##quality", &config.record_quality, 0, 100))
                        config.updateRecordBitrate();

                    #if BITLOOP_FFMPEG_ENABLED
                    ImGui::Text("= %.1f Mbps", (double)config.record_bitrate / 1000000.0);
                    #endif
                }

                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Text("Frame Count (0 = No Limit):");
                if (ImGui::InputInt("##frame_count", &config.record_frame_count, 1))
                {
                    config.record_frame_count = std::clamp(config.record_frame_count, 0, 10000000);
                }

                ImGui::EndTabBox();
            }

            if (ImGui::TabBox("Edit Presets"))
            {
                // don't preview while editing
                //selected_preset_type = SelectedPresetType::EDIT_PRESET;

                populateCapturePresetsEditor();

                if (!presets[selected_image_preset].isSupportedSize())
                    selected_image_preset = 0;
                if (!presets[config.target_video_preset].isSupportedSize())
                    config.target_video_preset = 0;

                ImGui::EndTabBox();
            }

            ImGui::EndTabBar();
        }

        ImGui::EndLabelledBox();
    }
    
    ImGui::PopStyleVar();

    if (capturing)
        ImGui::EndDisabled();

    ImGui::Text("Compiled:  %s %s", __DATE__, __TIME__);
}

BL_END_NS;
