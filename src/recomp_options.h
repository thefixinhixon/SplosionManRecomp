// recomp_options.h - persisted in-game options for the 'Splosion Man recomp.
//
// The launcher passes every setting on the command line, and command-line
// values outrank the config file, so options changed in the in-game menu are
// kept in their own file (<user_data_root>/recomp_options.toml) and applied
// at startup with runtime priority.
//
// To keep the launcher authoritative for what the user changes *there*, each
// saved option also records the value the setting had at launch when it was
// saved (its "base"). At the next start an option is applied only if the
// launch value still equals that base; if the launcher (or a flag) changed
// the setting since, the launcher wins and the stale option is dropped.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace splosion {

class OptionsStore {
 public:
  // Reads the file and applies the options that are still valid. Call once,
  // after the command line is parsed and before the runtime is constructed.
  void LoadAndApply(const std::filesystem::path& file, const std::vector<std::string>& managed);

  // Sets a cvar from the menu and records it as an override.
  bool Set(std::string_view cvar, std::string_view value);

  // Current value of a cvar (string form).
  static std::string Get(std::string_view cvar);

  // Drops every override and restores the values the game launched with.
  void ResetAll();

  // Writes the file if anything changed since the last save.
  void SaveIfDirty();

  bool HasPendingRestart() const { return !restart_pending_.empty(); }
  const std::vector<std::string>& pending_restart() const { return restart_pending_; }

 private:
  std::filesystem::path file_;
  std::map<std::string, std::string> overrides_;  // cvar -> value set in the menu
  std::map<std::string, std::string> base_;       // cvar -> launch value when saved
  std::map<std::string, std::string> launch_;     // cvar -> value at this launch
  std::vector<std::string> restart_pending_;
  bool dirty_ = false;
};

}  // namespace splosion
