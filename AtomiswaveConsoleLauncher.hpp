#pragma once
#include "RetroArchConsoleLauncher.hpp"

class AtomiswaveConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Atomiswave", "OMNICON_ATOMISWAVE_CORE",
                                              "flycast_libretro.so", contentPath,
                                              "dc/awbios.zip");
    }
};
