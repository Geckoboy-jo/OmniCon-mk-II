#include "GameShell.hpp"
#include "3DS.hpp"
#include "Atari2600.hpp"
#include "Atari5200.hpp"
#include "Atari800.hpp"
#include "ConsoleLaunchers.hpp"
#include "DownloadManager.hpp"
#include "EpicGames.hpp"
#include "ExtraSystems.hpp"
#include "GBA.hpp"
#include "GameBoy.hpp"
#include "GameBoyColor.hpp"
#include "GameCube.hpp"
#include "Minecraft.hpp"
#include "N64.hpp"
#include "NDS.hpp"
#include "NES.hpp"
#include "Odyssey.hpp"
#include "PS1.hpp"
#include "PS2.hpp"
#include "PS3.hpp"
#include "SNES.hpp"
#include "SaveManager.hpp"
#include "SegaGenesis.hpp"
#include "ShellMessage.hpp"
#include "SteamVR.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Static members
// ───────────────────────────────────────────────────────────

// Grouped by SystemFamily in header tab order (Nintendo, Sega, Sony, Microsoft,
// Atari, NEC, SNK, Arcade, Other) so shoulder-button cycling moves linearly
// across the tabs. kSysAbbrev must stay in the same order.
const SystemType GameShell::kSystems[113] = {
    // Nintendo
    SystemType::NES,
    SystemType::SNES,
    SystemType::GameBoy,
    SystemType::GameBoyColor,
    SystemType::GBA,
    SystemType::NDS,
    SystemType::ThreeDS,
    SystemType::N64,
    SystemType::GameCube,
    SystemType::Wii,
    SystemType::WiiU,
    SystemType::NintendoSwitch,
    SystemType::VirtualBoy,
    SystemType::PokemonMini,
    SystemType::FamicomDiskSystem,
    SystemType::GameAndWatch,
    SystemType::Satellaview,
    // Sega
    SystemType::SegaGenesis,
    SystemType::Dreamcast,
    SystemType::SegaMasterSystem,
    SystemType::GameGear,
    SystemType::SegaSaturn,
    SystemType::SegaCD,
    SystemType::Sega32X,
    SystemType::SegaPico,
    SystemType::SG1000,
    // Sony
    SystemType::PS1,
    SystemType::PS2,
    SystemType::PS3,
    SystemType::PSP,
    SystemType::PSVita,
    // Microsoft
    SystemType::PC,
    SystemType::Steam,
    SystemType::Minecraft,
    SystemType::EpicGames,
    SystemType::Xbox,
    SystemType::Xbox360,
    SystemType::XboxOne,
    SystemType::XboxSeriesX,
    // Atari
    SystemType::Atari2600,
    SystemType::Atari5200,
    SystemType::Atari800,
    SystemType::Atari7800,
    SystemType::AtariLynx,
    SystemType::AtariJaguar,
    SystemType::AtariXEGS,
    // NEC
    SystemType::PCEngine,
    SystemType::PCFX,
    SystemType::SuperGrafx,
    SystemType::PCEngineCD,
    // SNK
    SystemType::NeoGeoAES,
    SystemType::NeoGeoMVS,
    SystemType::NeoGeoCD,
    SystemType::NeoGeoPocket,
    SystemType::NeoGeoPocketColor,
    // Arcade
    SystemType::Atomiswave,
    SystemType::Naomi,
    SystemType::Naomi2,
    SystemType::CPS1,
    SystemType::CPS2,
    SystemType::CPS3,
    SystemType::IGS_PGM,
    SystemType::SegaSystem16,
    SystemType::SegaSystem32,
    SystemType::SegaModel3,
    SystemType::Arcade,
    SystemType::SegaModel1,
    SystemType::SegaModel2,
    SystemType::NamcoSystem22,
    SystemType::NamcoSystem23,
    SystemType::NamcoSystem246,
    SystemType::NamcoSystem256,
    SystemType::CaveCV1000,
    SystemType::KonamiGX,
    SystemType::IGS_PGM2,
    SystemType::SegaHikaru,
    SystemType::SegaChihiro,
    SystemType::Triforce,
    SystemType::HyperNeoGeo64,
    // Other
    SystemType::Odyssey,
    SystemType::ColecoVision,
    SystemType::Intellivision,
    SystemType::Panasonic3DO,
    SystemType::AmigaCD32,
    SystemType::WonderSwan,
    SystemType::WonderswanColor,
    SystemType::SwanCrystal,
    SystemType::FairchildChannelF,
    SystemType::Vectrex,
    SystemType::WataraSupervision,
    SystemType::Odyssey2,
    SystemType::CDi,
    SystemType::CreatiVision,
    SystemType::CasioLoopy,
    SystemType::ApplePippin,
    SystemType::EpochSuperCassetteVision,
    SystemType::APFM1000,
    SystemType::EmersonArcadia2001,
    SystemType::IntertonVC4000,
    SystemType::BallyAstrocade,
    SystemType::RCAStudioII,
    SystemType::TigerGameCom,
    SystemType::AdventureVision,
    SystemType::BandaiRX78,
    SystemType::SuperACan,
    SystemType::FMM7,
    SystemType::FMTownsMarty,
    SystemType::HartungGameMaster,
    SystemType::GP32,
    // PC VR (SteamVR)
    SystemType::OculusRift,
    SystemType::HTCVive,
    SystemType::ValveIndex,
    SystemType::Pico4,
};

static_assert(sizeof(GameShell::kSystems) / sizeof(GameShell::kSystems[0]) ==
                  GameShell::kNumSystems,
              "kSystems must list every filterable system");

const char *const GameShell::kSysAbbrev[113] = {
    // Nintendo
    "NES", "SNES", "GB", "GBC", "GBA", "NDS", "3DS", "N64", "GCN", "WII",
    "WIIU", "SW", "VB", "PMIN", "FDS", "G&W", "BSX",
    // Sega
    "GEN", "DC", "SMS", "GG", "SAT", "SCD", "32X", "PICO", "SG1K",
    // Sony
    "PS1", "PS2", "PS3", "PSP", "VITA",
    // Microsoft
    "PC", "STM", "MC", "EPIC", "XBOX", "360", "XONE", "XSX",
    // Atari
    "2600", "5200", "800", "7800", "LYNX", "JAG", "XEGS",
    // NEC
    "PCE", "PCFX", "SGX", "PCCD",
    // SNK
    "AES", "MVS", "NGCD", "NGP", "NGPC",
    // Arcade
    "AW", "NAO", "NAO2", "CPS1", "CPS2", "CPS3", "PGM", "S16", "S32", "MDL3",
    "ARC", "MDL1", "MDL2", "N22", "N23", "N246", "N256", "CV1K", "KGX", "PGM2",
    "HIK", "CHIH", "TRI", "HNG",
    // Other
    "ODY", "COLECO", "INTV", "3DO", "CD32", "WS", "WSC", "CRYS", "CHF", "VECX",
    "SV", "O2", "CDI", "CRV", "LOOP", "PIP", "SCV", "APF", "ARCD", "VC4K",
    "ASTR", "ST2", "GCOM", "ADV", "RX78", "SACN", "FM7", "MRTY", "GMST",
    "GP32",
    // PC VR (SteamVR)
    "RIFT", "VIVE", "INDX", "PICO"};

static_assert(sizeof(GameShell::kSysAbbrev) /
                      sizeof(GameShell::kSysAbbrev[0]) ==
                  GameShell::kNumSystems,
              "kSysAbbrev must list one abbreviation per kSystems entry");

// ─── Colour palette
// ───────────────────────────────────────────────────────────

static const SDL_Color kColorBg = {10, 10, 22, 255};
static const SDL_Color kColorHeader = {8, 8, 18, 255};
static const SDL_Color kColorInfo = {6, 6, 14, 255};
static const SDL_Color kColorBoxNormal = {18, 18, 38, 255};
static const SDL_Color kColorBoxHover = {28, 28, 55, 255};
static const SDL_Color kColorBoxSel = {12, 38, 80, 255};
static const SDL_Color kColorBorderSel = {60, 150, 255, 255};
static const SDL_Color kColorBorderHov = {45, 55, 100, 255};
static const SDL_Color kColorTextPri = {220, 230, 255, 255};
static const SDL_Color kColorTextDim = {100, 110, 155, 255};
static const SDL_Color kColorFiltAct = {50, 120, 210, 255};
static const SDL_Color kColorFiltInact = {22, 22, 48, 255};

static bool launchExternalChecked(const std::vector<std::string> &args) {
  if (args.empty())
    return false;

  int statusPipe[2] = {};
  if (pipe(statusPipe) != 0) {
    std::cerr << "[GameShell] Could not create launcher status pipe.\n";
    return false;
  }
  fcntl(statusPipe[1], F_SETFD, fcntl(statusPipe[1], F_GETFD) | FD_CLOEXEC);

  pid_t pid = fork();
  if (pid < 0) {
    close(statusPipe[0]);
    close(statusPipe[1]);
    std::cerr << "[GameShell] Could not fork external launcher.\n";
    return false;
  }
  if (pid == 0) {
    close(statusPipe[0]);
    setsid();
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const auto &arg : args)
      argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    const int error = errno;
    (void)!write(statusPipe[1], &error, sizeof(error));
    _exit(127);
  }

  close(statusPipe[1]);
  int error = 0;
  const ssize_t bytesRead = read(statusPipe[0], &error, sizeof(error));
  close(statusPipe[0]);
  if (bytesRead == sizeof(error)) {
    waitpid(pid, nullptr, 0);
    std::cerr << "[External] Could not start " << args.front() << ": "
              << std::strerror(error) << "\n";
    return false;
  }
  setLastLaunchedGamePid(pid);
  return true;
}

static SystemType probeBinConsole(const std::string &path) {
  std::cerr
      << "[BIN] Probing console backends in order (load-only, no saves).\n";

  {
    PS3Emulation candidate;
    if (candidate.loadRom(path))
      return SystemType::PS3;
  }
  {
    PS2Emulation candidate;
    if (candidate.loadRom(path))
      return SystemType::PS2;
  }
  {
    PS1Emulation candidate;
    if (candidate.loadRom(path))
      return SystemType::PS1;
  }
  {
    Atari2600Emulation candidate;
    if (candidate.loadRom(path))
      return SystemType::Atari2600;
  }
  {
    Atari5200Emulation candidate;
    if (candidate.loadRom(path))
      return SystemType::Atari5200;
  }
  {
    Atari800Emulation candidate;
    if (candidate.loadRom(path))
      return SystemType::Atari800;
  }

  {
    SegaGenesisEmulation candidate;
    if (candidate.loadRom(path))
      return SystemType::SegaGenesis;
  }

  std::cerr << "[BIN] No supported console backend accepted the image.\n";
  return SystemType::Unknown;
}

static SDL_Color sysBadgeColor(SystemType sys) {
  switch (sys) {
  case SystemType::NES:
    return {170, 35, 35, 255};
  case SystemType::SNES:
    return {75, 35, 155, 255};
  case SystemType::GameBoy:
    return {35, 130, 35, 255};
  case SystemType::GameBoyColor:
    return {35, 170, 90, 255};
  case SystemType::GBA:
    return {155, 90, 35, 255};
  case SystemType::NDS:
    return {35, 110, 190, 255};
  case SystemType::ThreeDS:
    return {200, 60, 120, 255};
  case SystemType::Atari2600:
    return {190, 140, 35, 255};
  case SystemType::Atari5200:
    return {190, 100, 50, 255};
  case SystemType::Atari800:
    return {140, 170, 50, 255};
  case SystemType::SegaGenesis:
    return {20, 80, 180, 255}; // Sega blue
  case SystemType::N64:
    return {140, 20, 20, 255}; // N64 red
  case SystemType::PS1:
    return {10, 50, 140, 255}; // PS1 navy blue
  case SystemType::PS2:
    return {0, 70, 170, 255}; // PS2 blue
  case SystemType::PS3:
    return {0, 40, 100, 255}; // PS3 dark blue
  case SystemType::GameCube:
    return {88, 76, 168, 255}; // GameCube indigo
  case SystemType::Odyssey:
    return {45, 160, 160, 255}; // teal
  case SystemType::Steam:
    return {23, 134, 192, 255}; // Steam blue
  case SystemType::SegaMasterSystem:
    return {30, 105, 180, 255};
  case SystemType::GameGear:
    return {35, 150, 170, 255};
  case SystemType::SegaSaturn:
    return {130, 130, 135, 255};
  case SystemType::Wii:
    return {50, 155, 190, 255};
  case SystemType::WiiU:
    return {0, 105, 190, 255};
  case SystemType::NintendoSwitch:
    return {210, 40, 55, 255};
  case SystemType::PSP:
    return {25, 85, 175, 255};
  case SystemType::PSVita:
    return {65, 85, 165, 255};
  case SystemType::Xbox:
    return {70, 145, 65, 255};
  case SystemType::Xbox360:
    return {45, 125, 50, 255};
  case SystemType::XboxOne:
    return {16, 124, 16, 255}; // Xbox green
  case SystemType::XboxSeriesX:
    return {14, 100, 14, 255};
  case SystemType::Atari7800:
    return {175, 105, 35, 255};
  case SystemType::AtariLynx:
    return {180, 125, 45, 255};
  case SystemType::AtariJaguar:
    return {150, 145, 45, 255};
  case SystemType::PCEngine:
    return {170, 45, 55, 255};
  case SystemType::ColecoVision:
    return {45, 145, 140, 255};
  case SystemType::Intellivision:
    return {120, 80, 150, 255};
  case SystemType::OculusRift:
    return {30, 30, 35, 255}; // Rift black
  case SystemType::HTCVive:
    return {15, 160, 190, 255}; // Vive teal
  case SystemType::ValveIndex:
    return {120, 60, 180, 255}; // Index purple
  case SystemType::Pico4:
    return {60, 90, 200, 255}; // Pico blue
  case SystemType::Minecraft:
    return {90, 160, 60, 255}; // Minecraft grass green
  case SystemType::EpicGames:
    return {40, 40, 45, 255}; // Epic dark grey
  default:
    if (const ExtraSystemInfo *info = findExtraSystem(sys))
      return {info->color[0], info->color[1], info->color[2], 255};
    return {70, 70, 70, 255};
  }
}

static int sysIndex(SystemType sys) {
  for (int i = 0; i < GameShell::kNumSystems; ++i)
    if (GameShell::kSystems[i] == sys)
      return i;
  return -1;
}

// ─── Font discovery
// ───────────────────────────────────────────────────────────

static const char *findFont() {
  static const char *candidates[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", nullptr};
  for (int i = 0; candidates[i]; ++i)
    if (access(candidates[i], R_OK) == 0)
      return candidates[i];
  return nullptr;
}

// ─── Lifecycle
// ────────────────────────────────────────────────────────────────

GameShell::~GameShell() {
  cancelXboxSignIn();
  cleanup();
}

bool GameShell::init() {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    std::cerr << "[GameShell] SDL_InitSubSystem: " << SDL_GetError() << "\n";
    return false;
  }
  if (TTF_Init() != 0) {
    std::cerr << "[GameShell] TTF_Init: " << TTF_GetError() << "\n";
    return false;
  }
  if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG)) ==
      0) {
    std::cerr << "[GameShell] IMG_Init: " << IMG_GetError() << "\n";
    TTF_Quit();
    return false;
  }

  // Use the actual display bounds rather than SDL_WINDOW_FULLSCREEN_DESKTOP.
  // On WSLg / XWayland (RDP-RAIL mode) the fullscreen state-machine often does
  // not expand the physical window, leaving it at kW×kH while SDL internally
  // records the desktop size.  That mismatch halves every mouse coordinate.
  // Creating a borderless window at the exact display rect is more reliable.
  SDL_Rect dispBounds = {0, 0, kW, kH};
  SDL_GetDisplayBounds(0, &dispBounds);
  if (dispBounds.w <= 0 || dispBounds.h <= 0) {
    dispBounds.w = kW;
    dispBounds.h = kH;
  }
  window_ = SDL_CreateWindow("OmniCon \xe2\x80\x94 Game Library", dispBounds.x,
                             dispBounds.y, dispBounds.w, dispBounds.h,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
  if (!window_) {
    std::cerr << "[GameShell] SDL_CreateWindow: " << SDL_GetError() << "\n";
    IMG_Quit();
    TTF_Quit();
    return false;
  }

  renderer_ = SDL_CreateRenderer(
      window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_)
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer_) {
    std::cerr << "[GameShell] SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    IMG_Quit();
    TTF_Quit();
    return false;
  }

  // Flush any pending OS/compositor events (e.g. Wayland's async fullscreen
  // configure) so SDL_GetRendererOutputSize returns the actual desktop size
  // before we compute the logical scale.
  SDL_PumpEvents();
  // Scale all rendering to fill the screen while preserving the 1280×720 layout
  SDL_RenderSetLogicalSize(renderer_, kW, kH);

  const char *font = findFont();
  if (!font) {
    std::cerr << "[GameShell] No system font found.\n";
    cleanup();
    return false;
  }

  fontLg_ = TTF_OpenFont(font, 26);
  fontMd_ = TTF_OpenFont(font, 14);
  fontSm_ = TTF_OpenFont(font, 12);
  if (!fontLg_ || !fontMd_ || !fontSm_) {
    std::cerr << "[GameShell] TTF_OpenFont: " << TTF_GetError() << "\n";
    cleanup();
    return false;
  }

  // Load any saved SDL controller mapping so it's active before the first frame
  {
    std::ifstream mf("saves/controller_sdlmap.txt");
    std::string line;
    if (std::getline(mf, line) && !line.empty()) {
      if (SDL_GameControllerAddMapping(line.c_str()) >= 0)
        std::cerr << "[GameShell] Controller SDL mapping loaded.\n";
      else
        std::cerr << "[GameShell] SDL mapping load failed: " << SDL_GetError()
                  << "\n";
    }
  }

  return true;
}

void GameShell::cleanup() {
  stopMacroEngine(); // macros stop when the shell session ends
  clearCoverCache();
  if (fontLg_) {
    TTF_CloseFont(fontLg_);
    fontLg_ = nullptr;
  }
  if (fontMd_) {
    TTF_CloseFont(fontMd_);
    fontMd_ = nullptr;
  }
  if (fontSm_) {
    TTF_CloseFont(fontSm_);
    fontSm_ = nullptr;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  IMG_Quit();
  TTF_Quit();
}

void GameShell::clearCoverCache() {
  for (auto &[key, tex] : coverCache_)
    if (tex)
      SDL_DestroyTexture(tex);
  coverCache_.clear();
}

// ─── Texture helpers
// ──────────────────────────────────────────────────────────

SDL_Texture *GameShell::loadTexture(const std::string &path) {
  if (path.empty() || !renderer_)
    return nullptr;
  SDL_Surface *surf = IMG_Load(path.c_str());
  if (!surf)
    return nullptr;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer_, surf);
  SDL_FreeSurface(surf);
  return tex;
}

SDL_Texture *GameShell::getCoverTexture(const GameEntry &game) {
  if (game.coverPath.empty())
    return nullptr;
  auto it = coverCache_.find(game.coverPath);
  if (it != coverCache_.end())
    return it->second;
  SDL_Texture *tex = loadTexture(game.coverPath);
  coverCache_[game.coverPath] = tex;
  return tex;
}

// ─── Text rendering
// ───────────────────────────────────────────────────────────

void GameShell::renderText(const std::string &text, int x, int y, SDL_Color col,
                           TTF_Font *font, int maxW) {
  if (!font || !renderer_ || text.empty())
    return;
  SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text.c_str(), col);
  if (!surf)
    return;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer_, surf);
  if (tex) {
    SDL_Rect dst = {x, y, surf->w, surf->h};
    if (maxW > 0 && dst.w > maxW)
      dst.w = maxW;
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
  }
  SDL_FreeSurface(surf);
}

void GameShell::renderCenteredText(const std::string &text, int x, int y, int w,
                                   SDL_Color col, TTF_Font *font) {
  if (!font || !renderer_ || text.empty())
    return;
  int tw = 0, th = 0;
  TTF_SizeUTF8(font, text.c_str(), &tw, &th);
  (void)th;
  int rx = x + (w - std::min(tw, w)) / 2;
  renderText(text, rx, y, col, font, w);
}

// ─── Filter logic
// ─────────────────────────────────────────────────────────────

// romPath sentinel for the Steam "+" tile, which opens the Steam store instead
// of launching a game (real entries hold a numeric AppID).
static constexpr const char *kSteamStoreId = "STEAMSTORE";

void GameShell::rebuildFilter() {
  filteredGames_.clear();
  const auto &dm = DownloadManager::instance();

  // Pre-compute lowercase search query once
  std::string queryLow = searchQuery_;
  std::transform(queryLow.begin(), queryLow.end(), queryLow.begin(), ::tolower);

  for (auto &ge : library_->games()) {
    bool include = false;
    if (currentFilter_ < 0) {
      include = true;
    } else if (currentFilter_ == kNumSystems) {
      // Downloaded tab: local ROM exists on disk
      if (!ge.romPath.empty() && fs::exists(ge.romPath)) {
        include = true;
      } else if (!ge.downloadUrl.empty() && dm.isDownloaded(ge.downloadUrl)) {
        // Sync romPath so launch skips re-download
        ge.romPath = dm.localPath(ge.downloadUrl);
        // Refine system type from the downloaded file's extension if it
        // wasn't determined at load time (e.g. DB entry with blank systemName).
        if (ge.system == SystemType::Unknown) {
          std::string ext = fs::path(ge.romPath).extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          SystemType extSys = GameLibrary::systemFromExtension(ext);
          if (extSys != SystemType::Unknown)
            ge.system = extSys;
        }
        include = true;
      }
    } else {
      include = (ge.system == kSystems[currentFilter_]);
    }

    // Apply case-insensitive search query
    if (include && !queryLow.empty()) {
      std::string nameLow = ge.displayName;
      std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(),
                     ::tolower);
      include = (nameLow.find(queryLow) != std::string::npos);
    }

    if (include)
      filteredGames_.push_back(&ge);
  }
  // XONE / XSX end with a "+" tile that opens Xbox Cloud Gaming and the Xbox
  // Store to add more games.
  if (xboxCloudFilterActive() && searchQuery_.empty()) {
    xboxAddEntry_.romPath = kAddGamesId;
    xboxAddEntry_.displayName = "Add more games";
    xboxAddEntry_.system = kSystems[currentFilter_];
    filteredGames_.push_back(&xboxAddEntry_);
  }
  // Steam ends with a "+" tile that opens the Steam store to add more games.
  if (currentFilter_ >= 0 && currentFilter_ < kNumSystems &&
      kSystems[currentFilter_] == SystemType::Steam && searchQuery_.empty()) {
    steamAddEntry_.romPath = kSteamStoreId;
    steamAddEntry_.displayName = "Add more games";
    steamAddEntry_.system = SystemType::Steam;
    filteredGames_.push_back(&steamAddEntry_);
  }
  // Minecraft and Epic Games have no scannable library; show a single tile
  // that opens the respective launcher.
  if (currentFilter_ >= 0 && currentFilter_ < kNumSystems &&
      searchQuery_.empty()) {
    const SystemType sys = kSystems[currentFilter_];
    if (sys == SystemType::Minecraft) {
      minecraftEntry_.romPath = "";
      minecraftEntry_.displayName = "Minecraft Launcher";
      minecraftEntry_.system = SystemType::Minecraft;
      filteredGames_.push_back(&minecraftEntry_);
    } else if (sys == SystemType::EpicGames) {
      epicEntry_.romPath = "";
      epicEntry_.displayName = "Epic Games Launcher";
      epicEntry_.system = SystemType::EpicGames;
      filteredGames_.push_back(&epicEntry_);
    }
  }
  selectedIdx_ = 0;
  scrollRow_ = 0;
  hoverIdx_ = -1;
  clearCoverCache();
  std::cout << "[Shell] rebuildFilter: " << filteredGames_.size()
            << " games visible (filter=" << currentFilter_
            << ", total in library=" << library_->games().size() << ")\n";
}

void GameShell::ensureVisible() {
  if (filteredGames_.empty())
    return;
  int row = selectedIdx_ / kCols;
  if (row < scrollRow_)
    scrollRow_ = row;
  else if (row >= scrollRow_ + 2)
    scrollRow_ = row - 1;
  if (scrollRow_ < 0)
    scrollRow_ = 0;
}

// ─── Filter button layout
// ─────────────────────────────────────────────────────

void GameShell::computeFilterRects() {
  // One row: All, one button per family, Downloaded.
  const int count = kNumSystemFamilies + 2;
  const int btnH = 32;
  const int marginX = 8;
  const int gapX = 6;
  const int btnW = (kW - 2 * marginX - (count - 1) * gapX) / count;
  const int y = (kHeaderH - btnH) / 2;

  for (int i = 0; i < count; ++i)
    filterBtnRects_[i] = {marginX + i * (btnW + gapX), y, btnW, btnH};
  layoutDropdown();
}

void GameShell::layoutDropdown() {
  dropdownItems_.clear();
  dropdownPanel_ = {0, 0, 0, 0};
  if (openFamily_ < 0)
    return;

  constexpr int itemW = 230, itemH = 26, rowsPerCol = 12, pad = 6;
  std::vector<int> members;
  for (int i = 0; i < kNumSystems; ++i)
    if (static_cast<int>(systemFamily(kSystems[i])) == openFamily_)
      members.push_back(i);
  if (members.empty())
    return;

  const int rows = std::min(rowsPerCol, static_cast<int>(members.size()));
  const int cols =
      (static_cast<int>(members.size()) + rowsPerCol - 1) / rowsPerCol;
  const int panelW = cols * itemW + 2 * pad;
  const int panelH = rows * itemH + 2 * pad;
  const int anchorX = filterBtnRects_[1 + openFamily_].x;
  const int panelX = std::max(0, std::min(anchorX, kW - panelW));
  dropdownPanel_ = {panelX, kHeaderH, panelW, panelH};

  for (std::size_t k = 0; k < members.size(); ++k) {
    const int col = static_cast<int>(k) / rowsPerCol;
    const int row = static_cast<int>(k) % rowsPerCol;
    dropdownItems_.push_back({{panelX + pad + col * itemW,
                               kHeaderH + pad + row * itemH, itemW, itemH},
                              members[k]});
  }
}

// ─── Hit testing
// ──────────────────────────────────────────────────────────────

int GameShell::gameAtMouse(int mx, int my) const {
  if (my < kHeaderH || my >= kH - kInfoH)
    return -1;
  int col = (mx - kGridLeft) / kSlotW;
  if (col < 0 || col >= kCols)
    return -1;
  int visRow = (my - kHeaderH) / kSlotH;
  int idx = (visRow + scrollRow_) * kCols + col;
  if (idx < 0 || idx >= static_cast<int>(filteredGames_.size()))
    return -1;
  return idx;
}

// Returns -2 for no hit, -1 for All, kNumSystems for Downloaded, and
// kNumSystems + 1 + family for a family button.
int GameShell::filterBtnAtMouse(int mx, int my) const {
  for (int i = 0; i < kNumSystemFamilies + 2; ++i) {
    const SDL_Rect &r = filterBtnRects_[i];
    if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
      if (i == 0)
        return -1;
      if (i == kNumSystemFamilies + 1)
        return kNumSystems;
      return kNumSystems + i;
    }
  }
  return -2;
}

int GameShell::dropdownItemAtMouse(int mx, int my) const {
  for (const auto &item : dropdownItems_) {
    const SDL_Rect &r = item.first;
    if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
      return item.second;
  }
  return -1;
}

// ─── Rendering
// ────────────────────────────────────────────────────────────────

void GameShell::renderHeader() {
  SDL_Rect hdr = {0, 0, kW, kHeaderH};
  SDL_SetRenderDrawColor(renderer_, kColorHeader.r, kColorHeader.g,
                         kColorHeader.b, 255);
  SDL_RenderFillRect(renderer_, &hdr);

  // Bottom separator
  SDL_SetRenderDrawColor(renderer_, 30, 30, 65, 255);
  SDL_RenderDrawLine(renderer_, 0, kHeaderH - 1, kW - 1, kHeaderH - 1);

  const int selectedFamily =
      (currentFilter_ >= 0 && currentFilter_ < kNumSystems)
          ? static_cast<int>(systemFamily(kSystems[currentFilter_]))
          : -1;

  for (int i = 0; i < kNumSystemFamilies + 2; ++i) {
    const bool isAll = (i == 0);
    const bool isDownloaded = (i == kNumSystemFamilies + 1);
    const int family = i - 1;
    const bool active = isAll          ? currentFilter_ == -1
                        : isDownloaded ? currentFilter_ == kNumSystems
                                       : family == selectedFamily;
    const bool open = !isAll && !isDownloaded && family == openFamily_;
    const SDL_Rect &r = filterBtnRects_[i];

    SDL_Color bg = isDownloaded && active ? SDL_Color{25, 110, 65, 255}
                   : active               ? kColorFiltAct
                   : open                 ? kColorBoxHover
                                          : kColorFiltInact;
    SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
    SDL_RenderFillRect(renderer_, &r);

    if (active || open) {
      SDL_Color ul = isDownloaded ? SDL_Color{70, 200, 120, 255}
                                  : SDL_Color{100, 180, 255, 255};
      SDL_SetRenderDrawColor(renderer_, ul.r, ul.g, ul.b, 255);
      SDL_Rect ulr = {r.x, r.y + r.h - 2, r.w, 2};
      SDL_RenderFillRect(renderer_, &ulr);
    }

    std::string label;
    if (isAll)
      label = "All";
    else if (isDownloaded)
      label = "Downloaded";
    else {
      label = systemFamilyName(static_cast<SystemFamily>(family));
      if (active)
        label += ": " + std::string(kSysAbbrev[currentFilter_]);
      label += open ? " \xe2\x96\xb4" : " \xe2\x96\xbe";
    }
    SDL_Color tc =
        (active || open) ? SDL_Color{230, 240, 255, 255} : kColorTextDim;
    renderCenteredText(label, r.x, r.y + (r.h - 14) / 2, r.w, tc, fontMd_);
  }
}

void GameShell::renderDropdown() {
  if (openFamily_ < 0 || dropdownItems_.empty())
    return;

  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer_, 12, 12, 28, 245);
  SDL_RenderFillRect(renderer_, &dropdownPanel_);
  SDL_SetRenderDrawColor(renderer_, 80, 150, 255, 255);
  SDL_RenderDrawRect(renderer_, &dropdownPanel_);

  for (const auto &item : dropdownItems_) {
    const SDL_Rect &r = item.first;
    const SystemType sys = kSystems[item.second];
    const bool selected = item.second == currentFilter_;
    const bool hovered = item.second == hoverDropdown_;
    if (selected || hovered) {
      SDL_Color bg = selected ? kColorBoxSel : kColorBoxHover;
      SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
      SDL_RenderFillRect(renderer_, &r);
    }
    const SDL_Color badge = sysBadgeColor(sys);
    SDL_SetRenderDrawColor(renderer_, badge.r, badge.g, badge.b, 255);
    SDL_Rect swatch = {r.x + 6, r.y + 7, 12, 12};
    SDL_RenderFillRect(renderer_, &swatch);
    renderText(GameLibrary::systemName(sys), r.x + 26, r.y + 6,
               selected ? kColorTextPri : kColorTextDim, fontMd_, r.w - 32);
  }
}

void GameShell::renderGrid() {
  SDL_Rect gridArea = {0, kHeaderH, kW, kGridH};
  SDL_SetRenderDrawColor(renderer_, kColorBg.r, kColorBg.g, kColorBg.b, 255);
  SDL_RenderFillRect(renderer_, &gridArea);

  if (filteredGames_.empty() || xboxPromptActive()) {
    if (xboxPromptActive()) {
      renderXboxPanel();
      return;
    }
    renderCenteredText("No games found", 0, kHeaderH + kGridH / 2 - 10, kW,
                       kColorTextDim, fontMd_);
    return;
  }

  SDL_RenderSetClipRect(renderer_, &gridArea);

  constexpr int padTop = 9;
  constexpr int gap1 = 5; // cover → name gap
  constexpr int nameH = 16;
  constexpr int gap2 = 3; // name → badge gap
  constexpr int badgeH = 13;

  int n = static_cast<int>(filteredGames_.size());
  int totalRows = (n + kCols - 1) / kCols;
  int visRows = kGridH / kSlotH + 2; // +2 so partial bottom row is rendered

  for (int row = scrollRow_; row < std::min(totalRows, scrollRow_ + visRows);
       ++row) {
    int sy = kHeaderH + (row - scrollRow_) * kSlotH;
    for (int col = 0; col < kCols; ++col) {
      int idx = row * kCols + col;
      if (idx >= n)
        break;

      const GameEntry &ge = *filteredGames_[idx];
      int sx = kGridLeft + col * kSlotW;

      bool selected = (idx == selectedIdx_);
      bool hovered = (idx == hoverIdx_);

      // Slot background
      SDL_Color bg = selected  ? kColorBoxSel
                     : hovered ? kColorBoxHover
                               : kColorBoxNormal;
      SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
      SDL_Rect slotR = {sx, sy, kSlotW, kSlotH};
      SDL_RenderFillRect(renderer_, &slotR);

      // Cover image
      int coverX = sx + (kSlotW - kCoverW) / 2;
      int coverY = sy + padTop;
      SDL_Rect coverR = {coverX, coverY, kCoverW, kCoverH};

      SDL_Texture *tex = getCoverTexture(ge);
      if (tex) {
        SDL_RenderCopy(renderer_, tex, nullptr, &coverR);
      } else if (&ge == &xboxAddEntry_ || &ge == &steamAddEntry_) {
        // "+" tile (Xbox green / Steam blue)
        const bool steam = (&ge == &steamAddEntry_);
        const SDL_Color tileBg =
            steam ? SDL_Color{18, 32, 54, 255} : SDL_Color{20, 48, 24, 255};
        const SDL_Color tilePlus =
            steam ? SDL_Color{90, 160, 220, 255} : SDL_Color{80, 190, 90, 255};
        SDL_SetRenderDrawColor(renderer_, tileBg.r, tileBg.g, tileBg.b, 255);
        SDL_RenderFillRect(renderer_, &coverR);
        SDL_SetRenderDrawColor(renderer_, tilePlus.r, tilePlus.g, tilePlus.b,
                               255);
        const int cx = coverX + kCoverW / 2, cy = coverY + kCoverH / 2;
        SDL_Rect hBar = {cx - 28, cy - 4, 56, 8};
        SDL_Rect vBar = {cx - 4, cy - 28, 8, 56};
        SDL_RenderFillRect(renderer_, &hBar);
        SDL_RenderFillRect(renderer_, &vBar);
      } else {
        // Coloured placeholder
        SDL_Color pc = sysBadgeColor(ge.system);
        SDL_SetRenderDrawColor(renderer_, pc.r / 2, pc.g / 2, pc.b / 2, 255);
        SDL_RenderFillRect(renderer_, &coverR);
        int si = sysIndex(ge.system);
        if (si >= 0)
          renderCenteredText(kSysAbbrev[si], coverX, coverY + kCoverH / 2 - 7,
                             kCoverW, {200, 210, 240, 255}, fontMd_);
      }

      // Selection / hover border
      if (selected) {
        SDL_SetRenderDrawColor(renderer_, kColorBorderSel.r, kColorBorderSel.g,
                               kColorBorderSel.b, 255);
        SDL_Rect b1 = {coverX - 2, coverY - 2, kCoverW + 4, kCoverH + 4};
        SDL_Rect b2 = {coverX - 3, coverY - 3, kCoverW + 6, kCoverH + 6};
        SDL_RenderDrawRect(renderer_, &b1);
        SDL_RenderDrawRect(renderer_, &b2);
      } else if (hovered) {
        SDL_SetRenderDrawColor(renderer_, kColorBorderHov.r, kColorBorderHov.g,
                               kColorBorderHov.b, 255);
        SDL_Rect bh = {coverX - 1, coverY - 1, kCoverW + 2, kCoverH + 2};
        SDL_RenderDrawRect(renderer_, &bh);
      }

      // System badge — small coloured pill at bottom-left of slot
      int si = sysIndex(ge.system);
      if (si >= 0) {
        int badgeX = coverX;
        int badgeY = coverY + kCoverH + gap1 + nameH + gap2;
        int tw = 0, th = 0;
        TTF_SizeUTF8(fontMd_, kSysAbbrev[si], &tw, &th);
        (void)th;
        int bw = tw + 8;
        SDL_Color bc = sysBadgeColor(ge.system);
        SDL_SetRenderDrawColor(renderer_, bc.r / 2, bc.g / 2, bc.b / 2, 200);
        SDL_Rect badgeRect = {badgeX, badgeY, bw, badgeH};
        SDL_RenderFillRect(renderer_, &badgeRect);
        renderText(kSysAbbrev[si], badgeX + 4, badgeY + 1, {220, 225, 240, 255},
                   fontMd_);
      }

      // Achievement progress bar (Steam / Xbox) under the badge row
      if (ge.achievementPercent >= 0) {
        const int pct = std::min(100, ge.achievementPercent);
        const int barY = sy + kSlotH - 8;
        SDL_Rect track = {coverX, barY, kCoverW, 5};
        SDL_SetRenderDrawColor(renderer_, 40, 44, 60, 255);
        SDL_RenderFillRect(renderer_, &track);
        const SDL_Color fill = pct >= 100 ? SDL_Color{240, 190, 60, 255}
                                          : SDL_Color{80, 190, 90, 255};
        SDL_Rect filled = {coverX, barY, kCoverW * pct / 100, 5};
        SDL_SetRenderDrawColor(renderer_, fill.r, fill.g, fill.b, 255);
        SDL_RenderFillRect(renderer_, &filled);

        // Percentage label, right-aligned on the badge row
        const std::string label = std::to_string(pct) + "%";
        int lw = 0, lh = 0;
        TTF_SizeUTF8(fontMd_, label.c_str(), &lw, &lh);
        (void)lh;
        renderText(label, coverX + kCoverW - lw,
                   coverY + kCoverH + gap1 + nameH + gap2 + 1, fill, fontMd_);
      }

      // Game name (centred, clipped to slot width)
      int nameY = coverY + kCoverH + gap1;
      SDL_Color nc = selected ? kColorTextPri : kColorTextDim;
      renderCenteredText(ge.displayName, sx, nameY, kSlotW, nc, fontSm_);
    }
  }

  SDL_RenderSetClipRect(renderer_, nullptr);
}

void GameShell::renderInfoBar() {
  int barY = kH - kInfoH;
  SDL_Rect bar = {0, barY, kW, kInfoH};
  SDL_SetRenderDrawColor(renderer_, kColorInfo.r, kColorInfo.g, kColorInfo.b,
                         255);
  SDL_RenderFillRect(renderer_, &bar);

  SDL_SetRenderDrawColor(renderer_, 30, 30, 65, 255);
  SDL_RenderDrawLine(renderer_, 0, barY, kW - 1, barY);

  if (!filteredGames_.empty() &&
      selectedIdx_ < static_cast<int>(filteredGames_.size())) {
    const GameEntry &ge = *filteredGames_[selectedIdx_];
    renderText(ge.displayName, 20, barY + 7, kColorTextPri, fontLg_,
               kW / 2 - 30);
    std::string sysName = "\xe2\x80\xa2 " + GameLibrary::systemName(ge.system);
    renderText(sysName, kW / 2, barY + 11, kColorTextDim, fontMd_);
  }

  renderText("\xe2\x86\x90\xe2\x86\x92\xe2\x86\x91\xe2\x86\x93 Navigate    "
             "Enter / Double-click = Play    Mouse Wheel = Scroll    "
             "Ctrl+F = Search    F2 = Macros    Esc = Exit",
             20, barY + kInfoH - 16, kColorTextDim, fontSm_);

  std::string toast;
  if (currentShellMessage(toast)) {
    SDL_Rect tr = {kW / 2 - 400, barY - 34, 800, 28};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 150, 60, 20, 235);
    SDL_RenderFillRect(renderer_, &tr);
    renderText(toast, tr.x + 10, tr.y + 6, {255, 235, 200, 255}, fontMd_,
               tr.w - 20);
  }

  // Controller indicator (right-aligned)
  if (!ctrlName_.empty()) {
    std::string ctrlLabel = "\xf0\x9f\x8e\xae  " + ctrlName_;
    renderText(ctrlLabel, kW - 360, barY + 11, {80, 210, 120, 255}, fontMd_,
               340);
  }
}

void GameShell::render() {
  // Re-apply logical size every frame as the definitive safety net for the
  // async Wayland fullscreen race: by the time the first frame actually draws,
  // the compositor will have delivered the fullscreen size.
  SDL_RenderSetLogicalSize(renderer_, kW, kH);

  SDL_SetRenderDrawColor(renderer_, kColorBg.r, kColorBg.g, kColorBg.b, 255);
  SDL_RenderClear(renderer_);
  renderHeader();
  renderGrid();
  renderInfoBar();
  renderDropdown();
  if (searchActive_)
    renderSearchBox();
  SDL_RenderPresent(renderer_);
}

void GameShell::renderSearchBox() {
  constexpr int boxW = 440, boxH = 34;
  const int boxX = (kW - boxW) / 2;
  const int boxY = kHeaderH + 8;

  // Semi-transparent backdrop
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
  SDL_Rect backdrop = {boxX - 4, boxY - 4, boxW + 8, boxH + 8};
  SDL_RenderFillRect(renderer_, &backdrop);

  // Box background
  SDL_SetRenderDrawColor(renderer_, 14, 14, 30, 240);
  SDL_Rect bg = {boxX, boxY, boxW, boxH};
  SDL_RenderFillRect(renderer_, &bg);

  // Border
  SDL_SetRenderDrawColor(renderer_, 80, 150, 255, 255);
  SDL_RenderDrawRect(renderer_, &bg);

  // "Search: " label + query + blinking cursor
  bool showCursor = (SDL_GetTicks() % 800) < 420;
  std::string display = "Search: " + searchQuery_;
  if (showCursor)
    display += "|";
  renderText(display, boxX + 10, boxY + (boxH - 14) / 2, {210, 225, 255, 255},
             fontMd_, boxW - 20);

  // Result count (right-aligned inside box)
  std::string count = std::to_string(filteredGames_.size()) + " result" +
                      (filteredGames_.size() == 1 ? "" : "s");
  int tw = 0, th = 0;
  TTF_SizeUTF8(fontMd_, count.c_str(), &tw, &th);
  (void)th;
  renderText(count, boxX + boxW - tw - 10, boxY + (boxH - 14) / 2,
             kColorTextDim, fontMd_);
}

// ─── Xbox Cloud Gaming sign-in
// ────────────────────────────────────────────────

bool GameShell::xboxSignedIn() {
  const char *custom = std::getenv("OMNICON_XBOX_TOKENS");
  if (custom && *custom)
    return fs::exists(custom);
  const char *home = std::getenv("HOME");
  return home && *home &&
         fs::exists(fs::path(home) / ".omnicon" / "xbox_cloud_tokens.json");
}

bool GameShell::xboxCloudFilterActive() const {
  if (currentFilter_ < 0 || currentFilter_ >= kNumSystems)
    return false;
  const SystemType sys = kSystems[currentFilter_];
  return sys == SystemType::XboxOne || sys == SystemType::XboxSeriesX;
}

bool GameShell::xboxPromptActive() const {
  if (!xboxCloudFilterActive() || !searchQuery_.empty())
    return false;
  // Nothing to show except (at most) the "+" tile.
  return filteredGames_.empty() ||
         (filteredGames_.size() == 1 && filteredGames_[0] == &xboxAddEntry_);
}

bool GameShell::xboxSignInWaiting() {
  std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
  return xboxSignIn_.state == XboxSignIn::State::Waiting;
}

void GameShell::cancelXboxSignIn() {
  {
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    if (xboxSignIn_.state == XboxSignIn::State::Waiting)
      xboxSignIn_.state = XboxSignIn::State::Idle;
    if (xboxSignIn_.pid > 0)
      kill(xboxSignIn_.pid, SIGTERM);
  }
  // Joined outside the lock: the worker takes it when its pipe closes.
  if (xboxSignIn_.worker.joinable())
    xboxSignIn_.worker.join();
}

void GameShell::startXboxSignIn() {
  cancelXboxSignIn();

  auto fail = [&](const std::string &message) {
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    xboxSignIn_.state = XboxSignIn::State::Failed;
    xboxSignIn_.message = message;
  };

  int fds[2] = {};
  if (pipe(fds) != 0) {
    fail("Could not start the sign-in helper.");
    return;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    fail("Could not start the sign-in helper.");
    return;
  }
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    close(fds[1]);
    execlp("python3", "python3", "scripts/xcloud_library.py", "login",
           "--machine", static_cast<char *>(nullptr));
    _exit(127);
  }
  close(fds[1]);

  {
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    xboxSignIn_.state = XboxSignIn::State::Waiting;
    xboxSignIn_.code.clear();
    xboxSignIn_.url.clear();
    xboxSignIn_.message.clear();
    xboxSignIn_.pid = pid;
  }

  const int readFd = fds[0];
  xboxSignIn_.worker = std::thread([this, readFd, pid]() {
    using State = XboxSignIn::State;
    FILE *in = fdopen(readFd, "r");
    char buf[512];
    while (in && fgets(buf, sizeof(buf), in)) {
      std::string line(buf);
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
      std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
      if (xboxSignIn_.state != State::Waiting)
        continue;
      if (line.rfind("CODE ", 0) == 0) {
        std::istringstream fields(line.substr(5));
        fields >> xboxSignIn_.code >> xboxSignIn_.url;
      } else if (line == "OK") {
        xboxSignIn_.state = State::Done;
      } else if (line.rfind("FAIL", 0) == 0) {
        xboxSignIn_.state = State::Failed;
        xboxSignIn_.message =
            line.size() > 5 ? line.substr(5) : "Sign-in failed.";
      }
    }
    if (in)
      fclose(in);
    else
      close(readFd);
    waitpid(pid, nullptr, 0);
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    xboxSignIn_.pid = -1;
    if (xboxSignIn_.state == State::Waiting) {
      xboxSignIn_.state = State::Failed;
      xboxSignIn_.message =
          "The sign-in helper stopped unexpectedly (is python3 installed?).";
    }
  });
}

void GameShell::reloadXboxLibrary() {
  if (!library_)
    return;
  // The fetch blocks for a few seconds, so draw the loading message first.
  xboxLoading_ = true;
  render();
  library_->scanXboxCloud("saves/xbox_cloud_library.json", coversDir_, true);
  xboxLoading_ = false;

  const std::size_t count =
      library_->gamesForSystem(SystemType::XboxOne).size() +
      library_->gamesForSystem(SystemType::XboxSeriesX).size();
  rebuildFilter();
  computeFilterRects();
  postShellMessage(
      count > 0 ? "Xbox Cloud: " + std::to_string(count) + " game(s) loaded."
                : "Xbox Cloud: no playable games found for this account.");
}

void GameShell::pollXboxSignIn() {
  bool done = false;
  {
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    if (xboxSignIn_.state == XboxSignIn::State::Done) {
      xboxSignIn_.state = XboxSignIn::State::Idle;
      done = true;
    }
  }
  if (!done)
    return;
  if (xboxSignIn_.worker.joinable())
    xboxSignIn_.worker.join();
  reloadXboxLibrary();
}

void GameShell::xboxPromptAction() {
  if (xboxLoading_ || xboxSignInWaiting())
    return;
  {
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    if (xboxSignIn_.state == XboxSignIn::State::Failed)
      xboxSignIn_.state = XboxSignIn::State::Idle;
  }
  if (xboxSignedIn())
    reloadXboxLibrary();
  else
    startXboxSignIn();
}

void GameShell::renderXboxPanel() {
  const SDL_Color accent = {80, 190, 90, 255};
  const SDL_Color warn = {255, 170, 110, 255};
  const int cy = kHeaderH + kGridH / 2;

  XboxSignIn::State state;
  std::string code, url, message;
  {
    std::lock_guard<std::mutex> lock(xboxSignIn_.mutex);
    state = xboxSignIn_.state;
    code = xboxSignIn_.code;
    url = xboxSignIn_.url;
    message = xboxSignIn_.message;
  }

  const bool onePlatform = kSystems[currentFilter_] == SystemType::XboxOne;
  renderCenteredText(onePlatform
                         ? "Xbox One \xe2\x80\x94 Xbox Cloud Gaming"
                         : "Xbox Series X|S \xe2\x80\x94 Xbox Cloud Gaming",
                     0, cy - 110, kW, accent, fontLg_);

  if (xboxLoading_) {
    renderCenteredText("Loading your Xbox Cloud Gaming library...", 0, cy - 30,
                       kW, kColorTextPri, fontMd_);
  } else if (state == XboxSignIn::State::Waiting) {
    if (code.empty()) {
      renderCenteredText("Contacting Microsoft...", 0, cy - 30, kW,
                         kColorTextPri, fontMd_);
    } else {
      renderCenteredText(
          "On any phone or PC, open " + url + " and enter this code:", 0,
          cy - 60, kW, kColorTextPri, fontMd_);
      renderCenteredText(code, 0, cy - 25, kW, {255, 255, 255, 255}, fontLg_);
      renderCenteredText(
          "Waiting for you to finish signing in...    Esc / B = Cancel", 0,
          cy + 25, kW, kColorTextDim, fontMd_);
    }
  } else if (state == XboxSignIn::State::Failed) {
    renderCenteredText(message.empty() ? "Sign-in failed." : message, 0,
                       cy - 40, kW, warn, fontMd_);
    renderCenteredText("Enter / A = Try again", 0, cy, kW, kColorTextDim,
                       fontMd_);
  } else if (!xboxSignedIn()) {
    renderCenteredText("Sign in with your Microsoft account to see your Xbox "
                       "Cloud Gaming library.",
                       0, cy - 50, kW, kColorTextPri, fontMd_);
    renderCenteredText("Enter / A / Click = Sign in", 0, cy - 10, kW, accent,
                       fontMd_);
    renderCenteredText(
        "Needs an Xbox profile; most games also need Game Pass Ultimate.", 0,
        cy + 30, kW, kColorTextDim, fontMd_);
  } else {
    renderCenteredText("No cloud games found for this account.", 0, cy - 50, kW,
                       kColorTextPri, fontMd_);
    renderCenteredText("Enter / A / Click = Refresh      S / Y = Sign in again",
                       0, cy - 10, kW, accent, fontMd_);
    renderCenteredText("Most games need Game Pass Ultimate.", 0, cy + 30, kW,
                       kColorTextDim, fontMd_);
  }
}

// ─── Main loop
// ────────────────────────────────────────────────────────────────

void GameShell::run(GameLibrary &library, const std::string &coversDir) {
  library_ = &library;
  coversDir_ = coversDir;
  currentFilter_ = -1;
  rebuildFilter();
  computeFilterRects();

  // Initialize save DB (saves/saves.db)
  try {
    const std::string saveDb =
        (fs::current_path() / "saves" / "saves.db").string();
    SaveManager::instance().init(saveDb);
  } catch (...) {
    // ignore init failures; save operations will be best-effort
  }

  ctrl_ = nullptr;
  ctrlName_.clear();
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (SDL_IsGameController(i)) {
      ctrl_ = SDL_GameControllerOpen(i);
      if (ctrl_)
        ctrlName_ = SDL_GameControllerName(ctrl_);
      break;
    }
  }

  // The bridge is started once in main() before SDL is initialised.
  // Here we just wait up to 4 s for SDL to fire SDL_CONTROLLERDEVICEADDED
  // in case the bridge was still spinning up when SDL first enumerated devices.
  if (!ctrl_) {
    bool wsl2 = false;
    {
      std::ifstream pv("/proc/version");
      std::string line;
      if (std::getline(pv, line))
        wsl2 = line.find("icrosoft") != std::string::npos;
    }
    if (wsl2) {
      Uint32 deadline = SDL_GetTicks() + 4000;
      while (SDL_GetTicks() < deadline && !ctrl_) {
        SDL_Delay(50);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
          if (ev.type == SDL_CONTROLLERDEVICEADDED && !ctrl_) {
            ctrl_ = SDL_GameControllerOpen(ev.cdevice.which);
            if (ctrl_)
              ctrlName_ = SDL_GameControllerName(ctrl_);
          }
        }
      }
      if (ctrl_)
        std::cerr << "[GameShell] Controller ready: " << ctrlName_ << "\n";
    }
  }

  // Re-apply after library load – by now the Wayland compositor has almost
  // certainly delivered its fullscreen configure.  This is also the fallback
  // if SDL_PumpEvents() in init() wasn't enough.
  SDL_RenderSetLogicalSize(renderer_, kW, kH);

  // On WSLg/XWayland (RDP-RAIL), SDL pointer events are delivered in the
  // X11 logical coordinate space, which equals the compositor's logical
  // resolution (1280×720 = kW×kH here).  SDL_GetWindowSize() however returns
  // physical pixels (2560×1440 on a 2560×1440 display) because XWayland
  // exposes the window at physical size.  Any division by winW/winH or by
  // the render scale would therefore halve the coords and restrict interaction
  // to the top-left quadrant.  The raw coords are already in kW×kH space, so
  // pass them through unchanged.
  auto toLogical = [](int wx, int wy, int *lx, int *ly) {
    *lx = wx;
    *ly = wy;
  };

  bool quit = false;

  // Run the controller rebind screen on startup, before the library UI.
  // This lets the user confirm/calibrate their pad every launch; ESC cancels.
  // (Previously this only auto-launched on first run when no mapping existed.)
  if (ctrl_) {
    std::cerr << "[GameShell] Launching controller setup.\n";
    runRebindScreen();
    rebuildFilter();
    computeFilterRects();
  } else if (!std::filesystem::exists("saves/controller_sdlmap.txt")) {
    std::cerr << "[GameShell] No controller detected; skipping setup.\n";
  }

  // Global F2 hotkey: opens the Macro Studio even while an emulator window
  // has focus (XInput2 raw key listener; no-op on pure Wayland).
  startMacroHotkeyListener();

  while (!quit) {
    SDL_Event ev;
    bool pendingLaunch = false;
    GameEntry launchTarget;

    // Global hotkey fired while another window had focus: open the studio.
    if (macroHotkeyPending_.exchange(false)) {
      runMacroStudio();
      rebuildFilter();
      computeFilterRects();
    }

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        quit = true;
        break;
      }

      // ── Window resize (e.g. display change or async fullscreen settle) ─
      if (ev.type == SDL_WINDOWEVENT &&
          (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
           ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
        SDL_RenderSetLogicalSize(renderer_, kW, kH);
      }

      // ── Keyboard ─────────────────────────────────────────────────────
      if (ev.type == SDL_KEYDOWN) {
        int n = static_cast<int>(filteredGames_.size());
        switch (ev.key.keysym.sym) {
        case SDLK_F1:
          // Open the interactive controller-rebind screen.
          runRebindScreen();
          rebuildFilter();
          computeFilterRects();
          break;
        case SDLK_F2:
          // Open the "Create Macro" studio (pauses a running game).
          if (!searchActive_) {
            runMacroStudio();
            rebuildFilter();
            computeFilterRects();
          }
          break;
        case SDLK_ESCAPE:
          if (searchActive_) {
            if (!searchQuery_.empty()) {
              searchQuery_.clear();
            } else {
              searchActive_ = false;
              SDL_StopTextInput();
            }
            rebuildFilter();
            computeFilterRects();
          } else if (xboxSignInWaiting()) {
            cancelXboxSignIn();
          } else {
            quit = true;
          }
          break;
        case SDLK_BACKSPACE:
          if (searchActive_ && !searchQuery_.empty()) {
            // Remove last UTF-8 code point
            while (searchQuery_.size() > 1 &&
                   (static_cast<unsigned char>(searchQuery_.back()) & 0xC0) ==
                       0x80)
              searchQuery_.pop_back();
            searchQuery_.pop_back();
            rebuildFilter();
            computeFilterRects();
          }
          break;
        case SDLK_f:
          if (ev.key.keysym.mod & KMOD_CTRL) {
            searchActive_ = true;
            SDL_StartTextInput();
          }
          break;
        case SDLK_m:
          // Ctrl+M — same as F2: open the "Create Macro" studio.
          if (!searchActive_ && (ev.key.keysym.mod & KMOD_CTRL)) {
            runMacroStudio();
            rebuildFilter();
            computeFilterRects();
          }
          break;
        case SDLK_LEFT:
          if (selectedIdx_ > 0) {
            --selectedIdx_;
            ensureVisible();
          }
          break;
        case SDLK_RIGHT:
          if (selectedIdx_ < n - 1) {
            ++selectedIdx_;
            ensureVisible();
          }
          break;
        case SDLK_UP:
          if (selectedIdx_ >= kCols) {
            selectedIdx_ -= kCols;
            ensureVisible();
          }
          break;
        case SDLK_DOWN:
          if (selectedIdx_ + kCols < n) {
            selectedIdx_ += kCols;
            ensureVisible();
          }
          break;
        case SDLK_PAGEUP:
          selectedIdx_ = std::max(0, selectedIdx_ - kCols * 3);
          ensureVisible();
          break;
        case SDLK_PAGEDOWN:
          selectedIdx_ = std::min(n - 1, selectedIdx_ + kCols * 3);
          ensureVisible();
          break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          if (xboxPromptActive()) {
            xboxPromptAction();
          } else if (!filteredGames_.empty()) {
            launchTarget = *filteredGames_[selectedIdx_];
            pendingLaunch = true;
          }
          break;
        case SDLK_F6:
          // Pick up cover images downloaded while the shell is open.
          library_->resolveCoverPaths(coversDir_);
          clearCoverCache();
          break;
        case SDLK_F5:
          if (xboxCloudFilterActive() && !xboxSignInWaiting() &&
              !xboxLoading_ && xboxSignedIn())
            reloadXboxLibrary();
          break;
        case SDLK_s:
          if (!searchActive_ && xboxPromptActive() && !xboxSignInWaiting() &&
              !xboxLoading_)
            startXboxSignIn();
          break;
        // 0 = All, 1-9 = system filter (disabled while search is active)
        case SDLK_0:
          if (!searchActive_) {
            currentFilter_ = -1;
            rebuildFilter();
            computeFilterRects();
          }
          break;
        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
        case SDLK_5:
        case SDLK_6:
        case SDLK_7:
        case SDLK_8:
        case SDLK_9: {
          if (!searchActive_) {
            int fi = ev.key.keysym.sym - SDLK_1;
            if (fi < kNumSystems) {
              currentFilter_ = fi;
              rebuildFilter();
              computeFilterRects();
            }
          }
          break;
        }
        default:
          break;
        }
      }

      // ── Mouse motion ─────────────────────────────────────────────────
      if (ev.type == SDL_MOUSEMOTION) {
        int lx, ly;
        toLogical(ev.motion.x, ev.motion.y, &lx, &ly);
        hoverIdx_ = gameAtMouse(lx, ly);
        hoverDropdown_ = dropdownItemAtMouse(lx, ly);
      }

      // ── Text input (search) ───────────────────────────────────────────
      if (ev.type == SDL_TEXTINPUT && searchActive_) {
        searchQuery_ += ev.text.text;
        rebuildFilter();
        computeFilterRects();
      }

      // ── Mouse button ─────────────────────────────────────────────────
      if (ev.type == SDL_MOUSEBUTTONDOWN &&
          ev.button.button == SDL_BUTTON_LEFT) {
        int mx, my;
        toLogical(ev.button.x, ev.button.y, &mx, &my);
        // DEBUG: print raw and logical coords so we can tune the transform
        {
          int ww, wh, ow, oh;
          SDL_GetWindowSize(window_, &ww, &wh);
          SDL_GetRendererOutputSize(renderer_, &ow, &oh);
          std::cerr << "[Click] raw=(" << ev.button.x << "," << ev.button.y
                    << ") logical=(" << mx << "," << my << ") winSize=" << ww
                    << "x" << wh << " outSize=" << ow << "x" << oh
                    << " kW=" << kW << " kH=" << kH
                    << " gameAtMouse=" << gameAtMouse(mx, my) << "\n";
        }
        int fi = filterBtnAtMouse(mx, my);
        int di = dropdownItemAtMouse(mx, my);
        if (di >= 0) {
          currentFilter_ = di;
          openFamily_ = -1;
          rebuildFilter();
          computeFilterRects();
        } else if (fi >= kNumSystems + 1) {
          int fam = fi - kNumSystems - 1;
          openFamily_ = (openFamily_ == fam) ? -1 : fam;
          layoutDropdown();
        } else if (fi >= -1) {
          openFamily_ = -1;
          currentFilter_ = fi;
          rebuildFilter();
          computeFilterRects();
        } else if (openFamily_ >= 0) {
          openFamily_ = -1;
          layoutDropdown();
        } else {
          // Game grid clicked
          int gi = gameAtMouse(mx, my);
          if (xboxPromptActive()) {
            xboxPromptAction();
          } else if (gi >= 0) {
            Uint32 now = SDL_GetTicks();
            if (gi == lastClickIdx_ && now - lastClickMs_ < 400) {
              // Double-click → launch
              launchTarget = *filteredGames_[gi];
              pendingLaunch = true;
              lastClickIdx_ = -1;
            } else {
              selectedIdx_ = gi;
              lastClickIdx_ = gi;
              lastClickMs_ = now;
            }
          }
        }
      }

      // ── Mouse wheel ──────────────────────────────────────────────────
      if (ev.type == SDL_MOUSEWHEEL) {
        int totalRows =
            (static_cast<int>(filteredGames_.size()) + kCols - 1) / kCols;
        scrollRow_ -= ev.wheel.y;
        int maxRow = std::max(0, totalRows - 2);
        scrollRow_ = std::max(0, std::min(scrollRow_, maxRow));
      }

      // ── Controller ───────────────────────────────────────────────────
      if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
        int n = static_cast<int>(filteredGames_.size());
        switch (ev.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_B:
          if (xboxSignInWaiting())
            cancelXboxSignIn();
          else
            quit = true;
          break;
        case SDL_CONTROLLER_BUTTON_X:
          if (xboxCloudFilterActive() && !xboxSignInWaiting() &&
              !xboxLoading_ && xboxSignedIn())
            reloadXboxLibrary();
          break;
        case SDL_CONTROLLER_BUTTON_Y:
          if (xboxPromptActive() && !xboxSignInWaiting() && !xboxLoading_)
            startXboxSignIn();
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
          if (selectedIdx_ > 0) {
            --selectedIdx_;
            ensureVisible();
          }
          ctrlRepeatBtn_ = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
          ctrlRepeatNext_ = SDL_GetTicks() + 400;
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
          if (selectedIdx_ < n - 1) {
            ++selectedIdx_;
            ensureVisible();
          }
          ctrlRepeatBtn_ = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
          ctrlRepeatNext_ = SDL_GetTicks() + 400;
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
          if (selectedIdx_ >= kCols) {
            selectedIdx_ -= kCols;
            ensureVisible();
          }
          ctrlRepeatBtn_ = SDL_CONTROLLER_BUTTON_DPAD_UP;
          ctrlRepeatNext_ = SDL_GetTicks() + 400;
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
          if (selectedIdx_ + kCols < n) {
            selectedIdx_ += kCols;
            ensureVisible();
          }
          ctrlRepeatBtn_ = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
          ctrlRepeatNext_ = SDL_GetTicks() + 400;
          break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: {
          // Cycle filter backwards (All → systems → Downloaded → All)
          int idx = (currentFilter_ + 1 + kNumSystems + 1) % (kNumSystems + 2);
          currentFilter_ = idx - 1;
          rebuildFilter();
          computeFilterRects();
          break;
        }
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
          // Cycle filter forwards (All → systems → Downloaded → All)
          int idx = (currentFilter_ + 2) % (kNumSystems + 2);
          currentFilter_ = idx - 1;
          rebuildFilter();
          computeFilterRects();
          break;
        }
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START:
          if (xboxPromptActive()) {
            xboxPromptAction();
          } else if (!filteredGames_.empty()) {
            launchTarget = *filteredGames_[selectedIdx_];
            pendingLaunch = true;
          }
          break;
        default:
          break;
        }
      }

      if (ev.type == SDL_CONTROLLERBUTTONUP) {
        if (ev.cbutton.button == static_cast<Uint8>(ctrlRepeatBtn_))
          ctrlRepeatBtn_ = -1;
      }

      if (ev.type == SDL_CONTROLLERDEVICEADDED && !ctrl_) {
        ctrl_ = SDL_GameControllerOpen(ev.cdevice.which);
        if (ctrl_)
          ctrlName_ = SDL_GameControllerName(ctrl_);
      }
      if (ev.type == SDL_CONTROLLERDEVICEREMOVED && ctrl_ &&
          SDL_GameControllerGetJoystick(ctrl_) ==
              SDL_JoystickFromInstanceID(ev.cdevice.which)) {
        SDL_GameControllerClose(ctrl_);
        ctrl_ = nullptr;
        ctrlName_.clear();
      }
    }

    // ── Controller: D-pad auto-repeat ─────────────────────────────────────
    if (ctrlRepeatBtn_ >= 0) {
      Uint32 now = SDL_GetTicks();
      if (now >= ctrlRepeatNext_) {
        int n = static_cast<int>(filteredGames_.size());
        switch (ctrlRepeatBtn_) {
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
          if (selectedIdx_ > 0) {
            --selectedIdx_;
            ensureVisible();
          }
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
          if (selectedIdx_ < n - 1) {
            ++selectedIdx_;
            ensureVisible();
          }
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
          if (selectedIdx_ >= kCols) {
            selectedIdx_ -= kCols;
            ensureVisible();
          }
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
          if (selectedIdx_ + kCols < n) {
            selectedIdx_ += kCols;
            ensureVisible();
          }
          break;
        }
        ctrlRepeatNext_ = now + 150;
      }
    }

    // ── Controller: left analogue stick navigation ────────────────────────
    if (ctrl_) {
      constexpr Sint16 kThresh = 12000;
      Sint16 lx = SDL_GameControllerGetAxis(ctrl_, SDL_CONTROLLER_AXIS_LEFTX);
      Sint16 ly = SDL_GameControllerGetAxis(ctrl_, SDL_CONTROLLER_AXIS_LEFTY);
      // Diagonal: prefer the dominant axis
      int stickDir = -1;
      if (std::abs(lx) >= std::abs(ly)) {
        if (lx < -kThresh)
          stickDir = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        else if (lx > kThresh)
          stickDir = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
      } else {
        if (ly < -kThresh)
          stickDir = SDL_CONTROLLER_BUTTON_DPAD_UP;
        else if (ly > kThresh)
          stickDir = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
      }

      Uint32 now = SDL_GetTicks();
      if (stickDir != ctrlStickDir_) {
        ctrlStickDir_ = stickDir;
        if (stickDir >= 0) {
          // Fire immediately on first entry into a new direction
          int n = static_cast<int>(filteredGames_.size());
          switch (stickDir) {
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (selectedIdx_ > 0) {
              --selectedIdx_;
              ensureVisible();
            }
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (selectedIdx_ < n - 1) {
              ++selectedIdx_;
              ensureVisible();
            }
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (selectedIdx_ >= kCols) {
              selectedIdx_ -= kCols;
              ensureVisible();
            }
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (selectedIdx_ + kCols < n) {
              selectedIdx_ += kCols;
              ensureVisible();
            }
            break;
          }
          ctrlStickNext_ = now + 400;
        }
      } else if (stickDir >= 0 && now >= ctrlStickNext_) {
        int n = static_cast<int>(filteredGames_.size());
        switch (stickDir) {
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
          if (selectedIdx_ > 0) {
            --selectedIdx_;
            ensureVisible();
          }
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
          if (selectedIdx_ < n - 1) {
            ++selectedIdx_;
            ensureVisible();
          }
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
          if (selectedIdx_ >= kCols) {
            selectedIdx_ -= kCols;
            ensureVisible();
          }
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
          if (selectedIdx_ + kCols < n) {
            selectedIdx_ += kCols;
            ensureVisible();
          }
          break;
        }
        ctrlStickNext_ = now + 150;
      }
    }

    if (pendingLaunch) {
      // If the ROM is not local but we have a download URL, fetch it first.
      // Steam and Xbox Cloud games launch externally; nothing to download
      // and the shell stays open.
      const bool launchesExternally =
          launchTarget.system == SystemType::Steam ||
          launchTarget.system == SystemType::Minecraft ||
          launchTarget.system == SystemType::EpicGames ||
          launchTarget.system == SystemType::XboxOne ||
          launchTarget.system == SystemType::XboxSeriesX;
      if (!launchesExternally && !launchTarget.downloadUrl.empty() &&
          (launchTarget.romPath.empty() ||
           !std::filesystem::exists(launchTarget.romPath))) {

        // Show "Downloading..." overlay on the existing window
        auto renderOverlay = [&](const std::string &msg, long long cur,
                                 long long tot) {
          // Dim the screen
          SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
          SDL_Rect full = {0, 0, kW, kH};
          SDL_RenderFillRect(renderer_, &full);

          // Progress bar background
          int barW = kW / 2, barH = 24;
          int barX = (kW - barW) / 2, barY = kH / 2 + 20;
          SDL_SetRenderDrawColor(renderer_, 60, 60, 90, 255);
          SDL_Rect bg = {barX, barY, barW, barH};
          SDL_RenderFillRect(renderer_, &bg);

          if (tot > 0) {
            int fill = std::min(
                static_cast<int>((double)cur / (double)tot * barW), barW);
            SDL_SetRenderDrawColor(renderer_, 80, 140, 240, 255);
            SDL_Rect fg = {barX, barY, fill, barH};
            SDL_RenderFillRect(renderer_, &fg);
          }

          // Label
          std::string label = msg;
          if (tot > 0 && cur > 0) {
            int pct = std::min(static_cast<int>(100.0 * cur / tot), 100);
            label += "  " + std::to_string(pct) + "%";
          }
          renderText(label, (kW - barW) / 2, kH / 2 - 14, {220, 220, 255, 255},
                     fontLg_, barW);

          SDL_RenderPresent(renderer_);
        };

        // Pump events so the window stays responsive during download
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
          if (ev.type == SDL_QUIT)
            goto done;
        }

        renderOverlay("Downloading " + launchTarget.displayName + "...", 0, 0);

        QueuedDownload qd;
        qd.id = launchTarget.dbId;
        qd.name = launchTarget.displayName;
        qd.url = launchTarget.downloadUrl;
        qd.system = GameLibrary::systemName(launchTarget.system);

        std::string dlError;
        std::string resolvedUrl;
        std::string localRom = DownloadManager::instance().downloadBlocking(
            qd,
            [&](long long cur, long long tot) {
              const bool extracting = tot < 0;
              renderOverlay((extracting ? "Extracting " : "Downloading ") +
                                launchTarget.displayName + "...",
                            extracting ? 0 : cur, extracting ? 0 : tot);
              while (SDL_PollEvent(&ev)) {
              }
            },
            &dlError, &resolvedUrl);

        // If fallback resolved a better URL, persist it to DB so future
        // downloads skip the search entirely.
        if (!localRom.empty() && !resolvedUrl.empty() &&
            resolvedUrl != launchTarget.downloadUrl && launchTarget.dbId > 0) {
          updateGameDownloadUrl(dbCfg_, launchTarget.dbId, resolvedUrl);
          launchTarget.downloadUrl = resolvedUrl;
        }
        if (!localRom.empty() && launchTarget.dbId > 0)
          updateGameLocalPath(dbCfg_, launchTarget.dbId, localRom);

        if (localRom.empty()) {
          // Show the error but keep the game in the library so the
          // user can retry later once the URL is fixed or connectivity
          // is restored.
          std::string msg;
          if (dlError.find("404") != std::string::npos) {
            msg = "File not found (404). Check the game's download URL.";
          } else if (dlError.find("could not be extracted") !=
                     std::string::npos) {
            msg = "Download completed, but the archive contained no supported "
                  "ROM.";
          } else if (dlError.find("401") != std::string::npos ||
                     dlError.find("403") != std::string::npos) {
            msg =
                "Archive.org authorization failed. Refresh the cookie export.";
          } else {
            msg = "Download failed. Check the connection and log.";
          }
          renderText(msg, (kW - 500) / 2, kH / 2 + 60, {240, 80, 80, 255},
                     fontLg_, 500);
          SDL_RenderPresent(renderer_);
          SDL_Delay(2000);
          pendingLaunch = false;
          goto done;
        }
        launchTarget.romPath = localRom;
        // Re-detect system from the extracted file's extension if still
        // Unknown.
        if (launchTarget.system == SystemType::Unknown) {
          std::string ext = fs::path(localRom).extension().string();
          for (auto &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          launchTarget.system = GameLibrary::systemFromExtension(ext);
        }
      }

      if (launchesExternally) {
        // External games launch separately; keep the shell open
        launchGame(launchTarget);
      } else {
        if (ctrl_) {
          SDL_GameControllerClose(ctrl_);
          ctrl_ = nullptr;
          ctrlName_.clear();
        }
        cleanup();
        // Ensure bridge is still alive before the emulator starts SDL.
        // (bridge is a separate process so it normally survives, but
        //  restart it if it crashed during a previous session.)
        if (std::system("pgrep -f '[u]input_client.py' >/dev/null 2>&1") != 0 &&
            std::filesystem::exists("controller_bridge/start_bridge.sh")) {
          std::cerr << "[GameShell] Bridge not running — restarting...\n";
          std::system("modprobe uinput 2>/dev/null; bash "
                      "controller_bridge/start_bridge.sh >/dev/null 2>&1 &");
          SDL_Delay(1500); // brief pause so uinput device appears before
                           // emulator's SDL_Init
        }
        launchGame(launchTarget);
        if (!init())
          break;
        // Re-apply logical size after renderer recreation for the same
        // reason as at the top of run() (async fullscreen on Wayland).
        SDL_RenderSetLogicalSize(renderer_, kW, kH);
        rebuildFilter();
        computeFilterRects();
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
          if (SDL_IsGameController(i)) {
            ctrl_ = SDL_GameControllerOpen(i);
            if (ctrl_)
              ctrlName_ = SDL_GameControllerName(ctrl_);
            break;
          }
        }
      }
    done:;
    }

    if (!quit) {
      pollXboxSignIn();
      render();
    }
  }

  stopMacroHotkeyListener();
  cancelXboxSignIn();
  if (ctrl_) {
    SDL_GameControllerClose(ctrl_);
    ctrl_ = nullptr;
    ctrlName_.clear();
  }
}

// ─── Game launch
// ──────────────────────────────────────────────────────────────

// Systems emulated in-process: runInteractive() blocks the shell until the
// game exits.  Everything else launches an external emulator and returns.
static bool usesBuiltinEmulator(SystemType sys) {
  switch (sys) {
  case SystemType::NES:
  case SystemType::SNES:
  case SystemType::GameBoy:
  case SystemType::GameBoyColor:
  case SystemType::GBA:
  case SystemType::NDS:
  case SystemType::ThreeDS:
  case SystemType::Atari2600:
  case SystemType::Atari5200:
  case SystemType::Atari800:
  case SystemType::SegaGenesis:
  case SystemType::N64:
  case SystemType::PS1:
  case SystemType::PS2:
  case SystemType::PS3:
  case SystemType::GameCube:
  case SystemType::Odyssey:
    return true;
  default:
    return false;
  }
}

void GameShell::launchGame(const GameEntry &game) {
  // Start the PyAutoGUI macro engine for any macros bound to this game
  // (no-op when none match).  Works with every console: input is injected
  // at the OS level and reaches whichever emulator window has focus.
  startMacroEngine(game);

  const std::string &path = game.romPath;
  // Defensive: if system is Unknown, try to detect from the file extension.
  SystemType sys = game.system;
  if (sys == SystemType::Unknown && !path.empty()) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    sys = GameLibrary::systemFromExtension(ext);
    if (sys == SystemType::Unknown && ext == ".bin") {
      sys = probeBinConsole(path);
      if (sys == SystemType::Unknown) {
        std::cerr << "[BIN] Console probes failed; treating the image as a PC "
                     "CUE hypothesis.\n";
        sys = SystemType::PC;
      }
    }
  }
  switch (sys) {
  case SystemType::NES: {
    NESEmulation nes;
    if (nes.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        nes.restoreSaveData(data);
      nes.runInteractive();
      SaveManager::instance().put(path, nes.dumpSaveData());
    }
    break;
  }
  case SystemType::SNES: {
    SNESEmulation snes;
    if (snes.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        snes.restoreSaveData(data);
      snes.runInteractive();
      SaveManager::instance().put(path, snes.dumpSaveData());
    }
    break;
  }
  case SystemType::GameBoy: {
    GameBoyEmulation gb;
    if (gb.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        gb.restoreSaveData(data);
      gb.runInteractive();
      SaveManager::instance().put(path, gb.dumpSaveData());
    }
    break;
  }
  case SystemType::GameBoyColor: {
    GameBoyColorEmulation gbc;
    if (gbc.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        gbc.restoreSaveData(data);
      gbc.runInteractive();
      SaveManager::instance().put(path, gbc.dumpSaveData());
    }
    break;
  }
  case SystemType::GBA: {
    GBAEmulation gba;
    try {
      if (gba.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          gba.restoreSaveData(data);
        gba.runInteractive();
        SaveManager::instance().put(path, gba.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "GBA error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::NDS: {
    NDSEmulation nds;
    try {
      if (nds.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          nds.restoreSaveData(data);
        nds.runInteractive();
        SaveManager::instance().put(path, nds.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "NDS error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::ThreeDS: {
    N3DSEmulation n3ds;
    try {
      if (n3ds.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          n3ds.restoreSaveData(data);
        n3ds.runInteractive();
        SaveManager::instance().put(path, n3ds.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "3DS error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::Atari2600: {
    Atari2600Emulation a;
    if (a.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        a.restoreSaveData(data);
      a.runInteractive();
      SaveManager::instance().put(path, a.dumpSaveData());
    }
    break;
  }
  case SystemType::Atari5200: {
    Atari5200Emulation a;
    if (a.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        a.restoreSaveData(data);
      a.runInteractive();
      SaveManager::instance().put(path, a.dumpSaveData());
    }
    break;
  }
  case SystemType::Atari800: {
    Atari800Emulation a;
    if (a.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        a.restoreSaveData(data);
      a.runInteractive();
      SaveManager::instance().put(path, a.dumpSaveData());
    }
    break;
  }
  case SystemType::SegaGenesis: {
    SegaGenesisEmulation gen;
    if (gen.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        gen.restoreSaveData(data);
      gen.runInteractive();
      SaveManager::instance().put(path, gen.dumpSaveData());
    }
    break;
  }
  case SystemType::N64: {
    N64Emulation n64;
    if (n64.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        n64.restoreSaveData(data);
      n64.runInteractive();
      SaveManager::instance().put(path, n64.dumpSaveData());
    }
    break;
  }
  case SystemType::PS1: {
    PS1Emulation ps1;
    try {
      if (ps1.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          ps1.restoreSaveData(data);
        ps1.runInteractive();
        SaveManager::instance().put(path, ps1.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "PS1 error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::PS2: {
    PS2Emulation ps2;
    try {
      if (ps2.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          ps2.restoreSaveData(data);
        ps2.runInteractive();
        SaveManager::instance().put(path, ps2.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "PS2 error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::PS3: {
    PS3Emulation ps3;
    try {
      if (ps3.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          ps3.restoreSaveData(data);
        ps3.runInteractive();
        SaveManager::instance().put(path, ps3.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "PS3 error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::GameCube: {
    GameCubeEmulation gc;
    try {
      if (gc.loadRom(path)) {
        std::vector<unsigned char> data;
        if (SaveManager::instance().get(path, data))
          gc.restoreSaveData(data);
        gc.runInteractive();
        SaveManager::instance().put(path, gc.dumpSaveData());
      }
    } catch (const std::exception &e) {
      std::cerr << "GameCube error: " << e.what() << "\n";
    }
    break;
  }
  case SystemType::Odyssey: {
    OdysseyEmulation ody;
    if (ody.loadRom(path)) {
      std::vector<unsigned char> data;
      if (SaveManager::instance().get(path, data))
        ody.restoreSaveData(data);
      ody.runInteractive();
      SaveManager::instance().put(path, ody.dumpSaveData());
    }
    break;
  }
  case SystemType::PC: {
    const std::string ext = [&]() {
      std::string e = fs::path(path).extension().string();
      std::transform(e.begin(), e.end(), e.begin(), ::tolower);
      return e;
    }();
    const char *configured = std::getenv(
        ext == ".exe" ? "OMNICON_PC_LAUNCHER" : "OMNICON_BIN_LAUNCHER");
    std::string launcher = configured && *configured
                               ? configured
                               : (ext == ".exe" ? "wine" : "xdg-open");
    std::string target = path;
    if (ext == ".bin") {
      fs::path cue = fs::path(path);
      cue.replace_extension(".cue");
      if (!fs::exists(cue)) {
        std::ofstream cueFile(cue);
        if (cueFile) {
          cueFile << "FILE \"" << fs::path(path).filename().string()
                  << "\" BINARY\n"
                  << "  TRACK 01 MODE1/2352\n"
                  << "    INDEX 01 00:00:00\n";
        }
      }
      if (!fs::exists(cue)) {
        std::cerr << "[PC] Could not create a CUE hypothesis for: " << path
                  << "\n";
        break;
      }
      target = cue.string();
    }
    if (!launchExternalChecked({launcher, target})) {
      if (ext != ".bin")
        break;
      std::cerr << "[PC] PC launcher did not start; trying the BIN with PS1 as "
                   "a fallback.\n";
      PS1Emulation ps1;
      if (ps1.loadRom(path))
        ps1.runInteractive();
    }
    break;
  }
  case SystemType::Dreamcast: {
    const char *configured = std::getenv("OMNICON_DREAMCAST_LAUNCHER");
    const char *coreEnv = std::getenv("OMNICON_FLYCAST_CORE");
    const std::string launcher =
        configured && *configured ? configured : "retroarch";
    const std::string core = coreEnv && *coreEnv
                                 ? coreEnv
                                 : "third_party/libretro/flycast_libretro.so";
    if (!fs::exists(core)) {
      std::cerr << "[Dreamcast] Flycast core not found: " << core << "\n"
                << "             Set OMNICON_FLYCAST_CORE to an installed "
                   "flycast_libretro.so.\n";
      break;
    }
    const std::string corePath = fs::absolute(core).string();
    const std::string configPath =
        fs::absolute("retroarch_flycast.cfg").string();
    if (!fs::exists(configPath)) {
      std::cerr << "[Dreamcast] RetroArch configuration not found: "
                << configPath << "\n";
      break;
    }
    const std::string contentPath = fs::absolute(path).string();
    std::cerr << "[Dreamcast] Launching " << launcher << " with core "
              << corePath << " and content " << contentPath << "\n";
    std::string libraryPath = "/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu";
    if (const char *existing = std::getenv("LD_LIBRARY_PATH");
        existing && *existing) {
      libraryPath += ":";
      libraryPath += existing;
    }
    std::vector<std::string> dcCommand = {"env"};
    const std::string systemWayland =
        "/usr/lib/x86_64-linux-gnu/libwayland-client.so.0";
    if (fs::exists(systemWayland))
      dcCommand.push_back("LD_PRELOAD=" + systemWayland);
    dcCommand.insert(dcCommand.end(), {"LD_LIBRARY_PATH=" + libraryPath,
                                       launcher, "--appendconfig", configPath,
                                       "-L", corePath, contentPath});
    if (!launchExternalChecked(dcCommand))
      std::cerr
          << "[Dreamcast] Failed to start " << launcher
          << ". Install RetroArch/Flycast or set OMNICON_DREAMCAST_LAUNCHER.\n";
    break;
  }
  case SystemType::SegaMasterSystem:
    SegaMasterSystemConsoleLauncher().launch(path);
    break;
  case SystemType::GameGear:
    GameGearConsoleLauncher().launch(path);
    break;
  case SystemType::SegaSaturn:
    SegaSaturnConsoleLauncher().launch(path);
    break;
  case SystemType::Wii:
    WiiConsoleLauncher().launch(path);
    break;
  case SystemType::WiiU:
    WiiUConsoleLauncher().launch(path);
    break;
  case SystemType::NintendoSwitch:
    NintendoSwitchConsoleLauncher().launch(path);
    break;
  case SystemType::PSP:
    PSPConsoleLauncher().launch(path);
    break;
  case SystemType::PSVita:
    PSVitaConsoleLauncher().launch(path);
    break;
  case SystemType::Xbox:
    XboxConsoleLauncher().launch(path);
    break;
  case SystemType::Xbox360:
    Xbox360ConsoleLauncher().launch(path);
    break;
  case SystemType::XboxOne:
  case SystemType::XboxSeriesX:
    XboxCloudConsoleLauncher().launch(path);
    break;
  // PC VR headsets run their Steam library entries through SteamVR.
  case SystemType::OculusRift:
  case SystemType::HTCVive:
  case SystemType::ValveIndex:
  case SystemType::Pico4:
    SteamVRConsoleLauncher().launch(path);
    break;
  case SystemType::Atari7800:
    Atari7800ConsoleLauncher().launch(path);
    break;
  case SystemType::AtariLynx:
    AtariLynxConsoleLauncher().launch(path);
    break;
  case SystemType::AtariJaguar:
    AtariJaguarConsoleLauncher().launch(path);
    break;
  case SystemType::PCEngine:
    PCEngineConsoleLauncher().launch(path);
    break;
  case SystemType::ColecoVision:
    ColecoVisionConsoleLauncher().launch(path);
    break;
  case SystemType::Intellivision:
    IntellivisionConsoleLauncher().launch(path);
    break;
  case SystemType::Steam: {
    // game.romPath holds the numeric Steam AppID (validated all-digits at
    // scan time); the "+" tile uses kSteamStoreId to open the store.
    const std::string url =
        (path == kSteamStoreId) ? "steam://store" : "steam://rungameid/" + path;

    // Detect WSL2 with Windows Steam on a /mnt drive: then cmd.exe is the
    // right launcher. Otherwise (native Linux, or WSL2 using the Linux
    // Steam client) fall back to xdg-open.
    bool inWSL2 = false;
    {
      std::ifstream pv("/proc/version");
      std::string line;
      if (std::getline(pv, line))
        inWSL2 = line.find("Microsoft") != std::string::npos ||
                 line.find("microsoft") != std::string::npos;
    }
    if (inWSL2) {
      bool windowsSteam = false;
      std::error_code ec;
      for (const char *drv : {"c", "d", "e", "f"})
        for (const char *s :
             {"/Program Files (x86)/Steam", "/Program Files/Steam"})
          if (std::filesystem::is_directory(
                  std::string("/mnt/") + drv + s + "/steamapps", ec))
            windowsSteam = true;
      inWSL2 = windowsSteam;
    }

    pid_t pid = fork();
    if (pid == 0) {
      setsid();
      if (inWSL2) {
        // cmd.exe /c start "" <url>  — the "" is the window-title arg
        // required when the first arg after start begins with a quote
        const char *argv[] = {"cmd.exe", "/c",        "start",
                              "",        url.c_str(), nullptr};
        execvp(argv[0], const_cast<char *const *>(argv));
      } else {
        const char *argv[] = {"xdg-open", url.c_str(), nullptr};
        execvp(argv[0], const_cast<char *const *>(argv));
      }
      _exit(1);
    }
    if (pid > 0)
      setLastLaunchedGamePid(pid);
    // Parent returns immediately; Steam launches asynchronously
    break;
  }
  case SystemType::Minecraft:
    MinecraftConsoleLauncher().launch(path);
    break;
  case SystemType::EpicGames:
    EpicGamesConsoleLauncher().launch(path);
    break;
  default:
    if (!launchExtraSystem(sys, path))
      std::cerr << "[GameShell] Unknown system for ROM: " << path << "\n";
    break;
  }

  // Built-in backends block until the game exits; their macros must not
  // keep injecting input into the shell afterwards.  External emulators
  // keep running, so their engine keeps running too (stop it from the
  // Macro Studio with S, or it exits when the shell closes).
  if (usesBuiltinEmulator(sys))
    stopMacroEngine();
}

// ─── Controller helpers
// ───────────────────────────────────────────────────────

std::string GameShell::joystickGUID() const {
  if (!ctrl_)
    return "";
  SDL_Joystick *joy = SDL_GameControllerGetJoystick(ctrl_);
  if (!joy)
    return "";
  char buf[64] = {};
  SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), buf, sizeof(buf));
  return buf;
}

// ─── Rebind screen
// ──────────────────────────────────────────────────────────── Press F1 from
// the shell, or triggered automatically on first run. For each logical action
// the user presses the physical button/axis they want. Saves an SDL
// GameController mapping to saves/controller_sdlmap.txt and applies it
// immediately via SDL_GameControllerAddMapping().

void GameShell::runRebindScreen() {
  if (!renderer_)
    return;

  // Helper: full-screen status message
  auto showMsg = [&](const std::string &msg,
                     SDL_Color col = {200, 200, 255, 255}) {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);
    renderCenteredText(msg, 0, kH / 2 - 14, kW, col, fontLg_);
    SDL_RenderPresent(renderer_);
  };

  // ── Step 1: kill old bridge + restart with up-to-date xinput_server.py ───
  showMsg("Restarting controller bridge...");
  if (ctrl_) {
    SDL_GameControllerClose(ctrl_);
    ctrl_ = nullptr;
    ctrlName_.clear();
  }
  std::system("pkill -f '[u]input_client.py' 2>/dev/null; pkill -f "
              "'[x]input_server.py' 2>/dev/null");
  { // drain events while bridge dies
    Uint32 t = SDL_GetTicks() + 1500;
    SDL_Event ev;
    while (SDL_GetTicks() < t) {
      SDL_Delay(30);
      while (SDL_PollEvent(&ev)) {
      }
    }
  }
  if (std::filesystem::exists("controller_bridge/start_bridge.sh")) {
    std::system("modprobe uinput 2>/dev/null; bash "
                "controller_bridge/start_bridge.sh >/dev/null 2>&1 &");
  }
  showMsg("Waiting for controller...");
  {
    Uint32 deadline = SDL_GetTicks() + 5000;
    SDL_Event ev;
    while (SDL_GetTicks() < deadline && !ctrl_) {
      SDL_Delay(50);
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_CONTROLLERDEVICEADDED && !ctrl_) {
          ctrl_ = SDL_GameControllerOpen(ev.cdevice.which);
          if (ctrl_)
            ctrlName_ = SDL_GameControllerName(ctrl_);
        }
      }
    }
  }
  if (!ctrl_) {
    showMsg("No controller detected — press any key to cancel.",
            {240, 80, 80, 255});
    SDL_Event ev;
    while (SDL_WaitEvent(&ev))
      if (ev.type == SDL_KEYDOWN || ev.type == SDL_QUIT)
        break;
    return;
  }
  showMsg("Controller ready: " + ctrlName_);
  SDL_Delay(800);

  // ── Step 2: capture each action ──────────────────────────────────────────
  struct Action {
    const char *label;  // shown on screen
    const char *sdlKey; // SDL mapping token (a, b, leftx, lefty, …)
    bool isAxis;        // false = SDL_JOYBUTTONDOWN, true = SDL_JOYAXISMOTION
    int rawIdx = -1;
    bool invert = false;
  };
  std::vector<Action> actions = {
      // ── Face buttons ──────────────────────────────────────────────────
      {"A  (bottom face / confirm)", "a", false},
      {"B  (right face / cancel)", "b", false},
      {"X  (left face)", "x", false},
      {"Y  (top face)", "y", false},
      // ── Shoulder / trigger ────────────────────────────────────────────
      {"L  (left shoulder)", "leftshoulder", false},
      {"R  (right shoulder)", "rightshoulder", false},
      {"ZL (left trigger — press fully)", "lefttrigger", true},
      {"ZR (right trigger — press fully)", "righttrigger", true},
      // ── Menu ──────────────────────────────────────────────────────────
      {"Back / \xe2\x88\x92 (Minus)", "back", false},
      {"Start / + (Plus)", "start", false},
      // ── Stick clicks ──────────────────────────────────────────────────
      {"L3 (left stick click)", "leftstick", false},
      {"R3 (right stick click)", "rightstick", false},
      // ── Axes: ask for a specific direction to detect sign ─────────────
      {"Left Stick \xe2\x86\x92  RIGHT (then return to centre)", "leftx", true},
      {"Left Stick \xe2\x86\x93  DOWN  (then return to centre)", "lefty", true},
      {"Right Stick \xe2\x86\x92 RIGHT (then return to centre)", "rightx",
       true},
      {"Right Stick \xe2\x86\x93 DOWN  (then return to centre)", "righty",
       true},
  };

  SDL_JoystickID joyId = -1;
  SDL_Joystick *joy = ctrl_ ? SDL_GameControllerGetJoystick(ctrl_) : nullptr;
  if (joy)
    joyId = SDL_JoystickInstanceID(joy);

  // Snapshot axis centres for deflection detection
  std::array<Sint16, 32> axisCenter = {};
  if (joy) {
    int n = std::min(SDL_JoystickNumAxes(joy), 32);
    for (int i = 0; i < n; ++i)
      axisCenter[i] = SDL_JoystickGetAxis(joy, i);
  }

  int idx = 0;
  bool done = false;

  auto drawRebind = [&]() {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);

    // Title bar
    renderCenteredText(
        "Controller Setup   \xe2\x80\x94   F1 to re-open   \xe2\x80\x94   ESC "
        "to cancel   \xe2\x80\x94   Backspace = redo last",
        0, 6, kW, {140, 140, 200, 255}, fontMd_);

    // Left column: already-mapped
    int listY = 32;
    for (int i = 0; i < idx && i < (int)actions.size(); ++i) {
      std::string txt = std::string(actions[i].label) + "  \xe2\x86\x92  ";
      if (actions[i].isAxis)
        txt += "axis " + std::to_string(actions[i].rawIdx) +
               (actions[i].invert ? "~" : "");
      else
        txt += "btn " + std::to_string(actions[i].rawIdx);
      renderText(txt, 30, listY, {80, 200, 80, 255}, fontSm_, kW / 2 - 50);
      listY += 20;
    }

    // Right half: current prompt
    if (idx < (int)actions.size()) {
      int py = kH / 2 - 50;
      renderCenteredText(actions[idx].isAxis ? "Push:" : "Press:", kW / 2, py,
                         kW / 2, {160, 160, 200, 255}, fontMd_);
      renderCenteredText(actions[idx].label, kW / 2, py + 26, kW / 2,
                         {255, 220, 60, 255}, fontLg_);
      std::string progress =
          std::to_string(idx + 1) + " / " + std::to_string(actions.size());
      renderCenteredText(progress, kW / 2, py + 70, kW / 2,
                         {110, 110, 150, 255}, fontMd_);
    }

    SDL_RenderPresent(renderer_);
  };

  while (!done && idx < (int)actions.size()) {
    drawRebind();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        done = true;
        break;
      }
      if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_ESCAPE) {
          done = true;
          break;
        }
        if (ev.key.keysym.sym == SDLK_BACKSPACE && idx > 0) {
          --idx;
          break;
        }
      }

      auto &act = actions[idx];
      if (!act.isAxis && ev.type == SDL_JOYBUTTONDOWN &&
          ev.jbutton.which == joyId) {
        act.rawIdx = ev.jbutton.button;
        ++idx;
      } else if (act.isAxis && ev.type == SDL_JOYAXISMOTION &&
                 ev.jaxis.which == joyId) {
        int ai = std::min((int)ev.jaxis.axis, 31);
        // Use int arithmetic to avoid Sint16 overflow when a trigger
        // goes from its idle value (−32768) to fully pressed (+32767).
        int delta = (int)ev.jaxis.value - (int)axisCenter[ai];
        if (std::abs(delta) > 14000) {
          act.rawIdx = ev.jaxis.axis;
          act.invert = (ev.jaxis.value < 0);
          ++idx;

          // ── Wait for axis to return to centre ─────────────────
          // Drain events until the captured axis is back near idle,
          // so the deflection doesn't bleed into the next prompt.
          {
            Uint32 deadline = SDL_GetTicks() + 1500;
            SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
            SDL_RenderClear(renderer_);
            renderCenteredText("Release...", 0, kH / 2 - 14, kW,
                               {160, 160, 200, 255}, fontMd_);
            SDL_RenderPresent(renderer_);

            bool released = false;
            while (!released && SDL_GetTicks() < deadline) {
              SDL_Event re;
              while (SDL_PollEvent(&re)) {
                if (re.type == SDL_JOYAXISMOTION && re.jaxis.which == joyId &&
                    re.jaxis.axis == act.rawIdx) {
                  int cur = (int)re.jaxis.value - (int)axisCenter[ai];
                  if (std::abs(cur) < 6000)
                    released = true;
                }
              }
              SDL_Delay(16);
            }
            // Extra 150 ms gap before showing the next prompt
            SDL_Delay(150);
          }
        }
      }
    }
    SDL_Delay(16);
  }
  if (done)
    return;

  // ── Step 3: build SDL mapping string ─────────────────────────────────────
  std::string guid = joystickGUID();
  if (guid.empty()) {
    std::cerr << "[rebind] Failed to get joystick GUID\n";
    return;
  }
  std::string mapping = guid + ",OmniCon Controller,";
  for (const auto &act : actions) {
    if (act.rawIdx < 0)
      continue;
    mapping += std::string(act.sdlKey) + ":";
    if (act.isAxis) {
      mapping += "a" + std::to_string(act.rawIdx);
      if (act.invert)
        mapping += "~";
    } else {
      mapping += "b" + std::to_string(act.rawIdx);
    }
    mapping += ",";
  }
  // D-pad always comes from the HAT (ABS_HAT0X/Y set by uinput_client.py)
  mapping += "dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,platform:Linux,";

  // ── Step 4: apply ─────────────────────────────────────────────────────────
  if (SDL_GameControllerAddMapping(mapping.c_str()) < 0)
    std::cerr << "[rebind] SDL_GameControllerAddMapping: " << SDL_GetError()
              << "\n";

  // Close + reopen so the running controller picks up the new mapping
  if (ctrl_) {
    SDL_GameControllerClose(ctrl_);
    ctrl_ = nullptr;
  }
  SDL_Delay(100);
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (SDL_IsGameController(i)) {
      ctrl_ = SDL_GameControllerOpen(i);
      if (ctrl_) {
        ctrlName_ = SDL_GameControllerName(ctrl_);
        break;
      }
    }
  }

  // ── Step 5: persist ───────────────────────────────────────────────────────
  std::filesystem::create_directories("saves");
  {
    std::ofstream f("saves/controller_sdlmap.txt");
    f << mapping << "\n";
  }

  showMsg("Saved!  Controller ready.", {80, 230, 80, 255});
  SDL_Delay(1500);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-console controller remapping
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

using BtnDef = std::pair<std::string, SDL_GameControllerButton>;
using BtnList = std::vector<BtnDef>;

// ── conf file helpers
// ─────────────────────────────────────────────────────────
static const std::string kConsoleBindFile = "saves/console_bindings.conf";

// Write (or update) one [Section] in the INI file.
void saveConsoleBind(const std::string &section, const BtnList &defs,
                     const std::vector<SDL_GameControllerButton> &vals) {
  // Read all existing sections
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> all;
  std::vector<std::string> order; // preserve section order
  {
    std::ifstream f(kConsoleBindFile);
    std::string cur, line;
    while (std::getline(f, line)) {
      if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
        cur = line.substr(1, line.size() - 2);
        if (all.find(cur) == all.end())
          order.push_back(cur);
      } else if (!cur.empty() && line.find('=') != std::string::npos) {
        auto eq = line.find('=');
        all[cur].emplace_back(line.substr(0, eq), line.substr(eq + 1));
      }
    }
  }
  // Rebuild section
  if (all.find(section) == all.end())
    order.push_back(section);
  all[section].clear();
  for (size_t i = 0; i < defs.size() && i < vals.size(); ++i)
    all[section].emplace_back(defs[i].first, std::to_string((int)vals[i]));

  std::filesystem::create_directories("saves");
  std::ofstream f(kConsoleBindFile);
  for (const auto &sec : order) {
    f << "[" << sec << "]\n";
    for (const auto &kv : all[sec])
      f << kv.first << "=" << kv.second << "\n";
    f << "\n";
  }
}

// Load one [Section]; returns defaults if section absent or file missing.
std::vector<SDL_GameControllerButton>
loadConsoleBind(const std::string &section, const BtnList &defs) {
  std::vector<SDL_GameControllerButton> res;
  res.reserve(defs.size());
  for (auto &d : defs)
    res.push_back(d.second); // start with defaults

  std::ifstream f(kConsoleBindFile);
  if (!f)
    return res;

  std::string cur, line;
  while (std::getline(f, line)) {
    if (line.size() > 2 && line.front() == '[' && line.back() == ']')
      cur = line.substr(1, line.size() - 2);
    else if (cur == section) {
      auto eq = line.find('=');
      if (eq != std::string::npos) {
        std::string key = line.substr(0, eq);
        int val = 0;
        try {
          val = std::stoi(line.substr(eq + 1));
        } catch (...) {
        }
        for (size_t i = 0; i < defs.size(); ++i)
          if (defs[i].first == key) {
            res[i] = (SDL_GameControllerButton)val;
            break;
          }
      }
    }
  }
  return res;
}

// Apply a flat vector of buttons to a globalControllerBindings struct.
// All ControllerBindings structs contain only SDL_GameControllerButton fields
// so the reinterpret_cast is safe (standard-layout, no padding).
void applyFlat(void *target, bool *configured,
               const std::vector<SDL_GameControllerButton> &v) {
  auto *arr = reinterpret_cast<SDL_GameControllerButton *>(target);
  for (size_t i = 0; i < v.size(); ++i)
    arr[i] = v[i];
  if (configured)
    *configured = true;
}

} // anonymous namespace

// ── Console definitions table
// ───────────────────────────────────────────────── Each entry: { section-name,
// button-list-with-defaults, &globalBindings, &globalConfigured } Button list
// order MUST match the C++ struct field order.

struct ConsoleCtrlDef {
  const char *name;
  BtnList btns;
  void *globalBindPtr;
  bool *globalConfigured;
};

static std::vector<ConsoleCtrlDef> makeConsoleTable() {
  using B = SDL_GameControllerButton;
  constexpr B U = SDL_CONTROLLER_BUTTON_DPAD_UP;
  constexpr B D = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  constexpr B L = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  constexpr B R = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  constexpr B A = SDL_CONTROLLER_BUTTON_A;
  constexpr B Bb = SDL_CONTROLLER_BUTTON_B;
  constexpr B X = SDL_CONTROLLER_BUTTON_X;
  constexpr B Y = SDL_CONTROLLER_BUTTON_Y;
  constexpr B LS = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  constexpr B RS = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  constexpr B BK = SDL_CONTROLLER_BUTTON_BACK;
  constexpr B ST = SDL_CONTROLLER_BUTTON_START;
  constexpr B L3 = SDL_CONTROLLER_BUTTON_LEFTSTICK;
  constexpr B R3 = SDL_CONTROLLER_BUTTON_RIGHTSTICK;

  return {
      // ── NES ────────────────────────────────────────────────────────────────
      {"NES",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"Select", BK},
               {"Start", ST}},
       &NESEmulation::globalControllerBindings,
       &NESEmulation::globalInputConfigured},

      // ── SNES ───────────────────────────────────────────────────────────────
      {"SNES",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"X", X},
               {"Y", Y},
               {"L", LS},
               {"R", RS},
               {"Select", BK},
               {"Start", ST}},
       &SNESEmulation::globalControllerBindings,
       &SNESEmulation::globalInputConfigured},

      // ── Game Boy (GB + GBC share the same global) ──────────────────────────
      {"GB",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"Select", BK},
               {"Start", ST}},
       &GameBoyEmulation::globalControllerBindings,
       &GameBoyEmulation::globalInputConfigured},

      // ── GBA — struct order: a,b,select,start,right,left,up,down,r,l ────────
      {"GBA",
       BtnList{{"A", A},
               {"B", Bb},
               {"Select", BK},
               {"Start", ST},
               {"\xe2\x86\x92 Right", R},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"R", RS},
               {"L", LS}},
       &GBAEmulation::globalControllerBindings,
       &GBAEmulation::globalInputConfigured},

      // ── NDS — struct order: up,down,left,right,a,b,x,y,start,select,l,r ───
      {"NDS",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"X", X},
               {"Y", Y},
               {"Start", ST},
               {"Select", BK},
               {"L", LS},
               {"R", RS}},
       &NDSEmulation::globalControllerBindings,
       &NDSEmulation::globalInputConfigured},

      // ── 3DS — same as NDS + ZL, ZR ────────────────────────────────────────
      {"3DS",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"X", X},
               {"Y", Y},
               {"Start", ST},
               {"Select", BK},
               {"L", LS},
               {"R", RS},
               {"ZL", L3},
               {"ZR", R3}},
       &N3DSEmulation::globalControllerBindings,
       &N3DSEmulation::globalInputConfigured},

      // ── N64 — up,down,left,right,a,b,c_up,c_down,c_left,c_right,l,r,z,start
      {"N64",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"C-Up", Y},
               {"C-Down", BK},
               {"C-Left", L3},
               {"C-Right", R3},
               {"L", LS},
               {"R", RS},
               {"Z", X},
               {"Start", ST}},
       &N64Emulation::globalControllerBindings,
       &N64Emulation::globalInputConfigured},

      // ── PS1 —
      // up,down,left,right,cross,circle,square,triangle,start,select,l1,r1
      {"PS1",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"Cross \xc3\x97", A},
               {"Circle \xe2\x97\x8b", Bb},
               {"Square \xe2\x96\xa1", X},
               {"Triangle \xe2\x96\xb3", Y},
               {"Start", ST},
               {"Select", BK},
               {"L1", LS},
               {"R1", RS}},
       &PS1Emulation::globalControllerBindings,
       &PS1Emulation::globalInputConfigured},

      // ── PS2 — same struct layout as PS1 ────────────────────────────────────
      {"PS2",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"Cross \xc3\x97", A},
               {"Circle \xe2\x97\x8b", Bb},
               {"Square \xe2\x96\xa1", X},
               {"Triangle \xe2\x96\xb3", Y},
               {"Start", ST},
               {"Select", BK},
               {"L1", LS},
               {"R1", RS}},
       &PS2Emulation::globalControllerBindings,
       &PS2Emulation::globalInputConfigured},

      // ── PS3 — same struct layout as PS1/PS2 ────────────────────────────────
      {"PS3",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"Cross \xc3\x97", A},
               {"Circle \xe2\x97\x8b", Bb},
               {"Square \xe2\x96\xa1", X},
               {"Triangle \xe2\x96\xb3", Y},
               {"Start", ST},
               {"Select", BK},
               {"L1", LS},
               {"R1", RS}},
       &PS3Emulation::globalControllerBindings,
       &PS3Emulation::globalInputConfigured},

      // ── GameCube — same struct layout (cross→A,circle→B,square→X,triangle→Y)
      {"GameCube",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", A},
               {"B", Bb},
               {"X", X},
               {"Y", Y},
               {"Start", ST},
               {"Select", BK},
               {"L", LS},
               {"R", RS}},
       &GameCubeEmulation::globalControllerBindings,
       &GameCubeEmulation::globalInputConfigured},

      // ── Sega Genesis — up,down,left,right,a,b,c,x,y,z,start,mode ──────────
      {"Genesis",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"A", X},
               {"B", A},
               {"C", Bb},
               {"X", Y},
               {"Y", LS},
               {"Z", RS},
               {"Start", ST},
               {"Mode", BK}},
       &SegaGenesisEmulation::globalControllerBindings,
       &SegaGenesisEmulation::globalInputConfigured},

      // ── Atari 2600 — up,down,left,right,fire,select,start,reset ───────────
      {"Atari2600",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"Fire", A},
               {"Select", BK},
               {"Start", ST},
               {"Reset", X}},
       &Atari2600Emulation::globalControllerBindings,
       &Atari2600Emulation::globalInputConfigured},

      // ── Atari 5200 — up,down,left,right,fire,secondary,select,start,reset ─
      {"Atari5200",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"Fire", A},
               {"Secondary", Bb},
               {"Select", BK},
               {"Start", ST},
               {"Reset", X}},
       &Atari5200Emulation::globalControllerBindings,
       &Atari5200Emulation::globalInputConfigured},

      // ── Atari 800 — up,down,left,right,fire,secondary,select,start,option ─
      {"Atari800",
       BtnList{{"\xe2\x86\x91 Up", U},
               {"\xe2\x86\x93 Down", D},
               {"\xe2\x86\x90 Left", L},
               {"\xe2\x86\x92 Right", R},
               {"Fire", A},
               {"Secondary", Bb},
               {"Select", BK},
               {"Start", ST},
               {"Option", LS}},
       &Atari800Emulation::globalControllerBindings,
       &Atari800Emulation::globalInputConfigured},

      // ── Odyssey (Pong-style) — p1Up,p1Down,p2Up,p2Down,serve ─────────────
      {"Odyssey",
       BtnList{{"P1 Up", U},
               {"P1 Down", D},
               {"P2 Up", LS},
               {"P2 Down", RS},
               {"Serve", A}},
       &OdysseyEmulation::globalControllerBindings,
       &OdysseyEmulation::globalInputConfigured},
  };
}

// ── Per-console rebind UI
// ─────────────────────────────────────────────────────

void GameShell::runConsoleRebindScreen(const std::string &sysName) {
  if (!renderer_ || !ctrl_)
    return;

  auto consoles = makeConsoleTable();

  // Filter: "all" runs every console; otherwise match by name
  // (case-insensitive)
  bool runAll = (sysName.empty() || sysName == "all");
  std::vector<const ConsoleCtrlDef *> todo;
  for (const auto &c : consoles) {
    if (runAll) {
      todo.push_back(&c);
      continue;
    }
    std::string a = sysName, b = c.name;
    for (auto &ch : a)
      ch = (char)std::tolower((unsigned char)ch);
    for (auto &ch : b)
      ch = (char)std::tolower((unsigned char)ch);
    if (a == b)
      todo.push_back(&c);
  }

  // Helper: draw the current state
  auto drawConsole = [&](const ConsoleCtrlDef &con, int idx,
                         const std::vector<SDL_GameControllerButton> &current) {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);

    // Title
    std::string title = std::string(con.name) + " Controller Setup" +
                        "   \xe2\x80\x94   ESC=cancel   Backspace=redo last";
    renderCenteredText(title, 0, 6, kW, {140, 140, 200, 255}, fontMd_);

    // Left column: completed
    int listY = 32;
    for (int i = 0; i < idx; ++i) {
      std::string line = con.btns[i].first + "  \xe2\x86\x92  " +
                         SDL_GameControllerGetStringForButton(current[i]);
      renderText(line, 30, listY, {80, 200, 80, 255}, fontSm_, kW / 2 - 50);
      listY += 20;
    }

    // Right half: current prompt
    if (idx < (int)con.btns.size()) {
      int py = kH / 2 - 50;
      renderCenteredText("Press:", kW / 2, py, kW / 2, {160, 160, 200, 255},
                         fontMd_);
      renderCenteredText(con.btns[idx].first, kW / 2, py + 26, kW / 2,
                         {255, 220, 60, 255}, fontLg_);
      renderCenteredText(
          std::to_string(idx + 1) + " / " + std::to_string(con.btns.size()),
          kW / 2, py + 70, kW / 2, {110, 110, 150, 255}, fontMd_);
    }

    SDL_RenderPresent(renderer_);
  };

  for (const auto *cp : todo) {
    const auto &con = *cp;

    // Load existing saved values as starting point
    auto current = loadConsoleBind(con.name, con.btns);

    int idx = 0;
    bool skip = false;

    while (!skip && idx < (int)con.btns.size()) {
      drawConsole(con, idx, current);

      SDL_Event ev;
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
          skip = true;
          break;
        }
        if (ev.type == SDL_KEYDOWN) {
          if (ev.key.keysym.sym == SDLK_ESCAPE) {
            skip = true;
            break;
          }
          if (ev.key.keysym.sym == SDLK_BACKSPACE && idx > 0) {
            --idx;
            break;
          }
        }
        // Accept any SDL controller button (includes DPAD_*)
        if (ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.which != -1) {
          current[idx] = (SDL_GameControllerButton)ev.cbutton.button;
          ++idx;
          break;
        }
      }
      SDL_Delay(16);
    }
    if (skip)
      break;

    // Save + apply
    saveConsoleBind(con.name, con.btns, current);
    applyFlat(con.globalBindPtr, con.globalConfigured, current);

    // Brief confirmation before the next console
    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);
    renderCenteredText(std::string(con.name) + " saved!", 0, kH / 2 - 14, kW,
                       {80, 230, 80, 255}, fontLg_);
    SDL_RenderPresent(renderer_);
    SDL_Delay(700);
  }
}

// ── remapConsole (public, stand-alone wrapper)
// ────────────────────────────────

void GameShell::remapConsole(const std::string &sysName) {
  if (!init()) {
    std::cerr << "[remapConsole] SDL init failed\n";
    return;
  }

  // Connect controller (bridge already running from startup)
  for (int i = 0; i < SDL_NumJoysticks() && !ctrl_; ++i) {
    if (SDL_IsGameController(i)) {
      ctrl_ = SDL_GameControllerOpen(i);
      if (ctrl_)
        ctrlName_ = SDL_GameControllerName(ctrl_);
    }
  }
  if (!ctrl_) {
    // Wait up to 4 s
    Uint32 dl = SDL_GetTicks() + 4000;
    SDL_Event ev;
    while (SDL_GetTicks() < dl && !ctrl_) {
      SDL_Delay(50);
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_CONTROLLERDEVICEADDED && !ctrl_) {
          ctrl_ = SDL_GameControllerOpen(ev.cdevice.which);
          if (ctrl_)
            ctrlName_ = SDL_GameControllerName(ctrl_);
        }
      }
    }
  }
  if (!ctrl_) {
    std::cerr << "[remapConsole] No controller found\n";
    cleanup();
    return;
  }
  runConsoleRebindScreen(sysName);
  cleanup();
}

// ── remapRetroArchPlayer (RetroArch player-N bindings →
// retroarch_controller.cfg)

void GameShell::remapRetroArchPlayer(int player) {
  if (!init()) {
    std::cerr << "[remapRetroArchPlayer] SDL init failed\n";
    return;
  }

  // Helper: full-screen status message
  auto showMsg = [&](const std::string &msg,
                     SDL_Color col = {200, 200, 255, 255}) {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);
    renderCenteredText(msg, 0, kH / 2 - 14, kW, col, fontLg_);
    SDL_RenderPresent(renderer_);
  };

  // ── Step 1: restart the bridge so it runs the latest xinput_server.py ────
  showMsg("Restarting controller bridge...");
  std::system("pkill -f '[u]input_client.py' 2>/dev/null; pkill -f "
              "'[x]input_server.py' 2>/dev/null");
  { // drain events while bridge dies
    Uint32 t = SDL_GetTicks() + 1500;
    SDL_Event ev;
    while (SDL_GetTicks() < t) {
      SDL_Delay(30);
      while (SDL_PollEvent(&ev)) {
      }
    }
  }
  if (std::filesystem::exists("controller_bridge/start_bridge.sh")) {
    std::system("modprobe uinput 2>/dev/null; bash "
                "controller_bridge/start_bridge.sh >/dev/null 2>&1 &");
  }

  // ── Step 2: open every pad; the user picks one by pressing a button ──────
  // The bridge creates one virtual pad per physical controller, but only
  // after that controller's first input, and SDL enumeration order is not
  // guaranteed to match player order — so let the user identify the pad.
  struct OpenPad {
    SDL_GameController *ctrl;
    SDL_JoystickID joyId;
  };
  std::vector<OpenPad> openPads;
  auto refreshPads = [&]() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      if (!SDL_IsGameController(i))
        continue;
      SDL_GameController *c = SDL_GameControllerOpen(i);
      if (!c)
        continue;
      SDL_JoystickID id =
          SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
      bool dup = false;
      for (auto &p : openPads)
        if (p.joyId == id) {
          dup = true;
          break;
        }
      if (dup) {
        SDL_GameControllerClose(c);
        continue;
      }
      openPads.push_back({c, id});
    }
  };

  bool cancelled = false;
  int selected = -1;
  while (selected < 0 && !cancelled) {
    refreshPads();
    showMsg("Press any button on the controller for player " +
            std::to_string(player) + "   (ESC to cancel)");
    Uint32 deadline = SDL_GetTicks() + 250;
    SDL_Event ev;
    while (SDL_GetTicks() < deadline && selected < 0 && !cancelled) {
      SDL_Delay(20);
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
          cancelled = true;
        else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
          cancelled = true;
        else if (ev.type == SDL_JOYBUTTONDOWN) {
          for (size_t i = 0; i < openPads.size(); ++i)
            if (openPads[i].joyId == ev.jbutton.which)
              selected = (int)i;
        }
      }
    }
  }
  if (cancelled || selected < 0) {
    for (auto &p : openPads)
      SDL_GameControllerClose(p.ctrl);
    cleanup();
    return;
  }
  ctrl_ = openPads[selected].ctrl;
  ctrlName_ = SDL_GameControllerName(ctrl_);
  for (size_t i = 0; i < openPads.size(); ++i)
    if ((int)i != selected)
      SDL_GameControllerClose(openPads[i].ctrl);
  showMsg("Controller ready: " + ctrlName_);
  SDL_Delay(800);

  // ── Step 3: capture each input ───────────────────────────────────────────
  struct Action {
    const char *label; // shown on screen
    const char *raKey; // RetroArch key suffix ("b_btn", "l2_axis", "l_x")
    bool isAxis;       // false = SDL_JOYBUTTONDOWN, true = SDL_JOYAXISMOTION
    bool isStick;      // true → writes <raKey>_plus_axis / <raKey>_minus_axis
    int rawIdx = -1;
    bool positive = true; // axis deflection direction
  };
  std::vector<Action> actions = {
      // ── Face buttons ──────────────────────────────────────────────────
      {"B  (bottom face / confirm)", "b_btn", false, false},
      {"A  (right face / cancel)", "a_btn", false, false},
      {"X  (left face)", "x_btn", false, false},
      {"Y  (top face)", "y_btn", false, false},
      // ── Shoulder ──────────────────────────────────────────────────────
      {"L  (left shoulder)", "l_btn", false, false},
      {"R  (right shoulder)", "r_btn", false, false},
      // ── Menu ──────────────────────────────────────────────────────────
      {"Select / Back", "select_btn", false, false},
      {"Start", "start_btn", false, false},
      // ── Stick clicks ──────────────────────────────────────────────────
      {"L3 (left stick click)", "l3_btn", false, false},
      {"R3 (right stick click)", "r3_btn", false, false},
      // ── Triggers ──────────────────────────────────────────────────────
      {"L2 (left trigger \xe2\x80\x94 press fully)", "l2_axis", true, false},
      {"R2 (right trigger \xe2\x80\x94 press fully)", "r2_axis", true, false},
      // ── Stick axes: ask for a specific direction to detect sign ───────
      {"Left Stick \xe2\x86\x92  RIGHT (then return to centre)", "l_x", true,
       true},
      {"Left Stick \xe2\x86\x93  DOWN  (then return to centre)", "l_y", true,
       true},
      {"Right Stick \xe2\x86\x92 RIGHT (then return to centre)", "r_x", true,
       true},
      {"Right Stick \xe2\x86\x93 DOWN  (then return to centre)", "r_y", true,
       true},
  };

  SDL_JoystickID joyId = -1;
  SDL_Joystick *joy = SDL_GameControllerGetJoystick(ctrl_);
  if (joy)
    joyId = SDL_JoystickInstanceID(joy);

  // Snapshot axis centres for deflection detection
  std::array<Sint16, 32> axisCenter = {};
  if (joy) {
    int n = std::min(SDL_JoystickNumAxes(joy), 32);
    for (int i = 0; i < n; ++i)
      axisCenter[i] = SDL_JoystickGetAxis(joy, i);
  }

  int idx = 0;
  bool done = false;

  const std::string title = "RetroArch Player " + std::to_string(player) +
                            " Setup   \xe2\x80\x94   ESC to cancel   "
                            "\xe2\x80\x94   Backspace = redo last";

  auto drawRebind = [&]() {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);

    renderCenteredText(title, 0, 6, kW, {140, 140, 200, 255}, fontMd_);

    // Left column: already-mapped
    int listY = 32;
    for (int i = 0; i < idx && i < (int)actions.size(); ++i) {
      std::string txt = std::string(actions[i].label) + "  \xe2\x86\x92  ";
      if (actions[i].isAxis)
        txt += std::string("axis ") + (actions[i].positive ? "+" : "-") +
               std::to_string(actions[i].rawIdx);
      else
        txt += "btn " + std::to_string(actions[i].rawIdx);
      renderText(txt, 30, listY, {80, 200, 80, 255}, fontSm_, kW / 2 - 50);
      listY += 20;
    }

    // Right half: current prompt
    if (idx < (int)actions.size()) {
      int py = kH / 2 - 50;
      renderCenteredText(actions[idx].isAxis ? "Push:" : "Press:", kW / 2, py,
                         kW / 2, {160, 160, 200, 255}, fontMd_);
      renderCenteredText(actions[idx].label, kW / 2, py + 26, kW / 2,
                         {255, 220, 60, 255}, fontLg_);
      std::string progress =
          std::to_string(idx + 1) + " / " + std::to_string(actions.size());
      renderCenteredText(progress, kW / 2, py + 70, kW / 2,
                         {110, 110, 150, 255}, fontMd_);
    }

    SDL_RenderPresent(renderer_);
  };

  while (!done && idx < (int)actions.size()) {
    drawRebind();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        done = true;
        break;
      }
      if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_ESCAPE) {
          done = true;
          break;
        }
        if (ev.key.keysym.sym == SDLK_BACKSPACE && idx > 0) {
          --idx;
          break;
        }
      }

      auto &act = actions[idx];
      if (!act.isAxis && ev.type == SDL_JOYBUTTONDOWN &&
          ev.jbutton.which == joyId) {
        act.rawIdx = ev.jbutton.button;
        ++idx;
      } else if (act.isAxis && ev.type == SDL_JOYAXISMOTION &&
                 ev.jaxis.which == joyId) {
        int ai = std::min((int)ev.jaxis.axis, 31);
        // Use int arithmetic to avoid Sint16 overflow when a trigger
        // goes from its idle value (−32768) to fully pressed (+32767).
        int delta = (int)ev.jaxis.value - (int)axisCenter[ai];
        if (std::abs(delta) > 14000) {
          act.rawIdx = ev.jaxis.axis;
          act.positive = (ev.jaxis.value > axisCenter[ai]);
          ++idx;

          // ── Wait for axis to return to centre ─────────────────
          // Drain events until the captured axis is back near idle,
          // so the deflection doesn't bleed into the next prompt.
          {
            Uint32 deadline = SDL_GetTicks() + 1500;
            SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
            SDL_RenderClear(renderer_);
            renderCenteredText("Release...", 0, kH / 2 - 14, kW,
                               {160, 160, 200, 255}, fontMd_);
            SDL_RenderPresent(renderer_);

            bool released = false;
            while (!released && SDL_GetTicks() < deadline) {
              SDL_Event re;
              while (SDL_PollEvent(&re)) {
                if (re.type == SDL_JOYAXISMOTION && re.jaxis.which == joyId &&
                    re.jaxis.axis == act.rawIdx) {
                  int cur = (int)re.jaxis.value - (int)axisCenter[ai];
                  if (std::abs(cur) < 6000)
                    released = true;
                }
              }
              SDL_Delay(16);
            }
            // Extra 150 ms gap before showing the next prompt
            SDL_Delay(150);
          }
        }
      }
    }
    SDL_Delay(16);
  }
  if (done) {
    cleanup();
    return;
  }

  // ── Step 4: build RetroArch key/value pairs ──────────────────────────────
  const std::string pfx = "input_player" + std::to_string(player) + "_";
  std::vector<std::pair<std::string, std::string>> kv;
  kv.emplace_back(pfx + "joypad_index",
                  "\"" + std::to_string(player - 1) + "\"");
  // Buttons (same layout as the player 1 block)
  for (const auto &act : actions) {
    if (act.isAxis || act.rawIdx < 0)
      continue;
    kv.emplace_back(pfx + act.raKey, "\"" + std::to_string(act.rawIdx) + "\"");
  }
  // D-pad always comes from the HAT (ABS_HAT0X/Y set by uinput_client.py)
  kv.emplace_back(pfx + "up_btn", "\"h0up\"");
  kv.emplace_back(pfx + "down_btn", "\"h0down\"");
  kv.emplace_back(pfx + "left_btn", "\"h0left\"");
  kv.emplace_back(pfx + "right_btn", "\"h0right\"");
  // Axes
  for (const auto &act : actions) {
    if (!act.isAxis || act.rawIdx < 0)
      continue;
    const std::string ai = std::to_string(act.rawIdx);
    if (act.isStick) {
      kv.emplace_back(pfx + act.raKey + "_plus_axis",
                      "\"" + std::string(act.positive ? "+" : "-") + ai + "\"");
      kv.emplace_back(pfx + act.raKey + "_minus_axis",
                      "\"" + std::string(act.positive ? "-" : "+") + ai + "\"");
    } else {
      kv.emplace_back(pfx + act.raKey,
                      "\"" + std::string(act.positive ? "+" : "-") + ai + "\"");
    }
  }

  // ── Step 5: merge into retroarch_controller.cfg ──────────────────────────
  const fs::path cfgPath = "retroarch_controller.cfg";
  std::vector<std::string> kept;
  {
    std::ifstream f(cfgPath);
    std::string line;
    while (std::getline(f, line))
      if (line.rfind(pfx, 0) != 0) // drop stale player-N keys
        kept.push_back(line);
  }
  while (!kept.empty() && kept.back().empty())
    kept.pop_back();
  {
    std::ofstream f(cfgPath, std::ios::trunc);
    for (const auto &l : kept)
      f << l << "\n";
    if (!kept.empty())
      f << "\n";
    for (const auto &[k, v] : kv)
      f << k << " = " << v << "\n";
  }

  showMsg("Saved player " + std::to_string(player) +
              " bindings to retroarch_controller.cfg",
          {80, 230, 80, 255});
  SDL_Delay(1500);
  cleanup();
}

// ── loadConsoleBindings (static, called at startup)
// ───────────────────────────

void GameShell::loadConsoleBindings() {
  if (!std::filesystem::exists(kConsoleBindFile))
    return;

  auto consoles = makeConsoleTable();
  for (const auto &con : consoles) {
    auto vals = loadConsoleBind(con.name, con.btns);
    // Only apply if the section exists in the file (loadConsoleBind returns
    // defaults if the section is absent; skip those to avoid clobbering the
    // emulator's own defaults with stale data).
    bool hasSection = false;
    {
      std::ifstream f(kConsoleBindFile);
      std::string line;
      while (std::getline(f, line))
        if (line == std::string("[") + con.name + "]") {
          hasSection = true;
          break;
        }
    }
    if (hasSection)
      applyFlat(con.globalBindPtr, con.globalConfigured, vals);
  }
}
