// touch_controls.cpp - see touch_controls.h.
#include "touch_controls.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <rex/logging.h>

namespace {

struct ButtonDef {
  SDL_GamepadButton button;
  const char* label;
  // Position as fractions of the display (resolved per frame), radius in
  // reference pixels (scaled by display height / 720).
  float fx;
  float fy;
  float radius;
  ImU32 color;
};

// Right-hand diamond + shoulders. The big SPLODE button is separate.
constexpr ButtonDef kButtons[] = {
    {SDL_GAMEPAD_BUTTON_NORTH, "Y", 0.905f, 0.52f, 34.0f, IM_COL32(240, 200, 40, 255)},
    {SDL_GAMEPAD_BUTTON_WEST, "X", 0.845f, 0.63f, 34.0f, IM_COL32(60, 130, 240, 255)},
    {SDL_GAMEPAD_BUTTON_EAST, "B", 0.965f, 0.63f, 34.0f, IM_COL32(230, 60, 50, 255)},
    {SDL_GAMEPAD_BUTTON_SOUTH, "A", 0.905f, 0.74f, 34.0f, IM_COL32(90, 200, 90, 255)},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB", 0.80f, 0.16f, 26.0f, IM_COL32(200, 200, 200, 255)},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB", 0.955f, 0.16f, 26.0f, IM_COL32(200, 200, 200, 255)},
    {SDL_GAMEPAD_BUTTON_START, "START", 0.585f, 0.90f, 24.0f, IM_COL32(200, 200, 200, 255)},
    {SDL_GAMEPAD_BUTTON_BACK, "BACK", 0.415f, 0.90f, 24.0f, IM_COL32(200, 200, 200, 255)},
};

// The SPLODE button: bottom-right, deliberately the biggest target.
constexpr float kSplodeFx = 0.875f;
constexpr float kSplodeFy = 0.90f;
constexpr float kSplodeRadius = 52.0f;

float Dist(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  return std::sqrt(dx * dx + dy * dy);
}

void DrawButton(ImDrawList* draw, float cx, float cy, float radius,
                const char* label, bool pressed, ImU32 accent) {
  ImU32 fill = pressed ? IM_COL32(255, 120, 30, 190) : IM_COL32(20, 20, 24, 110);
  draw->AddCircleFilled(ImVec2(cx, cy), radius, fill, 40);
  draw->AddCircle(ImVec2(cx, cy), radius, accent, 40, 2.5f);
  ImVec2 text_size = ImGui::CalcTextSize(label);
  draw->AddText(ImVec2(cx - text_size.x * 0.5f, cy - text_size.y * 0.5f),
                IM_COL32(255, 255, 255, 235), label);
}

}  // namespace

TouchControlsDialog::~TouchControlsDialog() { ReleaseVirtualPad(); }

void TouchControlsDialog::EnsureVirtualPad() {
  if (joystick_) return;
  if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  // Present as an Xbox 360 pad so SDL's built-in gamepad mapping applies.
  desc.vendor_id = 0x045E;
  desc.product_id = 0x028E;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.button_mask = 0xFFFFFFFFu;
  desc.axis_mask = 0xFFFFFFFFu;
  desc.name = "Splosion Touch Pad";
  joystick_id_ = SDL_AttachVirtualJoystick(&desc);
  if (joystick_id_ == 0) {
    REXLOG_ERROR("Touch controls: SDL_AttachVirtualJoystick failed: {}", SDL_GetError());
    return;
  }
  joystick_ = SDL_OpenJoystick(joystick_id_);
  REXLOG_INFO("Touch controls: virtual gamepad attached (id {})", joystick_id_);
}

void TouchControlsDialog::ReleaseVirtualPad() {
  if (joystick_) {
    SDL_CloseJoystick(static_cast<SDL_Joystick*>(joystick_));
    joystick_ = nullptr;
  }
  if (joystick_id_ != 0) {
    SDL_DetachVirtualJoystick(joystick_id_);
    joystick_id_ = 0;
  }
}

void TouchControlsDialog::PushPadState() {
  if (!joystick_) return;
  auto* joy = static_cast<SDL_Joystick*>(joystick_);
  auto to_axis = [](float v) {
    return static_cast<Sint16>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
  };
  SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, to_axis(axis_lx_));
  SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, to_axis(axis_ly_));
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
    SDL_SetJoystickVirtualButton(joy, b, (buttons_ & (1u << b)) != 0);
  }
}

void TouchControlsDialog::OnDraw(ImGuiIO& io) {
  if (!tp::ControlsVisible().load()) {
    return;
  }
  EnsureVirtualPad();

  const float width = io.DisplaySize.x;
  const float height = io.DisplaySize.y;
  if (width <= 0 || height <= 0) return;
  const float scale = height / 720.0f;

  // ---- Gather fingers -------------------------------------------------
  std::vector<Finger> fingers;
  int device_count = 0;
  SDL_TouchID* devices = SDL_GetTouchDevices(&device_count);
  if (devices) {
    for (int d = 0; d < device_count; ++d) {
      int finger_count = 0;
      SDL_Finger** sdl_fingers = SDL_GetTouchFingers(devices[d], &finger_count);
      if (sdl_fingers) {
        for (int i = 0; i < finger_count; ++i) {
          fingers.push_back({sdl_fingers[i]->id, sdl_fingers[i]->x * width,
                             sdl_fingers[i]->y * height});
        }
        SDL_free(sdl_fingers);
      }
    }
    SDL_free(devices);
  }

  ImDrawList* draw = ImGui::GetForegroundDrawList();

  // ---- Show/hide pill (top center) ------------------------------------
  const float pill_x = width * 0.5f, pill_y = height * 0.055f, pill_r = 20.0f * scale;
  bool pill_down = false;
  for (const Finger& f : fingers) {
    if (Dist(f.x, f.y, pill_x, pill_y) <= pill_r * 1.6f) pill_down = true;
  }
  if (pill_down && !collapse_was_down_) collapsed_ = !collapsed_;
  collapse_was_down_ = pill_down;
  draw->AddCircleFilled(ImVec2(pill_x, pill_y), pill_r, IM_COL32(20, 20, 24, 110), 24);
  draw->AddCircle(ImVec2(pill_x, pill_y), pill_r, IM_COL32(255, 255, 255, 160), 24, 2.0f);
  {
    const char* label = collapsed_ ? "PAD" : "HIDE";
    ImVec2 ts = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(pill_x - ts.x * 0.5f, pill_y - ts.y * 0.5f),
                  IM_COL32(255, 255, 255, 220), label);
  }
  if (collapsed_) {
    buttons_ = 0;
    axis_lx_ = axis_ly_ = 0;
    stick_claimed_ = false;
    PushPadState();
    return;
  }

  // ---- Stick -----------------------------------------------------------
  const float stick_zone_w = width * 0.42f;
  const float stick_zone_y0 = height * 0.30f;
  const float stick_radius = 95.0f * scale;

  // Release the stick if its finger vanished.
  if (stick_claimed_) {
    bool found = false;
    for (const Finger& f : fingers) {
      if (f.id == stick_finger_) found = true;
    }
    if (!found) {
      stick_claimed_ = false;
      stick_dx_ = stick_dy_ = 0;
    }
  }
  // Claim a new finger landing in the stick zone.
  if (!stick_claimed_) {
    for (const Finger& f : fingers) {
      if (f.x < stick_zone_w && f.y > stick_zone_y0) {
        stick_claimed_ = true;
        stick_finger_ = f.id;
        stick_origin_x_ = f.x;
        stick_origin_y_ = f.y;
        stick_dx_ = stick_dy_ = 0;
        break;
      }
    }
  }
  if (stick_claimed_) {
    for (const Finger& f : fingers) {
      if (f.id == stick_finger_) {
        float dx = (f.x - stick_origin_x_) / stick_radius;
        float dy = (f.y - stick_origin_y_) / stick_radius;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0f) {
          dx /= len;
          dy /= len;
        }
        stick_dx_ = dx;
        stick_dy_ = dy;
        // Recentring origin if the finger drags past the rim keeps the
        // stick feeling like a real floating stick.
        if (len > 1.0f) {
          stick_origin_x_ = f.x - dx * stick_radius;
          stick_origin_y_ = f.y - dy * stick_radius;
        }
      }
    }
    // Draw base + nub.
    draw->AddCircleFilled(ImVec2(stick_origin_x_, stick_origin_y_),
                          stick_radius, IM_COL32(20, 20, 24, 90), 48);
    draw->AddCircle(ImVec2(stick_origin_x_, stick_origin_y_), stick_radius,
                    IM_COL32(255, 255, 255, 150), 48, 2.5f);
    draw->AddCircleFilled(
        ImVec2(stick_origin_x_ + stick_dx_ * stick_radius * 0.55f,
               stick_origin_y_ + stick_dy_ * stick_radius * 0.55f),
        34.0f * scale, IM_COL32(255, 120, 30, 170), 32);
  } else {
    // Idle hint: faint base at the home position.
    float home_x = width * 0.17f, home_y = height * 0.68f;
    draw->AddCircle(ImVec2(home_x, home_y), stick_radius,
                    IM_COL32(255, 255, 255, 45), 48, 2.0f);
    ImVec2 ts = ImGui::CalcTextSize("MOVE");
    draw->AddText(ImVec2(home_x - ts.x * 0.5f, home_y - ts.y * 0.5f),
                  IM_COL32(255, 255, 255, 70), "MOVE");
  }

  axis_lx_ = stick_dx_;
  axis_ly_ = stick_dy_;
  // Deadzone.
  if (std::abs(axis_lx_) < 0.14f) axis_lx_ = 0;
  if (std::abs(axis_ly_) < 0.14f) axis_ly_ = 0;

  // ---- Buttons ----------------------------------------------------------
  buttons_ = 0;
  // D-pad emulation from the stick (menus read either).
  constexpr float kDpadThreshold = 0.55f;
  if (stick_dy_ < -kDpadThreshold) buttons_ |= 1u << SDL_GAMEPAD_BUTTON_DPAD_UP;
  if (stick_dy_ > kDpadThreshold) buttons_ |= 1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN;
  if (stick_dx_ < -kDpadThreshold) buttons_ |= 1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT;
  if (stick_dx_ > kDpadThreshold) buttons_ |= 1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

  auto finger_presses = [&](float cx, float cy, float r) {
    for (const Finger& f : fingers) {
      if (stick_claimed_ && f.id == stick_finger_) continue;
      if (Dist(f.x, f.y, cx, cy) <= r * 1.25f) return true;
    }
    return false;
  };

  for (const ButtonDef& def : kButtons) {
    float cx = def.fx * width, cy = def.fy * height, r = def.radius * scale;
    bool pressed = finger_presses(cx, cy, r);
    if (pressed) buttons_ |= 1u << def.button;
    DrawButton(draw, cx, cy, r, def.label, pressed, def.color);
  }

  // SPLODE!
  {
    float cx = kSplodeFx * width, cy = kSplodeFy * height, r = kSplodeRadius * scale;
    bool pressed = finger_presses(cx, cy, r);
    if (pressed) buttons_ |= 1u << SDL_GAMEPAD_BUTTON_SOUTH;
    ImU32 fill = pressed ? IM_COL32(255, 90, 10, 210) : IM_COL32(200, 60, 10, 120);
    draw->AddCircleFilled(ImVec2(cx, cy), r, fill, 48);
    draw->AddCircle(ImVec2(cx, cy), r, IM_COL32(255, 190, 60, 255), 48, 3.0f);
    ImVec2 ts = ImGui::CalcTextSize("SPLODE");
    draw->AddText(ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f),
                  IM_COL32(255, 255, 255, 245), "SPLODE");
  }

  PushPadState();
}
