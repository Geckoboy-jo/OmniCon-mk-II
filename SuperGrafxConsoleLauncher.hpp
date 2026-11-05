#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SuperGrafxConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles(
        "SuperGrafx", "OMNICON_SUPERGRAFX_CORE",
        "mednafen_supergrafx_libretro.so", contentPath, "");
  }
};
