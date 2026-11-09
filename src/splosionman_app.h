// splosionman - ReXGlue Recompiled Project
//
// App hooks: in-game options menu (F1 / L3+R3), performance overlay,
// persisted options, controller filters.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/rex_app.h>

#include "options_menu.h"
#include "recomp_options.h"

struct ImFontAtlas;

// Defined by the generated init code (splosionman_init.cpp).
extern const rex::PPCImageInfo PPCImageConfig;

class SplosionmanApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SplosionmanApp>(new SplosionmanApp(ctx, "splosionman", PPCImageConfig));
  }

 protected:
  void OnPostInitLogging() override;
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnPostSetup() override;
  void OnShutdown() override;

 private:
  splosion::MenuHost MakeHost();
  void ToggleMenu();
  void OpenMenu();
  void CloseMenu();
  void QuitGame();
  void SyncPerfOverlay();
  void ApplyLive(const std::string& cvar);
  void InstallInputFilters();

  splosion::OptionsStore store_;
  std::array<ImFont*, 4> fonts_{};
  std::unique_ptr<splosion::OptionsMenu> menu_;
  std::unique_ptr<splosion::PerfOverlay> perf_overlay_;
  std::unique_ptr<splosion::HintToast> hint_;
  std::atomic<bool> menu_open_{false};
  // After the menu closes, keep the pad neutral for the game until every
  // button is released, so the B that closed it doesn't reach the game.
  std::atomic<bool> suppress_until_release_{false};
  std::array<std::atomic<bool>, 4> chord_down_{};
  bool frame_held_ = false;
};
