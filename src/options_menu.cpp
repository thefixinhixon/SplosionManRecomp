// options_menu.cpp - see options_menu.h.

#include "options_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/system/interfaces/graphics.h>

#include "recomp_options.h"

namespace splosion {

namespace {

using Clock = std::chrono::steady_clock;

// Palette: the game's logo orange on a dark steel panel.
const ImVec4 kAccent(1.00f, 0.42f, 0.10f, 1.00f);
const ImVec4 kAccentDim(1.00f, 0.42f, 0.10f, 0.35f);
const ImVec4 kPanel(0.06f, 0.07f, 0.09f, 0.94f);
const ImVec4 kText(0.93f, 0.94f, 0.96f, 1.00f);
const ImVec4 kTextDim(0.62f, 0.65f, 0.70f, 1.00f);
const ImVec4 kWarn(1.00f, 0.80f, 0.30f, 1.00f);
const ImVec4 kGood(0.45f, 0.90f, 0.55f, 1.00f);

bool ValuesEqual(const std::string& a, const std::string& b) {
  if (a == b) {
    return true;
  }
  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  std::string la = lower(a), lb = lower(b);
  auto as_bool = [](const std::string& s, int& out) {
    if (s == "true") return (out = 1), true;
    if (s == "false") return (out = 0), true;
    return false;
  };
  int ba, bb;
  if (as_bool(la, ba) && as_bool(lb, bb)) {
    return ba == bb;
  }
  char* end_a = nullptr;
  char* end_b = nullptr;
  double da = std::strtod(a.c_str(), &end_a);
  double db = std::strtod(b.c_str(), &end_b);
  if (end_a && *end_a == '\0' && end_b && *end_b == '\0' && !a.empty() && !b.empty()) {
    return std::fabs(da - db) < 1e-4;
  }
  return la == lb;
}

using Choice = OptionsMenu::Choice;
using Item = OptionsMenu::Item;

Choice C(std::string label, std::vector<std::pair<std::string, std::string>> sets) {
  return Choice{std::move(label), std::move(sets)};
}

std::vector<Choice> OnOff(const std::string& cvar) {
  return {C("Off", {{cvar, "false"}}), C("On", {{cvar, "true"}})};
}

std::vector<OptionsMenu::Page> BuildPages() {
  std::vector<OptionsMenu::Page> pages;

  // --- Game ----------------------------------------------------------------------------------
  {
    OptionsMenu::Page p{"Game", {}};
    Item resume;
    resume.label = "Resume";
    resume.help = "Close this menu and go back to the game.";
    resume.act = Item::Act::kResume;
    p.items.push_back(resume);
    Item quit;
    quit.label = "Quit game";
    quit.help = "Save the recomp options and exit to the desktop. Press twice to confirm. "
                "Game progress the game has not saved yet is lost.";
    quit.act = Item::Act::kQuit;
    p.items.push_back(quit);
    pages.push_back(std::move(p));
  }

  // --- Achievements (rows filled from the game's list each frame) ---------------------------
  pages.push_back(OptionsMenu::Page{"Achievements", {}, true});

  // --- Performance -------------------------------------------------------------------------
  {
    OptionsMenu::Page p{"Performance", {}};
    std::vector<Choice> limits;
    for (int fps : {30, 60, 75, 90, 120, 144, 165, 180, 240, 360}) {
      limits.push_back(C(std::to_string(fps) + " FPS", {{"frame_limit", std::to_string(fps)}}));
    }
    limits.push_back(C("Unlimited", {{"frame_limit", "0"}}));
    p.items.push_back(
        {"Frame rate limit",
         "Caps how fast the game runs. The game's physics use real elapsed time, so any rate "
         "plays at the correct speed. Set it to your monitor's refresh rate (or a little "
         "below it with VRR/G-Sync/FreeSync) for the smoothest, lowest-latency result. The "
         "limiter waits before the game reads your controller, not after, so a cap adds "
         "almost no input lag.",
         limits});
    p.items.push_back(
        {"VSync",
         "Off: lowest latency, may tear.\nOn - low latency: no tearing; newer frames replace "
         "queued ones (mailbox), so the display never shows a stale frame. Recommended.\n"
         "On - standard: classic FIFO vsync; smoothest on fixed-refresh displays without "
         "VRR, but adds up to a few frames of input lag.",
         {C("Off", {{"vsync", "false"}}),
          C("On - low latency", {{"vsync", "true"}, {"vsync_low_latency", "true"}}),
          C("On - standard", {{"vsync", "true"}, {"vsync_low_latency", "false"}})}});
    p.items.push_back(
        {"Frame queue",
         "How many frames the game may prepare ahead of the GPU.\n1 frame: lowest input lag "
         "(recommended).\n2 frames: smoother on slow GPUs, about one frame more lag.\n"
         "Unbounded: the original behaviour.",
         {C("1 frame (lowest lag)", {{"gpu_max_frames_ahead", "1"}}),
          C("2 frames", {{"gpu_max_frames_ahead", "2"}}),
          C("Unbounded", {{"gpu_max_frames_ahead", "0"}})}});
    p.items.push_back({"Performance overlay",
                       "Shows the frame rate in the top-left corner. Detailed adds frame time, "
                       "CPU time per frame, time spent waiting on the GPU and a frame-time "
                       "graph.",
                       {C("Off", {{"perf_overlay", "0"}}), C("FPS", {{"perf_overlay", "1"}}),
                        C("Detailed", {{"perf_overlay", "2"}})}});
    p.items.push_back({"Pause game while this menu is open",
                       "Freezes the game on its last frame while you change options. It "
                       "resumes exactly where it stopped.",
                       OnOff("menu_pause_game")});
    p.items.push_back({"Display mode", "Fullscreen or windowed. Alt+Enter also toggles.",
                       {C("Windowed", {{"fullscreen", "false"}}),
                        C("Fullscreen", {{"fullscreen", "true"}})}});
    pages.push_back(std::move(p));
  }

  // --- Graphics ----------------------------------------------------------------------------
  {
    OptionsMenu::Page p{"Graphics", {}};
    p.items.push_back(
        {"Upscaling",
         "How the game image is scaled to your screen.\nBilinear: plain scaling.\nAMD FSR 1: "
         "edge-aware upscaling, sharpest at 1080p and above.\nAMD CAS: contrast-adaptive "
         "sharpening with plain scaling. Applies immediately.",
         {C("Bilinear", {{"present_effect", "bilinear"}}),
          C("AMD FSR 1", {{"present_effect", "fsr"}}),
          C("AMD CAS", {{"present_effect", "cas"}})}});
    Item fsr_sharp{"Sharpness",
                   "FSR sharpening strength (RCAS). Applies immediately.",
                   {C("Off", {{"present_fsr_sharpness_reduction", "2.0"}}),
                    C("Low", {{"present_fsr_sharpness_reduction", "1.0"}}),
                    C("Medium", {{"present_fsr_sharpness_reduction", "0.5"}}),
                    C("High", {{"present_fsr_sharpness_reduction", "0.2"}}),
                    C("Maximum", {{"present_fsr_sharpness_reduction", "0.0"}})}};
    fsr_sharp.visible = [] { return OptionsStore::Get("present_effect") == "fsr"; };
    p.items.push_back(fsr_sharp);
    Item cas_sharp{"Sharpness",
                   "Extra CAS sharpening on top of the base amount. Applies immediately.",
                   {C("Base", {{"present_cas_additional_sharpness", "0.0"}}),
                    C("Low", {{"present_cas_additional_sharpness", "0.25"}}),
                    C("Medium", {{"present_cas_additional_sharpness", "0.5"}}),
                    C("High", {{"present_cas_additional_sharpness", "0.75"}}),
                    C("Maximum", {{"present_cas_additional_sharpness", "1.0"}})}};
    cas_sharp.visible = [] { return OptionsStore::Get("present_effect") == "cas"; };
    p.items.push_back(cas_sharp);
    p.items.push_back({"Anti-aliasing (post)",
                       "FXAA smooths jagged edges at a tiny GPU cost. Extreme is stronger and "
                       "softer. Applies immediately.",
                       {C("Off", {{"swap_post_effect", "none"}}),
                        C("FXAA", {{"swap_post_effect", "fxaa"}}),
                        C("FXAA extreme", {{"swap_post_effect", "fxaa_extreme"}})}});
    Item res{"Internal resolution",
             "Renders the game at a multiple of its native 720p. Higher is sharper and costs "
             "GPU time. Takes effect after restarting the game.",
             {C("1x (720p)", {{"resolution_scale", "1"}}),
              C("2x (1440p)", {{"resolution_scale", "2"}}),
              C("3x (2160p)", {{"resolution_scale", "3"}})}};
    res.restart = true;
    p.items.push_back(res);
    Item msaa{"Native 2x MSAA",
              "Uses the host GPU's 2x MSAA for the game's multisampled surfaces (as on the "
              "Xbox 360). Takes effect after restarting the game.",
              OnOff("native_2x_msaa")};
    msaa.restart = true;
    p.items.push_back(msaa);
    p.items.push_back({"Anisotropic filtering",
                       "Keeps floor and wall textures sharp at shallow angles. Very cheap on "
                       "modern GPUs. Fully applies from the next level load.",
                       {C("Game default", {{"anisotropic_override", "-1"}}),
                        C("Off", {{"anisotropic_override", "0"}}),
                        C("2x", {{"anisotropic_override", "2"}}),
                        C("4x", {{"anisotropic_override", "3"}}),
                        C("8x", {{"anisotropic_override", "4"}}),
                        C("16x", {{"anisotropic_override", "5"}})}});
    pages.push_back(std::move(p));
  }

  // --- Audio -------------------------------------------------------------------------------
  {
    OptionsMenu::Page p{"Audio", {}};
    std::vector<Choice> vol;
    for (int pct = 0; pct <= 200; pct += 10) {
      char v[16];
      std::snprintf(v, sizeof(v), "%.2f", pct / 100.0);
      vol.push_back(C(std::to_string(pct) + "%", {{"audio_output_gain", v}}));
    }
    p.items.push_back({"Master volume",
                       "Overall output volume. Above 100% boosts quiet mixes but can clip. "
                       "Applies immediately.",
                       vol});
    p.items.push_back({"Mute", "Silences all game audio.", OnOff("audio_mute")});
    Item stereo{"Force stereo",
                "Folds the 5.1 mix down to stereo even on a surround device (useful for "
                "headphones on a surround-configured output). Takes effect after restart.",
                OnOff("audio_force_stereo")};
    stereo.restart = true;
    p.items.push_back(stereo);
    pages.push_back(std::move(p));
  }

  // --- Controls ----------------------------------------------------------------------------
  {
    OptionsMenu::Page p{"Controls", {}};
    std::vector<Choice> rumble;
    for (int pct : {0, 25, 50, 75, 100}) {
      rumble.push_back(C(pct ? std::to_string(pct) + "%" : std::string("Off"),
                         {{"vibration_strength", std::to_string(pct)}}));
    }
    p.items.push_back({"Vibration", "Controller rumble strength.", rumble});
    p.items.push_back({"Keyboard as controller",
                       "Play with the keyboard: WASD move, Space / ; jump ('splode), Enter "
                       "start, Tab back. Works alongside a controller.",
                       OnOff("mnk_mode")});
    p.items.push_back({"Mouse cursor",
                       "Hide when idle: the cursor appears when you move the mouse and hides "
                       "again after 2 seconds.\nAlways hide: never shown over the game, except "
                       "while this menu is open.\nAlways show: the cursor stays visible.",
                       {C("Always show", {{"mouse_cursor", "0"}}),
                        C("Hide when idle", {{"mouse_cursor", "1"}}),
                        C("Always hide", {{"mouse_cursor", "2"}})}});
    Item hint;
    hint.label = "Open this menu: F1, or click both sticks (L3 + R3)";
    hint.info_only = true;
    p.items.push_back(hint);
    Item hint2;
    hint2.label = "Settings are saved automatically";
    hint2.help =
        "Saved to recomp_options.toml next to your saves. If you later change the "
        "same setting in the launcher, the launcher's value wins.";
    hint2.info_only = true;
    p.items.push_back(hint2);
    pages.push_back(std::move(p));
  }
  return pages;
}

// "Oct 5, 2026" from a Windows FILETIME (100 ns ticks since 1601), in local time.
std::string FormatFileTime(uint64_t filetime) {
  if (filetime == 0) {
    return {};
  }
  const std::time_t t = static_cast<std::time_t>(filetime / 10000000ull) - 11644473600ll;
  std::tm tm{};
#ifdef _WIN32
  if (localtime_s(&tm, &t) != 0) return {};
#else
  if (!localtime_r(&t, &tm)) return {};
#endif
  char month[16];
  if (!std::strftime(month, sizeof(month), "%b", &tm)) return {};
  return std::string(month) + " " + std::to_string(tm.tm_mday) + ", " +
         std::to_string(tm.tm_year + 1900);
}

void TextCentered(const char* text, const ImVec4& color) {
  float w = ImGui::CalcTextSize(text).x;
  ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - w) * 0.5f));
  ImGui::TextColored(color, "%s", text);
}

}  // namespace

ImFont* PickFont(const std::array<ImFont*, 4>& fonts, float display_height, int bias) {
  int index = display_height <= 600 ? 0 : display_height <= 900 ? 1 : display_height <= 1300 ? 2 : 3;
  index = std::clamp(index + bias, 0, 3);
  for (int i = index; i >= 0; --i) {
    if (fonts[i]) return fonts[i];
  }
  for (int i = index; i < 4; ++i) {
    if (fonts[i]) return fonts[i];
  }
  return nullptr;
}

// ------------------------------------------------------------------------------------------
// OptionsMenu

OptionsMenu::OptionsMenu(rex::ui::ImGuiDrawer* drawer, MenuHost host)
    : ImGuiDialog(drawer), host_(std::move(host)), pages_(BuildPages()) {
  // Treat buttons already down (the chord that opened the menu) as held.
  prev_buttons_ = host_.poll_pad ? host_.poll_pad() : 0;
}

OptionsMenu::~OptionsMenu() = default;

std::vector<int> OptionsMenu::VisibleRows(const Page& page) const {
  std::vector<int> rows;
  for (int i = 0; i < int(page.items.size()); ++i) {
    if (!page.items[i].visible || page.items[i].visible()) {
      rows.push_back(i);
    }
  }
  return rows;
}

void OptionsMenu::RefreshAchievements(Page& page) {
  achievements_ = host_.achievements ? host_.achievements() : std::vector<AchievementRow>{};
  page.items.clear();
  for (const AchievementRow& a : achievements_) {
    Item item;
    item.label = a.name;
    item.help = a.description;
    item.info_only = true;
    page.items.push_back(std::move(item));
  }
}

int OptionsMenu::CurrentChoice(const Item& item) const {
  for (int i = 0; i < int(item.choices.size()); ++i) {
    bool all = true;
    for (const auto& [cvar, value] : item.choices[i].sets) {
      if (!ValuesEqual(OptionsStore::Get(cvar), value)) {
        all = false;
        break;
      }
    }
    if (all) {
      return i;
    }
  }
  return -1;
}

void OptionsMenu::Activate(Item& item) {
  switch (item.act) {
    case Item::Act::kResume:
      if (host_.request_close) host_.request_close();
      break;
    case Item::Act::kQuit:
      if (quit_armed_ && Clock::now() - quit_time_ < std::chrono::seconds(3)) {
        quit_armed_ = false;
        if (host_.request_quit) host_.request_quit();
      } else {
        quit_armed_ = true;
        quit_time_ = Clock::now();
      }
      break;
    case Item::Act::kNone:
      break;
  }
}

void OptionsMenu::Step(Item& item, int dir) {
  if (item.act != Item::Act::kNone) {
    if (dir > 0) Activate(item);
    return;
  }
  if (item.info_only || item.choices.empty()) {
    return;
  }
  int n = int(item.choices.size());
  int cur = CurrentChoice(item);
  int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : std::clamp(cur + dir, 0, n - 1);
  if (next == cur) {
    return;
  }
  for (const auto& [cvar, value] : item.choices[next].sets) {
    if (host_.store->Set(cvar, value) && host_.on_changed) {
      host_.on_changed(cvar);
    }
  }
}

OptionsMenu::Action OptionsMenu::ReadAction(ImGuiIO& io) {
  (void)io;
  // Keyboard (ImGui receives window key events while a dialog is open).
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) || ImGui::IsKeyPressed(ImGuiKey_W, true))
    return kUp;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) || ImGui::IsKeyPressed(ImGuiKey_S, true))
    return kDown;
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) || ImGui::IsKeyPressed(ImGuiKey_A, true))
    return kLeft;
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) || ImGui::IsKeyPressed(ImGuiKey_D, true))
    return kRight;
  if (ImGui::IsKeyPressed(ImGuiKey_Q, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false) ||
      (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && ImGui::GetIO().KeyShift))
    return kPrevPage;
  if (ImGui::IsKeyPressed(ImGuiKey_E, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false) ||
      ImGui::IsKeyPressed(ImGuiKey_Tab, false))
    return kNextPage;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
    return kClose;
  if (ImGui::IsKeyPressed(ImGuiKey_R, false)) return kReset;
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false))
    return kAccept;

  // Controller.
  if (!host_.poll_pad) {
    return kNone;
  }
  const uint16_t buttons = host_.poll_pad();
  const uint16_t pressed = buttons & ~prev_buttons_;
  prev_buttons_ = buttons;
  using namespace rex::input;
  if (!armed_) {
    // Wait until the opening chord (L3+R3) is released.
    if (!(buttons & (X_INPUT_GAMEPAD_LEFT_THUMB | X_INPUT_GAMEPAD_RIGHT_THUMB))) {
      armed_ = true;
    }
    return kNone;
  }
  if (pressed & X_INPUT_GAMEPAD_B) return kClose;
  if ((pressed & X_INPUT_GAMEPAD_LEFT_THUMB) && (buttons & X_INPUT_GAMEPAD_RIGHT_THUMB))
    return kClose;
  if ((pressed & X_INPUT_GAMEPAD_RIGHT_THUMB) && (buttons & X_INPUT_GAMEPAD_LEFT_THUMB))
    return kClose;
  if (pressed & X_INPUT_GAMEPAD_START) return kClose;
  if (pressed & X_INPUT_GAMEPAD_LEFT_SHOULDER) return kPrevPage;
  if (pressed & X_INPUT_GAMEPAD_RIGHT_SHOULDER) return kNextPage;
  if (pressed & X_INPUT_GAMEPAD_Y) return kReset;
  if (pressed & X_INPUT_GAMEPAD_A) return kAccept;

  // Directions with key-repeat.
  const uint16_t dirs = buttons & (X_INPUT_GAMEPAD_DPAD_UP | X_INPUT_GAMEPAD_DPAD_DOWN |
                                   X_INPUT_GAMEPAD_DPAD_LEFT | X_INPUT_GAMEPAD_DPAD_RIGHT);
  auto now = Clock::now();
  uint16_t fire = 0;
  if (dirs != held_dir_) {
    held_dir_ = dirs;
    held_since_ = last_repeat_ = now;
    fire = dirs & pressed;
    if (!fire) fire = dirs;  // direction changed while another was held
  } else if (dirs && now - held_since_ > std::chrono::milliseconds(380) &&
             now - last_repeat_ > std::chrono::milliseconds(75)) {
    last_repeat_ = now;
    fire = dirs;
  }
  if (fire & X_INPUT_GAMEPAD_DPAD_UP) return kUp;
  if (fire & X_INPUT_GAMEPAD_DPAD_DOWN) return kDown;
  if (fire & X_INPUT_GAMEPAD_DPAD_LEFT) return kLeft;
  if (fire & X_INPUT_GAMEPAD_DPAD_RIGHT) return kRight;
  return kNone;
}

void OptionsMenu::OnDraw(ImGuiIO& io) {
  auto now = Clock::now();
  if (pages_[page_].achievements) RefreshAchievements(pages_[page_]);
  Page& page = pages_[page_];
  auto rows = VisibleRows(page);
  row_ = std::clamp(row_, 0, std::max(0, int(rows.size()) - 1));

  switch (ReadAction(io)) {
    case kUp:
      row_ = row_ > 0 ? row_ - 1 : int(rows.size()) - 1;
      break;
    case kDown:
      row_ = row_ + 1 < int(rows.size()) ? row_ + 1 : 0;
      break;
    case kLeft:
      if (!rows.empty()) Step(page.items[rows[row_]], -1);
      break;
    case kRight:
      if (!rows.empty()) Step(page.items[rows[row_]], +1);
      break;
    case kAccept:
      if (!rows.empty()) {
        // A / Enter cycles and wraps, so it alone can reach every value.
        Item& item = page.items[rows[row_]];
        if (item.act != Item::Act::kNone) {
          Activate(item);
        } else if (!item.info_only && CurrentChoice(item) == int(item.choices.size()) - 1) {
          Step(item, -int(item.choices.size()));
        } else {
          Step(item, +1);
        }
      }
      break;
    case kPrevPage:
      page_ = (page_ + int(pages_.size()) - 1) % int(pages_.size());
      row_ = 0;
      break;
    case kNextPage:
      page_ = (page_ + 1) % int(pages_.size());
      row_ = 0;
      break;
    case kReset:
      if (reset_presses_ > 0 && now - reset_time_ < std::chrono::seconds(3)) {
        host_.store->ResetAll();
        if (host_.on_changed) host_.on_changed("*");
        reset_presses_ = 0;
        toast_ = "All options reset to the launcher's values";
        toast_time_ = now;
      } else {
        reset_presses_ = 1;
        reset_time_ = now;
      }
      break;
    case kClose:
      if (host_.request_close) host_.request_close();
      break;
    case kNone:
      break;
  }
  if (reset_presses_ && now - reset_time_ >= std::chrono::seconds(3)) {
    reset_presses_ = 0;
  }
  if (quit_armed_ && now - quit_time_ >= std::chrono::seconds(3)) {
    quit_armed_ = false;
  }

  // --- Layout -------------------------------------------------------------------------------
  ImFont* font = PickFont(host_.fonts, io.DisplaySize.y);
  ImFont* big = PickFont(host_.fonts, io.DisplaySize.y, +1);
  if (font) ImGui::PushFont(font, font->LegacySize);
  const float fs = ImGui::GetFontSize();

  const ImVec2 size(std::min(io.DisplaySize.x * 0.92f, fs * 42.0f),
                    std::min(io.DisplaySize.y * 0.92f, fs * 29.0f));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
  ImGui::PushStyleColor(ImGuiCol_Border, kAccentDim);
  ImGui::PushStyleColor(ImGuiCol_Text, kText);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f));
  // Hover highlight only while the mouse is actually in use.
  if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseDown[0]) {
    last_mouse_move_ = now;
  }
  const bool mouse_active = now - last_mouse_move_ < std::chrono::milliseconds(1500);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                        mouse_active ? ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.14f)
                                     : ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f));
  ImGui::PushStyleColor(ImGuiCol_Separator, kAccentDim);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fs * 0.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.2f, fs * 0.9f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.5f, fs * 0.35f));

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoScrollbar;
  if (ImGui::Begin("##splosion_recomp_options", nullptr, flags)) {
    // Title.
    if (big) ImGui::PushFont(big, big->LegacySize);
    ImGui::TextColored(kAccent, "'SPLOSION MAN");
    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "recomp options");
    if (big) ImGui::PopFont();

    // Live stats in the title row.
    rex::system::GuestFrameStats st;
    if (host_.frame_stats && host_.frame_stats(&st) && st.frame_count) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.0f fps  %.1f ms", st.fps, st.frame_ms);
      ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(buf).x - fs * 1.2f);
      ImGui::TextColored(kTextDim, "%s", buf);
    }

    // Page tabs.
    ImGui::Spacing();
    for (int i = 0; i < int(pages_.size()); ++i) {
      if (i) ImGui::SameLine(0, fs * 1.4f);
      const bool sel = i == page_;
      ImGui::PushStyleColor(ImGuiCol_Text, sel ? kAccent : kTextDim);
      if (ImGui::Selectable(pages_[i].name.c_str(), sel, 0,
                            ImVec2(ImGui::CalcTextSize(pages_[i].name.c_str()).x, 0))) {
        page_ = i;
        row_ = 0;
      }
      ImGui::PopStyleColor();
      if (sel) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, b.y + 2), ImVec2(b.x, b.y + 5),
                                                  ImGui::GetColorU32(kAccent));
      }
    }
    ImGui::Dummy(ImVec2(0, fs * 0.2f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, fs * 0.2f));

    // Rows (re-read: a tab click may have changed the page).
    Page& cur_page = pages_[page_];
    if (cur_page.achievements && &cur_page != &page) RefreshAchievements(cur_page);
    rows = VisibleRows(cur_page);
    row_ = std::clamp(row_, 0, std::max(0, int(rows.size()) - 1));
    const float help_h = fs * 6.6f;
    // Footer: the status text and the button hints share a line when the longest of each
    // fits (wide windows); otherwise the hints get their own line.
    const char* hint = cur_page.achievements ? "LB/RB page   D-pad scroll   B close"
                                             : "LB/RB page   D-pad change   Y reset   B close";
    const bool footer_wrap =
        ImGui::CalcTextSize("Restart the game to apply the settings marked *").x +
            ImGui::CalcTextSize("LB/RB page   D-pad change   Y reset   B close").x + fs * 1.5f >
        ImGui::GetContentRegionAvail().x;
    const float footer_h = footer_wrap ? fs * 3.5f : fs * 2.2f;
    ImGui::BeginChild("##rows", ImVec2(0, ImGui::GetContentRegionAvail().y - help_h - footer_h),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
    const float value_col = ImGui::GetContentRegionAvail().x * 0.58f;
    if (cur_page.achievements && achievements_.empty()) {
      ImGui::Dummy(ImVec2(0, fs * 0.5f));
      TextCentered("This game's achievement list isn't available.", kTextDim);
    }
    for (int r = 0; cur_page.achievements && r < int(rows.size()); ++r) {
      const AchievementRow& a = achievements_[rows[r]];
      ImGui::PushID(r);
      const bool sel = r == row_;
      const float row_h = fs * 2.5f;
      const ImVec2 row_pos = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##ach", sel, 0, ImVec2(0, row_h))) {
        row_ = r;
      }
      if (ImGui::IsItemHovered() && mouse_active &&
          (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)) {
        row_ = r;
      }
      if (sel && !ImGui::IsWindowAppearing()) {
        ImGui::SetScrollHereY(0.5f);
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (sel) {
        dl->AddRectFilled(row_pos, ImVec2(row_pos.x + 4.0f, row_pos.y + row_h),
                          ImGui::GetColorU32(kAccent));
      }
      // Icon: full color when unlocked, faded when locked.
      const float icon = fs * 2.0f;
      const ImVec2 icon_min(row_pos.x + fs * 0.6f, row_pos.y + (row_h - icon) * 0.5f);
      const ImVec2 icon_max(icon_min.x + icon, icon_min.y + icon);
      if (a.icon && (a.unlocked || !a.secret)) {
        dl->AddImage(reinterpret_cast<ImTextureID>(a.icon), icon_min, icon_max, ImVec2(0, 0),
                     ImVec2(1, 1), a.unlocked ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 70));
      } else {
        dl->AddRectFilled(icon_min, icon_max, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)),
                          fs * 0.2f);
        const char* q = "?";
        const ImVec2 qs = ImGui::CalcTextSize(q);
        dl->AddText(ImVec2(icon_min.x + (icon - qs.x) * 0.5f, icon_min.y + (icon - qs.y) * 0.5f),
                    ImGui::GetColorU32(kTextDim), q);
      }
      // Name, and when it was unlocked.
      const float text_x = icon_max.x + fs * 0.7f;
      const float top = row_pos.y + row_h * 0.5f - fs * 1.05f;
      const bool hidden = a.secret && !a.unlocked;
      dl->AddText(ImVec2(text_x, top), ImGui::GetColorU32(a.unlocked ? kText : kTextDim),
                  hidden ? "Secret achievement" : a.name.c_str());
      std::string status = "Locked";
      if (a.unlocked) {
        const std::string when = FormatFileTime(a.unlocked_at);
        status = when.empty() ? "Unlocked" : "Unlocked " + when;
      }
      dl->AddText(ImVec2(text_x, top + fs * 1.1f),
                  ImGui::GetColorU32(a.unlocked ? kGood : ImVec4(0.45f, 0.47f, 0.52f, 1.0f)),
                  status.c_str());
      // Gamerscore on the right.
      char score[16];
      std::snprintf(score, sizeof(score), "%uG", a.gamerscore);
      const ImVec2 ss = ImGui::CalcTextSize(score);
      dl->AddText(ImVec2(row_pos.x + ImGui::GetContentRegionAvail().x - ss.x - fs * 0.5f,
                         row_pos.y + (row_h - ss.y) * 0.5f),
                  ImGui::GetColorU32(a.unlocked ? kAccent : kTextDim), score);
      ImGui::PopID();
    }
    for (int r = 0; !cur_page.achievements && r < int(rows.size()); ++r) {
      Item& item = cur_page.items[rows[r]];
      ImGui::PushID(r);
      const bool sel = r == row_;
      ImVec2 row_pos = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(0, fs * 1.55f))) {
        row_ = r;
        // Click on the left part of the value steps back, elsewhere forward.
        float mx = ImGui::GetIO().MousePos.x - row_pos.x;
        Step(item, (mx > value_col && mx < value_col + fs * 1.5f) ? -1 : +1);
      }
      if (ImGui::IsItemHovered() && mouse_active &&
          (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)) {
        row_ = r;
      }
      if (sel && ImGui::IsWindowAppearing() == false) {
        ImGui::SetScrollHereY(0.5f);
      }
      const float text_y = row_pos.y + (fs * 1.55f - fs) * 0.5f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (sel) {
        dl->AddRectFilled(row_pos, ImVec2(row_pos.x + 4.0f, row_pos.y + fs * 1.55f),
                          ImGui::GetColorU32(kAccent));
      }
      const bool confirm = item.act == Item::Act::kQuit && quit_armed_;
      const char* label = confirm ? "Press again to quit" : item.label.c_str();
      dl->AddText(ImVec2(row_pos.x + fs * 0.6f, text_y),
                  ImGui::GetColorU32(confirm ? kWarn : item.info_only ? kTextDim : kText), label);
      if (item.act != Item::Act::kNone) {
        if (sel) {
          const char* go = confirm ? "A / Enter: quit" : "A / Enter";
          float gw = ImGui::CalcTextSize(go).x;
          dl->AddText(ImVec2(row_pos.x + ImGui::GetContentRegionAvail().x - gw - fs * 0.5f, text_y),
                      ImGui::GetColorU32(confirm ? kWarn : kTextDim), go);
        }
      } else if (!item.info_only) {
        int ci = CurrentChoice(item);
        std::string value = ci >= 0 ? item.choices[ci].label : std::string("Custom");
        if (ci < 0 && !item.choices.empty() && !item.choices[0].sets.empty()) {
          value += " (" + OptionsStore::Get(item.choices[0].sets[0].first) + ")";
        }
        const bool can_left = ci != 0;
        const bool can_right = ci < int(item.choices.size()) - 1;
        ImU32 arrow_on = ImGui::GetColorU32(sel ? kAccent : kTextDim);
        ImU32 arrow_off = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.33f, 1.0f));
        float x = row_pos.x + value_col;
        dl->AddText(ImVec2(x, text_y), can_left ? arrow_on : arrow_off, "<");
        float vw = ImGui::CalcTextSize(value.c_str()).x;
        float span = ImGui::GetContentRegionAvail().x - value_col - fs * 0.5f;
        dl->AddText(ImVec2(x + (span - vw) * 0.5f, text_y), ImGui::GetColorU32(kText),
                    value.c_str());
        dl->AddText(ImVec2(x + span - fs * 0.5f, text_y), can_right ? arrow_on : arrow_off, ">");
        if (item.restart) {
          dl->AddText(ImVec2(row_pos.x + fs * 0.6f + ImGui::CalcTextSize(item.label.c_str()).x +
                                 fs * 0.5f,
                             text_y),
                      ImGui::GetColorU32(kWarn), "*");
        }
      }
      ImGui::PopID();
    }
    ImGui::EndChild();

    // Help box for the selected row.
    ImGui::Separator();
    ImGui::BeginChild("##help", ImVec2(0, help_h - fs * 0.6f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    if (!rows.empty()) {
      const Item& item = cur_page.items[rows[row_]];
      const bool hidden = cur_page.achievements && achievements_[rows[row_]].secret &&
                          !achievements_[rows[row_]].unlocked;
      ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
      ImGui::TextWrapped("%s", hidden ? "Keep playing to unlock this secret achievement."
                                      : item.help.c_str());
      ImGui::PopStyleColor();
      if (item.restart) {
        ImGui::TextColored(kWarn, "* Takes effect after restarting the game.");
      }
    }
    ImGui::EndChild();

    // Footer: status + controls.
    ImGui::Separator();
    if (!toast_.empty() && now - toast_time_ < std::chrono::seconds(3)) {
      ImGui::TextColored(kGood, "%s", toast_.c_str());
    } else if (quit_armed_) {
      ImGui::TextColored(kWarn, "Press A / Enter again to quit the game");
    } else if (reset_presses_) {
      ImGui::TextColored(kWarn, "Press Y / R again to reset every option");
    } else if (cur_page.achievements) {
      int got = 0;
      uint32_t score = 0, total = 0;
      for (const AchievementRow& a : achievements_) {
        total += a.gamerscore;
        if (a.unlocked) {
          ++got;
          score += a.gamerscore;
        }
      }
      ImGui::TextColored(kTextDim, "%d of %d unlocked   %u of %uG", got, int(achievements_.size()),
                         score, total);
    } else if (host_.store->HasPendingRestart()) {
      ImGui::TextColored(kWarn, "Restart the game to apply the settings marked *");
    } else {
      ImGui::TextColored(kTextDim, "Changes apply immediately and are saved");
    }
    const float hint_x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint).x - fs * 1.2f;
    if (footer_wrap) {
      ImGui::SetCursorPosX(hint_x);
    } else {
      ImGui::SameLine(hint_x);
    }
    ImGui::TextColored(kTextDim, "%s", hint);
  }
  ImGui::End();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor(7);
  if (font) ImGui::PopFont();
}

// ------------------------------------------------------------------------------------------
// PerfOverlay

PerfOverlay::PerfOverlay(rex::ui::ImGuiDrawer* drawer, MenuHost host)
    : ImGuiDialog(drawer), host_(std::move(host)) {}

PerfOverlay::~PerfOverlay() = default;

void PerfOverlay::OnDraw(ImGuiIO& io) {
  const int mode = rex::cvar::Query<int32_t>("perf_overlay");
  if (mode <= 0) {
    return;
  }
  rex::system::GuestFrameStats st;
  if (!host_.frame_stats || !host_.frame_stats(&st)) {
    return;
  }
  if (st.frame_count != last_frame_count_) {
    last_frame_count_ = st.frame_count;
    history_[history_pos_] = float(st.frame_ms);
    history_pos_ = (history_pos_ + 1) % history_.size();
  }
  ImFont* font = PickFont(host_.fonts, io.DisplaySize.y, -1);
  if (font) ImGui::PushFont(font, font->LegacySize);
  const float fs = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(ImVec2(fs * 0.6f, fs * 0.6f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fs * 0.3f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.5f, fs * 0.3f));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##splosion_perf", nullptr, flags)) {
    const ImVec4 color = st.fps >= 55 ? kGood : st.fps >= 40 ? kWarn : ImVec4(1, 0.4f, 0.4f, 1);
    ImGui::TextColored(color, "%.0f FPS", st.fps);
    ImGui::SameLine();
    ImGui::TextColored(kTextDim, "%.1f ms", st.frame_ms);
    if (mode >= 2) {
      ImGui::TextColored(kTextDim, "worst %.1f ms", st.frame_max_ms);
      ImGui::TextColored(kTextDim, "CPU %.1f  GPU wait %.1f  cap %.1f ms", st.work_ms,
                         st.gpu_wait_ms, st.limiter_ms);
      ImGui::PushStyleColor(ImGuiCol_PlotLines, kAccent);
      ImGui::PlotLines("##ft", history_.data(), int(history_.size()), int(history_pos_), nullptr,
                       0.0f, 40.0f, ImVec2(fs * 12.0f, fs * 2.2f));
      ImGui::PopStyleColor();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  if (font) ImGui::PopFont();
}

// ------------------------------------------------------------------------------------------
// HintToast

HintToast::HintToast(rex::ui::ImGuiDrawer* drawer, MenuHost host, std::function<void()> on_done)
    : ImGuiDialog(drawer),
      host_(std::move(host)),
      on_done_(std::move(on_done)),
      start_(Clock::now()) {}

HintToast::~HintToast() = default;

void HintToast::OnDraw(ImGuiIO& io) {
  const float t = std::chrono::duration<float>(Clock::now() - start_).count();
  constexpr float kShow = 6.0f, kFade = 0.8f;
  if (t > kShow) {
    if (!done_ && on_done_) {
      done_ = true;
      on_done_();
    }
    return;
  }
  const float alpha = t > kShow - kFade ? (kShow - t) / kFade : std::min(1.0f, t / 0.3f);
  ImFont* font = PickFont(host_.fonts, io.DisplaySize.y, -1);
  if (font) ImGui::PushFont(font, font->LegacySize);
  const float fs = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - fs * 2.0f),
                          ImGuiCond_Always, ImVec2(0.5f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.7f * alpha);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fs * 0.4f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.9f, fs * 0.5f));
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##splosion_hint", nullptr, flags)) {
    ImGui::TextColored(kAccent, "Recomp options:");
    ImGui::SameLine();
    ImGui::TextUnformatted("press F1 or click both sticks (L3 + R3)");
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  if (font) ImGui::PopFont();
}

}  // namespace splosion
