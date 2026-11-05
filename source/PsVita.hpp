#pragma once
#include "RetroArchConsoleLauncher.hpp"

class PSVitaConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};