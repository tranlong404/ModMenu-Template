#include "WorkspaceUI.h"
#include "imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace WorkspaceUI {
namespace {
ImVec4 Color(unsigned rgb, float alpha = 1.f) {
    return {float((rgb >> 16) & 255) / 255.f, float((rgb >> 8) & 255) / 255.f,
            float(rgb & 255) / 255.f, alpha};
}

ThemeColors gThemeColors{};
bool gIncreaseContrast = false;
bool gReduceTransparency = false;

ImVec4 Blend(ImVec4 a, ImVec4 b, float t) {
    return {ImLerp(a.x, b.x, t), ImLerp(a.y, b.y, t), ImLerp(a.z, b.z, t), ImLerp(a.w, b.w, t)};
}

struct NavInkState {
    float x = 0.f;
    float w = 0.f;
    bool initialized = false;
    float targetX = 0.f;
    float targetW = 0.f;
    bool hasTarget = false;
};

NavInkState &InkState() {
    static NavInkState state;
    return state;
}

void DrawIcon(Icon icon, ImVec2 a, float size, ImU32 color) {
    auto *draw = ImGui::GetWindowDrawList();
    const float t = std::max(1.2f, size * .10f);
    auto point = [&](float x, float y) { return ImVec2(a.x + size * x, a.y + size * y); };
    auto line = [&](float x, float y, float xx, float yy) { draw->AddLine(point(x, y), point(xx, yy), color, t); };
    switch (icon) {
    case Icon::Tools:
        line(.18f, .22f, .82f, .86f);
        line(.22f, .86f, .52f, .56f);
        draw->AddCircle(point(.75f, .27f), size * .16f, color, 10, t);
        break;
    case Icon::Script:
        line(.34f, .25f, .10f, .50f);
        line(.10f, .50f, .34f, .75f);
        line(.66f, .25f, .90f, .50f);
        line(.90f, .50f, .66f, .75f);
        line(.56f, .18f, .44f, .82f);
        break;
    case Icon::Menu:
        draw->AddRect(point(.16f, .16f), point(.44f, .44f), color, size * .10f, 0, t);
        draw->AddRect(point(.56f, .16f), point(.84f, .44f), color, size * .10f, 0, t);
        draw->AddRect(point(.16f, .56f), point(.44f, .84f), color, size * .10f, 0, t);
        draw->AddRectFilled(point(.56f, .56f), point(.84f, .84f), color, size * .10f);
        break;
    case Icon::Explorer:
        draw->AddCircle(point(.50f, .20f), size * .12f, color, 10, t);
        draw->AddCircle(point(.20f, .78f), size * .12f, color, 10, t);
        draw->AddCircle(point(.80f, .78f), size * .12f, color, 10, t);
        line(.50f, .32f, .50f, .52f);
        line(.20f, .52f, .80f, .52f);
        line(.20f, .52f, .20f, .66f);
        line(.80f, .52f, .80f, .66f);
        break;
    case Icon::Patches:
        draw->AddRect(point(.20f, .20f), point(.80f, .80f), color, size * .22f, 0, t);
        line(.50f, .34f, .50f, .66f);
        line(.34f, .50f, .66f, .50f);
        break;
    case Icon::Trace:
        line(.06f, .55f, .28f, .55f);
        line(.28f, .55f, .43f, .18f);
        line(.43f, .18f, .60f, .84f);
        line(.60f, .84f, .75f, .45f);
        line(.75f, .45f, .94f, .45f);
        break;
    case Icon::Strings:
        line(.20f, .28f, .42f, .28f);
        line(.20f, .28f, .20f, .72f);
        line(.58f, .28f, .80f, .28f);
        line(.58f, .28f, .58f, .72f);
        draw->AddRectFilled(point(.20f, .52f), point(.42f, .76f), color, 1.f);
        draw->AddRectFilled(point(.58f, .52f), point(.80f, .76f), color, 1.f);
        break;
    case Icon::Dumper:
        line(.50f, .14f, .50f, .62f);
        line(.30f, .42f, .50f, .62f);
        line(.70f, .42f, .50f, .62f);
        line(.14f, .66f, .14f, .88f);
        line(.14f, .88f, .86f, .88f);
        line(.86f, .88f, .86f, .66f);
        break;
    case Icon::AI:
        line(.50f, .10f, .62f, .38f);
        line(.62f, .38f, .90f, .50f);
        line(.90f, .50f, .62f, .62f);
        line(.62f, .62f, .50f, .90f);
        line(.50f, .90f, .38f, .62f);
        line(.38f, .62f, .10f, .50f);
        line(.10f, .50f, .38f, .38f);
        line(.38f, .38f, .50f, .10f);
        break;
    case Icon::Settings:
        draw->AddCircle(point(.5f, .5f), size * .26f, color, 12, t);
        for (int i = 0; i < 8; ++i) {
            float angle = float(i) * 3.14159265f / 4.f;
            draw->AddLine(point(.5f + .30f * std::cos(angle), .5f + .30f * std::sin(angle)),
                          point(.5f + .46f * std::cos(angle), .5f + .46f * std::sin(angle)), color, t);
        }
        break;
    case Icon::Network:
        draw->AddCircle(point(.50f, .50f), size * .36f, color, 16, t);
        draw->AddCircle(point(.50f, .50f), size * .18f, color, 12, t * .85f);
        line(.14f, .50f, .86f, .50f);
        line(.50f, .14f, .50f, .86f);
        break;
    case Icon::Backup:
        draw->AddRect(point(.16f, .18f), point(.84f, .84f), color, size * .14f, 0, t);
        line(.30f, .18f, .30f, .44f);
        line(.70f, .18f, .70f, .44f);
        line(.30f, .44f, .70f, .44f);
        line(.32f, .64f, .68f, .64f);
        break;
    }
}
} // namespace

void SetAccessibility(bool increaseContrast, bool reduceTransparency) {
    gIncreaseContrast = increaseContrast;
    gReduceTransparency = reduceTransparency;
}

void ApplyTheme(int themeIndex) {
    auto &s = ImGui::GetStyle();
    s = ImGuiStyle{};

    auto setCommonMetrics = [&] {
        // Apple HIG geometric harmony: continuous squircle corners, touch-safe targets, crisp hairlines
        s.WindowRounding = 18.f;
        s.ChildRounding = 14.f;
        s.FrameRounding = 10.f;
        s.PopupRounding = 14.f;
        s.TabRounding = 10.f;
        s.ScrollbarRounding = 99.f;
        s.GrabRounding = 99.f;
        s.WindowBorderSize = gIncreaseContrast ? 1.5f : 1.f;
        s.ChildBorderSize = gIncreaseContrast ? 1.5f : 1.f;
        s.PopupBorderSize = gIncreaseContrast ? 1.5f : 1.f;
        s.FrameBorderSize = gIncreaseContrast ? 1.25f : 1.f;
        s.WindowPadding = {16.f, 14.f};
        s.FramePadding = {13.f, 8.f};
        s.ItemSpacing = {10.f, 10.f};
        s.CellPadding = {9.f, 6.f};
        s.TouchExtraPadding = {5.f, 5.f};
        s.ScrollbarSize = 17.f;
        s.GrabMinSize = 24.f;
        s.WindowTitleAlign = {0.f, .5f};
    };

    const float winAlphaDark = (gReduceTransparency || gIncreaseContrast) ? 1.f : .94f;
    const float childAlphaDark = (gReduceTransparency || gIncreaseContrast) ? 1.f : .97f;
    const float winAlphaLight = (gReduceTransparency || gIncreaseContrast) ? 1.f : .97f;
    const float childAlphaLight = (gReduceTransparency || gIncreaseContrast) ? 1.f : .98f;

    if (themeIndex == static_cast<int>(Theme::AppleDark)) {
        // Apple macOS / iPadOS Dark Vibrancy + deep console elevation (WCAG >= 4.5:1)
        ImGui::StyleColorsDark(&s);
        setCommonMetrics();
        const ImVec4 textPrimary = Color(0xf5f7fb);
        const ImVec4 textDim = gIncreaseContrast ? Color(0xd0d6e0) : Color(0x9ea8b8);
        const ImVec4 borderCol = gIncreaseContrast ? Color(0x7c8698) : Color(0x363d4a, .92f);
        const ImVec4 promFill = Color(0x0a6ce0);
        const ImVec4 promHover = Color(0x1f82f7);
        const ImVec4 promActive = Color(0x0056bd);
        const ImVec4 accent = Color(0x52a8ff);

        s.Colors[ImGuiCol_Text] = textPrimary;
        s.Colors[ImGuiCol_TextDisabled] = textDim;
        s.Colors[ImGuiCol_WindowBg] = Color(0x0e1117, winAlphaDark);
        s.Colors[ImGuiCol_ChildBg] = Color(0x161b24, childAlphaDark);
        s.Colors[ImGuiCol_PopupBg] = Color(0x1d2330, .99f);
        s.Colors[ImGuiCol_Border] = borderCol;
        s.Colors[ImGuiCol_Separator] = gIncreaseContrast ? Color(0x7c8698) : Color(0x2d3442, .90f);
        s.Colors[ImGuiCol_SeparatorHovered] = accent;
        s.Colors[ImGuiCol_SeparatorActive] = accent;
        s.Colors[ImGuiCol_TitleBg] = Color(0x141922, 1.f);
        s.Colors[ImGuiCol_TitleBgActive] = Color(0x181e29, 1.f);
        s.Colors[ImGuiCol_TitleBgCollapsed] = Color(0x141922, .95f);
        s.Colors[ImGuiCol_FrameBg] = Color(0x212836);
        s.Colors[ImGuiCol_FrameBgHovered] = Color(0x2c3547);
        s.Colors[ImGuiCol_FrameBgActive] = Color(0x374258);
        // Secondary actions use neutral elevated fills; reserve accent for PushProminentButton
        s.Colors[ImGuiCol_Button] = Color(0x232b3a);
        s.Colors[ImGuiCol_ButtonHovered] = Color(0x2f394c);
        s.Colors[ImGuiCol_ButtonActive] = Color(0x3a465c);
        s.Colors[ImGuiCol_Header] = Color(0x1f82f7, .24f);
        s.Colors[ImGuiCol_HeaderHovered] = Color(0x1f82f7, .36f);
        s.Colors[ImGuiCol_HeaderActive] = Color(0x1f82f7, .48f);
        s.Colors[ImGuiCol_Tab] = Color(0x1b212d);
        s.Colors[ImGuiCol_TabActive] = Color(0x1f82f7, .22f);
        s.Colors[ImGuiCol_TabHovered] = Color(0x262f40);
        s.Colors[ImGuiCol_CheckMark] = Color(0x30d158);
        s.Colors[ImGuiCol_SliderGrab] = Color(0xf5f7fb);
        s.Colors[ImGuiCol_SliderGrabActive] = accent;
        s.Colors[ImGuiCol_TableBorderLight] = Color(0x252c3a);
        s.Colors[ImGuiCol_TableBorderStrong] = borderCol;
        s.Colors[ImGuiCol_TableRowBg] = Color(0x161b24, .35f);
        s.Colors[ImGuiCol_TableRowBgAlt] = Color(0x212836, .36f);

        gThemeColors = {
            accent, Color(0x30d158), Color(0x30d158), Color(0xffb340), Color(0xff6961),
            textDim, borderCol, Color(0x252c3a),
            Color(0x161b24, childAlphaDark), Color(0x212836), Color(0x2c3547),
            Color(0x232b3a), Color(0x1b212d), Color(0x1f82f7, .22f), Color(0x262f40),
            promFill, promHover, promActive, accent, Color(0x374258)
        };
        return;
    }

    if (themeIndex == static_cast<int>(Theme::AppleLight)) {
        // Apple macOS / iPadOS Light Paper Vibrancy (WCAG >= 4.5:1 verified)
        ImGui::StyleColorsLight(&s);
        setCommonMetrics();
        const ImVec4 textPrimary = Color(0x1c1c1e);
        const ImVec4 textDim = gIncreaseContrast ? Color(0x3a3a3c) : Color(0x5c5c60);
        const ImVec4 borderCol = gIncreaseContrast ? Color(0x636366) : Color(0xc7c7cc, .95f);
        const ImVec4 promFill = Color(0x005ec4);
        const ImVec4 promHover = Color(0x0a70dd);
        const ImVec4 promActive = Color(0x004da3);

        s.Colors[ImGuiCol_Text] = textPrimary;
        s.Colors[ImGuiCol_TextDisabled] = textDim;
        s.Colors[ImGuiCol_WindowBg] = Color(0xf2f2f7, winAlphaLight);
        s.Colors[ImGuiCol_ChildBg] = Color(0xffffff, childAlphaLight);
        s.Colors[ImGuiCol_PopupBg] = Color(0xffffff, .99f);
        s.Colors[ImGuiCol_Border] = borderCol;
        s.Colors[ImGuiCol_Separator] = gIncreaseContrast ? Color(0x8e8e93) : Color(0xd1d1d6);
        s.Colors[ImGuiCol_SeparatorHovered] = promFill;
        s.Colors[ImGuiCol_SeparatorActive] = promFill;
        s.Colors[ImGuiCol_TitleBg] = Color(0xe5e5ea);
        s.Colors[ImGuiCol_TitleBgActive] = Color(0xf5f5fa);
        s.Colors[ImGuiCol_TitleBgCollapsed] = Color(0xe5e5ea);
        s.Colors[ImGuiCol_FrameBg] = Color(0xe5e5ea);
        s.Colors[ImGuiCol_FrameBgHovered] = Color(0xd8d8df);
        s.Colors[ImGuiCol_FrameBgActive] = Color(0xc7c7cc);
        // Secondary buttons stay neutral light gray with dark text (13.55:1)
        s.Colors[ImGuiCol_Button] = Color(0xe5e5ea);
        s.Colors[ImGuiCol_ButtonHovered] = Color(0xd8d8df);
        s.Colors[ImGuiCol_ButtonActive] = Color(0xc7c7cc);
        s.Colors[ImGuiCol_Header] = Color(0x007aff, .14f);
        s.Colors[ImGuiCol_HeaderHovered] = Color(0x007aff, .22f);
        s.Colors[ImGuiCol_HeaderActive] = Color(0x007aff, .30f);
        s.Colors[ImGuiCol_Tab] = Color(0xe5e5ea);
        s.Colors[ImGuiCol_TabActive] = Color(0x007aff, .14f);
        s.Colors[ImGuiCol_TabHovered] = Color(0xd8d8df);
        s.Colors[ImGuiCol_CheckMark] = Color(0x248a3d);
        s.Colors[ImGuiCol_SliderGrab] = Color(0x3a3a3c);
        s.Colors[ImGuiCol_SliderGrabActive] = promFill;
        s.Colors[ImGuiCol_TableBorderLight] = Color(0xe5e5ea);
        s.Colors[ImGuiCol_TableBorderStrong] = borderCol;

        gThemeColors = {
            promFill, Color(0x248a3d), Color(0x1b7a34), Color(0x995200), Color(0xc41e25),
            textDim, borderCol, Color(0xe5e5ea),
            Color(0xffffff, childAlphaLight), Color(0xe5e5ea), Color(0xd8d8df),
            Color(0xe5e5ea), Color(0xe5e5ea), Color(0x007aff, .14f), Color(0xd8d8df),
            promFill, promHover, promActive, promFill, Color(0xffffff)
        };
        return;
    }

    if (themeIndex == static_cast<int>(Theme::Dark)) {
        ImGui::StyleColorsDark(&s);
        setCommonMetrics();
        gThemeColors = {
            Color(0x6bb8ff), Color(0x5eadff), Color(0x7dd87d), Color(0xe5c07b), Color(0xff6b6b),
            s.Colors[ImGuiCol_TextDisabled], s.Colors[ImGuiCol_Border], Color(0x2a2d34),
            s.Colors[ImGuiCol_ChildBg], s.Colors[ImGuiCol_FrameBg], s.Colors[ImGuiCol_FrameBgHovered],
            s.Colors[ImGuiCol_Button], s.Colors[ImGuiCol_Tab], s.Colors[ImGuiCol_TabActive],
            s.Colors[ImGuiCol_TabHovered],
            Color(0x0066da), Color(0x0a78f0), Color(0x0054b8), Color(0x6bb8ff), Color(0x48484a)
        };
        return;
    }
    if (themeIndex == static_cast<int>(Theme::Light)) {
        ImGui::StyleColorsLight(&s);
        setCommonMetrics();
        gThemeColors = {
            Color(0x005ec4), Color(0x1b7a34), Color(0x1b7a34), Color(0x995200), Color(0xc41e25),
            s.Colors[ImGuiCol_TextDisabled], s.Colors[ImGuiCol_Border], Color(0xd0d7e2),
            s.Colors[ImGuiCol_ChildBg], s.Colors[ImGuiCol_FrameBg], s.Colors[ImGuiCol_FrameBgHovered],
            s.Colors[ImGuiCol_Button], s.Colors[ImGuiCol_Tab], s.Colors[ImGuiCol_TabActive],
            s.Colors[ImGuiCol_TabHovered],
            Color(0x005ec4), Color(0x0a70dd), Color(0x004da3), Color(0x005ec4), Color(0xffffff)
        };
        return;
    }
    if (themeIndex == static_cast<int>(Theme::Classic)) {
        ImGui::StyleColorsClassic(&s);
        setCommonMetrics();
        gThemeColors = {
            Color(0x88b4ff), Color(0x6bb8ff), Color(0x7dd87d), Color(0xe5c07b), Color(0xff6b6b),
            s.Colors[ImGuiCol_TextDisabled], s.Colors[ImGuiCol_Border], Color(0x3a3a4a),
            s.Colors[ImGuiCol_ChildBg], s.Colors[ImGuiCol_FrameBg], s.Colors[ImGuiCol_FrameBgHovered],
            s.Colors[ImGuiCol_Button], s.Colors[ImGuiCol_Tab], s.Colors[ImGuiCol_TabActive],
            s.Colors[ImGuiCol_TabHovered],
            Color(0x2856a3), Color(0x356cc4), Color(0x1d417d), Color(0x88b4ff), Color(0x52526e)
        };
        return;
    }
    if (themeIndex == static_cast<int>(Theme::Purple)) {
        // Cracked v721 Default purple palette (#2B2333 / #332B41 / #746A85 / #B8A8D1 / #D6D3E2)
        ImGui::StyleColorsDark(&s);
        setCommonMetrics();
        s.Colors[ImGuiCol_Text] = Color(0xd6d3e2);
        s.Colors[ImGuiCol_TextDisabled] = Color(0xa59bb8);
        s.Colors[ImGuiCol_WindowBg] = Color(0x1e1824, gReduceTransparency ? 1.f : .97f);
        s.Colors[ImGuiCol_ChildBg] = Color(0x261f2e);
        s.Colors[ImGuiCol_PopupBg] = Color(0x221b2a, .98f);
        s.Colors[ImGuiCol_Border] = Color(0x746a85);
        s.Colors[ImGuiCol_Separator] = Color(0x473c57);
        s.Colors[ImGuiCol_SeparatorHovered] = Color(0xb8a8d1);
        s.Colors[ImGuiCol_TitleBg] = Color(0x2b2333);
        s.Colors[ImGuiCol_TitleBgActive] = Color(0x332b41);
        s.Colors[ImGuiCol_TitleBgCollapsed] = Color(0x2b2333);
        s.Colors[ImGuiCol_FrameBg] = Color(0x332b41);
        s.Colors[ImGuiCol_FrameBgHovered] = Color(0x453a57);
        s.Colors[ImGuiCol_FrameBgActive] = Color(0x56486e);
        s.Colors[ImGuiCol_Button] = Color(0x3d3152);
        s.Colors[ImGuiCol_ButtonHovered] = Color(0x4f406b);
        s.Colors[ImGuiCol_ButtonActive] = Color(0x614f82);
        s.Colors[ImGuiCol_Header] = Color(0x4e3c6b);
        s.Colors[ImGuiCol_HeaderHovered] = Color(0x634d85);
        s.Colors[ImGuiCol_HeaderActive] = Color(0x755c9e);
        s.Colors[ImGuiCol_Tab] = Color(0x2b2333);
        s.Colors[ImGuiCol_TabActive] = Color(0x5b467a);
        s.Colors[ImGuiCol_TabHovered] = Color(0x725998);
        s.Colors[ImGuiCol_CheckMark] = Color(0xb8a8d1);
        s.Colors[ImGuiCol_SliderGrab] = Color(0xb8a8d1);
        s.Colors[ImGuiCol_SliderGrabActive] = Color(0xd6d3e2);

        gThemeColors = {
            Color(0xb8a8d1), Color(0xb8a8d1), Color(0x7dd87d), Color(0xe5c07b), Color(0xff6b6b),
            Color(0xa59bb8), Color(0x746a85), Color(0x473c57),
            Color(0x261f2e), Color(0x332b41), Color(0x453a57),
            Color(0x3d3152), Color(0x2b2333), Color(0x5b467a), Color(0x725998),
            Color(0x5b467a), Color(0x725998), Color(0x473561), Color(0xd0c2e8), Color(0x5b467a)
        };
        return;
    }

    // Theme::Blue — WebUI Redesign (:root tokens)
    ImGui::StyleColorsDark(&s);
    setCommonMetrics();
    s.Colors[ImGuiCol_Text] = Color(0xe8ecf4);
    s.Colors[ImGuiCol_TextDisabled] = Color(0xa2acbf);
    s.Colors[ImGuiCol_WindowBg] = Color(0x0e131d, gReduceTransparency ? 1.f : .97f);
    s.Colors[ImGuiCol_ChildBg] = Color(0x131a26);
    s.Colors[ImGuiCol_PopupBg] = Color(0x121722, .99f);
    s.Colors[ImGuiCol_Border] = Color(0x334a6b);
    s.Colors[ImGuiCol_Separator] = Color(0x20304a);
    s.Colors[ImGuiCol_SeparatorHovered] = Color(0x5eadff);
    s.Colors[ImGuiCol_SeparatorActive] = Color(0x6bb8ff);
    s.Colors[ImGuiCol_TitleBg] = Color(0x14335c);
    s.Colors[ImGuiCol_TitleBgActive] = Color(0x14335c);
    s.Colors[ImGuiCol_TitleBgCollapsed] = Color(0x14335c);
    s.Colors[ImGuiCol_FrameBg] = Color(0x1f2e45);
    s.Colors[ImGuiCol_FrameBgHovered] = Color(0x2b4a75);
    s.Colors[ImGuiCol_FrameBgActive] = Color(0x2e5992);
    s.Colors[ImGuiCol_Button] = Color(0x1f2e45);
    s.Colors[ImGuiCol_ButtonHovered] = Color(0x2b4a75);
    s.Colors[ImGuiCol_ButtonActive] = Color(0x2e5992);
    s.Colors[ImGuiCol_Header] = Color(0x1f4a7d);
    s.Colors[ImGuiCol_HeaderHovered] = Color(0x2b5c98);
    s.Colors[ImGuiCol_HeaderActive] = Color(0x2663b0);
    s.Colors[ImGuiCol_Tab] = Color(0x1a3052);
    s.Colors[ImGuiCol_TabActive] = Color(0x2663b0);
    s.Colors[ImGuiCol_TabHovered] = Color(0x3075c7);
    s.Colors[ImGuiCol_CheckMark] = Color(0x5eadff);
    s.Colors[ImGuiCol_SliderGrab] = Color(0x5eadff);
    s.Colors[ImGuiCol_SliderGrabActive] = Color(0x6bb8ff);
    s.Colors[ImGuiCol_TableBorderLight] = Color(0x20304a);
    s.Colors[ImGuiCol_TableBorderStrong] = Color(0x334a6b);

    gThemeColors = {
        Color(0x6bb8ff), Color(0x5eadff), Color(0x7dd87d), Color(0xe5c07b), Color(0xff6b6b),
        Color(0xa2acbf), Color(0x334a6b), Color(0x20304a),
        Color(0x131a26), Color(0x1f2e45), Color(0x2b4a75),
        Color(0x1f2e45), Color(0x1a3052), Color(0x2663b0), Color(0x3075c7),
        Color(0x0066da), Color(0x0a78f0), Color(0x0054b8), Color(0x6bb8ff), Color(0x2663b0)
    };
}

const ThemeColors &Colors() {
    static bool initialized = false;
    if (!initialized && gThemeColors.brand.w == 0.f) {
        initialized = true;
        ApplyTheme(0);
    }
    return gThemeColors;
}

void PushProminentButton() {
    const auto &c = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, c.prominentBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.prominentBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.prominentBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
}

void PopProminentButton() { ImGui::PopStyleColor(4); }

void PushDestructiveButton() {
    const auto &c = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, Blend(c.frameBg, c.danger, .16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Blend(c.frameBg, c.danger, .26f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Blend(c.frameBg, c.danger, .36f));
    ImGui::PushStyleColor(ImGuiCol_Text, c.danger);
}

void PopDestructiveButton() { ImGui::PopStyleColor(4); }

void Heading(const char *title, const char *subtitle) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float barH = ImGui::GetTextLineHeight() * 1.12f;
    ImGui::GetWindowDrawList()->AddRectFilled({p.x, p.y + 2.f}, {p.x + 3.f, p.y + barH},
                                              ImGui::GetColorU32(Colors().brand), 2.f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 9.f);
    ImGui::SetWindowFontScale(1.14f);
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", title);
    ImGui::SetWindowFontScale(1.0f);
    if (subtitle && *subtitle) {
        float subW = ImGui::CalcTextSize(subtitle).x + 14.f;
        float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (right - ImGui::GetItemRectMax().x >= subW) {
            ImGui::SameLine(0.f, 10.f);
            ImGui::TextColored(Colors().dim, "%s", subtitle);
        } else {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 9.f);
            ImGui::TextColored(Colors().dim, "%s", subtitle);
        }
    }
    ImGui::Spacing();
}

void SectionLabel(const char *title) {
    ImGui::Spacing();
    ImGui::TextColored(Colors().accentText, "%s", title);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec4 sep = gIncreaseContrast ? Colors().border : Blend(Colors().border, Colors().frameBg, .35f);
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y + 1.f}, {p.x + w, p.y + 1.f}, ImGui::GetColorU32(sep), 1.f);
    ImGui::Dummy({0, 5.f});
}

void NextControl(float width) {
    // Remaining width is relative to the current item. Never use a screen-space X offset.
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (right - ImGui::GetItemRectMax().x >= width + ImGui::GetStyle().ItemSpacing.x)
        ImGui::SameLine();
}

std::string Field(const char *label) {
    ImGui::TextWrapped("%s", label);
    ImGui::SetNextItemWidth(-1);
    return std::string("##") + label;
}

void BeginCard(const char *id, ImVec2 size, ImGuiWindowFlags flags, ImGuiChildFlags childFlags) {
    ImGui::BeginChild(id, size, ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding | childFlags, flags);
    if (!gReduceTransparency && !gIncreaseContrast) {
        ImVec2 p = ImGui::GetWindowPos();
        float w = ImGui::GetWindowWidth();
        float r = ImGui::GetStyle().ChildRounding;
        ImU32 sheen = IM_COL32(255, 255, 255, 18);
        ImGui::GetWindowDrawList()->AddLine({p.x + r * .6f, p.y + 1.f},
                                            {p.x + std::max(r * .6f, w - r * .6f), p.y + 1.f}, sheen, 1.f);
    }
}

void EndCard() { ImGui::EndChild(); }

bool BeginNavTab(const char *label, Icon icon, ImGuiTabItemFlags flags) {
    std::string text = "   ";
    text += label;
    text += "###nav";
    text += std::to_string(int(icon));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(Colors().brand.x, Colors().brand.y, Colors().brand.z, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(Colors().tabHover.x, Colors().tabHover.y, Colors().tabHover.z, 0.65f));
    bool active = ImGui::BeginTabItem(text.c_str(), nullptr, flags);
    ImGui::PopStyleColor(3);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (b.x > a.x) {
        auto *bar = ImGui::GetCurrentTabBar();
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect({bar->ScrollingRectMinX, bar->BarRect.Min.y},
                           {bar->ScrollingRectMaxX, bar->BarRect.Max.y}, true);
        float size = ImGui::GetFontSize() * .82f;
        DrawIcon(icon, {a.x + ImGui::GetStyle().FramePadding.x * .8f, a.y + (b.y - a.y - size) * .5f}, size,
                 ImGui::GetColorU32(active ? Colors().brand : Colors().dim));
        draw->PopClipRect();
        if (active) {
            auto &ink = InkState();
            ink.targetX = a.x;
            ink.targetW = b.x - a.x;
            ink.hasTarget = true;
            if (!ink.initialized) {
                ink.x = ink.targetX;
                ink.w = ink.targetW;
                ink.initialized = true;
            }
        }
    }
    return active;
}

void UpdateNavInk(const char * /*tabBarId*/) {
    auto &ink = InkState();
    if (!ink.hasTarget) return;
    if (auto *bar = ImGui::GetCurrentTabBar()) {
        float dt = std::clamp(ImGui::GetIO().DeltaTime * 18.f, 0.f, 1.f);
        ink.x = ImLerp(ink.x, ink.targetX, dt);
        ink.w = ImLerp(ink.w, ink.targetW, dt);
        auto *draw = ImGui::GetWindowDrawList();
        draw->PushClipRect({bar->ScrollingRectMinX, bar->BarRect.Min.y},
                           {bar->ScrollingRectMaxX, bar->BarRect.Max.y + 3.f}, true);
        float y = bar->BarRect.Max.y - 1.f;
        draw->AddLine({bar->ScrollingRectMinX, y}, {bar->ScrollingRectMaxX, y}, ImGui::GetColorU32(Colors().border), 1.f);
        draw->AddRectFilled({ink.x + 6.f, y - 2.5f}, {ink.x + std::max(12.f, ink.w - 6.f), y + 0.5f},
                            ImGui::GetColorU32(Colors().brand), 2.5f);
        draw->PopClipRect();
    }
    ink.hasTarget = false;
}

bool ToggleSwitch(const char *id, const char *label, bool *v, bool disabled) {
    static std::unordered_map<ImGuiID, float> animMap;
    ImGui::PushID(id);
    if (disabled) ImGui::BeginDisabled();

    float fontH = ImGui::GetFontSize();
    float h = std::max(22.f, fontH * 1.28f);
    float w = h * 1.82f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hasLabel = (label && *label);
    float avail = ImGui::GetContentRegionAvail().x;

    bool pressed = false;
    ImGui::BeginGroup();
    if (hasLabel) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(1.f, avail - w - ImGui::GetStyle().ItemSpacing.x));
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", label);
        ImGui::PopTextWrapPos();
        float switchX = p.x + std::max(0.f, avail - w);
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorScreenPos({switchX, p.y + std::max(0.f, (ImGui::GetFrameHeight() - h) * .5f)});
    }

    ImVec2 sp = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##sw", {w, h}) && !disabled) {
        *v = !*v;
        pressed = true;
    }
    ImGuiID animId = ImGui::GetItemID();
    float &t = animMap[animId];
    float target = *v ? 1.f : 0.f;
    t = ImLerp(t, target, std::clamp(ImGui::GetIO().DeltaTime * 18.f, 0.f, 1.f));

    auto *draw = ImGui::GetWindowDrawList();
    // iOS-style switch track (green/brand when active) + clean elevated thumb
    ImU32 bgCol = ImGui::GetColorU32(Blend(Colors().frameBg, Colors().check, t));
    ImU32 borderCol = ImGui::GetColorU32(Blend(Colors().border, Colors().check, t));
    if (disabled) {
        bgCol = ImGui::GetColorU32(ImVec4(Colors().frameBg.x, Colors().frameBg.y, Colors().frameBg.z, 0.45f));
    }
    float r = h * .5f;
    draw->AddRectFilled(sp, {sp.x + w, sp.y + h}, bgCol, r);
    draw->AddRect(sp, {sp.x + w, sp.y + h}, borderCol, r, 0, 1.f);
    float pad = std::max(2.f, h * .13f);
    float knobR = r - pad;
    float knobX = ImLerp(sp.x + r, sp.x + w - r, t);
    draw->AddCircleFilled({knobX, sp.y + r + 1.f}, knobR, IM_COL32(0, 0, 0, 48), 20);
    draw->AddCircleFilled({knobX, sp.y + r}, knobR, IM_COL32(255, 255, 255, disabled ? 140 : 255), 20);

    ImGui::EndGroup();
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
    return pressed;
}

bool ToggleChip(const char *label, bool *v, bool locked) {
    bool dummy = true;
    bool *ptr = v ? v : &dummy;
    ImGui::BeginChild(label, {0, ImGui::GetFrameHeight() + 6.f}, ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(3.f);
    if (locked) ImGui::BeginDisabled();
    bool changed = ToggleSwitch(label, label, ptr, locked);
    if (locked) ImGui::EndDisabled();
    ImGui::EndChild();
    return changed;
}

void StatusChip(const char *text, ChipTone tone) {
    ImVec4 dotCol = Colors().dim;
    if (tone == ChipTone::Ok) dotCol = Colors().ok;
    else if (tone == ChipTone::Warn) dotCol = Colors().warn;
    else if (tone == ChipTone::Danger) dotCol = Colors().danger;

    const bool hasTone = (tone != ChipTone::Neutral);
    ImVec4 pillBg = hasTone ? Blend(Colors().frameBg, dotCol, .14f) : Colors().frameBg;
    ImVec4 pillBorder = hasTone ? Blend(Colors().border, dotCol, .45f) : Colors().border;
    ImVec4 textCol = hasTone ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : Colors().dim;

    float h = ImGui::GetTextLineHeight() + 8.f;
    float dotR = std::max(3.f, ImGui::GetFontSize() * .20f);
    float padX = 10.f;
    float w = padX * 2.f + dotR * 2.f + 6.f + ImGui::CalcTextSize(text).x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::GetColorU32(pillBg), h * .5f);
    draw->AddRect(p, {p.x + w, p.y + h}, ImGui::GetColorU32(pillBorder), h * .5f, 0, 1.f);
    draw->AddCircleFilled({p.x + padX + dotR, p.y + h * .5f}, dotR, ImGui::GetColorU32(dotCol), 12);
    float textY = p.y + (h - ImGui::GetTextLineHeight()) * .5f;
    draw->AddText({p.x + padX + dotR * 2.f + 6.f, textY}, ImGui::GetColorU32(textCol), text);
    ImGui::Dummy({w, h});
}

void StatTile(const char *id, const char *value, const char *label) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().frameBg);
    ImGui::BeginChild(id, {0, ImGui::GetFontSize() * 4.3f}, ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImVec2 p = ImGui::GetWindowPos();
    float w = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled({p.x + 12.f, p.y + 1.f}, {p.x + std::max(14.f, w - 12.f), p.y + 3.f},
                                              ImGui::GetColorU32(Colors().brand), 1.5f);
    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetWindowFontScale(1.36f);
    float valW = ImGui::CalcTextSize(value).x;
    ImGui::SetCursorPosX(std::max(4.f, (availW - valW) * .5f));
    ImGui::TextUnformatted(value);
    ImGui::SetWindowFontScale(1.0f);
    float lblW = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(std::max(4.f, (availW - lblW) * .5f));
    ImGui::TextColored(Colors().dim, "%s", label);
    ImGui::EndChild();
}

void ProgressBarPill(float fraction, const char *caption) {
    float f = std::clamp(fraction, 0.f, 1.f);
    float w = std::max(40.f, ImGui::GetContentRegionAvail().x);
    float h = std::max(7.f, ImGui::GetFontSize() * .42f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::GetColorU32(Colors().frameBg), h * .5f);
    if (f > 0.f) {
        draw->AddRectFilled(p, {p.x + std::max(h, w * f), p.y + h},
                            ImGui::GetColorU32(Colors().prominentBtn), h * .5f);
    }
    draw->AddRect(p, {p.x + w, p.y + h}, ImGui::GetColorU32(Colors().border), h * .5f, 0, 1.f);
    ImGui::Dummy({w, h});
    if (caption && *caption) {
        ImGui::TextColored(Colors().dim, "%s", caption);
    }
}

bool RectSwitch(const char *id, const char *optA, const char *optB, int *selected) {
    // Apple HIG UISegmentedControl: single rounded trough with elevated contrasting thumb
    bool changed = false;
    ImGui::PushID(id);
    const float segW = std::max(ImGui::GetFontSize() * 6.2f,
                                std::max(ImGui::CalcTextSize(optA).x, ImGui::CalcTextSize(optB).x) + 24.f);
    const float segH = ImGui::GetFrameHeight();
    const float pad = 2.5f;
    const float totalW = segW * 2.f + pad * 2.f;
    const float totalH = segH + pad * 2.f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    const float outerR = 10.f;
    const float innerR = 8.f;
    draw->AddRectFilled(origin, {origin.x + totalW, origin.y + totalH}, ImGui::GetColorU32(Colors().frameBg), outerR);
    draw->AddRect(origin, {origin.x + totalW, origin.y + totalH}, ImGui::GetColorU32(Colors().border), outerR, 0, 1.f);

    auto drawSeg = [&](const char *lbl, int val, float offsetX) {
        const bool active = (*selected == val);
        ImVec2 sMin{origin.x + pad + offsetX, origin.y + pad};
        ImVec2 sMax{sMin.x + segW, sMin.y + segH};
        if (active) {
            draw->AddRectFilled(sMin, sMax, ImGui::GetColorU32(Colors().segmentedThumb), innerR);
            draw->AddRect(sMin, sMax, ImGui::GetColorU32(Colors().border), innerR, 0, 1.f);
        }
        ImGui::SetCursorScreenPos(sMin);
        if (ImGui::InvisibleButton(lbl, {segW, segH})) {
            if (*selected != val) {
                *selected = val;
                changed = true;
            }
        }
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        ImVec2 tp{sMin.x + (segW - ts.x) * .5f, sMin.y + (segH - ts.y) * .5f};
        draw->AddText(tp, ImGui::GetColorU32(active ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : Colors().dim), lbl);
    };
    drawSeg(optA, 0, 0.f);
    drawSeg(optB, 1, segW);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({totalW, totalH});
    ImGui::PopID();
    return changed;
}

void DrawResizeGrip(ImVec2 winPos, ImVec2 winSize) {
    auto *draw = ImGui::GetForegroundDrawList();
    float pad = 9.f;
    float s = 13.f;
    ImVec2 br{winPos.x + winSize.x - pad, winPos.y + winSize.y - pad};
    ImU32 col = ImGui::GetColorU32(Colors().brand);
    ImU32 colSoft = ImGui::GetColorU32(Blend(Colors().brand, Colors().dim, .45f));
    draw->AddLine({br.x - s, br.y}, {br.x, br.y - s}, colSoft, 1.8f);
    draw->AddLine({br.x - s * .62f, br.y}, {br.x, br.y - s * .62f}, col, 1.8f);
    draw->AddLine({br.x - s * .26f, br.y}, {br.x, br.y - s * .26f}, col, 1.6f);
}

bool UseSplit(float width) { return width >= ImGui::GetFontSize() * 42.f; }

void FitWindow(ImVec2 viewport, ImVec2 &position, ImVec2 &size) {
    const float margin = std::min(12.f, std::min(viewport.x, viewport.y) * .03f);
    const float maxX = std::max(1.f, viewport.x - 2 * margin), maxY = std::max(1.f, viewport.y - 2 * margin);
    size.x = std::clamp(size.x, std::min(480.f, maxX), maxX);
    size.y = std::clamp(size.y, std::min(320.f, maxY), maxY);
    position.x = std::clamp(position.x, margin, std::max(margin, viewport.x - size.x - margin));
    position.y = std::clamp(position.y, margin, std::max(margin, viewport.y - size.y - margin));
}

namespace {
bool gBubbleUser = false;
}

void BeginBubble(const char *id, const char *role, bool user) {
    gBubbleUser = user;
    float width = ImGui::GetContentRegionAvail().x;
    float bubble = width * (user ? .80f : .88f);
    if (user) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - bubble);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, user ? Colors().prominentBtn : Colors().frameBg);
    ImGui::PushStyleColor(ImGuiCol_Border, user ? Colors().prominentBtnHover : Blend(Colors().border, Colors().brand, .22f));
    if (user) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::BeginChild(id, {bubble, 0},
                      ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(0.90f);
    ImGui::TextColored(user ? ImVec4(.88f, .94f, 1.f, .92f) : Colors().accentText, "%s", role);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy({0.f, 2.f});
}

void EndBubble() {
    ImGui::EndChild();
    ImGui::PopStyleColor(gBubbleUser ? 3 : 2);
    ImGui::PopStyleVar();
    gBubbleUser = false;
    ImGui::Spacing();
}

void ToolCallPill(const char *label) {
    float avail = ImGui::GetContentRegionAvail().x;
    float dotR = std::max(3.f, ImGui::GetFontSize() * .18f);
    float padX = 11.f;
    float w = std::min(avail, padX * 2.f + dotR * 2.f + 7.f + ImGui::CalcTextSize(label).x);
    float h = ImGui::GetTextLineHeight() + 8.f;
    float x = ImGui::GetCursorPosX() + std::max(0.f, (avail - w) * .5f);
    ImGui::SetCursorPosX(x);
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto *draw = ImGui::GetWindowDrawList();
    ImVec4 bg = Blend(Colors().childBg, Colors().brand, .08f);
    ImVec4 bd = Blend(Colors().border, Colors().brand, .32f);
    draw->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::GetColorU32(bg), h * .5f);
    draw->AddRect(p, {p.x + w, p.y + h}, ImGui::GetColorU32(bd), h * .5f, 0, 1.f);
    draw->AddCircleFilled({p.x + padX + dotR, p.y + h * .5f}, dotR, ImGui::GetColorU32(Colors().brand), 12);
    float textY = p.y + (h - ImGui::GetTextLineHeight()) * .5f;
    draw->AddText({p.x + padX + dotR * 2.f + 7.f, textY}, ImGui::GetColorU32(Colors().dim), label);
    ImGui::Dummy({w, h + 4.f});
}
} // namespace WorkspaceUI
