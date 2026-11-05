#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SatellaviewConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Satellaview", "OMNICON_SATELLAVIEW_CORE",
                                              "snes9x_libretro.so", contentPath,
                                              "");
    }
};
