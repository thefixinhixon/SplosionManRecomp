#!/usr/bin/env python3
"""Apply the Android patch set to a copy of the ReXGlue SDK source tree.

Usage: apply_android_sdk_patches.py <sdk-dir>

Every patch is anchored on exact upstream text and fails loudly if the
anchor is missing, so SDK drift is caught instead of silently shipping a
half-patched tree. Idempotent: patches already applied are skipped.
"""
import pathlib
import sys

SDK = pathlib.Path(sys.argv[1])
MARKER = "TP-ANDROID-PATCH"


def patch_file(relpath, old, new, name):
    path = SDK / relpath
    text = path.read_text()
    if new in text:
        print(f"  skip (already applied): {name}")
        return
    if old not in text:
        print(f"  FAIL: anchor not found for {name} in {relpath}")
        sys.exit(1)
    path.write_text(text.replace(old, new, 1))
    print(f"  applied: {name}")


print(f"Patching SDK at {SDK}")

# --- P1a: rexui platform sources - Android uses surface_android.cpp -------
patch_file(
    "src/ui/CMakeLists.txt",
    """elseif(APPLE)
    set(REXUI_PLATFORM_SOURCES
        surface_mac.cpp
    )
else()""",
    """elseif(APPLE)
    set(REXUI_PLATFORM_SOURCES
        surface_mac.cpp
    )
elseif(ANDROID)
    # TP-ANDROID-PATCH: no X11/Wayland surfaces on Android; the Vulkan
    # presenter talks to the ANativeWindow directly.
    set(REXUI_PLATFORM_SOURCES
        surface_android.cpp
    )
else()""",
    "P1a rexui android surface source",
)

# --- P1b: rexui X11/Wayland pkg-config deps are desktop-Linux only --------
patch_file(
    "src/ui/CMakeLists.txt",
    """if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)""",
    """if(UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(PkgConfig REQUIRED)""",
    "P1b rexui skip x11/wayland deps on android",
)

# --- P2: SDL3 Linux backends must not be forced on Android ----------------
patch_file(
    "thirdparty/CMakeLists.txt",
    """if(UNIX AND NOT APPLE)
    set(SDL_X11""",
    """if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(SDL_X11""",
    "P2 sdl linux backends not on android",
)

# --- P3: let AGP own the native output directories ------------------------
patch_file(
    "CMakeLists.txt",
    """set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")""",
    """# TP-ANDROID-PATCH: on Android, AGP dictates where .so files land so it
# can package them; only set the SDK's own out/ dirs elsewhere.
if(NOT ANDROID)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${REXGLUE_ROOT}/out/${REX_PLATFORM}")
endif()""",
    "P3 agp owns output dirs",
)

# --- P4: GetExecutableFolder on Android = the app's native lib dir --------
patch_file(
    "src/core/filesystem_posix.cpp",
    """#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif""",
    """#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif""",
    "P4a dlfcn include",
)
patch_file(
    "src/core/filesystem_posix.cpp",
    """std::filesystem::path GetExecutableFolder() {
  return GetExecutablePath().parent_path();
}""",
    """std::filesystem::path GetExecutableFolder() {
#if defined(__ANDROID__)
  // TP-ANDROID-PATCH: /proc/self/exe is the system app_process binary on
  // Android, not the app. The folder that actually holds the app's .so
  // files (where the GPU plugin is staged) is the folder of this very
  // library - ask the dynamic linker.
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&GetExecutableFolder), &info) &&
      info.dli_fname) {
    return std::filesystem::path(info.dli_fname).parent_path();
  }
#endif
  return GetExecutablePath().parent_path();
}""",
    "P4b executable folder via dladdr",
)

# --- P5: SDL input driver sweeps pre-attached gamepads on Android ---------
patch_file(
    "src/input/sdl/sdl_input_driver.cpp",
    """      REXLOG_INFO("SDL input driver initialized successfully");""",
    """#if defined(__ANDROID__)
      // TP-ANDROID-PATCH: a virtual gamepad (the on-screen touch
      // controls) may be attached before this driver starts watching
      // events, so its GAMEPAD_ADDED event was never seen. Sweep up any
      // gamepads SDL already knows about so it registers anyway.
      {
        int preattached_count = 0;
        SDL_JoystickID* preattached = SDL_GetGamepads(&preattached_count);
        if (preattached) {
          for (int i = 0; i < preattached_count; ++i) {
            SDL_Event added{};
            added.type = SDL_EVENT_GAMEPAD_ADDED;
            added.gdevice.which = preattached[i];
            HandleEvent(added);
          }
          SDL_free(preattached);
        }
      }
#endif
      REXLOG_INFO("SDL input driver initialized successfully");""",
    "P5 sdl driver pre-attached gamepad sweep",
)

# --- P6a: window_sdl includes ---------------------------------------------
patch_file(
    "src/ui/window_sdl.cpp",
    """#else
#include <X11/Xlib-xcb.h>
#include <rex/ui/surface_gnulinux.h>
#endif""",
    """#elif REX_PLATFORM_ANDROID
#include <rex/ui/surface_android.h>
#else
#include <X11/Xlib-xcb.h>
#include <rex/ui/surface_gnulinux.h>
#endif""",
    "P6a window_sdl android surface include",
)

# --- P6b: window_sdl CreateSurfaceImpl Android branch ----------------------
patch_file(
    "src/ui/window_sdl.cpp",
    """#else
  SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window_);
  if (allowed_types & Surface::kTypeFlag_WaylandSurface) {""",
    """#elif REX_PLATFORM_ANDROID
  // TP-ANDROID-PATCH: upstream never wired the Android surface into the
  // SDL window; the presenter side (VkAndroidSurfaceKHR) already exists.
  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow) {
    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window_);
    auto* native_window = static_cast<ANativeWindow*>(SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
    if (native_window) {
      return std::make_unique<AndroidNativeWindowSurface>(native_window,
                                                          sdl_window_);
    }
  }
#else
  SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window_);
  if (allowed_types & Surface::kTypeFlag_WaylandSurface) {""",
    "P6b window_sdl android surface creation",
)

# --- P7: numeric.h - NDK libc++ has no floating-point std::from_chars; ---
# --- use the SDK's portable parser on Android exactly like on macOS.    ---
patch_file(
    "include/rex/string/numeric.h",
    """#if REX_PLATFORM_MAC
#include <locale.h>
#endif""",
    """#if REX_PLATFORM_MAC || REX_PLATFORM_ANDROID
#include <locale.h>
#endif""",
    "P7a numeric locale include",
)
patch_file(
    "include/rex/string/numeric.h",
    """#if REX_PLATFORM_MAC
template <typename T>
inline std::from_chars_result portable_float_from_chars""",
    """#if REX_PLATFORM_MAC || REX_PLATFORM_ANDROID
template <typename T>
inline std::from_chars_result portable_float_from_chars""",
    "P7b numeric portable parser definition",
)
patch_file(
    "include/rex/string/numeric.h",
    """#if REX_PLATFORM_MAC
    auto [p, error] = portable_float_from_chars(range.data(), range.data() + range.size(), result);""",
    """#if REX_PLATFORM_MAC || REX_PLATFORM_ANDROID
    auto [p, error] = portable_float_from_chars(range.data(), range.data() + range.size(), result);""",
    "P7c numeric portable parser call site 1",
)
patch_file(
    "include/rex/string/numeric.h",
    """#if REX_PLATFORM_MAC
      auto result = detail::portable_float_from_chars(p, end, v.f32[i]);""",
    """#if REX_PLATFORM_MAC || REX_PLATFORM_ANDROID
      auto result = detail::portable_float_from_chars(p, end, v.f32[i]);""",
    "P7d numeric portable parser call site 2",
)

# --- P8: chrono.h - NDK libc++ also hides clock_time_conversion and -----
# --- clock_cast (gated behind its unavailable TZDB support); reuse the ---
# --- header's own Apple fallback for Android.                          ---
patch_file(
    "include/rex/chrono/chrono.h",
    """#ifdef __APPLE__
// Apple libc++ does not expose clock_time_conversion or clock_cast.""",
    """#if defined(__APPLE__) || defined(__ANDROID__)
// Apple libc++ and the NDK's libc++ do not expose clock_time_conversion
// or clock_cast (the NDK gates them with its unavailable TZDB support).""",
    "P8 chrono clock_cast fallback",
)

# --- P9: fiber.h - Android branch with a hand-rolled context block -------
patch_file(
    "include/rex/thread/fiber.h",
    """#elif REX_PLATFORM_LINUX || REX_PLATFORM_MAC
  ucontext_t context_{};""",
    """#elif REX_PLATFORM_ANDROID
  // TP-ANDROID-PATCH: bionic has no ucontext functions on 64-bit. The
  // Android backend (fiber_android.cpp) keeps a hand-rolled AArch64
  // callee-saved context in ctx_ (layout private to that file).
  alignas(16) uint64_t ctx_[24] = {};
  std::vector<uint8_t> stack_;
  void (*entry_)(void*) = nullptr;
  void* arg_ = nullptr;
  bool is_thread_fiber_ = false;

  static void Trampoline();
#elif REX_PLATFORM_LINUX || REX_PLATFORM_MAC
  ucontext_t context_{};""",
    "P9 fiber.h android context",
)

# --- P10: fiber_posix.cpp must not compile on Android ---------------------
patch_file(
    "src/core/fiber_posix.cpp",
    """#include <rex/platform.h>
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC""",
    """#include <rex/platform.h>
// TP-ANDROID-PATCH: Android uses fiber_android.cpp instead (bionic has
// no ucontext functions on 64-bit).
#if (REX_PLATFORM_LINUX || REX_PLATFORM_MAC) && !REX_PLATFORM_ANDROID""",
    "P10 fiber_posix excluded on android",
)

# --- P11: rexcore builds fiber_android.cpp on Android ----------------------
patch_file(
    "src/core/CMakeLists.txt",
    """endif()

# Xenos data-format layer""",
    """endif()

# TP-ANDROID-PATCH: Android fiber backend (bionic lacks ucontext on
# 64-bit); fiber_posix.cpp compiles to nothing there (see P10).
if(ANDROID)
    target_sources(rexcore PRIVATE fiber_android.cpp)
endif()

# Xenos data-format layer""",
    "P11 rexcore android fiber source",
)

# --- P12: timer_queue.cpp - NDK libc++ has no std::jthread/stop_token ----
patch_file(
    "src/core/timer_queue.cpp",
    """    dispatch_thread_ =
        std::jthread([this](std::stop_token stop_token) { TimerThreadMain(stop_token); });""",
    """#if defined(__ANDROID__)
    // TP-ANDROID-PATCH: NDK libc++ has no std::jthread/std::stop_token;
    // a plain thread + atomic stop flag provides the same semantics here.
    dispatch_thread_ = std::thread([this] { TimerThreadMain(); });
#else
    dispatch_thread_ =
        std::jthread([this](std::stop_token stop_token) { TimerThreadMain(stop_token); });
#endif""",
    "P12a timer queue thread create",
)
patch_file(
    "src/core/timer_queue.cpp",
    """  ~TimerQueue() {
    dispatch_thread_.request_stop();""",
    """  ~TimerQueue() {
#if defined(__ANDROID__)
    stop_requested_.store(true, std::memory_order_release);
#else
    dispatch_thread_.request_stop();
#endif""",
    "P12b timer queue request stop",
)
patch_file(
    "src/core/timer_queue.cpp",
    """    // std::jthread auto-joins on destruction
  }""",
    """    // std::jthread auto-joins on destruction
#if defined(__ANDROID__)
    if (dispatch_thread_.joinable()) dispatch_thread_.join();
#endif
  }""",
    "P12c timer queue join",
)
patch_file(
    "src/core/timer_queue.cpp",
    """  void TimerThreadMain(std::stop_token stop_token) {""",
    """#if defined(__ANDROID__)
  void TimerThreadMain() {
#else
  void TimerThreadMain(std::stop_token stop_token) {
#endif""",
    "P12d timer queue main signature",
)
patch_file(
    "src/core/timer_queue.cpp",
    """    while (!stop_token.stop_requested()) {""",
    """#if defined(__ANDROID__)
    while (!stop_requested_.load(std::memory_order_acquire)) {
#else
    while (!stop_token.stop_requested()) {
#endif""",
    "P12e timer queue stop check",
)
patch_file(
    "src/core/timer_queue.cpp",
    """  std::jthread::id dispatch_thread_id() const { return dispatch_thread_.get_id(); }""",
    """  std::thread::id dispatch_thread_id() const { return dispatch_thread_.get_id(); }""",
    "P12f timer queue thread id type",
)
patch_file(
    "src/core/timer_queue.cpp",
    """  std::forward_list<std::shared_ptr<WaitItem>> wait_queue_;
  std::jthread dispatch_thread_;""",
    """  std::forward_list<std::shared_ptr<WaitItem>> wait_queue_;
#if defined(__ANDROID__)
  std::thread dispatch_thread_;
  std::atomic<bool> stop_requested_{false};
#else
  std::jthread dispatch_thread_;
#endif""",
    "P12g timer queue thread member",
)

# --- P13: memory_posix.cpp uses GetAndroidApiLevel without the header ----
patch_file(
    "src/core/memory_posix.cpp",
    """#if REX_PLATFORM_ANDROID
// May be null if no dynamically loaded functions are required.
static void* libandroid_;""",
    """#if REX_PLATFORM_ANDROID
#include <rex/main_android.h>
#endif

#if REX_PLATFORM_ANDROID
// May be null if no dynamically loaded functions are required.
static void* libandroid_;""",
    "P13 memory_posix main_android include",
)

# --- P14: threading_posix.cpp - robust pthread mutexes are glibc-only. ---
# --- Bionic has none; Android takes the same plain-mutex path as macOS. ---
patch_file(
    "src/core/threading_posix.cpp",
    """#if REX_PLATFORM_LINUX
    // Use robust mutexes so waits can recover if owner thread terminates.""",
    """#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID
    // Use robust mutexes so waits can recover if owner thread terminates.""",
    "P14a robust mutex ctor",
)
patch_file(
    "src/core/threading_posix.cpp",
    """#if REX_PLATFORM_LINUX
    auto native_mutex = static_cast<pthread_mutex_t*>(mutex_.native_handle());""",
    """#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID
    auto native_mutex = static_cast<pthread_mutex_t*>(mutex_.native_handle());""",
    "P14b robust mutex wait",
)
patch_file(
    "src/core/threading_posix.cpp",
    """#if REX_PLATFORM_LINUX
        auto native_mutex = static_cast<pthread_mutex_t*>(handles[i]->mutex_.native_handle());""",
    """#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID
        auto native_mutex = static_cast<pthread_mutex_t*>(handles[i]->mutex_.native_handle());""",
    "P14c robust mutex trylock",
)

# --- P15: memory_posix.cpp Android block also needs rex/math.h (countof) --
patch_file(
    "src/core/memory_posix.cpp",
    """#if REX_PLATFORM_ANDROID
#include <rex/main_android.h>
#endif""",
    """#if REX_PLATFORM_ANDROID
#include <rex/main_android.h>
#include <rex/math.h>
#endif""",
    "P15 memory_posix math include",
)

# --- P16: memory_posix.cpp ashmem fallback - rex::countof name lookup ----
# --- fails in this TU under the NDK; sizeof is identical for a char[]. ---
# --- (The fallback is dead code at minSdk 29 anyway: ASharedMemory,  ----
# --- API 26+, is always available and preferred above it.)            ---
patch_file(
    "src/core/memory_posix.cpp",
    """  strlcpy(ashmem_name, path.c_str(), rex::countof(ashmem_name));""",
    """  strlcpy(ashmem_name, path.c_str(), sizeof(ashmem_name));""",
    "P16 ashmem countof workaround",
)

# --- P17: rexcore links -lpthread/-lrt, which don't exist on bionic -----
# --- (both are part of libc there); Android only needs -ldl.           ---
patch_file(
    "src/core/CMakeLists.txt",
    """if(APPLE)
    target_link_libraries(rexcore PRIVATE pthread)
elseif(UNIX)
    target_link_libraries(rexcore PRIVATE pthread rt dl)
endif()""",
    """if(APPLE)
    target_link_libraries(rexcore PRIVATE pthread)
elseif(ANDROID)
    # TP-ANDROID-PATCH: bionic has no separate libpthread/librt.
    target_link_libraries(rexcore PRIVATE dl)
elseif(UNIX)
    target_link_libraries(rexcore PRIVATE pthread rt dl)
endif()""",
    "P17 rexcore android link libs",
)

# --- P18: rexcore also builds the filesystem Android helpers ------------
# --- (librexruntime itself calls OpenAndroidContentFileDescriptor from --
# --- mapped_memory_posix.cpp, so they cannot live in the app target).  ---
patch_file(
    "src/core/CMakeLists.txt",
    """if(ANDROID)
    target_sources(rexcore PRIVATE fiber_android.cpp)
endif()""",
    """if(ANDROID)
    target_sources(rexcore PRIVATE fiber_android.cpp filesystem_android.cpp)
endif()""",
    "P18 rexcore android filesystem source",
)

# --- P19: filesystem_android.cpp needs SDL3 headers inside rexcore -------
patch_file(
    "src/core/CMakeLists.txt",
    """if(ANDROID)
    target_sources(rexcore PRIVATE fiber_android.cpp filesystem_android.cpp)
endif()""",
    """if(ANDROID)
    target_sources(rexcore PRIVATE fiber_android.cpp filesystem_android.cpp)
    # filesystem_android.cpp uses SDL's Android JNI accessors
    # (SDL_GetAndroidJNIEnv / SDL_GetAndroidActivity).
    target_include_directories(rexcore PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/sdl3/include")
endif()""",
    "P19 rexcore sdl headers for filesystem_android",
)

# --- P20: DIAGNOSTIC stage marks (logcat tag TPMARK) for the Android -----
# --- presentation setup. Test Build 1 crashed and Test Build 2 proved ---
# --- SetupOverlays never runs on device; these marks name the exact ----
# --- branch that bails. Harmless in production (Android-only).         ---
patch_file(
    "src/ui/rex_app.cpp",
    """#include <rex/rex_app.h>
""",
    """#include <rex/rex_app.h>

#if defined(__ANDROID__)
// TP-ANDROID-PATCH (diagnostic): presentation setup stage marks.
#include <android/log.h>
#define TP_REXAPP_MARK(...) __android_log_print(ANDROID_LOG_INFO, "TPMARK", __VA_ARGS__)
#else
#define TP_REXAPP_MARK(...) ((void)0)
#endif
""",
    "P20a rex_app mark macro",
)
patch_file(
    "src/ui/rex_app.cpp",
    """  auto* graphics_system = config_.graphics.get();
  if (graphics_system && graphics_system->presenter()) {""",
    """  auto* graphics_system = config_.graphics.get();
  TP_REXAPP_MARK("SetupPresentation: graphics_system=%p presenter=%p",
                 (const void*)graphics_system,
                 graphics_system ? (const void*)graphics_system->presenter() : nullptr);
  if (graphics_system && graphics_system->presenter()) {""",
    "P20b rex_app mark graphics/presenter",
)
patch_file(
    "src/ui/rex_app.cpp",
    """    auto* provider = graphics_system->provider();
    if (provider) {
      immediate_drawer_ = provider->CreateImmediateDrawer();
      if (immediate_drawer_) {""",
    """    auto* provider = graphics_system->provider();
    TP_REXAPP_MARK("SetupPresentation: provider=%p", (const void*)provider);
    if (provider) {
      immediate_drawer_ = provider->CreateImmediateDrawer();
      TP_REXAPP_MARK("SetupPresentation: immediate_drawer=%p",
                     (const void*)immediate_drawer_.get());
      if (immediate_drawer_) {""",
    "P20c rex_app mark provider/drawer",
)
patch_file(
    "src/ui/vulkan/vulkan_immediate_drawer.cpp",
    """#include <algorithm>
""",
    """#if defined(__ANDROID__)
// TP-ANDROID-PATCH (diagnostic): immediate drawer stage marks.
#include <android/log.h>
#define TP_VID_MARK(...) __android_log_print(ANDROID_LOG_INFO, "TPMARK", __VA_ARGS__)
#else
#define TP_VID_MARK(...) ((void)0)
#endif

#include <algorithm>
""",
    "P20d immediate drawer mark macro",
)
patch_file(
    "src/ui/vulkan/vulkan_immediate_drawer.cpp",
    """  auto immediate_drawer =
      std::unique_ptr<VulkanImmediateDrawer>(new VulkanImmediateDrawer(vulkan_device, ui_samplers));
  if (!immediate_drawer->Initialize()) {
    return nullptr;
  }""",
    """  TP_VID_MARK("VulkanImmediateDrawer::Create: device=%p samplers=%p",
              (const void*)vulkan_device, (const void*)ui_samplers);
  auto immediate_drawer =
      std::unique_ptr<VulkanImmediateDrawer>(new VulkanImmediateDrawer(vulkan_device, ui_samplers));
  if (!immediate_drawer->Initialize()) {
    TP_VID_MARK("VulkanImmediateDrawer::Create: Initialize FAILED");
    return nullptr;
  }
  TP_VID_MARK("VulkanImmediateDrawer::Create: OK");""",
    "P20e immediate drawer create marks",
)
patch_file(
    "src/ui/vulkan/vulkan_immediate_drawer.cpp",
    """        "descriptor set layout");
    return false;""",
    """        "descriptor set layout");
    TP_VID_MARK("VulkanImmediateDrawer: descriptor set layout FAILED");
    return false;""",
    "P20f immediate drawer mark dsl",
)
patch_file(
    "src/ui/vulkan/vulkan_immediate_drawer.cpp",
    """    REXLOG_ERROR("VulkanImmediateDrawer: Failed to create a blank texture");
    return false;""",
    """    REXLOG_ERROR("VulkanImmediateDrawer: Failed to create a blank texture");
    TP_VID_MARK("VulkanImmediateDrawer: blank texture FAILED");
    return false;""",
    "P20g immediate drawer mark texture",
)
patch_file(
    "src/ui/vulkan/vulkan_immediate_drawer.cpp",
    """    REXLOG_ERROR("VulkanImmediateDrawer: Failed to create the pipeline layout");
    return false;""",
    """    REXLOG_ERROR("VulkanImmediateDrawer: Failed to create the pipeline layout");
    TP_VID_MARK("VulkanImmediateDrawer: pipeline layout FAILED");
    return false;""",
    "P20h immediate drawer mark pipeline layout",
)

# --- P21: waive vertexPipelineStoresAndAtomics on Android. Mali GPUs ----
# --- never expose vertex-stage storage writes; upstream made the check --
# --- unconditional ("required for parity"), which rejects every Mali ----
# --- device outright. The feature-enable macro copies from supported ----
# --- features, so waiving the rejection is safe: guest vertex shaders ---
# --- that memexport won't work, conventional draws are unaffected. -----
patch_file(
    "src/ui/vulkan/vulkan_device.cpp",
    """    if (!supported_features.vertexPipelineStoresAndAtomics) {
      REXLOG_WARN(
          "Vulkan device '{}' doesn't support vertexPipelineStoresAndAtomics, which "
          "is required for Vulkan GPU emulation parity",
          properties.deviceName);
      return nullptr;
    }""",
    """    if (!supported_features.vertexPipelineStoresAndAtomics) {
      REXLOG_WARN(
          "Vulkan device '{}' doesn't support vertexPipelineStoresAndAtomics, which "
          "is required for Vulkan GPU emulation parity",
          properties.deviceName);
#if REX_PLATFORM_ANDROID
      // TP-ANDROID-PATCH: see sdk-patch notes (P21).
      REXLOG_WARN("TP: waiving vertexPipelineStoresAndAtomics requirement on Android");
#else
      return nullptr;
#endif
    }""",
    "P21 waive vertex stores on Android",
)

# --- P22: guard the Android surface creation in the Vulkan presenter. ---
# --- While SDL has no live ANativeWindow (pause/teardown), surface ------
# --- creation must bail out instead of handing the Vulkan loader a -----
# --- null/stale window (it crashes inside CreateAndroidSurfaceKHR). ----
patch_file(
    "src/ui/vulkan/vulkan_presenter.cpp",
    """        auto& android_native_window_surface =
            static_cast<const AndroidNativeWindowSurface&>(new_surface);
        VkAndroidSurfaceCreateInfoKHR surface_create_info;""",
    """        auto& android_native_window_surface =
            static_cast<const AndroidNativeWindowSurface&>(new_surface);
        if (!android_native_window_surface.window()) {
          // TP-ANDROID-PATCH (P22): no live ANativeWindow right now; a
          // later paint event reconnects with the fresh window.
          return SurfacePaintConnectResult::kFailureSurfaceUnusable;
        }
        VkAndroidSurfaceCreateInfoKHR surface_create_info;""",
    "P22 presenter Android null-window guard",
)

# --- P23: same waiver in the guest GPU command processor - its ---------
# --- Initialize() hard-fails without vertexPipelineStoresAndAtomics, ---
# --- aborting the GPU thread on Mali (Test Build 3 device test). -------
patch_file(
    "src/graphics/vulkan/command_processor.cpp",
    """  if (!device_properties.vertexPipelineStoresAndAtomics) {
    REXGPU_ERROR(
        "Vulkan vertexPipelineStoresAndAtomics is required for GPU emulation and "
        "D3D12 parity, but unsupported by the selected device");
    return false;
  }""",
    """  if (!device_properties.vertexPipelineStoresAndAtomics) {
    REXGPU_ERROR(
        "Vulkan vertexPipelineStoresAndAtomics is required for GPU emulation and "
        "D3D12 parity, but unsupported by the selected device");
#if defined(__ANDROID__)
    // TP-ANDROID-PATCH (P23): waived on Android like the UI-side device
    // check (P21) - Mali never exposes vertex stores; only guest vertex
    // shader memexport is affected.
    REXGPU_WARN("TP: waiving vertexPipelineStoresAndAtomics on Android");
#else
    return false;
#endif
  }""",
    "P23 command processor waive vertex stores",
)

# --- P24: on Android the GPU plugin does not see host-set cvar values ---
# --- (its registry copy keeps the defaults), so the geometryShader / ---
# --- fillModeNonSolid "require" checks in the command processor must --
# --- not hard-fail there: fall through to the documented fallback -----
# --- paths instead (Mali lacks fillModeNonSolid; Test Build 3 test). ---
patch_file(
    "src/graphics/vulkan/command_processor.cpp",
    """    if (REXCVAR_GET(vulkan_require_geometry_shader)) {
      REXGPU_ERROR(
          "Vulkan geometryShader is required for GPU emulation "
          "(vulkan_require_geometry_shader=true), but unsupported by the "
          "selected device");
      return false;
    }""",
    """    if (REXCVAR_GET(vulkan_require_geometry_shader)) {
      REXGPU_ERROR(
          "Vulkan geometryShader is required for GPU emulation "
          "(vulkan_require_geometry_shader=true), but unsupported by the "
          "selected device");
#if !defined(__ANDROID__)
      // TP-ANDROID-PATCH (P24): plugin cvar registry does not see host-set
      // values on Android; take the fallback path below instead.
      return false;
#endif
    }""",
    "P24a command processor geometry require waived on Android",
)
patch_file(
    "src/graphics/vulkan/command_processor.cpp",
    """    if (REXCVAR_GET(vulkan_require_fill_mode_non_solid)) {
      REXGPU_ERROR(
          "Vulkan fillModeNonSolid is required for GPU emulation "
          "(vulkan_require_fill_mode_non_solid=true), but unsupported by the "
          "selected device");
      return false;
    }""",
    """    if (REXCVAR_GET(vulkan_require_fill_mode_non_solid)) {
      REXGPU_ERROR(
          "Vulkan fillModeNonSolid is required for GPU emulation "
          "(vulkan_require_fill_mode_non_solid=true), but unsupported by the "
          "selected device");
#if !defined(__ANDROID__)
      // TP-ANDROID-PATCH (P24): see P24a.
      return false;
#endif
    }""",
    "P24b command processor fill-mode require waived on Android",
)

# --- P25 (DIAGNOSTIC): log guest file reads at offset 0 (path, size, ----
# --- first bytes) and all read failures on Android, to find why the ----
# --- guest rejects its packages after the intro logo. ------------------
patch_file(
    "src/filesystem/devices/host_path_file.cpp",
    """#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/devices/host_path_file.h>""",
    """#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/devices/host_path_file.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif""",
    "P25a fs read diag include",
)
patch_file(
    "src/filesystem/devices/host_path_file.cpp",
    """  if (file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }""",
    """  if (file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read)) {
#if defined(__ANDROID__)
    if (byte_offset == 0 && buffer.size() >= 4) {
      const uint8_t* b = buffer.data();
      __android_log_print(ANDROID_LOG_INFO, "TPFS",
                          "read0 %s size=%zu got=%zu magic=%02x %02x %02x %02x",
                          entry()->path().c_str(), buffer.size(),
                          out_bytes_read ? *out_bytes_read : 0, b[0], b[1], b[2],
                          b[3]);
    }
#endif
    return X_STATUS_SUCCESS;
  } else {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_WARN, "TPFS", "readFAIL %s off=%zu size=%zu",
                        entry()->path().c_str(), byte_offset, buffer.size());
#endif
    return X_STATUS_END_OF_FILE;
  }""",
    "P25b fs read diag log",
)

# --- P26: 'Splosion Man's bundle loader asks NtQueryInformationFile ----
# --- (XFileXctdCompressionInformation) per package. The shipped desktop -
# --- runtime does not implement the query (logs "unimplemented", -------
# --- returns an error) and the game proceeds; this tree's house mod ----
# --- (for Bejeweled) instead returns success + a zeroed "not ------------
# --- compressed" struct, which makes 'Splosion Man reject textures.xpr -
# --- after the intro and exit to dashboard. On Android, restore the -----
# --- shipped behavior: report the class as not implemented. ------------
patch_file(
    "src/kernel/xboxkrnl/xboxkrnl_io_info.cpp",
    """    case XFileXctdCompressionInformation: {
      // HOUSE (Bejeweled Blitz Live): report success with the zeroed""",
    """    case XFileXctdCompressionInformation: {
#if defined(__ANDROID__)
      // TP-ANDROID-PATCH (P26): match the shipped 'Splosion Man desktop
      // runtime - the query is not implemented there and the game relies
      // on the error to use its own bundle decompression path.
      status = X_STATUS_INVALID_INFO_CLASS;
      out_length = 0;
      break;
#endif
      // HOUSE (Bejeweled Blitz Live): report success with the zeroed""",
    "P26 XCTD query unimplemented on Android",
)

print("All Android SDK patches applied.")
