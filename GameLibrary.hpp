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
    GameCube,
    N64,
    NDS,
    ThreeDS,
    Wii,
    WiiU,
    NintendoSwitch,
    NintendoSwitch2,
    VirtualBoy,
    PokemonMini,
    FamicomDiskSystem,
    GameAndWatch, // arguable, but generally treated as a platform family
    Satellaview, 
    ColorTVGame,
    Atari2600,
    Atari5200,
    Atari800,
    SegaGenesis,
    PS1,
    PS2,
    PS3,
    PS4,
    PS5,
    PSP,
    PSVita,
    PSVR,
    PSVR2,
    PocketStation, // Japan-only, had exclusive software
    PSX,         // the Japanese PS2/DVR hybrid, had some unique software
    Odyssey,
    PC,
    Dreamcast,
    SegaMasterSystem,
    GameGear,
    SegaSaturn,
    Xbox,
    Xbox360,
    XboxOne,
    XboxSeriesX,
    Atari7800,
    AtariLynx,
    AtariJaguar, 
    AtariXEGS,
    AtariVCS,
    PCEngine,
    ColecoVision,
    Intellivision,
    SG1000,
    SegaCD, 
    Sega32X,
    SegaPico,
    Teradrive,
    NeoGeoAES,
    NeoGeoCD,
    NeoGeoPocket,
    NeoGeoMVS,
    HyperNeoGeo64,
    NeoGeoPocketColor,
    PCFX,
    SuperGrafx,
    Panasonic3DO, 
    CDi,
    AmigaCD32, 
    FMTownsMarty,
    WonderSwan,
    WonderswanColor,
    SwanCrystal,
    Playdate,
    TigerGameCom,
    NokiaNGage,
    Gizmondo,
    FairchildChannelF,
    RCAStudioII,
    BallyAstrocade,
    Vectrex,
    Odyssey2,
    Ouya,
    Evercade,
    Polymega,
    MetaQuest,
    MetaQuest2,
    MetaQuest3,
    MetaQuestPro,
    OculusRift,
    HTCVive,
    ValveIndex,
    Pico4,
    AppleVisionPro,
    CasioLoopy,
    ApplePippin,
    BandaiPlaydia,
    EpochSuperCassetteVision,
    APFM1000,
    EmersonArcadia2001,
    IntertonVC4000,
    CreatiVision,
    Zeebo,
    GameStick,
    NvidiaShield,
    Atomiswave,
    Naomi,
    Naomi2,
    CPS1,
    CPS2,
    CPS3,
    PCEngineCD,  // PC Engine CD-ROM² / Super CD-ROM² (if you're counting Sega CD and Famicom Disk System as separate)
    SegaModel1,
    SegaModel2,
    SegaModel3,
    SegaSystem16,
    SegaSystem32,
    SegaHikaru,
    SegaChihiro,
    Triforce, // Nintendo/Sega/Namco
    NamcoSystem22,
    NamcoSystem23,
    TaitoTypeX,
    TaitoTypeX2,
    CaveCV1000,
    KonamiGX,
    IGS_PGM,
    IGS_PGM2,
    Microvision,
    WataraSupervision,
    HartungGameMaster,
    GP32,
    GP2X,
    TapwaveZodiac,
    AdventureVision,
    LaserActive,
    BandaiRX78,
    GakkenCompactVision,
    SuperACan,
    FMM7, // arguable
    VMLabsNUON, 
    CapcomCPSChanger,
    NamcoSystem246,
    NamcoSystem256,
    SegaRingEdge,
    SegaRingWide,
    SegaLindbergh,
    TaitoTypeXPlus,
    TaitoTypeX3,
    KonamiBemaniPCPlatform,
    Arcade, //generic catagory will bunch many consoles into this catagory
    Steam,//both steam deck and steam machine  
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
