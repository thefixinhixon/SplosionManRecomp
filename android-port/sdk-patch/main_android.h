#pragma once
/**
 ******************************************************************************
 * @file        rex/main_android.h
 * @brief       Android platform entry helpers.
 *
 * TP-ANDROID-PATCH: threading_posix.cpp includes this header upstream,
 * but it was never shipped. It only needs to provide
 * rex::GetAndroidApiLevel(); the per-subsystem AndroidInitialize hooks
 * are declared by their own headers (rex/thread.h, rex/memory/utils.h,
 * rex/filesystem.h).
 ******************************************************************************
 */

#include <rex/platform.h>

#if REX_PLATFORM_ANDROID

#include <android/api-level.h>

namespace rex {

// Runtime Android API level of the device (e.g. 34), as opposed to the
// compile-time __ANDROID_API__ the binary was built against.
inline int GetAndroidApiLevel() { return android_get_device_api_level(); }

}  // namespace rex

#endif  // REX_PLATFORM_ANDROID
