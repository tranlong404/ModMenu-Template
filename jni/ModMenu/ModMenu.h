// jni/ModMenu/ModMenu.h
#pragma once

#include "WorkspaceUI.h"
#include "imgui/imgui.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ModMenu {

struct MenuConfig {
    std::string title = "Mod Menu";
    std::string subtitle = "v1.0.0 · ARM64";
    std::string pillText = "MOD";
    std::string targetLib = "libil2cpp.so"; // Leave empty "" to run immediately without waiting for a library.
    int defaultTheme = 0;                   // 0=AppleDark, 1=AppleLight, 2=Blue, 3=Purple, 4=Dark, 5=Light, 6=Classic
    int defaultScaleIndex = 3;              // 0=0.75x, 1=0.85x, 2=0.95x, 3=1.0x, 4=1.15x, 5=1.3x, 6=1.5x, 7=1.75x
    bool startOpen = true;
};

class TabBuilder {
public:
    explicit TabBuilder(size_t tabIndex) : tabIndex_(tabIndex) {}

    // Starts a new Apple HIG card section inside this tab.
    TabBuilder &Category(const std::string &title, const std::string &subtitle = "");

    // Boolean toggle switch (persisted automatically).
    TabBuilder &Toggle(const std::string &label, const std::string &desc, bool defaultVal,
                       std::function<void(bool)> onChange);

    // One-line KittyMemory ARM64/ARM hex patch toggle (patches on ON, restores original bytes on OFF).
    TabBuilder &PatchToggle(const std::string &label, const std::string &libName, uintptr_t offset,
                            const std::string &hexBytes, const std::string &desc = "");

    // Integer slider (persisted automatically).
    TabBuilder &SliderInt(const std::string &label, int minVal, int maxVal, int defaultVal,
                          const std::string &format, std::function<void(int)> onChange);

    // Float slider (persisted automatically).
    TabBuilder &SliderFloat(const std::string &label, float minVal, float maxVal, float defaultVal,
                            const std::string &format, std::function<void(float)> onChange);

    // One-shot action button.
    TabBuilder &Button(const std::string &label, const std::string &desc, std::function<void()> onClick,
                       bool prominent = false);

    // Dropdown selector (persisted automatically).
    TabBuilder &Combo(const std::string &label, std::vector<std::string> items, int defaultIdx,
                      std::function<void(int)> onChange);

    // Numeric input field with Apply button (persisted automatically).
    TabBuilder &InputInt(const std::string &label, int defaultVal, std::function<void(int)> onApply);

    // Text input field with Apply button (uses native Android keyboard; persisted automatically).
    TabBuilder &InputText(const std::string &label, const std::string &defaultVal,
                          std::function<void(const std::string &)> onApply);

    // Static informational text row.
    TabBuilder &Info(const std::string &text);

private:
    size_t tabIndex_;
};

// Configure menu title, pill badge, target library, and default look.
void SetConfig(const MenuConfig &cfg);
const MenuConfig &GetConfig();

// Create or append to a named navigation tab.
TabBuilder Tab(const std::string &title, WorkspaceUI::Icon icon = WorkspaceUI::Icon::Tools);

// Register an always-on overlay renderer (runs every frame even while the menu is minimized).
void OnOverlayDraw(std::function<void(ImDrawList *draw, ImVec2 screen)> callback);

// Register a one-shot callback invoked on a background thread once targetLib is mapped (or immediately if empty).
void OnGameReady(std::function<void(uintptr_t libBase)> callback);

// Show a semantic toast notification in the bottom-right corner.
void Notify(const std::string &title, const std::string &message, bool ok = true, float durationSec = 3.5f);

// Persistence & lifecycle helpers (called automatically by Main.cpp; also callable from Features.cpp).
void SaveConfig();
void LoadConfig();
void ResetAllFeatures();

// Internal engine entry points used by Main.cpp.
void BootInit();
void OnImGuiSetup();
void DrawFrame();
uintptr_t TargetLibBase();

} // namespace ModMenu

// User-defined hook in jni/Features.cpp — declare all your tabs & cheats here!
void RegisterFeatures();
