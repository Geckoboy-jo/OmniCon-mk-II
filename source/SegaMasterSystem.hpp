

#pragma once
#include "RetroArchConsoleLauncher.hpp"
class SegaMasterSystemConsoleLauncher final : public RetroArchConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};