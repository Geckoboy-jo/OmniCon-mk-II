#pragma once
#include "top.hpp"
#include "RetroArchConsoleLauncher.hpp"

class PCEngineConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};