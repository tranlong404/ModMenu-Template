// jni/ModMenu/Keyboard.cpp
#include "Keyboard.h"
#include "Overlay.h"
#include "imgui/imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace ModKeyboard {
namespace {

struct EditorState {
    std::mutex mutex;
    uint64_t nextRequest = 1;
    uint64_t activeRequest = 0;
    std::function<void(const std::string &)> callback;
    bool hasEvent = false;
    uint64_t eventRequest = 0;
    std::string eventText;
    int eventStatus = 0;
};

EditorState &State() {
    static auto *s = new EditorState;
    return *s;
}

} // namespace

void PostEditorEvent(uint64_t request, std::string text, int status) {
    auto &st = State();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (request == st.activeRequest) {
        st.hasEvent = true;
        st.eventRequest = request;
        st.eventText = std::move(text);
        st.eventStatus = status;
    }
}

void Update() {
    std::function<void(const std::string &)> cb;
    std::string text;
    int status = 0;
    {
        auto &st = State();
        std::lock_guard<std::mutex> lock(st.mutex);
        if (!st.hasEvent) return;
        st.hasEvent = false;
        status = st.eventStatus;
        text = std::move(st.eventText);
        if (st.eventRequest == st.activeRequest) {
            cb = st.callback;
            if (status != 0) {
                st.activeRequest = 0;
                st.callback = nullptr;
            }
        }
    }
    // status 0 = live text update, status 1 = confirmed/OK, status < 0 = cancelled
    if (cb && status >= 0) {
        cb(text);
    }
}

bool Open(const std::string &initialText, const std::function<void(const std::string &)> &onCommit,
          bool multiline, bool secret) {
    if (!ModOverlay::IsSurfaceActive()) return false;
    uint64_t req = 0;
    {
        auto &st = State();
        std::lock_guard<std::mutex> lock(st.mutex);
        req = ++st.nextRequest;
        st.activeRequest = req;
        st.callback = onCommit;
        st.hasEvent = false;
    }
    if (!ModOverlay::ShowTextInput(req, initialText, multiline, secret)) {
        auto &st = State();
        std::lock_guard<std::mutex> lock(st.mutex);
        if (st.activeRequest == req) {
            st.activeRequest = 0;
            st.callback = nullptr;
        }
        return false;
    }
    return true;
}

bool InputString(const char *id, const char *hint, std::string &value, bool secret) {
    static std::unordered_map<std::string, std::string> buffers;
    auto &buf = buffers[id ? id : ""];
    if (buf.size() < 512) buf.resize(512, '\0');
    if (std::strcmp(buf.c_str(), value.c_str()) != 0) {
        std::memset(buf.data(), 0, buf.size());
        std::strncpy(buf.data(), value.c_str(), buf.size() - 1);
    }
    ImGuiInputTextFlags flags = secret ? ImGuiInputTextFlags_Password : 0;
    bool changed = hint && *hint
        ? ImGui::InputTextWithHint(id, hint, buf.data(), buf.size(), flags)
        : ImGui::InputText(id, buf.data(), buf.size(), flags);
    if (changed) {
        value = buf.c_str();
    }
    if (ImGui::IsItemActivated() && ModOverlay::IsSurfaceActive()) {
        std::string *targetPtr = &value;
        std::string key = id ? id : "";
        Open(value, [targetPtr, key](const std::string &updated) {
            *targetPtr = updated;
            auto it = buffers.find(key);
            if (it != buffers.end()) {
                std::memset(it->second.data(), 0, it->second.size());
                std::strncpy(it->second.data(), updated.c_str(), it->second.size() - 1);
            }
        }, false, secret);
    }
    return changed;
}

bool InputInt(const char *label, int *value, int minVal, int maxVal) {
    if (!value) return false;
    int v = *value;
    bool changed = ImGui::InputInt(label, &v, 1, 10);
    if (changed) {
        *value = std::clamp(v, minVal, maxVal);
    }
    if (ImGui::IsItemActivated() && ModOverlay::IsSurfaceActive()) {
        Open(std::to_string(*value), [value, minVal, maxVal](const std::string &txt) {
            try {
                int parsed = std::stoi(txt);
                *value = std::clamp(parsed, minVal, maxVal);
            } catch (...) {}
        }, false, false);
    }
    return changed;
}

} // namespace ModKeyboard
