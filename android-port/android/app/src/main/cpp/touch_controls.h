// touch_controls.h - on-screen gamepad for the Android port.
//
// An ImGui dialog that renders a transparent, full-screen control layer
// (left analog stick with D-pad emulation, A/B/X/Y diamond, a big SPLODE
// button, LB/RB, Start/Back, and a show/hide pill) and feeds it into an
// SDL virtual gamepad, so the ReXGlue SDL input driver treats the touch
// layer exactly like a connected Xbox 360 pad - including rumble routing
// back through SDL (dropped here; a screen can't rumble the player).
//
// Input is polled from raw SDL touch fingers (true multitouch), NOT from
// ImGui's single-pointer mouse emulation: you can hold the stick with one
// thumb and mash SPLODE with the other.
//
// 'Splosion Man specifics: every face button "'splodes" in game, so the
// diamond doubles as menu navigation (A = confirm/SPLODE, B = back) and
// the big SPLODE button is the one you actually play with.
#pragma once

#include <atomic>

#include <rex/ui/imgui_dialog.h>

namespace tp {

// The app flips this on once the game is actually running (assets loaded,
// runtime up). The dialog renders nothing while it is off, which keeps the
// controls out of the asset loader wizard.
inline std::atomic<bool>& ControlsVisible() {
  static std::atomic<bool> visible{false};
  return visible;
}

}  // namespace tp

class TouchControlsDialog : public rex::ui::ImGuiDialog {
 public:
  explicit TouchControlsDialog(rex::ui::ImGuiDrawer* imgui_drawer)
      : rex::ui::ImGuiDialog(imgui_drawer) {}
  ~TouchControlsDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Finger {
    uint64_t id = 0;
    float x = 0;
    float y = 0;
  };

  void EnsureVirtualPad();
  void ReleaseVirtualPad();
  void PushPadState();

  // Virtual gamepad plumbing (opaque SDL handles kept as void* so this
  // header stays SDL-free).
  void* joystick_ = nullptr;   // SDL_Joystick*
  unsigned int joystick_id_ = 0;  // SDL_JoystickID

  // Stick state.
  bool stick_claimed_ = false;
  uint64_t stick_finger_ = 0;
  float stick_origin_x_ = 0;
  float stick_origin_y_ = 0;
  float stick_dx_ = 0;  // -1..1
  float stick_dy_ = 0;

  // Current gamepad outputs (SDL_GamepadButton indices in a bitmask).
  unsigned int buttons_ = 0;
  float axis_lx_ = 0;
  float axis_ly_ = 0;

  bool collapsed_ = false;         // Only the show/hide pill is drawn.
  bool collapse_was_down_ = false; // Edge detection for the pill.
};
