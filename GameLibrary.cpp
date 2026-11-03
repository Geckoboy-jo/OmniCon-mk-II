#include "GameLibrary.hpp"
#include "OracleDB.hpp"
#include "DownloadManager.hpp"
#include "ExtraSystems.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
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
    if (has("master_system") || has("mastersystem") || has("sms_roms") || has("/roms_sms"))
        return SystemType::SegaMasterSystem;
    if (has("game_gear") || has("gamegear") || has("/roms_gg") || has("gg_roms"))
        return SystemType::GameGear;
    if (has("saturn") || has("sega_saturn"))
        return SystemType::SegaSaturn;
    if (has("wii_u") || has("wii-u") || has("wiiu"))
        return SystemType::WiiU;
    if (has("nintendo_switch") || has("switch_roms") || has("/roms_switch") ||
        has("_nsz") || has("nsz_") || has("/nsz"))
        return SystemType::NintendoSwitch;
    if (has("playstation_vita") || has("psvita") || has("vita_roms"))
        return SystemType::PSVita;
    if (has("psp_roms") || has("playstation_portable"))
        return SystemType::PSP;
    if (has("/roms_wii") || has("nintendo_wii") || has("wii_roms"))
        return SystemType::Wii;
    if (has("xbox_360") || has("xbox360"))
        return SystemType::Xbox360;
    if (has("xbox_roms") || has("/roms_xbox"))
        return SystemType::Xbox;
    if (has("atari_7800") || has("atari7800") || has("roms_7800"))
        return SystemType::Atari7800;
    if (has("atari_lynx") || has("atarilynx") || has("lynx_roms"))
        return SystemType::AtariLynx;
    if (has("atari_jaguar") || has("atarijaguar") || has("jaguar_roms"))
        return SystemType::AtariJaguar;
    if (has("turbografx") || has("turbo_grafx") || has("pc_engine") || has("pcengine"))
        return SystemType::PCEngine;
    if (has("colecovision") || has("coleco_vision"))
        return SystemType::ColecoVision;
    if (has("intellivision"))
        return SystemType::Intellivision;
    if (has("/roms_gba") || has("gameboy_advance"))
        return SystemType::GBA;
    if (has("atari_2600") || has("roms_2600") || has("atari2600"))
        return SystemType::Atari2600;
    if (has("atari_5200") || has("roms_5200") || has("atari5200"))
        return SystemType::Atari5200;
    if (has("atari_800") || has("roms_800") || has("atari800"))
        return SystemType::Atari800;
    return extraSystemFromUrl(url);
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
    if (e == ".sms")                                           return SystemType::SegaMasterSystem;
    if (e == ".gg")                                            return SystemType::GameGear;
    if (e == ".lnx")                                           return SystemType::AtariLynx;
    if (e == ".a78")                                           return SystemType::Atari7800;
    if (e == ".j64" || e == ".jag")                            return SystemType::AtariJaguar;
    if (e == ".pce")                                           return SystemType::PCEngine;
    if (e == ".col")                                           return SystemType::ColecoVision;
    if (e == ".int")                                           return SystemType::Intellivision;
    if (e == ".cso")                                           return SystemType::PSP;
    if (e == ".vpk")                                           return SystemType::PSVita;
    if (e == ".wbfs" || e == ".wad" || e == ".dol")            return SystemType::Wii;
    if (e == ".wud" || e == ".wux" || e == ".rpx" || e == ".wua")
                                                               return SystemType::WiiU;
    if (e == ".nsp" || e == ".xci" || e == ".nsz" || e == ".xcz")                            return SystemType::NintendoSwitch;
    if (e == ".xbe" || e == ".xiso")                           return SystemType::Xbox;
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
    return extraSystemFromExtension(e);
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
    if (name == "Sega Master System" || name == "Master System" || name == "SMS") return SystemType::SegaMasterSystem;
    if (name == "Game Gear" || name == "Sega Game Gear") return SystemType::GameGear;
    if (name == "Sega Saturn" || name == "Saturn") return SystemType::SegaSaturn;
    if (name == "Nintendo Wii" || name == "Wii") return SystemType::Wii;
    if (name == "Nintendo Wii U" || name == "Wii U") return SystemType::WiiU;
    if (name == "Nintendo Switch" || name == "Switch") return SystemType::NintendoSwitch;
    if (name == "PlayStation Portable" || name == "PSP") return SystemType::PSP;
    if (name == "PlayStation Vita" || name == "PS Vita" || name == "Vita") return SystemType::PSVita;
    if (name == "Xbox") return SystemType::Xbox;
    if (name == "Xbox 360" || name == "Xbox360") return SystemType::Xbox360;
    if (name == "Xbox One" || name == "XboxOne") return SystemType::XboxOne;
    if (name == "Xbox Series X|S" || name == "Xbox Series X" || name == "Xbox Series S" ||
        name == "XboxSeriesX") return SystemType::XboxSeriesX;
    if (name == "Atari 7800") return SystemType::Atari7800;
    if (name == "Atari Lynx" || name == "Lynx") return SystemType::AtariLynx;
    if (name == "Atari Jaguar" || name == "Jaguar") return SystemType::AtariJaguar;
    if (name == "TurboGrafx-16" || name == "PC Engine" || name == "TurboGrafx 16") return SystemType::PCEngine;
    if (name == "ColecoVision") return SystemType::ColecoVision;
    if (name == "Intellivision") return SystemType::Intellivision;
    if(name == "Namco System 246") return SystemType::NamcoSystem246;
    if(name == "Namco System 256") return SystemType::NamcoSystem256;
    if(name == "Sega RingEdge") return SystemType::SegaRingEdge;
    if(name == "Sega RingWide") return SystemType::SegaRingWide;
    if(name == "Sega Lindbergh") return SystemType::SegaLindbergh;
    if(name == "Taito Type X+" || name == "Taito Type X Plus") return SystemType::TaitoTypeXPlus;
    if(name == "Taito Type X3") return SystemType::TaitoTypeX3;
    if(name == "Konami Bemani PC Platform") return SystemType::KonamiBemaniPCPlatform;
    if(name == "Arcade") return SystemType::Arcade;
    if(name == "Microvision") return SystemType::Microvision;
    if(name == "Watara Supervision") return SystemType::WataraSupervision;
    if(name == "Hartung Game Master") return SystemType::HartungGameMaster;
    if(name == "GP32") return SystemType::GP32;
    if(name == "GP2X") return SystemType::GP2X;
    if(name == "Tapwave Zodiac") return SystemType::TapwaveZodiac;
    if(name == "Adventure Vision") return SystemType::AdventureVision;
    if(name == "LaserActive") return SystemType::LaserActive;
    if(name == "Bandai RX-78") return SystemType::BandaiRX78;
    if(name == "Gakken Compact Vision") return SystemType::GakkenCompactVision;
    if(name == "Super A'Can") return SystemType::SuperACan;
    if(name == "FM-7") return SystemType::FMM7;
    if(name == "VM Labs NUON") return SystemType::VMLabsNUON;
    if(name == "Capcom CPS Changer") return SystemType::CapcomCPSChanger;
    if (const ExtraSystemInfo* info = findExtraSystemByName(name)) return info->type;
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
        case SystemType::SegaMasterSystem: return "Sega Master System";
        case SystemType::GameGear:      return "Game Gear";
        case SystemType::SegaSaturn:    return "Sega Saturn";
        case SystemType::Wii:           return "Nintendo Wii";
        case SystemType::WiiU:          return "Nintendo Wii U";
        case SystemType::NintendoSwitch:return "Nintendo Switch";
        case SystemType::PSP:           return "PlayStation Portable";
        case SystemType::PSVita:        return "PlayStation Vita";
        case SystemType::Xbox:          return "Xbox";
        case SystemType::Xbox360:       return "Xbox 360";
        case SystemType::XboxOne:       return "Xbox One";
        case SystemType::XboxSeriesX:   return "Xbox Series X|S";
        case SystemType::Atari7800:     return "Atari 7800";
        case SystemType::AtariLynx:     return "Atari Lynx";
        case SystemType::AtariJaguar:   return "Atari Jaguar";
        case SystemType::PCEngine:      return "TurboGrafx-16";
        case SystemType::ColecoVision:  return "ColecoVision";
        case SystemType::Intellivision: return "Intellivision";
        default:
            if (const ExtraSystemInfo* info = findExtraSystem(sys)) return info->name;
            return "Unknown";
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
        case SystemType::SegaMasterSystem: return "mastersystem";
        case SystemType::GameGear:      return "gamegear";
        case SystemType::SegaSaturn:    return "saturn";
        case SystemType::Wii:           return "wii";
        case SystemType::WiiU:          return "wiiu";
        case SystemType::NintendoSwitch:return "switch";
        case SystemType::PSP:           return "psp";
        case SystemType::PSVita:        return "vita";
        case SystemType::Xbox:          return "xbox";
        case SystemType::Xbox360:       return "xbox360";
        case SystemType::XboxOne:       return "xboxone";
        case SystemType::XboxSeriesX:   return "xboxseriesx";
        case SystemType::Atari7800:     return "atari7800";
        case SystemType::AtariLynx:     return "atarilynx";
        case SystemType::AtariJaguar:   return "atarijaguar";
        case SystemType::PCEngine:      return "pcengine";
        case SystemType::ColecoVision:  return "colecovision";
        case SystemType::Intellivision: return "intellivision";
        default:
            if (const ExtraSystemInfo* info = findExtraSystem(sys)) return info->dir;
            return "unknown";
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
        case SystemType::SegaMasterSystem: return 2;
        case SystemType::GameGear:      return 20;
        case SystemType::SegaSaturn:    return 22;
        case SystemType::Wii:           return 16;
        case SystemType::WiiU:          return 18;
        case SystemType::NintendoSwitch:return 225;
        case SystemType::PSP:           return 61;
        case SystemType::PSVita:        return 62;
        case SystemType::Xbox:          return 32;
        case SystemType::Xbox360:       return 33;
        case SystemType::XboxOne:       return 0;   // cloud library, not scraped
        case SystemType::XboxSeriesX:   return 0;   // cloud library, not scraped
        case SystemType::Atari7800:     return 41;
        case SystemType::AtariLynx:     return 28;
        case SystemType::AtariJaguar:   return 27;
        case SystemType::PCEngine:      return 31;
        case SystemType::ColecoVision:  return 48;
        case SystemType::Intellivision: return 115;
        default:
            if (const ExtraSystemInfo* info = findExtraSystem(sys)) return info->screenscraperId;
            return 0;
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
        if (forceSystem != SystemType::Unknown && findExtraSystem(forceSystem) &&
            !extraSystemAcceptsExtension(forceSystem, ext))
            continue;
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
        {"ps3",       SystemType::PS3},
        {"gamecube",  SystemType::GameCube},
        {"odyssey",   SystemType::Odyssey},
        {"pc",        SystemType::PC},
        {"dreamcast", SystemType::Dreamcast},
        {"mastersystem", SystemType::SegaMasterSystem},
        {"gamegear",  SystemType::GameGear},
        {"saturn",    SystemType::SegaSaturn},
        {"wii",       SystemType::Wii},
        {"wiiu",      SystemType::WiiU},
        {"switch",    SystemType::NintendoSwitch},
        {"psp",       SystemType::PSP},
        {"vita",      SystemType::PSVita},
        {"xbox",      SystemType::Xbox},
        {"xbox360",   SystemType::Xbox360},
        {"atari7800", SystemType::Atari7800},
        {"atarilynx", SystemType::AtariLynx},
        {"atarijaguar", SystemType::AtariJaguar},
        {"pcengine",  SystemType::PCEngine},
        {"colecovision", SystemType::ColecoVision},
        {"intellivision", SystemType::Intellivision},
    };
    for (const auto& sd : subdirs)
        scanDirectory(root / sd.dir, sd.sys);
    for (const auto& info : extraSystems())
        scanDirectory(root / info.dir, info.type);

    // Sort by system then display name
    std::stable_sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b) {
        if (a.system != b.system)
            return static_cast<int>(a.system) < static_cast<int>(b.system);
        return a.displayName < b.displayName;
    });
}

// ─── Non-Steam shortcuts ─────────────────────────────────────────────────────

// A non-Steam game's launch id is (appid << 32) | 0x02000000, which is always
// more than 9 digits; real Steam AppIDs are far smaller.
static bool isNonSteamShortcutId(const std::string& id) { return id.size() > 9; }

namespace {

struct Shortcut {
    std::string name, exe;
    uint32_t    appid = 0;
    bool        hasAppid = false;
};

uint32_t crc32Bytes(const std::string& s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool readCString(const std::string& d, size_t& p, std::string& out) {
    const size_t end = d.find('\0', p);
    if (end == std::string::npos) return false;
    out.assign(d, p, end - p);
    p = end + 1;
    return true;
}

// Reads one binary-VDF object (after its opening key) up to its 0x08 terminator.
// Field values are copied into sc when it is non-null; nested objects are skipped.
bool readVdfObject(const std::string& d, size_t& p, Shortcut* sc, int depth = 0) {
    if (depth > 8) return false;
    while (p < d.size()) {
        const unsigned char type = static_cast<unsigned char>(d[p++]);
        if (type == 0x08) return true;
        std::string key;
        if (!readCString(d, p, key)) return false;
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (type == 0x00) {
            if (!readVdfObject(d, p, nullptr, depth + 1)) return false;
        } else if (type == 0x01) {
            std::string value;
            if (!readCString(d, p, value)) return false;
            if (sc && lower == "appname") sc->name = value;
            else if (sc && lower == "exe") sc->exe = value;
        } else if (type == 0x02) {
            if (p + 4 > d.size()) return false;
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(d[p + i])) << (8 * i);
            p += 4;
            if (sc && lower == "appid") { sc->appid = v; sc->hasAppid = true; }
        } else {
            return false;  // unknown field type; the file is not what we expect
        }
    }
    return false;
}

// Parses userdata/<id>/config/shortcuts.vdf. Returns what could be read.
std::vector<Shortcut> parseShortcuts(const std::string& data) {
    std::vector<Shortcut> out;
    size_t p = 0;
    std::string key;
    if (data.empty() || data[p++] != 0x00 || !readCString(data, p, key)) return out;
    while (p < data.size()) {
        const unsigned char type = static_cast<unsigned char>(data[p++]);
        if (type == 0x08) break;
        if (type != 0x00 || !readCString(data, p, key)) break;
        Shortcut sc;
        if (!readVdfObject(data, p, &sc)) break;
        out.push_back(std::move(sc));
    }
    return out;
}

}  // namespace

// Adds the non-Steam games the user has added to their Steam library.
static void addNonSteamShortcuts(const std::string& steamRoot, std::vector<GameEntry>& games) {
    namespace fs = std::filesystem;
    const fs::path userdata = fs::path(steamRoot) / "userdata";
    std::error_code ec;
    if (!fs::is_directory(userdata, ec)) return;

    std::set<std::string> existing;
    for (const auto& g : games)
        if (g.system == SystemType::Steam) existing.insert(g.romPath);

    int added = 0;
    for (const auto& user : fs::directory_iterator(userdata, ec)) {
        const fs::path config = user.path() / "config";
        std::ifstream in(config / "shortcuts.vdf", std::ios::binary);
        if (!in) continue;
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        for (const Shortcut& sc : parseShortcuts(data)) {
            if (sc.name.empty()) continue;
            const uint32_t appid = sc.hasAppid ? sc.appid
                                               : (crc32Bytes(sc.exe + sc.name) | 0x80000000u);
            const std::string gameId =
                std::to_string((static_cast<uint64_t>(appid) << 32) | 0x02000000ull);
            if (!existing.insert(gameId).second) continue;

            GameEntry ge;
            ge.romPath     = gameId;
            ge.displayName = sc.name;
            ge.system      = SystemType::Steam;

            // Custom artwork set in Steam: portrait first, then the wide capsule.
            const std::string id = std::to_string(appid);
            for (const char* name : { "p.png", "p.jpg", "p.jpeg", ".png", ".jpg", ".jpeg" }) {
                const fs::path art = config / "grid" / (id + name);
                if (fs::exists(art, ec)) { ge.coverPath = art.string(); break; }
            }
            games.push_back(std::move(ge));
            ++added;
        }
    }
    if (added > 0)
        std::cout << "[Steam] Added " << added << " non-Steam game(s) from your library.\n";
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

    addNonSteamShortcuts(steamRoot, games_);

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

        // If already downloaded, point at the local file. An explicit database
        // system label is authoritative for ambiguous formats such as .iso.
        if (!row.localPath.empty() && fs::exists(row.localPath)) {
            ge.romPath = row.localPath;
            SystemType extSys = systemFromExtension(romExtension(row.localPath));
            if (ge.system == SystemType::Unknown && extSys != SystemType::Unknown)
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

        // A previous flat-directory scan cannot infer the platform of an .iso.
        // Merge that local entry with this database record so its explicit
        // system label (for example, Xbox) controls the launcher.
        if (!ge.romPath.empty()) {
            const auto existing = std::find_if(
                games_.begin(), games_.end(), [&](const GameEntry& game) {
                    return game.romPath == ge.romPath;
                });
            if (existing != games_.end()) {
                existing->downloadUrl = ge.downloadUrl;
                existing->dbId = ge.dbId;
                if (ge.system != SystemType::Unknown)
                    existing->system = ge.system;
                ++skipped;
                continue;
            }
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

static std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\"'\"'";
        else quoted += c;
    }
    return quoted + "'";
}

static std::string fetchString(const std::string& url) {
    std::string cmd = "curl --location --fail --silent --show-error " +
                      shellQuote(url);
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

// ─── fetchSteamAchievements (Web API) ────────────────────────────────────────

static std::string expandHome(const std::string& path) {
    if (path.rfind("~/", 0) == 0)
        if (const char* home = getenv("HOME")) return std::string(home) + path.substr(1);
    return path;
}

void GameLibrary::fetchSteamAchievements(const std::string& credFile, const std::string& cacheFile) {
    // scanSteamOwned() saves credentials to the literal path; also accept ~ expanded.
    std::string credPath = credFile;
    if (!fs::exists(credPath)) credPath = expandHome(credFile);
    const std::string apiKey  = readCredFile(credPath, "steam_api_key");
    const std::string steamId = readCredFile(credPath, "steam_id");
    if (apiKey.empty() || steamId.empty()) return;

    struct CacheRow { int percent; long long expires; };
    std::map<std::string, CacheRow> cache;
    {
        std::ifstream in(cacheFile);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string appId; int pct; long long exp;
            if (ls >> appId >> pct >> exp) cache[appId] = {pct, exp};
        }
    }

    const long long now = static_cast<long long>(std::time(nullptr));
    std::vector<GameEntry*> todo;
    for (auto& g : games_) {
        if (g.system != SystemType::Steam || g.romPath.empty() || isNonSteamShortcutId(g.romPath) ||
            !std::all_of(g.romPath.begin(), g.romPath.end(),
                         [](unsigned char c) { return std::isdigit(c) != 0; }))
            continue;
        auto it = cache.find(g.romPath);
        if (it != cache.end() && it->second.expires > now) {
            g.achievementPercent = it->second.percent;
        } else {
            todo.push_back(&g);
        }
    }

    if (!todo.empty()) {
        std::cout << "[Steam] Fetching achievements for " << todo.size() << " game(s)...\n";
        std::cout.flush();

        constexpr long long kOkTtl = 12 * 3600, kNoStatsTtl = 7 * 24 * 3600, kRetryTtl = 3600;
        std::mutex mtx;
        std::atomic<size_t> next{0};
        std::atomic<bool> detailsPrivate{false};

        auto worker = [&]() {
            for (size_t i; !detailsPrivate && (i = next++) < todo.size(); ) {
                const std::string appId = todo[i]->romPath;
                const std::string url =
                    "https://api.steampowered.com/ISteamUserStats/GetPlayerAchievements/v0001/"
                    "?appid=" + appId + "&key=" + apiKey + "&steamid=" + steamId;
                // The last line of curl's output is the HTTP status code.
                const std::string cmd = "curl --location --silent --max-time 20 -w '\\n%{http_code}' " +
                                        shellQuote(url);
                std::string out;
                if (FILE* f = popen(cmd.c_str(), "r")) {
                    char buf[8192];
                    while (fgets(buf, sizeof(buf), f)) out += buf;
                    pclose(f);
                }
                const size_t nl = out.find_last_of('\n');
                const std::string status = nl == std::string::npos ? "" : out.substr(nl + 1);
                const std::string body   = nl == std::string::npos ? out : out.substr(0, nl);

                int percent = -1;
                long long ttl = kRetryTtl;   // network errors / private profiles: retry soon
                static const std::regex okRe(R"x("success"\s*:\s*true)x");
                if (status == "200" && std::regex_search(body, okRe)) {
                    int total = 0, earned = 0;
                    static const std::regex achRe(R"x("achieved"\s*:\s*(\d))x");
                    for (auto it = std::sregex_iterator(body.begin(), body.end(), achRe);
                         it != std::sregex_iterator(); ++it) {
                        ++total;
                        if ((*it)[1].str() == "1") ++earned;
                    }
                    if (total > 0) percent = earned * 100 / total;
                    ttl = total > 0 ? kOkTtl : kNoStatsTtl;
                } else if (body.find("no stats") != std::string::npos) {
                    ttl = kNoStatsTtl;
                } else if (body.find("not public") != std::string::npos) {
                    // Steam hides achievements unless "Game details" are public; every
                    // other request would fail the same way, so stop and don't cache.
                    detailsPrivate = true;
                    continue;
                }

                std::lock_guard<std::mutex> lock(mtx);
                todo[i]->achievementPercent = percent;
                cache[appId] = {percent, now + ttl};
            }
        };

        std::vector<std::thread> pool;
        for (int t = 0; t < 8; ++t) pool.emplace_back(worker);
        for (auto& t : pool) t.join();

        if (detailsPrivate)
            std::cerr << "[Steam] Achievements are hidden: set your Steam profile's \"Game details\" to "
                         "Public (https://steamcommunity.com/my/edit/settings), then restart OmniCon.\n";

        std::error_code ec;
        fs::create_directories(fs::path(cacheFile).parent_path(), ec);
        std::ofstream out(cacheFile, std::ios::trunc);
        for (const auto& [appId, row] : cache)
            out << appId << '\t' << row.percent << '\t' << row.expires << '\n';
    }
}

// ─── scanXboxCloud (Xbox Cloud Gaming) ───────────────────────────────────────

void GameLibrary::scanXboxCloud(const std::string& cacheFile, const std::string& coversDir,
                                bool forceRefresh) {
    const fs::path helper = "scripts/xcloud_library.py";
    if (!fs::exists(helper)) return;

    // The helper prints nothing and exits 2 until the user has run 'xbox-login'.
    const std::string cmd = "python3 " + shellQuote(helper.string()) +
                            " list --cache " + shellQuote(cacheFile) +
                            " --covers-dir " + shellQuote(coversDir) +
                            (forceRefresh ? " --refresh" : "");
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    // A forced refresh replaces the previous Xbox entries instead of adding to them.
    if (forceRefresh)
        games_.erase(std::remove_if(games_.begin(), games_.end(), [](const GameEntry& g) {
            return g.system == SystemType::XboxOne || g.system == SystemType::XboxSeriesX;
        }), games_.end());

    std::set<std::string> existing;
    for (const auto& g : games_)
        if (g.system == SystemType::XboxOne || g.system == SystemType::XboxSeriesX)
            existing.insert(g.romPath);

    // Each line: productId <TAB> XboxOne|XboxSeriesX <TAB> name <TAB> cover path <TAB> cloud|store <TAB> achievement %
    int added = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '\t')) fields.push_back(field);
        if (!line.empty() && line.back() == '\t') fields.emplace_back();
        if (fields.size() < 3) continue;

        // The product id ends up in a launch URL, so only accept plain alphanumerics.
        const std::string& productId = fields[0];
        if (productId.empty() || !std::all_of(productId.begin(), productId.end(),
                                              [](unsigned char c) { return std::isalnum(c) != 0; }))
            continue;
        GameEntry ge;
        // Owned games that can't be streamed carry a ":store" suffix so the
        // launcher opens their Store page instead of cloud play.
        const bool storeOnly = fields.size() > 4 && fields[4] == "store";
        ge.romPath = storeOnly ? productId + ":store" : productId;
        if (existing.count(ge.romPath) || fields[2].empty()) continue;
        ge.displayName = fields[2];
        ge.system      = fields[1] == "XboxSeriesX" ? SystemType::XboxSeriesX : SystemType::XboxOne;
        if (fields.size() > 3 && !fields[3].empty() && fs::exists(fields[3]))
            ge.coverPath = fields[3];
        if (fields.size() > 5 && !fields[5].empty() &&
            std::all_of(fields[5].begin(), fields[5].end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; }))
            ge.achievementPercent = std::min(100, std::atoi(fields[5].c_str()));
        existing.insert(ge.romPath);
        games_.push_back(std::move(ge));
        ++added;
    }
    pclose(pipe);

    if (added > 0) {
        std::stable_sort(games_.begin(), games_.end(), [](const GameEntry& a, const GameEntry& b) {
            if (a.system != b.system)
                return static_cast<int>(a.system) < static_cast<int>(b.system);
            return a.displayName < b.displayName;
        });
        std::cout << "[Xbox Cloud] Loaded " << added << " game(s).\n";
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
        // Non-Steam shortcuts have no store art; only their own Steam artwork applies.
        if (isNonSteamShortcutId(game.romPath)) return "";
        // Use appId (game.romPath) as filename — more stable than display name
        const std::string dest = baseDir + "/" + game.romPath + ".jpg";
        if (fs::exists(dest)) return dest;
        fs::create_directories(baseDir);
        // Try portrait art first (600×900), fall back to landscape header
        for (const char* img : { "library_600x900.jpg", "header.jpg" }) {
            std::string url = "https://cdn.akamai.steamstatic.com/steam/apps/"
                              + game.romPath + "/" + img;
            std::string cmd = "curl --location --fail --silent --show-error --max-time 10 -o " +
                              shellQuote(dest) + " " + shellQuote(url) + " 2>/dev/null";
            if (std::system(cmd.c_str()) == 0 && fs::is_regular_file(dest))
                return dest;
            fs::remove(dest);
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
    std::string apiUrl =
        "https://www.screenscraper.fr/api2/jeuInfos.php"
        "?devid=" + std::string(devid) +
        "&devpassword=" + std::string(devpass) +
        "&softname=omnicon&output=json"
        "&systemeid=" + std::to_string(sysId) +
        "&romnom=" + encoded;
    std::string apiCmd = "curl --location --fail --silent --show-error --max-time 15 " +
                         shellQuote(apiUrl) + " -o " + shellQuote(tmpJson) +
                         " 2>/dev/null";
    if (system(apiCmd.c_str()) != 0 || !fs::is_regular_file(tmpJson)) {
        fs::remove(tmpJson);
        return "";
    }

    // Extract box-2D URL via python3
    std::string pyScript =
        "import json,sys\n"
        "try:\n"
        "    d=json.load(open(sys.argv[1]))\n"
        "    ms=d.get('response',{}).get('jeu',{}).get('medias',[])\n"
        "    u=[m.get('url','') for m in ms if m.get('type')=='box-2D' and m.get('url','')]\n"
        "    print(u[0] if u else '')\n"
        "except: pass\n";
    std::string pyCmd = "python3 -c \"" + pyScript + "\" " +
                        shellQuote(tmpJson) + " 2>/dev/null";

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
    std::string dlCmd = "curl --location --fail --silent --show-error --max-time 30 -o " +
                        shellQuote(destPath) + " " + shellQuote(imageUrl) +
                        " 2>/dev/null";
    int rc = system(dlCmd.c_str());
    if (rc != 0 || !fs::is_regular_file(destPath)) {
        fs::remove(destPath);
        return "";
    }

    return destPath;
}

// ─── fetchSteamCovers ────────────────────────────────────────────────────────

void GameLibrary::fetchSteamCovers(const std::string& coversDir) {
    int fetched = 0;
    int total   = 0;

    for (auto& ge : games_) {
        if (ge.system != SystemType::Steam || isNonSteamShortcutId(ge.romPath)) continue;
        if (!ge.coverPath.empty() && fs::exists(ge.coverPath)) continue;
        ++total;
    }

    if (total == 0) return;

    std::cout << "[Steam] Fetching covers for " << total << " game(s)...\n";
    std::cout.flush();

    for (auto& ge : games_) {
        if (ge.system != SystemType::Steam || isNonSteamShortcutId(ge.romPath)) continue;
        if (!ge.coverPath.empty() && fs::exists(ge.coverPath)) continue;

        std::string path = fetchCoverArt(ge, coversDir);
        if (!path.empty()) {
            ge.coverPath = path;
            ++fetched;
        }
    }

    std::cout << "[Steam] Downloaded " << fetched << "/" << total << " cover(s).\n";
}
