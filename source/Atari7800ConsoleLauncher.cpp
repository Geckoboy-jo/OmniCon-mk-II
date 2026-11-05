#include "ConsoleLaunchers.hpp"

bool Atari7800ConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("Atari 7800", "OMNICON_ATARI7800_CORE",
                           "prosystem_libretro.so", contentPath);
}
