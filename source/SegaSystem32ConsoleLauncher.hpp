#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SegaSystem32ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Sega System 32", "OMNICON_SYSTEM32_CORE",
                                              "fbneo_libretro.so", contentPath,
                                              "");
    }
};
