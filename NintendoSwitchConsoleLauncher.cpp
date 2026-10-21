#include "ConsoleLaunchers.hpp"
#include "top.hpp"
#include "NintendoSwitch.hpp"

bool NintendoSwitchConsoleLauncher::launch(const std::string& contentPath) const {
    const char* configured = std::getenv("OMNICON_SWITCH_LAUNCHER");
    const std::string launcher = configured && *configured ? configured : "Ryujinx";
    return launchExternalApplication(launcher, {std::filesystem::absolute(contentPath).string()});
}
