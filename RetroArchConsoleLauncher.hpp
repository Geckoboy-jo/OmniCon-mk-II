#pragma once
#include "ConsoleLaunchers.hpp"

class RetroArchConsoleLauncher : public ConsoleLauncher {
protected:
    static bool launchRetroArch(const std::string& systemName,
                                const char* coreEnvironmentVariable,
                                const char* defaultCore,
                                const std::string& contentPath);
};