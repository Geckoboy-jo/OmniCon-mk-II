#pragma once
#include "RetroArchConsoleLauncher.hpp"

class NeoGeoPocketConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Neo Geo Pocket", "OMNICON_NGP_CORE",
                                          "mednafen_ngp_libretro.so",
                                          contentPath, "");
  }
};
