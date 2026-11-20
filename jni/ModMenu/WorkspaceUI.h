#pragma once
#include "imgui/imgui.h"
#include <string>

// Presentation only. These helpers never attach to IL2CPP or schedule runtime work.
namespace WorkspaceUI {
enum class Icon { Tools, Script, Menu, Explorer, Patches, Trace, Strings, Dumper, AI, Settings, Network, Backup };
enum class Theme { AppleDark = 0, AppleLight = 1, Blue = 2, Purple = 3, Dark = 4, Light = 5, Classic = 6 };
enum class ChipTone { Neutral = 0, Ok = 1, Warn = 2, Danger = 3 };

struct ThemeColors {
    ImVec4 brand;
    ImVec4 check;
    ImVec4 ok;
    ImVec4 warn;
    ImVec4 danger;
    ImVec4 dim;
    ImVec4 border;
    ImVec4 borderSoft;
    ImVec4 childBg;
    ImVec4 frameBg;
    ImVec4 frameHover;
    ImVec4 button;
    ImVec4 tab;
    ImVec4 tabActive;
    ImVec4 tabHover;
    ImVec4 prominentBtn;
    ImVec4 prominentBtnHover;
    ImVec4 prominentBtnActive;
    ImVec4 accentText;
    ImVec4 segmentedThumb;
};

void SetAccessibility(bool increaseContrast, bool reduceTransparency);
void ApplyTheme(int themeIndex = 0);
const ThemeColors &Colors();
void PushProminentButton();
void PopProminentButton();
void PushDestructiveButton();
void PopDestructiveButton();

void Heading(const char *title, const char *subtitle = nullptr);
void SectionLabel(const char *title);
void NextControl(float width);
std::string Field(const char *label);

void BeginCard(const char *id, ImVec2 size = {0, 0}, ImGuiWindowFlags flags = 0,
               ImGuiChildFlags childFlags = 0);
void EndCard();

bool BeginNavTab(const char *label, Icon icon, ImGuiTabItemFlags flags = 0);
void UpdateNavInk(const char *tabBarId);

bool ToggleSwitch(const char *id, const char *label, bool *v, bool disabled = false);
bool ToggleChip(const char *label, bool *v, bool locked = false);
void StatusChip(const char *text, ChipTone tone = ChipTone::Neutral);
void StatTile(const char *id, const char *value, const char *label);
void ProgressBarPill(float fraction, const char *caption = nullptr);
bool RectSwitch(const char *id, const char *optA, const char *optB, int *selected);
void DrawResizeGrip(ImVec2 winPos, ImVec2 winSize);

bool UseSplit(float width);
void FitWindow(ImVec2 viewport, ImVec2 &position, ImVec2 &size);
void BeginBubble(const char *id, const char *role, bool user);
void EndBubble();
void ToolCallPill(const char *label);
}
