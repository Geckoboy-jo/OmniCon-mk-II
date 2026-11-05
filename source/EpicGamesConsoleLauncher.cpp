#include "EpicGames.hpp"
#include "ShellMessage.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {

bool runningInWSL() {
  std::ifstream version("/proc/version");
  std::string line;
  if (!std::getline(version, line))
    return false;
  return line.find("Microsoft") != std::string::npos ||
         line.find("microsoft") != std::string::npos;
}

bool hasCommand(const std::string &command) {
  const std::string probe = "command -v " + command + " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

} // namespace

bool EpicGamesConsoleLauncher::launch(const std::string &contentPath) const {
  (void)contentPath; // the launcher manages its own library

  // Allow an override (e.g. a specific Heroic/Lutris invocation).
  if (const char *env = std::getenv("OMNICON_EPIC_LAUNCHER"); env && *env) {
    if (launchExternalApplication(env, {}))
      return true;
    std::cerr << "[Epic Games] OMNICON_EPIC_LAUNCHER failed: " << env << "\n";
  }

  bool started = false;
  if (runningInWSL()) {
    // The launcher lives on Windows; open it via its protocol handler.
    started = launchExternalApplication(
        "cmd.exe", {"/c", "start", "", "com.epicgames.launcher://"});
  }
  if (!started) {
    // Native Linux: no official build, so use a compatibility front-end.
    // Heroic Games Launcher is the most common; fall back to Lutris, then a
    // Wine-installed Epic Games Launcher.
    if (hasCommand("heroic"))
      started = launchExternalApplication("heroic", {});
    else if (hasCommand("flatpak"))
      started = launchExternalApplication(
          "flatpak", {"run", "com.heroicgameslauncher.hgl"});
    else if (hasCommand("lutris"))
      started = launchExternalApplication("lutris",
                                          {"lutris:rungame/epic-games-store"});
  }

  if (!started) {
    std::cerr << "[Epic Games] Could not find a way to open the Epic Games "
                 "Launcher. Install Heroic (scripts/install_deps.sh) or set "
                 "OMNICON_EPIC_LAUNCHER.\n";
    postShellMessage("Epic Games: launcher not found");
  }
  return started;
}
