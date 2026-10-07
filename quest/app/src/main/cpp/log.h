#pragma once
#include <android/log.h>

#define LOG_TAG "XRWrist"
// VRC: release builds stay quiet; debug keeps full telemetry.
#ifdef NDEBUG
#define LOGI(...) ((void)0)
#define LOGD(...) ((void)0)
#else
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#endif
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
