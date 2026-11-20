// jni/ModMenu/Overlay.cpp
#include "Overlay.h"
#include "Keyboard.h"
#include "Dobby/include/dobby.h"
#include "Includes/Logger.h"
#include "Includes/Roboto-Regular.h"
#include "Includes/Utils.h"
#include "Includes/obfuscate.h"
#include "generated/overlay_dex.hpp"
#include "imgui/backends/imgui_impl_android.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <deque>
#include <dlfcn.h>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ModOverlay {

RuntimeFlags &Flags() {
    static auto *f = new RuntimeFlags;
    return *f;
}

namespace {

std::atomic<bool> gSurfaceActive{false};
std::atomic<bool> gCaptureAll{false};
std::mutex gRenderMutex, gHitMutex, gInputMutex;

JavaVM *gVm = nullptr;
jclass gOverlayClass = nullptr;
FrameCallback gDrawFn = nullptr;
InitCallback gInitFn = nullptr;

bool gImGuiInitialized = false;
bool gBackendReady = false;
bool gMenuReady = false;
bool gNeedClear = true;
int gWidth = 0;
int gHeight = 0;
std::chrono::steady_clock::time_point gPrevFrame;

struct Rect { float x, y, w, h; };
struct InputEvent {
    enum class Kind { Touch, Motion };
    Kind kind = Kind::Touch;
    int action = 0, toolType = 0, buttonState = 0;
    float x = 0.f, y = 0.f, hScroll = 0.f, vScroll = 0.f;
};

std::vector<Rect> gFrameRects, gPublishedRects;
std::deque<InputEvent> gPendingInputs;

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn gOrigSwapBuffers = nullptr;

JavaVM *ResolveVm() {
    if (gVm) return gVm;
    using GetVMsFn = jint (*)(JavaVM **, jsize, jsize *);
    auto getVms = reinterpret_cast<GetVMsFn>(dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    if (!getVms) {
        static void *art = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
        if (art) getVms = reinterpret_cast<GetVMsFn>(dlsym(art, "JNI_GetCreatedJavaVMs"));
    }
    JavaVM *vm = nullptr;
    jsize count = 0;
    if (getVms && getVms(&vm, 1, &count) == JNI_OK && count > 0) {
        gVm = vm;
        return vm;
    }
    return nullptr;
}

struct ScopedEnv {
    JavaVM *vm = nullptr;
    JNIEnv *e = nullptr;
    bool attached = false;
    ScopedEnv() {
        vm = ResolveVm();
        if (!vm) return;
        jint rc = vm->GetEnv(reinterpret_cast<void **>(&e), JNI_VERSION_1_6);
        if (rc == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&e, nullptr) == JNI_OK && e) attached = true;
            else e = nullptr;
        } else if (rc != JNI_OK) {
            e = nullptr;
        }
    }
    ~ScopedEnv() {
        if (attached && vm) vm->DetachCurrentThread();
    }
    bool ok() const { return e != nullptr; }
};

bool JniOk(JNIEnv *e, const char *stage) {
    if (!e->ExceptionCheck()) return true;
    LOGE("ModOverlay JNI error at %s", stage);
    e->ExceptionDescribe();
    e->ExceptionClear();
    return false;
}

jobject CurrentActivity(JNIEnv *e, jclass threadCls) {
    jmethodID curMid = e->GetStaticMethodID(threadCls, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (!JniOk(e, "currentActivityThread") || !curMid) return nullptr;
    jobject thread = e->CallStaticObjectMethod(threadCls, curMid);
    if (!JniOk(e, "thread") || !thread) return nullptr;
    jfieldID regFld = e->GetFieldID(threadCls, "mActivities", "Landroid/util/ArrayMap;");
    if (!JniOk(e, "mActivities") || !regFld) return nullptr;
    jobject map = e->GetObjectField(thread, regFld);
    if (!JniOk(e, "map") || !map) return nullptr;
    jclass mapCls = e->FindClass("java/util/Map");
    jclass colCls = e->FindClass("java/util/Collection");
    jclass itCls = e->FindClass("java/util/Iterator");
    if (!JniOk(e, "collections") || !mapCls || !colCls || !itCls) return nullptr;
    jobject vals = e->CallObjectMethod(map, e->GetMethodID(mapCls, "values", "()Ljava/util/Collection;"));
    if (!JniOk(e, "values") || !vals) return nullptr;
    jobject it = e->CallObjectMethod(vals, e->GetMethodID(colCls, "iterator", "()Ljava/util/Iterator;"));
    if (!JniOk(e, "iterator") || !it) return nullptr;
    jmethodID hasNext = e->GetMethodID(itCls, "hasNext", "()Z");
    jmethodID next = e->GetMethodID(itCls, "next", "()Ljava/lang/Object;");
    for (unsigned i = 0; i < 64; ++i) {
        if (!e->CallBooleanMethod(it, hasNext) || !JniOk(e, "hasNext")) break;
        jobject rec = e->CallObjectMethod(it, next);
        if (!JniOk(e, "next") || !rec) break;
        jclass recCls = e->GetObjectClass(rec);
        jfieldID actFld = e->GetFieldID(recCls, "activity", "Landroid/app/Activity;");
        jfieldID pFld = e->GetFieldID(recCls, "paused", "Z");
        bool paused = e->GetBooleanField(rec, pFld);
        jobject act = e->GetObjectField(rec, actFld);
        e->DeleteLocalRef(recCls);
        e->DeleteLocalRef(rec);
        if (!JniOk(e, "record")) return nullptr;
        if (!paused && act) return act;
        if (act) e->DeleteLocalRef(act);
    }
    return nullptr;
}

void ImGuiSetClipboard(void *, const char *text) {
    if (text) CopyClipboard(text);
}

void SetupImGuiContext() {
    if (gImGuiInitialized) return;
    if (!ImGui::CreateContext()) return;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(gWidth), static_cast<float>(gHeight));
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.SetClipboardTextFn = ImGuiSetClipboard;

    ImGui_ImplAndroid_Init(nullptr);
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        return;
    }
    io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, sizeof(Roboto_Regular), 38.0f, nullptr,
                                   io.Fonts->GetGlyphRangesVietnamese());
    ImGui::GetStyle().ScaleAllSizes(2.0f);
    ImGui::GetStyle().ScrollbarSize *= 2.2f;
    ImGui::GetStyle().WindowTitleAlign = ImVec2(0.5f, 0.5f);

    if (gInitFn) gInitFn();
    gImGuiInitialized = true;
}

void DrawImGuiFrame(int width, int height) {
    if (!gImGuiInitialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(width, height);
    ImGui::NewFrame();
    if (gSurfaceActive.load(std::memory_order_acquire))
        UpdatePointerGestures();
    if (gDrawFn) gDrawFn();
    ImGui::Render();
    if (gNeedClear) {
        glViewport(0, 0, width, height);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (ImGui::GetDrawData())
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void NativeCreate(JNIEnv *, jclass) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        gBackendReady = ImGui_ImplOpenGL3_Init("#version 300 es");
    } else {
        gBackendReady = true;
    }
    gPrevFrame = std::chrono::steady_clock::now();
    { std::lock_guard<std::mutex> h(gHitMutex); gPublishedRects.clear(); }
    { std::lock_guard<std::mutex> i(gInputMutex); gPendingInputs.clear(); }
}

void NativeTouch(JNIEnv *, jclass, jint action, jfloat x, jfloat y) {
    std::lock_guard<std::mutex> lock(gInputMutex);
    if (gPendingInputs.size() >= 512) gPendingInputs.clear();
    gPendingInputs.push_back({InputEvent::Kind::Touch, action, 0, 0, x, y, 0.f, 0.f});
}

void NativeMotion(JNIEnv *, jclass, jint action, jint toolType, jfloat x, jfloat y,
                  jint buttonState, jfloat hScroll, jfloat vScroll) {
    std::lock_guard<std::mutex> lock(gInputMutex);
    if (gPendingInputs.size() >= 512) gPendingInputs.clear();
    gPendingInputs.push_back({InputEvent::Kind::Motion, action, toolType, buttonState, x, y, hScroll, vScroll});
}

void NativeText(JNIEnv *env, jclass, jlong request, jbyteArray chars, jint status) {
    std::string text;
    if (chars) {
        text.resize(env->GetArrayLength(chars));
        if (!text.empty())
            env->GetByteArrayRegion(chars, 0, static_cast<jsize>(text.size()),
                                    reinterpret_cast<jbyte *>(text.data()));
        if (!JniOk(env, "nativeText")) return;
    }
    ModKeyboard::PostEditorEvent(static_cast<uint64_t>(request), std::move(text), status);
}

jboolean NativeMenuOpen(JNIEnv *, jclass) {
    return Flags().menuOpen.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeHit(JNIEnv *, jclass, jfloat x, jfloat y) {
    if (gCaptureAll.load(std::memory_order_acquire)) return JNI_TRUE;
    std::lock_guard<std::mutex> lock(gHitMutex);
    for (const auto &r : gPublishedRects)
        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
            return JNI_TRUE;
    return JNI_FALSE;
}

void NativeFrame(JNIEnv *, jclass, jint width, jint height, jint editorTop) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    if (!gBackendReady || width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    auto now = std::chrono::steady_clock::now();
    gWidth = width;
    gHeight = height;
    if (!gMenuReady) {
        SetupImGuiContext();
        gMenuReady = ImGui::GetCurrentContext() != nullptr;
        if (!gMenuReady) return;
    }
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - gPrevFrame).count(), 0.001f, 0.1f);
    gPrevFrame = now;

    std::deque<InputEvent> events;
    {
        std::lock_guard<std::mutex> inLock(gInputMutex);
        events.swap(gPendingInputs);
    }
    static float lastPtrX = -1.f, lastPtrY = -1.f;
    for (const auto &ev : events) {
        if (ev.kind == InputEvent::Kind::Touch) {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(ev.x, ev.y);
            lastPtrX = ev.x; lastPtrY = ev.y;
            if (ev.action == 0) io.AddMouseButtonEvent(0, true);
            else if (ev.action == 1 || ev.action == 3) io.AddMouseButtonEvent(0, false);
        } else {
            if (ev.toolType == 2 || ev.toolType == 4) io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
            else if (ev.toolType == 3 || ev.action == 8 || ev.action == 7 || ev.action == 9 || ev.action == 10)
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            else io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);

            switch (ev.action) {
            case 0:
                io.AddMousePosEvent(ev.x, ev.y);
                lastPtrX = ev.x; lastPtrY = ev.y;
                if (ev.toolType == 3) {
                    bool any = (ev.buttonState & 7) != 0;
                    io.AddMouseButtonEvent(0, any ? ((ev.buttonState & 1) != 0) : true);
                    io.AddMouseButtonEvent(1, (ev.buttonState & 2) != 0);
                    io.AddMouseButtonEvent(2, (ev.buttonState & 4) != 0);
                } else io.AddMouseButtonEvent(0, true);
                break;
            case 1:
            case 3:
                io.AddMousePosEvent(ev.x, ev.y);
                lastPtrX = ev.x; lastPtrY = ev.y;
                io.AddMouseButtonEvent(0, false);
                if (ev.toolType == 3) {
                    io.AddMouseButtonEvent(1, false);
                    io.AddMouseButtonEvent(2, false);
                }
                break;
            case 2: case 7: case 9:
                io.AddMousePosEvent(ev.x, ev.y);
                lastPtrX = ev.x; lastPtrY = ev.y;
                break;
            case 10:
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                break;
            case 11: case 12:
                io.AddMousePosEvent(ev.x, ev.y);
                lastPtrX = ev.x; lastPtrY = ev.y;
                io.AddMouseButtonEvent(0, (ev.buttonState & 1) != 0);
                io.AddMouseButtonEvent(1, (ev.buttonState & 2) != 0);
                io.AddMouseButtonEvent(2, (ev.buttonState & 4) != 0);
                break;
            case 8:
                if ((ev.x > 0.f || ev.y > 0.f) && (ev.x != lastPtrX || ev.y != lastPtrY)) {
                    io.AddMousePosEvent(ev.x, ev.y);
                    lastPtrX = ev.x; lastPtrY = ev.y;
                }
                io.AddMouseWheelEvent(ev.hScroll, ev.vScroll);
                break;
            default: break;
            }
        }
    }

    DrawImGuiFrame(width, height);

    if (editorTop >= 0 && editorTop < height) {
        GLint oldScissor[4];
        glGetIntegerv(GL_SCISSOR_BOX, oldScissor);
        const bool wasScissored = glIsEnabled(GL_SCISSOR_TEST);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, width, height - editorTop);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(oldScissor[0], oldScissor[1], oldScissor[2], oldScissor[3]);
        if (!wasScissored) glDisable(GL_SCISSOR_TEST);
    }

    gCaptureAll.store(!Flags().passthrough.load() && Flags().menuOpen.load(), std::memory_order_release);
    gFrameRects.clear();
    if (auto *ctx = ImGui::GetCurrentContext()) {
        for (auto *win : ctx->Windows) {
            if (!win->WasActive || win->Hidden || (win->Flags & ImGuiWindowFlags_NoMouseInputs)) continue;
            gFrameRects.push_back({win->Pos.x, win->Pos.y, win->Size.x, win->Size.y});
        }
    }
    { std::lock_guard<std::mutex> h(gHitMutex); gPublishedRects = gFrameRects; }

    int fps = std::clamp(Flags().maxFps.load(), 15, 120);
    std::this_thread::sleep_until(now + std::chrono::microseconds(1000000 / fps));
}

EGLBoolean HookedSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) {
        gWidth = w;
        gHeight = h;
        SetupImGuiContext();
        DrawImGuiFrame(w, h);
    }
    return gOrigSwapBuffers(dpy, surf);
}

} // namespace

bool IsSurfaceActive() { return gSurfaceActive.load(std::memory_order_acquire); }

void UpdatePointerGestures() {
    static ImGuiID scrollWindow = 0;
    static ImVec2 start, last;
    static bool panning = false;
    auto &g = *ImGui::GetCurrentContext();
    auto &io = g.IO;
    if (io.MouseClicked[0]) {
        scrollWindow = 0;
        panning = false;
        start = last = io.MousePos;
        auto *w = g.HoveredWindow;
        if (w && w->InnerRect.Contains(start)) {
            for (; w; w = w->ParentWindow) {
                if (!(w->Flags & ImGuiWindowFlags_NoScrollWithMouse) &&
                    (w->ScrollMax.y > 0.f || w->ScrollMax.x > 0.f)) {
                    scrollWindow = w->ID;
                    break;
                }
            }
        }
    }
    auto *w = scrollWindow ? ImGui::FindWindowByID(scrollWindow) : nullptr;
    if (!w || !w->WasActive || (!io.MouseDown[0] && !io.MouseReleased[0])) {
        scrollWindow = 0;
        panning = false;
        return;
    }
    const bool windowBg = g.ActiveIdWindow &&
        (g.ActiveId == g.ActiveIdWindow->MoveId || g.ActiveId == g.ActiveIdWindow->RootWindow->MoveId);
    if (!panning && (g.DragDropActive ||
        (g.ActiveId && (g.ActiveId == g.InputTextState.ID || (g.ActiveIdUsingNavDirMask && !windowBg) ||
         g.ActiveId == ImGui::GetWindowScrollbarID(w, ImGuiAxis_X) ||
         g.ActiveId == ImGui::GetWindowScrollbarID(w, ImGuiAxis_Y))))) {
        scrollWindow = 0;
        return;
    }
    const ImVec2 dist(io.MousePos.x - start.x, io.MousePos.y - start.y);
    const float thresh = std::max(io.MouseDragThreshold, ImGui::GetFontSize() * 0.35f);
    if (!panning && (std::abs(dist.y) > thresh || std::abs(dist.x) > thresh))
        panning = (w->ScrollMax.y > 0.f && std::abs(dist.y) > thresh) ||
                  (w->ScrollMax.x > 0.f && std::abs(dist.x) > thresh);
    if (panning) {
        g.MovingWindow = nullptr;
        const ImVec2 delta(io.MousePos.x - last.x, io.MousePos.y - last.y);
        if (w->ScrollMax.y > 0.f) ImGui::SetScrollY(w, std::clamp(w->Scroll.y - delta.y, 0.f, w->ScrollMax.y));
        if (w->ScrollMax.x > 0.f && std::abs(dist.x) > std::abs(dist.y))
            ImGui::SetScrollX(w, std::clamp(w->Scroll.x - delta.x, 0.f, w->ScrollMax.x));
        ImGui::ClearActiveID();
        ImGui::SetKeyOwner(ImGuiKey_MouseLeft, ImHashStr("ModMenuListPan"), ImGuiInputFlags_LockUntilRelease);
        io.MouseClicked[0] = io.MouseReleased[0] = io.MouseDoubleClicked[0] = false;
    }
    last = io.MousePos;
    if (!io.MouseDown[0]) {
        scrollWindow = 0;
        panning = false;
    }
}

bool InstallSurface(JNIEnv *e, FrameCallback drawFn, InitCallback initFn) {
    if (!e) return false;
    gDrawFn = drawFn;
    gInitFn = initFn;
    gNeedClear = true;
    e->GetJavaVM(&gVm);

    if (e->PushLocalFrame(128) < 0) {
        JniOk(e, "PushLocalFrame");
        return false;
    }
    struct Frame { JNIEnv *env; ~Frame() { env->PopLocalFrame(nullptr); } } pop{e};

    // Respect user-saved backend preference if set to "egl"
    auto privDir = PrivateDirectory();
    if (!privDir.empty()) {
        std::string savedBackend;
        std::ifstream f(privDir + "/render-backend");
        f >> savedBackend;
        if (savedBackend == "egl") return false;
    }

    jclass threadCls = e->FindClass("android/app/ActivityThread");
    if (!JniOk(e, "ActivityThread") || !threadCls) return false;
    jmethodID curAppMid = e->GetStaticMethodID(threadCls, "currentApplication", "()Landroid/app/Application;");
    jobject app = (JniOk(e, "curAppMid") && curAppMid) ? e->CallStaticObjectMethod(threadCls, curAppMid) : nullptr;
    if (!JniOk(e, "app") || !app) return false;

    jclass ctxCls = e->FindClass("android/content/Context");
    jmethodID getLoader = e->GetMethodID(ctxCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject parent = (JniOk(e, "getLoader") && getLoader) ? e->CallObjectMethod(app, getLoader) : nullptr;
    if (!JniOk(e, "parentLoader") || !parent) return false;

    jclass dexCls = e->FindClass("dalvik/system/InMemoryDexClassLoader");
    jmethodID dexCtor = e->GetMethodID(dexCls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject buf = e->NewDirectByteBuffer(const_cast<unsigned char *>(kOverlayDex), sizeof(kOverlayDex));
    jobject loader = (JniOk(e, "dexBuf") && dexCtor && buf) ? e->NewObject(dexCls, dexCtor, buf, parent) : nullptr;
    if (!JniOk(e, "dexLoader") || !loader) return false;

    jclass clCls = e->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = e->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring clsName = e->NewStringUTF("dev.libtool.rewrite.Overlay");
    jclass local = (JniOk(e, "loadClass") && loadClass)
        ? static_cast<jclass>(e->CallObjectMethod(loader, loadClass, clsName))
        : nullptr;
    if (!JniOk(e, "OverlayClass") || !local) return false;

    JNINativeMethod methods[] = {
        {const_cast<char *>("nativeCreate"), const_cast<char *>("()V"), reinterpret_cast<void *>(NativeCreate)},
        {const_cast<char *>("nativeFrame"), const_cast<char *>("(III)V"), reinterpret_cast<void *>(NativeFrame)},
        {const_cast<char *>("nativeTouch"), const_cast<char *>("(IFF)V"), reinterpret_cast<void *>(NativeTouch)},
        {const_cast<char *>("nativeHit"), const_cast<char *>("(FF)Z"), reinterpret_cast<void *>(NativeHit)},
        {const_cast<char *>("nativeMotion"), const_cast<char *>("(IIFFIFF)V"), reinterpret_cast<void *>(NativeMotion)},
        {const_cast<char *>("nativeText"), const_cast<char *>("(J[BI)V"), reinterpret_cast<void *>(NativeText)},
        {const_cast<char *>("nativeMenuOpen"), const_cast<char *>("()Z"), reinterpret_cast<void *>(NativeMenuOpen)},
    };
    if (e->RegisterNatives(local, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK || !JniOk(e, "RegisterNatives"))
        return false;

    gOverlayClass = static_cast<jclass>(e->NewGlobalRef(local));
    if (!gOverlayClass) return false;

    if (jmethodID initStorage = e->GetStaticMethodID(local, "initializeStorage", "(Landroid/app/Application;)V");
        JniOk(e, "initStorage") && initStorage) {
        e->CallStaticVoidMethod(local, initStorage, app);
        JniOk(e, "callInitStorage");
    }

    jmethodID installMid = e->GetStaticMethodID(local, "install", "(Landroid/app/Application;Landroid/app/Activity;)V");
    if (!JniOk(e, "installMid") || !installMid) return false;

    jobject activity = CurrentActivity(e, threadCls);
    gSurfaceActive.store(true, std::memory_order_release);
    e->CallStaticVoidMethod(local, installMid, app, activity);
    if (!JniOk(e, "Overlay.install")) {
        gSurfaceActive.store(false, std::memory_order_release);
        return false;
    }
    LOGI("ModOverlay: SurfaceView overlay attached");
    return true;
}

void InstallEglHook(FrameCallback drawFn, InitCallback initFn) {
    gDrawFn = drawFn;
    gInitFn = initFn;
    gNeedClear = false;
    while (!isLibraryLoaded(OBFUSCATE("libEGL.so"))) {
        sleep(1);
    }
    void *sym = DobbySymbolResolver(OBFUSCATE("libEGL.so"), OBFUSCATE("eglSwapBuffers"));
    if (!sym) {
        LOGE("ModOverlay: eglSwapBuffers not found");
        return;
    }
    if (DobbyHook(sym, reinterpret_cast<void *>(HookedSwapBuffers), reinterpret_cast<void **>(&gOrigSwapBuffers)) == 0) {
        LOGI("ModOverlay: EGL swapbuffers hook installed");
    }
}

bool ShowTextInput(uint64_t request, const std::string &initial, bool multiline, bool secret) {
    if (!gOverlayClass) return false;
    ScopedEnv env;
    if (!env.ok()) return false;
    JNIEnv *e = env.e;
    jmethodID mid = e->GetStaticMethodID(gOverlayClass, "showTextInput", "(J[BZZ)Z");
    if (!JniOk(e, "showTextInput") || !mid) return false;
    jbyteArray arr = e->NewByteArray(static_cast<jsize>(initial.size()));
    if (!JniOk(e, "byteArr") || !arr) return false;
    if (!initial.empty())
        e->SetByteArrayRegion(arr, 0, static_cast<jsize>(initial.size()),
                              reinterpret_cast<const jbyte *>(initial.data()));
    jboolean ok = e->CallStaticBooleanMethod(gOverlayClass, mid, static_cast<jlong>(request), arr,
                                             static_cast<jboolean>(multiline), static_cast<jboolean>(secret));
    e->DeleteLocalRef(arr);
    return JniOk(e, "callShowTextInput") && ok == JNI_TRUE;
}

bool CopyClipboard(const std::string &text) {
    ScopedEnv env;
    if (!env.ok()) return false;
    JNIEnv *e = env.e;
    if (e->PushLocalFrame(32) < 0) return false;
    struct Frame { JNIEnv *env; ~Frame() { env->PopLocalFrame(nullptr); } } pop{e};
    jclass threadCls = e->FindClass("android/app/ActivityThread");
    if (!JniOk(e, "clipThread") || !threadCls) return false;
    jobject app = e->CallStaticObjectMethod(
        threadCls, e->GetStaticMethodID(threadCls, "currentApplication", "()Landroid/app/Application;"));
    if (!JniOk(e, "clipApp") || !app) return false;
    jclass ctxCls = e->FindClass("android/content/Context");
    jobject mgr = e->CallObjectMethod(
        app, e->GetMethodID(ctxCls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"),
        e->NewStringUTF("clipboard"));
    if (!JniOk(e, "clipMgr") || !mgr) return false;
    jclass clipCls = e->FindClass("android/content/ClipData");
    jobject clip = e->CallStaticObjectMethod(
        clipCls,
        e->GetStaticMethodID(clipCls, "newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;"),
        e->NewStringUTF("ModMenu"), e->NewStringUTF(text.c_str()));
    if (!JniOk(e, "clipData") || !clip) return false;
    jclass mgrCls = e->GetObjectClass(mgr);
    e->CallVoidMethod(mgr, e->GetMethodID(mgrCls, "setPrimaryClip", "(Landroid/content/ClipData;)V"), clip);
    return JniOk(e, "setPrimaryClip");
}

std::string PrivateDirectory() {
    ScopedEnv env;
    if (!env.ok()) return {};
    JNIEnv *e = env.e;
    if (e->PushLocalFrame(32) < 0) return {};
    struct Frame { JNIEnv *env; ~Frame() { env->PopLocalFrame(nullptr); } } pop{e};
    jclass threadCls = e->FindClass("android/app/ActivityThread");
    if (!JniOk(e, "privThread") || !threadCls) return {};
    jobject app = e->CallStaticObjectMethod(
        threadCls, e->GetStaticMethodID(threadCls, "currentApplication", "()Landroid/app/Application;"));
    if (!JniOk(e, "privApp") || !app) return {};
    jclass ctxCls = e->FindClass("android/content/Context");
    jobject f = e->CallObjectMethod(app, e->GetMethodID(ctxCls, "getFilesDir", "()Ljava/io/File;"));
    if (!JniOk(e, "filesDir") || !f) return {};
    jclass fCls = e->FindClass("java/io/File");
    auto js = static_cast<jstring>(e->CallObjectMethod(f, e->GetMethodID(fCls, "getAbsolutePath", "()Ljava/lang/String;")));
    if (!JniOk(e, "absPath") || !js) return {};
    const char *p = e->GetStringUTFChars(js, nullptr);
    std::string dir = p ? (std::string(p) + "/modmenu") : "";
    if (p) e->ReleaseStringUTFChars(js, p);
    if (!dir.empty()) {
        if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return {};
        chmod(dir.c_str(), 0700);
    }
    return dir;
}

std::string PackageName() {
    std::ifstream f("/proc/self/cmdline", std::ios::binary);
    if (f) {
        std::string raw;
        std::getline(f, raw, '\0');
        size_t c = raw.find(':');
        if (c != std::string::npos) raw.resize(c);
        if (!raw.empty()) return raw;
    }
    return "unknown";
}

} // namespace ModOverlay
