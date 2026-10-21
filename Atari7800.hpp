#pragma once 
#include "top.hpp"
#include "RetroArchConsoleLauncher.hpp"

class Atari7800ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};