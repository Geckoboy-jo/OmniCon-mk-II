#pragma once
#include "RetroArchConsoleLauncher.hpp"

class Sega32XConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Sega 32X", "OMNICON_SEGA32X_CORE",
                                              "picodrive_libretro.so", contentPath,
                                              "");
    }
};
