#include <bitloop/imguix/imguix.h>
#include <bitloop/util/math_util.h>

#include <cstring>

std::ostream& operator<<(std::ostream& os, const ImSpline::Spline& spline)
{
    (void)spline;
    return os << "Spline";
}

std::ostream& operator<<(std::ostream& os, const ImGradient& gradient)
{
    (void)gradient;
    return os << "Gradient";
}

// fdd replacements

struct ParsedPrintf {
    bool has_width = false;
    bool width_from_arg = false;
    int  width = 0;

    bool has_precision = false;
    bool precision_from_arg = false; // true for "%.*f"
    int  precision = 0;              // valid if has_precision && !precision_from_arg

    char spec = '\0';                // conversion specifier ('f','g','e','a', etc.)
};

static const char* ParsePrintfSpec(const char* s, ParsedPrintf& out)
{
    if (!s || *s != '%') return nullptr;
    const char* p = s + 1;

    // 1) Flags
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;

    // 2) Width
    if (*p == '*') { out.has_width = true; out.width_from_arg = true; ++p; }
    else if (std::isdigit((unsigned char)*p)) {
        out.has_width = true;
        long w = 0;
        while (std::isdigit((unsigned char)*p)) {
            w = w * 10 + (*p - '0');
            if (w > INT_MAX) { w = INT_MAX; }
            ++p;
        }
        out.width = (int)w;
    }

    // 3) Precision
    if (*p == '.') {
        ++p;
        out.has_precision = true;
        if (*p == '*') { out.precision_from_arg = true; ++p; }
        else {
            long pr = 0;
            bool any = false;
            while (std::isdigit((unsigned char)*p)) {
                any = true;
                pr = pr * 10 + (*p - '0');
                if (pr > INT_MAX) { pr = INT_MAX; }
                ++p;
            }
            // printf rule: "." with no digits -> precision = 0
            out.precision = any ? (int)pr : 0;
        }
    }

    // 4) Length modifiers (skip)
    if (*p == 'h') { ++p; if (*p == 'h') ++p; }
    else if (*p == 'l') { ++p; if (*p == 'l') ++p; }
    else if (*p == 'L' || *p == 'z' || *p == 't' || *p == 'j') { ++p; }

    // 5) Conversion spec
    if (*p == '\0') return nullptr;
    out.spec = *p++;
    return p;
}

static bool FindAndParseNextSpec(const char* fmt, ParsedPrintf& out)
{
    if (!fmt) return false;
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') continue;
        if (p[1] == '%') { ++p; continue; } // "%%" escape
        const char* after = ParsePrintfSpec(p, out);
        return after != nullptr;
    }
    return false;
}

static int ImParseFormatPrecision128(const char* fmt, int default_precision, bool* from_arg = nullptr)
{
    ParsedPrintf pf;
    if (!FindAndParseNextSpec(fmt, pf)) {
        if (from_arg) *from_arg = false;
        return default_precision;
    }
    if (!pf.has_precision) {
        if (from_arg) *from_arg = false;
        return default_precision;
    }
    if (pf.precision_from_arg) {
        if (from_arg) *from_arg = true;
        return default_precision; // caller should read it from variadic args
    }
    if (from_arg) *from_arg = false;
    return pf.precision; // un-capped; up to INT_MAX
}

template<typename T>
static inline T bl_round_to_decimals(int p, T v)
{
    if (p <= 0) return (T)(int)(v + (v >= (T)0.0 ? (T)0.5 : (T)-0.5));
    const T s = (T)bl::pow(10, p);
    return (T)((int)(v * s + (v >= (T)0.0 ? (T)0.5 : (T)-0.5))) / s;
}


// ---- helpers for SliderDouble_InvLog ----------------------------------------------------

    // --- Logarithmic "toward max" helpers ---------------------------------------
template<typename TYPE, typename SIGNEDTYPE, typename FLOATTYPE>
static inline float ScaleRatioFromValue_TowardMax(ImGuiDataType, TYPE v, TYPE v_min, TYPE v_max, float epsilon)
{
    if (v_min == v_max)
        return 0.0f;

    // Keep orientation info, but operate with (v1 <= v2) internally.
    bool flipped = (v_max < v_min);
    if (flipped)
        ImSwap(v_min, v_max);

    const TYPE v_clamped = ImClamp(v, v_min, v_max);

    // u = distance-to-max, range [0 .. Umax]
    const FLOATTYPE Umax = (FLOATTYPE)(v_max - v_min);
    const FLOATTYPE u = (FLOATTYPE)v_max - (FLOATTYPE)v_clamped;

    // Avoid degenerate log bases
    const FLOATTYPE u_min_f = (FLOATTYPE)epsilon;                     // avoid log(0)
    const FLOATTYPE u_max_f = (Umax < (FLOATTYPE)epsilon) ? (FLOATTYPE)epsilon : Umax;

    float t_u;
    if (u <= u_min_f)              // exactly at max (or inside epsilon)
        t_u = 0.0f;
    else if (u >= u_max_f)         // exactly at min (or beyond)
        t_u = 1.0f;
    else
        t_u = (float)(ImLog(u / u_min_f) / ImLog(u_max_f / u_min_f));

    // Convert back: small u (near max) -> t near 1
    float t = 1.0f - t_u;

    // Restore original orientation if the user gave a reversed range
    return flipped ? (1.0f - t) : t;
}

template<typename TYPE, typename SIGNEDTYPE, typename FLOATTYPE>
static inline TYPE ScaleValueFromRatio_TowardMax(ImGuiDataType data_type, float t, TYPE v_min, TYPE v_max, float epsilon)
{
    // Clamp extremes explicitly (avoids epsilon effects at ends)
    if (t <= 0.0f || v_min == v_max)
        return v_min;
    if (t >= 1.0f)
        return v_max;

    bool flipped = (v_max < v_min);
    if (flipped)
        ImSwap(v_min, v_max);

    // t_with_flip maintains the caller's orientation
    float t_with_flip = flipped ? (1.0f - t) : t;

    // Work in u-space (distance-to-max). We want fine control near v_max,
    // so apply standard log in u, then convert back v = v_max - u.
    const FLOATTYPE Umax = (FLOATTYPE)(v_max - v_min);
    const FLOATTYPE u_min_f = (FLOATTYPE)epsilon;
    const FLOATTYPE u_max_f = (Umax < (FLOATTYPE)epsilon) ? (FLOATTYPE)epsilon : Umax;

    // t_u grows from 0 at u_min_f (near v_max) to 1 at u_max_f (near v_min)
    const float t_u = 1.0f - t_with_flip;

    // u = epsilon * pow(Umax/epsilon, t_u)
    const FLOATTYPE u = (FLOATTYPE)(u_min_f * ImPow(u_max_f / u_min_f, (FLOATTYPE)t_u));

    FLOATTYPE v_f = (FLOATTYPE)v_max - u;

    // Restore orientation
    TYPE v_out = (TYPE)v_f;
    if (flipped)
    {
        // When flipped, v_min/v_max were swapped; no extra transform needed here
        // because t_with_flip already handled orientation.
    }

    // For integer types, mirror the behavior of imgui's rounding in linear mode:
    const bool is_floating_point = (data_type == ImGuiDataType_Float) || (data_type == ImGuiDataType_Double);
    if (!is_floating_point)
    {
        // Match integer semantics: nearest integer within range.
        if (v_out < v_min) v_out = v_min;
        if (v_out > v_max) v_out = v_max;
    }
    return v_out;
}

// --- Slider behavior using the "toward max" mapping --------------------------
template<typename TYPE, typename SIGNEDTYPE, typename FLOATTYPE>
bool SliderBehaviorT_TowardMax(const ImRect& bb, ImGuiID id, ImGuiDataType data_type,
    TYPE* v, const TYPE v_min, const TYPE v_max,
    const char* format, ImGuiSliderFlags flags, ImRect* out_grab_bb)
{
    using namespace ImGui;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;
    const bool is_floating_point = (data_type == ImGuiDataType_Float) || (data_type == ImGuiDataType_Double);
    const float v_range_f = (float)(v_min < v_max ? v_max - v_min : v_min - v_max);

    // Geometry
    const float grab_padding = 2.0f;
    const float slider_sz = (bb.Max[axis] - bb.Min[axis]) - grab_padding * 2.0f;
    float grab_sz = style.GrabMinSize;
    if (!is_floating_point && v_range_f >= 0.0f)
        grab_sz = ImMax(slider_sz / (v_range_f + 1), style.GrabMinSize);
    grab_sz = ImMin(grab_sz, slider_sz);
    const float slider_usable_sz = slider_sz - grab_sz;
    const float slider_usable_pos_min = bb.Min[axis] + grab_padding + grab_sz * 0.5f;
    const float slider_usable_pos_max = bb.Max[axis] - grab_padding - grab_sz * 0.5f;

    // Epsilon picked from the display precision (like imgui's log)
    const int decimal_precision = is_floating_point ? ImParseFormatPrecision128(format, 3) : 1;
    const float epsilon = ImPow(0.1f, (float)decimal_precision);

    bool value_changed = false;

    if (g.ActiveId == id)
    {
        bool set_new_value = false;
        float clicked_t = 0.0f;

        if (g.ActiveIdSource == ImGuiInputSource_Mouse)
        {
            if (!g.IO.MouseDown[0])
            {
                ClearActiveID();
            }
            else
            {
                const float mouse_abs_pos = g.IO.MousePos[axis];
                if (g.ActiveIdIsJustActivated)
                {
                    float grab_t = ScaleRatioFromValue_TowardMax<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, *v, v_min, v_max, epsilon);
                    if (axis == ImGuiAxis_Y)
                        grab_t = 1.0f - grab_t;
                    const float grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, grab_t);
                    const bool clicked_around_grab = (mouse_abs_pos >= grab_pos - grab_sz * 0.5f - 1.0f) && (mouse_abs_pos <= grab_pos + grab_sz * 0.5f + 1.0f);
                    g.SliderGrabClickOffset = (clicked_around_grab && is_floating_point) ? mouse_abs_pos - grab_pos : 0.0f;
                }
                if (slider_usable_sz > 0.0f)
                    clicked_t = ImSaturate((mouse_abs_pos - g.SliderGrabClickOffset - slider_usable_pos_min) / slider_usable_sz);
                if (axis == ImGuiAxis_Y)
                    clicked_t = 1.0f - clicked_t;
                set_new_value = true;
            }
        }
        else if (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
        {
            if (g.ActiveIdIsJustActivated)
            {
                g.SliderCurrentAccum = 0.0f;
                g.SliderCurrentAccumDirty = false;
            }

            float input_delta = (axis == ImGuiAxis_X) ? GetNavTweakPressedAmount(axis) : -GetNavTweakPressedAmount(axis);
            if (input_delta != 0.0f)
            {
                const bool tweak_slow = IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakSlow : ImGuiKey_NavKeyboardTweakSlow);
                const bool tweak_fast = IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakFast : ImGuiKey_NavKeyboardTweakFast);
                if (decimal_precision > 0)
                {
                    input_delta /= 100.0f;
                    if (tweak_slow)
                        input_delta /= 10.0f;
                }
                else
                {
                    if ((v_range_f >= -100.0f && v_range_f <= 100.0f && v_range_f != 0.0f) || tweak_slow)
                        input_delta = ((input_delta < 0.0f) ? -1.0f : +1.0f) / v_range_f;
                    else
                        input_delta /= 100.0f;
                }
                if (tweak_fast)
                    input_delta *= 10.0f;

                g.SliderCurrentAccum += input_delta;
                g.SliderCurrentAccumDirty = true;
            }

            if (g.NavActivatePressedId == id && !g.ActiveIdIsJustActivated)
            {
                ClearActiveID();
            }
            else if (g.SliderCurrentAccumDirty)
            {
                float old_t = ScaleRatioFromValue_TowardMax<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, *v, v_min, v_max, epsilon);
                float new_t = ImSaturate(old_t + g.SliderCurrentAccum);

                // Convert to value, then back to t to see how much we actually moved
                TYPE v_new = ScaleValueFromRatio_TowardMax<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, new_t, v_min, v_max, epsilon);

                if (is_floating_point && !(flags & ImGuiSliderFlags_NoRoundToFormat))
                {
                    v_new = bl_round_to_decimals(decimal_precision, v_new);
                }

                float realized_t = ScaleRatioFromValue_TowardMax<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, v_new, v_min, v_max, epsilon);

                float delta = g.SliderCurrentAccum;
                g.SliderCurrentAccum -= (delta > 0) ? ImMin(realized_t - old_t, delta)
                    : ImMax(realized_t - old_t, delta);
                g.SliderCurrentAccumDirty = false;

                clicked_t = new_t;
                set_new_value = true;
            }
        }

        if (set_new_value)
            if ((g.LastItemData.ItemFlags & ImGuiItemFlags_ReadOnly) || (flags & ImGuiSliderFlags_ReadOnly))
                set_new_value = false;

        if (set_new_value)
        {
            TYPE v_new = ScaleValueFromRatio_TowardMax<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, clicked_t, v_min, v_max, epsilon);

            if (is_floating_point && !(flags & ImGuiSliderFlags_NoRoundToFormat))
                v_new = bl_round_to_decimals(decimal_precision, v_new);

            if (*v != v_new)
            {
                *v = v_new;
                value_changed = true;
            }
        }
    }

    // Grab rect
    if (slider_sz < 1.0f)
    {
        *out_grab_bb = ImRect(bb.Min, bb.Min);
    }
    else
    {
        float grab_t = ScaleRatioFromValue_TowardMax<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, *v, v_min, v_max, epsilon);
        if (axis == ImGuiAxis_Y)
            grab_t = 1.0f - grab_t;
        const float grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, grab_t);
        if (axis == ImGuiAxis_X)
            *out_grab_bb = ImRect(grab_pos - grab_sz * 0.5f, bb.Min.y + grab_padding, grab_pos + grab_sz * 0.5f, bb.Max.y - grab_padding);
        else
            *out_grab_bb = ImRect(bb.Min.x + grab_padding, grab_pos - grab_sz * 0.5f, bb.Max.x - grab_padding, grab_pos + grab_sz * 0.5f);
    }

    return value_changed;
}

bool SliderBehavior_TowardMax(const ImRect& bb, ImGuiID id, ImGuiDataType data_type, void* p_v, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, ImRect* out_grab_bb)
{
    // Read imgui.cpp "API BREAKING CHANGES" section for 1.78 if you hit this assert.
    IM_ASSERT((flags == 1 || (flags & ImGuiSliderFlags_InvalidMask_) == 0) && "Invalid ImGuiSliderFlags flags! Has the legacy 'float power' argument been mistakenly cast to flags? Call function with ImGuiSliderFlags_Logarithmic flags instead.");
    IM_ASSERT((flags & ImGuiSliderFlags_WrapAround) == 0); // Not supported by SliderXXX(), only by DragXXX()

    switch (data_type)
    {
    case ImGuiDataType_S8: { ImS32 v32 = (ImS32) * (ImS8*)p_v;  bool r = SliderBehaviorT_TowardMax<ImS32, ImS32, float>(bb, id, ImGuiDataType_S32, &v32, *(const ImS8*)p_min, *(const ImS8*)p_max, format, flags, out_grab_bb); if (r) *(ImS8*)p_v = (ImS8)v32;  return r; }
    case ImGuiDataType_U8: { ImU32 v32 = (ImU32) * (ImU8*)p_v;  bool r = SliderBehaviorT_TowardMax<ImU32, ImS32, float>(bb, id, ImGuiDataType_U32, &v32, *(const ImU8*)p_min, *(const ImU8*)p_max, format, flags, out_grab_bb); if (r) *(ImU8*)p_v = (ImU8)v32;  return r; }
    case ImGuiDataType_S16: { ImS32 v32 = (ImS32) * (ImS16*)p_v; bool r = SliderBehaviorT_TowardMax<ImS32, ImS32, float>(bb, id, ImGuiDataType_S32, &v32, *(const ImS16*)p_min, *(const ImS16*)p_max, format, flags, out_grab_bb); if (r) *(ImS16*)p_v = (ImS16)v32; return r; }
    case ImGuiDataType_U16: { ImU32 v32 = (ImU32) * (ImU16*)p_v; bool r = SliderBehaviorT_TowardMax<ImU32, ImS32, float>(bb, id, ImGuiDataType_U32, &v32, *(const ImU16*)p_min, *(const ImU16*)p_max, format, flags, out_grab_bb); if (r) *(ImU16*)p_v = (ImU16)v32; return r; }
    case ImGuiDataType_S32:
        //IM_ASSERT(*(const ImS32*)p_min >= IM_S32_MIN / 2 && *(const ImS32*)p_max <= IM_S32_MAX / 2);
        return SliderBehaviorT_TowardMax<ImS32, ImS32, float >(bb, id, data_type, (ImS32*)p_v, *(const ImS32*)p_min, *(const ImS32*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_U32:
        //IM_ASSERT(*(const ImU32*)p_max <= IM_U32_MAX / 2);
        return SliderBehaviorT_TowardMax<ImU32, ImS32, float >(bb, id, data_type, (ImU32*)p_v, *(const ImU32*)p_min, *(const ImU32*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_S64:
        //IM_ASSERT(*(const ImS64*)p_min >= IM_S64_MIN / 2 && *(const ImS64*)p_max <= IM_S64_MAX / 2);
        return SliderBehaviorT_TowardMax<ImS64, ImS64, double>(bb, id, data_type, (ImS64*)p_v, *(const ImS64*)p_min, *(const ImS64*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_U64:
        //IM_ASSERT(*(const ImU64*)p_max <= IM_U64_MAX / 2);
        return SliderBehaviorT_TowardMax<ImU64, ImS64, double>(bb, id, data_type, (ImU64*)p_v, *(const ImU64*)p_min, *(const ImU64*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_Float:
        //IM_ASSERT(*(const float*)p_min >= -FLT_MAX / 2.0f && *(const float*)p_max <= FLT_MAX / 2.0f);
        return SliderBehaviorT_TowardMax<float, float, float >(bb, id, data_type, (float*)p_v, *(const float*)p_min, *(const float*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_Double:
        //IM_ASSERT(*(const double*)p_min >= -DBL_MAX / 2.0f && *(const double*)p_max <= DBL_MAX / 2.0f);
        return SliderBehaviorT_TowardMax<double, double, double>(bb, id, data_type, (double*)p_v, *(const double*)p_min, *(const double*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_COUNT: break;
    }
    IM_ASSERT(0);
    return false;
}

bool SliderScalar_TowardMax(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags)
{
    using namespace ImGui;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const float w = CalcItemWidth();

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
        return false;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    if (!temp_input_is_active)
    {
        // Tabbing or CTRL+click on Slider turns it into an input box
        const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool make_active = (clicked || g.NavActivateId == id);
        if (make_active && clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if ((clicked && g.IO.KeyCtrl) || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;

        // Store initial value (not used by main lib but available as a convenience but some mods e.g. to revert)
        if (make_active)
            memcpy(&g.ActiveIdValueOnActivation, p_data, DataTypeGetInfo(data_type)->Size);

        if (make_active && !temp_input_is_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
    }

    if (temp_input_is_active)
    {
        // Only clamp CTRL+Click input when ImGuiSliderFlags_ClampOnInput is set (generally via ImGuiSliderFlags_AlwaysClamp)
        const bool clamp_enabled = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
        return TempInputScalar(frame_bb, id, label, data_type, p_data, format, clamp_enabled ? p_min : NULL, clamp_enabled ? p_max : NULL);
    }

    // Draw frame
    const ImU32 frame_col = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    RenderNavCursor(frame_bb, id);
    RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, g.Style.FrameRounding);

    // Slider behavior
    ImRect grab_bb;
    const bool value_changed = SliderBehavior_TowardMax(frame_bb, id, data_type, p_data, p_min, p_max, format, flags, &grab_bb);
    if (value_changed)
        MarkItemEdited(id);

    // Render grab
    if (grab_bb.Max.x > grab_bb.Min.x)
        window->DrawList->AddRectFilled(grab_bb.Min, grab_bb.Max, GetColorU32(g.ActiveId == id ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab), style.GrabRounding);

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    char value_buf[64];
    const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    if (g.LogEnabled)
        LogSetNextTextDecoration("{", "}");
    RenderTextClipped(frame_bb.Min, frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.5f));

    if (label_size.x > 0.0f)
        RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (temp_input_allowed ? ImGuiItemStatusFlags_Inputable : 0));
    return value_changed;
}

using bl::fdd;
using namespace bl::literals;

static const float DRAG_MOUSE_THRESHOLD_FACTOR = 0.50f;

static inline fdd ImAbs128(fdd x) { return x < 0 ? -x : x; }
static inline fdd ImLog128(fdd x) { using std::log; return log(x); }
static inline fdd ImPow128(fdd a, fdd b) { using std::pow; return pow(a, b); }
static inline fdd ImClamp128(fdd v, fdd mn, fdd mx)
{
    if (v < mn) return mn;
    if (v > mx) return mx;
    return v;
}

// Round to decimal precision parsed from printf-like format (precision only)
static inline fdd RoundScalarWithFormat128(const char* fmt, fdd v) {
    if (!fmt) return v;
    const char* s = ImParseFormatFindStart(fmt);
    if (s[0] != '%' || s[1] == '%') return v;
    int prec = ImParseFormatPrecision128(s, 3);
    if (prec <= 0) return v;
    return round_decimals(v, prec);
}

// value -> [0,1] ratio on drag/slider (log-support)
static float ScaleRatioFromValue_flt128(fdd v, fdd v_min, fdd v_max, bool is_logarithmic, float logarithmic_zero_epsilon)
{
    if (v_min == v_max) return 0.0f;

    fdd v_clamped = (v_min < v_max) ? ImClamp128(v, v_min, v_max) : ImClamp128(v, v_max, v_min);

    if (is_logarithmic)
    {
        bool flipped = v_max < v_min;
        if (flipped) { fdd t = v_min; v_min = v_max; v_max = t; }

        fdd eps = fdd(logarithmic_zero_epsilon);
        fdd vminf = (ImAbs128(v_min) < eps) ? (v_min < 0 ? -eps : eps) : v_min;
        fdd vmaxf = (ImAbs128(v_max) < eps) ? (v_max < 0 ? -eps : eps) : v_max;

        if (v_min == 0 && v_max < 0) vminf = -eps;
        else if (v_max == 0 && v_min < 0) vmaxf = -eps;

        float res;
        if (v_clamped <= vminf) res = 0.0f;
        else if (v_clamped >= vmaxf) res = 1.0f;
        else if ((v_min * v_max) < 0)
        {
            // Crosses zero: split portions (deadzone not used for drags)
            float zc = (-(float)v_min) / ((float)v_max - (float)v_min);
            if (v == 0) res = zc;
            else if (v < 0)
                res = (1.0f - (float)(ImLog128(-(v_clamped) / eps) / ImLog128(-vminf / eps))) * zc;
            else
                res = zc + ((float)(ImLog128(v_clamped / eps) / ImLog128(vmaxf / eps)) * (1.0f - zc));
        }
        else if ((v_min < 0) || (v_max < 0))
            res = 1.0f - (float)(ImLog128(-(v_clamped) / -vmaxf) / ImLog128(-vminf / -vmaxf));
        else
            res = (float)(ImLog128(v_clamped / vminf) / ImLog128(vmaxf / vminf));

        return flipped ? (1.0f - res) : res;
    }
    // linear
    return (float)((double)(v_clamped - v_min) / (double)(v_max - v_min));
}

// [0,1] ratio -> value (log-support)
static fdd ScaleValueFromRatio_flt128(float t, fdd v_min, fdd v_max, bool is_logarithmic, float logarithmic_zero_epsilon)
{
    if (t <= 0.0f || v_min == v_max) return v_min;
    if (t >= 1.0f) return v_max;

    if (is_logarithmic)
    {
        fdd eps = fdd(logarithmic_zero_epsilon);
        fdd vminf = (ImAbs128(v_min) < eps) ? (v_min < 0 ? -eps : eps) : v_min;
        fdd vmaxf = (ImAbs128(v_max) < eps) ? (v_max < 0 ? -eps : eps) : v_max;

        bool flipped = v_max < v_min;
        if (flipped) { fdd t2 = vminf; vminf = vmaxf; vmaxf = t2; }
        float tf = flipped ? (1.0f - t) : t;

        if ((v_max == 0) && (v_min < 0))
            vmaxf = -eps;

        if ((v_min * v_max) < 0)
        {
            float zc = (-(float)ImMin((double)v_min, (double)v_max)) / ImAbs((float)v_max - (float)v_min);
            if (tf < zc)
                return -(eps * ImPow128(-vminf / eps, fdd{ 1.0f - (tf / zc) }));
            else if (tf > zc)
                return (eps * ImPow128(vmaxf / eps, fdd{ (tf - zc) / (1.0f - zc) }));
            else
                return 0_dd;
        }
        else if ((v_min < 0) || (v_max < 0))
            return -(-vmaxf * ImPow128(-vminf / -vmaxf, fdd{ 1.0f - tf }));
        else
            return (vminf * ImPow128(vmaxf / vminf, fdd{ tf }));
    }

    // linear
    return bl::math::lerp(v_min, v_max, fdd(t));
}

static int DataTypeCompareFlt128(const fdd* lhs, const fdd* rhs)
{
    if (*lhs < *rhs) return -1;
    if (*lhs > *rhs) return +1;
    return 0;
}

static bool TempInputFlt128(const ImRect& bb, ImGuiID id, const char* label, fdd* p_data, const char* format, const fdd* p_clamp_min, const fdd* p_clamp_max)
{
    using namespace ImGui;

    // FIXME: May need to clarify display behavior if format doesn't contain %.
    // "%d" -> "%d" / "There are %d items" -> "%d" / "items" -> "%d" (fallback). Also see #6405
    ImGuiContext& g = *GImGui;
    //const ImGuiDataTypeInfo* type_info = DataTypeGetInfo(data_type);
    char fmt_buf[64];
    char data_buf[128];
    format = ImParseFormatTrimDecorations(format, fmt_buf, IM_ARRAYSIZE(fmt_buf));
    if (format[0] == 0)
        format = "%f";

    //DataTypeFormatString(data_buf, IM_ARRAYSIZE(data_buf), data_type, p_data, format);
    const int precision = ImParseFormatPrecision128(format ? format : "%.3f", 3);
    std::string s = to_string(*p_data, precision);
    strcpy(data_buf, s.c_str());

    ImStrTrimBlanks(data_buf);

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll | (ImGuiInputTextFlags)ImGuiInputTextFlags_LocalizeDecimalPoint;
    g.LastItemData.ItemFlags |= ImGuiItemFlags_NoMarkEdited; // Because TempInputText() uses ImGuiInputTextFlags_MergedItem it doesn't submit a new item, so we poke LastItemData.
    bool value_changed = false;
    if (TempInputText(bb, id, label, data_buf, IM_ARRAYSIZE(data_buf), flags))
    {
        // Backup old value
        size_t data_type_size = sizeof(fdd);
        fdd data_backup;
        memcpy(&data_backup, p_data, data_type_size);

        // Apply new value (or operations) then clamp
        ///DataTypeApplyFromText(data_buf, data_type, p_data, format, NULL);

        fdd v_new;
        const auto parsed = bl::from_chars(
            data_buf,
            data_buf + std::strlen(data_buf),
            v_new);
        // accept if parse succeeded and consumed something meaningful
        if (parsed.ec == std::errc{} && parsed.ptr != data_buf)
        {
            if (p_clamp_min || p_clamp_max) v_new = ImClamp128(v_new, *p_clamp_min, *p_clamp_max);
            // optional: keep full precision; if you must, round for *display* only
            bool value_changed = (*p_data != v_new);
            *p_data = v_new;

            //if (changed) MarkItemEdited(id);

            g.LastItemData.ItemFlags &= ~ImGuiItemFlags_NoMarkEdited;
            value_changed = memcmp(&data_backup, p_data, data_type_size) != 0;
            if (value_changed)
                MarkItemEdited(id);
        }

        ///if (p_clamp_min || p_clamp_max)
        ///{
        ///    if (p_clamp_min && p_clamp_max && DataTypeCompareFlt128(p_clamp_min, p_clamp_max) > 0)
        ///        ImSwap(p_clamp_min, p_clamp_max);
        ///    *p_data = ImClamp128(*p_data, *p_clamp_min, *p_clamp_max);
        ///}
        ///
        ///// Only mark as edited if new value is different
        ///g.LastItemData.ItemFlags &= ~ImGuiItemFlags_NoMarkEdited;
        ///value_changed = memcmp(&data_backup, p_data, data_type_size) != 0;
        ///if (value_changed)
        ///    MarkItemEdited(id);
    }
    return value_changed;
}

//
// ---- Behavior core for fdd drag ----
//

static bool DragBehaviorFloat128(ImGuiID id, fdd* v, fdd v_speed, fdd v_min, fdd v_max, const char* format, ImGuiSliderFlags flags)
{
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    //if (g.ActiveId != id) return false;

    // Clear active state when mouse is released (or when nav says deactivate)
    if (g.ActiveId == id)
    {
        if (g.ActiveIdSource == ImGuiInputSource_Mouse && !g.IO.MouseDown[0])
            ClearActiveID();
        else if ((g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
            && g.NavActivatePressedId == id && !g.ActiveIdIsJustActivated)
            ClearActiveID();
    }
    else
        return false;

    if ((g.LastItemData.ItemFlags & ImGuiItemFlags_ReadOnly) || (flags & ImGuiSliderFlags_ReadOnly))
        return false;

    const ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;
    const bool is_bounded = (v_min < v_max) || ((v_min == v_max) && ((v_min != 0) || (flags & ImGuiSliderFlags_ClampZeroRange)));
    const bool is_wrapped = is_bounded && (flags & ImGuiSliderFlags_WrapAround);
    const bool is_logarithmic = (flags & ImGuiSliderFlags_Logarithmic) != 0;

    // Default speed if not provided and there’s a sane range
    if (v_speed == 0 && is_bounded)
    {
        // g.DragSpeedDefaultRatio is float; map it into fdd range
        fdd range = v_max - v_min;

        if (range > 0 && range < std::numeric_limits<fdd>::max())
            v_speed = range * fdd(g.DragSpeedDefaultRatio);
        else
            v_speed = 1;
    }

    // Mouse / Nav input -> accumulate float delta, then scale to fdd
    float adjust_delta_f = 0.0f;
    if (g.ActiveIdSource == ImGuiInputSource_Mouse && ImGui::IsMousePosValid() && ImGui::IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * DRAG_MOUSE_THRESHOLD_FACTOR))
    {
        adjust_delta_f = g.IO.MouseDelta[axis];
        if (g.IO.KeyAlt && !(flags & ImGuiSliderFlags_NoSpeedTweaks))   adjust_delta_f *= 1.0f / 100.0f;
        if (g.IO.KeyShift && !(flags & ImGuiSliderFlags_NoSpeedTweaks)) adjust_delta_f *= 10.0f;
    }
    else if (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
    {
        const bool tweak_slow = ImGui::IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakSlow : ImGuiKey_NavKeyboardTweakSlow);
        const bool tweak_fast = ImGui::IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakFast : ImGuiKey_NavKeyboardTweakFast);
        float tweak_factor = (flags & ImGuiSliderFlags_NoSpeedTweaks) ? 1.0f : (tweak_slow ? 0.1f : (tweak_fast ? 10.0f : 1.0f));
        adjust_delta_f = ImGui::GetNavTweakPressedAmount(axis) * tweak_factor;
    }

    if (axis == ImGuiAxis_Y) adjust_delta_f = -adjust_delta_f;

    // For logarithmic, scale into parametric [0,1] space
    if (is_logarithmic)
    {
        float log_eps = 0.0f;
        // Estimate epsilon from format precision (default 3)
        int prec = ImParseFormatPrecision128(format ? format : "%.3f", 3);
        log_eps = ImPow(0.1f, (float)prec);

        const bool just_activated = g.ActiveIdIsJustActivated;
        const bool pushing_out =
            is_bounded && !is_wrapped &&
            ((*v >= v_max && adjust_delta_f > 0.0f) || (*v <= v_min && adjust_delta_f < 0.0f));
        if (just_activated || pushing_out)
        {
            g.DragCurrentAccum = 0.0f;
            g.DragCurrentAccumDirty = false;
        }
        else if (adjust_delta_f != 0.0f)
        {
            g.DragCurrentAccum += (float)(fdd(adjust_delta_f) / std::max(fdd::eps(), bl::abs(v_max - v_min)));
            g.DragCurrentAccumDirty = true;
        }

        if (!g.DragCurrentAccumDirty) return false;

        float old_param = ScaleRatioFromValue_flt128(*v, v_min, v_max, true, log_eps);
        float new_param = ImSaturate(old_param + g.DragCurrentAccum);
        fdd v_new = ScaleValueFromRatio_flt128(new_param, v_min, v_max, true, log_eps);

        if (!(flags & ImGuiSliderFlags_NoRoundToFormat))
            v_new = RoundScalarWithFormat128(format, v_new);

        // preserve remainder
        float new_param_after_round = ScaleRatioFromValue_flt128(v_new, v_min, v_max, true, log_eps);
        g.DragCurrentAccum -= (new_param_after_round - old_param);
        g.DragCurrentAccumDirty = false;

        // wrapping/clamp
        if (is_bounded && !is_wrapped)
            v_new = ImClamp128(v_new, v_min, v_max);

        if (*v == v_new)
            return false;
        *v = v_new;
        return true;
    }
    else
    {
        const bool just_activated = g.ActiveIdIsJustActivated;
        const bool pushing_out =
            is_bounded && !is_wrapped &&
            ((*v >= v_max && adjust_delta_f > 0.0f) || (*v <= v_min && adjust_delta_f < 0.0f));

        if (just_activated || pushing_out)
        {
            g.DragCurrentAccum = 0.0f;
            g.DragCurrentAccumDirty = false;
        }
        else if (adjust_delta_f != 0.0f)
        {
            g.DragCurrentAccum += adjust_delta_f;
            g.DragCurrentAccumDirty = true;
        }

        if (!g.DragCurrentAccumDirty) return false;

        fdd v_cur = *v + (fdd(g.DragCurrentAccum) * v_speed);
        if (!(flags & ImGuiSliderFlags_NoRoundToFormat))
            v_cur = RoundScalarWithFormat128(format, v_cur);

        // preserve remainder after rounding
        g.DragCurrentAccum -= (float)((double)((v_cur - *v) / v_speed));
        g.DragCurrentAccumDirty = false;

        // lose negative zero
        if (v_cur == fdd(-0)) v_cur = 0;

        // clamp/wrap
        if (is_bounded)
        {
            if (is_wrapped)
            {
                fdd range = v_max - v_min;
                if (range != 0)
                {
                    // wrap into [v_min, v_max]
                    while (v_cur < v_min) v_cur = v_cur + range;
                    while (v_cur > v_max) v_cur = v_cur - range;
                }
            }
            else
            {
                v_cur = ImClamp128(v_cur, v_min, v_max);
            }
        }

        if (*v == v_cur) return false;
        *v = v_cur;
        return true;
    }
}


namespace ImGui
{
    bool collapsing_head_box_contents_disabled = false;

    void BeginCollapsingHeaderContentsDisabled()
    {
        collapsing_head_box_contents_disabled = true;
    }

    void EndCollapsingHeaderContentsDisabled()
    {
        collapsing_head_box_contents_disabled = false;
    }

    bool CollapsingHeaderContentsDisabled()
    {
        return collapsing_head_box_contents_disabled;
    }

    bool CollapsingHeaderBox(const char* id, bool open_by_default, float pad, float pad_extra_w)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DlCtx& c = box_dlctx(dl);
        box_ensure_split(dl, c);

        const int my_depth = c.depth;
        c.depth++;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        const bool open = ImGui::CollapsingHeader(id, open_by_default ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ImGui::PopStyleVar();

        if (open && collapsing_head_box_contents_disabled)
            ImGui::BeginDisabled();

        const ImVec2 hmin = ImGui::GetItemRectMin();
        const ImVec2 hmax = ImGui::GetItemRectMax();

        float spacing = 0.5f;
        ImGui::Dummy(ImVec2(0, spacing));

        if (!open)
        {
            c.depth--;
            box_maybe_merge(dl, c);
            return false;
        }

        CurrentBoxState st{};
        st.kind = BoxKind::Header;
        st.dl = dl;
        st.pad = pad;
        st.extra = pad_extra_w;
        st.depth = my_depth;
        st.content_ch = c.content_ch;
        st.header_min = hmin;
        st.header_max = hmax;

        st.start_cursor = ImGui::GetCursorScreenPos();
        st.span_w = ImMax(0.0f, ImGui::GetContentRegionAvail().x + st.extra * 2);

        box_stack().push_back(st);

        ImGui::PushID(id);
        box_begin_contents(box_stack().back());
        return true;
    }

    void EndCollapsingHeaderBox(float end_spacing)
    {
        auto& s = box_stack();
        IM_ASSERT(!s.empty() && s.back().kind == BoxKind::Header);

        ImGui::Dummy(ImVec2(0, 1.0f));

        CurrentBoxState st = s.back();
        s.pop_back();

        ImGui::EndGroup();

        // tight content bounds (group)
        const ImVec2 content_max = ImGui::GetItemRectMax();

        // RESTORE window rects so subsequent items use full width again
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            window->WorkRect.Max.x = st.old_work_rect_max_x;
            window->ContentRegionRect.Max.x = st.old_content_rect_max_x;
        }

        // outer frame
        const float y0 = (st.header_min.y + st.header_max.y) * 0.5f;
        const float x0 = st.start_cursor.x - st.extra;
        const float x1 = x0 + st.span_w;

        ImVec2 outer_min(x0, y0);
        ImVec2 outer_max(x1, content_max.y + st.pad);

        // draw behind content
        box_draw_bg_border(st, outer_min, outer_max);

        // advance cursor below the box and restore X to normal content start
        ImGui::SetCursorScreenPos(ImVec2(st.start_cursor.x, outer_max.y));
        ImGui::Dummy(ImVec2(st.span_w, bl::scale_size(end_spacing)));

        ImGui::PopID();

        DlCtx& c = box_dlctx(st.dl);
        c.depth--;
        box_maybe_merge(st.dl, c);

        if (collapsing_head_box_contents_disabled)
            ImGui::EndDisabled();
    }

    bool Section(const char* name, bool open_by_default, float header_spacing, float body_margin_top)
    {
        ImGui::Dummy(bl::scale_size(0.0f, header_spacing));
        bool ret = ImGui::CollapsingHeader(name, open_by_default ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (ret) ImGui::Dummy(bl::scale_size(0.0f, body_margin_top));
        return ret;
    }

    bool ResetBtn(const char* id)
    {
        // todo: automatic GL destruction on exit
        static int size = (int)bl::platform()->lineHeight();
        static GLuint reset_icon = bl::loadGLTextureRGBA8("/data/icon/reset.svg", nullptr, nullptr, size, size);
        return ImGui::ImageButton(id, reset_icon, ImVec2((float)size, (float)size));
    }

    bool InlResetBtn(const char* id)
    {
        ImGui::SameLine();
        bool ret = ResetBtn(id);
        return ret;
    }

    bool ImageButtonCentered(
        const char* str_id,
        ImTextureID user_texture_id,
        const ImVec2& image_size,
        const ImVec2& uv0,
        const ImVec2& uv1,
        const ImVec4& bg_col,
        const ImVec4& tint_col)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiStyle& style = ImGui::GetStyle();
        ImDrawList* dl = window->DrawList;

        // make the button the same size ImageButton would use
        ImVec2 button_size(
            image_size.x + style.FramePadding.x * 2.0f,
            image_size.y + style.FramePadding.y * 2.0f
        );

        // width actually available in this cell *before* adding the item
        float cell_width = ImGui::GetContentRegionAvail().x;
        if (cell_width <= 0.0f)
            cell_width = button_size.x; // fallback (non-table or odd layout)

        // standard InvisibleButton for layout + hit test
        if (!ImGui::InvisibleButton(str_id, button_size))
        {
            // still need rect for drawing, even if not pressed
        }
        bool pressed = ImGui::IsItemActivated(); // same as return value

        ImVec2 p_min = ImGui::GetItemRectMin();
        ImVec2 p_max = ImGui::GetItemRectMax();

        // ---- draw button frame (like a normal Button) ----
        ImGuiCol col_idx = ImGuiCol_Button;
        if (ImGui::IsItemActive())      col_idx = ImGuiCol_ButtonActive;
        else if (ImGui::IsItemHovered()) col_idx = ImGuiCol_ButtonHovered;

        ImU32 frame_col = ImGui::GetColorU32(col_idx);
        dl->AddRectFilled(p_min, p_max, frame_col, style.FrameRounding);

        if (style.FrameBorderSize > 0.0f)
        {
            ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);
            dl->AddRect(p_min, p_max, border_col, style.FrameRounding, 0, style.FrameBorderSize);
        }

        // ---- center the image in the *cell*, not the button ----

        // horizontal: center in the cell width (even if button extends beyond)
        float visible_width = cell_width;
        float cell_center_x = p_min.x + visible_width * 0.5f;

        // vertical: center inside the inner (padded) region of the button
        float inner_h = button_size.y - style.FramePadding.y * 2.0f;

        ImVec2 img_min(
            cell_center_x - image_size.x * 0.5f,
            p_min.y + style.FramePadding.y + (inner_h - image_size.y) * 0.5f
        );
        ImVec2 img_max(
            img_min.x + image_size.x,
            img_min.y + image_size.y
        );

        if (bg_col.w > 0.0f)
            dl->AddRectFilled(img_min, img_max, ImGui::GetColorU32(bg_col));

        dl->AddImage(user_texture_id, img_min, img_max, uv0, uv1, ImGui::GetColorU32(tint_col));

        return pressed;
    }

    bool SliderDouble(const char* label, double* v, double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        return SliderScalar(label, ImGuiDataType_Double, v, &v_min, &v_max, format, flags);
    }

    bool DragDouble(const char* label, double* v, double v_speed, double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        return DragScalar(label, ImGuiDataType_Double, v, (float)v_speed, &v_min, &v_max, format, flags);
    }

    bool SliderDouble2(const char* label, double v[2], double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        return SliderScalarN(label, ImGuiDataType_Double, v, 2, &v_min, &v_max, format, flags);
    }

    bool DragDouble2(const char* label, double v[2], double v_speed, double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        return DragScalarN(label, ImGuiDataType_Double, v, 2, (float)v_speed, &v_min, &v_max, format, flags);
    }

    bool SliderDouble_InvLog(const char* label, double* v, double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        return SliderScalar_TowardMax(label, ImGuiDataType_Double, v, &v_min, &v_max, format, flags);
    }

    bool DragFloat128(const char* label, fdd* v, fdd v_speed, fdd v_min, fdd v_max, const char* format, ImGuiSliderFlags flags)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;
        
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const float w = CalcItemWidth();

        const ImVec2 label_size = CalcTextSize(label, NULL, true);
        const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
        const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

        const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
        ItemSize(total_bb, style.FramePadding.y);
        if (!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
            return false;

        if (format == NULL)
            format = "%f";

        const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
        bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
        if (!temp_input_is_active)
        {
            const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
            const bool double_clicked = (hovered && g.IO.MouseClickedCount[0] == 2 && TestKeyOwner(ImGuiKey_MouseLeft, id));
            const bool make_active = (clicked || double_clicked || g.NavActivateId == id);
            if (make_active && (clicked || double_clicked))
                SetKeyOwner(ImGuiKey_MouseLeft, id);
            if (make_active && temp_input_allowed)
                if ((clicked && g.IO.KeyCtrl) || double_clicked || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                    temp_input_is_active = true;

            if (g.IO.ConfigDragClickToInputText && temp_input_allowed && !temp_input_is_active)
                if (g.ActiveId == id && hovered && g.IO.MouseReleased[0] && !IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * DRAG_MOUSE_THRESHOLD_FACTOR))
                {
                    g.NavActivateId = id;
                    g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                    temp_input_is_active = true;
                }

            if (make_active && !temp_input_is_active)
            {
                SetActiveID(id, window);
                SetFocusID(id, window);
                FocusWindow(window);
                g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
            }
        }

        // --- Text input mode (double-click / Ctrl+Click) ---
        if (temp_input_is_active)
        {
            const bool clamp_enabled = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
            return TempInputFlt128(frame_bb, id, label, v, format, clamp_enabled ? &v_min : NULL, clamp_enabled ? &v_max : NULL);
        }

        // Draw frame
        const ImU32 frame_col = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        RenderNavCursor(frame_bb, id);
        RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, style.FrameRounding);

        // Drag behavior (active-id path uses internal accumulators)
        const bool value_changed = DragBehaviorFloat128(id, v, v_speed, v_min, v_max, format ? format : "%.3f", flags);
        if (value_changed)
            MarkItemEdited(id);

        // Display value using your to_string(fdd)
        std::string value_s = to_string(
            *v,
            ImParseFormatPrecision128(format, 3),
            std::ios_base::fixed);
        if (g.LogEnabled) LogSetNextTextDecoration("{", "}");
        RenderTextClipped(frame_bb.Min, frame_bb.Max, value_s.c_str(), NULL, NULL, ImVec2(0.5f, 0.5f));

        if (label_size.x > 0.0f)
            RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (temp_input_allowed ? ImGuiItemStatusFlags_Inputable : 0));
        return value_changed;
    }

    bool SliderAngle(const char* label, double* v_rad, double v_rad_min, double v_rad_max, const char* format, ImGuiSliderFlags flags)
    {
        double v_deg = (*v_rad * 180.0 / std::numbers::pi);

        bool changed = ImGui::SliderDouble(label, &v_deg,
            (v_rad_min * 180.0 / std::numbers::pi),
            (v_rad_max * 180.0 / std::numbers::pi),
            format, flags);

        if (changed)
            *v_rad = (v_deg * std::numbers::pi / 180.0);

        return changed;
    }

    bool SliderAngle(const char* label, double* v_rad, double v_rad_min, double v_rad_max, int decimals, ImGuiSliderFlags flags)
    {
        char format[8];
        snprintf(format, 8, "%%.%df\xC2\xB0", decimals);
        return SliderAngle(label, v_rad, v_rad_min, v_rad_max, format, flags);
    }

    bool SliderAngle(const char* label, float* v_rad, float v_rad_min, float v_rad_max, int decimals, ImGuiSliderFlags flags)
    {
        double v_rad_d = *v_rad;
        if (SliderAngle(label, &v_rad_d, v_rad_min, v_rad_max, decimals, flags))
        {
            *v_rad = (float)v_rad_d;
            return true;
        }
        return false;
    }

    bool RevertableSliderDouble(const char* label, double* v, double* initial, double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= SliderScalar(label, ImGuiDataType_Double, v, &v_min, &v_max, format, flags);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if (*v != *initial && InlResetBtn(label))
        {
            *v = *initial;
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    bool RevertableDragDouble(const char* label, double* v, double* initial, double v_speed, double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= DragScalar(label, ImGuiDataType_Double, v, (float)v_speed, &v_min, &v_max, format, flags);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if (*v != *initial && InlResetBtn(label))
        {
            *v = *initial;
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    bool RevertableDragFloat128(const char* label, fdd* v, fdd* initial, fdd v_speed, fdd v_min, fdd v_max, const char* format, ImGuiSliderFlags flags)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= DragFloat128(label, v, v_speed, v_min, v_max, format, flags);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if (*v != *initial && InlResetBtn(label))
        {
            *v = *initial;
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    bool RevertableSliderDouble2(const char* label, double v[2], double initial[2], double v_min, double v_max, const char* format, ImGuiSliderFlags flags)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= SliderScalarN(label, ImGuiDataType_Double, v, 2, &v_min, &v_max, format, flags);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if ((v[0] != initial[0] || v[1] != initial[1]) && InlResetBtn(label))
        {
            v[0] = initial[0];
            v[1] = initial[1];
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    bool RevertableSliderAngle(const char* label, double* v_rad, double* initial, double v_rad_min, double v_rad_max, const char* format, ImGuiSliderFlags flags)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= SliderAngle(label, v_rad, v_rad_min, v_rad_max, format, flags);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if (*v_rad != *initial && InlResetBtn(label))
        {
            *v_rad = *initial;
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    bool RevertableSliderAngle(const char* label, double* v_rad, double* initial, double v_rad_min, double v_rad_max, int decimals, ImGuiSliderFlags flags)
    {
        char format[8];
        snprintf(format, 8, "%%.%df\xC2\xB0", decimals);
        return RevertableSliderAngle(label, v_rad, initial, v_rad_min, v_rad_max, format, flags);
    }

    float old_work_rect_width = 0.0f;
    float old_conent_rect_width = 0.0f;
    float region_padding = 0.0f;

    void BeginPaddedRegion(float padding) 
    {
        //const float fullW = ImGui::GetContentRegionAvail().x;
        ImVec2 p0 = ImGui::GetCursorScreenPos() + ImVec2(padding, padding);
        ImGui::SetCursorScreenPos(p0);
        ImGui::BeginGroup();

        region_padding = padding;

        old_work_rect_width = ImGui::GetCurrentWindow()->WorkRect.Max.x;
        old_conent_rect_width = ImGui::GetCurrentWindow()->ContentRegionRect.Max.x;
        ImGui::GetCurrentWindow()->WorkRect.Max.x -= padding;
        ImGui::GetCurrentWindow()->ContentRegionRect.Max.x -= padding;
        ImGui::PushTextWrapPos(old_conent_rect_width - padding);
    }
    void EndPaddedRegion()
    {
        ImGui::PopTextWrapPos();
        ImGui::GetCurrentWindow()->ContentRegionRect.Max.x = old_conent_rect_width;
        ImGui::GetCurrentWindow()->WorkRect.Max.x = old_work_rect_width;

        ImGui::EndGroup();
        ImVec2 p1 = ImGui::GetItemRectMax() + ImVec2(region_padding, region_padding);
        ImGui::Dummy(ImVec2(region_padding, region_padding));
        region_padding = 0.0f;
    }

    
}
