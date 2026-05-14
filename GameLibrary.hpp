#pragma once

#include <string>
#include <vector>
#include <filesystem>

enum class SystemType {
    NES,
    SNES,
    GameBoy,
    GameBoyColor,
    GBA,
    NDS,
    ThreeDS,
    Atari2600,
    Atari5200,
    Atari800,
    SegaGenesis,
    Odyssey,
    Steam,
    Unknown
};

struct GameEntry {
    std::string romPath;
    std::string displayName;
    std::string coverPath;   // empty if not yet resolved
    SystemType  system = SystemType::Unknown;
};

class GameLibrary {
public:
    // Scan romsDir (flat root + per-system subdirs) for supported ROMs.
    void scan(const std::string& romsDir);

    // Scan the local Steam installation for installed games.
    // Appended after scan(); safe to call even if Steam is not installed.
    void scanSteam();

    // Populate coverPath on each entry from cached files in coversBaseDir.
    void resolveCoverPaths(const std::string& coversBaseDir);

    const std::vector<GameEntry>& games() const { return games_; }
    std::vector<GameEntry>&       games()       { return games_; }

    std::vector<GameEntry> gamesForSystem(SystemType sys) const;

    // Network-fetch cover from ScreenScraper.fr (requires env vars SS_DEVID / SS_DEVPASSWORD).
    // Returns local path on success, "" on failure.
    std::string fetchCoverArt(const GameEntry& game, const std::string& coversBaseDir);

    static SystemType   systemFromExtension(const std::string& ext);
    static std::string  cleanFileName(const std::string& filename);
    static std::string  systemName(SystemType sys);
    static std::string  systemDirName(SystemType sys);
    static int          screenscraper_systemId(SystemType sys);

private:
    std::vector<GameEntry> games_;
    void scanDirectory(const std::filesystem::path& dir, SystemType forceSystem);
};
