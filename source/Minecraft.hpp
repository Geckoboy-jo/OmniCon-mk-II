#pragma once
#include "ConsoleLaunchers.hpp"

// Launches Minecraft. contentPath is a profile/instance name from the scan,
// or kAddGamesId to just open the launcher (the "+" tile).
inline constexpr const char *kMinecraftLauncherId = "MINECRAFTLAUNCHER";

class MinecraftConsoleLauncher final : public ConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};
