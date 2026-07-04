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
    N64,
    PS1,
    PS2,
    PS3,
    GameCube,
    Odyssey,
    Steam,
    Unknown
};

struct GameEntry {
    std::string romPath;      // local ROM path (empty if not yet downloaded)
    std::string downloadUrl;  // source URL from DB (empty for purely local ROMs)
    std::string displayName;
    std::string coverPath;    // empty if not yet resolved
    SystemType  system = SystemType::Unknown;
    int         dbId   = 0;   // Oracle DB row id (0 if not in DB)
};

// Forward-declare so GameLibrary::loadFromDB can use it without a full include chain
struct OracleDBConfig;

class GameLibrary {
public:
    // Scan romsDir (flat root + per-system subdirs) for supported ROMs.
    void scan(const std::string& romsDir);

    // Load entries from the Oracle GAMES table (status != 'failed').
    // Appended after scan(); safe to call even if DB is unavailable.
    void loadFromDB(const OracleDBConfig& cfg);

    // Scan the local Steam installation for installed games.
    void scanSteam();

    // Scan Steam-owned games via the Steam Web API.
    // apiKey and steamId are saved to credFile on first use.
    void scanSteamOwned(const std::string& credFile);

    // Populate coverPath on each entry from cached files in coversBaseDir.
    void resolveCoverPaths(const std::string& coversBaseDir);

    const std::vector<GameEntry>& games() const { return games_; }
    std::vector<GameEntry>&       games()       { return games_; }

    std::vector<GameEntry> gamesForSystem(SystemType sys) const;

    // Network-fetch cover from ScreenScraper.fr (or Steam CDN for Steam entries)
    std::string fetchCoverArt(const GameEntry& game, const std::string& coversBaseDir);

    // Download missing Steam cover images from the Steam CDN for all Steam entries.
    // Saves to coversDir/steam/{appid}.jpg and updates coverPath in-place.
    void fetchSteamCovers(const std::string& coversDir);

    static SystemType   systemFromExtension(const std::string& ext);
    static SystemType   systemFromName(const std::string& name); // reverse of systemName()
    static SystemType   systemFromUrl(const std::string& url);  // infer from URL path keywords
    static std::string  cleanFileName(const std::string& filename);
    static std::string  systemName(SystemType sys);
    static std::string  systemDirName(SystemType sys);
    static int          screenscraper_systemId(SystemType sys);

private:
    std::vector<GameEntry> games_;
    void scanDirectory(const std::filesystem::path& dir, SystemType forceSystem);
};
