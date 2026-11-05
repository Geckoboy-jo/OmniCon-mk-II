#pragma once
#include "RetroArchConsoleLauncher.hpp"
#include "top.hpp"

class IntellivisionConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};
