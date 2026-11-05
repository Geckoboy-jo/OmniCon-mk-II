#pragma once
#include "RetroArchConsoleLauncher.hpp"

class Naomi2ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Sega Naomi 2", "OMNICON_NAOMI2_CORE",
                                          "flycast_libretro.so", contentPath,
                                          "dc/naomi2.zip");
  }
};
