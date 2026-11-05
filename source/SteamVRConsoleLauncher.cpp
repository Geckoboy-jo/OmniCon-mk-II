#include "SteamVR.hpp"
  
#include "ShellMessage.hpp"

#include <algorithm>
#include <cctype>
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

// True if the ALVR flatpak is installed.
bool alvrInstalled() {
  return hasCommand("flatpak") &&
         std::system("flatpak list --app 2>/dev/null | grep -q org.alvr.ALVR") ==
             0;
}

} // namespace

bool SteamVRConsoleLauncher::launch(const std::string &contentPath) const {
  // contentPath is the numeric Steam AppID; it goes into a steam:// URL, so
  // refuse anything that is not all digits.
  if (contentPath.empty() ||
      !std::all_of(contentPath.begin(), contentPath.end(),
                   [](unsigned char c) { return std::isdigit(c) != 0; })) {
    std::cerr << "[SteamVR] Invalid Steam AppID: " << contentPath << "\n";
    return false;
  }

  // Start the ALVR streamer in the background so a standalone headset (Quest,
  // Pico) can connect. Skip quietly when ALVR is not installed.
  if (alvrInstalled()) {
    if (!launchExternalApplication("flatpak", {"run", "org.alvr.ALVR"}))
      std::cerr << "[SteamVR] Could not start ALVR; continuing without it.\n";
  } else {
    std::cerr << "[SteamVR] ALVR not installed; launching without the "
                 "streamer (run scripts/install_deps.sh to add it).\n";
  }

  const std::string url = "steam://rungameid/" + contentPath;
  bool started;
  if (runningInWSL()) {
    // Steam lives on Windows; the empty argument is start's window title.
    started = launchExternalApplication("cmd.exe", {"/c", "start", "", url});
  } else {
    started = launchExternalApplication("steam", {url});
  }

  if (!started) {
    std::cerr << "[SteamVR] Could not launch Steam for AppID " << contentPath
              << ".\n";
    postShellMessage("SteamVR: could not launch Steam for " + contentPath);
  }
  return started;
}