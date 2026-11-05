#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SegaCDConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override {
    return launchRetroArchWithSystemFiles(
        "Sega CD", "OMNICON_SEGACD_CORE", "genesis_plus_gx_libretro.so",
        contentPath, "bios_CD_U.bin|bios_CD_E.bin|bios_CD_J.bin");
  }
};
