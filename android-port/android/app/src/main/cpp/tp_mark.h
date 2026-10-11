// tp_mark.h - unbuffered startup stage markers for on-device diagnosis.
//
// The runtime's spdlog file sink buffers, so a hard native crash can take
// the interesting log lines with it (Test Build 1 died with a 0-byte
// game.log). These markers go to logcat (tag TPMARK) AND to an append-only
// file in the app's external logs folder, flushed on every call, so the
// last marker standing names the stage that killed the process.
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

#include <android/log.h>

#include "android_paths.h"

namespace tp {

inline void Mark(const std::string& what) {
  __android_log_print(ANDROID_LOG_INFO, "TPMARK", "%s", what.c_str());
  std::error_code ec;
  auto dir = ExternalDir() / "logs";
  std::filesystem::create_directories(dir, ec);
  auto path = dir / "stage.log";
  if (FILE* f = std::fopen(path.string().c_str(), "a")) {
    std::fprintf(f, "%s\n", what.c_str());
    std::fflush(f);
    std::fclose(f);
  }
}

inline std::string PtrStr(const void* p) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%p", p);
  return buf;
}

}  // namespace tp
