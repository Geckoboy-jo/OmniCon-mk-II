#pragma once
#include "RetroArchConsoleLauncher.hpp"

class PCEngineCDConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("TurboGrafx-CD", "OMNICON_PCECD_CORE",
                                          "mednafen_pce_libretro.so",
                                          contentPath, "syscard3.pce");
  }
};
