#pragma once
#include "RetroArchConsoleLauncher.hpp"

class GameAndWatchConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("Game & Watch",
                                          "OMNICON_GAMEANDWATCH_CORE",
                                          "gw_libretro.so", contentPath, "");
  }
};
