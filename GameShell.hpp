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

    // Initialise SDL, run the interactive controller-rebind screen, then clean
    // up.  Safe to call stand-alone (e.g. from the REPL "remap" command).
    void remap() {
        if (init()) { runRebindScreen(); cleanup(); }
    }

    // Per-console button mapping for built-in SDL emulator backends.
    // External emulator systems use the input configuration of their launcher.
    // Pass "all" to step through every built-in console mapping.
    void remapConsole(const std::string& sysName = "all");

    // Load saves/console_bindings.conf and apply to every globalControllerBindings.
    // Called once at program startup (no SDL required).
    static void loadConsoleBindings();

    // Public so free functions in the .cpp can access them
    static const SystemType  kSystems[105];
    static const char* const kSysAbbrev[105];
    static constexpr int     kNumSystems = 105;

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
    static constexpr int kHeaderH  = 50;   // single-row family filter bar
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

    // -1 = All games shown, non-negative values index into kSystems[]
    int                           currentFilter_ = -1;
    std::vector<const GameEntry*> filteredGames_;

    int                           selectedIdx_   = 0;
    int                           hoverIdx_      = -1;   // game slot under mouse
    int                           scrollRow_     = 0;

    // Controller
    SDL_GameController* ctrl_         = nullptr;
    std::string         ctrlName_;          // name of connected controller, or empty

    // Controller navigation auto-repeat (D-pad held)
    int    ctrlRepeatBtn_  = -1;   // SDL_CONTROLLER_BUTTON_* direction held, or -1
    Uint32 ctrlRepeatNext_ = 0;    // SDL tick when the next repeat fires

    // Left analogue stick navigation (per-frame polling)
    int    ctrlStickDir_   = -1;   // current stick direction (SDL_CONTROLLER_BUTTON_DPAD_*) or -1
    Uint32 ctrlStickNext_  = 0;    // SDL tick when stick repeat fires

    // Double-click tracking
    int    lastClickIdx_ = -1;
    Uint32 lastClickMs_  = 0;

    // Search
    bool        searchActive_ = false;
    std::string searchQuery_;

    // Hit rects for header buttons: [0]=All, [1..kNumSystemFamilies]=family, [kNumSystemFamilies+1]=Downloaded
    SDL_Rect filterBtnRects_[kNumSystems + 2];

    // Family dropdown: openFamily_ is -1 when closed, otherwise a SystemFamily index.
    // Items hold the index into kSystems[] that a click selects.
    int  openFamily_     = -1;
    int  hoverDropdown_  = -1;
    SDL_Rect dropdownPanel_ = {0, 0, 0, 0};
    std::vector<std::pair<SDL_Rect, int>> dropdownItems_;
    void layoutDropdown();
    void renderDropdown();
    int  dropdownItemAtMouse(int mx, int my) const;

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

    // ── Controller rebinding ──────────────────────────────────────────────────
    // Interactive "press each button" screen.  Builds an SDL GameController
    // mapping, applies it immediately, and saves to saves/controller_sdlmap.txt
    // so it is reloaded on every future startup.
    void runRebindScreen();

    // Per-console binding UI (called by the public remapConsole()).
    void runConsoleRebindScreen(const std::string& sysName);

    // Returns the GUID string of the currently-open joystick, or "".
    std::string joystickGUID() const;
};
