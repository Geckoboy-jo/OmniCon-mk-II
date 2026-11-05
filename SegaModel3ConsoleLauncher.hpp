#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SegaModel3ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Sega Model 3", "OMNICON_MODEL3_CORE",
                                          "supermodel_libretro.so", contentPath,
                                          "");
  }
};
