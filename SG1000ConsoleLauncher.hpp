#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SG1000ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Sega SG-1000", "OMNICON_SG1000_CORE",
                                          "genesis_plus_gx_libretro.so",
                                          contentPath, "");
  }
};
