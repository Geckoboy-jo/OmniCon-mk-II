#ifndef ODYSSEY_HPP
#define ODYSSEY_HPP

// Magnavox Odyssey (1972) — original analog console simulation.
//
// The Odyssey had no CPU or ROM.  Games were selected via physical "game
// cards" (PCBs that rewired the analog circuitry) and transparent plastic
// overlays laid over the TV.  This class simulates the core hardware: two
// movable rectangular paddles and one ball, rendered authentic black-and-white.
//
// "ROM" paths are .odyssey stub files whose filename stem determines which
// game card is loaded (e.g. "Odyssey - Tennis.odyssey" → Tennis card).

#include "top.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

enum class OdysseyCard {
    Tennis       = 1,
    Hockey       = 2,
    Football     = 3,
    Ski          = 4,
    Analogic     = 5,
    BrainWave    = 6,
    States       = 7,
    HauntedHouse = 8,
    CatAndMouse  = 9,
    Invasion     = 10,
    OdysseyGame  = 11,
    SimonSays    = 12,
};

class OdysseyEmulation {
public:
    enum class InputMode {
        KeyboardOnly,
        ControllerOnly,
        KeyboardAndController
    };

    struct KeyBindings {
        SDL_Keycode p1Up;
        SDL_Keycode p1Down;
        SDL_Keycode p2Up;
        SDL_Keycode p2Down;
        SDL_Keycode serve;
    };

    struct ControllerBindings {
        SDL_GameControllerButton p1Up;
        SDL_GameControllerButton p1Down;
        SDL_GameControllerButton p2Up;
        SDL_GameControllerButton p2Down;
        SDL_GameControllerButton serve;
    };

    static InputMode          globalInputMode;
    static KeyBindings        globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool               globalInputConfigured;

    // "path" is a .odyssey game-card stub file.
    // The filename stem determines which card loads.
    bool loadRom(const std::string& path);
    void runInteractive();

    void configureInput(InputMode mode);
    void saveGlobalInputSettings();
    void loadGlobalInputSettings();
    void configureKeyBinding(const std::string& action, SDL_Keycode key);
    void configureControllerBinding(const std::string& action,
                                    SDL_GameControllerButton btn);
    void printCurrentSettings() const;

    // The original Odyssey had no electronic memory; these are no-ops kept
    // for interface consistency with other emulation classes.
    std::vector<unsigned char> dumpSaveData() const { return {}; }
    bool restoreSaveData(const std::vector<unsigned char>&) { return true; }

private:
    // ── Game card ─────────────────────────────────────────────────────────────
    OdysseyCard card_     = OdysseyCard::Tennis;
    std::string cardName_ = "Tennis";

    // ── SDL ───────────────────────────────────────────────────────────────────
    SDL_Window*         window_     = nullptr;
    SDL_Renderer*       renderer_   = nullptr;
    TTF_Font*           font_       = nullptr;
    SDL_GameController* controller_ = nullptr;

    // ── Layout ────────────────────────────────────────────────────────────────
    static constexpr int   kW        = 640;
    static constexpr int   kH        = 480;
    static constexpr int   kPadW     = 10;
    static constexpr int   kPadH     = 60;
    static constexpr int   kBallSz   = 10;
    static constexpr float kPadSpeed = 270.f;   // px / s
    static constexpr float kBallSpd  = 285.f;   // px / s

    // ── Physics ───────────────────────────────────────────────────────────────
    struct Vec2 { float x = 0.f, y = 0.f; };
    Vec2 ball_;
    Vec2 ballVel_;
    Vec2 p1_;    // left paddle top-left corner
    Vec2 p2_;    // right paddle top-left corner
    int  score1_ = 0;
    int  score2_ = 0;
    bool inPlay_ = false;

    // ── Active input bindings ─────────────────────────────────────────────────
    InputMode         inputMode_;
    KeyBindings       keys_;
    ControllerBindings ctrl_;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void        initGame();
    void        resetBall(int serveDir);        // +1 = toward P2, -1 = toward P1
    void        update(float dt,
                       bool p1Up, bool p1Down,
                       bool p2Up, bool p2Down,
                       bool serve);
    void        render();
    void        renderText(const std::string& text, int x, int y,
                           SDL_Color col);
    OdysseyCard cardFromStem(const std::string& stem) const;
    const char* cardDescription() const;
};

#endif // ODYSSEY_HPP
