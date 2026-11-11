// options_menu.h - the in-game "Recomp Options" menu and the performance overlay.
//
// Open with F1 on the keyboard or by clicking both sticks (L3 + R3) on a
// controller. Navigation: D-pad / left stick or arrow keys to move, Left /
// Right to change a value, LB / RB (Q / E, Tab) to switch pages, B / Esc to
// close, Y (R) twice to reset everything. The Game page has Resume and Quit.

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

struct ImFont;

namespace splosion {

class OptionsStore;

// What the menu needs from the app. All callbacks run on the UI thread.
struct MenuHost {
  OptionsStore* store = nullptr;
  std::array<ImFont*, 4> fonts{};          // ascending sizes; any may be null
  std::function<uint16_t()> poll_pad;       // raw buttons of pad 1 (sticks folded into the D-pad)
  std::function<bool(void*)> frame_stats;   // fills rex::system::GuestFrameStats
  std::function<void(const std::string& cvar)> on_changed;  // live-apply hook
  std::function<void()> request_close;      // closes the menu after the current frame
  std::function<void()> request_quit;       // saves options and exits the game
};

ImFont* PickFont(const std::array<ImFont*, 4>& fonts, float display_height, int bias = 0);

class OptionsMenu : public rex::ui::ImGuiDialog {
 public:
  OptionsMenu(rex::ui::ImGuiDrawer* drawer, MenuHost host);
  ~OptionsMenu() override;

  struct Choice {
    std::string label;
    std::vector<std::pair<std::string, std::string>> sets;  // cvar = value
  };
  struct Item {
    std::string label;
    std::string help;
    std::vector<Choice> choices;
    std::function<bool()> visible;  // null = always
    bool restart = false;           // takes effect after a restart
    bool info_only = false;         // a text row, no value
    enum class Act { kNone, kResume, kQuit };
    Act act = Act::kNone;           // a command row (no value): runs on A / Enter / click
  };
  struct Page {
    std::string name;
    std::vector<Item> items;
  };

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum Action { kNone, kUp, kDown, kLeft, kRight, kPrevPage, kNextPage, kClose, kReset, kAccept };
  Action ReadAction(ImGuiIO& io);
  int CurrentChoice(const Item& item) const;
  void Step(Item& item, int dir);
  void Activate(Item& item);
  std::vector<int> VisibleRows(const Page& page) const;

  MenuHost host_;
  std::vector<Page> pages_;
  int page_ = 0;
  int row_ = 0;
  uint16_t prev_buttons_ = 0;
  uint16_t held_dir_ = 0;
  std::chrono::steady_clock::time_point held_since_{}, last_repeat_{};
  bool armed_ = false;  // ignore the chord that opened the menu until released
  int reset_presses_ = 0;
  std::chrono::steady_clock::time_point reset_time_{};
  bool quit_armed_ = false;  // "Quit game" pressed once; a second press quits
  std::chrono::steady_clock::time_point quit_time_{};
  std::chrono::steady_clock::time_point last_mouse_move_{};
  std::string toast_;
  std::chrono::steady_clock::time_point toast_time_{};
};

// Small always-on-top frame rate readout. mode 1 = FPS, 2 = detailed.
class PerfOverlay : public rex::ui::ImGuiDialog {
 public:
  PerfOverlay(rex::ui::ImGuiDrawer* drawer, MenuHost host);
  ~PerfOverlay() override;
  // Redrawn with each guest frame; must not keep the UI thread repainting (mailbox busy loop).
  bool WantsContinuousRepaint() override { return false; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  MenuHost host_;
  std::array<float, 120> history_{};
  size_t history_pos_ = 0;
  uint64_t last_frame_count_ = 0;
};

// One-shot hint shown at startup ("Press F1 / L3+R3 for Recomp Options").
class HintToast : public rex::ui::ImGuiDialog {
 public:
  HintToast(rex::ui::ImGuiDrawer* drawer, MenuHost host, std::function<void()> on_done);
  ~HintToast() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  MenuHost host_;
  std::function<void()> on_done_;
  std::chrono::steady_clock::time_point start_;
  bool done_ = false;
};

}  // namespace splosion
