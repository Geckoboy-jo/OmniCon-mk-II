#pragma once
#include "RetroArchConsoleLauncher.hpp"

class VirtualBoyConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles(
        "Virtual Boy", "OMNICON_VIRTUALBOY_CORE", "mednafen_vb_libretro.so",
        contentPath, "");
  }
};
