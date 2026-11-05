#pragma once
#include "RetroArchConsoleLauncher.hpp"

class WiiUConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};