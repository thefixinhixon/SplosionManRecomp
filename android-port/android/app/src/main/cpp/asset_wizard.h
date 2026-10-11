// asset_wizard.h - the "load the assets needed to play" menu.
//
// Shown by SplosionmanApp::OnFinalizePaths (Android) when no usable game
// data is installed yet. The wizard offers every asset source it can find:
//
//   1. A raw XBLA/STFS package dropped into the app's external "inbox"
//      folder (Android/data/com.tp.splosionman/files/inbox) - extracted
//      in place by the built-in STFS reader, no PC tools needed.
//   2. An already-extracted game folder (containing default.xex) placed
//      in the app's external folder - used in place.
//   3. A system file-picker (SDL dialogs / SAF) for a package anywhere
//      else on the device.
//
// On success it calls the ReXApp resume callback with the resolved paths
// and closes itself; the game then boots straight into the title screen
// with the touch controls live. On later launches the assets are found
// in place and the wizard never appears.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>

class AssetWizardDialog : public rex::ui::ImGuiDialog {
 public:
  AssetWizardDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                    rex::PathConfig defaults,
                    std::function<void(rex::PathConfig)> resume);
  ~AssetWizardDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Candidate {
    enum class Kind { kPackage, kExtractedFolder } kind;
    std::string path;   // Package file or folder path.
    std::string label;  // Display text.
    int64_t size = 0;
  };

  void Scan();
  void StartImport(const Candidate& candidate);
  void StartImportPath(const std::string& path);  // From the file picker.
  void FinishIfDone();
  static void OnFilePicked(void* userdata, const char* const* filelist,
                           int filter);

  rex::PathConfig defaults_;
  std::function<void(rex::PathConfig)> resume_;
  std::vector<Candidate> candidates_;

  enum class State { kBrowsing, kWorking, kError };
  State state_ = State::kBrowsing;

  // Worker-thread import state (UI reads, worker writes).
  std::thread worker_;
  std::atomic<bool> work_done_{false};
  std::atomic<bool> work_ok_{false};
  std::atomic<float> work_progress_{0.0f};
  std::atomic<int64_t> work_done_count_{0};
  std::atomic<int64_t> work_total_count_{0};
  std::string work_status_;       // Guarded by work_status_mutex_.
  std::string work_error_;        // Written before work_done_ is set.
  std::string result_game_dir_;   // Written before work_done_ is set.
  mutable std::mutex work_status_mutex_;

  // File-picker result, delivered on the SDL main thread via callback.
  std::string picked_path_;
  std::atomic<bool> picked_ready_{false};

  bool resumed_ = false;
};
