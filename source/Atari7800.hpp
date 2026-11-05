#pragma once
#include "RetroArchConsoleLauncher.hpp"
#include "top.hpp"

class Atari7800ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};