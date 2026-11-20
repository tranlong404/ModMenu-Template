LOCAL_PATH := $(call my-dir)
MY_ROOT_PATH := $(LOCAL_PATH)

include $(MY_ROOT_PATH)/Dobby/Android.mk

LOCAL_PATH := $(MY_ROOT_PATH)
include $(CLEAR_VARS)

LOCAL_MODULE := ModMenu

LOCAL_CFLAGS := -w -s -fvisibility=hidden -fexceptions
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wno-unused-parameter -fvisibility=hidden -fexceptions
LOCAL_LDFLAGS += -Wl,--gc-sections,--strip-all -Wl,-z,max-page-size=16384
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -ldl -lm -lc

LOCAL_C_INCLUDES := \
    $(MY_ROOT_PATH) \
    $(MY_ROOT_PATH)/imgui \
    $(MY_ROOT_PATH)/Dobby/include

LOCAL_STATIC_LIBRARIES := dobby

LOCAL_SRC_FILES := \
    Main.cpp \
    Features.cpp \
    ModMenu/ModMenu.cpp \
    ModMenu/WorkspaceUI.cpp \
    ModMenu/Overlay.cpp \
    ModMenu/Keyboard.cpp \
    Includes/Utils.cpp \
    Includes/Logger.cpp \
    KittyMemory/KittyMemory.cpp \
    KittyMemory/MemoryPatch.cpp \
    KittyMemory/MemoryBackup.cpp \
    KittyMemory/KittyUtils.cpp \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_android.cpp \
    imgui/backends/imgui_impl_opengl3.cpp

include $(BUILD_SHARED_LIBRARY)
