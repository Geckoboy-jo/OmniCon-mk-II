#include "ConsoleLaunchers.hpp"
#include "ShellMessage.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <gnu/libc-version.h>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace {

// First of the given command names that is executable somewhere on PATH.
std::string findOnPath(std::initializer_list<const char *> names) {
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv)
    return "";
  for (const char *name : names) {
    std::stringstream dirs(pathEnv);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
      if (dir.empty())
        continue;
      const std::filesystem::path candidate = std::filesystem::path(dir) / name;
      if (access(candidate.c_str(), X_OK) == 0)
        return name;
    }
  }
  return "";
}

bool glibcAtLeast(int major, int minor) {
  int haveMajor = 0, haveMinor = 0;
  if (std::sscanf(gnu_get_libc_version(), "%d.%d", &haveMajor, &haveMinor) != 2)
    return true;
  return haveMajor > major || (haveMajor == major && haveMinor >= minor);
}

} // namespace

bool Xbox360ConsoleLauncher::launch(const std::string &contentPath) const {
  const char *configured = std::getenv("OMNICON_XBOX360_LAUNCHER");
  const std::string launcher = configured && *configured
                                   ? std::string(configured)
                                   : findOnPath({"xenia_canary", "xenia"});
  if (launcher.empty()) {
    std::cerr << "[Xbox 360] Xenia was not found. Run scripts/install_deps.sh "
                 "--runtime "
                 "(installs xenia_canary) or set OMNICON_XBOX360_LAUNCHER.\n";
    postShellMessage(
        "Xbox 360: Xenia is not installed. Run scripts/install_deps.sh "
        "--runtime or set OMNICON_XBOX360_LAUNCHER.");
    return false;
  }
  // The xenia_canary Linux build needs glibc 2.38+ (Ubuntu 24.04 or newer).
  if (launcher == "xenia_canary" && !glibcAtLeast(2, 38)) {
    std::cerr
        << "[Xbox 360] xenia_canary needs glibc 2.38+ but this system has "
        << gnu_get_libc_version()
        << ". Upgrade to Ubuntu 24.04+ or set "
           "OMNICON_XBOX360_LAUNCHER to a working Xenia.\n";
    postShellMessage(
        std::string(
            "Xbox 360: xenia_canary needs glibc 2.38+ (this system has ") +
        gnu_get_libc_version() + "). Upgrade to Ubuntu 24.04 or newer.");
    return false;
  }
  return launchExternalApplication(
      launcher, {std::filesystem::absolute(contentPath).string()});
}
