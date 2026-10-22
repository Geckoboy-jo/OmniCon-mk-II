#pragma once
#include "RetroArchConsoleLauncher.hpp"

class NeoGeoMVSConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Neo Geo MVS", "OMNICON_NEOGEOMVS_CORE",
                                              "geolith_libretro.so", contentPath,
                                              "neogeo.zip");
    }
};
