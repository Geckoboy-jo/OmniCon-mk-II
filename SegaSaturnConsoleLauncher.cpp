#include "ConsoleLaunchers.hpp"

bool SegaSaturnConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("Sega Saturn", "OMNICON_SATURN_CORE",
                           "yabasanshiro_libretro.so", contentPath);
}
