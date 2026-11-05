#pragma once
#include "ConsoleLaunchers.hpp"

// Opens the Minecraft Launcher (Java Edition). contentPath is unused; the
// launcher manages its own game instances.
class MinecraftConsoleLauncher final : public ConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};
