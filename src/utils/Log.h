#pragma once

#include <android/log.h>

#include "config/ModuleConfig.h"

#define ARC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, arc_autoplay::cfg::module::kLogTag, __VA_ARGS__)
#define ARC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, arc_autoplay::cfg::module::kLogTag, __VA_ARGS__)
