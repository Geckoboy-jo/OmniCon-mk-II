#pragma once
#include "RetroArchConsoleLauncher.hpp"

class PCFXConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("PC-FX", "OMNICON_PCFX_CORE",
                                          "mednafen_pcfx_libretro.so",
                                          contentPath, "pcfx.rom");
  }
};
