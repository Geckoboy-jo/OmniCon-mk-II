#pragma once

#include "GameEntry.hpp"
#include "SystemType.hpp"
#include <filesystem>
#include <string>
#include <vector>

// Forward-declare so GameLibrary::loadFromDB can use it without a full include
// chain
struct OracleDBConfig;

class GameLibrary {
public:
  // Scan romsDir (flat root + per-system subdirs) for supported ROMs.
  void scan(const std::string &romsDir);

  // Load entries from the Oracle GAMES table (status != 'failed').
  // Appended after scan(); safe to call even if DB is unavailable.
  void loadFromDB(const OracleDBConfig &cfg);

  // Scan the local Steam installation for installed games.
  void scanSteam();

  // Scan Steam-owned games via the Steam Web API.
  // apiKey and steamId are saved to credFile on first use.
  void scanSteamOwned(const std::string &credFile);

  // Fill achievementPercent on Steam entries via the Steam Web API
  // (GetPlayerAchievements), using the same credentials as scanSteamOwned().
  // Results are cached in cacheFile so only stale entries hit the network.
  void fetchSteamAchievements(const std::string &credFile,
                              const std::string &cacheFile);

  // Load the signed-in account's Xbox Cloud Gaming (xCloud) library as Xbox One
  // and Xbox Series X|S entries via scripts/xcloud_library.py. Does nothing
  // until the user has signed in with the 'xbox-login' command. Entries carry
  // the Microsoft Store product id in romPath; covers are cached in coversDir.
  void scanXboxCloud(const std::string &cacheFile, const std::string &coversDir,
                     bool forceRefresh = false);

  // Populate coverPath on each entry from cached files in coversBaseDir.
  void resolveCoverPaths(const std::string &coversBaseDir);

  const std::vector<GameEntry> &games() const { return games_; }
  std::vector<GameEntry> &games() { return games_; }

  std::vector<GameEntry> gamesForSystem(SystemType sys) const;

  // Network-fetch cover from ScreenScraper.fr (or Steam CDN for Steam entries)
  std::string fetchCoverArt(const GameEntry &game,
                            const std::string &coversBaseDir);

  // Download missing Steam cover images from the Steam CDN for all Steam
  // entries. Saves to coversDir/steam/{appid}.jpg and updates coverPath
  // in-place.
  void fetchSteamCovers(const std::string &coversDir);

  static SystemType systemFromExtension(const std::string &ext);
  static SystemType
  systemFromName(const std::string &name); // reverse of systemName()
  static SystemType
  systemFromUrl(const std::string &url); // infer from URL path keywords
  static std::string cleanFileName(const std::string &filename);

  // The file Xenia should open inside an Xbox 360 game folder: default.xex, an
  // image (.iso/.xex/.zar/.god), or a Games on Demand package (an
  // extension-less file with a sibling "<name>.data" folder). "" if none.
  static std::string findXbox360Target(const std::filesystem::path &root);

  // True for an STFS package (Games on Demand, Arcade, installed game) that
  // Xenia can boot; false for DLC, title updates and other content packages.
  static bool isBootableXbox360Package(const std::filesystem::path &file);
  static std::string systemName(SystemType sys);
  static std::string systemDirName(SystemType sys);
  static int screenscraper_systemId(SystemType sys);

private:
  std::vector<GameEntry> games_;
  void scanDirectory(const std::filesystem::path &dir, SystemType forceSystem);
};
