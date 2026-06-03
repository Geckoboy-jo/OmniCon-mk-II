#pragma once

#include "GameLibrary.hpp"
#include "OracleDB.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include <map>
#include <string>
#include <vector>

class GameShell {
public:
    GameShell()  = default;
    ~GameShell();

    bool init();
    void cleanup();
    void run(GameLibrary& library, const std::string& coversDir);
    void setDBConfig(const OracleDBConfig& cfg) { dbCfg_ = cfg; }

    // Public so free functions in the .cpp can access them
    static const SystemType  kSystems[16];
    static const char* const kSysAbbrev[16];
    static constexpr int     kNumSystems = 16;

private:
    // SDL resources
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font*     fontLg_   = nullptr;   // 26px — info bar title
    TTF_Font*     fontMd_   = nullptr;   // 14px — filter buttons & badges
    TTF_Font*     fontSm_   = nullptr;   // 12px — game names

    std::map<std::string, SDL_Texture*> coverCache_;

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kW        = 1280;
    static constexpr int kH        = 720;
    static constexpr int kHeaderH  = 54;   // filter bar height
    static constexpr int kInfoH    = 46;   // bottom info bar height
    static constexpr int kGridH    = kH - kHeaderH - kInfoH;   // 620
    static constexpr int kCols     = 7;
    static constexpr int kSlotW    = 170;
    static constexpr int kCoverW   = 130;
    static constexpr int kCoverH   = 170;
    // padTop(9)+cover(170)+gap(5)+name(16)+gap(3)+badge(13)+padBot(9) = 225
    static constexpr int kSlotH    = 225;
    static constexpr int kGridLeft = (kW - kCols * kSlotW) / 2;   // 45

    // ── State ─────────────────────────────────────────────────────────────────
    GameLibrary*                  library_       = nullptr;
    std::string                   coversDir_;
    OracleDBConfig                dbCfg_;

    // -1 = All games shown, 0-8 = index into kSystems[]
    int                           currentFilter_ = -1;
    std::vector<const GameEntry*> filteredGames_;

    int                           selectedIdx_   = 0;
    int                           hoverIdx_      = -1;   // game slot under mouse
    int                           scrollRow_     = 0;

    // Controller
    SDL_GameController* ctrl_         = nullptr;
    std::string         ctrlName_;          // name of connected controller, or empty

    // Double-click tracking
    int    lastClickIdx_ = -1;
    Uint32 lastClickMs_  = 0;

    // Search
    bool        searchActive_ = false;
    std::string searchQuery_;

    // Hit rects for filter buttons: [0]=All, [1..kNumSystems]=system 0..kNumSystems-1, [kNumSystems+1]=Downloaded
    SDL_Rect filterBtnRects_[kNumSystems + 2];

    // ── Logic helpers ─────────────────────────────────────────────────────────
    void rebuildFilter();
    void ensureVisible();
    void computeFilterRects();
    // Returns index into filteredGames_ (-1 if not over a game slot)
    int  gameAtMouse(int mx, int my) const;
    // Returns -2=miss, -1=All button, 0-8=system button
    int  filterBtnAtMouse(int mx, int my) const;

    // ── Rendering ─────────────────────────────────────────────────────────────
    void render();
    void renderHeader();
    void renderGrid();
    void renderInfoBar();

    void renderText(const std::string& text, int x, int y,
                    SDL_Color col, TTF_Font* font, int maxW = 0);
    void renderCenteredText(const std::string& text,
                            int x, int y, int w, SDL_Color col, TTF_Font* font);
    void renderSearchBox();

    // ── Cover art ─────────────────────────────────────────────────────────────
    SDL_Texture* getCoverTexture(const GameEntry& game);
    SDL_Texture* loadTexture(const std::string& path);
    void         clearCoverCache();

    // ── Launch ────────────────────────────────────────────────────────────────
    void launchGame(const GameEntry& game);
};
