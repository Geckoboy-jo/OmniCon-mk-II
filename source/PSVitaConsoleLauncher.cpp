#include "ConsoleLaunchers.hpp"

#include <cstdlib>
#include <filesystem>

bool PSVitaConsoleLauncher::launch(const std::string& contentPath) const {
    const char* configured = std::getenv("OMNICON_VITA_LAUNCHER");
    const std::string launcher = configured && *configured ? configured : "Vita3K";
    return launchExternalApplication(launcher, {"-r", std::filesystem::absolute(contentPath).string()});
}
