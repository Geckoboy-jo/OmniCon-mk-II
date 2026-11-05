#pragma once
#include "RetroArchConsoleLauncher.hpp"

class AtariXEGSConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Atari XEGS", "OMNICON_XEGS_CORE",
                                          "atari800_libretro.so", contentPath,
                                          "ATARIXL.ROM");
  }
};
