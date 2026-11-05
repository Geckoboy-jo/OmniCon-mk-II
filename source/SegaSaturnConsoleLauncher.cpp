#include "ConsoleLaunchers.hpp"

bool SegaSaturnConsoleLauncher::launch(const std::string &contentPath) const {
  return launchRetroArch("Sega Saturn", "OMNICON_SATURN_CORE",
                         "mednafen_saturn_libretro.so", contentPath);
}
