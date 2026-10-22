#pragma once
#include "RetroArchConsoleLauncher.hpp"

class VectrexConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Vectrex", "OMNICON_VECTREX_CORE",
                                              "vecx_libretro.so", contentPath,
                                              "");
    }
};
