#include <bitloop/platform/platform.h>
#include <bitloop/core/main_window.h>
#include <bitloop/core/project_worker.h>
#include <bitloop/util/text_util.h>
//#include <bitloop/imguix/imgui_debug_ui.h>

#include <filesystem>

#ifndef BL_WEB_BUILD
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <charconv>
#include <cctype>
#endif

/// TODO
/// > Make tab buttons always visible, but the panels hidden on startup (customizable)
/// > When panels visible, put a "Vertical Collapse" button


BL_BEGIN_NS

MainWindow* MainWindow::singleton = nullptr;

ImDebugLog project_log;
ImDebugLog debug_log;

void ImDebugPrint(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    debug_log.vlog(fmt, ap);
    va_end(ap);
}

#ifndef BL_WEB_BUILD
struct Token
{
    enum class Kind { Lit, Index, Alias } kind = Kind::Lit;
    std::string lit; 
    int pad_width = 0;
};

// returns npos if not found
static size_t find_insensitive(std::string_view txt, std::string_view substr_txt, size_t from)
{
    if (substr_txt.empty()) return from;
    if (substr_txt.size() > txt.size()) return std::string_view::npos;

    for (size_t i = from; i + substr_txt.size() <= txt.size(); ++i)
    {
        bool ok = true;
        for (size_t j = 0; j < substr_txt.size(); ++j)
        {
            if (!text::eqInsensitive(txt[i + j], substr_txt[j])) { ok = false; break; }
        }
        if (ok) return i;
    }
    return std::string_view::npos;
}
static std::string normalize_extension(std::string_view ext)
{
    if (ext.empty()) return {};
    if (ext.front() == '.') return std::string(ext);
    std::string out;
    out.reserve(ext.size() + 1);
    out.push_back('.');
    out.append(ext.begin(), ext.end());
    return out;
}
static void push_lit(std::vector<Token>& toks, std::string&& s)
{
    if (s.empty()) return;
    if (!toks.empty() && toks.back().kind == Token::Kind::Lit)
        toks.back().lit += s;
    else
    {
        Token t;
        t.kind = Token::Kind::Lit;
        t.lit = std::move(s);
        toks.push_back(std::move(t));
    }
}
static std::vector<Token> tokenize_segment(std::string_view fmt, bool& have_index_global)
{
    // tokenizes one path segment: %s => alias wildcard; first %d/%0Nd => index (global); %% => '%'
    std::vector<Token> toks;
    std::string cur;
    cur.reserve(fmt.size());

    for (size_t i = 0; i < fmt.size(); ++i)
    {
        char c = fmt[i];
        if (c != '%' || i + 1 >= fmt.size())
        {
            cur.push_back(c);
            continue;
        }

        char n = fmt[i + 1];

        if (n == '%') { cur.push_back('%'); ++i; continue; }

        if (n == 's')
        {
            push_lit(toks, std::move(cur));
            cur.clear();
            Token t; t.kind = Token::Kind::Alias;
            toks.push_back(std::move(t));
            ++i;
            continue;
        }

        if (!have_index_global)
        {
            if (n == 'd')
            {
                push_lit(toks, std::move(cur));
                cur.clear();
                Token t; t.kind = Token::Kind::Index; t.pad_width = 0;
                toks.push_back(std::move(t));
                have_index_global = true;
                ++i;
                continue;
            }

            if (n == '0')
            {
                size_t j = i + 2;
                int width = 0;
                bool any = false;
                while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j])))
                {
                    any = true;
                    width = width * 10 + (fmt[j] - '0');
                    ++j;
                }
                if (any && j < fmt.size() && fmt[j] == 'd')
                {
                    push_lit(toks, std::move(cur));
                    cur.clear();
                    Token t; t.kind = Token::Kind::Index; t.pad_width = width;
                    toks.push_back(std::move(t));
                    have_index_global = true;
                    i = j; // consume through 'd'
                    continue;
                }
            }
        }

        // unknown or extra %d when already used: treat '%' literally
        cur.push_back('%');
    }

    push_lit(toks, std::move(cur));
    return toks;
}
static bool match_segment_and_extract_index(const std::vector<Token>& toks, std::string_view segment, bool& idx_set_inout, int& idx_inout)
{
    // backtracking matcher for ONE segment. Alias is wildcard; Index parses digits and outputs idx
    struct State { size_t ti, pos; bool idx_set; int idx; };
    std::vector<State> stack;
    stack.push_back({ 0, 0, idx_set_inout, idx_inout });

    auto parse_int = [](std::string_view digits, int& val) -> bool {
        if (digits.empty()) return false;
        int x = 0;
        auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), x);
        if (ec != std::errc{} || ptr != digits.data() + digits.size()) return false;
        val = x;
        return true;

    };
    while (!stack.empty())
    {
        State st = stack.back();
        stack.pop_back();

        if (st.ti == toks.size())
        {
            if (st.pos == segment.size())
            {
                idx_set_inout = st.idx_set;
                idx_inout = st.idx;
                return true;
            }
            continue;
        }

        const Token& tk = toks[st.ti];

        if (tk.kind == Token::Kind::Lit)
        {
            std::string_view lit = tk.lit;
            if (st.pos + lit.size() > segment.size()) continue;
            if (!text::eqInsensitive(segment.substr(st.pos, lit.size()), lit)) continue;
            stack.push_back({ st.ti + 1, st.pos + lit.size(), st.idx_set, st.idx });
            continue;
        }

        if (tk.kind == Token::Kind::Index)
        {
            // consume digits (try all splits, longest-first)
            size_t p = st.pos;
            size_t max = p;
            while (max < segment.size() && segment[max] >= '0' && segment[max] <= '9')
                ++max;

            if (max == p) continue;

            for (size_t end = max; end > p; --end)
            {
                int idx = 0;
                if (!parse_int(segment.substr(p, end - p), idx)) continue;
                stack.push_back({ st.ti + 1, end, true, idx });
            }
            continue;
        }

        // alias wildcard: consume until next literal (or end)
        size_t next_lit_i = st.ti + 1;
        while (next_lit_i < toks.size() && toks[next_lit_i].kind != Token::Kind::Lit)
            ++next_lit_i;

        if (next_lit_i >= toks.size())
        {
            stack.push_back({ st.ti + 1, segment.size(), st.idx_set, st.idx });
            continue;
        }

        std::string_view next_lit = toks[next_lit_i].lit;
        size_t search_from = st.pos;

        while (true)
        {
            size_t occ = find_insensitive(segment, next_lit, search_from);
            if (occ == std::string_view::npos) break;
            stack.push_back({ st.ti + 1, occ, st.idx_set, st.idx });
            search_from = occ + 1;
        }
    }

    return false;
}
static bool path_contains(const std::filesystem::path& root, const std::filesystem::path& p)
{
    std::error_code ec1, ec2;
    std::filesystem::path pc = std::filesystem::weakly_canonical(p, ec1);
    std::filesystem::path rootc = std::filesystem::weakly_canonical(root, ec2);

    if (ec1 || ec2)
    {
        pc = p.lexically_normal();
        rootc = root.lexically_normal();
    }

    if (pc == rootc) return true;
    std::filesystem::path rel = pc.lexically_relative(rootc);

    auto s = rel.native();
    return !s.empty() && !(s.size() >= 2 && s[0] == '.' && s[1] == '.');
}
static bool ensure_parent_directories_exist(const std::filesystem::path& file_path, std::error_code& ec)
{
    ec.clear();
    const std::filesystem::path parent = file_path.parent_path();
    if (parent.empty()) return true; // nothing to create
    return std::filesystem::create_directories(parent, ec);
}

// returns max index matched by rel_format under base_dir
// %s (alias) is treated as wildcard, and extension is ignored (matches by stem)
static int getHighestCaptureIndex(std::string_view base_dir, std::string_view rel_format)
{
    std::filesystem::path fmt_path{ std::string(rel_format) };

    // build pattern segments (directories + filename STEM) from rel_format
    std::vector<std::string> pattern_segs;
    pattern_segs.reserve(8);
    {
        std::filesystem::path pdir = fmt_path.parent_path();
        for (const auto& part : pdir) pattern_segs.push_back(part.string());

        // ignore extension: use stem
        std::filesystem::path fname = fmt_path.filename();
        pattern_segs.push_back(fname.stem().string());
    }

    // find unchanging anchor directory: longest leading directory prefix with no '%' in the segment
    std::filesystem::path anchor_rel;
    size_t anchor_count = 0; // number of segments in anchor
    for (; anchor_count + 1 < pattern_segs.size(); ++anchor_count) // +1 to exclude filename seg
    {
        const std::string& seg = pattern_segs[anchor_count];
        if (seg.find('%') != std::string::npos) break;
        anchor_rel /= seg;
    }

    // remaining pattern (relative to scan_root)
    std::vector<std::string> rem_pattern(
        pattern_segs.begin() + static_cast<std::ptrdiff_t>(anchor_count),
        pattern_segs.end());

    // tokenize remaining pattern segments (global: only first %d is Index).
    bool have_index_global = false;
    size_t index_seg = static_cast<size_t>(-1); // segment index (within rem_pattern) that introduced %d
    std::vector<std::vector<Token>> rem_tokens;
    rem_tokens.reserve(rem_pattern.size());

    for (size_t i = 0; i < rem_pattern.size(); ++i)
    {
        const bool before = have_index_global;
        rem_tokens.push_back(tokenize_segment(rem_pattern[i], have_index_global));
        if (!before && have_index_global)
            index_seg = i;
    }

    // no %d means caller intends overwrite semantics.
    if (!have_index_global)
        return -1;

    // only require matching up to (and including) the segment containing the first %d.
    const size_t match_depth = (index_seg == static_cast<size_t>(-1)) ? rem_tokens.size() : (index_seg + 1);

    // compute scan root directory
    const bool fmt_abs = fmt_path.is_absolute();
    std::filesystem::path scan_root = fmt_abs
        ? anchor_rel
        : (std::filesystem::path(std::string(base_dir)) / anchor_rel);

    std::error_code ec;
    if (!std::filesystem::exists(scan_root, ec) || ec)
        return 0;

    const bool need_recursive = (rem_tokens.size() > 1); // wildcard directories possible
    int max_idx = 0;

    auto try_match_rel_path = [&](const std::filesystem::path& full_path) {
        std::filesystem::path rel = full_path.lexically_relative(scan_root);
        if (rel.empty()) return;

        // build candidate segments: directories + filename STEM
        std::vector<std::string> cand;
        cand.reserve(8);

        // count components to quickly reject mismatched depths
        std::vector<std::filesystem::path> comps;
        for (const auto& part : rel) comps.push_back(part);
        if (comps.empty()) return;

        for (size_t i = 0; i + 1 < comps.size(); ++i)
            cand.push_back(comps[i].string());

        // last component: filename stem (ignore extension)
        cand.push_back(comps.back().stem().string());

        if (cand.size() < match_depth)
            return;

        bool idx_set = false;
        int idx = 0;

        for (size_t i = 0; i < match_depth; ++i)
        {
            if (!match_segment_and_extract_index(rem_tokens[i], cand[i], idx_set, idx))
                return;
        }

        if (idx_set && idx > max_idx)
            max_idx = idx;
    };

    if (!need_recursive)
    {
        std::filesystem::directory_iterator it(
            scan_root, std::filesystem::directory_options::skip_permission_denied, ec);

        if (ec) return 0;

        for (const auto& entry : it)
        {
            std::error_code ec2;
            if (!entry.is_regular_file(ec2) || ec2) continue;
            try_match_rel_path(entry.path());
        }
    }
    else
    {
        std::filesystem::recursive_directory_iterator it(
            scan_root, std::filesystem::directory_options::skip_permission_denied, ec);

        if (ec) return 0;

        for (const auto& entry : it)
        {
            std::error_code ec2;
            if (!entry.is_regular_file(ec2) || ec2) continue;
            try_match_rel_path(entry.path());
        }
    }

    return max_idx;
}

// expand rel_format with index + alias, append fallback_extension only if format filename has no extension
std::string constructCapturePath(std::string_view rel_format, int index, std::string_view alias, std::string_view fallback_extension = ".webp")
{
    std::filesystem::path fmt_path{ std::string(rel_format) };
    const bool fmt_has_ext = fmt_path.filename().has_extension();

    std::string out;
    out.reserve(rel_format.size() + alias.size() + 32);

    auto append_padded_index = [&](int w) {
        std::string num = std::to_string(index);
        if (w > 0 && static_cast<int>(num.size()) < w)
            num.insert(num.begin(), static_cast<size_t>(w - num.size()), '0');
        out += num;
    };

    for (size_t i = 0; i < rel_format.size(); ++i)
    {
        char c = rel_format[i];
        if (c != '%' || i + 1 >= rel_format.size()) { out.push_back(c); continue; }

        char n = rel_format[i + 1];

        if (n == '%') { out.push_back('%'); ++i; continue; }
        if (n == 's') { out.append(alias.begin(), alias.end()); ++i; continue; }
        if (n == 'd') { append_padded_index(0); ++i; continue; }

        if (n == '0')
        {
            size_t j = i + 2;
            int width = 0;
            bool any = false;
            while (j < rel_format.size() && std::isdigit(static_cast<unsigned char>(rel_format[j])))
            {
                any = true;
                width = width * 10 + (rel_format[j] - '0');
                ++j;
            }
            if (any && j < rel_format.size() && rel_format[j] == 'd')
            {
                append_padded_index(width);
                i = j; // consume through 'd'
                continue;
            }
        }

        // unknown: treat '%' literally
        out.push_back('%');
    }

    if (!fmt_has_ext)
        out += normalize_extension(fallback_extension);

    return std::filesystem::path(out).lexically_normal().string();
}
std::string getPreferredCapturesDirectory() {
    std::filesystem::path path = platform()->executableDir();
    std::filesystem::path project_root = std::filesystem::path(CMAKE_SOURCE_DIR).lexically_normal();

    // If executable dir lives inside the cmake project dir, clamp to root cmake project dir
    if (path_contains(project_root, path)) {
        path = project_root;
    }
    else {
        std::filesystem::path trimmed;
        if (!project_worker()->getCurrentProject()) {
            blBreak();
        }
        std::string active_sim_name =
            project_worker()->getCurrentProject()->getProjectInfo()->name;

        for (const auto& part : path) {
            trimmed /= part;
            if (!active_sim_name.empty() && part == active_sim_name) break;
        }
    }

    path /= "captures";
    return path.lexically_normal().string();
}

std::filesystem::path MainWindow::getProjectSnapshotsDir()
{
    namespace fs = std::filesystem;
    fs::path dir = getPreferredCapturesDirectory();                        // e.g. "C:/dev/bitloop-gallery/captures/"
    dir /= project_worker()->getCurrentProject()->getProjectInfo()->name;  // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/"
    dir /= "image";                                                        // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/image/"
    return dir;
}
std::filesystem::path MainWindow::getProjectVideosDir()
{
    namespace fs = std::filesystem;
    fs::path dir = getPreferredCapturesDirectory();                        // e.g. "C:/dev/bitloop-gallery/captures/"
    dir /= project_worker()->getCurrentProject()->getProjectInfo()->name;  // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/"
    dir /= "video";                                                        // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/video/"
    return dir;
}
std::filesystem::path MainWindow::getProjectAnimationsDir()
{
    namespace fs = std::filesystem;
    fs::path dir = getPreferredCapturesDirectory();                        // e.g. "C:/dev/bitloop-gallery/captures/"
    dir /= project_worker()->getCurrentProject()->getProjectInfo()->name;  // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/"
    dir /= "animation";                                                    // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/animation/"
    return dir;
}
#endif

static bool _isEditingUI()
{
    ImGuiID active_id = ImGui::GetActiveID();

    static ImGuiID old_active_id = 0;
    ImGuiID lagged_active_id = active_id ? active_id : old_active_id;

    old_active_id = active_id;

    if (lagged_active_id == 0)
        return false;

    static ImGuiID viewport_id = ImHashStr("Viewport");
    return (lagged_active_id != ImGui::FindWindowByID(viewport_id)->MoveId);
}
bool MainWindow::isEditingUI()
{
    return is_editing_ui;
}

void MainWindow::init()
{
    ImGui::LoadIniSettingsFromMemory("");

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Always initializes window on first call
    checkChangedDPR();

    updateActivePreset();
    canvas.create(platform()->dpr());
    //canvas.create(5.0);
    //canvas.create(0.75);

    settings_panel.init();
}

void MainWindow::checkChangedDPR()
{
    platform()->update();

    static float last_dpr = -1.0f;
    if (std::fabs(platform()->dpr() - last_dpr) > 0.01f)
    {
        // DPR changed
        last_dpr = platform()->dpr();

        initStyles();
        initFonts();
    }
}

void MainWindow::initStyles()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // base sizes
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.PopupRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 6.0f;

    style.ScrollbarRounding = 6.0f * platform()->thumbScale();
    style.ScrollbarSize = 20.0f * platform()->thumbScale(0.85f);
    style.GrabMinSize = 35.0f * platform()->thumbScale(0.85f);

    //style.ScrollbarRounding = platform()->isMobile() ? 12.0f : 6.0f; // Extra scrollbar size for mobile
    //style.ScrollbarSize = platform()->isMobile() ? 30.0f : 20.0f;    // Extra scrollbar size for mobile

    // update by dpr
    style.ScaleAllSizes(platform()->dpr());

    // colors
    ImVec4* colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);                  // Light grey text for readability
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.54f, 1.00f);          // Subtle grey for disabled text
    colors[ImGuiCol_WindowBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);              // Dark background with a hint of blue
    colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);               // Slightly lighter for child elements
    colors[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);               // Popup background
    colors[ImGuiCol_Border] = ImVec4(0.28f, 0.29f, 0.30f, 0.60f);                // Soft border color
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);          // No border shadow
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);               // Frame background
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.26f, 1.00f);        // Frame hover effect
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.28f, 1.00f);         // Active frame background
    colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);               // Title background
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);         // Active title background
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);      // Collapsed title background
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);             // Menu bar background
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);           // Scrollbar background
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.26f, 0.28f, 1.00f);         // Dark accent for scrollbar grab
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.30f, 0.32f, 1.00f);  // Scrollbar grab hover
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.32f, 0.34f, 0.36f, 1.00f);   // Scrollbar grab active
    colors[ImGuiCol_CheckMark] = ImVec4(0.46f, 0.56f, 0.66f, 1.00f);             // Dark blue checkmark
    colors[ImGuiCol_SliderGrab] = ImVec4(0.36f, 0.46f, 0.56f, 1.00f);            // Dark blue slider grab
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.50f, 0.60f, 1.00f);      // Active slider grab
    colors[ImGuiCol_Button] = ImVec4(0.24f, 0.34f, 0.44f, 1.00f);                // Dark blue button
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.38f, 0.48f, 1.00f);         // Button hover effect
    colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.42f, 0.52f, 1.00f);          // Active button
    colors[ImGuiCol_Header] = ImVec4(0.24f, 0.34f, 0.44f, 1.00f);                // Header color similar to button
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.38f, 0.48f, 1.00f);         // Header hover effect
    colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.42f, 0.52f, 1.00f);          // Active header
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.29f, 0.30f, 1.00f);             // Separator color
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.46f, 0.56f, 0.66f, 1.00f);      // Hover effect for separator
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.46f, 0.56f, 0.66f, 1.00f);       // Active separator
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.36f, 0.46f, 0.56f, 1.00f);            // Resize grip
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.50f, 0.60f, 1.00f);     // Hover effect for resize grip
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.44f, 0.54f, 0.64f, 1.00f);      // Active resize grip
    colors[ImGuiCol_Tab] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);                   // Inactive tab
    colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.38f, 0.48f, 1.00f);            // Hover effect for tab
    colors[ImGuiCol_TabActive] = ImVec4(0.24f, 0.34f, 0.44f, 1.00f);             // Active tab color
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);          // Unfocused tab
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.24f, 0.34f, 0.44f, 1.00f);    // Active but unfocused tab
    colors[ImGuiCol_PlotLines] = ImVec4(0.46f, 0.56f, 0.66f, 1.00f);             // Plot lines
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.46f, 0.56f, 0.66f, 1.00f);      // Hover effect for plot lines
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.36f, 0.46f, 0.56f, 1.00f);         // Histogram color
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.40f, 0.50f, 0.60f, 1.00f);  // Hover effect for histogram
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);         // Table header background
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.28f, 0.29f, 0.30f, 1.00f);     // Strong border for tables
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.24f, 0.25f, 0.26f, 1.00f);      // Light border for tables
    colors[ImGuiCol_TableRowBg] = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);            // Table row background
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.22f, 0.24f, 0.26f, 1.00f);         // Alternate row background
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.24f, 0.34f, 0.64f, 0.85f);        // Selected text background
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.46f, 0.56f, 0.66f, 0.90f);        // Drag and drop target
    colors[ImGuiCol_NavHighlight] = ImVec4(0.46f, 0.56f, 0.66f, 1.00f);          // Navigation highlight
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f); // Windowing highlight
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);     // Dim background for windowing
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);      // Dim background for modal windows
}
void MainWindow::initFonts()
{
    // Update FreeType fonts
    ImGuiIO& io = ImGui::GetIO();
    
    float base_pt = 16.0f;
    std::string font_path = platform()->path("/data/fonts/DroidSans.ttf");
    std::string font_path_mono = platform()->path("/data/fonts/UbuntuMono.ttf");
    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 3;
    
    io.Fonts->Clear();
    main_font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), base_pt * platform()->dpr() * platform()->fontScale(), &config);
    mono_font = io.Fonts->AddFontFromFileTTF(font_path_mono.c_str(), base_pt * platform()->dpr() * platform()->fontScale(), &config);
    io.Fonts->Build();
}

void MainWindow::populateProjectUI()
{
    if (!project_worker()->hasCurrentProject())
        return;

    BL_TAKE_OWNERSHIP("ui");

    ImGui::BeginPaddedRegion(scale_size(3.0f));
    project_worker()->populateAttributes();
    ImGui::EndPaddedRegion();
}
void MainWindow::populateOverlay()
{
    BL_TAKE_OWNERSHIP("ui");

    project_worker()->populateOverlay();
}

std::string MainWindow::prepareFullCapturePath(
    const CapturePreset& preset,
    std::filesystem::path base_dir,
    const char* rel_path_fmt,
    int file_idx,
    const char* fallback_extension)
{
    namespace fs = std::filesystem;

    fs::path rel_filepath = rel_path_fmt;

    std::string base_filename = rel_filepath.filename().string();     // e.g. "seahorse_valley"

    #ifdef BL_WEB_BUILD
    (void)base_dir; (void)file_idx; (void)rel_path_fmt; (void)fallback_extension;
    /// todo: Use capture format for name
    std::string qualified_filename = (base_filename + '_') + preset.alias_cstr(); // e.g. "seahorse_valley_pixel9a"

    if (rel_filepath.has_extension())
        return qualified_filename;
    else
        return qualified_filename + fallback_extension;
    #else
    // organize by project name, relative to project capture dir                                                         
    std::string filled_rel_filepath = constructCapturePath(rel_path_fmt, file_idx, preset.getAlias(), fallback_extension);
    fs::path filled_full_filepath = base_dir / filled_rel_filepath;

    std::error_code ec;
    ensure_parent_directories_exist(filled_full_filepath, ec);

    return filled_full_filepath.string();
    #endif
}

void MainWindow::queueBeginSnapshot(const SnapshotPresetList& presets, std::string_view rel_path_fmt, int request_id)
{
    capture_context.queueBeginSnapshot(presets, rel_path_fmt, request_id);
}
void MainWindow::queueBeginRecording()
{
    capture_context.queueBeginRecording();
}
void MainWindow::queueEndRecording()
{
    capture_context.queueEndRecording();
}
void MainWindow::beginRecording(const CapturePreset& preset, const char* rel_path_fmt)
{
    namespace fs = std::filesystem;

    CaptureManager& capture_manager = capture_context.manager();
    SettingsConfig& settings = capture_context.settings();

    assert(!capture_manager.isRecording());
    assert(!capture_manager.isSnapshotting());

    snapshot.enabled = false;
    record.toggled = true;
    //record.enabled = true; // keep record button enabled. Clicking again ends recording

    // Mainly used for snapshot lists, but set anyway for consistency
    capture_context.active_capture_rel_path_fmt = rel_path_fmt;

    // don't capture until simulation is ready to draw at the new resolution
    capture_context.new_frame_prepared = false;

    // If not web, decide on save path
    #ifndef BL_WEB_BUILD
    fs::path capture_dir;
    fs::path filename;

    switch ((CaptureFormat)settings.getRecordFormat())
    {
        #if BITLOOP_FFMPEG_ENABLED
        #if BITLOOP_FFMPEG_X265_ENABLED
        case CaptureFormat::x265:
        #endif
        case CaptureFormat::x264:
        {

            capture_dir    = getProjectVideosDir();
            int file_idx   = getHighestCaptureIndex(capture_dir.string(), rel_path_fmt) + 1;
            filename       = prepareFullCapturePath(preset, capture_dir, rel_path_fmt, file_idx, ".mp4");
        }
        break;
        #endif

        case CaptureFormat::WEBP_VIDEO:
        {
            capture_dir    = getProjectAnimationsDir();
            int file_idx   = getHighestCaptureIndex(capture_dir.string(), rel_path_fmt) + 1;
            filename       = prepareFullCapturePath(preset, capture_dir, rel_path_fmt, file_idx, ".webp");
        }
        break;
    }
    #endif

    CaptureConfig config;
    config.format             =       settings.getRecordFormat();
    config.resolution         =       preset.getResolution();
    config.fps                =       settings.record_fps;
    config.record_frame_count =       settings.record_frame_count;
    config.quality            = (f32) settings.record_quality;

    if (config.format == CaptureFormat::WEBP_VIDEO)
    {
        config.lossless = settings.record_webp_mode != WebPCompressionMode::Lossy;
        config.near_lossless = settings.record_webp_mode == WebPCompressionMode::NearLossless ? settings.record_near_lossless : 100;
        if (config.lossless)
            config.quality = (f32)settings.record_webp_effort;
    }

    #if BITLOOP_FFMPEG_ENABLED
    config.bitrate = settings.record_bitrate;
    #endif

    #ifndef BL_WEB_BUILD
    config.filename = filename.string();
    #endif

    capture_context.enabled_capture_presets = SnapshotPresetList(preset);
    capture_context.enabled_capture_presets_current_idx = 0;
    capture_context.active_snapshot_preset_request_id = 0; // no assigned callback for recording

    /// TODO: check for any ffmpeg/webp initialization errors
    capture_manager.startCapture(config);
}

void MainWindow::_beginSnapshot(const char* filepath [[maybe_unused]], IVec2 res)
{
    // begin snapshot with provided args (lowest level, no awareness of presets)

    assert(!capture_context.manager().isRecording());
    assert(!capture_context.manager().isSnapshotting());

    // disable everything until snapshot complete
    snapshot.enabled = false;
    record.enabled = false;
    pause.enabled = false;
    stop.enabled = false; 

    snapshot.toggled = true;
    record.toggled = false;

    // don't capture until simulation is ready to draw at the new resolution
    capture_context.new_frame_prepared = false;

    CaptureConfig config;
    config.format = capture_context.settings().getSnapshotFormat();
    config.resolution = res;
    config.quality = 100.0f;
    config.lossless = true;

    #ifndef BL_WEB_BUILD
    config.filename = filepath;
    #endif

    const auto project = project_worker()->getCurrentProject();
    const int project_uid = project->getProjectInfo()->sim_uid;
    project_worker()->requestSnapshot(
        [this, config, project_uid](std::optional<SnapshotFrame> output, std::string error) mutable
        {
            const auto current = project_worker()->getCurrentProject();
            if (!current || current->getProjectInfo()->sim_uid != project_uid)
                error = "The project changed before its snapshot was captured";
            if (!error.empty())
            {
                blPrint() << error;
                capture_context.is_snapshotting_list = false;
                capture_context.manager().failCapture();
                return;
            }
            if (output) config.resolution = output->resolution;
            capture_context.prepared_snapshot = std::move(output);
            capture_context.new_frame_prepared = false;
            capture_context.manager().startCapture(config);
        });
}
void MainWindow::beginSnapshot(const CapturePreset& preset, const char* rel_path_fmt, int file_idx)
{
    // begin snapshot using provided preset (ssaa/sharpen falls back to global defaults if "unset")

    /// todo: Check extension is valid image format
    std::filesystem::path base_dir;
    #ifdef BL_WEB_BUILD
    base_dir = "";
    #else
    base_dir = getProjectSnapshotsDir();
    #endif

    std::string full_path = prepareFullCapturePath(preset, base_dir, rel_path_fmt, file_idx, ".webp");
    _beginSnapshot(full_path.c_str(), preset.getResolution()/*, ssaa, sharpen*/);
}
void MainWindow::beginSnapshotList(const SnapshotPresetList& presets, const char* rel_path_fmt, int request_id)
{
    capture_context.enabled_capture_presets = presets.filterSupported(capture_context.settings().default_ssaa);

    if (capture_context.enabled_capture_presets.size() == 0)
    {
        blPrint("No supported presets to snapshot. Aborting.\n");
        return;
    }
    
    capture_context.is_snapshotting_list = true;
    capture_context.enabled_capture_presets_current_idx = 0;
    capture_context.active_capture_rel_path_fmt = rel_path_fmt;
    capture_context.active_snapshot_preset_request_id = request_id;

    // Determine snapshot group index for this batch for the target directory (ignored on web)
    #ifndef BL_WEB_BUILD
    namespace fs = std::filesystem;
    fs::path rel_filepath = rel_path_fmt;
    fs::path dir = getPreferredCapturesDirectory();                        // e.g. "C:/dev/bitloop-gallery/captures/"
    dir /= project_worker()->getCurrentProject()->getProjectInfo()->name;  // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/"
    dir /= "image";                                                        // e.g. "C:/dev/bitloop-gallery/captures/Mandelbrot/image/"
    capture_context.shared_batch_fileindex = getHighestCaptureIndex(dir.string(), rel_path_fmt) + 1;
    #else
    capture_context.shared_batch_fileindex = 0;
    #endif

    beginSnapshot(
        capture_context.enabled_capture_presets[capture_context.enabled_capture_presets_current_idx],
        capture_context.active_capture_rel_path_fmt.c_str(),
        capture_context.shared_batch_fileindex);
}
void MainWindow::endRecording()
{
    // signal to capture_manager to finish, so isCaptureToMemoryComplete()==true below
    CaptureManager& capture_manager = capture_context.manager();
    if (capture_manager.isRecording())
    {
        // called immediately when end recording signalled, even if current frame hasn't finished encoding
        record.toggled = false;

        // disable everything while finalizing capture
        play.enabled = false;
        pause.enabled = false;
        stop.enabled = false;
        record.enabled = false;
        snapshot.enabled = false;

        capture_manager.finalizeCapture();
    }
}

void MainWindow::checkCaptureComplete()
{
    static bool show_error_box = false;
    ImGui::DrawMessageBox("Capture Error", &show_error_box, "Reason: Undetermined", scale_size(350, 170));

    CaptureManager& capture_manager = capture_context.manager();

    bool captured_to_memory = false;
    bool error = false;
    if (capture_manager.handleCaptureComplete(&captured_to_memory, &error))
    {
        if (error)
        {
            show_error_box = true;
        }
        else
        {
            if (captured_to_memory)
            {
                // capture to memory is currently always webp... (still OR webp animation)
                bytebuf data;
                capture_manager.takeCompletedCaptureFromMemory(data);

                #ifdef BL_WEB_BUILD
                #ifdef __EMSCRIPTEN__ // do nothing if simulating web with Emscripten, otherwise in real web build, trigger download
                platform()->download_snapshot_webp(data, "snapshot.webp");
                #endif
                #else
                std::ofstream out(capture_manager.filename(), std::ios::out | std::ios::binary);
                out.write((const char*)data.data(), data.size());
                out.close();
                #endif

                // todo: When you include other snapshot types (PNG, JPEG, etc) use a more robust "is snapshot" check
                if (capture_manager.format() == CaptureFormat::WEBP_SNAPSHOT)
                {
                    // do we have another preset to capture?
                    capture_context.enabled_capture_presets_current_idx++;
                    if (capture_context.enabled_capture_presets_current_idx < capture_context.enabled_capture_presets.size())
                    {
                        // todo: maybe queue to begin capture at more suitable time
                        queueMainWindowCommand({ MainWindowCommandType::BEGIN_SNAPSHOT_ACTIVE_PRESET });
                    }
                    else
                    {
                        capture_context.is_snapshotting_list = false;
                    }
                }
            }
            else
            {
                // ffmpeg saving to disk handled automatically by encoder...
            }
        }

        // untoggle
        record.toggled = false;
        snapshot.toggled = false;

        // have we ended recording while the project is still "started/paused" state?
        // or have we ended recording with a hard "stop"?
        bool project_active =
            project_worker()->hasCurrentProject() &&
            project_worker()->getCurrentProject()->isActive();

        if (project_active)
        {
            bool isPaused = project_worker()->getCurrentProject()->isPaused();

            // re-enable
            play.enabled = isPaused;
            pause.enabled = !isPaused;
            stop.enabled = true;
            record.enabled = true;
            snapshot.enabled = true;
        }
        else
        {
            play.enabled = true;
            pause.enabled = false;
            stop.enabled = false;
            record.enabled = true;
            snapshot.enabled = false;
        }
    }
}

void MainWindow::handleCaptureCommand(CaptureCommandEvent e)
{
    switch (e.type)
    {
    case CaptureCommandType::BEGIN_SNAPSHOT_PRESET_LIST:
        beginSnapshotList(
            e.snapshot_args.presets,
            e.snapshot_args.rel_path_fmt.c_str(),
            e.snapshot_args.request_id);
        break;

    case CaptureCommandType::BEGIN_RECORDING:
        beginRecording(capture_context.settings().enabledVideoPreset(), "clip%d_%s");
        break;

    case CaptureCommandType::END_RECORDING:
        endRecording();
        break;
    }
}

void MainWindow::handleCommand(MainWindowCommandEvent e)
{
    // handle commands which can be initiated by both GUI thread & project-worker thread
    switch (e.type)
    {
    case MainWindowCommandType::ON_SELECT_PROJECT:
    {
        std::string window_name = std::any_cast<std::string>(e.payload);
        #ifdef BL_DEBUG
        window_name += " (debug)";
        #endif
        SDL_SetWindowTitle(platform()->sdlWindow(), window_name.c_str());

        // in a "stopped" state to begin with
        play.enabled = true;
        stop.enabled = false;
        pause.enabled = false;

        record.enabled = true;
        snapshot.enabled = false;
    }
    break;

    case MainWindowCommandType::ON_PLAY_PROJECT:
    {
        /// note: could be starting OR resuming project

        // received from project-worker once project succesfully started
        play.enabled = false;
        stop.enabled = true;
        pause.enabled = true;

        record.enabled = true; // may already be recording, but enable in case we're starting new project
        snapshot.enabled = !capture_context.manager().isRecording(); // don't re-enable snapshot button if we're still recording (i.e. resuming)

        static bool first = true;
        if (first)
        {
            // set initial preferred folder once
            #ifndef BL_WEB_BUILD
            capture_context.settings().capture_dir = getPreferredCapturesDirectory();
            #endif

            first = false;
        }
    }
    break;

    case MainWindowCommandType::ON_STOPPED_PROJECT:

        // set states here as well as on toolbar button click, in case
        // the project gets stopped in some other way.
        play.enabled = true;
        pause.enabled = false;

        stop.enabled = false;
        record.toggled = false;
        snapshot.enabled = false;

        // If recording, finalize
        endRecording();
        break;

    case MainWindowCommandType::ON_PAUSED_PROJECT:
        play.enabled = true;
        pause.enabled = false;
        snapshot.enabled = !capture_context.manager().isRecording();
        break;

    case MainWindowCommandType::BEGIN_SNAPSHOT_ACTIVE_PRESET:
    {
        // "private" command triggered on completing a snapshot - triggers a snapshot on the next batch preset
        SnapshotPresetList presets;
        presets.add(capture_context.enabled_capture_presets[capture_context.enabled_capture_presets_current_idx]);
        beginSnapshot(
            capture_context.enabled_capture_presets[capture_context.enabled_capture_presets_current_idx],
            capture_context.active_capture_rel_path_fmt.c_str(),
            capture_context.shared_batch_fileindex);
    }
    break;
    }
}

/// ======== Toolbar ========

void MainWindow::drawToolbarButton(ImDrawList* drawList, ImVec2 pos, ImVec2 size, const char* symbol, ImU32 color)
{
    float cx = pos.x + size.x * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    float r = ImMin(size.x, size.y) * 0.25f;

    if (strcmp(symbol, "play") == 0) {
        ImVec2 p1(cx - r * 0.6f, cy - r);
        ImVec2 p2(cx - r * 0.6f, cy + r);
        ImVec2 p3(cx + r, cy);
        drawList->AddTriangleFilled(p1, p2, p3, color);
    }
    else if (strcmp(symbol, "stop") == 0) {
        drawList->AddRectFilled(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), color);
    }
    else if (strcmp(symbol, "pause") == 0) {
        float w = r * 0.4f;
        drawList->AddRectFilled(ImVec2(cx - r, cy - r), ImVec2(cx - r + w, cy + r), color);
        drawList->AddRectFilled(ImVec2(cx + r - w, cy - r), ImVec2(cx + r, cy + r), color);
    }
    else if (strcmp(symbol, "record") == 0) {
        drawList->AddCircleFilled(ImVec2(cx, cy), r, color);
    }
    else if (strcmp(symbol, "folder") == 0) {
        const float u = ImMin(size.x, size.y);
        const float t = ImClamp(u * 0.06f, 1.0f, 3.0f);
        const ImVec2 outline[] = {
            {cx - u * 0.32f, cy - u * 0.16f}, {cx - u * 0.32f, cy - u * 0.28f},
            {cx - u * 0.08f, cy - u * 0.28f}, {cx + u * 0.04f, cy - u * 0.16f},
            {cx + u * 0.32f, cy - u * 0.16f}, {cx + u * 0.32f, cy + u * 0.25f},
            {cx - u * 0.32f, cy + u * 0.25f}
        };
        drawList->AddPolyline(outline, IM_ARRAYSIZE(outline), color, ImDrawFlags_Closed, t);
        drawList->AddLine(ImVec2(cx - u * 0.32f, cy - u * 0.06f), ImVec2(cx + u * 0.32f, cy - u * 0.06f), color, t);
    }
    else if (strcmp(symbol, "snapshot") == 0) {
        cy += r * 0.2f;

        // Scales with the button rect
        float u = ImMin(size.x, size.y);
        float t = ImClamp(u * 0.06f, 1.0f, 3.0f);   // stroke thickness
        float round = u * 0.10f;                    // body corner radius

        // Camera body
        float body_w = u * 0.62f;
        float body_h = u * 0.46f;
        ImVec2 body_min(cx - body_w * 0.5f, cy - body_h * 0.5f);
        ImVec2 body_max(cx + body_w * 0.5f, cy + body_h * 0.5f);
        drawList->AddRect(body_min, body_max, color, round, 0, t);

        // Top hump (viewfinder housing)
        float hump_w = u * 0.26f;
        float hump_h = u * 0.12f;
        ImVec2 hump_min(body_min.x + u * 0.06f, body_min.y - hump_h + t * 0.5f);
        ImVec2 hump_max(hump_min.x + hump_w, body_min.y + t * 0.5f);
        drawList->AddRect(hump_min, hump_max, color, round * 0.5f, 0, t);

        // Lens (outer ring)
        float lens_r = u * 0.14f;
        drawList->AddCircle(ImVec2(cx, cy), lens_r, color, 0, t);

        // Lens inner accent (tiny dot)
        drawList->AddCircleFilled(ImVec2(cx + lens_r * 0.35f, cy - lens_r * 0.35f),
            ImMax(1.0f, u * 0.02f), color);

        // Shutter button nub (small filled circle on top-right)
        ImVec2 nub(hump_max.x + u * 0.04f, hump_min.y + (hump_h * 0.35f));
        drawList->AddCircleFilled(nub, ImMax(1.0f, u * 0.02f), color);
    }
}
bool MainWindow::toolbarButton(const char* id, const char* symbol, const ToolbarButtonState& state, ImVec2 size, float inactive_alpha)
{
    auto icon_col =
        state.enabled ?
        state.symbolColor :
        ImVec4(state.symbolColor.x, state.symbolColor.y, state.symbolColor.z, inactive_alpha);

    auto bg_col =
        state.enabled ?
        state.bgColor :
        ImVec4(state.bgColor.x, state.bgColor.y, state.bgColor.z, inactive_alpha);

    if (state.toggled)
        bg_col = state.bgColorToggled;

    ImGui::PushStyleColor(ImGuiCol_Button, bg_col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_col);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_col);
    bool pressed = ImGui::Button(id, size);
    ImGui::PopStyleColor(3);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetItemRectMin();
    ImVec2 sz = ImGui::GetItemRectSize();

    drawToolbarButton(drawList, p, sz, symbol, ImGui::ColorConvertFloat4ToU32(icon_col));

    if (state.enabled)
        return pressed;

    return false;
}
void MainWindow::populateToolbar()
{
    //if (platform()->isMobile())
    //    return;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 0));

    ImGui::Spacing();
    ImGui::Spacing();

    float size = scale_size(36.0f);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 toolbarSize(avail.x, size); // Outer frame size
    ImVec4 frameColor = ImVec4(0, 0, 0, 0); // Toolbar background color


    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, frameColor);

    ImGui::BeginChild("ToolbarFrame", toolbarSize, false,
        ImGuiWindowFlags_AlwaysUseWindowPadding |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    // Layout the buttons inside the frame
    if (toolbarButton("##play", "play", play, ImVec2(size, size)))
    {
        // immediately disable play button, handle other button states in handleCommand(..)
        // in response to the project-worker succesfully switching project.
        play.enabled = false;

        if (!project_worker()->getCurrentProject()->isPaused())
        {
            // not paused/resuming - begin recording immediately if record button "on" so we start capturing from the very first frame
            if (record.toggled)
                beginRecording(capture_context.settings().enabledVideoPreset(), "clip%d_%s");
        }

        project_worker()->startProject();
    }

    ImGui::SameLine();
    if (toolbarButton("##pause", "pause", pause, ImVec2(size, size)))
    {
        // immediately disable pause button, handle other button states in handleCommand(..)
        // in response to the project-worker succesfully switching project.
        pause.enabled = false;

        project_worker()->pauseProject();
    }

    ImGui::SameLine();
    if (toolbarButton("##stop", "stop", stop, ImVec2(size, size)))
    {
        // immediately disable stop/pause buttons, handle other button states in handleCommand(..)
        // in response to the project-worker succesfully switching project.
        stop.enabled = false;
        pause.enabled = false;

        project_worker()->stopProject();
    }

    if (!platform()->isMobile())
    {
        ImGui::SameLine();
        if (toolbarButton("##record", "record", record, ImVec2(size, size)))
        {
            if (project_worker()->hasCurrentProject() &&
                project_worker()->getCurrentProject()->isActive())
            {
                // sim already 'active' (and possibly in 'paused' state)
                if (!record.toggled)
                {
                    // Start recording
                    beginRecording(capture_context.settings().enabledVideoPreset(), "clip%d_%s");
                }
                else
                {
                    /// endRecording() signals finalize_requested=true, so recording finalizes in MainWindow::checkCaptureComplete()
                    endRecording();
                }
            }
            else
            {
                // sim not started yet, but toggle state so it auto-begins
                // recording on project started
                record.toggled = !record.toggled;
            }
        }
    }

    ImGui::SameLine();
    if (toolbarButton("##snapshot", "snapshot", snapshot, ImVec2(size, size)))
    {
        beginSnapshotList(capture_context.settings().enabledImagePresets(), "snap%d_%s", 0);
    }

    #ifndef BL_WEB_BUILD
    if (platform()->isDesktopNative())
    {
        ImGui::SameLine();
        const ToolbarButtonState captureFolder = {
            ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.3f, 0.3f, 0.3f, 1.0f), ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
            project_worker()->hasCurrentProject()
        };
        if (toolbarButton("##open_capture_folder", "folder", captureFolder, ImVec2(size, size)))
        {
            const auto directory = getProjectSnapshotsDir().parent_path();
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            std::string message;
            if (error) message = "Could not create the capture folder: " + error.message();
            else if (!platform()->openDirectory(directory)) message = SDL_GetError();
            if (!message.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open capture folder", message.c_str(), platform()->sdlWindow());
        }
        ImGui::SetItemTooltip("Open capture folder");
    }
    #endif

    if (capture_context.settings().show_fps)
    {
        ImGui::SameLine();

        const float pad_y = ImGui::GetStyle().FramePadding.y;
        float text_h = ImGui::GetTextLineHeight();
        float y_off = (size - text_h) * 0.5f;

        // move cursor down within the current line before rendering text
        ImVec2 p = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(p.x, p.y + y_off - pad_y));

        ImGui::Text("FPS: %.1f", worker_fps_timer.getFPS());
    }

    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::Dummy(scale_size(0, 6));
}

/// ======== Project Tree ========

void MainWindow::populateProjectTreeNodeRecursive(ProjectInfoNode& node, int& i, int depth)
{
    ImGui::PushID(node.uid);
    if (node.project_info)
    {
        // Leaf project node
        if (ImGui::Button(node.name.c_str()))
        {
            project_worker()->setActiveProject(node.project_info->sim_uid);
            project_worker()->startProject();
        }
        #if 1
        ImGui::SameLine();
        if (ImGui::Button("[...]"))
        {
            project_worker()->setActiveProject(node.project_info->sim_uid);
        }
        #endif
    }
    else
    {
        int flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (ImGui::CollapsingHeader(node.name.c_str(), flags))
        {
            ImGui::Indent();
            for (size_t child_i = 0; child_i < node.children.size(); child_i++)
            {
                populateProjectTreeNodeRecursive(node.children[child_i], i, depth + 1);
            }
            ImGui::Unindent();
        }
    }
    ImGui::PopID();
}
void MainWindow::populateProjectTree(bool expand_vertical)
{
    if (ProjectBase::projectInfoList().size() <= 1)
        return;

    ImVec2 frameSize = ImVec2(0.0f, expand_vertical ? 0 : 170.0f); // Let height auto-expand
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    ImVec4 dim_bg = ImVec4(bg.x * 0.8f, bg.y * 0.8f, bg.x * 0.8f, bg.w);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

    {
        ImGui::BeginChild("TreeFrame", frameSize, 0, ImGuiChildFlags_AlwaysUseWindowPadding);

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, scale_size(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, scale_size(3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, platform()->thumbScale(0.7f) * scale_size(4.0f)));

        ImGui::PushStyleColor(ImGuiCol_ChildBg, dim_bg);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.5f, 1.0f));

        const float margin = 6.0f;
        const ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 clip_min = win_pos + ImGui::GetWindowContentRegionMin();
        ImVec2 clip_max = win_pos + ImGui::GetWindowContentRegionMax();

        clip_min.x += margin; clip_min.y += margin;
        clip_max.x -= margin; clip_max.y -= margin;

        {
            ImGui::BeginChild("TreeFrameInner", ImVec2(0.0f, 0.0f), true);

            ImGui::BeginPaddedRegion(scale_size(6.0f));
            ImGui::PushClipRect(clip_min, clip_max, true);

            auto& tree = ProjectBase::projectTreeRootInfo();
            int i = 0; // for unique ID's
            for (size_t child_i = 0; child_i < tree.children.size(); child_i++)
            {
                int depth = 0; // track recursion depth
                populateProjectTreeNodeRecursive(tree.children[child_i], i, depth);
            }

            ImGui::PopClipRect();
            ImGui::EndPaddedRegion();

            ImGui::EndChild();
        }


        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        ImGui::EndChild();
    }
    ImGui::PopStyleVar(2);
}

/// ======== Main Window populate ========

bool MainWindow::manageDockingLayout()
{
    // Create a fullscreen dockspace
    ImGuiWindowFlags dockspace_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Make main dock space that fills the entire imgui viewport
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("MainDockSpaceHost", nullptr, dockspace_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    // Detect viewport resize events (in case we need to switch layout)
    static ImVec2 last_viewport_size = ImVec2(0, 0);
    ImVec2 current_viewport_size = ImGui::GetMainViewport()->Size;
    if (current_viewport_size.x != last_viewport_size.x ||
        current_viewport_size.y != last_viewport_size.y)
    {
        update_docking_layout = true;
        last_viewport_size = current_viewport_size;
    }

    // Determine landscape/portrait layout
    vertical_layout = (last_viewport_size.x < last_viewport_size.y);

    // Build initial layout (once)
    if (!initialized || update_docking_layout)
    {
        initialized = true;
        update_docking_layout = false;

        if (ImGui::GetMainViewport()->Size.x <= 0 ||
            ImGui::GetMainViewport()->Size.y <= 0)
        {
            return false;
        }

        // Remove previous dockspace and rebuild
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_sidebar = ImGui::DockBuilderSplitNode(
            dock_main_id,
            vertical_layout ? ImGuiDir_Down : ImGuiDir_Right,
            vertical_layout ? 0.4f : 0.3f,
            nullptr,
            &dock_main_id
        );


        if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock_sidebar))
            n->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;

        bool support_timeline = !vertical_layout && !platform()->isMobile();
        ImGuiID dock_timeline;

        if (support_timeline)
        {
            dock_timeline = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.2f, nullptr, &dock_main_id);
            if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock_timeline))
                n->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
        }
        //else
        //{
        //    ImGui::DockBuilderRemoveNode(dock_timeline);
        //}

        //if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock_main_id))
        //    n->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;

        ImGui::DockBuilderDockWindow("Projects",    dock_sidebar);  // dock to sidebar
        ImGui::DockBuilderDockWindow("Active",      dock_sidebar);  // dock to sidebar
        ImGui::DockBuilderDockWindow("Debug",       dock_sidebar);  // dock to sidebar
        ImGui::DockBuilderDockWindow("Settings",    dock_sidebar);  // dock to sidebar
        if (support_timeline)
            ImGui::DockBuilderDockWindow("Timeline",    dock_timeline); // dock to top of main window
        ImGui::DockBuilderDockWindow("Viewport",    dock_main_id);  // dock to main window
        
        ImGui::DockBuilderFinish(dockspace_id);
    }
    return true;
}
bool MainWindow::focusWindow(const char* id)
{
    // SetWindowFocus doesn't switch focused tab unless second frame, so 
    // never call on first frame and wait for the next call
    static bool first_frame = true;
    bool ret = false;
    if (initialized && !first_frame)
    {
        ImGui::SetWindowFocus(id);
        ret = true;
    }
    first_frame = false;
    return ret;
}

void MainWindow::populateCollapsedLayout()
{
    // Collapse layout
    if (ImGui::Begin("Projects", nullptr, window_flags))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scale_size(6.0f, 6.0f));
        populateToolbar();

        populateProjectTree(true);
        ImGui::SwipeScrollWindow();

        ImGui::PopStyleVar();
    }
    ImGui::End();
    
    if (ImGui::Begin("Active", nullptr, window_flags))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scale_size(8.0f, 8.0f));

        populateToolbar();
        populateProjectUI();

        ImGui::SwipeScrollWindow();
        ImGui::PopStyleVar();
    }

    ImGui::End();
}
void MainWindow::populateExpandedLayout()
{
    // Show both windows
    if (ImGui::Begin("Projects", nullptr, window_flags))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scale_size(6, 6));

        populateToolbar();
        populateProjectTree(false);
        populateProjectUI();

        ImGui::SwipeScrollWindow();
        ImGui::PopStyleVar();
    }

    ImGui::End();
}
void MainWindow::populateViewport(bool& projectDrawn)
{
    {
        std::vector<CaptureCommandEvent> commands = capture_context.takeQueuedCommands();
        for (CaptureCommandEvent& command : commands)
            handleCaptureCommand(command);
    }

    /// todo: Make more thread-safe by only handling commands when we draw? (where both GUI and live buffers are unused)
    ///       As long as you queue commands for project_worker, you're safe here, but if invoke a project callback directly
    ///       e.g. onBeginSnapshot, there's a chance the project could still be mutating live buffer
    if (!command_queue.empty())
    {
        // We don't call populateAttributes() if holding shadow_buffer_mutex,
        // meaning we won't loop over scenes here while processing project commands
        std::vector<MainWindowCommandEvent> commands;
        {
            std::lock_guard<std::mutex> lock(command_mutex);
            commands = std::move(command_queue);
        }

        if (!commands.empty()) {
            for (auto& e : commands)
                handleCommand(e);
        }
    }

    // Always process viewport, even if not visible
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    {
        client_size = ImGui::GetContentRegionAvail();

        CaptureManager& capture_manager = capture_context.manager();
        SettingsConfig& settings = capture_context.settings();

        bool is_capturing = capture_manager.isCapturing();        
        if (!is_capturing)
        {
            // if capturing, lock "viewport" preset resolution until finished
            SnapshotPresetList& presets = capture_context.presetManager().allPresets();
            if (presets[0].setResolution((int)client_size.x, (int)client_size.y))
                settings.updateRecordBitrate();
        }

        assert(capture_context.active_preset != nullptr);

        if (capture_context.changed_size)
        {
            canvas.begin(0.05f, 0.05f, 0.1f, 1.0f);
            canvas.setFillStyle(0, 0, 0);
            canvas.fillRect(0, 0, (float)canvas.fboWidth(), (float)canvas.fboHeight());
            canvas.end();

            // todo: Force need_draw. Don't switch resolution until next frame?
        }

        if (!done_first_size)
        {
            done_first_size = true;
        }
        else if (shared_sync.project_thread_started)
        {
            // Launch startup simulation 1 frame late once we have a valid canvas size
            if (!project_worker()->getCurrentProject() && !project_worker()->isSwitchingProject())
            {
                auto first_project = ProjectBase::projectInfoList().front();
                project_worker()->setActiveProject(first_project->sim_uid);
                project_worker()->startProject();

                // Kick-start work-render-work-render loop
                need_draw = true;
            }
        }

        // returns true if preprocessor texture was needed
        auto preprocessFrame = [&](bool capturing_frame) -> bool
        {
            if (!capturing_frame && capture_context.active_ssaa == 1 && capture_context.active_sharpen == 0)
            {
                // no preprocessing/flipping needed, just copy canvas to preprocessed_frame
                canvas.readPixels(capture_context.preprocessed_frame);
                return false;
            }

            // preprocess frame before encoding
            CapturePreprocessParams params{};
            params.src_resolution = capture_context.active_canvas_size;
            params.dst_resolution = capture_context.active_target_size;
            params.ssaa = capture_context.active_ssaa;
            params.sharpen = capture_context.active_sharpen;
            params.flip_y = true;

            if (capturing_frame)
                capture_context.preprocessor.preprocessToFrame(canvas.texture(), params, capture_context.preprocessed_frame);
            else
                capture_context.preprocessor.preprocessToTexture(canvas.texture(), params);

            return true;
        };

        bool preprocess_frame = (
            capture_context.changed_scale ||
            capture_context.changed_size ||
            capture_context.changed_sharpening);
        if (need_draw)
        {
            // Draw the fresh frame
            {
                std::unique_lock<std::mutex> lock(shared_sync.state_mutex);

                // ---------------------------------------------------------------------------------------
                // For the rest of this scope, we can safely mutate both shadow(GUI) data AND live data...
                // ---------------------------------------------------------------------------------------

                /// todo: Project::_projectProcess() must have just occured
                /// this would seem like a smarter place to block and call invokeScheduledCalls() (on worker thread)
                /// to ensure bl_schedule lamdas can safely access both live/shadow data. The difficulty is not breaking
                /// variable syncing as it needs to be called before pushDataToUnchangedShadowVars(). It's best thought of
                /// as part of _projectProcess.
                ///
                /// Rather than an aggressive block/wait, *if* there are any scheduled calls, lock shadow_lock, remark live/shadow
                /// vars, invoke scheduled calls and then repush with pushDataToUnchangedShadowVars? (just like you currently do
                /// for _projectProcess)

                worker_fps_timer.tick();

                bool capturing_frame =
                    capture_manager.isCapturing() &&
                    capture_context.permit_frame_capture &&
                    capture_context.new_frame_prepared;
                if (canvas.isDirty() || capturing_frame)
                {
                    canvas.begin(0.05f, 0.05f, 0.1f, 1.0f);
                    project_worker()->draw();
                    canvas.end();

                    preprocess_frame = true;

                    canvas.setDirty(false);
                    // set projectDrawn=true here?
                }

                // preprocess canvas on ssaa/sharpen change OR project signals it redrew (before encoding frame)
                if (preprocess_frame)
                    capture_context.use_preprocessor_texture = preprocessFrame(capturing_frame);

                if (project_worker()->hasRunningProject() || capture_manager.isSnapshotting())
                {
                    // Even if not recording, behave as though we are for consistency
                    capture_context.captured_last_frame = capture_context.permit_frame_capture;

                    if (capturing_frame || capture_context.prepared_snapshot)
                    {
                        // give project a chance to attach metadata to the frame before sending it to the encoder
                        CapturePreset preset_info = *capture_context.active_preset;
                        if (capture_context.prepared_snapshot)
                            preset_info.setResolution(capture_context.prepared_snapshot->resolution);
                        preset_info.setVideo(capture_manager.isRecording()); // Tell user whether preset is being used a video or snapshot
                        EncodeFrame& encoded_frame = capture_context.prepared_snapshot
                            ? capture_context.prepared_snapshot->frame
                            : capture_context.preprocessed_frame;
                        project_worker()->onEncodeFrame(
                            encoded_frame,
                            capture_context.active_snapshot_preset_request_id,
                            preset_info);

                        // send to encoder
                        capture_manager.encodeFrame(encoded_frame);
                        capture_context.prepared_snapshot.reset();
                    }

                }

                // Force worker to tell us when it wants to encode a new frame.
                capture_context.permit_frame_capture = false;

                shared_sync.frame_ready_to_draw = false;
                shared_sync.frame_consumed = true;

                // update settings for the next frame (and flag that we can correctly capture the next frame)
                updateActivePreset();
                capture_context.new_frame_prepared = true; // frame should be ready to capture after next worker draw call
            }

            // Let worker continue
            shared_sync.cv.notify_one();

            projectDrawn = true;
        }
        else
        {
            if (preprocess_frame)
                capture_context.use_preprocessor_texture = preprocessFrame(false);

            // update settings for the next frame
            updateActivePreset();
        }

        bool needs_scaling = false;

        // "canvas" refers to internal canvas dimensions
        // "image" in this context = ImGui image
        // "client" viewport size (max image size)
        ImVec2 image_pos = ImGui::GetCursorScreenPos();
        ImVec2 image_size;

        float sw = client_size.x, sh = client_size.y; // calculates to image size (after scaling)
        float ox = 0, oy = 0;

        // If image small enough to render at 100% scale with no resizing?
        if (!settings.fill_viewport)
        {
            needs_scaling =
                capture_context.active_target_size.x > client_size.x ||
                capture_context.active_target_size.y > client_size.y;
        }
        else
        {
            needs_scaling = true;
        }

        if (needs_scaling)
        {
            float client_aspect = (client_size.x / client_size.y);
            float canvas_aspect = ((float)capture_context.active_target_size.x / (float)capture_context.active_target_size.y);

            if (canvas_aspect > client_aspect)
            {
                sh = client_size.y * (client_aspect / canvas_aspect); // Render aspect is too wide
                oy = 0.5f * (client_size.y - sh);                     // Center vertically
            }
            else
            {
                sw = client_size.x * (canvas_aspect / client_aspect); // Render aspect is too tall
                ox = 0.5f * (client_size.x - sw);                     // Center horizontally
            }
        }
        else
        {
            ox = 0.5f * (client_size.x - capture_context.active_target_size.x);
            oy = 0.5f * (client_size.y - capture_context.active_target_size.y);
            sw = (float)capture_context.active_target_size.x;
            sh = (float)capture_context.active_target_size.y;
        }

        image_pos += { ox, oy };
        image_size = { sw, sh };

        canvas.setClientRect(IRect((int)image_pos.x, (int)image_pos.y, (int)(image_pos.x + sw), (int)(image_pos.y + sh)));
     
        ImGui::SetCursorScreenPos(ImFloor(image_pos));

        if (capture_context.use_preprocessor_texture)
            // use the preprocessor's output texture (no flip needed)
            ImGui::Image((ImTextureID)capture_context.preprocessor.outputTexture(), image_size, ImVec2(0, 0), ImVec2(1, 1));
        else
            // render canvas texture directly (flip y)
            ImGui::Image(canvas.texture(), image_size, ImVec2(0, 1), ImVec2(1, 0));

        // check if anything that requires updating the canvas/frame preprocessor
        capture_context.changed_size = canvas.resize(capture_context.active_canvas_size.x, capture_context.active_canvas_size.y);
        capture_context.changed_scale = canvas.setGlobalScale(platform()->dpr() * capture_context.active_ssaa);
        capture_context.changed_sharpening = capture_context.active_sharpen != capture_context.old_sharpen;

        capture_context.old_sharpen = capture_context.active_sharpen;

        viewport_hovered = ImGui::IsWindowHovered();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
void MainWindow::populateTimeline(std::int64_t currentFrame, bool playing)
{
    if (ImGui::Begin("Timeline", nullptr, window_flags))
    {
        ImGui::TextUnformatted("Time (frames)");

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 0.0f && size.y > 0.0f)
        {
            const float layerWidth = std::min(scale_size(140.0f), size.x * 0.35f);
            const float rowHeight = ImGui::GetFrameHeight();
            const float scrollbarHeight = std::min(ImGui::GetStyle().ScrollbarSize, size.y);
            const ImVec2 end = origin + ImVec2(size.x, size.y - scrollbarHeight);
            const float rulerX = origin.x + layerWidth;
            const ImVec2 padding = ImGui::GetStyle().FramePadding;
            const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ImS64 frameCount = currentFrame + 1;
            for (const TimelineLayer& layer : timelineLayers)
                frameCount = std::max(frameCount, static_cast<ImS64>(layer.frames.size()));
            const std::string lastFrameLabel = std::to_string(frameCount - 1);
            const float frameWidth = std::max(scale_size(12.0f), (ImGui::CalcTextSize(lastFrameLabel.c_str()).x + padding.x * 2.0f) / 5.0f);
            const ImS64 visibleFrames = std::max<ImS64>(1, static_cast<ImS64>(std::floor((end.x - rulerX) / frameWidth)));
            frameCount = std::max(frameCount, visibleFrames);
            timelineScrollFrame = std::clamp(timelineScrollFrame, ImS64{ 0 }, frameCount - visibleFrames);
            if (playing && currentFrame != timelinePreviousFrame)
            {
                const ImS64 firstVisibleFrame = std::max<ImS64>(0, currentFrame - visibleFrames + 1);
                timelineScrollFrame = std::clamp(timelineScrollFrame, firstVisibleFrame, static_cast<ImS64>(currentFrame));
            }
            ImGui::ScrollbarEx(ImRect(ImVec2(rulerX, end.y), origin + size), ImGui::GetID("##timelineScroll"),
                ImGuiAxis_X, &timelineScrollFrame, visibleFrames, frameCount);

            drawList->PushClipRect(origin, end, true);
            drawList->AddRectFilled(origin, ImVec2(rulerX, end.y), ImGui::GetColorU32(ImGuiCol_ChildBg));
            drawList->AddRectFilled(origin, ImVec2(end.x, origin.y + rowHeight), ImGui::GetColorU32(ImGuiCol_FrameBg));

            drawList->PushClipRect(origin, ImVec2(rulerX, end.y), true);
            drawList->AddText(origin + padding, textColor, "Layers");
            for (size_t layer = 0; layer < timelineLayers.size(); ++layer)
            {
                const float y = origin.y + rowHeight * static_cast<float>(layer + 1);
                if (y >= end.y)
                    break;
                drawList->AddText(ImVec2(origin.x + padding.x, y + padding.y), textColor, timelineLayers[layer].name.c_str());
            }
            drawList->PopClipRect();

            drawList->PushClipRect(ImVec2(rulerX, origin.y), end, true);
            const int visibleColumns = static_cast<int>(std::ceil((end.x - rulerX) / frameWidth));
            if (currentFrame >= timelineScrollFrame && currentFrame - timelineScrollFrame < visibleColumns)
            {
                const float x = rulerX + static_cast<float>(currentFrame - timelineScrollFrame) * frameWidth;
                drawList->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + frameWidth, end.y), IM_COL32(255, 40, 40, 70));
                drawList->AddRect(ImVec2(x, origin.y), ImVec2(x + frameWidth, end.y), IM_COL32(255, 40, 40, 255));
            }
            for (int column = 0; column < visibleColumns; ++column)
            {
                const ImS64 frame = timelineScrollFrame + column;
                const float x = rulerX + (static_cast<float>(column) + 0.5f) * frameWidth;
                const bool majorTick = frame % 5 == 0;
                const float tickHeight = scale_size(majorTick ? 5.0f : 2.0f);
                drawList->AddLine(ImVec2(x, origin.y + rowHeight - tickHeight), ImVec2(x, origin.y + rowHeight), borderColor);
                if (majorTick)
                {
                    const std::string label = std::to_string(frame);
                    const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
                    drawList->AddText(ImVec2(x - labelWidth * 0.5f, origin.y), textColor, label.c_str());
                }
            }
            drawList->PopClipRect();

            for (size_t row = 0; row <= timelineLayers.size(); ++row)
            {
                const float y = origin.y + rowHeight * static_cast<float>(row + 1);
                if (y >= end.y)
                    break;
                drawList->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), borderColor);
            }
            drawList->AddLine(ImVec2(rulerX, origin.y), ImVec2(rulerX, end.y), borderColor);
            drawList->PopClipRect();

            ImGui::Dummy(size);
            timelinePreviousFrame = currentFrame;
        }
    }
    ImGui::End();
}

void MainWindow::populateUI()
{
    if (!manageDockingLayout())
        return;

    bool project_drawn = false;

    // determine if we are ready to draw *before* populating simulation imgui attributes
    {
        std::lock_guard<std::mutex> g(shared_sync.state_mutex);
        need_draw = shared_sync.frame_ready_to_draw;
    }

    // stall here (GUI-thread) until worker finishes copying data to live thread
    shared_sync.wait_until_live_buffer_updated();

    {
        // prevent deadlock if worker thread is waiting
        std::unique_lock<std::mutex> shadow_lock(shared_sync.shadow_buffer_mutex, std::defer_lock);
#ifdef BL_WEB_BUILD
        // GPU startup may await browser tasks while holding this lock. Let the
        // main event loop run and try the UI again on the next animation frame.
        if (!shadow_lock.try_lock())
            return;
#else
        while (!shadow_lock.try_lock())
        {
            thread_queue.pump();
            std::this_thread::yield();
        }
#endif

        // ------------------------------------------------------------------------------------------------------------
        // For the rest of this scope, it's safe to mutate GUI data buffers in preparation for the next worker frame...
        // ------------------------------------------------------------------------------------------------------------

        bool collapse_layout = vertical_layout || platform()->maxCharRows() < 40.0f;

        if (ProjectBase::projectInfoList().size() <= 1)
            collapse_layout = false;


        // ==== populate UI / draw nanovg overlay ====


        // shadow buffer is now up-to-date and free to access (while worker does processing)
        {
            // Draw sidebar
            {
                if (sidebar_visible)
                {
                    if (collapse_layout)
                        populateCollapsedLayout();
                    else
                        populateExpandedLayout();

                    ///#ifdef BL_DEBUG_INCLUDE_LOG_TABS
                    ///if (!done_first_focus && focusWindow("Debug"))
                    ///    done_first_focus = true;
                    ///#else
                    if (!done_first_focus && focusWindow(collapse_layout ? "Active" : "Projects"))
                        done_first_focus = true;
                    ///#endif
                }

                populateOverlay();
            }

            const ProjectBase* currentProject = project_worker()->getCurrentProject();
            const bool playing = currentProject && currentProject->isActive() && !currentProject->isPaused();
            populateTimeline(currentProject ? currentProject->currentFrameIndex() : 0, playing);

            // Draw viewport
            {
                ImGuiWindowClass wc{};
                wc.DockNodeFlagsOverrideSet = (int)ImGuiDockNodeFlags_NoTabBar | (int)ImGuiWindowFlags_NoDocking;
                ImGui::SetNextWindowClass(&wc);

                populateViewport(project_drawn);
            }
        }

        if (sidebar_visible)
        {
            ImGui::Begin("Settings", nullptr, window_flags);
            {
                populateToolbar();
                settings_panel.populateSettings();
            }
            ImGui::End();
        }

        // end mutex lock
    }

    if (sidebar_visible)
    {
        #ifdef BL_DEBUG_INCLUDE_LOG_TABS
        ImGui::Begin("Debug"); // Begin Debug Window
        {
            if (ImGui::BeginTabBar("DebugTabs"))
            {
                if (ImGui::BeginTabItem("Display"))
                {
                    dpiDebugInfo();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Project Log"))
                {
                    project_log.draw();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Global Log"))
                {
                    debug_log.draw();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End(); // End Debug Window
        #endif
    }

    // always save capture output on main GUI thread (for Emscripten)
    checkCaptureComplete();

    is_editing_ui = _isEditingUI();

    //static bool demo_open = true;
    //ImGui::ShowDemoWindow(&demo_open);

    /// debugging
    {
        // Debug Log
        //project_log.draw();

        // Debug DPI
        //dpiDebugInfo();

        //threadsDebugInfo();

        /*ImGuiID active_id = ImGui::GetActiveID();

        ImGui::Begin("Viewport Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Viewport hovered: %d", viewport_hovered ?1:0);
        ImGui::Text("active_id: %d", active_id);
        //ImGui::Text("Viewport: {%.0f, %.0f, %.0f, %.0f}",
        //    viewport_rect.Min.x,
        //    viewport_rect.Min.y,
        //    viewport_rect.Max.x,
        //    viewport_rect.Max.y
        //);
        ImGui::End();*/
    }

    ///gui_destruction_queue.flush();

    
    if (shared_sync.immediateUpdateRequested())
        shared_sync.setImmediateUpdate(false);
    else
    {
        #ifdef BL_WEB_BUILD
        bool should_delay = true;
        #else
        bool should_delay = !project_drawn; // if project drawn, delay is already handled by worker/lock (assuming desktop build)
        #endif

        if (should_delay)
        {
            // prevent spinning aggressively on main thread (if project drawn, delay was already handled by worker/lock)
            auto frame_dt = std::chrono::steady_clock::now() - last_frame_time;
            constexpr uint64_t target_ns = 1000000000 / 60;
            uint64_t frame_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(frame_dt).count());
            uint64_t delay_ns = std::max((uint64_t)0, target_ns - frame_ns);
            if (delay_ns) SDL_DelayPrecise(delay_ns);

            last_frame_time = std::chrono::steady_clock::now();
        }
    }
}

const CapturePreset* MainWindow::determineRenderedPreset() const
{
    bool is_capturing = capture_context.manager().isCapturing();

    const SnapshotPresetList& presets = capture_context.presetManager().allPresets();
    int selected_preset_idx = settings_panel.getSelectedPresetIndex(); //  selected "allPresets" index

    const CapturePreset* ret = nullptr; // first preset always represents viewport. Use by default

    if (is_capturing)
    {
        // *always* use capture preset if capturing, even if "preview" mode on
        assert(capture_context.enabled_capture_presets_current_idx < capture_context.enabled_capture_presets.size());
        ret = &capture_context.enabled_capture_presets[capture_context.enabled_capture_presets_current_idx];
    }
    else
    {
        // if "preview" mode on, use selected preset (if one is selected)
        assert(selected_preset_idx >= 0 && selected_preset_idx < presets.size());
        ret = &presets[selected_preset_idx];
    }

    assert(ret != nullptr);

    return ret;
}

bool MainWindow::updateActivePreset()
{
    const CapturePreset* next_preset = determineRenderedPreset();
    int   next_ssaa        = (next_preset->getSSAA() > 0)       ? next_preset->getSSAA()       : capture_context.settings().default_ssaa;
    float next_sharpen     = (next_preset->getSharpening() > 0) ? next_preset->getSharpening() : capture_context.settings().default_sharpen;
    IVec2 next_canvas_size = next_preset->getResolution() * next_ssaa;
    IVec2 next_target_size = next_preset->getResolution();

    if (next_preset      != capture_context.active_preset ||
        next_ssaa        != capture_context.active_ssaa ||
        next_sharpen     != capture_context.active_sharpen ||
        next_canvas_size != capture_context.active_canvas_size ||
        next_target_size != capture_context.active_target_size)
    {
        capture_context.active_preset      = next_preset;
        capture_context.active_ssaa        = next_ssaa;
        capture_context.active_sharpen     = next_sharpen;
        capture_context.active_canvas_size = next_canvas_size;
        capture_context.active_target_size = next_target_size;

        return true;
    }
    return false;
}

BL_END_NS
