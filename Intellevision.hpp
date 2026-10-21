#pragma once
#include "top.hpp"
#include "RetroArchConsoleLauncher.hpp"

class IntellivisionConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};
