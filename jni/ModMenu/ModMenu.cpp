// jni/ModMenu/ModMenu.cpp
#include "ModMenu.h"
#include "Keyboard.h"
#include "MemoryHelper.h"
#include "Overlay.h"
#include "Includes/Logger.h"
#include "json/json.hpp"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <thread>
#include <unistd.h>

namespace ModMenu {
namespace {

using Json = nlohmann::json;

enum class ItemKind {
    Toggle,
    PatchToggle,
    SliderInt,
    SliderFloat,
    Button,
    Combo,
    InputInt,
    InputText,
    Info
};

struct FeatureItem {
    std::string id;
    ItemKind kind = ItemKind::Info;
    std::string label;
    std::string desc;
    std::string format;

    bool boolVal = false, boolDefault = false;
    int intVal = 0, intDefault = 0, intMin = 0, intMax = 100;
    float floatVal = 0.f, floatDefault = 0.f, floatMin = 0.f, floatMax = 10.f;
    std::string strVal, strDefault;
    std::vector<std::string> comboItems;
    bool prominent = false;

    // PatchToggle metadata
    std::string patchLib;
    uintptr_t patchOffset = 0;
    std::string patchHex;

    std::function<void(bool)> onBool;
    std::function<void(int)> onInt;
    std::function<void(float)> onFloat;
    std::function<void()> onClick;
    std::function<void(const std::string &)> onStr;
};

struct CategorySection {
    std::string title;
    std::string subtitle;
    std::vector<FeatureItem> items;
};

struct TabData {
    std::string title;
    WorkspaceUI::Icon icon = WorkspaceUI::Icon::Tools;
    std::vector<CategorySection> sections;
};

struct Toast {
    std::string title;
    std::string message;
    bool ok = true;
    std::chrono::steady_clock::time_point expires;
};

constexpr std::array<float, 8> kScaleFactors = {0.75f, 0.85f, 0.95f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f};
constexpr const char *kScaleLabels[] = {"0.75x", "0.85x", "0.95x", "1.0x", "1.15x", "1.3x", "1.5x", "1.75x"};
constexpr const char *kThemeLabels[] = {
    "Apple Dark", "Apple Light", "Ocean Blue", "Amethyst Purple", "Classic Dark", "Classic Light", "ImGui Classic"
};

struct EngineState {
    MenuConfig config;
    std::vector<TabData> tabs;
    std::vector<std::function<void(ImDrawList *, ImVec2)>> overlayCallbacks;
    std::vector<std::function<void(uintptr_t)>> readyCallbacks;

    std::mutex toastMutex;
    std::vector<Toast> toasts;

    std::atomic<uintptr_t> targetBase{0};
    std::atomic<bool> gameReady{false};
    std::atomic<bool> booted{false};

    // UI state
    int selectedTheme = 0;
    int selectedScale = 3;
    bool applyThemePending = true;
    bool applyScalePending = true;
    bool showToggler = true;
    bool transparentToggler = false;
    bool increaseContrast = false;
    bool reduceTransparency = false;
    bool collapsed = false;
    bool resizableWindow = false;
    bool fullScreen = false;
    bool resetWindow = true;
    bool showActiveOnly = false;
    std::string searchQuery;
    ImVec2 windowPos{40.f, 40.f};
    ImVec2 lastSize{0.f, 0.f};
    ImVec2 lastPos{40.f, 40.f};
    ImGuiStyle baseStyle{};
    bool hasBaseStyle = false;
};

EngineState &S() {
    static auto *s = new EngineState;
    return *s;
}

std::string Lower(std::string t) {
    for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t;
}

CategorySection &CurrentSection(size_t tabIndex) {
    auto &tab = S().tabs[tabIndex];
    if (tab.sections.empty()) {
        tab.sections.push_back({"GENERAL", "", {}});
    }
    return tab.sections.back();
}

std::string MakeItemId(size_t tabIdx, const std::string &label) {
    return S().tabs[tabIdx].title + "::" + CurrentSection(tabIdx).title + "::" + label;
}

bool ItemMatchesFilter(const FeatureItem &it, const std::string &needle, bool activeOnly) {
    if (activeOnly) {
        bool isActive = (it.kind == ItemKind::Toggle || it.kind == ItemKind::PatchToggle) && it.boolVal;
        if (!isActive) return false;
    }
    if (needle.empty()) return true;
    if (Lower(it.label).find(needle) != std::string::npos) return true;
    if (!it.desc.empty() && Lower(it.desc).find(needle) != std::string::npos) return true;
    return false;
}

int CountActiveFeatures() {
    int n = 0;
    for (const auto &tab : S().tabs) {
        for (const auto &sec : tab.sections) {
            for (const auto &it : sec.items) {
                if ((it.kind == ItemKind::Toggle || it.kind == ItemKind::PatchToggle) && it.boolVal)
                    ++n;
            }
        }
    }
    return n;
}

void ApplyItemChange(FeatureItem &it) {
    if (it.kind == ItemKind::Toggle) {
        if (it.onBool) it.onBool(it.boolVal);
    } else if (it.kind == ItemKind::PatchToggle) {
        std::string msg;
        bool ok = ModMemory::PatchRegistry::Instance().SetHexPatch(
            it.id, it.patchLib, it.patchOffset, it.patchHex, it.boolVal, msg);
        if (!ok) it.boolVal = !it.boolVal; // revert switch if patch failed
        Notify(it.label, msg, ok, 3.0f);
    } else if (it.kind == ItemKind::SliderInt || it.kind == ItemKind::Combo || it.kind == ItemKind::InputInt) {
        if (it.onInt) it.onInt(it.intVal);
    } else if (it.kind == ItemKind::SliderFloat) {
        if (it.onFloat) it.onFloat(it.floatVal);
    } else if (it.kind == ItemKind::InputText) {
        if (it.onStr) it.onStr(it.strVal);
    }
}

void DrawToasts(ImVec2 screen) {
    std::vector<Toast> active;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(S().toastMutex);
        auto &v = S().toasts;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Toast &t) { return now >= t.expires; }), v.end());
        active = v;
    }
    if (active.empty()) return;

    const auto &c = WorkspaceUI::Colors();
    const float margin = 18.f;
    const float width = std::min(screen.x * 0.42f, 380.f);
    float yBottom = screen.y - margin;

    for (auto it = active.rbegin(); it != active.rend(); ++it) {
        const float textW = std::max(80.f, width - 34.f);
        const ImVec2 tSz = ImGui::CalcTextSize(it->title.c_str(), nullptr, false, textW);
        const ImVec2 mSz = ImGui::CalcTextSize(it->message.c_str(), nullptr, false, textW);
        const float cardH = std::max(56.f, tSz.y + mSz.y + 26.f);
        const float yTop = yBottom - cardH;
        if (yTop < margin) break;

        char winId[48];
        std::snprintf(winId, sizeof(winId), "##ModToast_%zu", static_cast<size_t>(it - active.rbegin()));
        ImGui::SetNextWindowPos({screen.x - width - margin, yTop}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, cardH}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f, 10.f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, c.childBg);
        ImGui::PushStyleColor(ImGuiCol_Border, it->ok ? c.ok : c.danger);
        if (ImGui::Begin(winId, nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings)) {
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRectFilled(
                {wp.x, wp.y + 4.f}, {wp.x + 4.f, wp.y + ws.y - 4.f},
                ImGui::GetColorU32(it->ok ? c.ok : c.danger), 2.f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.f);
            ImGui::TextColored(it->ok ? c.ok : c.danger, "%s", it->title.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.f);
            ImGui::PushTextWrapPos(width - 18.f);
            ImGui::TextUnformatted(it->message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        yBottom = yTop - 8.f;
    }
}

void DrawFloatingPill(ImVec2 screen) {
    if (!S().showToggler) return;
    static ImVec2 pillPos{16.f, 44.f};
    static bool posInit = false;
    if (!posInit) {
        pillPos = {16.f, std::max(28.f, screen.y * 0.08f)};
        posInit = true;
    }

    const bool open = ModOverlay::Flags().menuOpen.load(std::memory_order_acquire);
    const int activeCount = CountActiveFeatures();
    char label[64];
    if (activeCount > 0)
        std::snprintf(label, sizeof(label), "%s · %d · %s", S().config.pillText.c_str(), activeCount, open ? "Hide" : "Open");
    else
        std::snprintf(label, sizeof(label), "%s · %s", S().config.pillText.c_str(), open ? "Hide" : "Open");

    const ImVec2 txtSz = ImGui::CalcTextSize(label);
    const float dotSpace = 14.f;
    const ImVec2 pillSz{txtSz.x + dotSpace + 28.f, std::max(34.f, txtSz.y + 14.f)};

    pillPos.x = std::clamp(pillPos.x, 4.f, std::max(4.f, screen.x - pillSz.x - 4.f));
    pillPos.y = std::clamp(pillPos.y, 4.f, std::max(4.f, screen.y - pillSz.y - 4.f));

    ImGui::SetNextWindowPos(pillPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(pillSz, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, pillSz.y * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S().transparentToggler ? 0.f : 1.f);
    ImVec4 bg = WorkspaceUI::Colors().childBg;
    bg.w = S().transparentToggler ? 0.0f : (S().reduceTransparency ? 0.98f : 0.88f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);

    if (ImGui::Begin("##ModMenuPill", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::InvisibleButton("##ModPillBtn", pillSz);
        static bool dragging = false;
        static ImVec2 dragStart{};
        if (ImGui::IsItemActivated()) {
            dragging = false;
            dragStart = ImGui::GetIO().MousePos;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
            ImVec2 cur = ImGui::GetIO().MousePos;
            if (std::hypot(cur.x - dragStart.x, cur.y - dragStart.y) > 8.f) dragging = true;
            if (dragging) {
                pillPos.x += ImGui::GetIO().MouseDelta.x;
                pillPos.y += ImGui::GetIO().MouseDelta.y;
            }
        }
        if (ImGui::IsItemDeactivated() && !dragging) {
            bool next = !open;
            ModOverlay::Flags().menuOpen.store(next, std::memory_order_release);
        }
        if (!S().transparentToggler) {
            auto *dl = ImGui::GetWindowDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec4 dotCol = activeCount > 0 ? WorkspaceUI::Colors().ok
                : (S().gameReady.load() ? WorkspaceUI::Colors().brand : WorkspaceUI::Colors().warn);
            dl->AddCircleFilled({wp.x + 15.f, wp.y + pillSz.y * 0.5f}, 4.5f, ImGui::GetColorU32(dotCol));
            dl->AddText({wp.x + 15.f + dotSpace, wp.y + (pillSz.y - txtSz.y) * 0.5f},
                        ImGui::GetColorU32(ImGuiCol_Text), label);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void DrawFeatureControl(FeatureItem &it, size_t idx) {
    ImGui::PushID(static_cast<int>(idx));
    switch (it.kind) {
    case ItemKind::Toggle:
    case ItemKind::PatchToggle: {
        if (WorkspaceUI::ToggleSwitch("sw", it.label.c_str(), &it.boolVal)) {
            ApplyItemChange(it);
        }
        if (!it.desc.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, WorkspaceUI::Colors().dim);
            ImGui::TextWrapped("%s", it.desc.c_str());
            ImGui::PopStyleColor();
        }
        break;
    }
    case ItemKind::SliderInt: {
        ImGui::TextUnformatted(it.label.c_str());
        ImGui::SetNextItemWidth(-1.f);
        const char *fmt = it.format.empty() ? "%d" : it.format.c_str();
        if (ImGui::SliderInt("##si", &it.intVal, it.intMin, it.intMax, fmt)) {
            ApplyItemChange(it);
        }
        break;
    }
    case ItemKind::SliderFloat: {
        ImGui::TextUnformatted(it.label.c_str());
        ImGui::SetNextItemWidth(-1.f);
        const char *fmt = it.format.empty() ? "%.2f" : it.format.c_str();
        if (ImGui::SliderFloat("##sf", &it.floatVal, it.floatMin, it.floatMax, fmt)) {
            ApplyItemChange(it);
        }
        break;
    }
    case ItemKind::Button: {
        if (it.prominent) WorkspaceUI::PushProminentButton();
        if (ImGui::Button(it.label.c_str(), {-1.f, 0.f})) {
            if (it.onClick) it.onClick();
        }
        if (it.prominent) WorkspaceUI::PopProminentButton();
        if (!it.desc.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, WorkspaceUI::Colors().dim);
            ImGui::TextWrapped("%s", it.desc.c_str());
            ImGui::PopStyleColor();
        }
        break;
    }
    case ItemKind::Combo: {
        ImGui::TextUnformatted(it.label.c_str());
        ImGui::SetNextItemWidth(-1.f);
        const char *preview = (it.intVal >= 0 && it.intVal < static_cast<int>(it.comboItems.size()))
            ? it.comboItems[it.intVal].c_str() : "";
        if (ImGui::BeginCombo("##cb", preview)) {
            for (int i = 0; i < static_cast<int>(it.comboItems.size()); ++i) {
                bool sel = (it.intVal == i);
                if (ImGui::Selectable(it.comboItems[i].c_str(), sel)) {
                    it.intVal = i;
                    ApplyItemChange(it);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        break;
    }
    case ItemKind::InputInt: {
        ImGui::TextUnformatted(it.label.c_str());
        const float btnW = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2.5f;
        ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x));
        ModKeyboard::InputInt("##ii", &it.intVal, -2147483647, 2147483647);
        ImGui::SameLine();
        WorkspaceUI::PushProminentButton();
        if (ImGui::Button("Apply##iib", {btnW, 0.f})) {
            ApplyItemChange(it);
        }
        WorkspaceUI::PopProminentButton();
        break;
    }
    case ItemKind::InputText: {
        ImGui::TextUnformatted(it.label.c_str());
        const float btnW = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2.5f;
        ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x));
        ModKeyboard::InputString("##it", "Enter text...", it.strVal);
        ImGui::SameLine();
        WorkspaceUI::PushProminentButton();
        if (ImGui::Button("Apply##itb", {btnW, 0.f})) {
            ApplyItemChange(it);
        }
        WorkspaceUI::PopProminentButton();
        break;
    }
    case ItemKind::Info: {
        ImGui::PushStyleColor(ImGuiCol_Text, WorkspaceUI::Colors().dim);
        ImGui::TextWrapped("%s", it.label.c_str());
        ImGui::PopStyleColor();
        break;
    }
    }
    ImGui::PopID();
}

void DrawFeatureTab(TabData &tab) {
    ImGui::BeginChild("FeatureTabScroll", {0.f, 0.f}, ImGuiChildFlags_None);
    const std::string needle = Lower(S().searchQuery);
    const bool activeOnly = S().showActiveOnly;
    const bool twoCol = WorkspaceUI::UseSplit(ImGui::GetContentRegionAvail().x) && tab.sections.size() > 1;

    std::vector<size_t> visibleSections;
    for (size_t si = 0; si < tab.sections.size(); ++si) {
        bool any = false;
        for (const auto &it : tab.sections[si].items) {
            if (ItemMatchesFilter(it, needle, activeOnly)) { any = true; break; }
        }
        if (any) visibleSections.push_back(si);
    }

    if (visibleSections.empty()) {
        WorkspaceUI::BeginCard("EmptyFilterCard", {0.f, 0.f}, 0, ImGuiChildFlags_AutoResizeY);
        ImGui::TextDisabled(activeOnly ? "No active toggles in this tab." : "No features matching your search.");
        WorkspaceUI::EndCard();
        ImGui::EndChild();
        return;
    }

    auto drawSectionCard = [&](size_t si) {
        auto &sec = tab.sections[si];
        char cardId[64];
        std::snprintf(cardId, sizeof(cardId), "SecCard_%zu", si);
        WorkspaceUI::BeginCard(cardId, {0.f, 0.f}, 0, ImGuiChildFlags_AutoResizeY);
        WorkspaceUI::Heading(sec.title.c_str(), sec.subtitle.empty() ? nullptr : sec.subtitle.c_str());
        bool first = true;
        for (size_t ii = 0; ii < sec.items.size(); ++ii) {
            auto &it = sec.items[ii];
            if (!ItemMatchesFilter(it, needle, activeOnly)) continue;
            if (!first) ImGui::Spacing();
            first = false;
            DrawFeatureControl(it, ii);
        }
        WorkspaceUI::EndCard();
    };

    if (twoCol && ImGui::BeginTable("TabGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        for (size_t i = 0; i < visibleSections.size(); i += 2) {
            drawSectionCard(visibleSections[i]);
            ImGui::Spacing();
        }
        ImGui::TableNextColumn();
        for (size_t i = 1; i < visibleSections.size(); i += 2) {
            drawSectionCard(visibleSections[i]);
            ImGui::Spacing();
        }
        ImGui::EndTable();
    } else {
        for (size_t si : visibleSections) {
            drawSectionCard(si);
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
}

void DrawSettingsTab() {
    ImGui::BeginChild("SettingsScroll", {0.f, 0.f}, ImGuiChildFlags_None);
    const bool twoCol = WorkspaceUI::UseSplit(ImGui::GetContentRegionAvail().x);

    auto drawAppearanceCard = [&]() {
        WorkspaceUI::BeginCard("AppearanceCard", {0.f, 0.f}, 0, ImGuiChildFlags_AutoResizeY);
        WorkspaceUI::Heading("APPEARANCE", "Apple HIG ImGui skin");
        if (ImGui::BeginTable("AppTbl", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.2f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Theme");
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1.f);
            if (ImGui::Combo("##Theme", &S().selectedTheme, kThemeLabels, IM_ARRAYSIZE(kThemeLabels))) {
                S().applyThemePending = true;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Scale");
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1.f);
            if (ImGui::Combo("##Scale", &S().selectedScale, kScaleLabels, IM_ARRAYSIZE(kScaleLabels))) {
                S().applyScalePending = true;
            }
            ImGui::EndTable();
        }
        WorkspaceUI::ToggleSwitch("swShowPill", "Floating Pill Toggler", &S().showToggler);
        WorkspaceUI::ToggleSwitch("swTranspPill", "Transparent Toggler", &S().transparentToggler, !S().showToggler);
        bool pass = ModOverlay::Flags().passthrough.load();
        if (WorkspaceUI::ToggleSwitch("swPass", "Touch Passthrough", &pass))
            ModOverlay::Flags().passthrough.store(pass, std::memory_order_release);
        if (WorkspaceUI::ToggleSwitch("swContrast", "Increase Contrast", &S().increaseContrast))
            S().applyThemePending = true;
        if (WorkspaceUI::ToggleSwitch("swReduceT", "Reduce Transparency", &S().reduceTransparency))
            S().applyThemePending = true;
        int fps = ModOverlay::Flags().maxFps.load();
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::SliderInt("##MaxFps", &fps, 15, 120, "Overlay FPS: %d"))
            ModOverlay::Flags().maxFps.store(fps, std::memory_order_release);
        WorkspaceUI::EndCard();
    };

    auto drawRuntimeCard = [&]() {
        WorkspaceUI::BeginCard("RuntimeCard", {0.f, 0.f}, 0, ImGuiChildFlags_AutoResizeY);
        WorkspaceUI::Heading("CONFIG & RUNTIME");
        WorkspaceUI::PushProminentButton();
        if (ImGui::Button("Save Config", {ImGui::GetContentRegionAvail().x * 0.5f, 0.f})) {
            SaveConfig();
            Notify("Config", "Saved feature states & theme.", true, 2.5f);
        }
        WorkspaceUI::PopProminentButton();
        ImGui::SameLine();
        WorkspaceUI::PushDestructiveButton();
        if (ImGui::Button("Reset All", {-1.f, 0.f})) {
            ResetAllFeatures();
            Notify("Config", "All toggles & patches reset.", true, 2.5f);
        }
        WorkspaceUI::PopDestructiveButton();

        WorkspaceUI::SectionLabel("RENDERING BACKEND");
        ImGui::TextUnformatted("Active:");
        ImGui::SameLine();
        WorkspaceUI::StatusChip(ModOverlay::IsSurfaceActive() ? "Surface Overlay" : "EGL Hook",
                                WorkspaceUI::ChipTone::Ok);
        static int backendChoice = -1;
        if (backendChoice < 0) backendChoice = ModOverlay::IsSurfaceActive() ? 0 : 1;
        ImGui::SetNextItemWidth(-1.f);
        ImGui::Combo("##Backend", &backendChoice, "Surface Overlay (Recommended)\0EGL SwapBuffers Hook\0");
        if (ImGui::Button("Save Backend (Restart App)", {-1.f, 0.f})) {
            auto dir = ModOverlay::PrivateDirectory();
            if (!dir.empty()) {
                std::ofstream out(dir + "/render-backend", std::ios::trunc);
                out << (backendChoice == 1 ? "egl" : "surface");
                Notify("Backend", out ? "Saved. Reopen app to apply." : "Failed to save backend.", bool(out), 3.5f);
            }
        }

        WorkspaceUI::SectionLabel("TARGET INFO");
        static std::string pkg = ModOverlay::PackageName();
        static std::string dev = []() {
            char rel[PROP_VALUE_MAX]{}, abi[PROP_VALUE_MAX]{};
            __system_property_get("ro.build.version.release", rel);
            __system_property_get("ro.product.cpu.abi", abi);
            return std::string("Android ") + (rel[0] ? rel : "?") + " / " + (abi[0] ? abi : "arm64-v8a");
        }();
        ImGui::TextColored(WorkspaceUI::Colors().dim, "Package: %s", pkg.c_str());
        ImGui::TextColored(WorkspaceUI::Colors().dim, "Device: %s", dev.c_str());
        if (!S().config.targetLib.empty()) {
            uintptr_t base = S().targetBase.load();
            ImGui::TextColored(WorkspaceUI::Colors().dim, "%s: 0x%llX",
                               S().config.targetLib.c_str(), static_cast<unsigned long long>(base));
        }
        WorkspaceUI::EndCard();
    };

    if (twoCol && ImGui::BeginTable("SettingsGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); drawAppearanceCard();
        ImGui::TableNextColumn(); drawRuntimeCard();
        ImGui::EndTable();
    } else {
        drawAppearanceCard();
        ImGui::Spacing();
        drawRuntimeCard();
    }
    ImGui::EndChild();
}

} // namespace

// --- TabBuilder implementation ---

TabBuilder &TabBuilder::Category(const std::string &title, const std::string &subtitle) {
    S().tabs[tabIndex_].sections.push_back({title, subtitle, {}});
    return *this;
}

TabBuilder &TabBuilder::Toggle(const std::string &label, const std::string &desc, bool defaultVal,
                               std::function<void(bool)> onChange) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::Toggle;
    it.label = label;
    it.desc = desc;
    it.boolVal = it.boolDefault = defaultVal;
    it.onBool = std::move(onChange);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::PatchToggle(const std::string &label, const std::string &libName, uintptr_t offset,
                                    const std::string &hexBytes, const std::string &desc) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::PatchToggle;
    it.label = label;
    it.desc = desc.empty() ? (libName + " + 0x" + [offset]() {
        char b[32]; std::snprintf(b, sizeof(b), "%llX", static_cast<unsigned long long>(offset)); return std::string(b);
    }() + " -> " + hexBytes) : desc;
    it.patchLib = libName;
    it.patchOffset = offset;
    it.patchHex = hexBytes;
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::SliderInt(const std::string &label, int minVal, int maxVal, int defaultVal,
                                  const std::string &format, std::function<void(int)> onChange) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::SliderInt;
    it.label = label;
    it.intMin = minVal;
    it.intMax = maxVal;
    it.intVal = it.intDefault = std::clamp(defaultVal, minVal, maxVal);
    it.format = format;
    it.onInt = std::move(onChange);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::SliderFloat(const std::string &label, float minVal, float maxVal, float defaultVal,
                                    const std::string &format, std::function<void(float)> onChange) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::SliderFloat;
    it.label = label;
    it.floatMin = minVal;
    it.floatMax = maxVal;
    it.floatVal = it.floatDefault = std::clamp(defaultVal, minVal, maxVal);
    it.format = format;
    it.onFloat = std::move(onChange);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::Button(const std::string &label, const std::string &desc, std::function<void()> onClick,
                               bool prominent) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::Button;
    it.label = label;
    it.desc = desc;
    it.prominent = prominent;
    it.onClick = std::move(onClick);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::Combo(const std::string &label, std::vector<std::string> items, int defaultIdx,
                              std::function<void(int)> onChange) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::Combo;
    it.label = label;
    it.comboItems = std::move(items);
    it.intVal = it.intDefault = it.comboItems.empty() ? 0
        : std::clamp(defaultIdx, 0, static_cast<int>(it.comboItems.size()) - 1);
    it.onInt = std::move(onChange);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::InputInt(const std::string &label, int defaultVal, std::function<void(int)> onApply) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::InputInt;
    it.label = label;
    it.intVal = it.intDefault = defaultVal;
    it.onInt = std::move(onApply);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::InputText(const std::string &label, const std::string &defaultVal,
                                  std::function<void(const std::string &)> onApply) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, label);
    it.kind = ItemKind::InputText;
    it.label = label;
    it.strVal = it.strDefault = defaultVal;
    it.onStr = std::move(onApply);
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

TabBuilder &TabBuilder::Info(const std::string &text) {
    FeatureItem it;
    it.id = MakeItemId(tabIndex_, text);
    it.kind = ItemKind::Info;
    it.label = text;
    CurrentSection(tabIndex_).items.push_back(std::move(it));
    return *this;
}

// --- Public ModMenu API ---

void SetConfig(const MenuConfig &cfg) {
    S().config = cfg;
    S().selectedTheme = std::clamp(cfg.defaultTheme, 0, 6);
    S().selectedScale = std::clamp(cfg.defaultScaleIndex, 0, static_cast<int>(kScaleFactors.size()) - 1);
    ModOverlay::Flags().menuOpen.store(cfg.startOpen, std::memory_order_release);
}

const MenuConfig &GetConfig() { return S().config; }

TabBuilder Tab(const std::string &title, WorkspaceUI::Icon icon) {
    for (size_t i = 0; i < S().tabs.size(); ++i) {
        if (S().tabs[i].title == title) return TabBuilder(i);
    }
    S().tabs.push_back({title, icon, {}});
    return TabBuilder(S().tabs.size() - 1);
}

void OnOverlayDraw(std::function<void(ImDrawList *, ImVec2)> callback) {
    if (callback) S().overlayCallbacks.push_back(std::move(callback));
}

void OnGameReady(std::function<void(uintptr_t)> callback) {
    if (callback) S().readyCallbacks.push_back(std::move(callback));
}

void Notify(const std::string &title, const std::string &message, bool ok, float durationSec) {
    std::lock_guard<std::mutex> lock(S().toastMutex);
    if (S().toasts.size() >= 6) S().toasts.erase(S().toasts.begin());
    auto dur = std::chrono::milliseconds(static_cast<int>(std::clamp(durationSec, 1.f, 15.f) * 1000.f));
    S().toasts.push_back({title, message, ok, std::chrono::steady_clock::now() + dur});
}

void SaveConfig() {
    auto dir = ModOverlay::PrivateDirectory();
    if (dir.empty()) return;
    Json j;
    j["ui"] = {
        {"theme", S().selectedTheme},
        {"scale", S().selectedScale},
        {"showToggler", S().showToggler},
        {"transparentToggler", S().transparentToggler},
        {"increaseContrast", S().increaseContrast},
        {"reduceTransparency", S().reduceTransparency},
        {"passthrough", ModOverlay::Flags().passthrough.load()},
        {"maxFps", ModOverlay::Flags().maxFps.load()}
    };
    Json feats = Json::object();
    for (const auto &tab : S().tabs) {
        for (const auto &sec : tab.sections) {
            for (const auto &it : sec.items) {
                if (it.kind == ItemKind::Toggle || it.kind == ItemKind::PatchToggle) feats[it.id] = it.boolVal;
                else if (it.kind == ItemKind::SliderInt || it.kind == ItemKind::Combo || it.kind == ItemKind::InputInt)
                    feats[it.id] = it.intVal;
                else if (it.kind == ItemKind::SliderFloat) feats[it.id] = it.floatVal;
                else if (it.kind == ItemKind::InputText) feats[it.id] = it.strVal;
            }
        }
    }
    j["features"] = feats;
    std::ofstream out(dir + "/modmenu_config.json", std::ios::trunc);
    if (out) out << j.dump(2);
}

void LoadConfig() {
    auto dir = ModOverlay::PrivateDirectory();
    if (dir.empty()) return;
    std::ifstream in(dir + "/modmenu_config.json");
    if (!in) return;
    try {
        Json j;
        in >> j;
        if (j.contains("ui") && j["ui"].is_object()) {
            const auto &u = j["ui"];
            S().selectedTheme = std::clamp(u.value("theme", S().selectedTheme), 0, 6);
            S().selectedScale = std::clamp(u.value("scale", S().selectedScale), 0, static_cast<int>(kScaleFactors.size()) - 1);
            S().showToggler = u.value("showToggler", S().showToggler);
            S().transparentToggler = u.value("transparentToggler", S().transparentToggler);
            S().increaseContrast = u.value("increaseContrast", S().increaseContrast);
            S().reduceTransparency = u.value("reduceTransparency", S().reduceTransparency);
            ModOverlay::Flags().passthrough.store(u.value("passthrough", true), std::memory_order_release);
            ModOverlay::Flags().maxFps.store(std::clamp(u.value("maxFps", 60), 15, 120), std::memory_order_release);
            S().applyThemePending = true;
            S().applyScalePending = true;
        }
        if (j.contains("features") && j["features"].is_object()) {
            const auto &f = j["features"];
            for (auto &tab : S().tabs) {
                for (auto &sec : tab.sections) {
                    for (auto &it : sec.items) {
                        if (!f.contains(it.id)) continue;
                        if ((it.kind == ItemKind::Toggle || it.kind == ItemKind::PatchToggle) && f[it.id].is_boolean()) {
                            it.boolVal = f[it.id].get<bool>();
                            if (it.boolVal) ApplyItemChange(it);
                        } else if ((it.kind == ItemKind::SliderInt || it.kind == ItemKind::Combo || it.kind == ItemKind::InputInt) && f[it.id].is_number_integer()) {
                            it.intVal = f[it.id].get<int>();
                            ApplyItemChange(it);
                        } else if (it.kind == ItemKind::SliderFloat && f[it.id].is_number()) {
                            it.floatVal = f[it.id].get<float>();
                            ApplyItemChange(it);
                        } else if (it.kind == ItemKind::InputText && f[it.id].is_string()) {
                            it.strVal = f[it.id].get<std::string>();
                            ApplyItemChange(it);
                        }
                    }
                }
            }
        }
    } catch (...) {}
}

void ResetAllFeatures() {
    ModMemory::PatchRegistry::Instance().RestoreAll();
    for (auto &tab : S().tabs) {
        for (auto &sec : tab.sections) {
            for (auto &it : sec.items) {
                if (it.kind == ItemKind::Toggle || it.kind == ItemKind::PatchToggle) {
                    if (it.boolVal != it.boolDefault) {
                        it.boolVal = it.boolDefault;
                        if (it.kind == ItemKind::Toggle && it.onBool) it.onBool(it.boolVal);
                    }
                } else if (it.kind == ItemKind::SliderInt || it.kind == ItemKind::Combo || it.kind == ItemKind::InputInt) {
                    it.intVal = it.intDefault;
                    if (it.onInt) it.onInt(it.intVal);
                } else if (it.kind == ItemKind::SliderFloat) {
                    it.floatVal = it.floatDefault;
                    if (it.onFloat) it.onFloat(it.floatVal);
                } else if (it.kind == ItemKind::InputText) {
                    it.strVal = it.strDefault;
                    if (it.onStr) it.onStr(it.strVal);
                }
            }
        }
    }
    SaveConfig();
}

uintptr_t TargetLibBase() { return S().targetBase.load(std::memory_order_acquire); }

void BootInit() {
    bool expected = false;
    if (!S().booted.compare_exchange_strong(expected, true)) return;
    RegisterFeatures();
    std::thread([]() {
        uintptr_t base = 0;
        if (!S().config.targetLib.empty()) {
            base = ModMemory::WaitForLibrary(S().config.targetLib.c_str(), 120);
        }
        S().targetBase.store(base, std::memory_order_release);
        S().gameReady.store(true, std::memory_order_release);
        LoadConfig();
        for (auto &cb : S().readyCallbacks) {
            if (cb) cb(base);
        }
        Notify(S().config.title,
               S().config.targetLib.empty() ? "Mod menu ready." : (S().config.targetLib + " attached."),
               true, 3.0f);
    }).detach();
}

void OnImGuiSetup() {
    WorkspaceUI::SetAccessibility(S().increaseContrast, S().reduceTransparency);
    WorkspaceUI::ApplyTheme(S().selectedTheme);
    S().baseStyle = ImGui::GetStyle();
    S().hasBaseStyle = true;
    S().applyThemePending = false;
    S().applyScalePending = true;
}

void DrawFrame() {
    auto &io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;

    ModKeyboard::Update();

    if (S().applyThemePending) {
        S().applyThemePending = false;
        WorkspaceUI::SetAccessibility(S().increaseContrast, S().reduceTransparency);
        WorkspaceUI::ApplyTheme(S().selectedTheme);
        S().baseStyle = ImGui::GetStyle();
        S().hasBaseStyle = true;
        S().applyScalePending = true;
    }
    if (S().applyScalePending && S().hasBaseStyle) {
        S().applyScalePending = false;
        const float factor = kScaleFactors[std::clamp(S().selectedScale, 0, static_cast<int>(kScaleFactors.size()) - 1)];
        ImGui::GetStyle() = S().baseStyle;
        ImGui::GetStyle().ScaleAllSizes(factor);
        io.FontGlobalScale = factor * 0.55f;
    }

    // 1. Always-visible ESP / HUD overlay callbacks (drawn even when menu is hidden!)
    if (!S().overlayCallbacks.empty()) {
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        for (auto &cb : S().overlayCallbacks) {
            if (cb) cb(bg, screen);
        }
    }

    // 2. Floating capsule toggler pill
    DrawFloatingPill(screen);

    // 3. Main Apple HIG Mod Menu window
    if (ModOverlay::Flags().menuOpen.load(std::memory_order_acquire)) {
        const float minW = std::min(screen.x * 0.92f, 340.f);
        const float defaultW = std::clamp(screen.x * (screen.x > screen.y ? 0.54f : 0.90f), minW, 760.f);
        const float defaultH = std::clamp(screen.y * (screen.x > screen.y ? 0.72f : 0.62f), 280.f, 640.f);
        const float barH = ImGui::GetFrameHeight() + 18.f;
        ImVec2 targetSz = S().collapsed ? ImVec2{defaultW, barH + 14.f}
            : (S().fullScreen ? ImVec2{screen.x - 16.f, screen.y - 16.f} : ImVec2{defaultW, defaultH});

        if (S().resetWindow) {
            S().resetWindow = false;
            S().windowPos = S().fullScreen ? ImVec2{8.f, 8.f}
                : ImVec2{(screen.x - targetSz.x) * 0.5f, (screen.y - targetSz.y) * 0.5f};
            WorkspaceUI::FitWindow(screen, S().windowPos, targetSz);
            ImGui::SetNextWindowPos(S().windowPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(targetSz, ImGuiCond_Always);
        } else if (S().collapsed) {
            ImGui::SetNextWindowSize(targetSz, ImGuiCond_Always);
        }

        ImGuiWindowFlags wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse;
        if (!S().resizableWindow || S().collapsed || S().fullScreen)
            wflags |= ImGuiWindowFlags_NoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
        if (ImGui::Begin("##ModMenuMain", nullptr, wflags)) {
            S().windowPos = ImGui::GetWindowPos();
            ImVec2 curSz = ImGui::GetWindowSize();

            // Custom Apple HIG title bar
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f, 8.f});
            ImGui::BeginChild("ModTitleBar", {0.f, barH}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            if (ImGui::SmallButton(S().collapsed ? "v##Col" : "^##Col")) {
                S().collapsed = !S().collapsed;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(S().config.title.c_str());
            if (!S().config.subtitle.empty()) {
                ImGui::SameLine();
                WorkspaceUI::StatusChip(S().config.subtitle.c_str(), WorkspaceUI::ChipTone::Neutral);
            }
            int activeCount = CountActiveFeatures();
            if (activeCount > 0) {
                ImGui::SameLine();
                char ab[32];
                std::snprintf(ab, sizeof(ab), "%d ON", activeCount);
                WorkspaceUI::StatusChip(ab, WorkspaceUI::ChipTone::Ok);
            }

            const char *modeLbl = S().fullScreen ? "Full" : (S().resizableWindow ? "Resize" : "Fixed");
            const float rightBtnsW = ImGui::CalcTextSize(modeLbl).x + ImGui::CalcTextSize("X").x + 48.f;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.f, ImGui::GetWindowWidth() - rightBtnsW));
            if (ImGui::SmallButton(modeLbl)) {
                if (S().fullScreen) {
                    S().fullScreen = false;
                    S().resizableWindow = false;
                    S().resetWindow = true;
                } else if (S().resizableWindow) {
                    S().fullScreen = true;
                    S().resetWindow = true;
                } else {
                    S().resizableWindow = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##HideMenu")) {
                ModOverlay::Flags().menuOpen.store(false, std::memory_order_release);
            }

            // Bottom hairline divider
            {
                ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
                ImGui::GetWindowDrawList()->AddLine(
                    {wp.x, wp.y + ws.y - 1.f}, {wp.x + ws.x, wp.y + ws.y - 1.f},
                    ImGui::GetColorU32(WorkspaceUI::Colors().border), 1.f);
            }

            // Drag title bar to move window
            static bool draggingTitle = false;
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) draggingTitle = false;
            if (!S().fullScreen) {
                if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    draggingTitle = true;
                if (draggingTitle && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
                    ImVec2 nextPos = {S().windowPos.x + io.MouseDelta.x, S().windowPos.y + io.MouseDelta.y};
                    WorkspaceUI::FitWindow(screen, nextPos, curSz);
                    ImGui::SetWindowPos("##ModMenuMain", nextPos);
                    S().windowPos = nextPos;
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();

            if (!S().collapsed) {
                ImGui::SetCursorPos({12.f, ImGui::GetCursorPosY() + 6.f});
                ImGui::BeginChild("ModBody", {curSz.x - 24.f, curSz.y - barH - 14.f}, ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                // Search & Active-only bar
                const float activeBtnW = ImGui::CalcTextSize("Active Only").x + 28.f;
                const float clearW = S().searchQuery.empty() ? 0.f : (ImGui::CalcTextSize("Clear").x + 22.f);
                ImGui::SetNextItemWidth(std::max(100.f, ImGui::GetContentRegionAvail().x - activeBtnW - clearW - 14.f));
                ModKeyboard::InputString("##ModSearch", "Search features...", S().searchQuery);
                if (!S().searchQuery.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear##ClrSearch")) S().searchQuery.clear();
                }
                ImGui::SameLine();
                WorkspaceUI::ToggleChip("Active Only", &S().showActiveOnly, false);
                ImGui::Spacing();

                if (ImGui::BeginTabBar("##ModTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                    for (auto &tab : S().tabs) {
                        if (WorkspaceUI::BeginNavTab(tab.title.c_str(), tab.icon)) {
                            DrawFeatureTab(tab);
                            ImGui::EndTabItem();
                        }
                    }
                    if (WorkspaceUI::BeginNavTab("Settings", WorkspaceUI::Icon::Settings)) {
                        DrawSettingsTab();
                        ImGui::EndTabItem();
                    }
                    WorkspaceUI::UpdateNavInk("##ModTabs");
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();

                if (S().resizableWindow && !S().fullScreen) {
                    WorkspaceUI::DrawResizeGrip(S().windowPos, curSz);
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // 4. Toast notifications
    DrawToasts(screen);
}

} // namespace ModMenu
