#pragma once
#include "RetroArchConsoleLauncher.hpp"

class Panasonic3DOConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles("3DO", "OMNICON_3DO_CORE",
                                          "opera_libretro.so", contentPath,
                                          "panafz10.bin");
  }
};
