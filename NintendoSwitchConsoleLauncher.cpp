#include "ConsoleLaunchers.hpp"
#include "top.hpp"
#include "NintendoSwitch.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

bool runningUnderWsl() {
    std::ifstream osrelease("/proc/sys/kernel/osrelease");
    std::string line;
    std::getline(osrelease, line);
    std::transform(line.begin(), line.end(), line.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return line.find("microsoft") != std::string::npos;
}

bool executableOnPath(const std::string& name) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return false;
    std::stringstream paths(pathEnv);
    std::string dir;
    while (std::getline(paths, dir, ':')) {
        if (dir.empty()) continue;
        if (access((dir + "/" + name).c_str(), X_OK) == 0) return true;
    }
    return false;
}

} // namespace

bool NintendoSwitchConsoleLauncher::launch(const std::string& contentPath) const {
    const char* configured = std::getenv("OMNICON_SWITCH_LAUNCHER");
    std::string launcher = configured && *configured ? configured : "";
    if (launcher.empty()) {
        // WSL2 exposes only software Vulkan, which renders 3D games far too
        // slowly; there, prefer the Windows-native Ryubing via interop.
        launcher = runningUnderWsl() && executableOnPath("Ryubing-win")
                 ? "Ryubing-win"
                 : "Ryubing";
    }
    return launchExternalApplication(launcher, {"--fullscreen", std::filesystem::absolute(contentPath).string()});
}
