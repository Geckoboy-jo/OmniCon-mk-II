#include "ConsoleLaunchers.hpp"

bool SegaMasterSystemConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("Sega Master System", "OMNICON_SMS_CORE",
                           "genesis_plus_gx_libretro.so", contentPath);
}
