#pragma once
#include "RetroArchConsoleLauncher.hpp"
#include "top.hpp"
class AtariLynxConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};