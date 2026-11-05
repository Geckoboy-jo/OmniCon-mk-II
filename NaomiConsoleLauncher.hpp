#pragma once
#include "RetroArchConsoleLauncher.hpp"

class NaomiConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Sega Naomi", "OMNICON_NAOMI_CORE",
                                          "flycast_libretro.so", contentPath,
                                          "dc/naomi.zip");
  }
};
