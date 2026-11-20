LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := Loader
LOCAL_SRC_FILES := loader.cpp
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -Werror -fvisibility=hidden
LOCAL_LDLIBS := -llog -ldl
LOCAL_LDFLAGS := -Wl,--version-script=$(LOCAL_PATH)/loader.map -Wl,-z,max-page-size=16384
include $(BUILD_SHARED_LIBRARY)
