// recomp_options.cpp - see recomp_options.h.

#include "recomp_options.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace splosion {

namespace {

// Minimal reader/writer for the two-section file this class writes:
//   [options]  cvar = "value"
//   [launch]   cvar = "value"
// Values are always quoted strings; the cvar layer parses them.
std::string Unquote(std::string v) {
  auto trim = [](std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r"));
    s.erase(s.find_last_not_of(" \t\r") + 1);
  };
  trim(v);
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
    v = v.substr(1, v.size() - 2);
  }
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      out.push_back(v[++i]);
    } else {
      out.push_back(v[i]);
    }
  }
  return out;
}

std::string Quote(const std::string& v) {
  std::string out = "\"";
  for (char c : v) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace

void OptionsStore::LoadAndApply(const std::filesystem::path& file,
                                const std::vector<std::string>& managed) {
  file_ = file;
  // Launch value of each setting. Flags from the GPU plugin register later;
  // for those use the pending command-line value ("" = compiled default).
  for (const auto& name : managed) {
    if (rex::cvar::GetFlagInfo(name)) {
      launch_[name] = rex::cvar::GetFlagByName(name);
    } else {
      launch_[name] = rex::cvar::GetPendingCommandLineValue(name).value_or("");
    }
  }

  std::ifstream in(file);
  if (!in) {
    return;
  }
  std::map<std::string, std::string> saved, saved_base;
  std::map<std::string, std::string>* section = nullptr;
  std::string line;
  while (std::getline(in, line)) {
    auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    if (line[first] == '[') {
      section = line.find("[options]") != std::string::npos  ? &saved
                : line.find("[launch]") != std::string::npos ? &saved_base
                                                             : nullptr;
      continue;
    }
    auto eq = line.find('=');
    if (!section || eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(first, eq - first);
    key.erase(key.find_last_not_of(" \t") + 1);
    (*section)[key] = Unquote(line.substr(eq + 1));
  }

  int applied = 0, dropped = 0;
  for (const auto& [name, value] : saved) {
    auto launch_it = launch_.find(name);
    if (launch_it == launch_.end()) {
      continue;  // No longer a managed/known setting.
    }
    auto base_it = saved_base.find(name);
    if (base_it != saved_base.end() && base_it->second != launch_it->second) {
      // Changed in the launcher (or on the command line) since the option was
      // saved: the newer launcher choice wins.
      REXLOG_INFO("Options: '{}' changed outside the game ({} -> {}); dropping in-game value {}",
                  name, base_it->second, launch_it->second, value);
      ++dropped;
      dirty_ = true;
      continue;
    }
    if (rex::cvar::SetFlagByNameOrDefer(name, value)) {
      overrides_[name] = value;
      base_[name] = launch_it->second;
      ++applied;
    }
  }
  // Startup application is not a pending restart.
  rex::cvar::ClearPendingRestartFlags();
  if (applied || dropped) {
    REXLOG_INFO("Options: applied {} in-game setting(s) from {}, dropped {}", applied,
                file.string(), dropped);
  }
}

bool OptionsStore::Set(std::string_view cvar, std::string_view value) {
  const std::string name(cvar);
  if (Get(name) == value) {
    return true;
  }
  if (!rex::cvar::SetFlagByName(name, value)) {
    REXLOG_WARN("Options: rejected {} = {}", name, value);
    return false;
  }
  overrides_[name] = std::string(value);
  if (!base_.count(name)) {
    base_[name] = launch_.count(name) ? launch_[name] : std::string();
  }
  const auto* info = rex::cvar::GetFlagInfo(name);
  if (info && info->lifecycle != rex::cvar::Lifecycle::kHotReload) {
    const bool back_to_launch =
        launch_.count(name) && (launch_[name] == value ||
                                (launch_[name].empty() && info->default_value == value));
    auto it = std::find(restart_pending_.begin(), restart_pending_.end(), name);
    if (back_to_launch && it != restart_pending_.end()) {
      restart_pending_.erase(it);
    } else if (!back_to_launch && it == restart_pending_.end()) {
      restart_pending_.push_back(name);
    }
  }
  dirty_ = true;
  return true;
}

std::string OptionsStore::Get(std::string_view cvar) {
  return rex::cvar::GetFlagByName(cvar);
}

void OptionsStore::ResetAll() {
  for (const auto& [name, value] : launch_) {
    if (!overrides_.count(name)) {
      continue;
    }
    if (value.empty()) {
      rex::cvar::ResetToDefault(name);  // Launched with the compiled default.
    } else {
      Set(name, value);
    }
  }
  restart_pending_.clear();
  overrides_.clear();
  base_.clear();
  dirty_ = true;
}

void OptionsStore::SaveIfDirty() {
  if (!dirty_ || file_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  const auto tmp = std::filesystem::path(file_).concat(".tmp");
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      REXLOG_WARN("Options: cannot write {}", tmp.string());
      return;
    }
    out << "# 'Splosion Man recomp - options set in the in-game menu (F1 / L3+R3).\n"
           "# [launch] records each setting's launcher value when it was saved; if the\n"
           "# launcher value changes later, the launcher wins for that setting.\n\n"
           "[options]\n";
    for (const auto& [name, value] : overrides_) {
      out << name << " = " << Quote(value) << "\n";
    }
    out << "\n[launch]\n";
    for (const auto& [name, value] : base_) {
      if (overrides_.count(name)) {
        out << name << " = " << Quote(value) << "\n";
      }
    }
  }
  std::filesystem::rename(tmp, file_, ec);
  if (ec) {
    REXLOG_WARN("Options: cannot replace {}: {}", file_.string(), ec.message());
    return;
  }
  dirty_ = false;
}

}  // namespace splosion
