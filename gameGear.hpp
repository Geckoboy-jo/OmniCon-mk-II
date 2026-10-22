#pragma once
#include "RetroArchConsoleLauncher.hpp"

class GameGearConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};