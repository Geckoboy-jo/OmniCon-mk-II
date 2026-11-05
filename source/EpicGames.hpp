#pragma once
#include "ConsoleLaunchers.hpp"

// Opens the Epic Games Launcher. contentPath is unused; the launcher manages
// its own library. On Linux the launcher runs through a compatibility tool
// (Heroic, Lutris, or Wine) since there is no native Linux build.
class EpicGamesConsoleLauncher final : public ConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};
