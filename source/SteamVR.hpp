#pragma once
#include "ConsoleLaunchers.hpp"

// Launches a Steam title in VR. contentPath is the numeric Steam AppID.
// Starts the ALVR streamer (so a standalone headset can connect) and then asks
// Steam to run the game. SteamVR itself starts automatically when a VR title
// is launched through Steam.
class SteamVRConsoleLauncher final : public ConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};
