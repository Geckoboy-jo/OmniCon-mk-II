#pragma once
#include "RetroArchConsoleLauncher.hpp"

class SegaSaturnConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};