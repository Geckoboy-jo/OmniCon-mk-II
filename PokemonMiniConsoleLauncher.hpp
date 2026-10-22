#pragma once
#include "RetroArchConsoleLauncher.hpp"

class PokemonMiniConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Pokemon Mini", "OMNICON_POKEMINI_CORE",
                                              "pokemini_libretro.so", contentPath,
                                              "");
    }
};
