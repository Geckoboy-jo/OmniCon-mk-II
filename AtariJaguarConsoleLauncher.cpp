#include "ConsoleLaunchers.hpp"

bool AtariJaguarConsoleLauncher::launch(const std::string &contentPath) const {
  return launchRetroArch("Atari Jaguar", "OMNICON_JAGUAR_CORE",
                         "virtualjaguar_libretro.so", contentPath);
}
