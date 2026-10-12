#include "ConsoleLaunchers.hpp"

#include <cstdlib>
#include <filesystem>

bool WiiUConsoleLauncher::launch(const std::string& contentPath) const {
    const char* configured = std::getenv("OMNICON_WIIU_LAUNCHER");
    const std::string launcher = configured && *configured ? configured : "cemu";
    return launchExternalApplication(launcher, {"-g", std::filesystem::absolute(contentPath).string()});
}
