#include <bitloop/core/snapshot_presets.h>
#include <bitloop/platform/platform.h>

BL_BEGIN_NS

bool CapturePreset::updateSupportedSize(const GLCaps& caps, int default_ssaa) const
{
    int full_w = width() * (ssaa > 0 ? ssaa : default_ssaa);
    int full_h = height() * (ssaa > 0 ? ssaa : default_ssaa);
    is_supported_size = (full_w <= caps.max_viewport_w) && (full_h <= caps.max_viewport_h);
    return is_supported_size;
}

void SnapshotPresetList::updateSupportedSizes(int default_ssaa) const
{
    GLCaps caps = platform()->glCaps();
    for (auto& item : items)
        item.updateSupportedSize(caps, default_ssaa);
}

BL_END_NS
