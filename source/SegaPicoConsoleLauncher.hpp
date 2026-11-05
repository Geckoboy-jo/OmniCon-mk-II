#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SegaPicoConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Sega Pico", "OMNICON_SEGAPICO_CORE",
                                          "picodrive_libretro.so", contentPath,
                                          "");
  }
};
