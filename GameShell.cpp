#include "GameShell.hpp"
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
#include "Odyssey.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Static members ───────────────────────────────────────────────────────────

const SystemType GameShell::kSystems[13] = {
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
    SystemType::Odyssey,
    SystemType::Steam,
};

const char* const GameShell::kSysAbbrev[13] = {
    "NES", "SNES", "GB", "GBC", "GBA", "NDS", "3DS", "2600", "5200", "800", "GEN", "ODY", "STM"
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

    window_ = SDL_CreateWindow(
        "OmniCon \xe2\x80\x94 Game Library",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kW, kH,
        SDL_WINDOW_SHOWN);
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
    auto it = coverCache_.find(game.romPath);
    if (it != coverCache_.end()) return it->second;
    SDL_Texture* tex = game.coverPath.empty() ? nullptr : loadTexture(game.coverPath);
    coverCache_[game.romPath] = tex;
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
    for (const auto& ge : library_->games()) {
        if (currentFilter_ < 0 || ge.system == kSystems[currentFilter_])
            filteredGames_.push_back(&ge);
    }
    selectedIdx_ = 0;
    scrollRow_   = 0;
    hoverIdx_    = -1;
    clearCoverCache();
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
    // "All" (50px) + 9 system buttons (68px) separated by 5px gaps
    const int btnH   = 30;
    const int allW   = 50;
    const int sysW   = 68;
    const int gapX   = 5;
    const int totalW = allW + gapX + kNumSystems * sysW + (kNumSystems - 1) * gapX;
    int       x      = (kW - totalW) / 2;
    const int btnY   = (kHeaderH - btnH) / 2;

    filterBtnRects_[0] = {x, btnY, allW, btnH};
    x += allW + gapX;
    for (int i = 0; i < kNumSystems; ++i) {
        filterBtnRects_[i + 1] = {x, btnY, sysW, btnH};
        x += sysW + gapX;
    }
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
    for (int i = 0; i < kNumSystems + 1; ++i) {
        const SDL_Rect& r = filterBtnRects_[i];
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            return i - 1;   // [0]→-1=All, [1]→0=NES, etc.
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

    // Filter buttons
    for (int i = 0; i < kNumSystems + 1; ++i) {
        int fi      = i - 1;   // -1=All, 0-8=system
        bool active = (fi == currentFilter_);
        const SDL_Rect& r = filterBtnRects_[i];

        SDL_Color bg = active ? kColorFiltAct : kColorFiltInact;
        SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
        SDL_RenderFillRect(renderer_, &r);

        if (active) {
            SDL_SetRenderDrawColor(renderer_, 100, 180, 255, 255);
            SDL_Rect ul = {r.x, r.y + r.h - 2, r.w, 2};
            SDL_RenderFillRect(renderer_, &ul);
        }

        const char* label = (i == 0) ? "All" : kSysAbbrev[fi];
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
        "Enter / Double-click = Play    Mouse Wheel = Scroll    Esc = Exit",
        20, barY + kInfoH - 16, kColorTextDim, fontSm_);
}

void GameShell::render() {
    SDL_SetRenderDrawColor(renderer_, kColorBg.r, kColorBg.g, kColorBg.b, 255);
    SDL_RenderClear(renderer_);
    renderHeader();
    renderGrid();
    renderInfoBar();
    SDL_RenderPresent(renderer_);
}

// ─── Main loop ────────────────────────────────────────────────────────────────

void GameShell::run(GameLibrary& library, const std::string& coversDir) {
    library_       = &library;
    coversDir_     = coversDir;
    currentFilter_ = -1;
    rebuildFilter();
    computeFilterRects();

    SDL_GameController* ctrl = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { ctrl = SDL_GameControllerOpen(i); break; }

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        bool      pendingLaunch = false;
        GameEntry launchTarget;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { quit = true; break; }

            // ── Keyboard ─────────────────────────────────────────────────────
            if (ev.type == SDL_KEYDOWN) {
                int n = static_cast<int>(filteredGames_.size());
                switch (ev.key.keysym.sym) {
                    case SDLK_ESCAPE: quit = true; break;
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
                    // 0 = All, 1-9 = system filter
                    case SDLK_0:
                        currentFilter_ = -1; rebuildFilter(); computeFilterRects(); break;
                    case SDLK_1: case SDLK_2: case SDLK_3:
                    case SDLK_4: case SDLK_5: case SDLK_6:
                    case SDLK_7: case SDLK_8: case SDLK_9: {
                        int fi = ev.key.keysym.sym - SDLK_1;
                        if (fi < kNumSystems) {
                            currentFilter_ = fi;
                            rebuildFilter(); computeFilterRects();
                        }
                        break;
                    }
                    default: break;
                }
            }

            // ── Mouse motion ─────────────────────────────────────────────────
            if (ev.type == SDL_MOUSEMOTION)
                hoverIdx_ = gameAtMouse(ev.motion.x, ev.motion.y);

            // ── Mouse button ─────────────────────────────────────────────────
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int mx = ev.button.x, my = ev.button.y;
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
                        // Cycle filter backwards
                        int idx = (currentFilter_ + 1 + kNumSystems) % (kNumSystems + 1);
                        currentFilter_ = idx - 1;
                        rebuildFilter(); computeFilterRects();
                        break;
                    }
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
                        // Cycle filter forwards
                        int idx = (currentFilter_ + 2) % (kNumSystems + 1);
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

            if (ev.type == SDL_CONTROLLERDEVICEADDED && !ctrl)
                ctrl = SDL_GameControllerOpen(ev.cdevice.which);
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && ctrl &&
                SDL_GameControllerGetJoystick(ctrl) ==
                SDL_JoystickFromInstanceID(ev.cdevice.which)) {
                SDL_GameControllerClose(ctrl);
                ctrl = nullptr;
            }
        }

        if (pendingLaunch) {
            if (launchTarget.system == SystemType::Steam) {
                // Steam games launch externally; fire and keep the shell open
                launchGame(launchTarget);
            } else {
                if (ctrl) { SDL_GameControllerClose(ctrl); ctrl = nullptr; }
                cleanup();
                launchGame(launchTarget);
                if (!init()) break;
                rebuildFilter();
                computeFilterRects();
                for (int i = 0; i < SDL_NumJoysticks(); ++i)
                    if (SDL_IsGameController(i)) { ctrl = SDL_GameControllerOpen(i); break; }
            }
        }

        if (!quit) render();
    }

    if (ctrl) SDL_GameControllerClose(ctrl);
}

// ─── Game launch ──────────────────────────────────────────────────────────────

void GameShell::launchGame(const GameEntry& game) {
    const std::string& path = game.romPath;
    switch (game.system) {
        case SystemType::NES: {
            NESEmulation nes;
            if (nes.loadRom(path)) nes.runInteractive();
            break;
        }
        case SystemType::SNES: {
            SNESEmulation snes;
            if (snes.loadRom(path)) snes.runInteractive();
            break;
        }
        case SystemType::GameBoy: {
            GameBoyEmulation gb;
            if (gb.loadRom(path)) gb.runInteractive();
            break;
        }
        case SystemType::GameBoyColor: {
            GameBoyColorEmulation gbc;
            if (gbc.loadRom(path)) gbc.runInteractive();
            break;
        }
        case SystemType::GBA: {
            GBAEmulation gba;
            try { if (gba.loadRom(path)) gba.runInteractive(); }
            catch (const std::exception& e) { std::cerr << "GBA error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::NDS: {
            NDSEmulation nds;
            try { if (nds.loadRom(path)) nds.runInteractive(); }
            catch (const std::exception& e) { std::cerr << "NDS error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::ThreeDS: {
            N3DSEmulation n3ds;
            try { if (n3ds.loadRom(path)) n3ds.runInteractive(); }
            catch (const std::exception& e) { std::cerr << "3DS error: " << e.what() << "\n"; }
            break;
        }
        case SystemType::Atari2600: {
            Atari2600Emulation a;
            if (a.loadRom(path)) a.runInteractive();
            break;
        }
        case SystemType::Atari5200: {
            Atari5200Emulation a;
            if (a.loadRom(path)) a.runInteractive();
            break;
        }
        case SystemType::Atari800: {
            Atari800Emulation a;
            if (a.loadRom(path)) a.runInteractive();
            break;
        }
        case SystemType::SegaGenesis: {
            SegaGenesisEmulation gen;
            if (gen.loadRom(path)) gen.runInteractive();
            break;
        }
        case SystemType::Odyssey: {
            OdysseyEmulation ody;
            if (ody.loadRom(path)) ody.runInteractive();
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


