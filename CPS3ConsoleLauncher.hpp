#pragma once
#include "RetroArchConsoleLauncher.hpp"

class CPS3ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Capcom CPS-3", "OMNICON_CPS3_CORE",
                                          "fbneo_libretro.so", contentPath, "");
  }
};
