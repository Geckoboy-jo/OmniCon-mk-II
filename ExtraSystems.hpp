#pragma once

#include "SystemType.hpp"

#include <string>
#include <vector>

enum class SystemFamily {
  Nintendo,
  Sega,
  Sony,
  Microsoft,
  Atari,
  NEC,
  SNK,
  Arcade,
  Other
};
constexpr int kNumSystemFamilies = 9;

enum class ExtraBackend {
  Libretro,    // launched by the per-system RetroArch launcher classes
  MameMachine, // MAME console machine; content is attached as media
  MameArcade   // content is a MAME romset zip, launched by its basename
};

// Systems that run through an external emulator. The table drives the names,
// folders, extensions and badge colours used by GameLibrary and GameShell.
struct ExtraSystemInfo {
  SystemType type;
  const char *name;
  const char *dir;
  const char *abbrev;
  int screenscraperId;
  unsigned char color[3];
  const char *extensions; // accepted when scanning the system's folder
  const char
      *detectExtensions; // unambiguous extensions usable from a flat ROM folder
  ExtraBackend backend = ExtraBackend::Libretro;
  const char *machine = ""; // MAME machine (MameMachine only)
  const char *media = "";   // MAME media switch, e.g. "cart"
  const char *envVar = "";  // env var overriding the MAME machine
  const char *bios = "";    // required system files; ';' = all, '|' = any of
};

const std::vector<ExtraSystemInfo> &extraSystems();
const ExtraSystemInfo *findExtraSystem(SystemType type);
const ExtraSystemInfo *findExtraSystemByName(const std::string &name);
SystemType extraSystemFromExtension(const std::string &ext);
bool extraSystemAcceptsExtension(SystemType type, const std::string &ext);
bool launchExtraSystem(SystemType type, const std::string &contentPath);

// True if any extra system accepts the extension (with or without leading dot).
bool extraSystemKnowsExtension(const std::string &ext);
// Systems whose content is a zip romset must be stored as downloaded, not
// unpacked.
bool extraSystemKeepsArchive(SystemType type);
// Guess a system from collection-style URL fragments, e.g. "/odyssey2/" or
// "sega_model_2".
SystemType extraSystemFromUrl(const std::string &url);

SystemFamily systemFamily(SystemType type);
const char *systemFamilyName(SystemFamily family);
