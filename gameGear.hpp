#pragma once
#include "RetroArchConsoleLauncher.hpp"

class GameGearConsoleLauncher final : public RetroArchConsoleLauncher {
    bool launch(const std::string& contentPath) const override;
};