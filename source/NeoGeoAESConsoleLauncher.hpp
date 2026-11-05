#pragma once
#include "RetroArchConsoleLauncher.hpp"

class NeoGeoAESConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Neo Geo AES", "OMNICON_NEOGEOAES_CORE",
                                              "geolith_libretro.so", contentPath,
                                              "neogeo.zip");
    }
};
