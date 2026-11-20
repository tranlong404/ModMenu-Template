// jni/ModMenu/Keyboard.h
#pragma once

#include "imgui/imgui.h"
#include <cstdint>
#include <functional>
#include <string>

namespace ModKeyboard {

void Update();
void PostEditorEvent(uint64_t request, std::string text, int status);

// Opens native Android text input dialog via SurfaceOverlay (with inline fallback).
bool Open(const std::string &initialText, const std::function<void(const std::string &)> &onCommit,
          bool multiline = false, bool secret = false);

// ImGui text input widget that automatically triggers native Android keyboard on tap.
bool InputString(const char *id, const char *hint, std::string &value, bool secret = false);
bool InputInt(const char *label, int *value, int minVal, int maxVal);

} // namespace ModKeyboard
