# LibTool Mod Menu Template (Apple HIG ImGui · ARM64)

A modern, lightweight Android Mod Menu template built on **LibTool's Apple HIG ImGui UI** (`WorkspaceUI` + `SurfaceOverlay` + `EGL` fallback + `KittyMemory` + `Dobby`).

- **One-file authoring**: edit only [`jni/Features.cpp`](jni/Features.cpp) to add tabs, categories, toggles, hex patches, sliders, dropdowns, inputs, buttons, and always-on ESP/HUD overlays.
- **Zero legacy Java Views / Manifest edits**: uses `InMemoryDexClassLoader` (`SurfaceOverlay`) with automatic `eglSwapBuffers` fallback — works on Android 9–16 and MuMu/emulators.
- **Dual deployment**:
  1. **Non-root APK injection**: drop `artifacts/arm64-v8a/libModMenu.so` into your APK and load `"ModMenu"` in smali.
  2. **Root Zygisk module**: flash `artifacts/ModMenu-Template-1.0.0.zip` in Magisk/KernelSU and pick target apps in KsuWebUI (supports both native ARM64 and x86_64 NativeBridge/Houdini!).

---

## Quick Start

### 1. Edit your features in `jni/Features.cpp`
```cpp
void RegisterFeatures() {
    ModMenu::SetConfig({
        .title = "My Mod Menu",
        .subtitle = "v1.0.0 · ARM64",
        .pillText = "MOD",
        .targetLib = "libil2cpp.so", // or "" for non-Unity apps
    });

    auto combat = ModMenu::Tab("Combat", WorkspaceUI::Icon::Tools);
    combat
        .Category("PLAYER", "Live toggles & multipliers")
        .Toggle("God Mode", "Ignore incoming damage", false, [](bool on) { gGodMode = on; })
        .PatchToggle("Unlock VIP", "libil2cpp.so", 0x123456, "20 00 80 52 C0 03 5F D6")
        .SliderFloat("Move Speed", 1.0f, 5.0f, 1.0f, "%.2fx", [](float v) { gSpeed = v; })
        .Button("Add +10,000 Gold", "One-shot trigger", []() { /* ... */ }, true);
}
```

### 2. Build
```powershell
# Build libModMenu.so (for APK injection & Zygisk)
.\build.ps1 -Jobs 4

# Package flashable Zygisk module ZIP (arm64-v8a + x86_64 loaders + KsuWebUI)
.\build-zygisk.ps1 -Jobs 4
```

### 3. Deploy

#### Option A — Non-root APK injection (1-line smali, no Manifest/DEX merge!)
1. Copy `artifacts/arm64-v8a/libModMenu.so` into the unpacked APK's `lib/arm64-v8a/libModMenu.so`.
2. In the app's launcher `Activity` `onCreate` (or `Application.onCreate`) smali, add:
   ```smali
   const-string v0, "ModMenu"
   invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
   ```
3. Repack & sign the APK. `JNI_OnLoad` automatically attaches the Apple HIG ImGui overlay.

#### Option B — Root Zygisk (no APK repack/resign)
1. Flash `artifacts/ModMenu-Template-1.0.0.zip` in Magisk or KernelSU.
2. Reboot, open KsuWebUI -> select target game -> Save -> Launch.

---

## Built-in UI Features (Zero Boilerplate)
- Draggable floating pill toggler (`MOD · Open / Hide`) with active-feature counter & **Transparent Toggler** mode
- Responsive 2-column cards on landscape/tablets, 1-column stack on portrait phones
- Global **Search bar** + **Active Only** filter chip
- 7 themes (`Apple Dark`, `Apple Light`, `Ocean Blue`, `Amethyst Purple`, `Classic Dark`, `Classic Light`, `ImGui Classic`) + Scale (`0.75x`–`1.75x`) + Reduce Transparency + Increase Contrast
- Native Android keyboard input + Touch Passthrough + Mouse wheel & drag-scroll
- Automatic JSON config save/load (`<app>/files/modmenu/modmenu_config.json`) & **Reset All** (auto-restores all KittyMemory patches!)
- Always-visible `ModMenu::OnOverlayDraw` for ESP/crosshairs even while the menu is minimized
