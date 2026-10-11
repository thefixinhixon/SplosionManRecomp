// splosionman - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.
//
// Android additions (all inside #if REX_PLATFORM_ANDROID, desktop builds
// are unchanged):
//   - Storage paths rooted in the app's internal/external dirs.
//   - gpu_plugin / license_mask / log_file set programmatically (there is
//     no command line on Android).
//   - Asset loader wizard via OnFinalizePaths when no game data is
//     installed yet.
//   - On-screen touch controls via OnCreateDialogs.

#pragma once

#include <rex/rex_app.h>
#include <rex/cvar.h>

#if REX_PLATFORM_ANDROID
#include <filesystem>

#include "android_paths.h"
#include "asset_wizard.h"
#include "touch_controls.h"
#include "tp_mark.h"
#endif

class SplosionmanApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SplosionmanApp>(new SplosionmanApp(ctx, "splosionman",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // OnPreSetup runs inside SetupPresentation AFTER config_.gpu_plugin is
  // snapshotted from the cvar but BEFORE the plugin-load decision, and it
  // receives the config by reference - setting the field directly is the
  // reliable way to name the GPU plugin on Android. (OnPostSetup runs at
  // the END of ConstructRuntime in this SDK - far too late; Test Build 2's
  // stage marks proved config_.graphics stayed null and no presenter or
  // ImGui drawer was ever created.)
  void OnPreSetup(rex::RuntimeConfig& config) override {
#if REX_PLATFORM_ANDROID
      if (config.gpu_plugin.empty()) {
          config.gpu_plugin = "xenos";
      }
      tp::Mark("app: OnPreSetup gpu_plugin=" + config.gpu_plugin);
#endif
  }

  void OnPostSetup() override {
      // AMD/RADV fix: unclipped draw shaders cause blank screen during
      // gameplay on AMD GPUs. Execute them on CPU instead.
      rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu", "true");
#if REX_PLATFORM_ANDROID
      tp::Mark("app: OnPostSetup drawer=" + tp::PtrStr(imgui_drawer()) +
               " license_mask=" + rex::cvar::GetFlagByName("license_mask"));
#endif
  }

#if REX_PLATFORM_ANDROID
  void OnConfigurePaths(rex::PathConfig& paths) override {
      // Root everything in app-specific storage (no permissions needed,
      // survives until uninstall). Game data lives in external storage so
      // the user can also manage it over USB. The user/cache roots are
      // FORCED, not just filled when empty: the SDK pre-fills them with a
      // desktop XDG default (/data/.local/share/...) that the app cannot
      // write to on Android (Test Build 2 stage marks).
      paths.game_data_root = tp::GameDataDir();
      paths.user_data_root = tp::UserDataDir();
      paths.cache_root = paths.user_data_root / "cache";
      paths.config_path = paths.user_data_root / "splosionman.toml";

      // No launcher/CLI on Android: bake in the flags the desktop
      // launcher used to pass. This hook runs before every read of them
      // (gpu_plugin is also set directly on the config in OnPreSetup).
      bool ok_gpu = rex::cvar::SetFlagByName("gpu_plugin", "xenos");
      bool ok_lic = rex::cvar::SetFlagByName("license_mask", "1");
      tp::Mark("app: cvar sets gpu_plugin=" + std::string(ok_gpu ? "ok" : "FAIL") +
               " license_mask=" + (ok_lic ? "ok" : "FAIL") +
               " readback license_mask=" + rex::cvar::GetFlagByName("license_mask"));
      // NOTE: vulkan_async_skip_incomplete_frames stays at its default
      // (true). The desktop launcher forced it off for an AMD RADV
      // black-screen bug; on Mali the "off" behavior stalls the guest on
      // incomplete frames and craters the frame rate.
      // Mobile GPUs (Mali especially) may lack geometryShader /
      // fillModeNonSolid; the SDK has fallback paths for both - just stop
      // requiring them at device selection.
      rex::cvar::SetFlagByName("vulkan_require_geometry_shader", "false");
      rex::cvar::SetFlagByName("vulkan_require_fill_mode_non_solid", "false");

      std::error_code ec;
      std::filesystem::create_directories(paths.game_data_root, ec);
      std::filesystem::create_directories(paths.user_data_root / "logs", ec);
      std::filesystem::create_directories(paths.cache_root, ec);
      // Log to EXTERNAL storage on purpose: internal storage is not
      // user-readable without root, and the game log is the primary
      // diagnostic when something goes wrong on a device.
      std::filesystem::path log_dir = tp::ExternalDir() / "logs";
      std::filesystem::create_directories(log_dir, ec);
      rex::cvar::SetFlagByName("log_file", (log_dir / "game.log").string());
      // Flush the game log every second: a hard crash must not take the
      // interesting lines with it (Test Build 1 died with a 0-byte log).
      rex::cvar::SetFlagByName("log_flush_interval", "1");
      tp::Mark("app: OnConfigurePaths game=" + paths.game_data_root.string() +
               " user=" + paths.user_data_root.string());
  }

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
      bool have_assets = tp::HasGameAssets(defaults.game_data_root);
      tp::Mark("app: OnFinalizePaths drawer=" + tp::PtrStr(imgui_drawer()) +
               " assets=" + (have_assets ? "yes" : "no"));
      if (have_assets) {
          // Assets already installed: boot straight in, controls live.
          tp::ControlsVisible().store(true);
          return defaults;
      }
      if (!imgui_drawer()) {
          // No ImGui drawer (overlay setup did not run): the wizard has
          // nowhere to render. Boot with defaults instead of crashing;
          // the runtime will fail on the missing game data, but the
          // stage log will show exactly how far things got.
          tp::Mark("app: OnFinalizePaths NO DRAWER - wizard skipped");
          return defaults;
      }
      // Show the asset loader; it calls resume() when the user has
      // imported a package or picked an extracted folder.
      new AssetWizardDialog(imgui_drawer(), defaults, std::move(resume));
      tp::Mark("app: wizard created");
      return std::nullopt;
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
      tp::Mark("app: OnCreateDialogs drawer=" + tp::PtrStr(drawer));
      if (!drawer) {
          tp::Mark("app: OnCreateDialogs NULL - touch controls skipped");
          return;
      }
      // Lives for the whole session (never Close()d). It renders nothing
      // until ControlsVisible() is set by OnFinalizePaths / the wizard.
      new TouchControlsDialog(drawer);
      tp::Mark("app: touch controls created");
  }
#endif
  // void OnShutdown() override {}
};
