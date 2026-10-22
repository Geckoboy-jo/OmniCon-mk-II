#pragma once
#include "RetroArchConsoleLauncher.hpp"

class WataraSupervisionConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override {
        return launchRetroArchWithSystemFiles("Watara Supervision", "OMNICON_SUPERVISION_CORE",
                                              "potator_libretro.so", contentPath,
                                              "");
    }
};
