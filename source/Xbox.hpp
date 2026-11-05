#pragma once
#include "RetroArchConsoleLauncher.hpp"

class XboxConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};