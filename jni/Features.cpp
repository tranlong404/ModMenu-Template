// jni/Features.cpp
// ============================================================================
//  LibTool Mod Menu Template — EDIT THIS FILE TO ADD YOUR GAME FEATURES!
//  Everything else (Apple HIG ImGui UI, floating pill, themes, touch/mouse,
//  native keyboard, config save/load, SurfaceOverlay + EGL fallback) is
//  handled automatically by ModMenu.
// ============================================================================

#include "ModMenu/MemoryHelper.h"
#include "ModMenu/ModMenu.h"

#include <atomic>
#include <cstdio>
#include <string>

namespace {

// --- Feature state shared between UI and hooks ---
std::atomic<bool> gGodMode{false};
std::atomic<bool> gOneHitKill{false};
std::atomic<float> gMoveSpeed{1.0f};
std::atomic<int> gDamageMult{1};
std::atomic<int> gTargetMode{0};
std::atomic<bool> gShowCrosshair{false};
std::atomic<bool> gShowHudBadge{false};
std::atomic<float> gCrosshairSize{14.0f};
std::string gCustomTag = "MOD";

// --- Example Dobby hook (uncomment & replace offset from your game dump) ---
// MOD_HOOK(float, GetMoveSpeed, void *instance) {
//     float orig = orig_GetMoveSpeed(instance);
//     return orig * gMoveSpeed.load();
// }

} // namespace

void RegisterFeatures() {
    // 1. Menu branding & target library
    ModMenu::SetConfig({
        .title = "My Mod Menu",
        .subtitle = "v1.0.0 · ARM64",
        .pillText = "MOD",
        .targetLib = "libil2cpp.so", // Set to "" for non-Unity apps or instant startup
        .defaultTheme = 0,           // 0 = Apple Dark, 1 = Apple Light, 2 = Ocean Blue, 3 = Amethyst Purple
        .defaultScaleIndex = 3,      // 3 = 1.0x
        .startOpen = true,
    });

    // 2. Combat tab
    auto combat = ModMenu::Tab("Combat", WorkspaceUI::Icon::Tools);
    combat
        .Category("PLAYER", "Live toggles & multipliers")
        .Toggle("God Mode", "Ignore all incoming damage", false, [](bool on) {
            gGodMode.store(on);
            ModMenu::Notify("Combat", on ? "God Mode enabled" : "God Mode disabled", on);
        })
        .Toggle("One-Hit Kill", "Deal lethal damage on hit", false, [](bool on) {
            gOneHitKill.store(on);
        })
        .SliderInt("Damage Multiplier", 1, 100, 1, "%dx", [](int v) {
            gDamageMult.store(v);
        })
        .SliderFloat("Move Speed", 1.0f, 5.0f, 1.0f, "%.2fx", [](float v) {
            gMoveSpeed.store(v);
        })
        .Combo("Aim Priority", {"Nearest", "Lowest HP", "Crosshair"}, 0, [](int idx) {
            gTargetMode.store(idx);
        });

    combat
        .Category("HEX PATCHES", "One-line KittyMemory ARM64 patches")
        .Info("Replace 0x0 with your real RVA from libil2cpp.so / dump.cs before toggling.")
        // Example ARM64 "return true" patch: mov w0, #1; ret -> 20 00 80 52 C0 03 5F D6
        .PatchToggle("Unlock VIP (Demo Offset)", "libil2cpp.so", 0x0, "20 00 80 52 C0 03 5F D6",
                     "Demo entry — set a real non-zero offset in jni/Features.cpp");

    // 3. Economy / Utility tab
    auto misc = ModMenu::Tab("Misc", WorkspaceUI::Icon::Menu);
    misc
        .Category("ACTIONS", "One-shot triggers & inputs")
        .Button("Add +10,000 Gold", "Triggers reward hook on next tick", []() {
            ModMenu::Notify("Misc", "Add +10,000 Gold triggered!", true);
        }, true)
        .InputInt("Set Player Level", 50, [](int level) {
            ModMenu::Notify("Misc", "Target level set to " + std::to_string(level), true);
        })
        .InputText("Custom Player Tag", gCustomTag, [](const std::string &tag) {
            gCustomTag = tag;
            ModMenu::Notify("Misc", "Tag updated: " + tag, true);
        });

    // 4. Visuals / Overlay tab
    auto visuals = ModMenu::Tab("Visuals", WorkspaceUI::Icon::Explorer);
    visuals
        .Category("OVERLAY HUD", "Stays visible even when menu is hidden")
        .Toggle("Center Crosshair", "Draws a crisp centered aiming reticle", false, [](bool on) {
            gShowCrosshair.store(on);
        })
        .SliderFloat("Crosshair Size", 6.0f, 36.0f, 14.0f, "%.0f px", [](float v) {
            gCrosshairSize.store(v);
        })
        .Toggle("Corner FPS Badge", "Displays live overlay FPS and tag", false, [](bool on) {
            gShowHudBadge.store(on);
        });

    // 5. Always-on overlay drawing (ESP, crosshair, watermark)
    ModMenu::OnOverlayDraw([](ImDrawList *draw, ImVec2 screen) {
        if (gShowCrosshair.load()) {
            const ImVec2 c{screen.x * 0.5f, screen.y * 0.5f};
            const float r = gCrosshairSize.load();
            const float gap = 4.0f;
            const ImU32 col = IM_COL32(72, 214, 155, 235);
            const ImU32 outline = IM_COL32(0, 0, 0, 170);
            auto arm = [&](ImVec2 a, ImVec2 b) {
                draw->AddLine(a, b, outline, 4.0f);
                draw->AddLine(a, b, col, 2.0f);
            };
            arm({c.x - r, c.y}, {c.x - gap, c.y});
            arm({c.x + gap, c.y}, {c.x + r, c.y});
            arm({c.x, c.y - r}, {c.x, c.y - gap});
            arm({c.x, c.y + gap}, {c.x, c.y + r});
            draw->AddCircleFilled(c, 2.0f, col);
        }
        if (gShowHudBadge.load()) {
            char hud[96];
            std::snprintf(hud, sizeof(hud), "%s · %.0f FPS", gCustomTag.c_str(), ImGui::GetIO().Framerate);
            const ImVec2 sz = ImGui::CalcTextSize(hud);
            const ImVec2 pos{screen.x - sz.x - 28.f, 16.f};
            draw->AddRectFilled({pos.x - 10.f, pos.y - 5.f}, {pos.x + sz.x + 10.f, pos.y + sz.y + 5.f},
                                IM_COL32(16, 20, 28, 195), 8.f);
            draw->AddText(pos, IM_COL32(230, 238, 250, 255), hud);
        }
    });

    // 6. Runs once on a background thread after targetLib maps
    ModMenu::OnGameReady([](uintptr_t libBase) {
        if (!libBase) return;
        // Example Dobby hook installation:
        // ModMemory::HookAddress(libBase + 0xABCDEF,
        //                        reinterpret_cast<void *>(hook_GetMoveSpeed),
        //                        reinterpret_cast<void **>(&orig_GetMoveSpeed));
    });
}
