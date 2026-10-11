// asset_wizard.cpp - see asset_wizard.h.
#include "asset_wizard.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

#include "android_paths.h"
#include "stfs_package.h"
#include "touch_controls.h"

namespace fs = std::filesystem;

namespace {

bool LooksLikeStfs(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  char magic[4] = {};
  in.read(magic, 4);
  if (in.gcount() != 4) return false;
  std::string m(magic, 4);
  return m == "LIVE" || m == "PIRS" || m.rfind("CON", 0) == 0;
}

std::string HumanSize(int64_t bytes) {
  char buf[64];
  if (bytes >= int64_t(1024) * 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.2f GB", double(bytes) / (1024.0 * 1024 * 1024));
  } else if (bytes >= 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / (1024.0 * 1024));
  } else {
    std::snprintf(buf, sizeof(buf), "%lld KB", (long long)(bytes / 1024));
  }
  return buf;
}

}  // namespace

AssetWizardDialog::AssetWizardDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                                     rex::PathConfig defaults,
                                     std::function<void(rex::PathConfig)> resume)
    : rex::ui::ImGuiDialog(imgui_drawer),
      defaults_(std::move(defaults)),
      resume_(std::move(resume)) {
  // Make sure the drop folders exist so the instructions below are true
  // even on a first-ever launch.
  std::error_code ec;
  fs::create_directories(tp::InboxDir(), ec);
  fs::create_directories(tp::GameDataDir(), ec);
  Scan();
}

AssetWizardDialog::~AssetWizardDialog() {
  if (worker_.joinable()) worker_.join();
}

void AssetWizardDialog::Scan() {
  candidates_.clear();
  std::error_code ec;

  // Already-extracted game in the canonical spot: offer it directly.
  if (tp::HasGameAssets(tp::GameDataDir())) {
    candidates_.push_back({Candidate::Kind::kExtractedFolder,
                           tp::GameDataDir().string(),
                           "Extracted game in the game folder", 0});
  }

  // Inbox packages.
  if (fs::is_directory(tp::InboxDir())) {
    for (const auto& entry : fs::directory_iterator(tp::InboxDir(), ec)) {
      if (!entry.is_regular_file()) continue;
      int64_t size = int64_t(entry.file_size(ec));
      if (size < 1024 * 1024) continue;  // Real XBLA packages are far bigger.
      if (!LooksLikeStfs(entry.path())) continue;
      candidates_.push_back({Candidate::Kind::kPackage, entry.path().string(),
                             "XBLA package: " + entry.path().filename().string() +
                                 " (" + HumanSize(size) + ")",
                             size});
    }
  }

  // Any other extracted folder one level under the external dir.
  if (fs::is_directory(tp::ExternalDir())) {
    for (const auto& entry : fs::directory_iterator(tp::ExternalDir(), ec)) {
      if (!entry.is_directory()) continue;
      if (entry.path() == tp::GameDataDir() || entry.path() == tp::InboxDir())
        continue;
      if (tp::HasGameAssets(entry.path())) {
        candidates_.push_back({Candidate::Kind::kExtractedFolder,
                               entry.path().string(),
                               "Extracted game folder: " +
                                   entry.path().filename().string(),
                               0});
      }
    }
  }
}

void AssetWizardDialog::StartImport(const Candidate& candidate) {
  if (candidate.kind == Candidate::Kind::kExtractedFolder) {
    // Nothing to copy - validate and go.
    rex::PathConfig paths = defaults_;
    paths.game_data_root = candidate.path;
    resumed_ = true;
    tp::ControlsVisible().store(true);
    resume_(paths);
    Close();
    return;
  }
  StartImportPath(candidate.path);
}

void AssetWizardDialog::StartImportPath(const std::string& source) {
  state_ = State::kWorking;
  work_done_.store(false);
  work_ok_.store(false);
  work_progress_.store(0.0f);
  {
    std::lock_guard lock(work_status_mutex_);
    work_status_ = "Reading package...";
    work_error_.clear();
  }
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, source]() {
    std::string local_source = source;
    std::string error;

    // Android file pickers hand back content:// URIs; materialize the
    // package into the inbox first so the STFS reader can seek freely.
    if (rex::filesystem::IsAndroidContentUri(source)) {
      int fd = rex::filesystem::OpenAndroidContentFileDescriptor(source, "r");
      if (fd < 0) {
        error = "Could not open the picked file.";
      } else {
        fs::path temp = tp::InboxDir() / "picked-package.bin";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        FILE* in = fdopen(fd, "rb");
        bool copy_ok = in && out.is_open();
        if (copy_ok) {
          char buffer[1 << 16];
          size_t n;
          while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
            out.write(buffer, std::streamsize(n));
            if (!out) {
              copy_ok = false;
              break;
            }
          }
        }
        if (in) std::fclose(in);
        out.close();
        if (copy_ok) {
          local_source = temp.string();
        } else {
          error = "Could not copy the picked file into the app folder.";
        }
      }
    }

    bool ok = false;
    if (error.empty()) {
      StfsPackage package;
      if (!package.Open(local_source, &error)) {
        // error already set
      } else if (package.TitleId() != "5841098F") {
        error = "That package is for title " + package.TitleId() +
                ", not 'Splosion Man (5841098F). Pick the right game file.";
      } else {
        // Wipe any previous (partial) extraction for a clean import.
        std::error_code ec;
        fs::remove_all(tp::GameDataDir(), ec);
        fs::create_directories(tp::GameDataDir(), ec);
        ok = package.ExtractAll(
            tp::GameDataDir().string(),
            [this](int64_t done, int64_t total, const std::string& name) {
              work_done_count_.store(done);
              work_total_count_.store(total);
              work_progress_.store(total > 0 ? float(done) / float(total) : 0.0f);
              std::lock_guard lock(work_status_mutex_);
              work_status_ = name;
            },
            &error);
        if (ok && !tp::HasGameAssets(tp::GameDataDir())) {
          ok = false;
          error = "Extraction finished but no playable default.xex was "
                  "found. The package may be incomplete.";
        }
        if (ok) result_game_dir_ = tp::GameDataDir().string();
      }
    }

    if (!ok) {
      std::lock_guard lock(work_status_mutex_);
      work_error_ = error;
    }
    work_ok_.store(ok);
    work_done_.store(true);
  });
}

void AssetWizardDialog::FinishIfDone() {
  if (state_ != State::kWorking || !work_done_.load()) return;
  if (worker_.joinable()) worker_.join();
  if (work_ok_.load()) {
    rex::PathConfig paths = defaults_;
    paths.game_data_root = result_game_dir_;
    resumed_ = true;
    tp::ControlsVisible().store(true);
    resume_(paths);
    Close();
  } else {
    state_ = State::kError;
  }
}

// static
void AssetWizardDialog::OnFilePicked(void* userdata,
                                     const char* const* filelist, int filter) {
  (void)filter;
  auto* self = static_cast<AssetWizardDialog*>(userdata);
  if (!filelist || !filelist[0]) return;  // Cancelled.
  self->picked_path_ = filelist[0];
  self->picked_ready_.store(true);
}

void AssetWizardDialog::OnDraw(ImGuiIO& io) {
  FinishIfDone();

  // A file-picker result arrives asynchronously; consume it here.
  if (state_ == State::kBrowsing && picked_ready_.exchange(false)) {
    if (!picked_path_.empty()) {
      StartImportPath(picked_path_);
      picked_path_.clear();
    }
  }

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  float win_w = std::min(680.0f, io.DisplaySize.x * 0.92f);
  float win_h = std::min(560.0f, io.DisplaySize.y * 0.92f);
  ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Always);
  if (!ImGui::Begin("'Splosion Man - Load Game Assets", nullptr,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  ImGui::TextWrapped(
      "'Splosion Man needs its original Xbox 360 game data, which is not "
      "included with this app. If you own the game, load your copy here - "
      "this only has to be done once.");
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (state_ == State::kWorking) {
    int64_t done = work_done_count_.load();
    int64_t total = work_total_count_.load();
    ImGui::Text("Importing game assets...");
    ImGui::ProgressBar(work_progress_.load(),
                       ImVec2(-1, 0),
                       (std::to_string(done) + " / " + std::to_string(total) +
                        " files")
                           .c_str());
    std::string status;
    {
      std::lock_guard lock(work_status_mutex_);
      status = work_status_;
    }
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::End();
    return;
  }

  if (state_ == State::kError) {
    std::string error;
    {
      std::lock_guard lock(work_status_mutex_);
      error = work_error_;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Import failed");
    ImGui::TextWrapped("%s", error.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(140, 48))) {
      state_ = State::kBrowsing;
      Scan();
    }
    ImGui::End();
    return;
  }

  // Browsing.
  if (candidates_.empty()) {
    ImGui::TextWrapped(
        "No game assets found yet. Two ways to add them:\n\n"
        "1) Copy your XBLA package file (the file named like "
        "\"5841098F...\" from your Xbox 360 content folder) into:\n");
    ImGui::TextWrapped("%s", tp::InboxDir().string().c_str());
    ImGui::TextWrapped(
        "\nThat folder is under Android/data/com.tp.splosionman/files/inbox "
        "on your device storage - reachable over USB.\n\n"
        "2) Or tap Browse and pick the package anywhere on the device.");
  } else {
    ImGui::Text("Found:");
    for (const Candidate& candidate : candidates_) {
      if (ImGui::Button((candidate.label + "##" + candidate.path).c_str(),
                        ImVec2(-1, 52))) {
        StartImport(candidate);
        ImGui::End();
        return;
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button("Browse for XBLA package...", ImVec2(260, 52))) {
    SDL_ShowOpenFileDialog(&AssetWizardDialog::OnFilePicked, this, nullptr,
                           nullptr, 0, nullptr, false);
  }
  ImGui::SameLine();
  if (ImGui::Button("Rescan", ImVec2(120, 52))) {
    Scan();
  }
  ImGui::SameLine();
  if (ImGui::Button("Quit", ImVec2(120, 52))) {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
  }

  ImGui::End();
}
