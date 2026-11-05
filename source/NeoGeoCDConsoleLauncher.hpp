#pragma once
#include "RetroArchConsoleLauncher.hpp"

class NeoGeoCDConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Neo Geo CD", "OMNICON_NEOGEOCD_CORE",
                                              "neocd_libretro.so", contentPath,
                                              "");
    }
};
