#pragma once
#include "RetroArchConsoleLauncher.hpp"

class PSPConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};