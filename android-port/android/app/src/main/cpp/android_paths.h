// android_paths.h - storage locations for the Android port.
//
// Two roots, both app-specific (no runtime permissions needed):
//   Internal: /data/data/com.tp.splosionman/files      - saves, logs, config
//   External: /sdcard/Android/data/com.tp.splosionman/files
//             - game data + the user-facing "inbox" for XBLA packages.
//             Reachable over USB / file managers so the user can drop
//             their package in without any in-app file permission.
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

#include <SDL3/SDL.h>

namespace tp {

inline std::filesystem::path InternalDir() {
  const char* path = SDL_GetAndroidInternalStoragePath();
  return path ? std::filesystem::path(path) : std::filesystem::path("/data/data/com.tp.splosionman/files");
}

inline std::filesystem::path ExternalDir() {
  const char* path = SDL_GetAndroidExternalStoragePath();
  return path ? std::filesystem::path(path) : InternalDir();
}

// Where the extracted game lives (also where a user may manually place an
// extracted game folder containing default.xex).
inline std::filesystem::path GameDataDir() { return ExternalDir() / "game"; }

// Drop folder scanned by the asset loader for STFS/XBLA packages.
inline std::filesystem::path InboxDir() { return ExternalDir() / "inbox"; }

// Saves, shader cache, logs, config.
inline std::filesystem::path UserDataDir() { return InternalDir() / "userdata"; }

// True if the folder looks like an extracted 'Splosion Man game root.
inline bool HasGameAssets(const std::filesystem::path& dir) {
  if (dir.empty()) return false;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return false;
  // The runtime loads game:\default.xex - require the XEX and check magic.
  auto xex = dir / "default.xex";
  if (!std::filesystem::exists(xex, ec)) return false;
  FILE* f = std::fopen(xex.string().c_str(), "rb");
  if (!f) return false;
  char magic[4] = {};
  bool ok = std::fread(magic, 1, 4, f) == 4 && std::string(magic, 4) == "XEX2";
  std::fclose(f);
  return ok;
}

}  // namespace tp
