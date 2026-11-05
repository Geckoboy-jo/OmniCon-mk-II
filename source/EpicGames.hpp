#pragma once
#include "ConsoleLaunchers.hpp"

// Launches an Epic Games title. contentPath is the Epic app name from the
// scan, or kEpicLauncherId to just open the launcher (the "+" tile). On Linux
// the games run through a compatibility front-end (Heroic, Lutris, or Wine)
// since there is no native Linux Epic Games Launcher.
inline constexpr const char *kEpicLauncherId = "EPICLAUNCHER";

class EpicGamesConsoleLauncher final : public ConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};
