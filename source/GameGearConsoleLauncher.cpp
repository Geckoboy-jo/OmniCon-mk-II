#include "ConsoleLaunchers.hpp"

bool GameGearConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("Game Gear", "OMNICON_GAMEGEAR_CORE",
                           "genesis_plus_gx_libretro.so", contentPath);
}
