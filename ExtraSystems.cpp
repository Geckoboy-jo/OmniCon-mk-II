#include "ExtraSystems.hpp"
#include "ExtraConsoleLaunchers.hpp"
#include "MameConsoleLauncher.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool listContains(const char* list, const std::string& ext) {
    std::istringstream in(list);
    std::string token;
    while (in >> token)
        if (token == ext) return true;
    return false;
}

const std::vector<ExtraSystemInfo> kExtraSystems = {
    {SystemType::VirtualBoy, "Virtual Boy", "virtualboy", "VB", 11, {190, 30, 40}, ".vb", ".vb"},
    {SystemType::PokemonMini, "Pokemon Mini", "pokemonmini", "PMIN", 211, {60, 130, 200}, ".min", ".min"},
    {SystemType::FamicomDiskSystem, "Famicom Disk System", "fds", "FDS", 106, {200, 80, 60}, ".fds .qd", ".fds .qd"},
    {SystemType::GameAndWatch, "Game & Watch", "gameandwatch", "G&W", 52, {150, 150, 60}, ".mgw", ".mgw"},
    {SystemType::Satellaview, "Satellaview", "satellaview", "BSX", 107, {110, 70, 170}, ".bs", ".bs"},
    {SystemType::AtariXEGS, "Atari XEGS", "atarixegs", "XEGS", 43, {150, 175, 60}, ".xex .atr .car", ".xegs"},
    {SystemType::SegaCD, "Sega CD", "segacd", "SCD", 20, {40, 90, 170}, ".cue .chd .iso", ".scd"},
    {SystemType::Sega32X, "Sega 32X", "sega32x", "32X", 19, {30, 70, 150}, ".32x", ".32x"},
    {SystemType::SegaPico, "Sega Pico", "segapico", "PICO", 250, {60, 160, 190}, ".pco .bin .md", ".pco"},
    {SystemType::SG1000, "Sega SG-1000", "sg1000", "SG1K", 109, {30, 100, 160}, ".sg", ".sg"},
    {SystemType::NeoGeoAES, "Neo Geo AES", "neogeoaes", "AES", 142, {200, 170, 40}, ".neo", ".neo"},
    {SystemType::NeoGeoMVS, "Neo Geo MVS", "neogeomvs", "MVS", 142, {210, 140, 30}, ".neo", ""},
    {SystemType::NeoGeoCD, "Neo Geo CD", "neogeocd", "NGCD", 70, {190, 150, 50}, ".cue .chd", ""},
    {SystemType::NeoGeoPocket, "Neo Geo Pocket", "neogeopocket", "NGP", 25, {180, 160, 70}, ".ngp", ".ngp"},
    {SystemType::NeoGeoPocketColor, "Neo Geo Pocket Color", "neogeopocketcolor", "NGPC", 82, {170, 120, 170}, ".ngc", ".ngc"},
    {SystemType::PCFX, "PC-FX", "pcfx", "PCFX", 72, {150, 60, 110}, ".cue .chd", ""},
    {SystemType::SuperGrafx, "SuperGrafx", "supergrafx", "SGX", 105, {160, 50, 70}, ".sgx", ".sgx"},
    {SystemType::PCEngineCD, "TurboGrafx-CD", "pcenginecd", "PCCD", 114, {180, 60, 80}, ".cue .chd", ""},
    {SystemType::Panasonic3DO, "3DO", "3do", "3DO", 29, {200, 160, 50}, ".iso .cue .chd", ""},
    {SystemType::AmigaCD32, "Amiga CD32", "amigacd32", "CD32", 130, {90, 90, 200}, ".cue .chd .iso", ""},
    {SystemType::WonderSwan, "WonderSwan", "wonderswan", "WS", 45, {70, 120, 170}, ".ws", ".ws"},
    {SystemType::WonderswanColor, "WonderSwan Color", "wonderswancolor", "WSC", 46, {70, 150, 190}, ".wsc", ".wsc"},
    {SystemType::SwanCrystal, "SwanCrystal", "swancrystal", "CRYS", 46, {90, 170, 170}, ".wsc", ""},
    {SystemType::FairchildChannelF, "Fairchild Channel F", "channelf", "CHF", 80, {140, 100, 60}, ".chf .bin", ".chf"},
    {SystemType::Vectrex, "Vectrex", "vectrex", "VECX", 102, {60, 60, 60}, ".vec .bin", ".vec"},
    {SystemType::WataraSupervision, "Watara Supervision", "supervision", "SV", 207, {110, 150, 110}, ".sv", ".sv"},
    {SystemType::Atomiswave, "Atomiswave", "atomiswave", "AW", 53, {200, 100, 40}, ".zip .lst .bin .dat .chd", ""},
    {SystemType::Naomi, "Sega Naomi", "naomi", "NAO", 56, {50, 110, 200}, ".zip .lst .bin .dat .chd", ""},
    {SystemType::Naomi2, "Sega Naomi 2", "naomi2", "NAO2", 56, {40, 90, 180}, ".zip .lst .bin .dat .chd", ""},
    {SystemType::CPS1, "Capcom CPS-1", "cps1", "CPS1", 6, {200, 40, 40}, ".zip", ""},
    {SystemType::CPS2, "Capcom CPS-2", "cps2", "CPS2", 7, {210, 60, 50}, ".zip", ""},
    {SystemType::CPS3, "Capcom CPS-3", "cps3", "CPS3", 8, {220, 80, 60}, ".zip", ""},
    {SystemType::IGS_PGM, "IGS PGM", "igspgm", "PGM", 75, {160, 60, 160}, ".zip", ""},
    {SystemType::SegaSystem16, "Sega System 16", "segasystem16", "S16", 75, {40, 80, 160}, ".zip", ""},
    {SystemType::SegaSystem32, "Sega System 32", "segasystem32", "S32", 75, {50, 100, 170}, ".zip", ""},
    {SystemType::SegaModel3, "Sega Model 3", "segamodel3", "MDL3", 0, {30, 60, 140}, ".zip", ""},
    {SystemType::Arcade, "Arcade", "arcade", "ARC", 75, {120, 120, 130}, ".zip", ""},

    // MAME console machines: content is attached with the machine's media switch.
    {SystemType::Odyssey2, "Magnavox Odyssey 2", "odyssey2", "O2", 104, {90, 150, 110}, ".bin .o2", ".o2", ExtraBackend::MameMachine, "odyssey2", "cart", "OMNICON_ODYSSEY2_MACHINE"},
    {SystemType::CDi, "Philips CD-i", "cdi", "CDI", 133, {60, 140, 170}, ".cue .chd .iso", "", ExtraBackend::MameMachine, "cdimono1", "cdrom", "OMNICON_CDI_MACHINE"},
    {SystemType::CreatiVision, "CreatiVision", "creativision", "CRV", 241, {170, 120, 70}, ".bin .rom", "", ExtraBackend::MameMachine, "crvision", "cart", "OMNICON_CREATIVISION_MACHINE"},
    {SystemType::CasioLoopy, "Casio Loopy", "casioloopy", "LOOP", 98, {200, 90, 130}, ".bin", "", ExtraBackend::MameMachine, "casloopy", "cart", "OMNICON_LOOPY_MACHINE"},
    {SystemType::ApplePippin, "Apple Pippin", "pippin", "PIP", 0, {170, 170, 175}, ".iso .chd .cue", "", ExtraBackend::MameMachine, "pippin", "cdrom", "OMNICON_PIPPIN_MACHINE"},
    {SystemType::EpochSuperCassetteVision, "Super Cassette Vision", "scv", "SCV", 67, {190, 70, 90}, ".bin .0 .scv", "", ExtraBackend::MameMachine, "scv", "cart", "OMNICON_SCV_MACHINE"},
    {SystemType::APFM1000, "APF MP1000/M-1000", "apfm1000", "APF", 0, {130, 110, 90}, ".bin", "", ExtraBackend::MameMachine, "apfm1000", "cart", "OMNICON_APF_MACHINE"},
    {SystemType::EmersonArcadia2001, "Emerson Arcadia 2001", "arcadia2001", "ARCD", 94, {120, 100, 160}, ".bin", "", ExtraBackend::MameMachine, "arcadia", "cart", "OMNICON_ARCADIA_MACHINE"},
    {SystemType::IntertonVC4000, "Interton VC 4000", "vc4000", "VC4K", 0, {110, 130, 150}, ".bin", "", ExtraBackend::MameMachine, "vc4000", "cart", "OMNICON_VC4000_MACHINE"},
    {SystemType::BallyAstrocade, "Bally Astrocade", "astrocade", "ASTR", 44, {150, 90, 60}, ".bin", "", ExtraBackend::MameMachine, "astrocde", "cart", "OMNICON_ASTROCADE_MACHINE"},
    {SystemType::RCAStudioII, "RCA Studio II", "studio2", "ST2", 0, {100, 110, 120}, ".bin .st2", ".st2", ExtraBackend::MameMachine, "studio2", "cart", "OMNICON_STUDIO2_MACHINE"},
    {SystemType::TigerGameCom, "Tiger Game.com", "gamecom", "GCOM", 121, {160, 140, 60}, ".bin .tgc", ".tgc", ExtraBackend::MameMachine, "gamecom", "cart1", "OMNICON_GAMECOM_MACHINE"},
    {SystemType::AdventureVision, "Entex Adventure Vision", "adventurevision", "ADV", 78, {120, 60, 60}, ".bin", "", ExtraBackend::MameMachine, "advision", "cart", "OMNICON_ADVISION_MACHINE"},
    {SystemType::BandaiRX78, "Bandai RX-78", "rx78", "RX78", 0, {90, 120, 170}, ".bin .rom", "", ExtraBackend::MameMachine, "rx78", "cart", "OMNICON_RX78_MACHINE"},
    {SystemType::SuperACan, "Super A'Can", "superacan", "SACN", 100, {180, 110, 50}, ".bin", "", ExtraBackend::MameMachine, "supracan", "cart", "OMNICON_SUPERACAN_MACHINE"},
    {SystemType::FMM7, "Fujitsu FM-7", "fm7", "FM7", 97, {80, 130, 110}, ".d77 .t77 .d88", ".d77 .d88", ExtraBackend::MameMachine, "fm7", "flop1", "OMNICON_FM7_MACHINE"},
    {SystemType::FMTownsMarty, "FM Towns Marty", "fmtmarty", "MRTY", 53, {70, 110, 150}, ".cue .chd .iso", "", ExtraBackend::MameMachine, "fmtmarty", "cdrom", "OMNICON_MARTY_MACHINE"},
    {SystemType::HartungGameMaster, "Hartung Game Master", "gamemaster", "GMST", 0, {140, 150, 90}, ".bin", "", ExtraBackend::MameMachine, "gmaster", "cart", "OMNICON_GAMEMASTER_MACHINE"},
    {SystemType::GP32, "GamePark GP32", "gp32", "GP32", 101, {100, 140, 80}, ".smc .fxe .gxb", ".smc .fxe .gxb", ExtraBackend::MameMachine, "gp32", "memc", "OMNICON_GP32_MACHINE"},

    // MAME arcade boards: content is the romset zip, launched by its basename.
    {SystemType::SegaModel1, "Sega Model 1", "segamodel1", "MDL1", 75, {20, 50, 130}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::SegaModel2, "Sega Model 2", "segamodel2", "MDL2", 75, {25, 55, 135}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::NamcoSystem22, "Namco System 22", "namcosystem22", "N22", 75, {200, 60, 60}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::NamcoSystem23, "Namco System 23", "namcosystem23", "N23", 75, {190, 70, 60}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::NamcoSystem246, "Namco System 246", "namcosystem246", "N246", 75, {180, 80, 60}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::NamcoSystem256, "Namco System 256", "namcosystem256", "N256", 75, {170, 90, 60}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::CaveCV1000, "Cave CV1000", "cavecv1000", "CV1K", 75, {200, 120, 160}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::KonamiGX, "Konami GX", "konamigx", "KGX", 75, {90, 90, 180}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::IGS_PGM2, "IGS PGM2", "igspgm2", "PGM2", 75, {150, 70, 150}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::SegaHikaru, "Sega Hikaru", "segahikaru", "HIK", 75, {60, 100, 190}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::SegaChihiro, "Sega Chihiro", "segachihiro", "CHIH", 75, {70, 130, 80}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::Triforce, "Triforce", "triforce", "TRI", 75, {200, 180, 60}, ".zip", "", ExtraBackend::MameArcade},
    {SystemType::HyperNeoGeo64, "Hyper Neo Geo 64", "hyperneogeo64", "HNG", 75, {190, 160, 60}, ".zip", "", ExtraBackend::MameArcade},
};

}  // namespace

SystemFamily systemFamily(SystemType t) {
    switch (t) {
        case SystemType::NES: case SystemType::SNES: case SystemType::GameBoy:
        case SystemType::GameBoyColor: case SystemType::GBA: case SystemType::GameCube:
        case SystemType::N64: case SystemType::NDS: case SystemType::ThreeDS:
        case SystemType::Wii: case SystemType::WiiU: case SystemType::NintendoSwitch:
        case SystemType::VirtualBoy: case SystemType::PokemonMini:
        case SystemType::FamicomDiskSystem: case SystemType::GameAndWatch:
        case SystemType::Satellaview:
            return SystemFamily::Nintendo;
        case SystemType::SegaGenesis: case SystemType::Dreamcast:
        case SystemType::SegaMasterSystem: case SystemType::GameGear:
        case SystemType::SegaSaturn: case SystemType::SegaCD: case SystemType::Sega32X:
        case SystemType::SegaPico: case SystemType::SG1000:
            return SystemFamily::Sega;
        case SystemType::PS1: case SystemType::PS2: case SystemType::PS3:
        case SystemType::PSP: case SystemType::PSVita:
            return SystemFamily::Sony;
        case SystemType::Xbox: case SystemType::Xbox360: case SystemType::PC:
        case SystemType::Steam:
            return SystemFamily::Microsoft;
        case SystemType::Atari2600: case SystemType::Atari5200: case SystemType::Atari800:
        case SystemType::Atari7800: case SystemType::AtariLynx: case SystemType::AtariJaguar:
        case SystemType::AtariXEGS:
            return SystemFamily::Atari;
        case SystemType::PCEngine: case SystemType::PCEngineCD: case SystemType::SuperGrafx:
        case SystemType::PCFX:
            return SystemFamily::NEC;
        case SystemType::NeoGeoAES: case SystemType::NeoGeoMVS: case SystemType::NeoGeoCD:
        case SystemType::NeoGeoPocket: case SystemType::NeoGeoPocketColor:
            return SystemFamily::SNK;
        case SystemType::Atomiswave: case SystemType::Naomi: case SystemType::Naomi2:
        case SystemType::CPS1: case SystemType::CPS2: case SystemType::CPS3:
        case SystemType::IGS_PGM: case SystemType::IGS_PGM2: case SystemType::SegaSystem16:
        case SystemType::SegaSystem32: case SystemType::SegaModel1:
        case SystemType::SegaModel2: case SystemType::SegaModel3:
        case SystemType::NamcoSystem22: case SystemType::NamcoSystem23:
        case SystemType::NamcoSystem246: case SystemType::NamcoSystem256:
        case SystemType::CaveCV1000: case SystemType::KonamiGX: case SystemType::SegaHikaru:
        case SystemType::SegaChihiro: case SystemType::Triforce:
        case SystemType::HyperNeoGeo64: case SystemType::Arcade:
            return SystemFamily::Arcade;
        default:
            return SystemFamily::Other;
    }
}

const char* systemFamilyName(SystemFamily f) {
    switch (f) {
        case SystemFamily::Nintendo:  return "Nintendo";
        case SystemFamily::Sega:      return "Sega";
        case SystemFamily::Sony:      return "Sony";
        case SystemFamily::Microsoft: return "PC/Xbox";
        case SystemFamily::Atari:     return "Atari";
        case SystemFamily::NEC:       return "NEC";
        case SystemFamily::SNK:       return "SNK";
        case SystemFamily::Arcade:    return "Arcade";
        default:                      return "Other";
    }
}

const std::vector<ExtraSystemInfo>& extraSystems() { return kExtraSystems; }

const ExtraSystemInfo* findExtraSystem(SystemType type) {
    for (const auto& info : kExtraSystems)
        if (info.type == type) return &info;
    return nullptr;
}

const ExtraSystemInfo* findExtraSystemByName(const std::string& name) {
    const std::string wanted = lower(name);
    for (const auto& info : kExtraSystems)
        if (lower(info.name) == wanted || wanted == info.dir || wanted == lower(info.abbrev))
            return &info;
    return nullptr;
}

bool extraSystemKnowsExtension(const std::string& ext) {
    std::string e = lower(ext);
    if (!e.empty() && e[0] != '.') e.insert(e.begin(), '.');
    for (const auto& info : kExtraSystems)
        if (listContains(info.extensions, e)) return true;
    return false;
}

bool extraSystemKeepsArchive(SystemType type) {
    const ExtraSystemInfo* info = findExtraSystem(type);
    return info && listContains(info->extensions, ".zip");
}

SystemType extraSystemFromUrl(const std::string& url) {
    const std::string u = lower(url);
    SystemType best = SystemType::Unknown;
    std::size_t bestLen = 0;
    auto consider = [&](const std::string& token, SystemType type) {
        if (token.size() >= 5 && token.size() > bestLen && u.find(token) != std::string::npos) {
            best = type;
            bestLen = token.size();
        }
    };
    for (const auto& info : kExtraSystems) {
        const std::string name = lower(info.name);
        std::string underscored, hyphenated, joined;
        for (char c : name) {
            if (c == ' ') { underscored += '_'; hyphenated += '-'; continue; }
            if (!std::isalnum(static_cast<unsigned char>(c))) continue;
            underscored += c; hyphenated += c; joined += c;
        }
        consider(info.dir, info.type);
        consider(underscored, info.type);
        consider(hyphenated, info.type);
        consider(joined, info.type);
    }
    return best;
}

SystemType extraSystemFromExtension(const std::string& ext) {
    const std::string e = lower(ext);
    for (const auto& info : kExtraSystems)
        if (listContains(info.detectExtensions, e)) return info.type;
    return SystemType::Unknown;
}

bool extraSystemAcceptsExtension(SystemType type, const std::string& ext) {
    const ExtraSystemInfo* info = findExtraSystem(type);
    return info && listContains(info->extensions, lower(ext));
}

bool launchExtraSystem(SystemType type, const std::string& contentPath) {
    switch (type) {
        case SystemType::VirtualBoy: return VirtualBoyConsoleLauncher().launch(contentPath);
        case SystemType::PokemonMini: return PokemonMiniConsoleLauncher().launch(contentPath);
        case SystemType::FamicomDiskSystem: return FamicomDiskSystemConsoleLauncher().launch(contentPath);
        case SystemType::GameAndWatch: return GameAndWatchConsoleLauncher().launch(contentPath);
        case SystemType::Satellaview: return SatellaviewConsoleLauncher().launch(contentPath);
        case SystemType::AtariXEGS: return AtariXEGSConsoleLauncher().launch(contentPath);
        case SystemType::SegaCD: return SegaCDConsoleLauncher().launch(contentPath);
        case SystemType::Sega32X: return Sega32XConsoleLauncher().launch(contentPath);
        case SystemType::SegaPico: return SegaPicoConsoleLauncher().launch(contentPath);
        case SystemType::SG1000: return SG1000ConsoleLauncher().launch(contentPath);
        case SystemType::NeoGeoAES: return NeoGeoAESConsoleLauncher().launch(contentPath);
        case SystemType::NeoGeoMVS: return NeoGeoMVSConsoleLauncher().launch(contentPath);
        case SystemType::NeoGeoCD: return NeoGeoCDConsoleLauncher().launch(contentPath);
        case SystemType::NeoGeoPocket: return NeoGeoPocketConsoleLauncher().launch(contentPath);
        case SystemType::NeoGeoPocketColor: return NeoGeoPocketColorConsoleLauncher().launch(contentPath);
        case SystemType::PCFX: return PCFXConsoleLauncher().launch(contentPath);
        case SystemType::SuperGrafx: return SuperGrafxConsoleLauncher().launch(contentPath);
        case SystemType::PCEngineCD: return PCEngineCDConsoleLauncher().launch(contentPath);
        case SystemType::Panasonic3DO: return Panasonic3DOConsoleLauncher().launch(contentPath);
        case SystemType::AmigaCD32: return AmigaCD32ConsoleLauncher().launch(contentPath);
        case SystemType::WonderSwan: return WonderSwanConsoleLauncher().launch(contentPath);
        case SystemType::WonderswanColor: return WonderswanColorConsoleLauncher().launch(contentPath);
        case SystemType::SwanCrystal: return SwanCrystalConsoleLauncher().launch(contentPath);
        case SystemType::FairchildChannelF: return FairchildChannelFConsoleLauncher().launch(contentPath);
        case SystemType::Vectrex: return VectrexConsoleLauncher().launch(contentPath);
        case SystemType::WataraSupervision: return WataraSupervisionConsoleLauncher().launch(contentPath);
        case SystemType::Atomiswave: return AtomiswaveConsoleLauncher().launch(contentPath);
        case SystemType::Naomi: return NaomiConsoleLauncher().launch(contentPath);
        case SystemType::Naomi2: return Naomi2ConsoleLauncher().launch(contentPath);
        case SystemType::CPS1: return CPS1ConsoleLauncher().launch(contentPath);
        case SystemType::CPS2: return CPS2ConsoleLauncher().launch(contentPath);
        case SystemType::CPS3: return CPS3ConsoleLauncher().launch(contentPath);
        case SystemType::IGS_PGM: return IGSPGMConsoleLauncher().launch(contentPath);
        case SystemType::SegaSystem16: return SegaSystem16ConsoleLauncher().launch(contentPath);
        case SystemType::SegaSystem32: return SegaSystem32ConsoleLauncher().launch(contentPath);
        case SystemType::SegaModel3: return SegaModel3ConsoleLauncher().launch(contentPath);
        case SystemType::Arcade: return ArcadeConsoleLauncher().launch(contentPath);
        default: {
            const ExtraSystemInfo* info = findExtraSystem(type);
            if (info && info->backend != ExtraBackend::Libretro)
                return MameConsoleLauncher(*info).launch(contentPath);
            return false;
        }
    }
}
