#include "ConsoleLaunchers.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

bool XboxConsoleLauncher::launch(const std::string &contentPath) const {
  const char *configured = std::getenv("OMNICON_XBOX_LAUNCHER");
  std::string launcher = configured && *configured ? configured : "xemu";
  if (!configured || !*configured) {
    const char *home = std::getenv("HOME");
    if (home && *home) {
      const std::filesystem::path userLocalXemu =
          std::filesystem::path(home) / ".local" / "bin" / "xemu";
      if (std::filesystem::is_regular_file(userLocalXemu))
        launcher = userLocalXemu.string();
    }
  }
  if (!launchExternalApplication(
          launcher,
          {"-dvd_path", std::filesystem::absolute(contentPath).string()})) {
    std::cerr << "[Xbox] Xemu was not started. Install Xemu or set "
                 "OMNICON_XBOX_LAUNCHER to its executable path.\n";
    return false;
  }
  return true;
}
