// splosionman_app.cpp - see splosionman_app.h.

#include "splosionman_app.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include <imgui.h>

#include <rex/audio/downmix.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>

// App-level options (shown in the menu; persisted like any other cvar).
REXCVAR_DEFINE_INT32(perf_overlay, 0, "Recomp",
                     "Performance overlay: 0 = off, 1 = FPS, 2 = detailed")
    .range(0, 2);
REXCVAR_DEFINE_BOOL(menu_pause_game, true, "Recomp",
                    "Pause the game while the in-game options menu is open");
REXCVAR_DEFINE_INT32(vibration_strength, 100, "Recomp", "Controller vibration strength in percent")
    .range(0, 100);
REXCVAR_DEFINE_BOOL(show_menu_hint, true, "Recomp",
                    "Show the 'press F1 / L3+R3 for options' hint at startup");


namespace {

// Every setting the in-game menu can change; these persist in
// recomp_options.toml (see recomp_options.h for launcher precedence).
const std::vector<std::string> kManagedCvars = {
    "frame_limit",          "vsync",
    "vsync_low_latency",    "gpu_max_frames_ahead",
    "perf_overlay",         "menu_pause_game",
    "fullscreen",           "present_effect",
    "present_fsr_sharpness_reduction", "present_cas_additional_sharpness",
    "swap_post_effect",     "resolution_scale",
    "native_2x_msaa",       "anisotropic_override",
    "audio_output_gain",    "audio_mute",
    "audio_force_stereo",   "vibration_strength",
    "mnk_mode",             "show_menu_hint",
};

constexpr uint16_t kChord =
    rex::input::X_INPUT_GAMEPAD_LEFT_THUMB | rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB;

rex::input::InputSystem* GetInput(rex::Runtime* runtime) {
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}

}  // namespace

void SplosionmanApp::OnPostInitLogging() {
  // Paths and logging are ready; the window, GPU and runtime are not yet
  // created, so restart-only options (resolution, MSAA, vsync) still apply.
  store_.LoadAndApply(user_data_root() / "recomp_options.toml", kManagedCvars);
}

void SplosionmanApp::OnConfigureFonts(ImFontAtlas* atlas) {
  // The SDK's default overlay font is a 10 px bitmap font; the menu wants a
  // readable UI font. The atlas is pre-baked (legacy backend), so bake a few
  // sizes and pick one per display height at draw time.
  const char* candidates[] = {
#if defined(_WIN32)
      "C:\\Windows\\Fonts\\segoeui.ttf",
      "C:\\Windows\\Fonts\\arial.ttf",
#else
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
#endif
  };
  std::string path;
  for (const char* c : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(c, ec)) {
      path = c;
      break;
    }
  }
  const float sizes[4] = {17.0f, 22.0f, 30.0f, 40.0f};
  for (int i = 0; i < 4; ++i) {
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.SizePixels = sizes[i];
    ImFont* font = nullptr;
    if (!path.empty()) {
      font = atlas->AddFontFromFileTTF(path.c_str(), sizes[i], &cfg);
    }
    if (!font) {
      font = atlas->AddFontDefault(&cfg);
    }
    fonts_[i] = font;
  }
  REXLOG_INFO("Options menu font: {}", path.empty() ? "built-in" : path);
}

splosion::MenuHost SplosionmanApp::MakeHost() {
  splosion::MenuHost host;
  host.store = &store_;
  host.fonts = fonts_;
  host.poll_pad = [this]() -> uint16_t {
    auto* input = GetInput(runtime());
    if (!input) {
      return 0;
    }
    uint16_t buttons = 0;
    for (uint32_t user = 0; user < 4; ++user) {
      rex::input::X_INPUT_STATE st{};
      if (input->GetPhysicalPadState(user, &st) != 0) {  // X_ERROR_SUCCESS
        continue;
      }
      uint16_t b = st.gamepad.buttons;
      const int lx = st.gamepad.thumb_lx, ly = st.gamepad.thumb_ly;
      constexpr int kStick = 18000;
      if (ly > kStick) b |= rex::input::X_INPUT_GAMEPAD_DPAD_UP;
      if (ly < -kStick) b |= rex::input::X_INPUT_GAMEPAD_DPAD_DOWN;
      if (lx < -kStick) b |= rex::input::X_INPUT_GAMEPAD_DPAD_LEFT;
      if (lx > kStick) b |= rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT;
      buttons |= b;
    }
    return buttons;
  };
  host.frame_stats = [this](void* out) {
    auto* gs = runtime() ? runtime()->graphics_system() : nullptr;
    return gs && gs->GetGuestFrameStats(static_cast<rex::system::GuestFrameStats*>(out));
  };
  host.on_changed = [this](const std::string& cvar) { ApplyLive(cvar); };
  host.request_quit = [this]() {
    app_context().CallInUIThreadDeferred([this]() { QuitGame(); });
  };
  host.request_close = [this]() {
    app_context().CallInUIThreadDeferred([this]() { CloseMenu(); });
  };
  host.achievements = [this]() { return ListAchievements(); };
  return host;
}

// The game's achievements (from its XEX) with their unlock state. UI thread.
std::vector<splosion::AchievementRow> SplosionmanApp::ListAchievements() {
  std::vector<splosion::AchievementRow> rows;
  if (!runtime() || !runtime()->kernel_state()) {
    return rows;
  }
  if (!icon_cache_ && immediate_drawer()) {
    icon_cache_ = std::make_unique<rex::ui::AchievementIconCache>(immediate_drawer(), runtime());
  }
  const auto& manager = achievements();
  for (const auto& info : manager.ListAchievements()) {
    splosion::AchievementRow row;
    row.name = info.label;
    row.gamerscore = info.gamerscore;
    row.unlocked = manager.IsUnlocked(info.id);
    row.unlocked_at = row.unlocked ? manager.GetUnlockTime(info.id) : 0;
    // XDBF flag 0x8: the achievement is shown before it is unlocked; without it, it's secret.
    row.secret = (info.flags & 0x8) == 0;
    row.description = row.unlocked || info.unachieved_description.empty()
                          ? info.description
                          : info.unachieved_description;
    row.icon = icon_cache_ ? icon_cache_->GetIcon(info) : nullptr;
    rows.push_back(std::move(row));
  }
  return rows;
}

void SplosionmanApp::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  (void)drawer;
  rex::ui::RegisterBind("bind_recomp_options", "F1", "Toggle the recomp options menu",
                        [this] { ToggleMenu(); });
  SyncPerfOverlay();
  if (REXCVAR_GET(show_menu_hint)) {
    hint_ = std::make_unique<splosion::HintToast>(imgui_drawer(), MakeHost(), [this]() {
      app_context().CallInUIThreadDeferred([this]() { hint_.reset(); });
    });
  }
}

void SplosionmanApp::OnPostSetup() {
  // AMD/RADV fix: unclipped draw shaders cause blank screen during
  // gameplay on AMD GPUs. Execute them on CPU instead.
  rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu", "true");
  InstallInputFilters();
}

void SplosionmanApp::InstallInputFilters() {
  auto* input = GetInput(runtime());
  if (!input) {
    return;
  }
  // Runs on the guest thread that polls XInput.
  input->SetStateFilter([this](uint32_t user, rex::input::X_INPUT_STATE& st) {
    const uint16_t buttons = st.gamepad.buttons;
    if (user < chord_down_.size()) {
      const bool chord = (buttons & kChord) == kChord;
      if (chord && !chord_down_[user].exchange(true)) {
        app_context().CallInUIThread([this]() { ToggleMenu(); });
      } else if (!chord) {
        chord_down_[user] = false;
      }
    }
    if (suppress_until_release_.load() && !menu_open_.load()) {
      const bool idle = buttons == 0 && st.gamepad.left_trigger < 30 &&
                        st.gamepad.right_trigger < 30;
      if (idle) {
        suppress_until_release_ = false;
      }
    }
    if (menu_open_.load() || suppress_until_release_.load()) {
      st.gamepad.buttons = 0;
      st.gamepad.left_trigger = 0;
      st.gamepad.right_trigger = 0;
      st.gamepad.thumb_lx = 0;
      st.gamepad.thumb_ly = 0;
      st.gamepad.thumb_rx = 0;
      st.gamepad.thumb_ry = 0;
    }
  });
  input->SetVibrationFilter([this](uint32_t, rex::input::X_INPUT_VIBRATION& v) {
    const int pct = menu_open_.load() ? 0 : REXCVAR_GET(vibration_strength);
    v.left_motor_speed = uint16_t(uint32_t(uint16_t(v.left_motor_speed)) * uint32_t(pct) / 100u);
    v.right_motor_speed = uint16_t(uint32_t(uint16_t(v.right_motor_speed)) * uint32_t(pct) / 100u);
  });
}

void SplosionmanApp::ToggleMenu() {
  if (menu_) {
    CloseMenu();
  } else {
    OpenMenu();
  }
}

void SplosionmanApp::OpenMenu() {
  if (menu_ || !imgui_drawer()) {
    return;
  }
  hint_.reset();
  menu_open_ = true;
  menu_ = std::make_unique<splosion::OptionsMenu>(imgui_drawer(), MakeHost());
  if (auto* input = GetInput(runtime())) {
    // Stop any rumble that was playing; the filter keeps it at zero while open.
    for (uint32_t user = 0; user < 4; ++user) {
      rex::input::X_INPUT_VIBRATION off{};
      input->SetState(user, &off);
    }
  }
  if (REXCVAR_GET(menu_pause_game) && runtime() && runtime()->graphics_system()) {
    runtime()->graphics_system()->SetGuestFrameHold(true);
    frame_held_ = true;
  }
}

void SplosionmanApp::CloseMenu() {
  if (!menu_) {
    return;
  }
  menu_.reset();
  suppress_until_release_ = true;
  menu_open_ = false;
  if (frame_held_ && runtime() && runtime()->graphics_system()) {
    runtime()->graphics_system()->SetGuestFrameHold(false);
  }
  frame_held_ = false;
  store_.SaveIfDirty();
}

void SplosionmanApp::QuitGame() {
  REXLOG_INFO("Quit requested from the options menu");
  CloseMenu();  // releases the frame hold and saves the options
  store_.SaveIfDirty();
  // Same path as closing the window (title shutdown, then process exit).
  if (window()) {
    window()->RequestClose();
  } else {
    app_context().QuitFromUIThread();
  }
}

void SplosionmanApp::SyncPerfOverlay() {
  const bool want = REXCVAR_GET(perf_overlay) > 0;
  if (want && !perf_overlay_ && imgui_drawer()) {
    perf_overlay_ = std::make_unique<splosion::PerfOverlay>(imgui_drawer(), MakeHost());
  } else if (!want && perf_overlay_) {
    // Destroyed after the current ImGui frame: we may be inside a draw.
    app_context().CallInUIThreadDeferred([this]() {
      if (REXCVAR_GET(perf_overlay) == 0) {
        perf_overlay_.reset();
      }
    });
  }
}

void SplosionmanApp::ApplyLive(const std::string& cvar) {
  const bool all = cvar == "*";
  auto* gs = runtime() ? runtime()->graphics_system() : nullptr;
  auto* presenter = gs ? gs->presenter() : nullptr;
  if (all || cvar == "vsync" || cvar == "vsync_low_latency") {
    // Swapchain present mode: recreate it outside the ImGui draw.
    if (presenter) {
      app_context().CallInUIThreadDeferred([presenter]() { presenter->OnSurfaceResizeFromUIThread(); });
    }
  }
  if (all || cvar.rfind("present_", 0) == 0) {
    if (presenter) {
      presenter->ReloadGuestOutputPaintConfigFromCVarsFromUIThread();
    }
  }
  if (all || cvar == "fullscreen") {
    const bool fs = rex::cvar::Query<bool>("fullscreen");
    app_context().CallInUIThreadDeferred([this, fs]() {
      if (window()) {
        window()->SetFullscreen(fs);
      }
    });
  }
  if (all || cvar == "audio_output_gain") {
    rex::audio::SetOutputGain(float(rex::cvar::Query<double>("audio_output_gain")));
  }
  if (all || cvar == "perf_overlay") {
    SyncPerfOverlay();
  }
  if (cvar == "menu_pause_game" && menu_ && gs) {
    frame_held_ = REXCVAR_GET(menu_pause_game);
    gs->SetGuestFrameHold(frame_held_);
  }
}

void SplosionmanApp::OnShutdown() {
  rex::ui::UnregisterBind("bind_recomp_options");
  if (runtime() && runtime()->graphics_system()) {
    runtime()->graphics_system()->SetGuestFrameHold(false);
  }
  menu_.reset();
  perf_overlay_.reset();
  hint_.reset();
  icon_cache_.reset();
  store_.SaveIfDirty();
}
