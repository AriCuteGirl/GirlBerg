#include "steam_overlay.h"

#ifdef EMU_OVERLAY

#include <thread>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <imgui.h>

#include "../dll/dll.h"
#include "../dll/settings_parser.h"

#include "Renderer_Detector.h"

static constexpr int max_window_id = 10000;
static constexpr int base_notif_window_id  = 0 * max_window_id;
static constexpr int base_friend_window_id = 1 * max_window_id;
static constexpr int base_friend_item_id   = 2 * max_window_id;

static constexpr char *valid_languages[] = {
    "english",
    "arabic",
    "bulgarian",
    "schinese",
    "tchinese",
    "czech",
    "danish",
    "dutch",
    "finnish",
    "french",
    "german",
    "greek",
    "hungarian",
    "italian",
    "japanese",
    "koreana",
    "norwegian",
    "polish",
    "portuguese",
    "brazilian",
    "romanian",
    "russian",
    "spanish",
    "latam",
    "swedish",
    "thai",
    "turkish",
    "ukrainian",
    "vietnamese"
};

static constexpr const char *retro_themes[] = {
    "Green Terminal",
    "Amber CRT",
    "Windows 98",
    "Aero Blue",
    "Aero Slate",
    "Aero Emerald",
    "Pink Neon",
    "Catppuccin Latte",
    "Catppuccin Frappe",
    "Catppuccin Macchiato",
    "Catppuccin Mocha",
    "Red Light",
    "Red Classic"
};

static constexpr const char *overlay_frontends[] = {
    "ImGui Frontend",
    "Custom Aero (WIP)"
};

static constexpr const char *overlay_ui_scales[] = {
    "Small",
    "Medium",
    "Large"
};

#define URL_WINDOW_NAME "URL Window"

namespace {
static ImVec2 PixelAlign(const ImVec2 &v)
{
    return ImVec2(std::floor(v.x), std::floor(v.y));
}

static float GetOverlayUiScale(int ui_scale_idx)
{
    switch (ui_scale_idx) {
        case 0: return 0.90f; // Small
        case 2: return 1.10f; // Large
        case 1:
        default: return 1.00f; // Medium
    }
}

struct OverlayColorTuning
{
    bool enabled = false;
    ImVec4 text = ImVec4(0.58f, 0.95f, 0.58f, 1.0f);
    ImVec4 window_bg = ImVec4(0.02f, 0.05f, 0.02f, 0.58f);
    ImVec4 border = ImVec4(0.24f, 0.70f, 0.24f, 1.0f);
    ImVec4 button = ImVec4(0.03f, 0.11f, 0.03f, 1.0f);
    ImVec4 button_hovered = ImVec4(0.09f, 0.20f, 0.09f, 1.0f);
    ImVec4 button_active = ImVec4(0.12f, 0.29f, 0.12f, 1.0f);
    ImVec4 tab = ImVec4(0.03f, 0.11f, 0.03f, 1.0f);
    ImVec4 tab_active = ImVec4(0.12f, 0.29f, 0.12f, 1.0f);
    ImVec4 accent = ImVec4(0.35f, 0.84f, 0.35f, 1.0f);
};

static OverlayColorTuning g_overlay_colors = {};
static bool g_overlay_colors_loaded = false;

static ImVec4 Clamp01(ImVec4 c)
{
    c.x = std::max(0.0f, std::min(1.0f, c.x));
    c.y = std::max(0.0f, std::min(1.0f, c.y));
    c.z = std::max(0.0f, std::min(1.0f, c.z));
    c.w = std::max(0.0f, std::min(1.0f, c.w));
    return c;
}

static ImVec4 Mix(ImVec4 a, ImVec4 b, float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

static bool ParseColor4(const std::string &s, ImVec4 &out)
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    if (std::sscanf(s.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
        out = Clamp01(ImVec4(r, g, b, a));
        return true;
    }
    return false;
}

static std::string SerializeColor4(const ImVec4 &c)
{
    char buf[96] = {};
    std::snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f,%.4f", c.x, c.y, c.z, c.w);
    return std::string(buf);
}

static void LoadCustomOverlayColors(Local_Storage *storage)
{
    if (g_overlay_colors_loaded || !storage) return;
    g_overlay_colors_loaded = true;
    char data[2048] = {};
    int read = storage->get_data_settings("overlay_custom_colors.txt", data, sizeof(data) - 1);
    if (read <= 0) return;
    std::istringstream in(std::string(data, static_cast<size_t>(read)));
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "enabled") {
            g_overlay_colors.enabled = (val == "1" || val == "true");
        } else if (key == "text") {
            ParseColor4(val, g_overlay_colors.text);
        } else if (key == "window_bg") {
            ParseColor4(val, g_overlay_colors.window_bg);
        } else if (key == "border") {
            ParseColor4(val, g_overlay_colors.border);
        } else if (key == "button") {
            ParseColor4(val, g_overlay_colors.button);
        } else if (key == "button_hovered") {
            ParseColor4(val, g_overlay_colors.button_hovered);
        } else if (key == "button_active") {
            ParseColor4(val, g_overlay_colors.button_active);
        } else if (key == "tab") {
            ParseColor4(val, g_overlay_colors.tab);
        } else if (key == "tab_active") {
            ParseColor4(val, g_overlay_colors.tab_active);
        } else if (key == "accent") {
            ParseColor4(val, g_overlay_colors.accent);
        }
    }
}

static void SaveCustomOverlayColors(Local_Storage *storage)
{
    if (!storage) return;
    std::string out;
    out.reserve(512);
    out += std::string("enabled=") + (g_overlay_colors.enabled ? "1\n" : "0\n");
    out += "text=" + SerializeColor4(g_overlay_colors.text) + "\n";
    out += "window_bg=" + SerializeColor4(g_overlay_colors.window_bg) + "\n";
    out += "border=" + SerializeColor4(g_overlay_colors.border) + "\n";
    out += "button=" + SerializeColor4(g_overlay_colors.button) + "\n";
    out += "button_hovered=" + SerializeColor4(g_overlay_colors.button_hovered) + "\n";
    out += "button_active=" + SerializeColor4(g_overlay_colors.button_active) + "\n";
    out += "tab=" + SerializeColor4(g_overlay_colors.tab) + "\n";
    out += "tab_active=" + SerializeColor4(g_overlay_colors.tab_active) + "\n";
    out += "accent=" + SerializeColor4(g_overlay_colors.accent) + "\n";
    storage->store_data_settings("overlay_custom_colors.txt", const_cast<char *>(out.c_str()), static_cast<unsigned int>(out.size()));
}

static ImU32 ToU32(const ImVec4 &c)
{
    return ImGui::ColorConvertFloat4ToU32(Clamp01(c));
}

static bool IsUrlChar(char c)
{
    return !(std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\'' || c == '<' || c == '>' ||
             c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}');
}

static std::string NormalizeUrl(std::string url)
{
    if (url.rfind("www.", 0) == 0) {
        return std::string("https://") + url;
    }
    return url;
}

static std::vector<std::string> ExtractUrls(const std::string &line)
{
    std::vector<std::string> urls;
    size_t pos = 0;
    while (pos < line.size()) {
        size_t p_http = line.find("http://", pos);
        size_t p_https = line.find("https://", pos);
        size_t p_www = line.find("www.", pos);
        size_t p = std::string::npos;
        if (p_http != std::string::npos) p = p_http;
        if (p_https != std::string::npos) p = (p == std::string::npos) ? p_https : std::min(p, p_https);
        if (p_www != std::string::npos) p = (p == std::string::npos) ? p_www : std::min(p, p_www);
        if (p == std::string::npos) break;
        size_t e = p;
        while (e < line.size() && IsUrlChar(line[e])) ++e;
        if (e > p + 3) {
            std::string u = line.substr(p, e - p);
            while (!u.empty() && (u.back() == '.' || u.back() == ',' || u.back() == ';' || u.back() == ':')) {
                u.pop_back();
            }
            if (!u.empty()) urls.push_back(NormalizeUrl(u));
            if (urls.size() >= 3) break;
        }
        pos = e + 1;
    }
    return urls;
}

static std::string ShortUrlLabel(const std::string &url)
{
    const size_t max_len = 44;
    if (url.size() <= max_len) return url;
    return url.substr(0, max_len - 3) + "...";
}

static void DrawChatHistoryWithEmbeds(const char *id, std::string const& history, std::string &show_url_ref, float height)
{
    ImGui::BeginChild(id, ImVec2(-1.0f, height), true);
    if (history.empty()) {
        ImGui::TextDisabled("No messages yet.");
    } else {
        std::istringstream in(history);
        std::string line;
        int line_idx = 0;
        while (std::getline(in, line)) {
            ImGui::TextUnformatted(line.c_str());
            std::vector<std::string> urls = ExtractUrls(line);
            if (!urls.empty()) {
                ImGui::Indent(12.0f);
                for (size_t i = 0; i < urls.size(); ++i) {
                    std::string chip = "[" + ShortUrlLabel(urls[i]) + "]##chat_url_" + std::to_string(line_idx) + "_" + std::to_string(i);
                    if (ImGui::SmallButton(chip.c_str())) {
                        show_url_ref = urls[i];
                    }
                    if (i + 1 < urls.size()) ImGui::SameLine();
                }
                ImGui::Unindent(12.0f);
            }
            ++line_idx;
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

struct AeroThemeColors
{
    ImU32 overlay_top;
    ImU32 overlay_bottom;
    ImU32 overlay_border;
    ImU32 panel_top_left;
    ImU32 panel_top_right;
    ImU32 panel_bottom_left;
    ImU32 panel_bottom_right;
    ImU32 panel_border;
    ImU32 title_top;
    ImU32 title_bottom;
    ImU32 title_border;
    ImU32 text;
    ImU32 text_soft;
    ImU32 button_top;
    ImU32 button_bottom;
    ImU32 button_top_hover;
    ImU32 button_bottom_hover;
    ImU32 button_top_active;
    ImU32 button_bottom_active;
    ImU32 button_border;
    ImU32 tab_top;
    ImU32 tab_bottom;
    ImU32 tab_top_active;
    ImU32 tab_bottom_active;
    ImU32 tab_border;
    ImU32 row_top;
    ImU32 row_bottom;
    ImU32 row_top_hover;
    ImU32 row_bottom_hover;
    ImU32 row_border;
    ImU32 row_border_hover;
};

static AeroThemeColors GetAeroThemeColors(int theme)
{
    switch (theme) {
        case 4: // Aero Slate
            return {
                IM_COL32(32, 38, 52, 60), IM_COL32(12, 16, 25, 98), IM_COL32(188, 201, 224, 98),
                IM_COL32(120, 128, 146, 122), IM_COL32(108, 116, 136, 104), IM_COL32(32, 38, 54, 176), IM_COL32(24, 30, 42, 176), IM_COL32(220, 230, 248, 228),
                IM_COL32(172, 184, 206, 226), IM_COL32(74, 88, 115, 238), IM_COL32(236, 244, 255, 245),
                IM_COL32(241, 246, 255, 255), IM_COL32(222, 232, 248, 255),
                IM_COL32(130, 146, 177, 216), IM_COL32(70, 84, 110, 230),
                IM_COL32(154, 171, 202, 228), IM_COL32(84, 102, 134, 236),
                IM_COL32(105, 126, 161, 238), IM_COL32(52, 70, 101, 242), IM_COL32(225, 237, 255, 242),
                IM_COL32(115, 131, 160, 198), IM_COL32(58, 74, 102, 218),
                IM_COL32(155, 175, 208, 220), IM_COL32(72, 92, 128, 232), IM_COL32(226, 238, 255, 236),
                IM_COL32(108, 127, 160, 120), IM_COL32(48, 62, 89, 165),
                IM_COL32(136, 158, 192, 168), IM_COL32(62, 78, 109, 198),
                IM_COL32(196, 216, 244, 200), IM_COL32(226, 242, 255, 248)
            };
        case 5: // Aero Emerald
            return {
                IM_COL32(20, 56, 46, 54), IM_COL32(8, 21, 18, 96), IM_COL32(168, 233, 205, 96),
                IM_COL32(78, 173, 141, 116), IM_COL32(68, 160, 130, 98), IM_COL32(10, 48, 38, 182), IM_COL32(8, 34, 28, 184), IM_COL32(194, 244, 224, 228),
                IM_COL32(124, 224, 190, 228), IM_COL32(34, 130, 102, 238), IM_COL32(220, 255, 242, 246),
                IM_COL32(234, 255, 245, 255), IM_COL32(205, 245, 227, 255),
                IM_COL32(90, 192, 156, 214), IM_COL32(18, 122, 94, 228),
                IM_COL32(112, 214, 178, 224), IM_COL32(25, 145, 111, 236),
                IM_COL32(62, 177, 140, 238), IM_COL32(12, 104, 78, 242), IM_COL32(210, 252, 234, 240),
                IM_COL32(76, 170, 139, 194), IM_COL32(15, 108, 82, 218),
                IM_COL32(108, 205, 171, 214), IM_COL32(24, 138, 104, 230), IM_COL32(202, 248, 230, 234),
                IM_COL32(70, 164, 133, 118), IM_COL32(13, 88, 68, 165),
                IM_COL32(102, 196, 163, 166), IM_COL32(21, 126, 96, 196),
                IM_COL32(175, 236, 210, 198), IM_COL32(212, 255, 236, 248)
            };
        case 6: // Pink Neon
            return {
                IM_COL32(58, 19, 52, 56), IM_COL32(26, 8, 24, 96), IM_COL32(242, 170, 232, 98),
                IM_COL32(180, 92, 170, 120), IM_COL32(164, 80, 154, 98), IM_COL32(62, 16, 56, 184), IM_COL32(46, 10, 42, 184), IM_COL32(249, 214, 244, 228),
                IM_COL32(232, 136, 220, 230), IM_COL32(154, 50, 139, 238), IM_COL32(255, 230, 252, 246),
                IM_COL32(255, 236, 252, 255), IM_COL32(245, 204, 240, 255),
                IM_COL32(207, 96, 188, 214), IM_COL32(146, 46, 132, 228),
                IM_COL32(227, 124, 208, 224), IM_COL32(172, 60, 154, 236),
                IM_COL32(188, 72, 171, 238), IM_COL32(130, 36, 117, 242), IM_COL32(252, 218, 247, 240),
                IM_COL32(189, 84, 173, 198), IM_COL32(136, 44, 122, 218),
                IM_COL32(216, 112, 199, 218), IM_COL32(158, 56, 142, 230), IM_COL32(249, 213, 243, 234),
                IM_COL32(174, 78, 160, 120), IM_COL32(116, 38, 106, 165),
                IM_COL32(202, 104, 186, 168), IM_COL32(145, 52, 130, 196),
                IM_COL32(242, 178, 231, 198), IM_COL32(255, 221, 250, 248)
            };
        case 7: // Catppuccin Latte
            return {
                IM_COL32(220, 210, 235, 52), IM_COL32(198, 186, 224, 90), IM_COL32(146, 122, 191, 92),
                IM_COL32(237, 229, 244, 122), IM_COL32(231, 222, 241, 110), IM_COL32(220, 210, 235, 186), IM_COL32(212, 201, 230, 188), IM_COL32(126, 102, 168, 224),
                IM_COL32(179, 156, 220, 228), IM_COL32(138, 112, 183, 236), IM_COL32(92, 70, 132, 244),
                IM_COL32(66, 45, 98, 255), IM_COL32(94, 73, 130, 255),
                IM_COL32(210, 194, 236, 218), IM_COL32(188, 166, 224, 230),
                IM_COL32(219, 203, 242, 228), IM_COL32(196, 172, 230, 236),
                IM_COL32(198, 174, 231, 238), IM_COL32(172, 144, 211, 242), IM_COL32(121, 96, 164, 240),
                IM_COL32(212, 196, 236, 196), IM_COL32(184, 160, 222, 218),
                IM_COL32(220, 205, 242, 220), IM_COL32(194, 170, 229, 232), IM_COL32(108, 82, 150, 234),
                IM_COL32(206, 191, 233, 108), IM_COL32(178, 154, 216, 158),
                IM_COL32(214, 198, 240, 166), IM_COL32(186, 161, 224, 196),
                IM_COL32(131, 105, 173, 196), IM_COL32(88, 63, 126, 246)
            };
        case 8: // Catppuccin Frappe
            return {
                IM_COL32(72, 67, 102, 56), IM_COL32(45, 42, 66, 96), IM_COL32(176, 153, 255, 92),
                IM_COL32(99, 93, 139, 122), IM_COL32(88, 82, 126, 106), IM_COL32(55, 50, 82, 186), IM_COL32(47, 43, 74, 186), IM_COL32(198, 184, 255, 226),
                IM_COL32(136, 124, 207, 228), IM_COL32(88, 78, 146, 238), IM_COL32(216, 206, 255, 246),
                IM_COL32(239, 235, 255, 255), IM_COL32(212, 202, 245, 255),
                IM_COL32(112, 104, 176, 214), IM_COL32(72, 64, 132, 228),
                IM_COL32(128, 118, 196, 224), IM_COL32(84, 74, 150, 236),
                IM_COL32(100, 90, 167, 238), IM_COL32(58, 50, 118, 242), IM_COL32(223, 214, 255, 240),
                IM_COL32(106, 98, 172, 196), IM_COL32(64, 56, 124, 218),
                IM_COL32(132, 122, 196, 218), IM_COL32(86, 77, 149, 230), IM_COL32(220, 210, 255, 234),
                IM_COL32(96, 88, 164, 112), IM_COL32(58, 51, 118, 160),
                IM_COL32(122, 113, 192, 166), IM_COL32(80, 72, 146, 196),
                IM_COL32(183, 164, 255, 196), IM_COL32(223, 214, 255, 248)
            };
        case 9: // Catppuccin Macchiato
            return {
                IM_COL32(44, 47, 72, 56), IM_COL32(24, 26, 41, 98), IM_COL32(166, 200, 255, 94),
                IM_COL32(78, 88, 132, 122), IM_COL32(70, 80, 122, 106), IM_COL32(30, 35, 58, 188), IM_COL32(24, 29, 50, 188), IM_COL32(194, 215, 255, 226),
                IM_COL32(120, 165, 239, 230), IM_COL32(66, 116, 194, 238), IM_COL32(215, 229, 255, 246),
                IM_COL32(236, 243, 255, 255), IM_COL32(204, 220, 247, 255),
                IM_COL32(88, 132, 214, 214), IM_COL32(46, 90, 168, 228),
                IM_COL32(110, 151, 228, 224), IM_COL32(56, 104, 182, 236),
                IM_COL32(72, 121, 202, 238), IM_COL32(34, 74, 145, 242), IM_COL32(205, 225, 255, 240),
                IM_COL32(84, 127, 208, 196), IM_COL32(40, 82, 158, 218),
                IM_COL32(109, 148, 225, 218), IM_COL32(52, 97, 177, 230), IM_COL32(202, 222, 255, 234),
                IM_COL32(77, 120, 201, 112), IM_COL32(34, 70, 139, 160),
                IM_COL32(102, 140, 219, 166), IM_COL32(47, 88, 164, 196),
                IM_COL32(162, 198, 255, 196), IM_COL32(205, 226, 255, 248)
            };
        case 10: // Catppuccin Mocha
            return {
                IM_COL32(38, 35, 56, 58), IM_COL32(20, 18, 31, 100), IM_COL32(203, 166, 247, 96),
                IM_COL32(80, 66, 116, 122), IM_COL32(72, 59, 106, 106), IM_COL32(38, 30, 63, 188), IM_COL32(31, 25, 52, 188), IM_COL32(224, 202, 252, 228),
                IM_COL32(176, 134, 236, 230), IM_COL32(119, 82, 186, 238), IM_COL32(243, 231, 255, 246),
                IM_COL32(247, 239, 255, 255), IM_COL32(229, 211, 250, 255),
                IM_COL32(147, 109, 214, 216), IM_COL32(98, 66, 164, 230),
                IM_COL32(169, 129, 230, 226), IM_COL32(112, 77, 179, 236),
                IM_COL32(130, 91, 196, 238), IM_COL32(81, 53, 144, 242), IM_COL32(236, 220, 255, 240),
                IM_COL32(138, 100, 204, 196), IM_COL32(88, 58, 152, 218),
                IM_COL32(164, 125, 228, 220), IM_COL32(106, 73, 174, 232), IM_COL32(233, 215, 255, 236),
                IM_COL32(124, 90, 190, 114), IM_COL32(78, 50, 136, 162),
                IM_COL32(151, 113, 219, 168), IM_COL32(98, 68, 167, 198),
                IM_COL32(211, 178, 252, 198), IM_COL32(239, 222, 255, 248)
            };
        case 11: // Red Light
            return {
                IM_COL32(92, 30, 30, 54), IM_COL32(44, 14, 14, 94), IM_COL32(255, 190, 190, 96),
                IM_COL32(214, 132, 132, 116), IM_COL32(202, 120, 120, 96), IM_COL32(96, 28, 28, 182), IM_COL32(74, 22, 22, 184), IM_COL32(255, 220, 220, 230),
                IM_COL32(242, 152, 152, 230), IM_COL32(188, 88, 88, 238), IM_COL32(255, 236, 236, 246),
                IM_COL32(255, 244, 244, 255), IM_COL32(255, 222, 222, 255),
                IM_COL32(228, 126, 126, 216), IM_COL32(176, 76, 76, 228),
                IM_COL32(238, 148, 148, 224), IM_COL32(198, 94, 94, 236),
                IM_COL32(214, 106, 106, 238), IM_COL32(160, 64, 64, 242), IM_COL32(255, 230, 230, 242),
                IM_COL32(214, 116, 116, 194), IM_COL32(170, 72, 72, 218),
                IM_COL32(236, 142, 142, 214), IM_COL32(190, 88, 88, 230), IM_COL32(255, 224, 224, 234),
                IM_COL32(198, 106, 106, 112), IM_COL32(152, 64, 64, 158),
                IM_COL32(228, 136, 136, 158), IM_COL32(182, 84, 84, 196),
                IM_COL32(255, 194, 194, 198), IM_COL32(255, 232, 232, 248)
            };
        case 12: // Red Classic
            return {
                IM_COL32(66, 12, 12, 58), IM_COL32(30, 6, 6, 102), IM_COL32(245, 112, 112, 92),
                IM_COL32(160, 58, 58, 122), IM_COL32(146, 50, 50, 104), IM_COL32(76, 14, 14, 184), IM_COL32(56, 10, 10, 184), IM_COL32(246, 186, 186, 228),
                IM_COL32(224, 86, 86, 230), IM_COL32(156, 34, 34, 238), IM_COL32(255, 208, 208, 246),
                IM_COL32(255, 224, 224, 255), IM_COL32(240, 170, 170, 255),
                IM_COL32(186, 56, 56, 214), IM_COL32(126, 24, 24, 228),
                IM_COL32(206, 72, 72, 224), IM_COL32(144, 32, 32, 236),
                IM_COL32(174, 44, 44, 238), IM_COL32(112, 18, 18, 242), IM_COL32(248, 188, 188, 242),
                IM_COL32(170, 44, 44, 196), IM_COL32(114, 18, 18, 218),
                IM_COL32(202, 66, 66, 214), IM_COL32(138, 28, 28, 230), IM_COL32(244, 178, 178, 234),
                IM_COL32(156, 40, 40, 118), IM_COL32(102, 16, 16, 165),
                IM_COL32(188, 58, 58, 166), IM_COL32(126, 24, 24, 196),
                IM_COL32(240, 126, 126, 198), IM_COL32(255, 200, 200, 248)
            };
        case 0: // Green Terminal mapped to transparent glass-green for custom frontend
            return {
                IM_COL32(8, 34, 20, 58), IM_COL32(4, 14, 8, 102), IM_COL32(126, 223, 142, 90),
                IM_COL32(42, 118, 62, 122), IM_COL32(36, 110, 56, 102), IM_COL32(6, 34, 14, 184), IM_COL32(4, 24, 10, 184), IM_COL32(166, 236, 176, 226),
                IM_COL32(78, 188, 103, 230), IM_COL32(18, 110, 48, 238), IM_COL32(202, 250, 210, 246),
                IM_COL32(226, 255, 232, 255), IM_COL32(188, 236, 196, 255),
                IM_COL32(62, 152, 86, 216), IM_COL32(15, 102, 45, 228),
                IM_COL32(84, 178, 108, 228), IM_COL32(20, 125, 58, 236),
                IM_COL32(42, 148, 74, 238), IM_COL32(10, 92, 38, 242), IM_COL32(193, 248, 200, 240),
                IM_COL32(58, 142, 80, 194), IM_COL32(10, 88, 34, 216),
                IM_COL32(83, 173, 104, 218), IM_COL32(18, 118, 48, 230), IM_COL32(188, 244, 195, 234),
                IM_COL32(52, 138, 72, 120), IM_COL32(9, 78, 30, 165),
                IM_COL32(78, 168, 98, 168), IM_COL32(16, 108, 44, 196),
                IM_COL32(166, 234, 178, 198), IM_COL32(202, 252, 210, 248)
            };
        default: // Aero Blue, Amber CRT, Windows 98 all mapped to a blue-tinted glass style
            return {
                IM_COL32(40, 72, 116, 52), IM_COL32(14, 24, 44, 94), IM_COL32(185, 219, 255, 96),
                IM_COL32(92, 142, 210, 116), IM_COL32(78, 130, 198, 92), IM_COL32(12, 26, 49, 182), IM_COL32(10, 22, 42, 184), IM_COL32(208, 232, 255, 230),
                IM_COL32(142, 196, 255, 230), IM_COL32(48, 100, 167, 238), IM_COL32(228, 244, 255, 246),
                IM_COL32(238, 247, 255, 255), IM_COL32(218, 235, 252, 255),
                IM_COL32(105, 160, 225, 216), IM_COL32(44, 88, 148, 228),
                IM_COL32(132, 186, 245, 224), IM_COL32(57, 106, 171, 236),
                IM_COL32(80, 140, 205, 238), IM_COL32(30, 75, 130, 242), IM_COL32(214, 236, 255, 242),
                IM_COL32(90, 138, 202, 194), IM_COL32(35, 78, 132, 218),
                IM_COL32(124, 178, 238, 214), IM_COL32(50, 96, 157, 230), IM_COL32(214, 235, 255, 234),
                IM_COL32(78, 125, 190, 106), IM_COL32(25, 55, 98, 155),
                IM_COL32(116, 168, 228, 158), IM_COL32(38, 80, 136, 196),
                IM_COL32(188, 220, 255, 198), IM_COL32(226, 244, 255, 248)
            };
    }
}

static void ApplyCustomAeroOverrides(AeroThemeColors &theme)
{
    if (!g_overlay_colors.enabled) return;

    ImVec4 text = Clamp01(g_overlay_colors.text);
    ImVec4 win = Clamp01(g_overlay_colors.window_bg);
    ImVec4 border = Clamp01(g_overlay_colors.border);
    ImVec4 btn = Clamp01(g_overlay_colors.button);
    ImVec4 btn_h = Clamp01(g_overlay_colors.button_hovered);
    ImVec4 btn_a = Clamp01(g_overlay_colors.button_active);
    ImVec4 tab = Clamp01(g_overlay_colors.tab);
    ImVec4 tab_a = Clamp01(g_overlay_colors.tab_active);
    ImVec4 accent = Clamp01(g_overlay_colors.accent);

    theme.text = ToU32(text);
    theme.text_soft = ToU32(Mix(text, win, 0.35f));
    theme.overlay_top = ToU32(ImVec4(win.x, win.y, win.z, std::min(1.0f, win.w * 0.65f)));
    theme.overlay_bottom = ToU32(ImVec4(win.x, win.y, win.z, std::min(1.0f, win.w * 0.95f)));
    theme.overlay_border = ToU32(border);
    theme.panel_top_left = ToU32(Mix(win, tab, 0.28f));
    theme.panel_top_right = ToU32(Mix(win, tab, 0.24f));
    theme.panel_bottom_left = ToU32(Mix(win, btn, 0.22f));
    theme.panel_bottom_right = ToU32(Mix(win, btn, 0.26f));
    theme.panel_border = ToU32(border);
    theme.title_top = ToU32(Mix(tab_a, accent, 0.20f));
    theme.title_bottom = ToU32(tab_a);
    theme.title_border = ToU32(border);
    theme.button_top = ToU32(btn);
    theme.button_bottom = ToU32(Mix(btn, win, 0.18f));
    theme.button_top_hover = ToU32(btn_h);
    theme.button_bottom_hover = ToU32(Mix(btn_h, win, 0.14f));
    theme.button_top_active = ToU32(btn_a);
    theme.button_bottom_active = ToU32(Mix(btn_a, win, 0.12f));
    theme.button_border = ToU32(border);
    theme.tab_top = ToU32(tab);
    theme.tab_bottom = ToU32(Mix(tab, win, 0.16f));
    theme.tab_top_active = ToU32(tab_a);
    theme.tab_bottom_active = ToU32(Mix(tab_a, win, 0.12f));
    theme.tab_border = ToU32(border);
    theme.row_top = ToU32(Mix(win, tab, 0.14f));
    theme.row_bottom = ToU32(Mix(win, btn, 0.16f));
    theme.row_top_hover = ToU32(Mix(win, tab_a, 0.24f));
    theme.row_bottom_hover = ToU32(Mix(win, btn_h, 0.24f));
    theme.row_border = ToU32(Mix(border, accent, 0.20f));
    theme.row_border_hover = ToU32(Mix(border, accent, 0.45f));
}

static void DrawRetroPanel(const ImVec2 &min, const ImVec2 &max, bool double_border = false)
{
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 p0 = PixelAlign(min);
    ImVec2 p1 = PixelAlign(max);
    ImU32 bg = ImGui::GetColorU32(ImGuiCol_ChildBg);
    ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    draw->AddRectFilled(p0, p1, bg);
    draw->AddRect(p0, p1, border, 0.0f, 0, 1.0f);
    if (double_border && (p1.x - p0.x) > 4.0f && (p1.y - p0.y) > 4.0f) {
        draw->AddRect(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p1.x - 1.0f, p1.y - 1.0f), border, 0.0f, 0, 1.0f);
    }
}

static void DrawRetroSeparator()
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    if (width <= 0.0f) return;

    float y = std::floor(pos.y + ImGui::GetTextLineHeight() * 0.5f);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(std::floor(pos.x), y), ImVec2(std::floor(pos.x + width), y), ImGui::GetColorU32(ImGuiCol_Border), 1.0f);
    ImGui::Dummy(ImVec2(width, ImGui::GetStyle().ItemSpacing.y));
}

static bool RetroButton(const char *id, const char *label, const ImVec2 &size_arg = ImVec2(0.0f, 0.0f))
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 size = size_arg;
    if (size.x <= 0.0f) size.x = text_size.x + style.FramePadding.x * 2.0f;
    if (size.y <= 0.0f) size.y = text_size.y + style.FramePadding.y * 2.0f;

    ImVec2 pos = PixelAlign(ImGui::GetCursorScreenPos());
    bool pressed = ImGui::InvisibleButton(id, size);

    ImU32 bg = ImGui::GetColorU32(ImGuiCol_Button);
    if (ImGui::IsItemActive()) {
        bg = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    } else if (ImGui::IsItemHovered()) {
        bg = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    }

    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 max = PixelAlign(ImVec2(pos.x + size.x, pos.y + size.y));
    draw->AddRectFilled(pos, max, bg);
    draw->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_Border), 0.0f, 0, 1.0f);
    ImVec2 text_pos = PixelAlign(ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f));
    draw->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label);
    return pressed;
}

static bool RetroTab(const char *id, const char *label, bool active, const ImVec2 &size_arg = ImVec2(0.0f, 0.0f))
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 size = size_arg;
    if (size.x <= 0.0f) size.x = text_size.x + style.FramePadding.x * 2.0f;
    if (size.y <= 0.0f) size.y = text_size.y + style.FramePadding.y * 2.0f;

    ImVec2 pos = PixelAlign(ImGui::GetCursorScreenPos());
    bool pressed = ImGui::InvisibleButton(id, size);

    ImU32 bg = ImGui::GetColorU32(active ? ImGuiCol_TabActive : ImGuiCol_Tab);
    if (!active && ImGui::IsItemHovered()) {
        bg = ImGui::GetColorU32(ImGuiCol_TabHovered);
    }

    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 max = PixelAlign(ImVec2(pos.x + size.x, pos.y + size.y));
    draw->AddRectFilled(pos, max, bg);
    draw->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_Border), 0.0f, 0, 1.0f);
    ImVec2 text_pos = PixelAlign(ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f));
    draw->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label);
    return pressed;
}

static bool RetroCheckbox(const char *id, const char *label, bool *value, bool read_only = false)
{
    std::string checkbox_text = (*value ? "[X] " : "[ ] ");
    checkbox_text += label;

    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text_size = ImGui::CalcTextSize(checkbox_text.c_str());
    ImVec2 size = ImVec2(text_size.x + style.FramePadding.x * 2.0f, text_size.y + style.FramePadding.y * 2.0f);
    ImVec2 pos = PixelAlign(ImGui::GetCursorScreenPos());
    bool pressed = ImGui::InvisibleButton(id, size);
    if (pressed && !read_only) {
        *value = !*value;
    }

    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        draw->AddRectFilled(pos, PixelAlign(ImVec2(pos.x + size.x, pos.y + size.y)), ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    draw->AddText(PixelAlign(ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y)), ImGui::GetColorU32(ImGuiCol_Text), checkbox_text.c_str());
    return pressed && !read_only;
}

static bool AeroButton(const char *id, const char *label, const AeroThemeColors &theme, const ImVec2 &size_arg = ImVec2(0.0f, 0.0f))
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 size = size_arg;
    if (size.x <= 0.0f) size.x = text_size.x + style.FramePadding.x * 2.2f;
    if (size.y <= 0.0f) size.y = text_size.y + style.FramePadding.y * 1.8f + 1.0f;

    ImVec2 pos = PixelAlign(ImGui::GetCursorScreenPos());
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 max = PixelAlign(ImVec2(pos.x + size.x, pos.y + size.y));
    ImU32 c0 = theme.button_top;
    ImU32 c1 = theme.button_bottom;
    if (active) {
        c0 = theme.button_top_active;
        c1 = theme.button_bottom_active;
    } else if (hovered) {
        c0 = theme.button_top_hover;
        c1 = theme.button_bottom_hover;
    }
    draw->AddRectFilledMultiColor(pos, max, c0, c0, c1, c1);
    draw->AddRect(pos, max, theme.button_border, 4.0f, 0, 1.0f);
    draw->AddText(PixelAlign(ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f)),
                  theme.text, label);
    return pressed;
}

static bool AeroTab(const char *id, const char *label, bool active, const AeroThemeColors &theme, const ImVec2 &size_arg = ImVec2(0.0f, 0.0f))
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 size = size_arg;
    if (size.x <= 0.0f) size.x = text_size.x + style.FramePadding.x * 2.2f;
    if (size.y <= 0.0f) size.y = text_size.y + style.FramePadding.y * 1.8f + 1.0f;

    ImVec2 pos = PixelAlign(ImGui::GetCursorScreenPos());
    bool pressed = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();

    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 max = PixelAlign(ImVec2(pos.x + size.x, pos.y + size.y));
    ImU32 c0 = active ? theme.tab_top_active : theme.tab_top;
    ImU32 c1 = active ? theme.tab_bottom_active : theme.tab_bottom;
    if (!active && hovered) {
        c0 = theme.button_top_hover;
        c1 = theme.button_bottom_hover;
    }
    draw->AddRectFilledMultiColor(pos, max, c0, c0, c1, c1);
    draw->AddRect(pos, max, theme.tab_border, 4.0f, 0, 1.0f);
    draw->AddText(PixelAlign(ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f)),
                  theme.text, label);
    return pressed;
}

static custom_ui::InputState BuildCustomUiInput(ImGuiIO const& io)
{
    custom_ui::InputState input = {};
    input.mouse_x = io.MousePos.x;
    input.mouse_y = io.MousePos.y;
    input.mouse_dx = io.MouseDelta.x;
    input.mouse_dy = io.MouseDelta.y;
    input.left_down = io.MouseDown[0];
    input.left_pressed = io.MouseClicked[0];
    input.left_released = io.MouseReleased[0];
    input.left_double_clicked = io.MouseDoubleClicked[0];
    input.right_pressed = io.MouseClicked[1];
    return input;
}

static bool CustomUiAeroButton(custom_ui::UiContext &ctx, ImDrawList *draw, const AeroThemeColors &theme, const char *id_text, const char *label, const ImVec2 &min, const ImVec2 &size, bool active_visual = false)
{
    custom_ui::Rect rect = { min.x, min.y, size.x, size.y };
    bool hovered = false;
    bool held = false;
    bool clicked = ctx.Button(custom_ui::HashId(id_text), rect, &hovered, &held);

    ImVec2 p0 = PixelAlign(min);
    ImVec2 p1 = PixelAlign(ImVec2(min.x + size.x, min.y + size.y));

    ImU32 c0 = active_visual ? theme.tab_top_active : theme.button_top;
    ImU32 c1 = active_visual ? theme.tab_bottom_active : theme.button_bottom;
    if (held) {
        c0 = theme.button_top_active;
        c1 = theme.button_bottom_active;
    } else if (hovered) {
        c0 = theme.button_top_hover;
        c1 = theme.button_bottom_hover;
    }

    draw->AddRectFilledMultiColor(p0, p1, c0, c0, c1, c1);
    draw->AddRect(p0, p1, active_visual ? theme.tab_border : theme.button_border, 4.0f, 0, 1.0f);

    ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 text_pos = PixelAlign(ImVec2(min.x + (size.x - text_size.x) * 0.5f, min.y + (size.y - text_size.y) * 0.5f));
    draw->AddText(text_pos, theme.text, label);
    return clicked;
}
} // namespace

int find_free_id(std::vector<int> & ids, int base)
{
    std::sort(ids.begin(), ids.end());

    int id = base;
    for (auto i : ids)
    {
        if (id < i)
            break;
        id = i + 1;
    }

    return id > (base+max_window_id) ? 0 : id;
}

int find_free_friend_id(std::map<Friend, friend_window_state, Friend_Less> const& friend_windows)
{
    std::vector<int> ids;
    ids.reserve(friend_windows.size());

    std::for_each(friend_windows.begin(), friend_windows.end(), [&ids](std::pair<Friend const, friend_window_state> const& i)
    {
        ids.emplace_back(i.second.id);
    });
    
    return find_free_id(ids, base_friend_window_id);
}

int find_free_notification_id(std::vector<Notification> const& notifications)
{
    std::vector<int> ids;
    ids.reserve(notifications.size());

    std::for_each(notifications.begin(), notifications.end(), [&ids](Notification const& i)
    {
        ids.emplace_back(i.id);
    });
    

    return find_free_id(ids, base_friend_window_id);
}

#ifdef __WINDOWS__
#include "windows/Windows_Hook.h"
#endif

#include "notification.h"

void Steam_Overlay::steam_overlay_run_every_runcb(void* object)
{
    Steam_Overlay* _this = reinterpret_cast<Steam_Overlay*>(object);
    _this->RunCallbacks();
}

void Steam_Overlay::steam_overlay_callback(void* object, Common_Message* msg)
{
    Steam_Overlay* _this = reinterpret_cast<Steam_Overlay*>(object);
    _this->Callback(msg);
}

Steam_Overlay::Steam_Overlay(Settings* settings, SteamCallResults* callback_results, SteamCallBacks* callbacks, RunEveryRunCB* run_every_runcb, Networking* network) :
    settings(settings),
    callback_results(callback_results),
    callbacks(callbacks),
    run_every_runcb(run_every_runcb),
    network(network),
    setup_overlay_called(false),
    show_overlay(false),
    is_ready(false),
    notif_position(ENotificationPosition::k_EPositionBottomLeft),
    h_inset(0),
    v_inset(0),
    overlay_state_changed(false),
    i_have_lobby(false),
    show_achievements(false),
    show_settings(false),
    current_theme(0),
    current_frontend(0),
    current_ui_scale(1),
    theme_loaded(false),
    frontend_loaded(false),
    ui_scale_loaded(false),
    custom_context_open(false),
    custom_context_friend_id(0),
    custom_context_x(0.0f),
    custom_context_y(0.0f),
    _renderer(nullptr),
    fonts_atlas(nullptr),
    save_settings(false)
{
    strncpy(username_text, settings->get_local_name(), sizeof(username_text) - 1);
    username_text[sizeof(username_text) - 1] = '\0';

    if (settings->warn_forced) {
        this->disable_forced = true;
        this->warning_forced = true;
    } else {
        this->disable_forced = false;
        this->warning_forced = false;
    }

    if (settings->warn_local_save) {
        this->local_save = true;
    } else {
        this->local_save = false;
    }

    current_language = 0;
    const char *language = settings->get_language();

    int i = 0;
    for (auto l : valid_languages) {
        if (strcmp(l, language) == 0) {
            current_language = i;
            break;
        }

        ++i;
    }

    run_every_runcb->add(&Steam_Overlay::steam_overlay_run_every_runcb, this);
    this->network->setCallback(CALLBACK_ID_STEAM_MESSAGES, settings->get_local_steam_id(), &Steam_Overlay::steam_overlay_callback, this);
}

Steam_Overlay::~Steam_Overlay()
{
    run_every_runcb->remove(&Steam_Overlay::steam_overlay_run_every_runcb, this);
}

bool Steam_Overlay::Ready() const
{
    return is_ready;
}

bool Steam_Overlay::NeedPresent() const
{
    return true;
}

void Steam_Overlay::SetNotificationPosition(ENotificationPosition eNotificationPosition)
{
    notif_position = eNotificationPosition;
}

void Steam_Overlay::SetNotificationInset(int nHorizontalInset, int nVerticalInset)
{
    h_inset = nHorizontalInset;
    v_inset = nVerticalInset;
}

void Steam_Overlay::SetupOverlay()
{
    PRINT_DEBUG("%s\n", __FUNCTION__);
    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    if (!setup_overlay_called)
    {
        setup_overlay_called = true;
        future_renderer = ingame_overlay::DetectRenderer();
    }
}


void Steam_Overlay::UnSetupOverlay()
{
    ingame_overlay::StopRendererDetection();
    if (!Ready() && future_renderer.valid()) {
        if (future_renderer.wait_for(std::chrono::milliseconds{500}) ==  std::future_status::ready) {
            future_renderer.get();
            ingame_overlay::FreeDetector();
        }
    }
}

void Steam_Overlay::HookReady(bool ready)
{
    PRINT_DEBUG("%s %u\n", __FUNCTION__, ready);
    {
        // TODO: Uncomment this and draw our own cursor (cosmetics)
        ImGuiIO &io = ImGui::GetIO();
        //io.WantSetMousePos = false;
        //io.MouseDrawCursor = false;
        //io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        io.IniFilename = NULL;

        is_ready = ready;
    }
}

void Steam_Overlay::OpenOverlayInvite(CSteamID lobbyId)
{
    ShowOverlay(true);
}

void Steam_Overlay::OpenOverlay(const char* pchDialog)
{
    // TODO: Show pages depending on pchDialog
    ShowOverlay(true);
}

void Steam_Overlay::OpenOverlayWebpage(const char* pchURL)
{
    show_url = pchURL;
    ShowOverlay(true);
}

bool Steam_Overlay::ShowOverlay() const
{
    return show_overlay;
}

bool Steam_Overlay::OpenOverlayHook(bool toggle)
{
    if (toggle) {
        ShowOverlay(!show_overlay);
    }

    return show_overlay;
}

void Steam_Overlay::ShowOverlay(bool state)
{
    if (!Ready() || show_overlay == state)
        return;

    ImGuiIO &io = ImGui::GetIO();

    // Ensure a reliable cursor when overlay is open, even in games that hide OS cursor.
    io.MouseDrawCursor = state;

#ifdef __WINDOWS__
    static RECT old_clip;

    if (state)
    {
        HWND game_hwnd = Windows_Hook::Inst()->GetGameHwnd();
        RECT cliRect, wndRect, clipRect;

        GetClipCursor(&old_clip);
        // The window rectangle has borders and menus counted in the size
        GetWindowRect(game_hwnd, &wndRect);
        // The client rectangle is the window without borders
        GetClientRect(game_hwnd, &cliRect);

        clipRect = wndRect; // Init clip rectangle

        // Get Window width with borders
        wndRect.right -= wndRect.left;
        // Get Window height with borders & menus
        wndRect.bottom -= wndRect.top;
        // Compute the border width
        int borderWidth = (wndRect.right - cliRect.right) / 2;
        // Client top clip is the menu bar width minus bottom border
        clipRect.top += wndRect.bottom - cliRect.bottom - borderWidth;
        // Client left clip is the left border minus border width
        clipRect.left += borderWidth;
        // Here goes the same for right and bottom
        clipRect.right -= borderWidth;
        clipRect.bottom -= borderWidth;

        ClipCursor(&clipRect);
    }
    else
    {
        ClipCursor(&old_clip);
    }

#else

#endif

    show_overlay = state;
    overlay_state_changed = true;
}

void Steam_Overlay::NotifyUser(friend_window_state& friend_state)
{
    if (!(friend_state.window_state & window_state_show) || !show_overlay)
    {
        friend_state.window_state |= window_state_need_attention;
#ifdef __WINDOWS__
        PlaySound((LPCSTR)notif_invite_wav, NULL, SND_ASYNC | SND_MEMORY);
#endif
    }
}

void Steam_Overlay::QueueFriendAction(Friend const& frd)
{
    has_friend_action.push(frd);
}

void Steam_Overlay::SetupOverlayFrontend()
{
    OverlayFrontendKind kind = OverlayFrontendKind::ImGui;
    if (current_frontend == static_cast<int>(OverlayFrontendKind::CustomAero)) {
        kind = OverlayFrontendKind::CustomAero;
    }
    overlay_frontend = CreateOverlayFrontend(kind);
}

void Steam_Overlay::SetLobbyInvite(Friend friendId, uint64 lobbyId)
{
    if (!Ready())
        return;

    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    auto i = friends.find(friendId);
    if (i != friends.end())
    {
        auto& frd = i->second;
        frd.lobbyId = lobbyId;
        frd.window_state |= window_state_lobby_invite;
        // Make sure don't have rich presence invite and a lobby invite (it should not happen but who knows)
        frd.window_state &= ~window_state_rich_invite;
        AddInviteNotification(*i);
        NotifyUser(i->second);
    }
}

void Steam_Overlay::SetRichInvite(Friend friendId, const char* connect_str)
{
    if (!Ready())
        return;

    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    auto i = friends.find(friendId);
    if (i != friends.end())
    {
        auto& frd = i->second;
        strncpy(frd.connect, connect_str, k_cchMaxRichPresenceValueLength - 1);
        frd.window_state |= window_state_rich_invite;
        // Make sure don't have rich presence invite and a lobby invite (it should not happen but who knows)
        frd.window_state &= ~window_state_lobby_invite;
        AddInviteNotification(*i);
        NotifyUser(i->second);
    }
}

void Steam_Overlay::FriendConnect(Friend _friend)
{
    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    int id = find_free_friend_id(friends);
    if (id != 0)
    {
        auto& item = friends[_friend];
        item.window_title = std::move(_friend.name() + " playing " + std::to_string(_friend.appid()));
        item.window_state = window_state_none;
        item.id = id;
        memset(item.chat_input, 0, max_chat_len);
        item.joinable = false;
    }
    else
        PRINT_DEBUG("No more free id to create a friend window\n");
}

void Steam_Overlay::FriendDisconnect(Friend _friend)
{
    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    auto it = friends.find(_friend);
    if (it != friends.end())
        friends.erase(it);
}

void Steam_Overlay::AddMessageNotification(std::string const& message)
{
    std::lock_guard<std::recursive_mutex> lock(notifications_mutex);
    int id = find_free_notification_id(notifications);
    if (id != 0)
    {
        Notification notif;
        notif.id = id;
        notif.type = notification_type_message;
        notif.message = message;
        notif.start_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
        notifications.emplace_back(notif);
        have_notifications = true;
    }
    else
        PRINT_DEBUG("No more free id to create a notification window\n");
}

void Steam_Overlay::AddAchievementNotification(nlohmann::json const& ach)
{
    std::lock_guard<std::recursive_mutex> lock(notifications_mutex);
    int id = find_free_notification_id(notifications);
    if (id != 0)
    {
        Notification notif;
        notif.id = id;
        notif.type = notification_type_achievement;
        // Load achievement image
        notif.message = ach["displayName"].get<std::string>() + "\n" + ach["description"].get<std::string>();
        notif.start_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
        notifications.emplace_back(notif);
        have_notifications = true;
    }
    else
        PRINT_DEBUG("No more free id to create a notification window\n");

    std::string ach_name = ach.value("name", "");
    for (auto &a : achievements) {
        if (a.name == ach_name) {
            bool achieved = false;
            uint32 unlock_time = 0;
            get_steam_client()->steam_user_stats->GetAchievementAndUnlockTime(a.name.c_str(), &achieved, &unlock_time);
            a.achieved = achieved;
            a.unlock_time = unlock_time;
        }
    }
}

void Steam_Overlay::AddInviteNotification(std::pair<const Friend, friend_window_state>& wnd_state)
{
    std::lock_guard<std::recursive_mutex> lock(notifications_mutex);
    int id = find_free_notification_id(notifications);
    if (id != 0)
    {
        Notification notif;
        notif.id = id;
        notif.type = notification_type_invite;
        notif.frd = &wnd_state;
        notif.message = wnd_state.first.name() + " invited you to join a game";
        notif.start_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
        notifications.emplace_back(notif);
        have_notifications = true;
    }
    else
        PRINT_DEBUG("No more free id to create a notification window\n");
}

bool Steam_Overlay::FriendJoinable(std::pair<const Friend, friend_window_state> &f)
{
    Steam_Friends* steamFriends = get_steam_client()->steam_friends;

    if( std::string(steamFriends->GetFriendRichPresence(f.first.id(), "connect")).length() > 0 )
        return true;

    FriendGameInfo_t friend_game_info = {};
    steamFriends->GetFriendGamePlayed(f.first.id(), &friend_game_info);
    if (friend_game_info.m_steamIDLobby.IsValid() && (f.second.window_state & window_state_lobby_invite))
        return true;

    return false;
}

bool Steam_Overlay::IHaveLobby()
{
    Steam_Friends* steamFriends = get_steam_client()->steam_friends;
    if (std::string(steamFriends->GetFriendRichPresence(settings->get_local_steam_id(), "connect")).length() > 0)
        return true;

    if (settings->get_lobby().IsValid())
        return true;

    return false;
}

void Steam_Overlay::BuildContextMenu(Friend const& frd, friend_window_state& state)
{
    if (ImGui::BeginPopupContextItem("Friends_ContextMenu", 1))
    {
        bool close_popup = false;

        if (RetroButton("##ctx_chat", "Chat"))
        {
            state.window_state |= window_state_show;
            close_popup = true;
        }
        // If we have the same appid, activate the invite/join buttons
        if (settings->get_local_game_id().AppID() == frd.appid())
        {
            if (RetroButton("##ctx_invite", "Invite"))
            {
                state.window_state |= window_state_invite;
                QueueFriendAction(frd);
                close_popup = true;
            }
            if (state.joinable && RetroButton("##ctx_join", "Join"))
            {
                state.window_state |= window_state_join;
                QueueFriendAction(frd);
                close_popup = true;
            }
        }
        if( close_popup)
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Steam_Overlay::BuildFriendWindow(Friend const& frd, friend_window_state& state)
{
    if (!(state.window_state & window_state_show))
        return;

    if (current_frontend == static_cast<int>(OverlayFrontendKind::CustomAero)) {
        BuildFriendWindowCustomAero(frd, state);
        return;
    }

    bool show = true;
    bool send_chat_msg = false;

    float width = ImGui::CalcTextSize("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA").x;
    
    if (state.window_state & window_state_need_attention && ImGui::IsWindowFocused())
    {
        state.window_state &= ~window_state_need_attention;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2{ width, ImGui::GetFontSize()*8 + ImGui::GetFrameHeightWithSpacing()*4 },
        ImVec2{ std::numeric_limits<float>::max() , std::numeric_limits<float>::max() });

    // Window id is after the ###, the window title is the friend name
    std::string friend_window_id = std::move("###" + std::to_string(state.id));
    if (ImGui::Begin((state.window_title + friend_window_id).c_str(), &show))
    {
        if (state.window_state & window_state_need_attention && ImGui::IsWindowFocused())
        {
            state.window_state &= ~window_state_need_attention;
        }

        // Fill this with the chat box and maybe the invitation
        if (state.window_state & (window_state_lobby_invite | window_state_rich_invite))
        {
            ImGui::LabelText("##label", "%s invited you to join the game.", frd.name().c_str());
            ImGui::SameLine();
            if (RetroButton("##accept_invite", "Accept"))
            {
                state.window_state |= window_state_join;
                QueueFriendAction(frd);
            }
            ImGui::SameLine();
            if (RetroButton("##refuse_invite", "Refuse"))
            {
                state.window_state &= ~(window_state_lobby_invite | window_state_rich_invite);
            }
        }

        DrawChatHistoryWithEmbeds("##chat_history_embed", state.chat_history, show_url, -2.0f * ImGui::GetFontSize());
        // TODO: Fix the layout of the chat line + send button.
        // It should be like this: chat input should fill the window size minus send button size (button size is fixed)
        // |------------------------------|
        // | /--------------------------\ |
        // | |                          | |
        // | |       chat history       | |
        // | |                          | |
        // | \--------------------------/ |
        // | [____chat line______] [send] |
        // |------------------------------|
        //
        // And it is like this
        // |------------------------------|
        // | /--------------------------\ |
        // | |                          | |
        // | |       chat history       | |
        // | |                          | |
        // | \--------------------------/ |
        // | [__chat line__] [send]       |
        // |------------------------------|
        float wnd_width = ImGui::GetWindowContentRegionWidth();
        ImGuiStyle &style = ImGui::GetStyle();
        wnd_width -= ImGui::CalcTextSize("Send").x + style.FramePadding.x * 2 + style.ItemSpacing.x + 1;

        ImGui::PushItemWidth(wnd_width);
        if (ImGui::InputText("##chat_line", state.chat_input, max_chat_len, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            send_chat_msg = true;
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();

        if (RetroButton("##chat_send", "Send"))
        {
            send_chat_msg = true;
        }

        if (send_chat_msg)
        {
            if (!(state.window_state & window_state_send_message))
            {
                QueueFriendAction(frd);
                state.window_state |= window_state_send_message;
            }
        }
    }
    // User closed the friend window
    if (!show)
        state.window_state &= ~window_state_show;

    ImGui::End();
}

void Steam_Overlay::BuildFriendWindowCustomAero(Friend const& frd, friend_window_state& state)
{
    if (!(state.window_state & window_state_show))
        return;

    AeroThemeColors theme = GetAeroThemeColors(current_theme);
    ApplyCustomAeroOverrides(theme);
    bool show = true;
    bool send_chat_msg = false;
    float width = ImGui::CalcTextSize("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA").x;

    if (state.window_state & window_state_need_attention && ImGui::IsWindowFocused()) {
        state.window_state &= ~window_state_need_attention;
    }

    ImGui::SetNextWindowSizeConstraints(
        ImVec2{ width, ImGui::GetFontSize() * 8 + ImGui::GetFrameHeightWithSpacing() * 4 },
        ImVec2{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() });

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.18f, 0.30f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(theme.panel_border));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImGui::ColorConvertU32ToFloat4(theme.title_top));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImGui::ColorConvertU32ToFloat4(theme.title_bottom));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));

    std::string friend_window_id = std::move("###" + std::to_string(state.id));
    if (ImGui::Begin((state.window_title + friend_window_id).c_str(), &show))
    {
        if (state.window_state & window_state_need_attention && ImGui::IsWindowFocused()) {
            state.window_state &= ~window_state_need_attention;
        }

        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImVec2 w0 = ImGui::GetWindowPos();
        ImVec2 w1 = ImVec2(w0.x + ImGui::GetWindowSize().x, w0.y + ImGui::GetWindowSize().y);
        draw->AddRectFilledMultiColor(w0, w1,
            theme.panel_top_left, theme.panel_top_right,
            theme.panel_bottom_left, theme.panel_bottom_right);
        draw->AddRect(w0, w1, theme.panel_border, 4.0f, 0, 1.0f);

        if (state.window_state & (window_state_lobby_invite | window_state_rich_invite))
        {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.text), "%s invited you to join the game.", frd.name().c_str());
            if (AeroButton("##accept_invite_aero", "Accept", theme, ImVec2(ImGui::GetFontSize() * 5.4f, 0.0f))) {
                state.window_state |= window_state_join;
                QueueFriendAction(frd);
            }
            ImGui::SameLine();
            if (AeroButton("##refuse_invite_aero", "Refuse", theme, ImVec2(ImGui::GetFontSize() * 5.4f, 0.0f))) {
                state.window_state &= ~(window_state_lobby_invite | window_state_rich_invite);
            }
        }

        DrawChatHistoryWithEmbeds("##chat_history_aero_embed", state.chat_history, show_url, -2.0f * ImGui::GetFontSize());

        float wnd_width = ImGui::GetWindowContentRegionWidth();
        ImGuiStyle &style = ImGui::GetStyle();
        wnd_width -= ImGui::CalcTextSize("Send").x + style.FramePadding.x * 2 + style.ItemSpacing.x + 2.0f;

        ImGui::PushItemWidth(wnd_width);
        if (ImGui::InputText("##chat_line_aero", state.chat_input, max_chat_len, ImGuiInputTextFlags_EnterReturnsTrue)) {
            send_chat_msg = true;
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (AeroButton("##chat_send_aero", "Send", theme)) {
            send_chat_msg = true;
        }

        if (send_chat_msg) {
            if (!(state.window_state & window_state_send_message)) {
                QueueFriendAction(frd);
                state.window_state |= window_state_send_message;
            }
        }
    }

    if (!show)
        state.window_state &= ~window_state_show;

    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(5);
}

ImFont *font_default;
ImFont *font_notif;

void Steam_Overlay::ApplyRetroStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    if (current_ui_scale < 0 || current_ui_scale >= static_cast<int>(sizeof(overlay_ui_scales) / sizeof(overlay_ui_scales[0]))) {
        current_ui_scale = 1;
    }
    float ui_scale = GetOverlayUiScale(current_ui_scale);
    if (current_theme < 0 || current_theme >= static_cast<int>(sizeof(retro_themes) / sizeof(retro_themes[0]))) {
        current_theme = 0;
    }

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.LogSliderDeadzone = 0.0f;
    style.TabRounding = 0.0f;

    style.WindowPadding = ImVec2(5.0f * ui_scale, 5.0f * ui_scale);
    style.FramePadding = ImVec2(4.0f * ui_scale, 2.0f * ui_scale);
    style.ItemSpacing = ImVec2(3.0f * ui_scale, 3.0f * ui_scale);
    style.ItemInnerSpacing = ImVec2(3.0f * ui_scale, 2.0f * ui_scale);
    style.CellPadding = ImVec2(4.0f * ui_scale, 2.0f * ui_scale);
    style.IndentSpacing = 10.0f * ui_scale;
    style.ScrollbarSize = 12.0f * ui_scale;
    style.GrabMinSize = 8.0f * ui_scale;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;

    style.AntiAliasedFill = false;
    style.AntiAliasedLines = false;
    style.AntiAliasedLinesUseTex = false;

    struct RetroPalette {
        ImVec4 text;
        ImVec4 text_disabled;
        ImVec4 window_bg;
        ImVec4 child_bg;
        ImVec4 popup_bg;
        ImVec4 border;
        ImVec4 frame_bg;
        ImVec4 frame_bg_hovered;
        ImVec4 frame_bg_active;
        ImVec4 button;
        ImVec4 button_hovered;
        ImVec4 button_active;
        ImVec4 accent;
    };

    RetroPalette p = {};
    switch (current_theme) {
        case 1: // Amber CRT
            p.text = ImVec4(0.99f, 0.78f, 0.33f, 1.0f);
            p.text_disabled = ImVec4(0.56f, 0.42f, 0.18f, 1.0f);
            p.window_bg = ImVec4(0.06f, 0.03f, 0.00f, 0.58f);
            p.child_bg = ImVec4(0.05f, 0.02f, 0.00f, 0.72f);
            p.popup_bg = ImVec4(0.08f, 0.04f, 0.00f, 0.96f);
            p.border = ImVec4(0.86f, 0.54f, 0.16f, 1.0f);
            p.frame_bg = ImVec4(0.10f, 0.05f, 0.00f, 0.82f);
            p.frame_bg_hovered = ImVec4(0.16f, 0.08f, 0.01f, 0.90f);
            p.frame_bg_active = ImVec4(0.22f, 0.11f, 0.02f, 0.95f);
            p.button = ImVec4(0.11f, 0.06f, 0.00f, 1.0f);
            p.button_hovered = ImVec4(0.20f, 0.10f, 0.01f, 1.0f);
            p.button_active = ImVec4(0.30f, 0.14f, 0.03f, 1.0f);
            p.accent = ImVec4(1.00f, 0.73f, 0.29f, 1.0f);
            break;
        case 2: // Windows 98
            p.text = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
            p.text_disabled = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
            p.window_bg = ImVec4(0.72f, 0.72f, 0.72f, 0.82f);
            p.child_bg = ImVec4(0.80f, 0.80f, 0.80f, 0.88f);
            p.popup_bg = ImVec4(0.78f, 0.78f, 0.78f, 0.96f);
            p.border = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
            p.frame_bg = ImVec4(0.86f, 0.86f, 0.86f, 0.95f);
            p.frame_bg_hovered = ImVec4(0.91f, 0.91f, 0.91f, 1.0f);
            p.frame_bg_active = ImVec4(0.96f, 0.96f, 0.96f, 1.0f);
            p.button = ImVec4(0.84f, 0.84f, 0.84f, 1.0f);
            p.button_hovered = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
            p.button_active = ImVec4(0.76f, 0.76f, 0.76f, 1.0f);
            p.accent = ImVec4(0.00f, 0.00f, 0.00f, 1.0f);
            break;
        case 3: // Aero Blue
            p.text = ImVec4(0.93f, 0.97f, 1.00f, 1.0f);
            p.text_disabled = ImVec4(0.62f, 0.71f, 0.83f, 1.0f);
            p.window_bg = ImVec4(0.07f, 0.12f, 0.22f, 0.52f);
            p.child_bg = ImVec4(0.08f, 0.15f, 0.27f, 0.66f);
            p.popup_bg = ImVec4(0.09f, 0.17f, 0.30f, 0.92f);
            p.border = ImVec4(0.74f, 0.84f, 0.98f, 0.95f);
            p.frame_bg = ImVec4(0.16f, 0.30f, 0.50f, 0.78f);
            p.frame_bg_hovered = ImVec4(0.24f, 0.42f, 0.65f, 0.84f);
            p.frame_bg_active = ImVec4(0.18f, 0.35f, 0.58f, 0.90f);
            p.button = ImVec4(0.19f, 0.38f, 0.62f, 0.88f);
            p.button_hovered = ImVec4(0.27f, 0.50f, 0.75f, 0.92f);
            p.button_active = ImVec4(0.15f, 0.32f, 0.53f, 0.95f);
            p.accent = ImVec4(0.68f, 0.84f, 1.00f, 1.0f);
            break;
        case 4: // Aero Slate
            p.text = ImVec4(0.95f, 0.97f, 1.00f, 1.0f);
            p.text_disabled = ImVec4(0.65f, 0.69f, 0.78f, 1.0f);
            p.window_bg = ImVec4(0.09f, 0.10f, 0.14f, 0.52f);
            p.child_bg = ImVec4(0.13f, 0.15f, 0.21f, 0.68f);
            p.popup_bg = ImVec4(0.15f, 0.17f, 0.23f, 0.92f);
            p.border = ImVec4(0.82f, 0.86f, 0.95f, 0.94f);
            p.frame_bg = ImVec4(0.24f, 0.28f, 0.38f, 0.78f);
            p.frame_bg_hovered = ImVec4(0.32f, 0.36f, 0.47f, 0.86f);
            p.frame_bg_active = ImVec4(0.26f, 0.31f, 0.42f, 0.92f);
            p.button = ImVec4(0.30f, 0.35f, 0.46f, 0.88f);
            p.button_hovered = ImVec4(0.39f, 0.45f, 0.58f, 0.92f);
            p.button_active = ImVec4(0.24f, 0.28f, 0.39f, 0.95f);
            p.accent = ImVec4(0.82f, 0.89f, 1.00f, 1.0f);
            break;
        case 5: // Aero Emerald
            p.text = ImVec4(0.93f, 1.00f, 0.96f, 1.0f);
            p.text_disabled = ImVec4(0.60f, 0.76f, 0.68f, 1.0f);
            p.window_bg = ImVec4(0.03f, 0.13f, 0.10f, 0.50f);
            p.child_bg = ImVec4(0.05f, 0.19f, 0.15f, 0.66f);
            p.popup_bg = ImVec4(0.05f, 0.22f, 0.17f, 0.92f);
            p.border = ImVec4(0.72f, 0.95f, 0.84f, 0.94f);
            p.frame_bg = ImVec4(0.08f, 0.30f, 0.24f, 0.78f);
            p.frame_bg_hovered = ImVec4(0.12f, 0.40f, 0.32f, 0.86f);
            p.frame_bg_active = ImVec4(0.10f, 0.35f, 0.28f, 0.92f);
            p.button = ImVec4(0.10f, 0.39f, 0.31f, 0.88f);
            p.button_hovered = ImVec4(0.15f, 0.50f, 0.40f, 0.92f);
            p.button_active = ImVec4(0.08f, 0.31f, 0.25f, 0.95f);
            p.accent = ImVec4(0.76f, 1.00f, 0.89f, 1.0f);
            break;
        case 6: // Pink Neon
            p.text = ImVec4(1.00f, 0.84f, 0.98f, 1.0f);
            p.text_disabled = ImVec4(0.74f, 0.54f, 0.72f, 1.0f);
            p.window_bg = ImVec4(0.12f, 0.03f, 0.12f, 0.52f);
            p.child_bg = ImVec4(0.17f, 0.04f, 0.17f, 0.68f);
            p.popup_bg = ImVec4(0.21f, 0.05f, 0.21f, 0.92f);
            p.border = ImVec4(0.99f, 0.57f, 0.93f, 0.96f);
            p.frame_bg = ImVec4(0.27f, 0.08f, 0.27f, 0.78f);
            p.frame_bg_hovered = ImVec4(0.36f, 0.11f, 0.36f, 0.86f);
            p.frame_bg_active = ImVec4(0.31f, 0.10f, 0.31f, 0.92f);
            p.button = ImVec4(0.40f, 0.12f, 0.40f, 0.88f);
            p.button_hovered = ImVec4(0.50f, 0.15f, 0.50f, 0.92f);
            p.button_active = ImVec4(0.35f, 0.10f, 0.35f, 0.95f);
            p.accent = ImVec4(1.00f, 0.69f, 0.97f, 1.0f);
            break;
        case 7: // Catppuccin Latte
            p.text = ImVec4(0.30f, 0.24f, 0.41f, 1.0f);
            p.text_disabled = ImVec4(0.50f, 0.43f, 0.60f, 1.0f);
            p.window_bg = ImVec4(0.89f, 0.86f, 0.94f, 0.68f);
            p.child_bg = ImVec4(0.93f, 0.90f, 0.97f, 0.80f);
            p.popup_bg = ImVec4(0.94f, 0.91f, 0.98f, 0.95f);
            p.border = ImVec4(0.56f, 0.44f, 0.74f, 0.96f);
            p.frame_bg = ImVec4(0.83f, 0.78f, 0.92f, 0.86f);
            p.frame_bg_hovered = ImVec4(0.87f, 0.83f, 0.95f, 0.92f);
            p.frame_bg_active = ImVec4(0.80f, 0.74f, 0.90f, 0.95f);
            p.button = ImVec4(0.76f, 0.68f, 0.88f, 0.90f);
            p.button_hovered = ImVec4(0.82f, 0.74f, 0.92f, 0.95f);
            p.button_active = ImVec4(0.70f, 0.62f, 0.84f, 0.98f);
            p.accent = ImVec4(0.55f, 0.42f, 0.73f, 1.0f);
            break;
        case 8: // Catppuccin Frappe
            p.text = ImVec4(0.92f, 0.90f, 0.98f, 1.0f);
            p.text_disabled = ImVec4(0.67f, 0.63f, 0.78f, 1.0f);
            p.window_bg = ImVec4(0.15f, 0.14f, 0.24f, 0.58f);
            p.child_bg = ImVec4(0.20f, 0.19f, 0.30f, 0.72f);
            p.popup_bg = ImVec4(0.22f, 0.21f, 0.33f, 0.94f);
            p.border = ImVec4(0.67f, 0.60f, 0.94f, 0.96f);
            p.frame_bg = ImVec4(0.28f, 0.26f, 0.40f, 0.82f);
            p.frame_bg_hovered = ImVec4(0.35f, 0.33f, 0.49f, 0.90f);
            p.frame_bg_active = ImVec4(0.30f, 0.28f, 0.44f, 0.95f);
            p.button = ImVec4(0.37f, 0.35f, 0.52f, 0.90f);
            p.button_hovered = ImVec4(0.45f, 0.42f, 0.60f, 0.95f);
            p.button_active = ImVec4(0.32f, 0.30f, 0.47f, 0.98f);
            p.accent = ImVec4(0.72f, 0.65f, 0.99f, 1.0f);
            break;
        case 9: // Catppuccin Macchiato
            p.text = ImVec4(0.90f, 0.94f, 1.00f, 1.0f);
            p.text_disabled = ImVec4(0.62f, 0.70f, 0.80f, 1.0f);
            p.window_bg = ImVec4(0.10f, 0.12f, 0.20f, 0.56f);
            p.child_bg = ImVec4(0.13f, 0.16f, 0.25f, 0.70f);
            p.popup_bg = ImVec4(0.15f, 0.18f, 0.29f, 0.94f);
            p.border = ImVec4(0.55f, 0.70f, 0.95f, 0.96f);
            p.frame_bg = ImVec4(0.19f, 0.25f, 0.39f, 0.82f);
            p.frame_bg_hovered = ImVec4(0.26f, 0.33f, 0.50f, 0.90f);
            p.frame_bg_active = ImVec4(0.22f, 0.29f, 0.44f, 0.95f);
            p.button = ImVec4(0.24f, 0.33f, 0.52f, 0.90f);
            p.button_hovered = ImVec4(0.31f, 0.42f, 0.62f, 0.95f);
            p.button_active = ImVec4(0.20f, 0.27f, 0.46f, 0.98f);
            p.accent = ImVec4(0.65f, 0.79f, 1.00f, 1.0f);
            break;
        case 10: // Catppuccin Mocha
            p.text = ImVec4(0.95f, 0.91f, 1.00f, 1.0f);
            p.text_disabled = ImVec4(0.74f, 0.66f, 0.84f, 1.0f);
            p.window_bg = ImVec4(0.09f, 0.07f, 0.15f, 0.56f);
            p.child_bg = ImVec4(0.13f, 0.10f, 0.21f, 0.70f);
            p.popup_bg = ImVec4(0.16f, 0.12f, 0.25f, 0.94f);
            p.border = ImVec4(0.79f, 0.63f, 0.97f, 0.96f);
            p.frame_bg = ImVec4(0.21f, 0.15f, 0.34f, 0.82f);
            p.frame_bg_hovered = ImVec4(0.29f, 0.21f, 0.45f, 0.90f);
            p.frame_bg_active = ImVec4(0.25f, 0.18f, 0.39f, 0.95f);
            p.button = ImVec4(0.30f, 0.22f, 0.47f, 0.90f);
            p.button_hovered = ImVec4(0.40f, 0.29f, 0.59f, 0.95f);
            p.button_active = ImVec4(0.24f, 0.17f, 0.40f, 0.98f);
            p.accent = ImVec4(0.85f, 0.73f, 1.00f, 1.0f);
            break;
        case 11: // Red Light
            p.text = ImVec4(1.00f, 0.91f, 0.91f, 1.0f);
            p.text_disabled = ImVec4(0.79f, 0.58f, 0.58f, 1.0f);
            p.window_bg = ImVec4(0.22f, 0.07f, 0.07f, 0.54f);
            p.child_bg = ImVec4(0.30f, 0.10f, 0.10f, 0.68f);
            p.popup_bg = ImVec4(0.36f, 0.12f, 0.12f, 0.93f);
            p.border = ImVec4(0.98f, 0.63f, 0.63f, 0.96f);
            p.frame_bg = ImVec4(0.42f, 0.14f, 0.14f, 0.80f);
            p.frame_bg_hovered = ImVec4(0.50f, 0.18f, 0.18f, 0.90f);
            p.frame_bg_active = ImVec4(0.56f, 0.20f, 0.20f, 0.96f);
            p.button = ImVec4(0.48f, 0.16f, 0.16f, 0.90f);
            p.button_hovered = ImVec4(0.57f, 0.20f, 0.20f, 0.95f);
            p.button_active = ImVec4(0.63f, 0.23f, 0.23f, 0.98f);
            p.accent = ImVec4(1.00f, 0.72f, 0.72f, 1.0f);
            break;
        case 12: // Red Classic
            p.text = ImVec4(1.00f, 0.82f, 0.82f, 1.0f);
            p.text_disabled = ImVec4(0.70f, 0.41f, 0.41f, 1.0f);
            p.window_bg = ImVec4(0.16f, 0.03f, 0.03f, 0.58f);
            p.child_bg = ImVec4(0.23f, 0.05f, 0.05f, 0.72f);
            p.popup_bg = ImVec4(0.29f, 0.07f, 0.07f, 0.95f);
            p.border = ImVec4(0.92f, 0.35f, 0.35f, 0.98f);
            p.frame_bg = ImVec4(0.35f, 0.07f, 0.07f, 0.82f);
            p.frame_bg_hovered = ImVec4(0.45f, 0.10f, 0.10f, 0.90f);
            p.frame_bg_active = ImVec4(0.54f, 0.12f, 0.12f, 0.96f);
            p.button = ImVec4(0.41f, 0.08f, 0.08f, 0.92f);
            p.button_hovered = ImVec4(0.52f, 0.11f, 0.11f, 0.96f);
            p.button_active = ImVec4(0.60f, 0.14f, 0.14f, 0.99f);
            p.accent = ImVec4(0.98f, 0.44f, 0.44f, 1.0f);
            break;
        default: // Green Terminal
            p.text = ImVec4(0.58f, 0.95f, 0.58f, 1.0f);
            p.text_disabled = ImVec4(0.32f, 0.50f, 0.32f, 1.0f);
            p.window_bg = ImVec4(0.02f, 0.05f, 0.02f, 0.58f);
            p.child_bg = ImVec4(0.00f, 0.04f, 0.00f, 0.72f);
            p.popup_bg = ImVec4(0.01f, 0.07f, 0.01f, 0.96f);
            p.border = ImVec4(0.24f, 0.70f, 0.24f, 1.0f);
            p.frame_bg = ImVec4(0.02f, 0.09f, 0.02f, 0.82f);
            p.frame_bg_hovered = ImVec4(0.07f, 0.18f, 0.07f, 0.90f);
            p.frame_bg_active = ImVec4(0.10f, 0.24f, 0.10f, 0.95f);
            p.button = ImVec4(0.03f, 0.11f, 0.03f, 1.0f);
            p.button_hovered = ImVec4(0.09f, 0.20f, 0.09f, 1.0f);
            p.button_active = ImVec4(0.12f, 0.29f, 0.12f, 1.0f);
            p.accent = ImVec4(0.35f, 0.84f, 0.35f, 1.0f);
            break;
    }

    if (g_overlay_colors.enabled) {
        p.text = Clamp01(g_overlay_colors.text);
        p.text_disabled = Mix(p.text, Clamp01(g_overlay_colors.window_bg), 0.45f);
        p.window_bg = Clamp01(g_overlay_colors.window_bg);
        p.child_bg = Mix(p.window_bg, Clamp01(g_overlay_colors.button), 0.12f);
        p.popup_bg = Mix(p.window_bg, Clamp01(g_overlay_colors.button), 0.18f);
        p.border = Clamp01(g_overlay_colors.border);
        p.frame_bg = Mix(p.window_bg, Clamp01(g_overlay_colors.button), 0.16f);
        p.frame_bg_hovered = Clamp01(g_overlay_colors.button_hovered);
        p.frame_bg_active = Clamp01(g_overlay_colors.button_active);
        p.button = Clamp01(g_overlay_colors.button);
        p.button_hovered = Clamp01(g_overlay_colors.button_hovered);
        p.button_active = Clamp01(g_overlay_colors.button_active);
        p.accent = Clamp01(g_overlay_colors.accent);
    }

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = p.text;
    colors[ImGuiCol_TextDisabled] = p.text_disabled;
    colors[ImGuiCol_WindowBg] = p.window_bg;
    colors[ImGuiCol_ChildBg] = p.child_bg;
    colors[ImGuiCol_PopupBg] = p.popup_bg;
    colors[ImGuiCol_Border] = p.border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = p.frame_bg;
    colors[ImGuiCol_FrameBgHovered] = p.frame_bg_hovered;
    colors[ImGuiCol_FrameBgActive] = p.frame_bg_active;
    colors[ImGuiCol_TitleBg] = p.button;
    colors[ImGuiCol_TitleBgActive] = p.button_hovered;
    colors[ImGuiCol_TitleBgCollapsed] = p.button;
    colors[ImGuiCol_MenuBarBg] = p.frame_bg;
    colors[ImGuiCol_ScrollbarBg] = p.child_bg;
    colors[ImGuiCol_ScrollbarGrab] = p.frame_bg_hovered;
    colors[ImGuiCol_ScrollbarGrabHovered] = p.button_hovered;
    colors[ImGuiCol_ScrollbarGrabActive] = p.button_active;
    colors[ImGuiCol_CheckMark] = p.text;
    colors[ImGuiCol_SliderGrab] = p.border;
    colors[ImGuiCol_SliderGrabActive] = p.accent;
    colors[ImGuiCol_Button] = p.button;
    colors[ImGuiCol_ButtonHovered] = p.button_hovered;
    colors[ImGuiCol_ButtonActive] = p.button_active;
    colors[ImGuiCol_Header] = p.button;
    colors[ImGuiCol_HeaderHovered] = p.button_hovered;
    colors[ImGuiCol_HeaderActive] = p.button_active;
    colors[ImGuiCol_Separator] = p.border;
    colors[ImGuiCol_SeparatorHovered] = p.accent;
    colors[ImGuiCol_SeparatorActive] = p.accent;
    colors[ImGuiCol_ResizeGrip] = p.border;
    colors[ImGuiCol_ResizeGripHovered] = p.accent;
    colors[ImGuiCol_ResizeGripActive] = p.accent;
    colors[ImGuiCol_Tab] = p.button;
    colors[ImGuiCol_TabHovered] = p.button_hovered;
    colors[ImGuiCol_TabActive] = p.button_active;
    colors[ImGuiCol_TabUnfocused] = p.frame_bg;
    colors[ImGuiCol_TabUnfocusedActive] = p.frame_bg_hovered;
    colors[ImGuiCol_PlotLines] = p.text;
    colors[ImGuiCol_PlotLinesHovered] = p.accent;
    colors[ImGuiCol_PlotHistogram] = p.text;
    colors[ImGuiCol_PlotHistogramHovered] = p.accent;
    colors[ImGuiCol_TableHeaderBg] = p.button;
    colors[ImGuiCol_TableBorderStrong] = p.border;
    colors[ImGuiCol_TableBorderLight] = p.frame_bg_hovered;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = p.frame_bg;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.38f);
    colors[ImGuiCol_DragDropTarget] = p.accent;
    colors[ImGuiCol_NavHighlight] = p.accent;
    colors[ImGuiCol_NavWindowingHighlight] = p.accent;
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.80f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.80f);

    if (g_overlay_colors.enabled) {
        colors[ImGuiCol_Tab] = Clamp01(g_overlay_colors.tab);
        colors[ImGuiCol_TabHovered] = Mix(Clamp01(g_overlay_colors.tab), Clamp01(g_overlay_colors.accent), 0.25f);
        colors[ImGuiCol_TabActive] = Clamp01(g_overlay_colors.tab_active);
        colors[ImGuiCol_TabUnfocused] = Mix(Clamp01(g_overlay_colors.tab), Clamp01(g_overlay_colors.window_bg), 0.30f);
        colors[ImGuiCol_TabUnfocusedActive] = Mix(Clamp01(g_overlay_colors.tab_active), Clamp01(g_overlay_colors.window_bg), 0.24f);
    }
}

void Steam_Overlay::BuildNotifications(int width, int height)
{
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    int i = 0;

    int font_size = ImGui::GetFontSize();

    std::queue<Friend> friend_actions_temp;

    {
        std::lock_guard<std::recursive_mutex> lock(notifications_mutex);

        for (auto it = notifications.begin(); it != notifications.end(); ++it, ++i)
        {
            auto elapsed_notif = now - it->start_time;

            if ( elapsed_notif < Notification::fade_in)
            {
                float alpha = Notification::max_alpha * (elapsed_notif.count() / static_cast<float>(Notification::fade_in.count()));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, alpha));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Notification::r, Notification::g, Notification::b, alpha));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 255, 255, alpha*2));
            }
            else if ( elapsed_notif > Notification::fade_out_start)
            {
                float alpha = Notification::max_alpha * ((Notification::show_time - elapsed_notif).count() / static_cast<float>(Notification::fade_out.count()));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, alpha));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Notification::r, Notification::g, Notification::b, alpha));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 255, 255, alpha*2));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, Notification::max_alpha));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Notification::r, Notification::g, Notification::b, Notification::max_alpha));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 255, 255, Notification::max_alpha*2));
            }
            
            ImGui::SetNextWindowPos(ImVec2((float)width - width * Notification::width, Notification::height * font_size * i ));
            ImGui::SetNextWindowSize(ImVec2( width * Notification::width, Notification::height * font_size ));
            ImGui::Begin(std::to_string(it->id).c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | 
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDecoration);

            switch (it->type)
            {
                case notification_type_achievement:
                    ImGui::TextWrapped("%s", it->message.c_str());
                    break;
                case notification_type_invite:
                    {
                        ImGui::TextWrapped("%s", it->message.c_str());
                        if (RetroButton("##notif_join", "Join"))
                        {
                            it->frd->second.window_state |= window_state_join;
                            friend_actions_temp.push(it->frd->first);
                            it->start_time = std::chrono::seconds(0);
                        }
                    }
                    break;
                case notification_type_message:
                    ImGui::TextWrapped("%s", it->message.c_str()); break;
            }

            ImGui::End();

            ImGui::PopStyleColor(3);
        }
        notifications.erase(std::remove_if(notifications.begin(), notifications.end(), [&now](Notification &item) {
            return (now - item.start_time) > Notification::show_time;
        }), notifications.end());

        have_notifications = !notifications.empty();
    }

    if (!friend_actions_temp.empty()) {
        std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
        while (!friend_actions_temp.empty()) {
            QueueFriendAction(friend_actions_temp.front());
            friend_actions_temp.pop();
        }
    }
}

void Steam_Overlay::CreateFonts()
{
    if (fonts_atlas) return;

    ImFontAtlas *Fonts = new ImFontAtlas();

    ImFontConfig fontcfg;

    float font_size = 16.0;
    fontcfg.OversampleH = fontcfg.OversampleV = 1;
    fontcfg.PixelSnapH = true;
    fontcfg.SizePixels = font_size;

    ImFontGlyphRangesBuilder font_builder;
    for (auto & x : achievements) {
        font_builder.AddText(x.title.c_str());
        font_builder.AddText(x.description.c_str());
    }

    font_builder.AddRanges(Fonts->GetGlyphRangesDefault());

    ImVector<ImWchar> ranges;
    font_builder.BuildRanges(&ranges);

    bool need_extra_fonts = false;
    for (auto &x : ranges) {
        if (x > 0xFF) {
            need_extra_fonts = true;
            break;
        }
    }

    fontcfg.GlyphRanges = ranges.Data;
    ImFont *font = NULL;

#if defined(__WINDOWS__)
    font = Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\lucon.ttf", font_size, &fontcfg);
    if (!font) {
        font = Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", font_size, &fontcfg);
    }
    if (!font) {
        font = Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\micross.ttf", font_size, &fontcfg);
    }
#endif

    if (!font) {
        font = Fonts->AddFontDefault(&fontcfg);
    }

    font_notif = font_default = font;

    if (need_extra_fonts) {
        PRINT_DEBUG("loading extra fonts\n");
        fontcfg.MergeMode = true;
#if defined(__WINDOWS__)
        Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\simsun.ttc", font_size, &fontcfg);
        Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", font_size, &fontcfg);
#endif
    }

    Fonts->Build();
    fonts_atlas = (void *)Fonts;

    // ImGuiStyle& style = ImGui::GetStyle();
    // style.WindowRounding = 0.0; // Disable round window
    reset_LastError();
}

// Try to make this function as short as possible or it might affect game's fps.
void Steam_Overlay::OverlayProc()
{
    if (!Ready())
        return;

    ImGuiIO& io = ImGui::GetIO();
    LoadCustomOverlayColors(get_steam_client()->local_storage);
    if (current_ui_scale < 0 || current_ui_scale >= static_cast<int>(sizeof(overlay_ui_scales) / sizeof(overlay_ui_scales[0]))) {
        current_ui_scale = 1;
    }
    io.FontGlobalScale = GetOverlayUiScale(current_ui_scale);
    ApplyRetroStyle();

    if (!overlay_frontend) {
        SetupOverlayFrontend();
    }
    if (overlay_frontend) {
        overlay_frontend->Render(*this, io);
    } else {
        RenderOverlayWindows(io);
    }

    if (have_notifications) {
        ImGui::PushFont(font_notif);
        BuildNotifications(io.DisplaySize.x, io.DisplaySize.y);
        ImGui::PopFont();
    }
}

void Steam_Overlay::RenderOverlayWindows(ImGuiIO &io)
{
    if (show_overlay)
    {
        io.MouseDrawCursor = true;
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::SetNextWindowPos({ 0,0 });
        ImGui::SetNextWindowSize({ static_cast<float>(io.DisplaySize.x),
                                   static_cast<float>(io.DisplaySize.y) });
        ImGui::SetNextWindowBgAlpha(0.18f);

        ImGui::PushFont(font_default);

        bool show = true;
        if (ImGui::Begin("SteamOverlay", &show, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            static int selected_action = 0;
            const ImGuiStyle &style = ImGui::GetStyle();
            const float header_height = ImGui::GetFrameHeightWithSpacing() * 1.35f;
            const float panel_padding = style.FramePadding.x;

            ImVec2 header_pos = ImGui::GetCursorScreenPos();
            ImVec2 header_size = ImVec2(ImGui::GetContentRegionAvail().x, header_height);
            DrawRetroPanel(header_pos, ImVec2(header_pos.x + header_size.x, header_pos.y + header_size.y), true);
            ImGui::SetCursorScreenPos(ImVec2(header_pos.x + panel_padding, header_pos.y + panel_padding));
            ImGui::Text("USER: %s (%llu) APPID: %u",
                settings->get_local_name(),
                settings->get_local_steam_id().ConvertToUint64(),
                settings->get_local_game_id().AppID());
            ImGui::SetCursorScreenPos(ImVec2(header_pos.x, header_pos.y + header_size.y + style.ItemSpacing.y));

            DrawRetroSeparator();
            if (RetroTab("##retro_tab_ach", "ACHIEVEMENTS", selected_action == 0, ImVec2(ImGui::GetFontSize() * 8.8f, 0.0f))) {
                selected_action = 0;
                show_achievements = true;
                show_settings = false;
            }
            ImGui::SameLine();
            if (RetroTab("##retro_tab_settings", "SETTINGS", selected_action == 1, ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) {
                selected_action = 1;
                show_settings = true;
                show_achievements = false;
            }
            ImGui::SameLine();
            bool force_files_active = disable_forced;
            RetroCheckbox("##retro_force_files", "FORCED FILES", &force_files_active, true);

            DrawRetroSeparator();
            size_t friend_count = 0;
            {
                std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
                friend_count = friends.size();
            }
            RetroTab("##retro_friends_hdr", "FRIENDS", true, ImVec2(ImGui::GetFontSize() * 5.4f, 0.0f));
            ImGui::SameLine();
            ImGui::Text("%u ONLINE", static_cast<unsigned>(friend_count));

            ImVec2 friends_panel_pos = ImGui::GetCursorScreenPos();
            float friends_height = ImGui::GetContentRegionAvail().y;
            const float friends_min = ImGui::GetFrameHeightWithSpacing() * 6.0f;
            const float friends_max = ImGui::GetFrameHeightWithSpacing() * 14.0f;
            friends_height = std::max(friends_min, std::min(friends_height, friends_max));
            ImVec2 friends_panel_size(ImGui::GetContentRegionAvail().x, friends_height);
            DrawRetroPanel(friends_panel_pos, ImVec2(friends_panel_pos.x + friends_panel_size.x, friends_panel_pos.y + friends_panel_size.y), true);
            ImGui::SetCursorScreenPos(ImVec2(friends_panel_pos.x + panel_padding, friends_panel_pos.y + panel_padding));
            ImGui::BeginChild("##retro_friends_list", ImVec2(friends_panel_size.x - panel_padding * 2.0f, friends_panel_size.y - panel_padding * 2.0f), false);
            {
                std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
                if (friends.empty()) {
                    ImGui::Text("NO FRIENDS CONNECTED");
                } else {
                    std::for_each(friends.begin(), friends.end(), [this](std::pair<Friend const, friend_window_state> &i)
                    {
                        ImGui::PushID(i.second.id - base_friend_window_id + base_friend_item_id);
                        float row_btn_h = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f);
                        ImGui::Selectable(i.second.window_title.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, row_btn_h));
                        BuildContextMenu(i.first, i.second);
                        if (ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(0))
                            i.second.window_state |= window_state_show;
                        ImGui::SameLine();
                        if (RetroButton("##friend_chat", "Chat")) {
                            i.second.window_state |= window_state_show;
                        }
                        ImGui::SameLine();
                        if (RetroButton("##friend_invite", "Invite")) {
                            if (settings->get_local_game_id().AppID() == i.first.appid()) {
                                i.second.window_state |= window_state_invite;
                                QueueFriendAction(i.first);
                            } else {
                                AddMessageNotification("Invite unavailable: friend is in a different game.");
                            }
                        }
                        ImGui::SameLine();
                        if (RetroButton("##friend_join", "Join")) {
                            if (i.second.joinable) {
                                i.second.window_state |= window_state_join;
                                QueueFriendAction(i.first);
                            } else {
                                AddMessageNotification("Join unavailable: no active invite/lobby.");
                            }
                        }
                        ImGui::PopID();
                        BuildFriendWindow(i.first, i.second);
                    });
                }
            }
            ImGui::EndChild();

            if (show_achievements && achievements.size()) {
                ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 32, ImGui::GetFontSize() * 32), ImVec2(8192, 8192));
                bool show_achievement_window = show_achievements;
                if (ImGui::Begin("Achievement Window", &show_achievement_window)) {
                    ImGui::Text("List of achievements");
                    ImGui::BeginChild("Achievements");
                    for (auto &x : achievements) {
                        bool achieved = x.achieved;
                        bool hidden = x.hidden && !achieved;
                        ImGui::Separator();
                        ImGui::Text("%s", x.title.c_str());
                        if (hidden) {
                            ImGui::Text("hidden achievement");
                        } else {
                            ImGui::TextWrapped("%s", x.description.c_str());
                        }
                        if (achieved) {
                            char buffer[80] = {};
                            time_t unlock_time = (time_t)x.unlock_time;
                            std::strftime(buffer, 80, "%Y-%m-%d at %H:%M:%S", std::localtime(&unlock_time));
                            ImGui::TextColored(ImVec4(0, 255, 0, 255), "achieved on %s", buffer);
                        } else {
                            ImGui::TextColored(ImVec4(255, 0, 0, 255), "not achieved");
                        }
                        ImGui::Separator();
                    }
                    ImGui::EndChild();
                }
                ImGui::End();
                show_achievements = show_achievement_window;
            }

            if (show_settings) {
                if (ImGui::Begin("Global Settings Window", &show_settings)) {
                    ImGui::Text("These are global emulator settings and will apply to all games.");
                    ImGui::Separator();
                    ImGui::Text("Username:");
                    ImGui::SameLine();
                    ImGui::InputText("##username", username_text, sizeof(username_text), disable_forced ? ImGuiInputTextFlags_ReadOnly : 0);
                    ImGui::Separator();
                    ImGui::Text("Language:");
                    ImGui::ListBox("##language", &current_language, valid_languages, sizeof(valid_languages) / sizeof(char *), 7);
                    ImGui::Text("Selected Language: %s", valid_languages[current_language]);
                    ImGui::Separator();
                    ImGui::Text("Overlay Theme:");
                    if (ImGui::Combo("##overlay_theme", &current_theme, retro_themes, sizeof(retro_themes) / sizeof(retro_themes[0]))) {
                        char theme_text[8] = {};
                        std::snprintf(theme_text, sizeof(theme_text), "%d", current_theme);
                        get_steam_client()->local_storage->store_data_settings("overlay_theme.txt", theme_text, strlen(theme_text));
                    }
                    ImGui::Text("Current Theme: %s", retro_themes[current_theme]);
                    bool custom_changed = false;
                    bool custom_enabled = g_overlay_colors.enabled;
                    if (ImGui::Checkbox("Use Custom Theme Colors", &custom_enabled)) {
                        g_overlay_colors.enabled = custom_enabled;
                        custom_changed = true;
                    }
                    if (g_overlay_colors.enabled) {
                        ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float;
                        custom_changed |= ImGui::ColorEdit4("Text Color", &g_overlay_colors.text.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Window Color", &g_overlay_colors.window_bg.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Border Color", &g_overlay_colors.border.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Tab Color", &g_overlay_colors.tab.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Tab Active Color", &g_overlay_colors.tab_active.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Button Color", &g_overlay_colors.button.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Button Hover Color", &g_overlay_colors.button_hovered.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Button Active Color", &g_overlay_colors.button_active.x, color_flags);
                        custom_changed |= ImGui::ColorEdit4("Accent Color", &g_overlay_colors.accent.x, color_flags);
                    }
                    if (custom_changed) {
                        SaveCustomOverlayColors(get_steam_client()->local_storage);
                    }
                    ImGui::Separator();
                    ImGui::Text("UI Scale:");
                    if (ImGui::Combo("##overlay_ui_scale", &current_ui_scale, overlay_ui_scales, sizeof(overlay_ui_scales) / sizeof(overlay_ui_scales[0]))) {
                        char ui_scale_text[8] = {};
                        std::snprintf(ui_scale_text, sizeof(ui_scale_text), "%d", current_ui_scale);
                        get_steam_client()->local_storage->store_data_settings("overlay_ui_scale.txt", ui_scale_text, strlen(ui_scale_text));
                    }
                    ImGui::Text("Current Scale: %s", overlay_ui_scales[current_ui_scale]);
                    ImGui::Separator();
                    ImGui::Text("Overlay Frontend:");
                    if (ImGui::Combo("##overlay_frontend", &current_frontend, overlay_frontends, sizeof(overlay_frontends) / sizeof(overlay_frontends[0]))) {
                        char frontend_text[8] = {};
                        std::snprintf(frontend_text, sizeof(frontend_text), "%d", current_frontend);
                        get_steam_client()->local_storage->store_data_settings("overlay_frontend.txt", frontend_text, strlen(frontend_text));
                        SetupOverlayFrontend();
                    }
                const char *frontend_name = overlay_frontend ? overlay_frontend->Name() : "None";
                ImGui::Text("Active Frontend: %s", frontend_name);
                ImGui::Separator();
                if (!disable_forced) {
                    ImGui::Text("Language/name are saved; theme and scale apply immediately.");
                    if (RetroButton("##settings_save", "Save")) {
                        save_settings = true;
                        show_settings = false;
                    }
                    } else {
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                        ImGui::TextWrapped("Some steam_settings/force_*.txt files have been detected. Please delete them if you want this menu to work.");
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                    }
                }
                ImGui::End();
            }

            std::string url = show_url;
            if (!url.empty()) {
                bool show_url_window = true;
                if (ImGui::Begin(URL_WINDOW_NAME, &show_url_window)) {
                    ImGui::Text("The game tried to get the steam overlay to open this url:");
                    ImGui::Spacing();
                    ImGui::PushItemWidth(ImGui::CalcTextSize(url.c_str()).x + 20);
                    ImGui::InputText("##url_copy", (char *)url.data(), url.size(), ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopItemWidth();
                    ImGui::Spacing();
                    if (RetroButton("##url_close", "Close") || !show_url_window)
                        show_url = "";
                }
                ImGui::End();
            }

            bool show_warning = local_save || warning_forced || appid == 0;
            if (show_warning) {
                ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 32, ImGui::GetFontSize() * 32), ImVec2(8192, 8192));
                ImGui::SetNextWindowFocus();
                if (ImGui::Begin("WARNING", &show_warning)) {
                    if (appid == 0) {
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                        ImGui::TextWrapped("AppID is 0, please create a steam_appid.txt with the right appid and restart the game.");
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                    }
                    if (local_save) {
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                        ImGui::TextWrapped("local_save.txt detected, the emu is saving locally to the game folder. Please delete it if you don't want this.");
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                    }
                    if (warning_forced) {
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                        ImGui::TextWrapped("Some steam_settings/force_*.txt files have been detected. You will not be able to save some settings.");
                        ImGui::TextColored(ImVec4(255, 0, 0, 255), "WARNING WARNING WARNING");
                    }
                }
                ImGui::End();
                if (!show_warning) {
                    local_save = warning_forced = false;
                }
            }
        }
        ImGui::End();
        ImGui::PopFont();
        if (!show)
            ShowOverlay(false);
    } else {
        io.MouseDrawCursor = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
}

void Steam_Overlay::RenderCustomAeroOverlayWindows(ImGuiIO &io)
{
    if (!show_overlay) {
        io.MouseDrawCursor = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        return;
    }

    io.MouseDrawCursor = true;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({static_cast<float>(io.DisplaySize.x), static_cast<float>(io.DisplaySize.y)});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushFont(font_default);

    bool show = true;
    if (ImGui::Begin("SteamOverlayCustomRoot", &show, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        AeroThemeColors theme = GetAeroThemeColors(current_theme);
        ApplyCustomAeroOverrides(theme);
        static int selected_action = 0;
        static bool aero_minimized = false;
        static ImVec2 aero_offset = ImVec2(18.0f, 18.0f);
        const ImGuiStyle &style = ImGui::GetStyle();
        custom_ui::InputState custom_input = BuildCustomUiInput(io);
        custom_ui_ctx.BeginFrame(custom_input);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        ImVec2 canvas_max = ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);

        draw->AddRectFilledMultiColor(canvas_pos, canvas_max,
                                      theme.overlay_top, theme.overlay_top,
                                      theme.overlay_bottom, theme.overlay_bottom);

        const float panel_w = std::min(std::max(520.0f, canvas_size.x * 0.64f), canvas_size.x - 14.0f);
        const float panel_h = std::min(std::max(300.0f, canvas_size.y * 0.66f), canvas_size.y - 14.0f);
        aero_offset.x = std::max(6.0f, std::min(aero_offset.x, canvas_size.x - panel_w - 6.0f));
        aero_offset.y = std::max(6.0f, std::min(aero_offset.y, canvas_size.y - panel_h - 6.0f));
        ImVec2 panel_min = ImVec2(canvas_pos.x + aero_offset.x, canvas_pos.y + aero_offset.y);
        ImVec2 panel_max = ImVec2(panel_min.x + panel_w, panel_min.y + panel_h);
        draw->AddRectFilledMultiColor(panel_min, panel_max,
                                      theme.panel_top_left, theme.panel_top_right,
                                      theme.panel_bottom_left, theme.panel_bottom_right);
        draw->AddRect(panel_min, panel_max, theme.panel_border, 6.0f, 0, 1.0f);

        const float title_h = ImGui::GetFrameHeightWithSpacing() * 1.15f;
        ImVec2 title_min = panel_min;
        ImVec2 title_max = ImVec2(panel_max.x, panel_min.y + title_h);
        draw->AddRectFilledMultiColor(title_min, title_max,
                                      theme.title_top, theme.title_top,
                                      theme.title_bottom, theme.title_bottom);
        draw->AddRect(title_min, title_max, theme.title_border, 6.0f, 0, 1.0f);

        ImGui::SetCursorScreenPos(ImVec2(title_min.x + 8.0f, title_min.y + 2.0f));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.text), "Goldberg Overlay - Custom Aero");

        const float ctrl_w = 22.0f;
        const float ctrl_h = ImGui::GetFrameHeight();
        ImVec2 min_btn_min = ImVec2(title_max.x - ctrl_w * 2.1f - 6.0f, title_min.y + 2.0f);
        if (CustomUiAeroButton(custom_ui_ctx, draw, theme, "aero_min_custom", "_", min_btn_min, ImVec2(ctrl_w, ctrl_h))) {
            aero_minimized = !aero_minimized;
        }
        ImVec2 close_btn_min = ImVec2(min_btn_min.x + ctrl_w + 2.0f, min_btn_min.y);
        if (CustomUiAeroButton(custom_ui_ctx, draw, theme, "aero_close_custom", "X", close_btn_min, ImVec2(ctrl_w, ctrl_h))) {
            ShowOverlay(false);
        }

        custom_ui::Vec2 drag_delta = {};
        custom_ui::Rect drag_rect = {
            title_min.x + 4.0f,
            title_min.y + 2.0f,
            (title_max.x - title_min.x) - ctrl_w * 2.4f - 12.0f,
            title_h - 4.0f
        };
        if (custom_ui_ctx.DragRegion(custom_ui::HashId("aero_drag_title_custom"), drag_rect, &drag_delta)) {
            aero_offset.x += drag_delta.x;
            aero_offset.y += drag_delta.y;
        }

        if (!aero_minimized) {
            const float content_pad = 8.0f;
            const float top_h = ImGui::GetFrameHeightWithSpacing() * 1.45f;
            ImVec2 top_min = ImVec2(panel_min.x + content_pad, title_max.y + content_pad);
            ImVec2 top_max = ImVec2(panel_max.x - content_pad, top_min.y + top_h);
            draw->AddRectFilledMultiColor(top_min, top_max,
                                          theme.tab_top_active, theme.tab_top_active,
                                          theme.tab_bottom_active, theme.tab_bottom_active);
            draw->AddRect(top_min, top_max, theme.tab_border, 5.0f, 0, 1.0f);

            ImGui::SetCursorScreenPos(ImVec2(top_min.x + 8.0f, top_min.y + 4.0f));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.text), "USER: %s (%llu) APPID: %u",
                settings->get_local_name(),
                settings->get_local_steam_id().ConvertToUint64(),
                settings->get_local_game_id().AppID());

            ImVec2 tabs_min = ImVec2(top_min.x, top_max.y + 8.0f);
            ImVec2 ach_size = ImVec2(ImGui::GetFontSize() * 7.4f, ImGui::GetFrameHeight());
            if (CustomUiAeroButton(custom_ui_ctx, draw, theme, "aero_tab_ach_custom", "ACHIEVEMENTS", tabs_min, ach_size, selected_action == 0)) {
                selected_action = 0;
                show_achievements = true;
                show_settings = false;
            }
            ImVec2 set_min = ImVec2(tabs_min.x + ach_size.x + 4.0f, tabs_min.y);
            ImVec2 set_size = ImVec2(ImGui::GetFontSize() * 5.1f, ImGui::GetFrameHeight());
            if (CustomUiAeroButton(custom_ui_ctx, draw, theme, "aero_tab_set_custom", "SETTINGS", set_min, set_size, selected_action == 1)) {
                selected_action = 1;
                show_settings = true;
                show_achievements = false;
            }
            ImGui::SetCursorScreenPos(ImVec2(set_min.x + set_size.x + 8.0f, tabs_min.y));
            bool force_files_active = disable_forced;
            RetroCheckbox("##aero_forced_files", "FORCED FILES", &force_files_active, true);

            size_t friend_count = 0;
            {
                std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
                friend_count = friends.size();
            }

            ImVec2 friends_hdr_min = ImVec2(top_min.x, top_max.y + ImGui::GetFrameHeightWithSpacing() + 14.0f);
            ImVec2 friends_hdr_size = ImVec2(ImGui::GetFontSize() * 3.8f, ImGui::GetFrameHeight());
            CustomUiAeroButton(custom_ui_ctx, draw, theme, "aero_friends_hdr_custom", "FRIENDS", friends_hdr_min, friends_hdr_size, true);
            ImGui::SetCursorScreenPos(ImVec2(friends_hdr_min.x + friends_hdr_size.x + 8.0f, friends_hdr_min.y + 1.0f));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.text_soft), "%u ONLINE", static_cast<unsigned>(friend_count));

            ImVec2 friends_panel_pos = ImVec2(top_min.x, friends_hdr_min.y + friends_hdr_size.y + 6.0f);
            float friends_height = panel_max.y - friends_panel_pos.y - content_pad;
            const float friends_min = ImGui::GetFrameHeightWithSpacing() * 6.5f;
            friends_height = std::max(friends_min, friends_height);
            ImVec2 friends_panel_max = ImVec2(panel_max.x - content_pad, friends_panel_pos.y + friends_height);
            draw->AddRectFilledMultiColor(friends_panel_pos, friends_panel_max,
                                          theme.row_top, theme.row_top,
                                          theme.row_bottom, theme.row_bottom);
            draw->AddRect(friends_panel_pos, friends_panel_max, theme.row_border, 5.0f, 0, 1.0f);

            ImGui::SetCursorScreenPos(ImVec2(friends_panel_pos.x + style.FramePadding.x * 1.2f, friends_panel_pos.y + style.FramePadding.y));
            ImGui::BeginChild("##aero_friends_list", ImVec2((friends_panel_max.x - friends_panel_pos.x) - style.FramePadding.x * 2.2f,
                                                             (friends_panel_max.y - friends_panel_pos.y) - style.FramePadding.y * 2.0f), false);
            {
                std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
                if (friends.empty()) {
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.text_soft), "NO FRIENDS CONNECTED");
                } else {
                    std::for_each(friends.begin(), friends.end(), [this, theme, custom_input](std::pair<Friend const, friend_window_state> &i)
                    {
                        ImGui::PushID(i.second.id - base_friend_window_id + base_friend_item_id);
                        const float row_h = std::max(ImGui::GetTextLineHeight() + 5.0f, ImGui::GetFrameHeight() + 2.0f);
                        ImVec2 row_min = PixelAlign(ImGui::GetCursorScreenPos());
                        ImVec2 row_size(ImGui::GetContentRegionAvail().x, row_h);
                        custom_ui::Rect row_rect = { row_min.x, row_min.y, row_size.x, row_size.y };
                        std::string row_id = "aero_row_" + std::to_string(i.second.id);
                        bool hovered = false;
                        bool held = false;
                        bool clicked = custom_ui_ctx.Button(custom_ui::HashId(row_id.c_str()), row_rect, &hovered, &held);
                        bool dclicked = hovered && custom_input.left_double_clicked;

                        ImDrawList *row_draw = ImGui::GetWindowDrawList();
                        ImVec2 row_max = PixelAlign(ImVec2(row_min.x + row_size.x, row_min.y + row_size.y));
                        ImU32 r0 = hovered ? theme.row_top_hover : theme.row_top;
                        ImU32 r1 = hovered ? theme.row_bottom_hover : theme.row_bottom;
                        row_draw->AddRectFilledMultiColor(row_min, row_max, r0, r0, r1, r1);
                        row_draw->AddRect(row_min, row_max, hovered ? theme.row_border_hover : theme.row_border, 3.0f, 0, 1.0f);
                        const float btn_h = row_h - 2.0f;
                        const float chat_w = ImGui::GetFontSize() * 4.1f;
                        const float invite_w = ImGui::GetFontSize() * 5.3f;
                        const float join_w = ImGui::GetFontSize() * 4.1f;
                        const float btn_gap = 3.0f;
                        const float right_pad = 3.0f;
                        ImVec2 join_min = ImVec2(row_max.x - right_pad - join_w, row_min.y + 1.0f);
                        ImVec2 invite_min = ImVec2(join_min.x - btn_gap - invite_w, row_min.y + 1.0f);
                        ImVec2 chat_min = ImVec2(invite_min.x - btn_gap - chat_w, row_min.y + 1.0f);

                        float text_right = std::max(row_min.x + 6.0f, chat_min.x - 6.0f);
                        row_draw->PushClipRect(ImVec2(row_min.x + 4.0f, row_min.y + 1.0f), ImVec2(text_right, row_max.y - 1.0f), true);
                        row_draw->AddText(ImVec2(row_min.x + 6.0f, row_min.y + 2.0f), theme.text, i.second.window_title.c_str());
                        row_draw->PopClipRect();

                        std::string chat_id = "aero_row_chat_" + std::to_string(i.second.id);
                        if (CustomUiAeroButton(custom_ui_ctx, row_draw, theme, chat_id.c_str(), "Chat", chat_min, ImVec2(chat_w, btn_h))) {
                            i.second.window_state |= window_state_show;
                        }

                        std::string invite_id = "aero_row_invite_" + std::to_string(i.second.id);
                        if (CustomUiAeroButton(custom_ui_ctx, row_draw, theme, invite_id.c_str(), "Invite", invite_min, ImVec2(invite_w, btn_h))) {
                            if (settings->get_local_game_id().AppID() == i.first.appid()) {
                                i.second.window_state |= window_state_invite;
                                QueueFriendAction(i.first);
                            } else {
                                AddMessageNotification("Invite unavailable: friend is in a different game.");
                            }
                        }

                        std::string join_id = "aero_row_join_" + std::to_string(i.second.id);
                        bool join_active = i.second.joinable;
                        if (CustomUiAeroButton(custom_ui_ctx, row_draw, theme, join_id.c_str(), "Join", join_min, ImVec2(join_w, btn_h), join_active)) {
                            if (i.second.joinable) {
                                i.second.window_state |= window_state_join;
                                QueueFriendAction(i.first);
                            } else {
                                AddMessageNotification("Join unavailable: no active invite/lobby.");
                            }
                        }

                        if (hovered && custom_input.right_pressed) {
                            custom_context_open = true;
                            custom_context_friend_id = i.first.id();
                            custom_context_x = ImGui::GetIO().MousePos.x;
                            custom_context_y = ImGui::GetIO().MousePos.y;
                        }
                        if (clicked || dclicked)
                            i.second.window_state |= window_state_show;
                        ImGui::Dummy(row_size);
                        ImGui::PopID();
                        BuildFriendWindow(i.first, i.second);
                    });
                }
            }
            ImGui::EndChild();

            if (custom_context_open) {
                Friend frd_key;
                frd_key.set_id(custom_context_friend_id);
                auto f = friends.find(frd_key);
                if (f == friends.end()) {
                    custom_context_open = false;
                } else {
                    ImGui::SetNextWindowPos(ImVec2(custom_context_x, custom_context_y), ImGuiCond_Always);
                    ImGui::SetNextWindowBgAlpha(0.0f);
                    bool menu_open = true;
                    if (ImGui::Begin("##aero_friend_context_menu", &menu_open, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
                    {
                        ImDrawList *menu_draw = ImGui::GetWindowDrawList();
                        ImVec2 m0 = ImGui::GetWindowPos();
                        ImVec2 m1 = ImVec2(m0.x + ImGui::GetWindowSize().x, m0.y + ImGui::GetWindowSize().y);
                        menu_draw->AddRectFilledMultiColor(m0, m1,
                            theme.tab_top_active, theme.tab_top_active,
                            theme.tab_bottom_active, theme.tab_bottom_active);
                        menu_draw->AddRect(m0, m1, theme.tab_border, 4.0f, 0, 1.0f);

                        bool close_menu = false;
                        if (AeroButton("##aero_ctx_chat", "Chat", theme, ImVec2(ImGui::GetFontSize() * 6.8f, 0.0f))) {
                            f->second.window_state |= window_state_show;
                            close_menu = true;
                        }
                        if (settings->get_local_game_id().AppID() == f->first.appid()) {
                            if (AeroButton("##aero_ctx_invite", "Invite", theme, ImVec2(ImGui::GetFontSize() * 6.8f, 0.0f))) {
                                f->second.window_state |= window_state_invite;
                                QueueFriendAction(f->first);
                                close_menu = true;
                            }
                            if (f->second.joinable && AeroButton("##aero_ctx_join", "Join", theme, ImVec2(ImGui::GetFontSize() * 6.8f, 0.0f))) {
                                f->second.window_state |= window_state_join;
                                QueueFriendAction(f->first);
                                close_menu = true;
                            }
                        }

                        if (close_menu || (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))) {
                            custom_context_open = false;
                        }
                    }
                    ImGui::End();
                    if (!menu_open) {
                        custom_context_open = false;
                    }
                }
            }
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.20f, 0.31f, 0.62f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(theme.panel_border));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImGui::ColorConvertU32ToFloat4(theme.title_top));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImGui::ColorConvertU32ToFloat4(theme.title_bottom));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

        if (show_achievements && achievements.size()) {
            ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 32, ImGui::GetFontSize() * 32), ImVec2(8192, 8192));
            bool show_achievement_window = show_achievements;
            if (ImGui::Begin("Achievement Window", &show_achievement_window)) {
                ImGui::Text("List of achievements");
                ImGui::BeginChild("Achievements");
                for (auto &x : achievements) {
                    bool achieved = x.achieved;
                    bool hidden = x.hidden && !achieved;
                    ImGui::Separator();
                    ImGui::Text("%s", x.title.c_str());
                    if (hidden) ImGui::Text("hidden achievement");
                    else ImGui::TextWrapped("%s", x.description.c_str());
                    if (achieved) {
                        char buffer[80] = {};
                        time_t unlock_time = (time_t)x.unlock_time;
                        std::strftime(buffer, 80, "%Y-%m-%d at %H:%M:%S", std::localtime(&unlock_time));
                        ImGui::TextColored(ImVec4(0.85f, 1.0f, 0.85f, 1.0f), "achieved on %s", buffer);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.72f, 1.0f), "not achieved");
                    }
                    ImGui::Separator();
                }
                ImGui::EndChild();
            }
            ImGui::End();
            show_achievements = show_achievement_window;
        }

        if (show_settings) {
            if (ImGui::Begin("Global Settings Window", &show_settings)) {
                ImGui::Text("These are global emulator settings and will apply to all games.");
                ImGui::Separator();
                ImGui::Text("Username:");
                ImGui::SameLine();
                ImGui::InputText("##username", username_text, sizeof(username_text), disable_forced ? ImGuiInputTextFlags_ReadOnly : 0);
                ImGui::Separator();
                ImGui::Text("Language:");
                ImGui::ListBox("##language", &current_language, valid_languages, sizeof(valid_languages) / sizeof(char *), 7);
                ImGui::Text("Selected Language: %s", valid_languages[current_language]);
                ImGui::Separator();
                ImGui::Text("Overlay Theme:");
                if (ImGui::Combo("##overlay_theme", &current_theme, retro_themes, sizeof(retro_themes) / sizeof(retro_themes[0]))) {
                    char theme_text[8] = {};
                    std::snprintf(theme_text, sizeof(theme_text), "%d", current_theme);
                    get_steam_client()->local_storage->store_data_settings("overlay_theme.txt", theme_text, strlen(theme_text));
                }
                ImGui::Text("Current Theme: %s", retro_themes[current_theme]);
                bool custom_changed = false;
                bool custom_enabled = g_overlay_colors.enabled;
                if (ImGui::Checkbox("Use Custom Theme Colors", &custom_enabled)) {
                    g_overlay_colors.enabled = custom_enabled;
                    custom_changed = true;
                }
                if (g_overlay_colors.enabled) {
                    ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float;
                    custom_changed |= ImGui::ColorEdit4("Text Color", &g_overlay_colors.text.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Window Color", &g_overlay_colors.window_bg.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Border Color", &g_overlay_colors.border.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Tab Color", &g_overlay_colors.tab.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Tab Active Color", &g_overlay_colors.tab_active.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Button Color", &g_overlay_colors.button.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Button Hover Color", &g_overlay_colors.button_hovered.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Button Active Color", &g_overlay_colors.button_active.x, color_flags);
                    custom_changed |= ImGui::ColorEdit4("Accent Color", &g_overlay_colors.accent.x, color_flags);
                }
                if (custom_changed) {
                    SaveCustomOverlayColors(get_steam_client()->local_storage);
                }
                ImGui::Separator();
                ImGui::Text("UI Scale:");
                if (ImGui::Combo("##overlay_ui_scale", &current_ui_scale, overlay_ui_scales, sizeof(overlay_ui_scales) / sizeof(overlay_ui_scales[0]))) {
                    char ui_scale_text[8] = {};
                    std::snprintf(ui_scale_text, sizeof(ui_scale_text), "%d", current_ui_scale);
                    get_steam_client()->local_storage->store_data_settings("overlay_ui_scale.txt", ui_scale_text, strlen(ui_scale_text));
                }
                ImGui::Text("Current Scale: %s", overlay_ui_scales[current_ui_scale]);
                ImGui::Separator();
                ImGui::Text("Overlay Frontend:");
                if (ImGui::Combo("##overlay_frontend", &current_frontend, overlay_frontends, sizeof(overlay_frontends) / sizeof(overlay_frontends[0]))) {
                    char frontend_text[8] = {};
                    std::snprintf(frontend_text, sizeof(frontend_text), "%d", current_frontend);
                    get_steam_client()->local_storage->store_data_settings("overlay_frontend.txt", frontend_text, strlen(frontend_text));
                    SetupOverlayFrontend();
                }
                const char *frontend_name = overlay_frontend ? overlay_frontend->Name() : "None";
                ImGui::Text("Active Frontend: %s", frontend_name);
                ImGui::Separator();
                if (!disable_forced) {
                    ImGui::Text("Language/name are saved; theme and scale apply immediately.");
                    if (AeroButton("##settings_save_custom", "Save", theme)) {
                        save_settings = true;
                        show_settings = false;
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.75f, 1.0f), "WARNING WARNING WARNING");
                    ImGui::TextWrapped("Some steam_settings/force_*.txt files have been detected. Please delete them if you want this menu to work.");
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.75f, 1.0f), "WARNING WARNING WARNING");
                }
            }
            ImGui::End();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
    }
    ImGui::End();
    custom_ui_ctx.EndFrame();
    ImGui::PopFont();

    if (!show)
        ShowOverlay(false);
}

void Steam_Overlay::ProcessFriendActionFor(std::pair<const Friend, friend_window_state> &friend_info, Steam_Friends* steamFriends, Steam_Matchmaking* steamMatchmaking)
{
    uint64 friend_id = friend_info.first.id();

    // The user clicked on "Send"
    if (friend_info.second.window_state & window_state_send_message)
    {
        char* input = friend_info.second.chat_input;
        char* end_input = input + strlen(input);
        char* printable_char = std::find_if(input, end_input, [](char c) {
            return std::isgraph(c);
        });

        // Check if the message contains something else than blanks
        if (printable_char != end_input)
        {
            Common_Message msg;
            Steam_Messages* steam_messages = new Steam_Messages;
            steam_messages->set_type(Steam_Messages::FRIEND_CHAT);
            steam_messages->set_message(friend_info.second.chat_input);
            msg.set_allocated_steam_messages(steam_messages);
            msg.set_source_id(settings->get_local_steam_id().ConvertToUint64());
            msg.set_dest_id(friend_id);
            network->sendTo(&msg, true);

            friend_info.second.chat_history.append(get_steam_client()->settings_client->get_local_name()).append(": ").append(input).append("\n", 1);
        }
        *input = 0;
        friend_info.second.window_state &= ~window_state_send_message;
    }

    // The user clicked on "Invite"
    if (friend_info.second.window_state & window_state_invite)
    {
        std::string connect = steamFriends->GetFriendRichPresence(settings->get_local_steam_id(), "connect");
        if (connect.length() > 0) {
            steamFriends->InviteUserToGame(friend_id, connect.c_str());
            friend_info.second.chat_history.append("[SYSTEM] Invite sent.\n");
        } else if (settings->get_lobby().IsValid()) {
            steamMatchmaking->InviteUserToLobby(settings->get_lobby(), friend_id);
            friend_info.second.chat_history.append("[SYSTEM] Lobby invite sent.\n");
        } else {
            AddMessageNotification("Cannot send invite: no active lobby/connect string.");
            friend_info.second.chat_history.append("[SYSTEM] Cannot send invite: no active lobby/connect string.\n");
            friend_info.second.window_state |= window_state_show;
        }

        friend_info.second.window_state &= ~window_state_invite;
    }

    // The user clicked on "Join"
    if (friend_info.second.window_state & window_state_join)
    {
        std::string connect = steamFriends->GetFriendRichPresence(friend_id, "connect");
        if (friend_info.second.window_state & window_state_lobby_invite)
        {
            GameLobbyJoinRequested_t data;
            data.m_steamIDLobby.SetFromUint64(friend_info.second.lobbyId);
            data.m_steamIDFriend.SetFromUint64(friend_id);
            callbacks->addCBResult(data.k_iCallback, &data, sizeof(data));
            friend_info.second.window_state &= ~window_state_lobby_invite;
        } else {
            if (friend_info.second.window_state & window_state_rich_invite)
            {
                GameRichPresenceJoinRequested_t data = {};
                data.m_steamIDFriend.SetFromUint64(friend_id);
                strncpy(data.m_rgchConnect, friend_info.second.connect, k_cchMaxRichPresenceValueLength - 1);
                callbacks->addCBResult(data.k_iCallback, &data, sizeof(data));
                friend_info.second.window_state &= ~window_state_rich_invite;
            } else if (connect.length() > 0)
            {
                GameRichPresenceJoinRequested_t data = {};
                data.m_steamIDFriend.SetFromUint64(friend_id);
                strncpy(data.m_rgchConnect, connect.c_str(), k_cchMaxRichPresenceValueLength - 1);
                callbacks->addCBResult(data.k_iCallback, &data, sizeof(data));
            }

            // Not sure about this but it fixes sonic racing transformed invites
            FriendGameInfo_t friend_game_info = {};
            steamFriends->GetFriendGamePlayed(friend_id, &friend_game_info);
            uint64 lobby_id = friend_game_info.m_steamIDLobby.ConvertToUint64();
            if (lobby_id) {
                GameLobbyJoinRequested_t data;
                data.m_steamIDLobby.SetFromUint64(lobby_id);
                data.m_steamIDFriend.SetFromUint64(friend_id);
                callbacks->addCBResult(data.k_iCallback, &data, sizeof(data));
            }
        }

        friend_info.second.window_state &= ~window_state_join;
    }
}

void Steam_Overlay::ProcessPendingFriendActions(Steam_Friends* steamFriends, Steam_Matchmaking* steamMatchmaking)
{
    while (!has_friend_action.empty())
    {
        auto friend_info = friends.find(has_friend_action.front());
        if (friend_info != friends.end()) {
            ProcessFriendActionFor(*friend_info, steamFriends, steamMatchmaking);
        }
        has_friend_action.pop();
    }
}

void Steam_Overlay::Callback(Common_Message *msg)
{
    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    if (msg->has_steam_messages())
    {
        Friend frd;
        frd.set_id(msg->source_id());
        auto friend_info = friends.find(frd);
        if (friend_info != friends.end())
        {
            Steam_Messages const& steam_message = msg->steam_messages();
            // Change color to cyan for friend
            friend_info->second.chat_history.append(friend_info->first.name() + ": " + steam_message.message()).append("\n", 1);
            if (!(friend_info->second.window_state & window_state_show))
            {
                friend_info->second.window_state |= window_state_need_attention;
            }

            AddMessageNotification(friend_info->first.name() + " says: " + steam_message.message());
            NotifyUser(friend_info->second);
        }
    }
}

void Steam_Overlay::RunCallbacks()
{
    if (!achievements.size()) {
        Steam_User_Stats* steamUserStats = get_steam_client()->steam_user_stats;
        uint32 achievements_num = steamUserStats->GetNumAchievements();
        if (achievements_num) {
            PRINT_DEBUG("POPULATE OVERLAY ACHIEVEMENTS\n");
            for (unsigned i = 0; i < achievements_num; ++i) {
                Overlay_Achievement ach;
                ach.name = steamUserStats->GetAchievementName(i);
                ach.title = steamUserStats->GetAchievementDisplayAttribute(ach.name.c_str(), "name");
                ach.description = steamUserStats->GetAchievementDisplayAttribute(ach.name.c_str(), "desc");
                const char *hidden = steamUserStats->GetAchievementDisplayAttribute(ach.name.c_str(), "hidden");
                if (strlen(hidden) && hidden[0] == '1') {
                    ach.hidden = true;
                } else {
                    ach.hidden = false;
                }

                bool achieved = false;
                uint32 unlock_time = 0;
                if (steamUserStats->GetAchievementAndUnlockTime(ach.name.c_str(), &achieved, &unlock_time)) {
                    ach.achieved = achieved;
                    ach.unlock_time = unlock_time;
                } else {
                    ach.achieved = false;
                    ach.unlock_time = 0;
                }

                achievements.push_back(ach);
            }

            PRINT_DEBUG("POPULATE OVERLAY ACHIEVEMENTS DONE\n");
        }
    }

    if (!Ready() && future_renderer.valid()) {
        if (future_renderer.wait_for(std::chrono::milliseconds{0}) ==  std::future_status::ready) {
            _renderer = future_renderer.get();
            PRINT_DEBUG("got renderer %p\n", _renderer);
            CreateFonts();
        }
    }

    if (!Ready() && _renderer) {
            _renderer->OverlayHookReady = std::bind(&Steam_Overlay::HookReady, this, std::placeholders::_1);
            _renderer->OverlayProc = std::bind(&Steam_Overlay::OverlayProc, this);
            auto callback = std::bind(&Steam_Overlay::OpenOverlayHook, this, std::placeholders::_1);
            PRINT_DEBUG("start renderer\n", _renderer);
            std::set<ingame_overlay::ToggleKey> keys = {ingame_overlay::ToggleKey::SHIFT, ingame_overlay::ToggleKey::TAB};
            _renderer->ImGuiFontAtlas = fonts_atlas;
            bool started = _renderer->StartHook(callback, keys);
            PRINT_DEBUG("tried to start renderer %u\n", started);
    }

    if (overlay_state_changed)
    {
        GameOverlayActivated_t data = { 0 };
        data.m_bActive = show_overlay;
        data.m_bUserInitiated = true;
        data.m_nAppID = settings->get_local_game_id().AppID();
        callbacks->addCBResult(data.k_iCallback, &data, sizeof(data));

        overlay_state_changed = false;
    }

    Steam_Friends* steamFriends = get_steam_client()->steam_friends;
    Steam_Matchmaking* steamMatchmaking = get_steam_client()->steam_matchmaking;
    LoadCustomOverlayColors(get_steam_client()->local_storage);

    if (!theme_loaded) {
        char theme_text[8] = {};
        int read_size = get_steam_client()->local_storage->get_data_settings("overlay_theme.txt", theme_text, sizeof(theme_text) - 1);
        if (read_size > 0) {
            long saved_theme = std::strtol(theme_text, nullptr, 10);
            if (saved_theme >= 0 && saved_theme < static_cast<long>(sizeof(retro_themes) / sizeof(retro_themes[0]))) {
                current_theme = static_cast<int>(saved_theme);
            }
        }
        theme_loaded = true;
    }

    if (!frontend_loaded) {
        char frontend_text[8] = {};
        int read_size = get_steam_client()->local_storage->get_data_settings("overlay_frontend.txt", frontend_text, sizeof(frontend_text) - 1);
        if (read_size > 0) {
            long saved_frontend = std::strtol(frontend_text, nullptr, 10);
            if (saved_frontend >= 0 && saved_frontend < static_cast<long>(sizeof(overlay_frontends) / sizeof(overlay_frontends[0]))) {
                current_frontend = static_cast<int>(saved_frontend);
            }
        }
        SetupOverlayFrontend();
        frontend_loaded = true;
    }

    if (!ui_scale_loaded) {
        char ui_scale_text[8] = {};
        int read_size = get_steam_client()->local_storage->get_data_settings("overlay_ui_scale.txt", ui_scale_text, sizeof(ui_scale_text) - 1);
        if (read_size > 0) {
            long saved_ui_scale = std::strtol(ui_scale_text, nullptr, 10);
            if (saved_ui_scale >= 0 && saved_ui_scale < static_cast<long>(sizeof(overlay_ui_scales) / sizeof(overlay_ui_scales[0]))) {
                current_ui_scale = static_cast<int>(saved_ui_scale);
            }
        }
        ui_scale_loaded = true;
    }

    if (save_settings) {
        char *language_text = valid_languages[current_language];
        save_global_settings(get_steam_client()->local_storage, username_text, language_text);
        get_steam_client()->settings_client->set_local_name(username_text);
        get_steam_client()->settings_server->set_local_name(username_text);
        get_steam_client()->settings_client->set_language(language_text);
        get_steam_client()->settings_server->set_language(language_text);
        steamFriends->resend_friend_data();
        save_settings = false;
    }

    appid = settings->get_local_game_id().AppID();

    i_have_lobby = IHaveLobby();
    std::lock_guard<std::recursive_mutex> lock(overlay_mutex);
    std::for_each(friends.begin(), friends.end(), [this](std::pair<Friend const, friend_window_state> &i)
    {
        i.second.joinable = FriendJoinable(i);
    });

    ProcessPendingFriendActions(steamFriends, steamMatchmaking);
}

#endif
