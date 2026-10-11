LOCAL_PATH := $(call my-dir)

# libxash links against the pinned Khronos loader in every Android build type.
# Gradle fetches this build-only binary before ndk-build runs.
include $(CLEAR_VARS)
LOCAL_MODULE := openxr_loader
LOCAL_SRC_FILES := ../../../build/openxr-loader/1.1.63/libopenxr_loader.so
include $(PREBUILT_SHARED_LIBRARY)
