// jni/ModMenu/Overlay.h
#pragma once

#include <atomic>
#include <cstdint>
#include <jni.h>
#include <string>

namespace ModOverlay {

struct RuntimeFlags {
    std::atomic<bool> menuOpen{false};
    std::atomic<bool> passthrough{true};
    std::atomic<int> maxFps{60};
};

RuntimeFlags &Flags();

using FrameCallback = void (*)();
using InitCallback = void (*)();

// Installs SurfaceView overlay via InMemoryDexClassLoader (returns false to fall back to EGL hook).
bool InstallSurface(JNIEnv *env, FrameCallback drawFn, InitCallback initFn = nullptr);

// Installs EGL eglSwapBuffers fallback hook when SurfaceView is disabled or unavailable.
void InstallEglHook(FrameCallback drawFn, InitCallback initFn = nullptr);

bool IsSurfaceActive();
bool ShowTextInput(uint64_t request, const std::string &initial, bool multiline, bool secret);
bool CopyClipboard(const std::string &text);
std::string PrivateDirectory();
std::string PackageName();
void UpdatePointerGestures();

} // namespace ModOverlay
