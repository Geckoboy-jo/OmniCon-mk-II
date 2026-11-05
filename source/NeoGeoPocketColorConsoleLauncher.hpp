#pragma once
#include "RetroArchConsoleLauncher.hpp"

class NeoGeoPocketColorConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Neo Geo Pocket Color", "OMNICON_NGPC_CORE",
                                              "mednafen_ngp_libretro.so", contentPath,
                                              "");
    }
};
