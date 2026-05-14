#include "GameLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// ─── Static lookups ──────────────────────────────────────────────────────────

SystemType GameLibrary::systemFromExtension(const std::string& ext) {
    std::string e = toLower(ext);
    if (e == ".nes")                                           return SystemType::NES;
    if (e == ".smc" || e == ".sfc")                            return SystemType::SNES;
    if (e == ".gb")                                            return SystemType::GameBoy;
    if (e == ".gbc" || e == ".sgb")                            return SystemType::GameBoyColor;
    if (e == ".gba")                                           return SystemType::GBA;
    if (e == ".nds")                                           return SystemType::NDS;
    if (e == ".3ds" || e == ".cci" || e == ".cxi" ||
        e == ".3dsx" || e == ".cia")                            return SystemType::ThreeDS;
    if (e == ".a26")                                           return SystemType::Atari2600;
    if (e == ".a52")                                           return SystemType::Atari5200;
    if (e == ".atr" || e == ".xfd" || e == ".atx" ||
        e == ".xex" || e == ".car" || e == ".cas" || e == ".com")
                                                               return SystemType::Atari800;
    if (e == ".md" || e == ".gen" || e == ".smd" || e == ".68k" || e == ".sgd")
                                                               return SystemType::SegaGenesis;
    if (e == ".odyssey")                                       return SystemType::Odyssey;
    return SystemType::Unknown;
}

std::string GameLibrary::systemName(SystemType sys) {
    switch (sys) {
        case SystemType::NES:          return "NES";
        case SystemType::SNES:         return "SNES";
        case SystemType::GameBoy:      return "Game Boy";
        case SystemType::GameBoyColor: return "Game Boy Color";
        case SystemType::GBA:          return "GBA";
        case SystemType::NDS:          return "Nintendo DS";
        case SystemType::ThreeDS:      return "Nintendo 3DS";
        case SystemType::Atari2600:    return "Atari 2600";
        case SystemType::Atari5200:    return "Atari 5200";
        case SystemType::Atari800:     return "Atari 800";
        case SystemType::SegaGenesis: return "Sega Genesis";
        case SystemType::Odyssey:       return "Magnavox Odyssey";
        case SystemType::Steam:         return "Steam";
        default:                       return "Unknown";
    }
}

std::string GameLibrary::systemDirName(SystemType sys) {
    switch (sys) {
        case SystemType::NES:          return "nes";
        case SystemType::SNES:         return "snes";
        case SystemType::GameBoy:      return "gb";
        case SystemType::GameBoyColor: return "gbc";
        case SystemType::GBA:          return "gba";
        case SystemType::NDS:          return "nds";
        case SystemType::ThreeDS:      return "3ds";
        case SystemType::Atari2600:    return "atari2600";
        case SystemType::Atari5200:    return "atari5200";
        case SystemType::Atari800:     return "atari800";
        case SystemType::SegaGenesis: return "genesis";
        case SystemType::Odyssey:       return "odyssey";
        case SystemType::Steam:         return "steam";
        default:                       return "unknown";
    }
}

int GameLibrary::screenscraper_systemId(SystemType sys) {
    switch (sys) {
        case SystemType::NES:          return 3;
        case SystemType::SNES:         return 4;
        case SystemType::GameBoy:      return 9;
        case SystemType::GameBoyColor: return 10;
        case SystemType::GBA:          return 12;
        case SystemType::NDS:          return 15;
        case SystemType::ThreeDS:      return 17;
        case SystemType::Atari2600:    return 26;
        case SystemType::Atari5200:    return 40;
        case SystemType::Atari800:     return 43;
        case SystemType::SegaGenesis: return 1;   // Mega Drive / Genesis
        case SystemType::Odyssey:       return 0;   // no ROM dumps exist
        case SystemType::Steam:         return 0;   // not scraped
        default:                        return 0;
    }
}

// ─── cleanFileName ────────────────────────────────────────────────────────────

std::string GameLibrary::cleanFileName(const std::string& filename) {
    fs::path p(filename);
    std::string name = p.stem().string();

    // Strip parenthesised tags: (USA), (Rev 1), (SGB Enhanced), …
    name = std::regex_replace(name, std::regex(R"(\s*\([^)]*\))"), "");
    // Strip bracketed tags: [!], [cr LPS], [b], …
    name = std::regex_replace(name, std::regex(R"(\s*\[[^\]]*\])"), "");

    // Trim trailing/leading whitespace, dashes, dots
    while (!name.empty() && (name.back() == ' ' || name.back() == '-' || name.back() == '.'))
        name.pop_back();
    while (!name.empty() && name.front() == ' ')
        name.erase(name.begin());

    return name.empty() ? p.stem().string() : name;
}

// ─── Scanning ─────────────────────────────────────────────────────────────────

void GameLibrary::scanDirectory(const fs::path& dir, SystemType forceSystem) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) return;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        const std::string filename = entry.path().filename().string();
        // Skip metadata/zone files
        if (filename == ".gitkeep") continue;
        if (filename.find(':') != std::string::npos) continue;

        std::string ext = toLower(entry.path().extension().string());
        SystemType sys = (forceSystem != SystemType::Unknown)
                         ? forceSystem
                         : systemFromExtension(ext);
        if (sys == SystemType::Unknown) continue;

        GameEntry ge;
        ge.romPath    = entry.path().string();
        ge.displayName = cleanFileName(filename);
        ge.system     = sys;
        games_.push_back(std::move(ge));
    }
}

void GameLibrary::scan(const std::string& romsDir) {
    games_.clear();
    fs::path root(romsDir);

    // Flat root (detect system from extension)
    scanDirectory(root, SystemType::Unknown);

    // Per-system subdirectories
    static const struct { const char* dir; SystemType sys; } subdirs[] = {
        {"nes",       SystemType::NES},
        {"snes",      SystemType::SNES},
        {"gb",        SystemType::GameBoy},
        {"gbc",       SystemType::GameBoyColor},
        {"gba",       SystemType::GBA},
        {"nds",       SystemType::NDS},
        {"3ds",       SystemType::ThreeDS},
        {"atari2600", SystemType::Atari2600},
        {"atari5200", SystemType::Atari5200},
        {"atari800",  SystemType::Atari800},
    };
    for (const auto& sd : subdirs)
        scanDirectory(root / sd.dir, sd.sys);

    // Sort by system then display name
    std::stable_sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b) {
        if (a.system != b.system)
            return static_cast<int>(a.system) < static_cast<int>(b.system);
        return a.displayName < b.displayName;
    });
}

void GameLibrary::scanSteam() {
    namespace fs = std::filesystem;

    // Build candidate Steam roots: Linux paths first, then WSL2 Windows mounts
    std::vector<std::string> rootCandidates;
    if (const char* r = getenv("STEAM_ROOT"))
        rootCandidates.push_back(r);
    if (const char* home = getenv("HOME")) {
        for (const char* s : { "/.local/share/Steam", "/.steam/steam", "/Steam" })
            rootCandidates.push_back(std::string(home) + s);
    }
    // WSL2: Windows Steam on mounted drives (C–F)
    for (const char* drv : { "c", "d", "e", "f" }) {
        for (const char* s : { "/Program Files (x86)/Steam", "/Program Files/Steam" })
            rootCandidates.push_back(std::string("/mnt/") + drv + s);
    }

    std::string steamRoot;
    for (const auto& c : rootCandidates) {
        if (fs::exists(c + "/steamapps")) { steamRoot = c; break; }
    }
    if (steamRoot.empty()) return;

    // Detect Windows-side Steam (WSL2 mount) so we can convert paths later
    const bool isWindowsSteam = (steamRoot.rfind("/mnt/", 0) == 0);

    // Convert a raw VDF path to a usable Linux path.
    // Windows VDF paths look like "D:\\SteamLibrary" (escaped backslashes).
    auto toLinuxPath = [&](const std::string& raw) -> std::string {
        std::string p;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i+1] == '\\')
                { p += '/'; ++i; }
            else if (raw[i] == '\\')
                p += '/';
            else
                p += raw[i];
        }
        // "D:/..." → "/mnt/d/..."
        if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') {
            std::string r = "/mnt/";
            r += static_cast<char>(std::tolower(static_cast<unsigned char>(p[0])));
            r += (p.size() > 2 ? p.substr(2) : "/");
            return r;
        }
        return p;
    };

    // Collect all library paths from libraryfolders.vdf
    std::vector<std::string> libraryDirs;
    libraryDirs.push_back(steamRoot + "/steamapps");

    const std::string vdfPath = steamRoot + "/steamapps/libraryfolders.vdf";
    if (fs::exists(vdfPath)) {
        std::ifstream vdfFile(vdfPath);
        std::string vdf((std::istreambuf_iterator<char>(vdfFile)),
                         std::istreambuf_iterator<char>());
        // New VDF format (Steam 2021+): "path"   "/some/path"
        std::regex pathRe(R"re("path"\s+"([^"]+)")re");
        for (auto it = std::sregex_iterator(vdf.begin(), vdf.end(), pathRe);
             it != std::sregex_iterator(); ++it) {
            const std::string raw = (*it)[1].str();
            std::string d = (isWindowsSteam ? toLinuxPath(raw) : raw) + "/steamapps";
            if (d != libraryDirs[0] && fs::exists(d))
                libraryDirs.push_back(d);
        }
        // Old VDF format: "1"  "/some/path"  (only if new format found nothing extra)
        if (libraryDirs.size() == 1) {
            std::regex oldRe(R"re("\d+"\s+"([^"]+)")re");
            for (auto it = std::sregex_iterator(vdf.begin(), vdf.end(), oldRe);
                 it != std::sregex_iterator(); ++it) {
                const std::string raw = (*it)[1].str();
                std::string d = (isWindowsSteam ? toLinuxPath(raw) : raw) + "/steamapps";
                if (d != libraryDirs[0] && fs::exists(d))
                    libraryDirs.push_back(d);
            }
        }
    }

    const std::string appcache = steamRoot + "/appcache/librarycache";

    // Scan each library for appmanifest_*.acf files
    for (const auto& libDir : libraryDirs) {
        if (!fs::exists(libDir)) continue;
        for (const auto& entry : fs::directory_iterator(libDir)) {
            const std::string fname = entry.path().filename().string();
            if (fname.rfind("appmanifest_", 0) != 0 ||
                entry.path().extension() != ".acf") continue;

            std::ifstream acfFile(entry.path());
            std::string acf((std::istreambuf_iterator<char>(acfFile)),
                             std::istreambuf_iterator<char>());

            // Helper: extract first value for a quoted key
            auto kv = [&](const std::string& key) -> std::string {
                std::regex re('"' + key + "\"\\s+\"([^\"]+)\"");
                std::smatch m;
                if (std::regex_search(acf, m, re)) return m[1].str();
                return {};
            };

            const std::string appId     = kv("appid");
            const std::string name      = kv("name");
            const std::string flagsStr  = kv("StateFlags");
            if (appId.empty() || name.empty()) continue;

            // Validate appId is all-digits (used later in a shell command)
            if (!std::all_of(appId.begin(), appId.end(), ::isdigit)) continue;

            // StateFlags bit 2 (value 4) = fully installed
            try {
                if ((std::stoi(flagsStr) & 4) == 0) continue;
            } catch (...) { continue; }

            GameEntry ge;
            ge.romPath    = appId;
            ge.displayName = name;
            ge.system     = SystemType::Steam;

            // Look for cover art in Steam's local appcache
            for (const char* suffix : { "_library_600x900.jpg",
                                        "_library_600x900_2x.jpg",
                                        "_header.jpg" }) {
                std::string p = appcache + "/" + appId + suffix;
                if (fs::exists(p)) { ge.coverPath = p; break; }
            }

            games_.push_back(std::move(ge));
        }
    }

    // Re-sort so Steam games are ordered by name within the Steam bucket
    std::stable_sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b) {
        if (a.system != b.system)
            return static_cast<int>(a.system) < static_cast<int>(b.system);
        return a.displayName < b.displayName;
    });
}

void GameLibrary::resolveCoverPaths(const std::string& coversBaseDir) {
    for (auto& ge : games_) {
        if (!ge.coverPath.empty()) continue;  // appcache or prior download already set it
        if (ge.system == SystemType::Steam) {
            // Check OmniCon's own downloaded Steam cover cache
            std::string p = coversBaseDir + "/steam/" + ge.romPath + ".jpg";
            if (fs::exists(p)) ge.coverPath = p;
            continue;
        }
        std::string base = coversBaseDir + "/" + systemDirName(ge.system) + "/" + ge.displayName;
        for (const char* ext : {".jpg", ".jpeg", ".png"}) {
            std::string p = base + ext;
            if (fs::exists(p)) { ge.coverPath = p; break; }
        }
    }
}

std::vector<GameEntry> GameLibrary::gamesForSystem(SystemType sys) const {
    std::vector<GameEntry> result;
    for (const auto& g : games_)
        if (g.system == sys) result.push_back(g);
    return result;
}

// ─── fetchCoverArt (ScreenScraper.fr) ────────────────────────────────────────

std::string GameLibrary::fetchCoverArt(const GameEntry& game, const std::string& coversBaseDir) {
    std::string sysDir   = systemDirName(game.system);
    std::string baseDir  = coversBaseDir + "/" + sysDir;
    std::string basePath = baseDir + "/" + game.displayName;

    // Already cached?
    for (const char* ext : {".jpg", ".jpeg", ".png"}) {
        std::string p = basePath + ext;
        if (fs::exists(p)) return p;
    }

    // ── Steam: download from public CDN (no API key needed) ──────────────────
    if (game.system == SystemType::Steam) {
        // Use appId (game.romPath) as filename — more stable than display name
        const std::string dest = baseDir + "/" + game.romPath + ".jpg";
        if (fs::exists(dest)) return dest;
        fs::create_directories(baseDir);
        // Try portrait art first (600×900), fall back to landscape header
        for (const char* img : { "library_600x900.jpg", "header.jpg" }) {
            std::string url = "https://cdn.akamai.steamstatic.com/steam/apps/"
                              + game.romPath + "/" + img;
            std::string cmd = "curl -s -f -L --max-time 10 -o '" + dest + "' '" + url + "' 2>/dev/null";
            if (std::system(cmd.c_str()) == 0 && fs::exists(dest))
                return dest;
        }
        return "";
    }
    // ─────────────────────────────────────────────────────────────────────────
    const char* devid   = getenv("SS_DEVID");
    const char* devpass = getenv("SS_DEVPASSWORD");
    if (!devid || !devpass) return "";

    int sysId = screenscraper_systemId(game.system);
    if (sysId == 0) return "";

    // URL-encode ROM filename
    std::string romFilename = fs::path(game.romPath).filename().string();
    std::string encoded;
    encoded.reserve(romFilename.size() * 3);
    for (unsigned char c : romFilename) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }

    fs::create_directories(baseDir);
    std::string tmpJson  = "/tmp/omnicon_ss_" + std::to_string(getpid()) + ".json";
    std::string destPath = basePath + ".jpg";

    // Fetch metadata JSON
    std::string apiCmd =
        "curl -s --max-time 15 "
        "'https://www.screenscraper.fr/api2/jeuInfos.php"
        "?devid=" + std::string(devid) +
        "&devpassword=" + std::string(devpass) +
        "&softname=omnicon&output=json"
        "&systemeid=" + std::to_string(sysId) +
        "&romnom=" + encoded +
        "' -o '" + tmpJson + "' 2>/dev/null";
    system(apiCmd.c_str());
    if (!fs::exists(tmpJson)) return "";

    // Extract box-2D URL via python3
    std::string pyScript =
        "import json,sys\n"
        "try:\n"
        "    d=json.load(open('" + tmpJson + "'))\n"
        "    ms=d.get('response',{}).get('jeu',{}).get('medias',[])\n"
        "    u=[m.get('url','') for m in ms if m.get('type')=='box-2D' and m.get('url','')]\n"
        "    print(u[0] if u else '')\n"
        "except: pass\n";
    std::string pyCmd = "python3 -c \"" + pyScript + "\" 2>/dev/null";

    FILE* fp = popen(pyCmd.c_str(), "r");
    std::string imageUrl;
    if (fp) {
        char buf[2048] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            imageUrl = buf;
            while (!imageUrl.empty() &&
                   (imageUrl.back() == '\n' || imageUrl.back() == '\r' || imageUrl.back() == ' '))
                imageUrl.pop_back();
        }
        pclose(fp);
    }
    fs::remove(tmpJson);
    if (imageUrl.empty()) return "";

    // Download image
    std::string dlCmd = "curl -s --max-time 30 '" + imageUrl + "' -o '" + destPath + "' 2>/dev/null";
    int rc = system(dlCmd.c_str());
    if (rc != 0 || !fs::exists(destPath)) return "";

    return destPath;
}
