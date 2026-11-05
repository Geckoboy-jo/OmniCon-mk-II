#include "ConsoleLaunchers.hpp"

bool ColecoVisionConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("ColecoVision", "OMNICON_COLECO_CORE",
                           "bluemsx_libretro.so", contentPath);
}
