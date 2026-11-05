#pragma once
#include "RetroArchConsoleLauncher.hpp"

class WiiConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};