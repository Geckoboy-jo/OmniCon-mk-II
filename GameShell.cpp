#include "GameShell.hpp"
#include "DownloadManager.hpp"
#include "NES.hpp"
#include "SNES.hpp"
#include "GameBoy.hpp"
#include "GameBoyColor.hpp"
#include "GBA.hpp"
#include "NDS.hpp"
#include "3DS.hpp"
#include "Atari2600.hpp"
#include "Atari5200.hpp"
#include "Atari800.hpp"
#include "SegaGenesis.hpp"
#include "N64.hpp"
#include "PS1.hpp"
#include "PS2.hpp"
#include "GameCube.hpp"
#include "Odyssey.hpp"
#include "SaveManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Static members ───────────────────────────────────────────────────────────

const SystemType GameShell::kSystems[17] = {
    SystemType::NES,
    SystemType::SNES,
    SystemType::GameBoy,
    SystemType::GameBoyColor,
    SystemType::GBA,
    SystemType::NDS,
    SystemType::ThreeDS,
    SystemType::Atari2600,
    SystemType::Atari5200,
    SystemType::Atari800,
    SystemType::SegaGenesis,
    SystemType::N64,
    SystemType::PS1,
    SystemType::PS2,
    SystemType::GameCube,
    SystemType::Odyssey,
    SystemType::Steam,
};

const char* const GameShell::kSysAbbrev[17] = {
    "NES", "SNES", "GB", "GBC", "GBA", "NDS", "3DS", "2600", "5200", "800", "GEN", "N64", "PS1", "PS2", "GCN", "ODY", "STM"
};

// ─── Colour palette ───────────────────────────────────────────────────────────

static const SDL_Color kColorBg        = { 10,  10,  22, 255};
static const SDL_Color kColorHeader    = {  8,   8,  18, 255};
static const SDL_Color kColorInfo      = {  6,   6,  14, 255};
static const SDL_Color kColorBoxNormal = { 18,  18,  38, 255};
static const SDL_Color kColorBoxHover  = { 28,  28,  55, 255};
static const SDL_Color kColorBoxSel    = { 12,  38,  80, 255};
static const SDL_Color kColorBorderSel = { 60, 150, 255, 255};
static const SDL_Color kColorBorderHov = { 45,  55, 100, 255};
static const SDL_Color kColorTextPri   = {220, 230, 255, 255};
static const SDL_Color kColorTextDim   = {100, 110, 155, 255};
static const SDL_Color kColorFiltAct   = { 50, 120, 210, 255};
static const SDL_Color kColorFiltInact = { 22,  22,  48, 255};

static SDL_Color sysBadgeColor(SystemType sys) {
    switch (sys) {
        case SystemType::NES:          return {170,  35,  35, 255};
        case SystemType::SNES:         return { 75,  35, 155, 255};
        case SystemType::GameBoy:      return { 35, 130,  35, 255};
        case SystemType::GameBoyColor: return { 35, 170,  90, 255};
        case SystemType::GBA:          return {155,  90,  35, 255};
        case SystemType::NDS:          return { 35, 110, 190, 255};
        case SystemType::ThreeDS:      return {200,  60, 120, 255};
        case SystemType::Atari2600:    return {190, 140,  35, 255};
        case SystemType::Atari5200:    return {190, 100,  50, 255};
        case SystemType::Atari800:     return {140, 170,  50, 255};
        case SystemType::SegaGenesis:  return { 20,  80, 180, 255};   // Sega blue
        case SystemType::N64:           return {140,  20,  20, 255};   // N64 red
        case SystemType::PS1:           return { 10,  50, 140, 255};   // PS1 navy blue
        case SystemType::PS2:           return {  0,  70, 170, 255};   // PS2 blue
        case SystemType::GameCube:      return { 88,  76, 168, 255};   // GameCube indigo
        case SystemType::Odyssey:       return { 45, 160, 160, 255};   // teal
        case SystemType::Steam:         return { 23, 134, 192, 255};   // Steam blue
        default:                       return { 70,  70,  70, 255};
    }
}

static int sysIndex(SystemType sys) {
    for (int i = 0; i < GameShell::kNumSystems; ++i)
        if (GameShell::kSystems[i] == sys) return i;
    return -1;
}

// ─── Font discovery ───────────────────────────────────────────────────────────

static const char* findFont() {
    static const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i)
        if (access(candidates[i], R_OK) == 0) return candidates[i];
    return nullptr;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

GameShell::~GameShell() { cleanup(); }

bool GameShell::init() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "[GameShell] SDL_InitSubSystem: " << SDL_GetError() << "\n";
        return false;
    }
    if (TTF_Init() != 0) {
        std::cerr << "[GameShell] TTF_Init: " << TTF_GetError() << "\n";
        return false;
    }
    if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG)) == 0) {
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
        dispBounds.w = kW; dispBounds.h = kH;
    }
    window_ = SDL_CreateWindow(
        "OmniCon \xe2\x80\x94 Game Library",
        dispBounds.x, dispBounds.y,
        dispBounds.w, dispBounds.h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
    if (!window_) {
        std::cerr << "[GameShell] SDL_CreateWindow: " << SDL_GetError() << "\n";
        IMG_Quit(); TTF_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::cerr << "[GameShell] SDL_CreateRenderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window_); window_ = nullptr;
        IMG_Quit(); TTF_Quit();
        return false;
    }

    // Flush any pending OS/compositor events (e.g. Wayland's async fullscreen
    // configure) so SDL_GetRendererOutputSize returns the actual desktop size
    // before we compute the logical scale.
    SDL_PumpEvents();
    // Scale all rendering to fill the screen while preserving the 1280×720 layout
    SDL_RenderSetLogicalSize(renderer_, kW, kH);

    const char* font = findFont();
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

    return true;
}

void GameShell::cleanup() {
    clearCoverCache();
    if (fontLg_)   { TTF_CloseFont(fontLg_);       fontLg_   = nullptr; }
    if (fontMd_)   { TTF_CloseFont(fontMd_);       fontMd_   = nullptr; }
    if (fontSm_)   { TTF_CloseFont(fontSm_);       fontSm_   = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_)   { SDL_DestroyWindow(window_);    window_   = nullptr; }
    IMG_Quit();
    TTF_Quit();
}

void GameShell::clearCoverCache() {
    for (auto& [key, tex] : coverCache_)
        if (tex) SDL_DestroyTexture(tex);
    coverCache_.clear();
}

// ─── Texture helpers ──────────────────────────────────────────────────────────

SDL_Texture* GameShell::loadTexture(const std::string& path) {
    if (path.empty() || !renderer_) return nullptr;
    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    return tex;
}

SDL_Texture* GameShell::getCoverTexture(const GameEntry& game) {
    if (game.coverPath.empty()) return nullptr;
    auto it = coverCache_.find(game.coverPath);
    if (it != coverCache_.end()) return it->second;
    SDL_Texture* tex = loadTexture(game.coverPath);
    coverCache_[game.coverPath] = tex;
    return tex;
}

// ─── Text rendering ───────────────────────────────────────────────────────────

void GameShell::renderText(const std::string& text, int x, int y,
                           SDL_Color col, TTF_Font* font, int maxW) {
    if (!font || !renderer_ || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        if (maxW > 0 && dst.w > maxW) dst.w = maxW;
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void GameShell::renderCenteredText(const std::string& text,
                                   int x, int y, int w, SDL_Color col, TTF_Font* font) {
    if (!font || !renderer_ || text.empty()) return;
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, text.c_str(), &tw, &th);
    (void)th;
    int rx = x + (w - std::min(tw, w)) / 2;
    renderText(text, rx, y, col, font, w);
}

// ─── Filter logic ─────────────────────────────────────────────────────────────

void GameShell::rebuildFilter() {
    filteredGames_.clear();
    const auto& dm = DownloadManager::instance();

    // Pre-compute lowercase search query once
    std::string queryLow = searchQuery_;
    std::transform(queryLow.begin(), queryLow.end(), queryLow.begin(), ::tolower);

    for (auto& ge : library_->games()) {
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
                    if (extSys != SystemType::Unknown) ge.system = extSys;
                }
                include = true;
            }
        } else {
            include = (ge.system == kSystems[currentFilter_]);
        }

        // Apply case-insensitive search query
        if (include && !queryLow.empty()) {
            std::string nameLow = ge.displayName;
            std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(), ::tolower);
            include = (nameLow.find(queryLow) != std::string::npos);
        }

        if (include)
            filteredGames_.push_back(&ge);
    }
    selectedIdx_ = 0;
    scrollRow_   = 0;
    hoverIdx_    = -1;
    clearCoverCache();
    std::cout << "[Shell] rebuildFilter: " << filteredGames_.size()
              << " games visible (filter=" << currentFilter_
              << ", total in library=" << library_->games().size() << ")\n";
}

void GameShell::ensureVisible() {
    if (filteredGames_.empty()) return;
    int row = selectedIdx_ / kCols;
    if (row < scrollRow_)
        scrollRow_ = row;
    else if (row >= scrollRow_ + 2)
        scrollRow_ = row - 1;
    if (scrollRow_ < 0) scrollRow_ = 0;
}

// ─── Filter button layout ─────────────────────────────────────────────────────

void GameShell::computeFilterRects() {
    // "All" (50px) + 14 system buttons (68px) + "DL'd" (50px) separated by 5px gaps
    const int btnH   = 30;
    const int allW   = 50;
    const int sysW   = 68;
    const int dlW    = 50;
    const int gapX   = 5;
    const int totalW = allW + gapX + kNumSystems * sysW + (kNumSystems - 1) * gapX + gapX + dlW;
    int       x      = (kW - totalW) / 2;
    const int btnY   = (kHeaderH - btnH) / 2;

    filterBtnRects_[0] = {x, btnY, allW, btnH};
    x += allW + gapX;
    for (int i = 0; i < kNumSystems; ++i) {
        filterBtnRects_[i + 1] = {x, btnY, sysW, btnH};
        x += sysW + gapX;
    }
    // Downloaded button — last slot
    filterBtnRects_[kNumSystems + 1] = {x, btnY, dlW, btnH};
}

// ─── Hit testing ──────────────────────────────────────────────────────────────

int GameShell::gameAtMouse(int mx, int my) const {
    if (my < kHeaderH || my >= kH - kInfoH) return -1;
    int col    = (mx - kGridLeft) / kSlotW;
    if (col < 0 || col >= kCols) return -1;
    int visRow = (my - kHeaderH) / kSlotH;
    int idx    = (visRow + scrollRow_) * kCols + col;
    if (idx < 0 || idx >= static_cast<int>(filteredGames_.size())) return -1;
    return idx;
}

int GameShell::filterBtnAtMouse(int mx, int my) const {
    for (int i = 0; i < kNumSystems + 2; ++i) {
        const SDL_Rect& r = filterBtnRects_[i];
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            return i - 1;   // [0]→-1=All, [1..kNumSystems]→0..kNumSystems-1, [kNumSystems+1]→kNumSystems=Downloaded
    }
    return -2;   // no button hit
}

// ─── Rendering ────────────────────────────────────────────────────────────────

void GameShell::renderHeader() {
    SDL_Rect hdr = {0, 0, kW, kHeaderH};
    SDL_SetRenderDrawColor(renderer_, kColorHeader.r, kColorHeader.g, kColorHeader.b, 255);
    SDL_RenderFillRect(renderer_, &hdr);

    // Bottom separator
    SDL_SetRenderDrawColor(renderer_, 30, 30, 65, 255);
    SDL_RenderDrawLine(renderer_, 0, kHeaderH - 1, kW - 1, kHeaderH - 1);

    // Filter buttons — All + systems + Downloaded
    for (int i = 0; i < kNumSystems + 2; ++i) {
        int fi      = i - 1;   // -1=All, 0..kNumSystems-1=system, kNumSystems=Downloaded
        bool active = (fi == currentFilter_);
        const SDL_Rect& r = filterBtnRects_[i];

        SDL_Color bg;
        if (active && fi == kNumSystems)
            bg = {25, 110, 65, 255};   // green tint for Downloaded tab
        else
            bg = active ? kColorFiltAct : kColorFiltInact;
        SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
        SDL_RenderFillRect(renderer_, &r);

        if (active) {
            SDL_Color ul = (fi == kNumSystems)
                ? SDL_Color{70, 200, 120, 255}
                : SDL_Color{100, 180, 255, 255};
            SDL_SetRenderDrawColor(renderer_, ul.r, ul.g, ul.b, 255);
            SDL_Rect ulr = {r.x, r.y + r.h - 2, r.w, 2};
            SDL_RenderFillRect(renderer_, &ulr);
        }

        const char* label;
        if (i == 0)                 label = "All";
        else if (fi == kNumSystems) label = "DL'd";
        else                        label = kSysAbbrev[fi];
        SDL_Color tc = active ? SDL_Color{230, 240, 255, 255} : kColorTextDim;
        renderCenteredText(label, r.x, r.y + (r.h - 14) / 2, r.w, tc, fontMd_);
    }
}

void GameShell::renderGrid() {
    SDL_Rect gridArea = {0, kHeaderH, kW, kGridH};
    SDL_SetRenderDrawColor(renderer_, kColorBg.r, kColorBg.g, kColorBg.b, 255);
    SDL_RenderFillRect(renderer_, &gridArea);

    if (filteredGames_.empty()) {
        renderCenteredText("No games found", 0, kHeaderH + kGridH / 2 - 10,
                           kW, kColorTextDim, fontMd_);
        return;
    }

    SDL_RenderSetClipRect(renderer_, &gridArea);

    constexpr int padTop = 9;
    constexpr int gap1   = 5;    // cover → name gap
    constexpr int nameH  = 16;
    constexpr int gap2   = 3;    // name → badge gap
    constexpr int badgeH = 13;

    int n         = static_cast<int>(filteredGames_.size());
    int totalRows = (n + kCols - 1) / kCols;
    int visRows   = kGridH / kSlotH + 2;   // +2 so partial bottom row is rendered

    for (int row = scrollRow_; row < std::min(totalRows, scrollRow_ + visRows); ++row) {
        int sy = kHeaderH + (row - scrollRow_) * kSlotH;
        for (int col = 0; col < kCols; ++col) {
            int idx = row * kCols + col;
            if (idx >= n) break;

            const GameEntry& ge = *filteredGames_[idx];
            int sx = kGridLeft + col * kSlotW;

            bool selected = (idx == selectedIdx_);
            bool hovered  = (idx == hoverIdx_);

            // Slot background
            SDL_Color bg = selected ? kColorBoxSel
                         : hovered  ? kColorBoxHover
                         :            kColorBoxNormal;
            SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
            SDL_Rect slotR = {sx, sy, kSlotW, kSlotH};
            SDL_RenderFillRect(renderer_, &slotR);

            // Cover image
            int coverX = sx + (kSlotW - kCoverW) / 2;
            int coverY = sy + padTop;
            SDL_Rect coverR = {coverX, coverY, kCoverW, kCoverH};

            SDL_Texture* tex = getCoverTexture(ge);
            if (tex) {
                SDL_RenderCopy(renderer_, tex, nullptr, &coverR);
            } else {
                // Coloured placeholder
                SDL_Color pc = sysBadgeColor(ge.system);
                SDL_SetRenderDrawColor(renderer_, pc.r / 2, pc.g / 2, pc.b / 2, 255);
                SDL_RenderFillRect(renderer_, &coverR);
                int si = sysIndex(ge.system);
                if (si >= 0)
                    renderCenteredText(kSysAbbrev[si], coverX,
                                       coverY + kCoverH / 2 - 7,
                                       kCoverW, {200, 210, 240, 255}, fontMd_);
            }

            // Selection / hover border
            if (selected) {
                SDL_SetRenderDrawColor(renderer_,
                    kColorBorderSel.r, kColorBorderSel.g, kColorBorderSel.b, 255);
                SDL_Rect b1 = {coverX - 2, coverY - 2, kCoverW + 4, kCoverH + 4};
                SDL_Rect b2 = {coverX - 3, coverY - 3, kCoverW + 6, kCoverH + 6};
                SDL_RenderDrawRect(renderer_, &b1);
                SDL_RenderDrawRect(renderer_, &b2);
            } else if (hovered) {
                SDL_SetRenderDrawColor(renderer_,
                    kColorBorderHov.r, kColorBorderHov.g, kColorBorderHov.b, 255);
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
                renderText(kSysAbbrev[si], badgeX + 4, badgeY + 1,
                           {220, 225, 240, 255}, fontMd_);
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
    SDL_SetRenderDrawColor(renderer_, kColorInfo.r, kColorInfo.g, kColorInfo.b, 255);
    SDL_RenderFillRect(renderer_, &bar);

    SDL_SetRenderDrawColor(renderer_, 30, 30, 65, 255);
    SDL_RenderDrawLine(renderer_, 0, barY, kW - 1, barY);

    if (!filteredGames_.empty() &&
        selectedIdx_ < static_cast<int>(filteredGames_.size())) {
        const GameEntry& ge = *filteredGames_[selectedIdx_];
        renderText(ge.displayName, 20, barY + 7, kColorTextPri, fontLg_, kW / 2 - 30);
        std::string sysName = "\xe2\x80\xa2 " + GameLibrary::systemName(ge.system);
        renderText(sysName, kW / 2, barY + 11, kColorTextDim, fontMd_);
    }

    renderText(
        "\xe2\x86\x90\xe2\x86\x92\xe2\x86\x91\xe2\x86\x93 Navigate    "
        "Enter / Double-click = Play    Mouse Wheel = Scroll    "
        "Ctrl+F = Search    Esc = Exit",
        20, barY + kInfoH - 16, kColorTextDim, fontSm_);

    // Controller indicator (right-aligned)
    if (!ctrlName_.empty()) {
        std::string ctrlLabel = "\xf0\x9f\x8e\xae  " + ctrlName_;
        renderText(ctrlLabel, kW - 360, barY + 11, {80, 210, 120, 255}, fontMd_, 340);
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
    if (searchActive_) renderSearchBox();
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
    if (showCursor) display += "|";
    renderText(display, boxX + 10, boxY + (boxH - 14) / 2,
               {210, 225, 255, 255}, fontMd_, boxW - 20);

    // Result count (right-aligned inside box)
    std::string count = std::to_string(filteredGames_.size()) + " result"
                        + (filteredGames_.size() == 1 ? "" : "s");
    int tw = 0, th = 0;
    TTF_SizeUTF8(fontMd_, count.c_str(), &tw, &th);
    (void)th;
    renderText(count, boxX + boxW - tw - 10, boxY + (boxH - 14) / 2,
               kColorTextDim, fontMd_);
}

// ─── Main loop ────────────────────────────────────────────────────────────────

void GameShell::run(GameLibrary& library, const std::string& coversDir) {
    library_       = &library;
    coversDir_     = coversDir;
    currentFilter_ = -1;
    rebuildFilter();
    computeFilterRects();

    // Initialize save DB (saves/saves.db)
    try {
        const std::string saveDb = (fs::current_path() / "saves" / "saves.db").string();
        SaveManager::instance().init(saveDb);
    } catch (...) {
        // ignore init failures; save operations will be best-effort
    }

    ctrl_ = nullptr;
    ctrlName_.clear();
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            ctrl_ = SDL_GameControllerOpen(i);
            if (ctrl_) ctrlName_ = SDL_GameControllerName(ctrl_);
            break;
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
    auto toLogical = [](int wx, int wy, int* lx, int* ly) {
        *lx = wx;
        *ly = wy;
    };

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        bool      pendingLaunch = false;
        GameEntry launchTarget;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { quit = true; break; }

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
                    case SDLK_ESCAPE:
                        if (searchActive_) {
                            if (!searchQuery_.empty()) {
                                searchQuery_.clear();
                            } else {
                                searchActive_ = false;
                                SDL_StopTextInput();
                            }
                            rebuildFilter(); computeFilterRects();
                        } else {
                            quit = true;
                        }
                        break;
                    case SDLK_BACKSPACE:
                        if (searchActive_ && !searchQuery_.empty()) {
                            // Remove last UTF-8 code point
                            while (searchQuery_.size() > 1 &&
                                   (static_cast<unsigned char>(searchQuery_.back()) & 0xC0) == 0x80)
                                searchQuery_.pop_back();
                            searchQuery_.pop_back();
                            rebuildFilter(); computeFilterRects();
                        }
                        break;
                    case SDLK_f:
                        if (ev.key.keysym.mod & KMOD_CTRL) {
                            searchActive_ = true;
                            SDL_StartTextInput();
                        }
                        break;
                    case SDLK_LEFT:
                        if (selectedIdx_ > 0) { --selectedIdx_; ensureVisible(); }
                        break;
                    case SDLK_RIGHT:
                        if (selectedIdx_ < n - 1) { ++selectedIdx_; ensureVisible(); }
                        break;
                    case SDLK_UP:
                        if (selectedIdx_ >= kCols) { selectedIdx_ -= kCols; ensureVisible(); }
                        break;
                    case SDLK_DOWN:
                        if (selectedIdx_ + kCols < n) { selectedIdx_ += kCols; ensureVisible(); }
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
                        if (!filteredGames_.empty()) {
                            launchTarget  = *filteredGames_[selectedIdx_];
                            pendingLaunch = true;
                        }
                        break;
                    // 0 = All, 1-9 = system filter (disabled while search is active)
                    case SDLK_0:
                        if (!searchActive_) { currentFilter_ = -1; rebuildFilter(); computeFilterRects(); }
                        break;
                    case SDLK_1: case SDLK_2: case SDLK_3:
                    case SDLK_4: case SDLK_5: case SDLK_6:
                    case SDLK_7: case SDLK_8: case SDLK_9: {
                        if (!searchActive_) {
                            int fi = ev.key.keysym.sym - SDLK_1;
                            if (fi < kNumSystems) {
                                currentFilter_ = fi;
                                rebuildFilter(); computeFilterRects();
                            }
                        }
                        break;
                    }
                    default: break;
                }
            }

            // ── Mouse motion ─────────────────────────────────────────────────
            if (ev.type == SDL_MOUSEMOTION) {
                int lx, ly;
                toLogical(ev.motion.x, ev.motion.y, &lx, &ly);
                hoverIdx_ = gameAtMouse(lx, ly);
            }

            // ── Text input (search) ───────────────────────────────────────────
            if (ev.type == SDL_TEXTINPUT && searchActive_) {
                searchQuery_ += ev.text.text;
                rebuildFilter();
                computeFilterRects();
            }

            // ── Mouse button ─────────────────────────────────────────────────
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int mx, my;
                toLogical(ev.button.x, ev.button.y, &mx, &my);
                // DEBUG: print raw and logical coords so we can tune the transform
                {
                    int ww, wh, ow, oh;
                    SDL_GetWindowSize(window_, &ww, &wh);
                    SDL_GetRendererOutputSize(renderer_, &ow, &oh);
                    std::cerr << "[Click] raw=(" << ev.button.x << "," << ev.button.y
                              << ") logical=(" << mx << "," << my
                              << ") winSize=" << ww << "x" << wh
                              << " outSize=" << ow << "x" << oh
                              << " kW=" << kW << " kH=" << kH
                              << " gameAtMouse=" << gameAtMouse(mx, my) << "\n";
                }
                int fi = filterBtnAtMouse(mx, my);
                if (fi >= -1) {
                    // Filter button clicked
                    currentFilter_ = fi;
                    rebuildFilter();
                    computeFilterRects();
                } else {
                    // Game grid clicked
                    int gi = gameAtMouse(mx, my);
                    if (gi >= 0) {
                        Uint32 now = SDL_GetTicks();
                        if (gi == lastClickIdx_ && now - lastClickMs_ < 400) {
                            // Double-click → launch
                            launchTarget  = *filteredGames_[gi];
                            pendingLaunch = true;
                            lastClickIdx_ = -1;
                        } else {
                            selectedIdx_  = gi;
                            lastClickIdx_ = gi;
                            lastClickMs_  = now;
                        }
                    }
                }
            }

            // ── Mouse wheel ──────────────────────────────────────────────────
            if (ev.type == SDL_MOUSEWHEEL) {
                int totalRows = (static_cast<int>(filteredGames_.size()) + kCols - 1) / kCols;
                scrollRow_   -= ev.wheel.y;
                int maxRow    = std::max(0, totalRows - 2);
                scrollRow_    = std::max(0, std::min(scrollRow_, maxRow));
            }

            // ── Controller ───────────────────────────────────────────────────
            if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                int n = static_cast<int>(filteredGames_.size());
                switch (ev.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_B:
                        quit = true; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        if (selectedIdx_ > 0) { --selectedIdx_; ensureVisible(); }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        if (selectedIdx_ < n - 1) { ++selectedIdx_; ensureVisible(); }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (selectedIdx_ >= kCols) { selectedIdx_ -= kCols; ensureVisible(); }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (selectedIdx_ + kCols < n) { selectedIdx_ += kCols; ensureVisible(); }
                        break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: {
                        // Cycle filter backwards (All → systems → Downloaded → All)
                        int idx = (currentFilter_ + 1 + kNumSystems + 1) % (kNumSystems + 2);
                        currentFilter_ = idx - 1;
                        rebuildFilter(); computeFilterRects();
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
                        // Cycle filter forwards (All → systems → Downloaded → All)
                        int idx = (currentFilter_ + 2) % (kNumSystems + 2);
                        currentFilter_ = idx - 1;
                        rebuildFilter(); computeFilterRects();
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_A:
                        if (!filteredGames_.empty()) {
                            launchTarget  = *filteredGames_[selectedIdx_];
                            pendingLaunch = true;
                        }
                        break;
                    default: break;
                }
            }

            if (ev.type == SDL_CONTROLLERDEVICEADDED && !ctrl_) {
                ctrl_ = SDL_GameControllerOpen(ev.cdevice.which);
                if (ctrl_) ctrlName_ = SDL_GameControllerName(ctrl_);
            }
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && ctrl_ &&
                SDL_GameControllerGetJoystick(ctrl_) ==
                SDL_JoystickFromInstanceID(ev.cdevice.which)) {
                SDL_GameControllerClose(ctrl_);
                ctrl_ = nullptr;
                ctrlName_.clear();
            }
        }

        if (pendingLaunch) {
            // If the ROM is not local but we have a download URL, fetch it first.
            if (launchTarget.system != SystemType::Steam &&
                !launchTarget.downloadUrl.empty() &&
                (launchTarget.romPath.empty() || !std::filesystem::exists(launchTarget.romPath))) {

                // Show "Downloading..." overlay on the existing window
                auto renderOverlay = [&](const std::string& msg, long long cur, long long tot) {
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
                        int fill = static_cast<int>((double)cur / (double)tot * barW);
                        SDL_SetRenderDrawColor(renderer_, 80, 140, 240, 255);
                        SDL_Rect fg = {barX, barY, fill, barH};
                        SDL_RenderFillRect(renderer_, &fg);
                    }

                    // Label
                    std::string label = msg;
                    if (tot > 0 && cur > 0) {
                        int pct = static_cast<int>(100.0 * cur / tot);
                        label += "  " + std::to_string(pct) + "%";
                    }
                    renderText(label, (kW - barW) / 2, kH / 2 - 14, {220, 220, 255, 255}, fontLg_, barW);

                    SDL_RenderPresent(renderer_);
                };

                // Pump events so the window stays responsive during download
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) { if (ev.type == SDL_QUIT) goto done; }

                renderOverlay("Downloading " + launchTarget.displayName + "...", 0, 0);

                QueuedDownload qd;
                qd.id        = launchTarget.dbId;
                qd.name      = launchTarget.displayName;
                qd.url       = launchTarget.downloadUrl;
                qd.system    = GameLibrary::systemName(launchTarget.system);

                std::string dlError;
                std::string resolvedUrl;
                std::string localRom = DownloadManager::instance().downloadBlocking(
                    qd,
                    [&](long long cur, long long tot) {
                        renderOverlay("Downloading " + launchTarget.displayName + "...", cur, tot);
                        while (SDL_PollEvent(&ev)) {}
                    },
                    &dlError,
                    &resolvedUrl
                );

                // If fallback resolved a better URL, persist it to DB so future
                // downloads skip the search entirely.
                if (!localRom.empty() && !resolvedUrl.empty()
                        && resolvedUrl != launchTarget.downloadUrl
                        && launchTarget.dbId > 0) {
                    updateGameDownloadUrl(dbCfg_, launchTarget.dbId, resolvedUrl);
                    launchTarget.downloadUrl = resolvedUrl;
                }

                if (localRom.empty()) {
                    // Show the error but keep the game in the library so the
                    // user can retry later once the URL is fixed or connectivity
                    // is restored.
                    std::string msg = dlError.find("404") != std::string::npos
                        ? "File not found (404). Check the game's download URL."
                        : "Download failed. Check connection and try again.";
                    renderText(msg, (kW - 500) / 2, kH / 2 + 60,
                               {240, 80, 80, 255}, fontLg_, 500);
                    SDL_RenderPresent(renderer_);
                    SDL_Delay(2000);
                    pendingLaunch = false;
                    goto done;
                }
                launchTarget.romPath = localRom;
                // Re-detect system from the extracted file's extension if still Unknown.
                if (launchTarget.system == SystemType::Unknown) {
                    std::string ext = fs::path(localRom).extension().string();
                    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    launchTarget.system = GameLibrary::systemFromExtension(ext);
                }
            }

            if (launchTarget.system == SystemType::Steam) {
                // Steam games launch externally; fire and keep the shell open
                launchGame(launchTarget);
            } else {
                if (ctrl_) { SDL_GameControllerClose(ctrl_); ctrl_ = nullptr; ctrlName_.clear(); }
                cleanup();
                launchGame(launchTarget);
                if (!init()) break;
                // Re-apply logical size after renderer recreation for the same
                // reason as at the top of run() (async fullscreen on Wayland).
                SDL_RenderSetLogicalSize(renderer_, kW, kH);
                rebuildFilter();
                computeFilterRects();
                for (int i = 0; i < SDL_NumJoysticks(); ++i) {
                    if (SDL_IsGameController(i)) {
                        ctrl_ = SDL_GameControllerOpen(i);
                        if (ctrl_) ctrlName_ = SDL_GameControllerName(ctrl_);
                        break;
                    }
                }
            }
            done:;
        }

        if (!quit) render();
    }

    if (ctrl_) { SDL_GameControllerClose(ctrl_); ctrl_ = nullptr; ctrlName_.clear(); }
}

// ─── Game launch ──────────────────────────────────────────────────────────────

void GameShell::launchGame(const GameEntry& game) {
    const std::string& path = game.romPath;
    // Defensive: if system is Unknown, try to detect from the file extension.
    SystemType sys = game.system;
    if (sys == SystemType::Unknown && !path.empty()) {
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        sys = GameLibrary::systemFromExtension(ext);
    }
    switch (sys) {
        case SystemType::NES: {
            NESEmulation nes;
            if (nes.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) nes.restoreSaveData(data);
                nes.runInteractive();
                SaveManager::instance().put(path, nes.dumpSaveData());
            }
            break;
        }
        case SystemType::SNES: {
            SNESEmulation snes;
            if (snes.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) snes.restoreSaveData(data);
                snes.runInteractive();
                SaveManager::instance().put(path, snes.dumpSaveData());
            }
            break;
        }
        case SystemType::GameBoy: {
            GameBoyEmulation gb;
            if (gb.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) gb.restoreSaveData(data);
                gb.runInteractive();
                SaveManager::instance().put(path, gb.dumpSaveData());
            }
            break;
        }
        case SystemType::GameBoyColor: {
            GameBoyColorEmulation gbc;
            if (gbc.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) gbc.restoreSaveData(data);
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
                    if (SaveManager::instance().get(path, data)) gba.restoreSaveData(data);
                    gba.runInteractive();
                    SaveManager::instance().put(path, gba.dumpSaveData());
                }
            }
            catch (const std::exception& e) { std::cerr << "GBA error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::NDS: {
            NDSEmulation nds;
            try {
                if (nds.loadRom(path)) {
                    std::vector<unsigned char> data;
                    if (SaveManager::instance().get(path, data)) nds.restoreSaveData(data);
                    nds.runInteractive();
                    SaveManager::instance().put(path, nds.dumpSaveData());
                }
            }
            catch (const std::exception& e) { std::cerr << "NDS error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::ThreeDS: {
            N3DSEmulation n3ds;
            try {
                if (n3ds.loadRom(path)) {
                    std::vector<unsigned char> data;
                    if (SaveManager::instance().get(path, data)) n3ds.restoreSaveData(data);
                    n3ds.runInteractive();
                    SaveManager::instance().put(path, n3ds.dumpSaveData());
                }
            }
            catch (const std::exception& e) { std::cerr << "3DS error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::Atari2600: {
            Atari2600Emulation a;
            if (a.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) a.restoreSaveData(data);
                a.runInteractive();
                SaveManager::instance().put(path, a.dumpSaveData());
            }
            break;
        }
        case SystemType::Atari5200: {
            Atari5200Emulation a;
            if (a.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) a.restoreSaveData(data);
                a.runInteractive();
                SaveManager::instance().put(path, a.dumpSaveData());
            }
            break;
        }
        case SystemType::Atari800: {
            Atari800Emulation a;
            if (a.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) a.restoreSaveData(data);
                a.runInteractive();
                SaveManager::instance().put(path, a.dumpSaveData());
            }
            break;
        }
        case SystemType::SegaGenesis: {
            SegaGenesisEmulation gen;
            if (gen.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) gen.restoreSaveData(data);
                gen.runInteractive();
                SaveManager::instance().put(path, gen.dumpSaveData());
            }
            break;
        }
        case SystemType::N64: {
            N64Emulation n64;
            if (n64.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) n64.restoreSaveData(data);
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
                    if (SaveManager::instance().get(path, data)) ps1.restoreSaveData(data);
                    ps1.runInteractive();
                    SaveManager::instance().put(path, ps1.dumpSaveData());
                }
            }
            catch (const std::exception& e) { std::cerr << "PS1 error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::PS2: {
            PS2Emulation ps2;
            try {
                if (ps2.loadRom(path)) {
                    std::vector<unsigned char> data;
                    if (SaveManager::instance().get(path, data)) ps2.restoreSaveData(data);
                    ps2.runInteractive();
                    SaveManager::instance().put(path, ps2.dumpSaveData());
                }
            }
            catch (const std::exception& e) { std::cerr << "PS2 error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::GameCube: {
            GameCubeEmulation gc;
            try {
                if (gc.loadRom(path)) {
                    std::vector<unsigned char> data;
                    if (SaveManager::instance().get(path, data)) gc.restoreSaveData(data);
                    gc.runInteractive();
                    SaveManager::instance().put(path, gc.dumpSaveData());
                }
            }
            catch (const std::exception& e) { std::cerr << "GameCube error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::Odyssey: {
            OdysseyEmulation ody;
            if (ody.loadRom(path)) {
                std::vector<unsigned char> data;
                if (SaveManager::instance().get(path, data)) ody.restoreSaveData(data);
                ody.runInteractive();
                SaveManager::instance().put(path, ody.dumpSaveData());
            }
            break;
        }
        case SystemType::Steam: {
            // game.romPath holds the numeric Steam AppID (validated all-digits at scan time)
            const std::string url = "steam://rungameid/" + path;

            // Detect WSL2: if so, Steam lives on Windows and cmd.exe is the right launcher
            bool inWSL2 = false;
            {
                std::ifstream pv("/proc/version");
                std::string line;
                if (std::getline(pv, line))
                    inWSL2 = line.find("Microsoft") != std::string::npos ||
                             line.find("microsoft") != std::string::npos;
            }

            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                if (inWSL2) {
                    // cmd.exe /c start "" <url>  — the "" is the window-title arg
                    // required when the first arg after start begins with a quote
                    const char* argv[] = { "cmd.exe", "/c", "start", "", url.c_str(), nullptr };
                    execvp(argv[0], const_cast<char* const*>(argv));
                } else {
                    const char* argv[] = { "xdg-open", url.c_str(), nullptr };
                    execvp(argv[0], const_cast<char* const*>(argv));
                }
                _exit(1);
            }
            // Parent returns immediately; Steam launches asynchronously
            break;
        }
        default:
            std::cerr << "[GameShell] Unknown system for ROM: " << path << "\n";
            break;
    }
}


