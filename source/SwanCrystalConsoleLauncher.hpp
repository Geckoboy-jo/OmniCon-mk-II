#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SwanCrystalConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("SwanCrystal", "OMNICON_SWANCRYSTAL_CORE",
                                              "mednafen_wswan_libretro.so", contentPath,
                                              "");
    }
};
