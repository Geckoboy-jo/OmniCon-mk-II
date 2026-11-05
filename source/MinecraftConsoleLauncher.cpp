#include "Minecraft.hpp"
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

bool MinecraftConsoleLauncher::launch(const std::string &contentPath) const {
  // contentPath is a Prism Launcher instance name (from the scan), or
  // kMinecraftLauncherId to just open the launcher (the "+" tile).
  const bool openLauncher =
      (contentPath.empty() || contentPath == kMinecraftLauncherId);

  // Allow an override (e.g. a specific launcher binary).
  if (const char *env = std::getenv("OMNICON_MINECRAFT_LAUNCHER");
      env && *env) {
    if (launchExternalApplication(env, {}))
      return true;
    std::cerr << "[Minecraft] OMNICON_MINECRAFT_LAUNCHER failed: " << env
              << "\n";
  }

  bool started = false;
  if (runningInWSL()) {
    // The launcher lives on Windows; open it via the Start Menu shortcut.
    started = launchExternalApplication(
        "cmd.exe",
        {"/c", "start", "",
         "shell:AppsFolder\\Microsoft.4297127D64EC6_8wekyb3d8bbwe!Minecraft"});
  }
  if (!started) {
    // Prism Launcher can launch a specific instance directly with
    // `--launch <instance>`; the official launcher only opens its UI.
    if (hasCommand("prismlauncher")) {
      started = openLauncher ? launchExternalApplication("prismlauncher", {})
                             : launchExternalApplication(
                                   "prismlauncher", {"--launch", contentPath});
    } else if (hasCommand("minecraft-launcher")) {
      started = launchExternalApplication("minecraft-launcher", {});
    } else if (hasCommand("flatpak")) {
      started =
          launchExternalApplication("flatpak", {"run", "com.mojang.Minecraft"});
    } else if (hasCommand("prismlauncher")) {
      started = launchExternalApplication("prismlauncher", {});
    }
  }

  if (!started) {
    std::cerr << "[Minecraft] Could not find the Minecraft Launcher. Install "
                 "it (scripts/install_deps.sh) or set "
                 "OMNICON_MINECRAFT_LAUNCHER.\n";
    postShellMessage("Minecraft: launcher not found");
  }
  return started;
}
