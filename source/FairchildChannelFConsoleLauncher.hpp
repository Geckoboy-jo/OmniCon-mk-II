#pragma once
#include "RetroArchConsoleLauncher.hpp"

class FairchildChannelFConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Fairchild Channel F", "OMNICON_CHANNELF_CORE",
                                              "freechaf_libretro.so", contentPath,
                                              "sl31253.bin;sl31254.bin;sl90025.bin");
    }
};
