#include "ConsoleLaunchers.hpp"
#include "WiiUConverter.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

bool WiiUConsoleLauncher::launch(const std::string& contentPath) const {
    const char* configured = std::getenv("OMNICON_WIIU_LAUNCHER");
    const std::string launcher = configured && *configured ? configured : "cemu";

    std::string target = contentPath;
    if (WiiUConverter::isArchive(contentPath)) {
        // NUS archives are converted into Cemu's mlc folder the first time they
        // are launched; later launches find the recorded install and skip this.
        const auto result = WiiUConverter::ensureConverted(contentPath, [](const std::string& message) {
            std::cout << "[WiiU] " << message << std::endl;
        });
        if (!result.ok) {
            std::cerr << "[WiiU] " << result.error << "\n";
            return false;
        }
        // Updates and DLC cannot run alone; start the base game they belong to.
        const std::string baseDir = WiiUConverter::installedBaseDir(result.titleId);
        target = baseDir.empty() ? "" : WiiUConverter::findRpx(baseDir);
        if (target.empty()) {
            std::cerr << "[WiiU] Title " << result.titleId << " is installed, but its base game "
                         "is not. Download and launch the base game to play it.\n";
            return false;
        }
    }

    return launchExternalApplication(launcher, {"-m", WiiUConverter::mlcPath(), "-g",
                                                std::filesystem::absolute(target).string()});
}
