#include "ConsoleLaunchers.hpp"

#include <cstdlib>
#include <filesystem>

bool Xbox360ConsoleLauncher::launch(const std::string& contentPath) const {
    const char* configured = std::getenv("OMNICON_XBOX360_LAUNCHER");
    const std::string launcher = configured && *configured ? configured : "xenia";
    return launchExternalApplication(launcher, {std::filesystem::absolute(contentPath).string()});
}
