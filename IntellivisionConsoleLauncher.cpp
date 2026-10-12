#include "ConsoleLaunchers.hpp"

bool IntellivisionConsoleLauncher::launch(const std::string& contentPath) const {
    return launchRetroArch("Intellivision", "OMNICON_INTELLIVISION_CORE",
                           "freeintv_libretro.so", contentPath);
}
