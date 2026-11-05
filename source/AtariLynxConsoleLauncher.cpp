#include "ConsoleLaunchers.hpp"

bool AtariLynxConsoleLauncher::launch(const std::string &contentPath) const {
  return launchRetroArch("Atari Lynx", "OMNICON_LYNX_CORE", "handy_libretro.so",
                         contentPath);
}
