#pragma once
#include "ConsoleLaunchers.hpp"

class RetroArchConsoleLauncher : public ConsoleLauncher {
protected:
    static bool launchRetroArch(const std::string& systemName,
                                const char* coreEnvironmentVariable,
                                const char* defaultCore,
                                const std::string& contentPath);

    // Warns about missing BIOS/system files, then launches. requiredFiles uses
    // ';' between required entries and '|' between alternatives for one entry.
    static bool launchRetroArchWithSystemFiles(const std::string& systemName,
                                               const char* coreEnvironmentVariable,
                                               const char* defaultCore,
                                               const std::string& contentPath,
                                               const char* requiredFiles);
};

// Concrete launchers derive from the class above, so they are pulled in last.
#include "SegaMasterSystem.hpp"
#include "gameGear.hpp"
#include "SegaSaturn.hpp"
#include "Wii.hpp"
#include "WiiU.hpp"
#include "NintendoSwitch.hpp"
#include "PlayStationPortable.hpp"
#include "PsVita.hpp"
#include "Xbox.hpp"
#include "Xbox360.hpp"
#include "XboxCloud.hpp"
#include "Atari7800.hpp"
#include "AtariLynx.hpp"
#include "AtariJaguar.hpp"
#include "PCEngine.hpp"
#include "ColecoVision.hpp"
#include "Intellevision.hpp"
#include "ExtraConsoleLaunchers.hpp"