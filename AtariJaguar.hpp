#pragma once
#include "top.hpp"
#include "RetroArchConsoleLauncher.hpp"     
class AtariJaguarConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};