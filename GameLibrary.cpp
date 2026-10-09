#include "GameLibrary.hpp"
#include "OracleDB.hpp"
#include "DownloadManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// ─── Static lookups ──────────────────────────────────────────────────────────

// Infer system from known URL patterns (e.g. archive.org collection names).
// Returns Unknown if no pattern matched.
SystemType GameLibrary::systemFromUrl(const std::string& url) {
    std::string u = toLower(url);
    // archive.org collection paths: /roms_nes/, /nes_roms/, etc.
    auto has = [&](const char* s) { return u.find(s) != std::string::npos; };
    if (has("/roms_nes") || has("/nes_roms") || has("nintendo_nes") || has("famicom_roms"))
        return SystemType::NES;
    if (has("/roms_snes") || has("/snes_roms") || has("super_nintendo"))
        return SystemType::SNES;
    if (has("/roms_gba") || has("/gba_roms") || has("gameboy_advance"))
        return SystemType::GBA;
    if (has("/roms_gbc") || has("/gbc_roms") || has("gameboy_color"))
        return SystemType::GameBoyColor;
    if (has("/roms_gb") || has("/gb_roms") || has("gameboy_roms"))
        return SystemType::GameBoy;
    if (has("/roms_nds") || has("/nds_roms") || has("nintendo_ds"))
        return SystemType::NDS;
    if (has("/roms_n64") || has("/n64_roms") || has("nintendo_64"))
        return SystemType::N64;
    if (has("/roms_gamecube") || has("/gamecube_roms") || has("nintendo_gamecube") ||
        has("/roms_gcn") || has("/gcn_roms") || has("gamecube_iso"))
        return SystemType::GameCube;
    if (has("/roms_dreamcast") || has("/dreamcast_roms") ||
        has("sega_dreamcast") || has("sega-dreamcast"))
        return SystemType::Dreamcast;
    if (has("/roms_pc") || has("/pc_games") || has("pc_game") || has("pc_redump"))
        return SystemType::PC;
    if (has("/roms_genesis") || has("/genesis_roms") || has("/roms_sega") || has("sega_genesis"))
        return SystemType::SegaGenesis;
    if (has("/roms_ps1") || has("/ps1_roms") || has("playstation_roms") || has("psx_roms") || has("/roms_psx"))
        return SystemType::PS1;
    if (has("/roms_ps2") || has("/ps2_roms") || has("playstation2_roms") || has("ps2_iso"))
        return SystemType::PS2;
    if (has("/roms_ps3") || has("/ps3_roms") || has("playstation3_roms") || has("ps3_iso"))
        return SystemType::PS3;
    if (has("/roms_gba") || has("gameboy_advance"))
        return SystemType::GBA;
    if (has("atari_2600") || has("roms_2600") || has("atari2600"))
        return SystemType::Atari2600;
    if (has("atari_5200") || has("roms_5200") || has("atari5200"))
        return SystemType::Atari5200;
    if (has("atari_800") || has("roms_800") || has("atari800"))
        return SystemType::Atari800;
    return SystemType::Unknown;
}

// Return the ROM extension from a filename or URL path, stripping archive wrappers.
// e.g. "game.nes.zip" → ".nes",  "game.smc.gz" → ".smc",  "game.nes" → ".nes"
static std::string romExtension(const std::string& path) {
    // Strip query string / fragment first
    std::size_t qpos = path.find_first_of("?#");
    std::string clean = (qpos != std::string::npos) ? path.substr(0, qpos) : path;
    fs::path p(clean);
    std::string ext = toLower(p.extension().string());
    // If the outermost extension is an archive wrapper, peel it off
    static const char* const kArchive[] = { ".zip", ".7z", ".gz", ".bz2", ".xz", ".rar", nullptr };
    for (int i = 0; kArchive[i]; ++i) {
        if (ext == kArchive[i]) {
            std::string inner = toLower(fs::path(p.stem()).extension().string());
            if (!inner.empty()) return inner;
            break;
        }
    }
    return ext;
}

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
    if (e == ".n64" || e == ".z64" || e == ".v64")            return SystemType::N64;
    if (e == ".gcm" || e == ".gcz" || e == ".rvz" || e == ".ciso")
                                                               return SystemType::GameCube;
    if (e == ".cue" || e == ".iso" || e == ".pbp" || e == ".chd" || e == ".img" || e == ".mdf")
                                                               return SystemType::PS1;
    if (e == ".pkg")
                                                               return SystemType::PS3;
    if (e == ".exe")
                                                               return SystemType::PC;
    if (e == ".gdi" || e == ".cdi" || e == ".chd")
                                                               return SystemType::Dreamcast;
    if (e == ".odyssey")                                       return SystemType::Odyssey;
    return SystemType::Unknown;
}

SystemType GameLibrary::systemFromName(const std::string& name) {
    if (name == "NES")              return SystemType::NES;
    if (name == "SNES")             return SystemType::SNES;
    if (name == "Game Boy")         return SystemType::GameBoy;
    if (name == "Game Boy Color")   return SystemType::GameBoyColor;
    if (name == "GBA")              return SystemType::GBA;
    if (name == "Nintendo DS")      return SystemType::NDS;
    if (name == "Nintendo 3DS")     return SystemType::ThreeDS;
    if (name == "Atari 2600")       return SystemType::Atari2600;
    if (name == "Atari 5200")       return SystemType::Atari5200;
    if (name == "Atari 800")        return SystemType::Atari800;
    if (name == "Sega Genesis")     return SystemType::SegaGenesis;
    if (name == "Nintendo 64")      return SystemType::N64;
    if (name == "PlayStation" || name == "PS1") return SystemType::PS1;
    if (name == "PlayStation 2" || name == "PS2") return SystemType::PS2;
    if (name == "PlayStation 3" || name == "PS3") return SystemType::PS3;
    if (name == "Nintendo GameCube" || name == "GameCube" || name == "GCN") return SystemType::GameCube;
    if (name == "Magnavox Odyssey") return SystemType::Odyssey;
    if (name == "PC" || name == "PC Games") return SystemType::PC;
    if (name == "Dreamcast" || name == "Sega Dreamcast") return SystemType::Dreamcast;
    if (name == "Steam")            return SystemType::Steam;
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
        case SystemType::N64:          return "Nintendo 64";
        case SystemType::PS1:          return "PlayStation";
        case SystemType::PS2:          return "PlayStation 2";
        case SystemType::PS3:          return "PlayStation 3";
        case SystemType::GameCube:     return "Nintendo GameCube";
        case SystemType::Odyssey:       return "Magnavox Odyssey";
        case SystemType::PC:            return "PC Games";
        case SystemType::Dreamcast:     return "Dreamcast";
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
        case SystemType::N64:          return "n64";
        case SystemType::PS1:          return "ps1";
        case SystemType::PS2:          return "ps2";
        case SystemType::PS3:          return "ps3";
        case SystemType::GameCube:     return "gamecube";
        case SystemType::Odyssey:       return "odyssey";
        case SystemType::PC:            return "pc";
        case SystemType::Dreamcast:     return "dreamcast";
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
        case SystemType::N64:          return 14;  // Nintendo 64
        case SystemType::PS1:          return 57;  // PlayStation
        case SystemType::PS2:          return 58;  // PlayStation 2
        case SystemType::PS3:          return 59;  // PlayStation 3
        case SystemType::GameCube:     return 13;  // Nintendo GameCube
        case SystemType::Odyssey:       return 0;   // no ROM dumps exist
        case SystemType::PC:            return 0;
        case SystemType::Dreamcast:     return 23;
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
        if (forceSystem == SystemType::Unknown && ext == ".cue") {
            fs::path binPath = entry.path();
            binPath.replace_extension(".bin");
            if (fs::exists(binPath)) continue;
        }
        SystemType sys = (forceSystem != SystemType::Unknown)
                         ? forceSystem
                         : systemFromExtension(ext);
        if (forceSystem == SystemType::PC &&
            ext != ".exe" && ext != ".bin" && ext != ".cue")
            continue;
        if (forceSystem == SystemType::Dreamcast &&
            ext != ".gdi" && ext != ".cdi" && ext != ".chd" &&
            ext != ".cue" && ext != ".bin")
            continue;
        if (forceSystem == SystemType::Unknown && ext == ".bin") {
            fs::path cuePath = entry.path();
            cuePath.replace_extension(".cue");
            if (fs::exists(cuePath)) {
                std::ifstream cue(cuePath);
                std::string cueText((std::istreambuf_iterator<char>(cue)),
                                    std::istreambuf_iterator<char>());
                std::transform(cueText.begin(), cueText.end(), cueText.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                sys = cueText.find("MODE2/") != std::string::npos
                    ? SystemType::PS1 : SystemType::PC;
            }
        }
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
        {"genesis",   SystemType::SegaGenesis},
        {"n64",       SystemType::N64},
        {"ps1",       SystemType::PS1},
        {"ps2",       SystemType::PS2},
        {"gamecube",  SystemType::GameCube},
        {"odyssey",   SystemType::Odyssey},
        {"pc",        SystemType::PC},
        {"dreamcast", SystemType::Dreamcast},
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

// ─── loadFromDB ──────────────────────────────────────────────────────────────

void GameLibrary::loadFromDB(const OracleDBConfig& cfg) {
    std::cout << "[DB] loadFromDB: querying GAMES table...\n"; std::cout.flush();
    std::vector<OracleGameRow> rows;
    if (!queryGames(cfg, rows)) {
        // Error already printed inside queryGames
        return;
    }
    std::cout << "[DB] loadFromDB: query returned " << rows.size() << " row(s).\n"; std::cout.flush();
    if (rows.empty()) {
        std::cout << "[DB] GAMES table is empty — nothing to add.\n";
        return;
    }

    // Build a set of download URLs already present from local scan to avoid duplicates
    std::cout << "[DB] loadFromDB: building dedup set from " << games_.size() << " existing game(s)...\n"; std::cout.flush();
    std::set<std::string> existingUrls;
    for (const auto& g : games_)
        if (!g.downloadUrl.empty()) existingUrls.insert(g.downloadUrl);
    std::cout << "[DB] loadFromDB: " << existingUrls.size() << " existing URL(s) to deduplicate against.\n"; std::cout.flush();

    int added = 0, skipped = 0;
    for (const auto& row : rows) {
        // Skip if already represented by a local file
        if (!row.downloadUrl.empty() && existingUrls.count(row.downloadUrl)) { ++skipped; continue; }

        GameEntry ge;
        ge.dbId        = row.id;
        ge.downloadUrl = row.downloadUrl;
        // Decode the stored name and strip archive/ROM extension while preserving
        // version/region/demo tags: (USA), (En), (Demo), (Rev 1.1) etc.
        {
            // Percent-decode: "%20" → ' ', "%28" → '(', etc.
            std::string decoded;
            decoded.reserve(row.name.size());
            for (size_t i = 0; i < row.name.size(); ) {
                if (row.name[i] == '%' && i + 2 < row.name.size()) {
                    auto hexVal = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int h = hexVal(row.name[i+1]), l = hexVal(row.name[i+2]);
                    if (h >= 0 && l >= 0) { decoded += static_cast<char>(h * 16 + l); i += 3; continue; }
                }
                if (row.name[i] == '+') { decoded += ' '; ++i; continue; }
                decoded += row.name[i++];
            }
            std::replace(decoded.begin(), decoded.end(), '_', ' ');

            // Paren-aware extension stripper: skips dots inside (...)/[...] so
            // that version tags like "(Rev 1.1)" are never split at their dot.
            static const std::set<std::string> kArcExts = {
                ".zip",".7z",".gz",".bz2",".xz",".rar",".tar"
            };
            static const std::set<std::string> kRomExts = {
                ".nes",".smc",".sfc",".gb",".gbc",".gba",".nds",".n64",".z64",".v64",
                ".iso",".bin",".cue",".rom",".md",".gen",".smd",".a26",".a52",".cdi",
                ".chd",".pbp",".img",".mdf",".gcm",".gcz",".rvz",".wbfs",".ciso",
                ".3ds",".cia",".a78",".a800"
            };
            auto parenStripExt = [](const std::string& s,
                                    const std::set<std::string>& known)
                                  -> std::pair<std::string, bool>
            {
                int depth = 0, dotPos = -1;
                for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
                    if      (s[i] == ')' || s[i] == ']') ++depth;
                    else if ((s[i] == '(' || s[i] == '[') && depth > 0) --depth;
                    else if (s[i] == '.' && depth == 0)  { dotPos = i; break; }
                }
                if (dotPos < 0) return {s, false};
                std::string ext = toLower(s.substr(dotPos));
                if (known.count(ext)) return {s.substr(0, dotPos), true};
                return {s, false};
            };

            auto [afterArc, wasArc] = parenStripExt(decoded,   kArcExts);
            auto [afterRom, wasRom] = parenStripExt(afterArc,  kRomExts);
            std::string base = wasRom ? afterRom : afterArc;

            // Fallback: strip whatever extension is present paren-aware
            if (!wasArc && !wasRom) {
                int depth = 0, dotPos = -1;
                for (int i = static_cast<int>(decoded.size()) - 1; i >= 0; --i) {
                    if      (decoded[i] == ')' || decoded[i] == ']') ++depth;
                    else if ((decoded[i] == '(' || decoded[i] == '[') && depth > 0) --depth;
                    else if (decoded[i] == '.' && depth == 0)  { dotPos = i; break; }
                }
                base = (dotPos > 0) ? decoded.substr(0, dotPos) : decoded;
            }

            // Trim only leading/trailing spaces and dashes; tag content is untouched
            while (!base.empty() && (base.front() == ' ' || base.front() == '-')) base.erase(base.begin());
            while (!base.empty() && (base.back()  == ' ' || base.back()  == '-')) base.pop_back();

            ge.displayName = base.empty() ? row.name : base;
        }

        // Prefer the stored system name; fall back to extension detection.
        // romExtension() strips archive wrappers (.zip, .gz, etc.) so that
        // "game.nes.zip" is correctly identified as NES.
        ge.system = systemFromName(row.systemName);
        const SystemType urlSystem = systemFromUrl(row.downloadUrl);
        const std::string sourceExtension = romExtension(
            row.localPath.empty() ? row.downloadUrl : row.localPath);
        if (urlSystem == SystemType::Dreamcast &&
            (sourceExtension == ".chd" || sourceExtension == ".cdi" ||
             sourceExtension == ".gdi"))
            ge.system = SystemType::Dreamcast;
        if (ge.system == SystemType::Unknown) {
            const std::string& src = row.downloadUrl.empty() ? row.localPath : row.downloadUrl;
            ge.system = systemFromExtension(romExtension(src));
        }
        // Last resort: infer from URL path (e.g. archive.org collection name)
        if (ge.system == SystemType::Unknown && !row.downloadUrl.empty())
            ge.system = systemFromUrl(row.downloadUrl);

        // If already downloaded, point at the local file (extension may refine system)
        if (!row.localPath.empty() && fs::exists(row.localPath)) {
            ge.romPath = row.localPath;
            SystemType extSys = systemFromExtension(romExtension(row.localPath));
            const bool ambiguousDreamcastMedia =
                ge.system == SystemType::Dreamcast &&
                toLower(romExtension(row.localPath)) == ".chd";
            if (extSys != SystemType::Unknown && !ambiguousDreamcastMedia)
                ge.system = extSys;
        }
        if (ge.romPath.empty() && !row.localPath.empty()) {
            const fs::path expectedName = fs::path(row.localPath).filename();
            std::error_code searchEc;
            for (const auto& entry : fs::recursive_directory_iterator("Roms", searchEc)) {
                if (searchEc) break;
                if (entry.is_regular_file(searchEc) &&
                    entry.path().filename() == expectedName) {
                    ge.romPath = entry.path().string();
                    break;
                }
            }
        }
        if (ge.romPath.empty() && !row.downloadUrl.empty()) {
            const std::string cachedPath =
                DownloadManager::instance().localPath(row.downloadUrl);
            if (!cachedPath.empty() && fs::exists(cachedPath))
                ge.romPath = cachedPath;
        }

        std::cout << "[DB] + [" << systemName(ge.system) << "] " << ge.displayName
                  << " (id=" << row.id << ")";
        if (!ge.romPath.empty())
            std::cout << " [" << ge.romPath << "]";
        std::cout << "\n";
        std::cout.flush();
        games_.push_back(std::move(ge));
        ++added;
    }

    std::cout << "[DB] loadFromDB: " << added << " added, " << skipped << " skipped (duplicates).\n"; std::cout.flush();
    if (added > 0) {
        std::cout << "[DB] loadFromDB: sorting library...\n"; std::cout.flush();
        std::stable_sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b) {
            if (a.system != b.system)
                return static_cast<int>(a.system) < static_cast<int>(b.system);
            return a.displayName < b.displayName;
        });
        std::cout << "[DB] Loaded " << added << " game(s) from Oracle.\n"; std::cout.flush();
    }
}

// ─── scanSteamOwned (Web API) ────────────────────────────────────────────────

static std::string fetchString(const std::string& url) {
    std::string q = "'";
    for (char c : url) { if (c == '\'') q += "'\"'\"'"; else q += c; }
    q += "'";
    std::string cmd = "curl --location --fail --silent --show-error " + q;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    char buf[8192];
    std::ostringstream ss;
    while (fgets(buf, sizeof(buf), f)) ss << buf;
    if (pclose(f) != 0) return "";
    return ss.str();
}

static std::string readCredFile(const std::string& path, const std::string& key) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        // trim
        while (!k.empty() && (k.front()==' '||k.front()=='\t')) k.erase(k.begin());
        while (!k.empty() && (k.back()==' '||k.back()=='\t'))  k.pop_back();
        if (k == key) {
            std::string v = line.substr(eq + 1);
            while (!v.empty() && (v.front()==' '||v.front()=='\t')) v.erase(v.begin());
            while (!v.empty() && (v.back()==' '||v.back()=='\t'||v.back()=='\r'||v.back()=='\n')) v.pop_back();
            return v;
        }
    }
    return "";
}

void GameLibrary::scanSteamOwned(const std::string& credFile) {
    // Load or prompt for API key + SteamID
    std::string apiKey  = readCredFile(credFile, "steam_api_key");
    std::string steamId = readCredFile(credFile, "steam_id");

    if (apiKey.empty() || steamId.empty()) {
        std::cout << "[Steam] Enter Steam Web API credentials (saved to " << credFile << "):\n";
        if (apiKey.empty()) {
            std::cout << "  Steam API Key (from https://steamcommunity.com/dev/apikey): ";
            std::getline(std::cin, apiKey);
        }
        if (steamId.empty()) {
            std::cout << "  SteamID64 (numeric, e.g. 76561198XXXXXXXXX): ";
            std::getline(std::cin, steamId);
        }
        if (!apiKey.empty() && !steamId.empty()) {
            // Persist
            fs::create_directories(fs::path(credFile).parent_path());
            std::ofstream f(credFile, std::ios::app);
            if (!readCredFile(credFile, "steam_api_key").empty() == false)
                f << "steam_api_key=" << apiKey << "\n";
            if (!readCredFile(credFile, "steam_id").empty() == false)
                f << "steam_id=" << steamId << "\n";
        }
    }

    if (apiKey.empty() || steamId.empty()) {
        std::cerr << "[Steam] API key or SteamID missing. Skipping Web API scan.\n";
        return;
    }

    // Fetch owned games
    std::string url = "http://api.steampowered.com/IPlayerService/GetOwnedGames/v0001/"
                      "?key=" + apiKey
                    + "&steamid=" + steamId
                      + "&include_appinfo=1&include_played_free_games=1&format=json";
    std::string json = fetchString(url);
    if (json.empty()) {
        std::cerr << "[Steam] Failed to fetch owned games from Steam Web API.\n";
        return;
    }

    // Build set of AppIDs already in games_ (local scan + prior API call)
    std::set<std::string> existing;
    for (const auto& g : games_)
        if (g.system == SystemType::Steam) existing.insert(g.romPath);

    // Parse JSON with regex: "appid":N,"name":"GameName"
    std::regex appRe(R"x("appid"\s*:\s*(\d+)[^}]*?"name"\s*:\s*"([^"]+)")x");
    int added = 0;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), appRe);
         it != std::sregex_iterator(); ++it) {
        std::string appId = (*it)[1].str();
        std::string name  = (*it)[2].str();
        if (existing.count(appId)) continue; // already in list (installed locally)

        GameEntry ge;
        ge.romPath     = appId;
        ge.displayName = name;
        ge.system      = SystemType::Steam;
        games_.push_back(std::move(ge));
        existing.insert(appId);
        ++added;
    }

    if (added > 0) {
        std::stable_sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b) {
            if (a.system != b.system)
                return static_cast<int>(a.system) < static_cast<int>(b.system);
            return a.displayName < b.displayName;
        });
        std::cout << "[Steam] Loaded " << added << " owned game(s) from Web API.\n";
    }
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

// ─── fetchSteamCovers ────────────────────────────────────────────────────────

void GameLibrary::fetchSteamCovers(const std::string& coversDir) {
    int fetched = 0;
    int total   = 0;

    for (auto& ge : games_) {
        if (ge.system != SystemType::Steam) continue;
        if (!ge.coverPath.empty() && fs::exists(ge.coverPath)) continue;
        ++total;
    }

    if (total == 0) return;

    std::cout << "[Steam] Fetching covers for " << total << " game(s)...\n";
    std::cout.flush();

    for (auto& ge : games_) {
        if (ge.system != SystemType::Steam) continue;
        if (!ge.coverPath.empty() && fs::exists(ge.coverPath)) continue;

        std::string path = fetchCoverArt(ge, coversDir);
        if (!path.empty()) {
            ge.coverPath = path;
            ++fetched;
        }
    }

    std::cout << "[Steam] Downloaded " << fetched << "/" << total << " cover(s).\n";
}
