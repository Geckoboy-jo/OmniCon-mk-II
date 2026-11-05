#pragma once
#include "RetroArchConsoleLauncher.hpp"

class IGSPGMConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("IGS PGM", "OMNICON_PGM_CORE",
                                          "fbneo_libretro.so", contentPath, "");
  }
};
