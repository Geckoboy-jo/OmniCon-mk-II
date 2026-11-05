#pragma once
#include "RetroArchConsoleLauncher.hpp"

class WonderswanColorConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles(
        "WonderSwan Color", "OMNICON_WONDERSWANCOLOR_CORE",
        "mednafen_wswan_libretro.so", contentPath, "");
  }
};
