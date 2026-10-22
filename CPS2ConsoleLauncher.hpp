#pragma once
#include "RetroArchConsoleLauncher.hpp"

class CPS2ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Capcom CPS-2", "OMNICON_CPS2_CORE",
                                              "fbneo_libretro.so", contentPath,
                                              "");
    }
};
