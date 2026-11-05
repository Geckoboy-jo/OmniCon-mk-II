#include "ConsoleLaunchers.hpp"

bool PCEngineConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("TurboGrafx-16", "OMNICON_PCENGINE_CORE",
                           "mednafen_pce_fast_libretro.so", contentPath);
}
