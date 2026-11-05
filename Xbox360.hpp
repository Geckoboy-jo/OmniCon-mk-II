#pragma once
#include "ConsoleLaunchers.hpp"
class Xbox360ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};