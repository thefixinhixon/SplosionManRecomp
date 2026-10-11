#pragma once
/**
 ******************************************************************************
 * Android native-window surface for the ReXGlue UI layer.
 *
 * The SDK's Vulkan presenter already knows how to present to
 * kTypeIndex_AndroidNativeWindow, but the surface class itself was never
 * provided upstream. This header (with surface_android.cpp) fills that
 * gap for the Android port: it wraps the ANativeWindow SDL exposes for
 * its window and reports the current pixel size.
 ******************************************************************************
 */

#include <rex/ui/surface.h>

#include <SDL3/SDL.h>

struct ANativeWindow;

namespace rex {
namespace ui {

class AndroidNativeWindowSurface final : public Surface {
 public:
  // The ANativeWindow is owned by SDL and RECREATED across pause/resume,
  // so it is never cached here: window() queries SDL live on every call.
  // (Caching it crashed the Vulkan loader with a stale pointer on the
  // first surface teardown - Test Build 3 device test, 2026-10-10.)
  // `sdl_window` is also used to query the up-to-date pixel size.
  AndroidNativeWindowSurface(ANativeWindow* /*window*/, SDL_Window* sdl_window)
      : sdl_window_(sdl_window) {}
  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  // The CURRENT ANativeWindow for the SDL window, or nullptr while no
  // Android surface exists (paused / being torn down). Callers must check.
  ANativeWindow* window() const {
    if (!sdl_window_) {
      return nullptr;
    }
    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window_);
    return static_cast<ANativeWindow*>(SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
  }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  SDL_Window* sdl_window_;
};

}  // namespace ui
}  // namespace rex
