#pragma once
#include "RetroArchConsoleLauncher.hpp"

class ArcadeConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Arcade", "OMNICON_ARCADE_CORE",
                                              "fbneo_libretro.so", contentPath,
                                              "");
    }
};
