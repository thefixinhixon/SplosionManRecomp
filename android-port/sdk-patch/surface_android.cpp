/**
 ******************************************************************************
 * Android native-window surface - see surface_android.h.
 ******************************************************************************
 */

#include <rex/ui/surface_android.h>

#include <SDL3/SDL.h>

namespace rex {
namespace ui {

bool AndroidNativeWindowSurface::GetSizeImpl(uint32_t& width_out,
                                             uint32_t& height_out) const {
  int w = 0, h = 0;
  if (!sdl_window_ || !SDL_GetWindowSizeInPixels(sdl_window_, &w, &h) || w <= 0 ||
      h <= 0) {
    return false;
  }
  width_out = uint32_t(w);
  height_out = uint32_t(h);
  return true;
}

}  // namespace ui
}  // namespace rex
