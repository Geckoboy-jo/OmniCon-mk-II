#pragma once
#include "ConsoleLaunchers.hpp"

class RetroArchConsoleLauncher : public ConsoleLauncher {
protected:
  static bool launchRetroArch(const std::string &systemName,
                              const char *coreEnvironmentVariable,
                              const char *defaultCore,
                              const std::string &contentPath);

  // Warns about missing BIOS/system files, then launches. requiredFiles uses
  // ';' between required entries and '|' between alternatives for one entry.
  static bool launchRetroArchWithSystemFiles(
      const std::string &systemName, const char *coreEnvironmentVariable,
      const char *defaultCore, const std::string &contentPath,
      const char *requiredFiles);
  static void configureGpuEnvironment();
};
#include <cstdlib>
// Concrete launchers derive from the class above, so they are pulled in last.
#include "Atari7800.hpp"
#include "AtariJaguar.hpp"
#include "AtariLynx.hpp"
#include "ColecoVision.hpp"
#include "EpicGames.hpp"
#include "ExtraConsoleLaunchers.hpp"
#include "Intellevision.hpp"
#include "Minecraft.hpp"
#include "NintendoSwitch.hpp"
#include "PCEngine.hpp"
#include "PlayStationPortable.hpp"
#include "PsVita.hpp"
#include "SegaMasterSystem.hpp"
#include "SegaSaturn.hpp"
#include "Wii.hpp"
#include "WiiU.hpp"
#include "Xbox.hpp"
#include "Xbox360.hpp"
#include "XboxCloud.hpp"
#include "gameGear.hpp"
