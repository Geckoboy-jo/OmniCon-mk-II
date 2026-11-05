#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SegaSystem16ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Sega System 16", "OMNICON_SYSTEM16_CORE",
                                              "fbneo_libretro.so", contentPath,
                                              "");
    }
};
