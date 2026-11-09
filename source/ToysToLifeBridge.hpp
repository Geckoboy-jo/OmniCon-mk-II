#pragma once

#include "SystemType.hpp"

#include <string>

// Which toys-to-life hardware a title can use.
struct ToysToLifeProfile {
  bool skylanders = false;    // needs the Portal of Power
  bool infinity = false;      // needs the Disney Infinity base
  bool amiiboCapable = false; // system supports amiibo (Wii U / Switch / 3DS)
  std::string keyword;        // filename keyword that matched

  bool any() const { return skylanders || infinity || amiiboCapable; }
  bool requiresDevice() const { return skylanders || infinity; }
};

// Detects toys-to-life titles from the ROM filename and system.
ToysToLifeProfile detectToysToLifeProfile(SystemType system,
                                          const std::string &contentPath);

// Bridge between OmniCon and the toys-to-life simulation app
// (toys_bridge/bridge_app.py, a separate process speaking JSON-lines over
// TCP on 127.0.0.1:8399, override with OMNICON_TTL_PORT).
class ToysToLifeBridge {
public:
  // Called before a game launches. For Skylanders/Disney Infinity titles it
  // starts the bridge app if needed, announces the game, and stages the
  // figures currently on the simulated portal/base into
  // toys_bridge/active/<system>/ (exported to the emulator process as
  // OMNICON_TTL_FIGURES_DIR). For amiibo-capable systems it only talks to an
  // already-running app, so ordinary games never spawn it. Never fatal.
  static void prepareForLaunch(SystemType system,
                               const std::string &contentPath);

  // True when the bridge app answers on its TCP port.
  static bool isRunning();
};
