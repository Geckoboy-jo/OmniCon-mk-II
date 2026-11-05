#include "ConsoleLaunchers.hpp"

bool WiiConsoleLauncher::launch(const std::string &contentPath) const {
  return launchRetroArch("Nintendo Wii", "OMNICON_WII_CORE",
                         "dolphin_libretro.so", contentPath);
}
