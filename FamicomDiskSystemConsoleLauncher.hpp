#pragma once
#include "RetroArchConsoleLauncher.hpp"

class FamicomDiskSystemConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles(
        "Famicom Disk System", "OMNICON_FDS_CORE", "nestopia_libretro.so",
        contentPath, "disksys.rom");
  }
};
