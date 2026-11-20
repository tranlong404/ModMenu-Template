// jni/Main.cpp
#include "Includes/Logger.h"
#include "ModMenu/ModMenu.h"
#include "ModMenu/Overlay.h"

#include <atomic>
#include <jni.h>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<bool> gStarted{false};
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    if (!vm) return JNI_ERR;
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
        return JNI_ERR;

    bool expected = false;
    if (gStarted.compare_exchange_strong(expected, true)) {
        ModMenu::BootInit();
        if (!ModOverlay::InstallSurface(env, ModMenu::DrawFrame, ModMenu::OnImGuiSetup)) {
            LOGI("Surface overlay unavailable or EGL selected; falling back to eglSwapBuffers hook");
            std::thread([]() {
                ModOverlay::InstallEglHook(ModMenu::DrawFrame, ModMenu::OnImGuiSetup);
            }).detach();
        }
    }
    return JNI_VERSION_1_6;
}
