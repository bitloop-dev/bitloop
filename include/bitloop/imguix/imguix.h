#pragma once
#include <unordered_map>
#include <numbers>
#include <algorithm>

constexpr double pi = std::numbers::pi;

#define IMGUI_DEFINE_MATH_OPERATORS

#include <bitloop/core/project.h>
#include <bitloop/core/image_loader.h>
#include <fltx.h>

#include <imgui_internal.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>

#include "implot.h"

// Custom components
#include "imgui_splines.h"
#include "imgui_gradient_edit.h"
#include "imgui_log.h"


#ifdef __EMSCRIPTEN__
#define SDL_MAIN_HANDLED
#include <GLES3/gl3.h>
#else
#include "glad/glad.h"
#endif


inline ImTextureID ToImTextureID(GLuint tex)
{
    return (ImTextureID)(intptr_t)tex;
}

namespace bl 
{
    inline ImVec2 scale_size(const ImVec2& size)
    {
        float dpr = bl::platform()->dpr();
        return ImVec2(size.x * dpr, size.y * dpr);
    }

    inline ImVec2 scale_size(int w, int h)
    {
        float dpr = bl::platform()->dpr();
        return ImVec2((float)w * dpr, (float)h * dpr);
    }

    inline ImVec2 scale_size(float w, float h)
    {
        float dpr = bl::platform()->dpr();
        return ImVec2(w * dpr, h * dpr);
    }

    inline DVec2 scale_size(double w, double h)
    {
        float dpr = bl::platform()->dpr();
        return DVec2(w * dpr, h * dpr);
    }

    inline DVec2 scale_size(const DVec2& size)
    {
        float dpr = bl::platform()->dpr();
        return DVec2(size.x * dpr, size.y * dpr);
    }
}

std::ostream& operator<<(std::ostream& os, const ImSpline::Spline& spline);
std::ostream& operator<<(std::ostream& os, const ImGradient& gradient);

namespace ImGui
{
    using bl::fdd;
    using namespace bl::literals;

    struct StartValue { void* initial; int size; };
    static std::unordered_map<void*, StartValue> starting_map;
    static void updatePointerValues()
    {
        for (auto& item : starting_map)
            memcpy(item.first, item.second.initial, item.second.size);
    }

    bool Section(const char* name, bool open_by_default = false, float header_spacing=2.0f, float body_margin_top=2.0f);

    bool ResetBtn(const char* id);
    bool InlResetBtn(const char* id);
    bool ImageButtonCentered(
        const char* str_id,
        ImTextureID user_texture_id,
        const ImVec2& image_size,
        const ImVec2& uv0 = ImVec2(0, 0),
        const ImVec2& uv1 = ImVec2(1, 1),
        const ImVec4& bg_col = ImVec4(0, 0, 0, 0),
        const ImVec4& tint_col = ImVec4(1, 1, 1, 1));
    
    bool SliderDouble(const char* label, double* v, double v_min, double v_max, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool DragDouble(const char* label, double* v, double v_speed = 1.0, double v_min = 0.0, double v_max = 0.0, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool SliderDouble2(const char* label, double v[2], double v_min, double v_max, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool DragDouble2(const char* label, double v[2], double v_speed = 1.0f, double v_min = 0.0f, double v_max = 0.0f, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool SliderDouble_InvLog(const char* label, double* v, double v_min, double v_max, const char* format, ImGuiSliderFlags flags = 0);
    bool DragFloat128(const char* label, fdd* v, fdd v_speed=0_dd, fdd v_min=0_dd, fdd v_max=0_dd, const char* format = "%.32f", ImGuiSliderFlags flags = 0);

    template<typename T>
    bool DragFltx(const char* id, T* v, T v_speed, T v_min = {0.0}, T v_max = {1.0}, int precision=12)
    {
        double delta = 0.0;
        if (ImGui::DragDouble(id, &delta,
            1.0, 0.0, 0.0,
            bl::to_string(*v, precision, std::ios_base::scientific).c_str(), ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_NoInput))
        {
            *v += delta * v_speed;
            if (v_max > v_min)
            {
                if (*v < v_min) *v = v_min;
                if (*v > v_max) *v = v_max;
            }
            return true;
        }
        return false;
    }

    template<typename T, typename T_Initial>
    bool RevertableDragFltx(const char* id, T* v, T_Initial* initial, T v_speed, T v_min = {0.0}, T v_max = {1.0}, int precision = 12)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= DragFltx<T>(id, v, v_speed, v_min, v_max, precision);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if (*v != static_cast<T>(*initial) && InlResetBtn(id))
        {
            *v = static_cast<T>(*initial);
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    template<typename T>
    bool DragFltxCompact(const char* id, T* v, T v_speed, T v_min, T v_max, int max_precision, int peek_front=4, int peek_back=10)
    {
        double delta = 0.0;
        if (ImGui::DragDouble(id, &delta,
            1.0, 0.0, 0.0,
            bl::to_string(*v, bl::precision_info(max_precision, peek_front, peek_back)).c_str(),
            //bl::to_string_collapsed(*v, max_precision, peek_front, peek_back).c_str(),
            ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_NoInput))
        {
            *v += (T)delta * v_speed;
            if (v_max > v_min)
            {
                if (*v < v_min) *v = v_min;
                if (*v > v_max) *v = v_max;
            }
            return true;
        }
        return false;
    }

    template<typename T, typename T_Initial>
    bool RevertableDragFltxCompact(const char* id, T* v, T_Initial* initial, T v_speed, T v_min, T v_max, int max_precision, int peek_front = 4, int peek_back = 10)
    {
        bool ret = false;

        ImGui::BeginGroup();
        ImGui::PushItemWidth(ImGui::CalcItemWidth() - bl::platform()->lineHeight());
        ret |= DragFltxCompact<T>(id, v, v_speed, v_min, v_max, max_precision, peek_front, peek_back);
        ImGui::PopItemWidth();

        ImGui::PushID("reset_");
        if (*v != static_cast<T>(*initial) && InlResetBtn(id))
        {
            *v = static_cast<T>(*initial);
            ret |= true;
        }
        ImGui::PopID();
        ImGui::EndGroup();

        return ret;
    }

    bool SliderAngle(const char* label, double* v_rad, double v_rad_min, double v_rad_max, const char* format = "%.1f\xC2\xB0", ImGuiSliderFlags flags = 0);
    bool SliderAngle(const char* label, double* v_rad, double v_rad_min = 0.0, double v_rad_max = std::numbers::pi*2.0, int decimals = 1, ImGuiSliderFlags flags = 0);
    bool SliderAngle(const char* label, float* v_rad, float v_rad_min = 0.0, float v_rad_max = (float)std::numbers::pi * 2.0, int decimals = 1, ImGuiSliderFlags flags = 0);

    bool RevertableSliderDouble(const char* label, double* v, double* initial, double v_min, double v_max, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool RevertableDragDouble(const char* label, double* v, double* initial, double v_speed, double v_min, double v_max, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool RevertableDragFloat128(const char* label, fdd* v, fdd* initial, fdd v_speed, fdd v_min, fdd v_max, const char* format = "%.6f", ImGuiSliderFlags flags = 0);
    bool RevertableSliderDouble2(const char* label, double v[2], double initial[2], double v_min, double v_max, const char* format = "%.6f", ImGuiSliderFlags flags = 0);

    bool RevertableSliderAngle(const char* label, double* v_rad, double* initial, double v_rad_min, double v_rad_max, const char* format = "%.1f\xC2\xB0", ImGuiSliderFlags flags = 0);
    bool RevertableSliderAngle(const char* label, double* v_rad, double* initial, double v_rad_min = 0.0, double v_rad_max = std::numbers::pi * 2.0, int decimals = 1, ImGuiSliderFlags flags = 0);

    void BeginPaddedRegion(float padding);
    void EndPaddedRegion();

    inline void IncreaseRequiredSpaceForLabel(float& w, const char* label, float pad_right = bl::scale_size(10.0f))
    {
        float lw = ImGui::CalcTextSize(label).x + pad_right;
        if (lw > w) w = lw;
    }

    inline void SetNextItemWidthForSpace(float w)
    {
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - w - spacing);
    }

    inline void SetNextItemWidthForLabel(const char* label, float pad_right = bl::scale_size(10.0f))
    {
        float lw = ImGui::CalcTextSize(label).x + pad_right;
        float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - lw - spacing);
    }

    // Max nesting depth per window.
    static constexpr int kMaxDepth = 12;

    enum class BoxKind : unsigned char { Header, Tab, Labelled };

    struct CurrentBoxState
    {
        BoxKind kind{};
        ImDrawList* dl{};
        float pad{};
        float extra{};

        ImVec2 header_min{}, header_max{};
        ImVec2 start_cursor{};
        float  span_w{};

        float old_work_rect_max_x{};
        float old_content_rect_max_x{};

        int depth{};
        int content_ch{};

        // ---- Labelled box extras ----
        const char* label{};
        float label_pad_x{};
        ImVec2 text_sz{};
        float top_extra{};
    };

    inline std::vector<CurrentBoxState>& box_stack()
    {
        static thread_local std::vector<CurrentBoxState> s;
        return s;
    }

    struct DlCtx { int depth{}; int content_ch{}; bool split{}; };

    inline std::unordered_map<ImDrawList*, DlCtx>& box_ctx_map()
    {
        static thread_local std::unordered_map<ImDrawList*, DlCtx> m;
        return m;
    }

    inline void box_new_frame_guard()
    {
        static thread_local int last_frame = -1;
        const int frame = ImGui::GetFrameCount();
        if (frame != last_frame)
        {
            last_frame = frame;
            box_ctx_map().clear();

            // If you hit this, you're missing an End*Box() somewhere.
            IM_ASSERT(box_stack().empty() && "Unbalanced HeaderBox/TabBox (missing End...)");
            box_stack().clear();
        }
    }

    inline DlCtx& box_dlctx(ImDrawList* dl)
    {
        box_new_frame_guard();
        return box_ctx_map()[dl];
    }

    inline void box_ensure_split(ImDrawList* dl, DlCtx& c)
    {
        if (c.depth == 0 && !c.split)
        {
            const int channels = kMaxDepth + 1; // [0..kMaxDepth-1]=bg layers, [kMaxDepth]=content
            c.content_ch = kMaxDepth;
            dl->ChannelsSplit(channels);
            dl->ChannelsSetCurrent(c.content_ch);
            c.split = true;
        }
        else if (c.split)
        {
            dl->ChannelsSetCurrent(c.content_ch);
        }
    }

    inline void box_maybe_merge(ImDrawList* dl, DlCtx& c)
    {
        if (c.depth == 0 && c.split)
        {
            dl->ChannelsMerge();
            c.split = false;
        }
    }

    inline void box_begin_contents(CurrentBoxState& st)
    {
        // Move cursor into the inner padded area of the box
        ImGui::SetCursorScreenPos(ImVec2(st.start_cursor.x + st.pad,
            st.start_cursor.y + st.pad));

        // Shrink the current window's available width so everything inside
        // sees a reduced ContentRegionRect / WorkRect (right padding).
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        st.old_work_rect_max_x = window->WorkRect.Max.x;
        st.old_content_rect_max_x = window->ContentRegionRect.Max.x;

        window->WorkRect.Max.x -= st.pad;
        window->ContentRegionRect.Max.x -= st.pad;

        ImGui::BeginGroup();
    }

    inline void box_draw_bg_border(const CurrentBoxState& st, const ImVec2& outer_min, const ImVec2& outer_max)
    {
        ImDrawList* dl = st.dl;

        ImGuiStyle& style = ImGui::GetStyle();
        const float rounding = style.FrameRounding;
        const float t = (style.FrameBorderSize > 0.0f) ? style.FrameBorderSize : 1.0f;

        const ImU32 col_bg = ImColor(0.0f, 0.0f, 0.0f, 0.1f);// ImGui::GetColorU32(ImGuiCol_TitleBg);
        const ImU32 col_border = ImGui::GetColorU32(ImGuiCol_Border);
        const ImDrawFlags rf = ImDrawFlags_RoundCornersBottomLeft | ImDrawFlags_RoundCornersBottomRight;

        const int bg_ch = (st.depth < kMaxDepth) ? st.depth : (kMaxDepth - 1);

        // Allow drawing a little into window padding (your extra)
        const ImVec2 win_min = ImGui::GetWindowPos();
        const ImVec2 win_max = win_min + ImGui::GetWindowSize();

        dl->PushClipRect(win_min, win_max, true);
        dl->ChannelsSetCurrent(bg_ch);
        dl->AddRectFilled(outer_min, outer_max, col_bg, rounding, rf);
        dl->AddRect(outer_min, outer_max, col_border, rounding, rf, t);
        dl->PopClipRect();

        dl->ChannelsSetCurrent(st.content_ch);
    }

    // -------------------------
    // CollapsingHeaderBox / EndCollapsingHeaderBox
    // -------------------------

    void BeginCollapsingHeaderContentsDisabled();
    void EndCollapsingHeaderContentsDisabled();
    bool CollapsingHeaderContentsDisabled();

    bool CollapsingHeaderBox(const char* id, bool open_by_default = false, float pad = bl::scale_size(10.0f), float pad_extra_w = bl::scale_size(3.0f));
    void EndCollapsingHeaderBox(float end_spacing = bl::scale_size(4.0f));

    // ---------------------
    // TabBox / EndTabBox
    // ---------------------

    inline bool TabBox(const char* id, float pad = bl::scale_size(10.0f), float extra = bl::scale_size(3.0f))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DlCtx& c = box_dlctx(dl);
        box_ensure_split(dl, c);

        const int my_depth = c.depth;
        c.depth++;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        const bool open = ImGui::BeginTabItem(id);

        ImGui::PopStyleVar();

        const ImVec2 hmin = ImGui::GetItemRectMin();
        const ImVec2 hmax = ImGui::GetItemRectMax();

        if (!open)
        {
            c.depth--;
            box_maybe_merge(dl, c);
            return false;
        }

        CurrentBoxState st{};
        st.kind = BoxKind::Tab;
        st.dl = dl;
        st.pad = pad;
        st.extra = extra;
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

    inline void EndTabBox()
    {
        auto& s = box_stack();
        IM_ASSERT(!s.empty() && s.back().kind == BoxKind::Tab);

        CurrentBoxState st = s.back();
        s.pop_back();

        ImGui::Dummy(ImVec2(0, 1.0f));

        ImGui::EndGroup();

        const ImVec2 content_min = ImGui::GetItemRectMin();
        const ImVec2 content_max = ImGui::GetItemRectMax();

        // RESTORE window rects
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            window->WorkRect.Max.x = st.old_work_rect_max_x;
            window->ContentRegionRect.Max.x = st.old_content_rect_max_x;
        }

        // Finish the tab item AFTER we grabbed the group rect (otherwise "last item" changes)
        ImGui::EndTabItem();

        const float x0 = st.start_cursor.x - st.extra;
        const float x1 = x0 + st.span_w;

        ImVec2 outer_min(x0, content_min.y - st.pad);
        ImVec2 outer_max(x1, content_max.y + st.pad);

        box_draw_bg_border(st, outer_min, outer_max);

        ImGui::SetCursorScreenPos(ImVec2(st.start_cursor.x, outer_max.y));
        ImGui::Dummy(ImVec2(st.span_w, 0.0f));

        ImGui::PopID();

        DlCtx& c = box_dlctx(st.dl);
        c.depth--;
        box_maybe_merge(st.dl, c);
    }

    // ---------------------
    // LabelledBox / EndLabelledBox
    // ---------------------

    inline void BeginLabelledBox(const char* label = nullptr,
        float pad = bl::scale_size(13.0f),
        float label_pad_x = bl::scale_size(10.0f))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DlCtx& c = box_dlctx(dl);
        box_ensure_split(dl, c);

        const int my_depth = c.depth;
        c.depth++;

        // Measure label height and reserve vertical space for the title.
        ImVec2 label_sz = (label && *label) ? ImGui::CalcTextSize(label) : ImVec2(0.0f, 0.0f);
        if (label && *label)
            ImGui::Dummy(ImVec2(0.0f, label_sz.y)); // space above frame for the title

        // Full available width at this point (inside parent box / window).
        ImVec2 start_screen = ImGui::GetCursorScreenPos();
        float span_w = ImMax(0.0f, ImGui::GetContentRegionAvail().x);

        CurrentBoxState st{};
        st.kind = BoxKind::Labelled;
        st.dl = dl;
        st.pad = pad;
        st.extra = 0.0f;
        st.depth = my_depth;
        st.content_ch = c.content_ch;
        st.start_cursor = start_screen;
        st.span_w = span_w;

        st.label = label;
        st.label_pad_x = label_pad_x;
        st.text_sz = label_sz;
        st.top_extra = (label_sz.y > 0.0f) ? (label_sz.y * 0.5f) : 0.0f;

        // Clamp inner width while we're inside this labelled box.
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            st.old_work_rect_max_x = window->WorkRect.Max.x;
            st.old_content_rect_max_x = window->ContentRegionRect.Max.x;

            window->WorkRect.Max.x -= pad;
            window->ContentRegionRect.Max.x -= pad;
        }

        box_stack().push_back(st);

        ImGui::PushID(label ? label : "LabelledBox");

        // Move cursor inside the box (inner padding + title offset) and begin a group.
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + pad + st.top_extra));
        ImGui::BeginGroup();
    }

    inline void EndLabelledBox()
    {
        auto& s = box_stack();
        IM_ASSERT(!s.empty() && s.back().kind == BoxKind::Labelled);

        CurrentBoxState st = s.back();
        s.pop_back();

        ImGui::EndGroup();

        // Tight content bounds of the inner group.
        ImVec2 content_min = ImGui::GetItemRectMin();
        ImVec2 content_max = ImGui::GetItemRectMax();

        // Outer frame around content.
        ImVec2 outer_min = content_min - ImVec2(st.pad, st.pad + st.top_extra);
        ImVec2 outer_max = content_max + ImVec2(st.pad, st.pad);

        // Force frame to span the entire parent width.
        outer_min.x = st.start_cursor.x;
        outer_max.x = st.start_cursor.x + st.span_w;

        // Restore window rects so siblings see the original available width.
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            window->WorkRect.Max.x = st.old_work_rect_max_x;
            window->ContentRegionRect.Max.x = st.old_content_rect_max_x;
        }

        ImGuiStyle& style = ImGui::GetStyle();
        float rounding = style.FrameRounding;
        float t = (style.FrameBorderSize > 0.0f) ? style.FrameBorderSize : 1.0f;
        ImU32 col_bg = ImGui::GetColorU32(ImGuiCol_TitleBg);
        ImU32 col_border = ImGui::GetColorU32(ImGuiCol_Border);
        ImU32 col_text = ImGui::GetColorU32(ImGuiCol_Text);

        ImDrawList* dl = st.dl;

        // Draw frame in background channel for this depth.
        const int bg_ch = (st.depth < kMaxDepth) ? st.depth : (kMaxDepth - 1);
        dl->ChannelsSetCurrent(bg_ch);
        dl->AddRectFilled(outer_min, outer_max, col_bg, rounding);
        dl->AddRect(outer_min, outer_max, col_border, rounding, 0, t);

        // Label pill and text in the shared content channel.
        dl->ChannelsSetCurrent(st.content_ch);
        if (st.label && *st.label)
        {
            float x_text = outer_min.x + st.pad + st.label_pad_x;
            float y_line = outer_min.y; // top border y
            float text_margin_x = 8.0f;
            float text_margin_y = 4.0f;

            ImVec2 label_min(
                x_text - text_margin_x,
                y_line - st.text_sz.y * 0.5f - t - text_margin_y
            );
            ImVec2 label_max(
                x_text + st.text_sz.x + text_margin_x,
                y_line + st.text_sz.y * 0.5f + t + text_margin_y
            );

            dl->AddRectFilled(label_min, label_max, col_bg);
            dl->AddRect(label_min, label_max, col_border);
            dl->AddText(ImVec2(x_text, y_line - st.text_sz.y * 0.5f), col_text, st.label);
        }

        // Advance cursor below the frame.
        ImGui::SetCursorScreenPos(ImVec2(st.start_cursor.x, outer_max.y));
        ImGui::Dummy(ImVec2(st.span_w, 0.0f));

        ImGui::PopID();

        DlCtx& c = box_dlctx(st.dl);
        c.depth--;
        box_maybe_merge(st.dl, c);
    }

    inline void ScrollWhenDraggingOnVoid(const ImVec2& delta)
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImGuiWindow* window = g.CurrentWindow;
        bool hovered = false;
        bool held = false;
        ImGuiID id = window->GetID("##scrolldraggingoverlay");
        ImGui::KeepAliveID(id);

        static float vy = 0.0;
        static float scroll_amount = 0.0;

        //if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
        ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
        //if (held && delta.x != 0.0f)
        //    ImGui::SetScrollX(window, window->Scroll.x + delta.x);

        if (held) vy = delta.y;
        scroll_amount = vy;
        vy *= 0.93f;

        if (/*held && */scroll_amount != 0.0f)
            ImGui::SetScrollY(window, window->Scroll.y + scroll_amount);
    }

    inline void SwipeScrollWindow(float decay = 0.93f, float drag_threshold = bl::scale_size(20.0f))
    {
        return;

        if (!bl::platform()->isMobile())
            return;

        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImGuiIO& io = g.IO;
        ImGuiWindow* window = g.CurrentWindow;
        if (!window) return;

        // State - TODO: global for now, make per-window
        static bool   scrolling = false;
        static ImVec2 press_pos = ImVec2(0, 0);
        static float  vy = 0.0f;

        const bool hovered_anyhow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool down = io.MouseDown[0];
        const bool up = !down;

        // Begin potential gesture
        if (!scrolling && hovered_anyhow && ImGui::IsMouseClicked(0))
        {
            press_pos = io.MousePos;
            vy = 0.0f;
        }

        // Decide when to take over
        if (!scrolling && down && hovered_anyhow)
        {
            const float dy = io.MousePos.y - press_pos.y;
            if (fabsf(dy) >= drag_threshold)
            {
                // Take over: cancel the currently active item so it won't trigger on release
                if (g.ActiveId != 0)
                    ImGui::ClearActiveID();

                // Optionally set ourselves as active (not strictly required but keeps ID bookkeeping tidy)
                ImGuiID id = window->GetID("##swipe_scroll_captor");
                ImGui::SetActiveID(id, window);

                scrolling = true;
            }
        }

        // While dragging, update velocity and scroll
        if (scrolling && down)
        {
            vy = -io.MouseDelta.y; // swipe up -> scroll down; adjust sign to taste
            if (vy != 0.0f)
                ImGui::SetScrollY(window, window->Scroll.y + vy);
        }

        // On release, keep inertia; while idle, decay velocity
        if (scrolling && up)
        {
            // free active id if we grabbed it
            if (g.ActiveId != 0 && g.ActiveId == window->GetID("##swipe_scroll_captor"))
                ImGui::ClearActiveID();

            // keep scrolling with decay until velocity dies
            if (fabsf(vy) < 0.01f)
            {
                vy = 0.0f;
                scrolling = false;
            }
        }

        // Apply inertial decay when not dragging
        if (vy != 0.0f && !down)
        {
            ImGui::SetScrollY(window, window->Scroll.y + vy);
            vy *= decay;
            if (fabsf(vy) < 0.01f)
                vy = 0.0f;
        }
    }

    inline bool DrawMessageBox(
        const char* title,
        bool* show,
        std::string_view reason,
        ImVec2 size = ImVec2(350.0f, 170.0f),
        const char* ok_label = "Close")
    {
        if (!show || !*show)
            return false;

        if (reason.empty())
            reason = "Undetermined";

        if (!ImGui::IsPopupOpen(title))
            ImGui::OpenPopup(title);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        const ImGuiStyle& style = ImGui::GetStyle();

        const float width = (size.x > 0.0f) ? size.x : 420.0f;
        const float wrap_width = std::max(1.0f, width - style.WindowPadding.x * 2.0f);

        const ImVec2 text_size = ImGui::CalcTextSize(
            reason.data(), reason.data() + reason.size(),
            false, wrap_width);

        const float button_h = ImGui::GetFrameHeight();
        const float needed_h =
            style.WindowPadding.y * 2.0f +
            text_size.y +
            style.ItemSpacing.y +
            button_h + 50;

        const float max_h =
            (size.y > 0.0f) ? size.y : (vp->WorkSize.y * 0.9f);

        const float window_h = std::min(needed_h, max_h);
        const bool needs_scroll = (needed_h > window_h + 0.5f);

        ImGui::SetNextWindowSize(ImVec2(width, window_h), ImGuiCond_Always);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::BeginPopupModal(title, show, flags))
            return false;

        if (needs_scroll)
        {
            const float child_h = std::max(
                0.0f,
                ImGui::GetContentRegionAvail().y - (style.ItemSpacing.y + button_h + 20));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild("##error_msg", ImVec2(0.0f, child_h), false);

            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(reason.data(), reason.data() + reason.size());
            ImGui::PopTextWrapPos();

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        else
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(reason.data(), reason.data() + reason.size());
            ImGui::PopTextWrapPos();
        }

        ImGui::Dummy(ImVec2(0, 10));

        const float button_w = 110.0f;
        const float avail_x = ImGui::GetContentRegionAvail().x;
        const float button_x = (avail_x - button_w) * 0.5f;
        if (button_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + button_x);

        bool accepted = ImGui::Button(ok_label, ImVec2(button_w, 0.0f));
        accepted = accepted || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

        if (accepted || cancelled)
        {
            *show = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        return true;
    }
}


// Global Initial(&var) helper method for only updating 'var' when 
// updatePointerValues() gets called.
//
// Works by passing a separate placeholder value to ImGui, and copying that data to
// the pointer when updatePointerValues() called

template<typename T>
T* Initial(T* ptr)
{
    if (ImGui::starting_map.count(ptr) == 0)
        ImGui::starting_map[ptr] = { new T(*ptr), sizeof(T) };

    return reinterpret_cast<T*>(ImGui::starting_map[ptr].initial);
}
