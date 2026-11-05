#pragma once
#include "RetroArchConsoleLauncher.hpp"

class AmigaCD32ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Amiga CD32", "OMNICON_CD32_CORE",
                                              "puae_libretro.so", contentPath,
                                              "");
    }
};
