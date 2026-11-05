#pragma once
#include "RetroArchConsoleLauncher.hpp"

class CPS1ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Capcom CPS-1", "OMNICON_CPS1_CORE",
                                          "fbneo_libretro.so", contentPath, "");
  }
};
