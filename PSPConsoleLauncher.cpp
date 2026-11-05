#include "ConsoleLaunchers.hpp"

bool PSPConsoleLauncher::launch(const std::string &contentPath) const {
  return launchRetroArch("PlayStation Portable", "OMNICON_PSP_CORE",
                         "ppsspp_libretro.so", contentPath);
}
