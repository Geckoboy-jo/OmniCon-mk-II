#pragma once
#include "RetroArchConsoleLauncher.hpp"

class WonderSwanConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles(
        "WonderSwan", "OMNICON_WONDERSWAN_CORE", "mednafen_wswan_libretro.so",
        contentPath, "");
  }
};
