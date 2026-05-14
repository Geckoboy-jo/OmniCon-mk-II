#include "Odyssey.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

// ─── Static members ───────────────────────────────────────────────────────────

OdysseyEmulation::InputMode OdysseyEmulation::globalInputMode =
    OdysseyEmulation::InputMode::KeyboardAndController;

OdysseyEmulation::KeyBindings OdysseyEmulation::globalKeyBindings = {
    SDLK_w, SDLK_s,          // P1 up / down
    SDLK_UP, SDLK_DOWN,      // P2 up / down
    SDLK_SPACE               // serve
};

OdysseyEmulation::ControllerBindings OdysseyEmulation::globalControllerBindings = {
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_START
};

bool OdysseyEmulation::globalInputConfigured = false;

// ─── Card table ───────────────────────────────────────────────────────────────

struct CardInfo { OdysseyCard card; const char* name; const char* desc; };
static const CardInfo kCards[] = {
    { OdysseyCard::Tennis,       "Tennis",        "Serve past your opponent to score points"         },
    { OdysseyCard::Hockey,       "Hockey",        "Score goals past your opponent's paddle"          },
    { OdysseyCard::Football,     "Football",      "Advance the ball downfield to score a touchdown"  },
    { OdysseyCard::Ski,          "Ski",           "Guide through the slalom gates without missing"   },
    { OdysseyCard::Analogic,     "Analogic",      "Move both spots to matching positions"            },
    { OdysseyCard::BrainWave,    "Brain Wave",    "Position your spot on the matching target"        },
    { OdysseyCard::States,       "States",        "Locate the state called by the referee"           },
    { OdysseyCard::HauntedHouse, "Haunted House", "The ghost hunts the intruder through dark rooms"  },
    { OdysseyCard::CatAndMouse,  "Cat and Mouse", "Cat must catch the mouse — mouse must escape"     },
    { OdysseyCard::Invasion,     "Invasion",      "Repel invaders before they cross your line"       },
    { OdysseyCard::OdysseyGame,  "Odyssey",       "Classic head-to-head paddle duel"                 },
    { OdysseyCard::SimonSays,    "Simon Says",    "Mirror every move your opponent makes exactly"    },
};
static constexpr int kNumCards = static_cast<int>(sizeof(kCards) / sizeof(kCards[0]));

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::string toLowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

static const char* findFont() {
    static const char* kFonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        nullptr
    };
    for (int i = 0; kFonts[i]; ++i)
        if (access(kFonts[i], R_OK) == 0) return kFonts[i];
    return nullptr;
}

// ─── cardFromStem ─────────────────────────────────────────────────────────────

OdysseyCard OdysseyEmulation::cardFromStem(const std::string& stem) const {
    std::string l = toLowerStr(stem);
    if (l.find("tennis")   != std::string::npos) return OdysseyCard::Tennis;
    if (l.find("hockey")   != std::string::npos) return OdysseyCard::Hockey;
    if (l.find("football") != std::string::npos) return OdysseyCard::Football;
    if (l.find("ski")      != std::string::npos) return OdysseyCard::Ski;
    if (l.find("analogic") != std::string::npos) return OdysseyCard::Analogic;
    if (l.find("brain")    != std::string::npos) return OdysseyCard::BrainWave;
    if (l.find("states")   != std::string::npos) return OdysseyCard::States;
    if (l.find("haunted")  != std::string::npos) return OdysseyCard::HauntedHouse;
    if (l.find("cat")      != std::string::npos) return OdysseyCard::CatAndMouse;
    if (l.find("invasion") != std::string::npos) return OdysseyCard::Invasion;
    if (l.find("simon")    != std::string::npos) return OdysseyCard::SimonSays;
    return OdysseyCard::Tennis;
}

const char* OdysseyEmulation::cardDescription() const {
    for (int i = 0; i < kNumCards; ++i)
        if (kCards[i].card == card_) return kCards[i].desc;
    return "";
}

// ─── loadRom ──────────────────────────────────────────────────────────────────

bool OdysseyEmulation::loadRom(const std::string& path) {
    std::string stem = fs::path(path).stem().string();
    card_ = cardFromStem(stem);
    // Use friendly card name if available
    cardName_ = stem;
    for (int i = 0; i < kNumCards; ++i)
        if (kCards[i].card == card_) { cardName_ = kCards[i].name; break; }

    inputMode_ = globalInputMode;
    keys_      = globalKeyBindings;
    ctrl_      = globalControllerBindings;
    return true;
}

// ─── initGame / resetBall ─────────────────────────────────────────────────────

void OdysseyEmulation::initGame() {
    p1_ = { 24.f,                              static_cast<float>(kH / 2 - kPadH / 2) };
    p2_ = { static_cast<float>(kW - 24 - kPadW), static_cast<float>(kH / 2 - kPadH / 2) };
    score1_ = score2_ = 0;
    inPlay_ = false;
    resetBall(1);
}

void OdysseyEmulation::resetBall(int serveDir) {
    ball_ = { static_cast<float>(kW / 2 - kBallSz / 2),
              static_cast<float>(kH / 2 - kBallSz / 2) };
    float deg   = static_cast<float>(rand() % 50) - 25.f;
    float rad   = deg * 3.14159f / 180.f;
    ballVel_    = { kBallSpd * serveDir * std::cos(rad),
                    kBallSpd * std::sin(rad) };
    inPlay_     = false;
}

// ─── update ───────────────────────────────────────────────────────────────────

void OdysseyEmulation::update(float dt,
                               bool p1Up, bool p1Down,
                               bool p2Up, bool p2Down,
                               bool serve) {
    if (!inPlay_) {
        if (serve) inPlay_ = true;
        return;
    }

    // Move paddles
    if (p1Up)   p1_.y -= kPadSpeed * dt;
    if (p1Down) p1_.y += kPadSpeed * dt;
    if (p2Up)   p2_.y -= kPadSpeed * dt;
    if (p2Down) p2_.y += kPadSpeed * dt;

    p1_.y = std::clamp(p1_.y, 0.f, static_cast<float>(kH - kPadH));
    p2_.y = std::clamp(p2_.y, 0.f, static_cast<float>(kH - kPadH));

    // Move ball
    ball_.x += ballVel_.x * dt;
    ball_.y += ballVel_.y * dt;

    // Bounce off top / bottom walls
    if (ball_.y <= 0.f) {
        ball_.y   = 0.f;
        ballVel_.y = std::abs(ballVel_.y);
    }
    if (ball_.y + kBallSz >= kH) {
        ball_.y   = static_cast<float>(kH - kBallSz);
        ballVel_.y = -std::abs(ballVel_.y);
    }

    // Collide with P1 (left) paddle — only when ball is moving left
    SDL_Rect bR  = { (int)ball_.x, (int)ball_.y,  kBallSz, kBallSz };
    SDL_Rect p1R = { (int)p1_.x,   (int)p1_.y,    kPadW,   kPadH   };
    SDL_Rect p2R = { (int)p2_.x,   (int)p2_.y,    kPadW,   kPadH   };
    SDL_Rect dummy;

    if (ballVel_.x < 0 && SDL_IntersectRect(&bR, &p1R, &dummy)) {
        ball_.x = p1_.x + kPadW;
        float rel = (ball_.y + kBallSz * 0.5f - (p1_.y + kPadH * 0.5f)) / (kPadH * 0.5f);
        float ang = rel * 55.f * 3.14159f / 180.f;
        float spd = std::min(kBallSpd * 1.06f, kBallSpd * 1.8f);
        ballVel_  = { spd * std::cos(ang), spd * std::sin(ang) };
    }

    if (ballVel_.x > 0 && SDL_IntersectRect(&bR, &p2R, &dummy)) {
        ball_.x = p2_.x - kBallSz;
        float rel = (ball_.y + kBallSz * 0.5f - (p2_.y + kPadH * 0.5f)) / (kPadH * 0.5f);
        float ang = rel * 55.f * 3.14159f / 180.f;
        float spd = std::min(kBallSpd * 1.06f, kBallSpd * 1.8f);
        ballVel_  = { -spd * std::cos(ang), spd * std::sin(ang) };
    }

    // Scoring: ball exits screen left or right
    if (ball_.x + kBallSz < 0) {
        ++score2_;
        resetBall(-1);
    } else if (ball_.x > kW) {
        ++score1_;
        resetBall(1);
    }
}

// ─── renderText ───────────────────────────────────────────────────────────────

void OdysseyEmulation::renderText(const std::string& text, int x, int y,
                                   SDL_Color col) {
    if (!font_ || !renderer_ || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font_, text.c_str(), col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    if (tex) {
        // x == -1 → horizontally centred
        int rx = (x < 0) ? (kW - surf->w) / 2 : x;
        SDL_Rect dst = { rx, y, surf->w, surf->h };
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

// ─── render ───────────────────────────────────────────────────────────────────

void OdysseyEmulation::render() {
    // Authentic Odyssey output: black background, white sprites only
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);

    // Dashed centre divider (game play area: y 48 to y kH-32)
    for (int y = 52; y < kH - 36; y += 20) {
        SDL_Rect d = { kW / 2 - 2, y, 4, 12 };
        SDL_RenderFillRect(renderer_, &d);
    }

    // Paddles
    SDL_Rect pr1 = { (int)p1_.x, (int)p1_.y, kPadW, kPadH };
    SDL_Rect pr2 = { (int)p2_.x, (int)p2_.y, kPadW, kPadH };
    SDL_RenderFillRect(renderer_, &pr1);
    SDL_RenderFillRect(renderer_, &pr2);

    // Ball
    SDL_Rect br = { (int)ball_.x, (int)ball_.y, kBallSz, kBallSz };
    SDL_RenderFillRect(renderer_, &br);

    // ── HUD ──────────────────────────────────────────────────────────────────

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color grey   = {150, 150, 150, 255};
    SDL_Color dimgrey= { 85,  85,  85, 255};

    // Card name — top centre
    renderText("[ " + cardName_ + " ]", -1, 10, grey);

    // Scores — either side of centre line
    renderText(std::to_string(score1_), kW / 4, 10, white);
    renderText(std::to_string(score2_), 3 * kW / 4, 10, white);

    // "Serve" prompt
    if (!inPlay_)
        renderText("Press SPACE / Start to serve", -1, kH / 2 - 12, grey);

    // Card description
    renderText(cardDescription(), 10, kH - 44, dimgrey);

    // Controls hint
    renderText("W / S  \xe2\x80\x94  P1 Up/Down     Up / Down  \xe2\x80\x94  P2      Esc = Quit",
               10, kH - 22, dimgrey);

    SDL_RenderPresent(renderer_);
}

// ─── runInteractive ───────────────────────────────────────────────────────────

void OdysseyEmulation::runInteractive() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "[Odyssey] SDL_InitSubSystem: " << SDL_GetError() << "\n";
        return;
    }

    bool ttfOwned = (TTF_WasInit() == 0);
    if (ttfOwned && TTF_Init() != 0) {
        std::cerr << "[Odyssey] TTF_Init: " << TTF_GetError() << "\n";
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return;
    }

    window_ = SDL_CreateWindow(
        ("Magnavox Odyssey \xe2\x80\x94 " + cardName_).c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kW, kH,
        SDL_WINDOW_SHOWN);
    if (!window_) {
        std::cerr << "[Odyssey] SDL_CreateWindow: " << SDL_GetError() << "\n";
        if (ttfOwned) TTF_Quit();
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        std::cerr << "[Odyssey] SDL_CreateRenderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window_); window_ = nullptr;
        if (ttfOwned) TTF_Quit();
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return;
    }

    const char* fontPath = findFont();
    if (fontPath) font_ = TTF_OpenFont(fontPath, 18);

    // Open first available controller
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { controller_ = SDL_GameControllerOpen(i); break; }

    srand(static_cast<unsigned>(SDL_GetTicks()));
    initGame();

    Uint64 prev = SDL_GetPerformanceCounter();
    bool   quit = false;

    while (!quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        float  dt  = static_cast<float>(now - prev) /
                     static_cast<float>(SDL_GetPerformanceFrequency());
        prev = now;
        if (dt > 0.05f) dt = 0.05f;   // cap to 50 ms

        // Handle events
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { quit = true; break; }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                quit = true; break;
            }
        }
        if (quit) break;

        // Sample continuous input
        bool p1Up = false, p1Down = false;
        bool p2Up = false, p2Down = false;
        bool serve = false;

        const Uint8* kb = SDL_GetKeyboardState(nullptr);

        if (inputMode_ != InputMode::ControllerOnly) {
            p1Up   = kb[SDL_GetScancodeFromKey(keys_.p1Up)]   != 0;
            p1Down = kb[SDL_GetScancodeFromKey(keys_.p1Down)] != 0;
            p2Up   = kb[SDL_GetScancodeFromKey(keys_.p2Up)]   != 0;
            p2Down = kb[SDL_GetScancodeFromKey(keys_.p2Down)] != 0;
            serve  = kb[SDL_GetScancodeFromKey(keys_.serve)]  != 0;
        }

        if (controller_ && inputMode_ != InputMode::KeyboardOnly) {
            auto btn = [&](SDL_GameControllerButton b) {
                return SDL_GameControllerGetButton(controller_, b) != 0;
            };
            int16_t ly = SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY);
            int16_t ry = SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_RIGHTY);
            if (ly < -8000 || btn(ctrl_.p1Up))   p1Up   = true;
            if (ly >  8000 || btn(ctrl_.p1Down)) p1Down = true;
            if (ry < -8000 || btn(ctrl_.p2Up))   p2Up   = true;
            if (ry >  8000 || btn(ctrl_.p2Down)) p2Down = true;
            if (btn(ctrl_.serve))                serve  = true;
        }

        update(dt, p1Up, p1Down, p2Up, p2Down, serve);
        render();
    }

    // Cleanup
    if (controller_) { SDL_GameControllerClose(controller_); controller_ = nullptr; }
    if (font_)        { TTF_CloseFont(font_);                 font_       = nullptr; }
    if (renderer_)    { SDL_DestroyRenderer(renderer_);       renderer_   = nullptr; }
    if (window_)      { SDL_DestroyWindow(window_);           window_     = nullptr; }
    if (ttfOwned)     TTF_Quit();
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
}

// ─── Input configuration ──────────────────────────────────────────────────────

void OdysseyEmulation::configureInput(InputMode mode) {
    globalInputMode      = mode;
    globalInputConfigured = true;
}

void OdysseyEmulation::saveGlobalInputSettings() {
    globalInputConfigured = true;
}

void OdysseyEmulation::loadGlobalInputSettings() {
    inputMode_ = globalInputMode;
    keys_      = globalKeyBindings;
    ctrl_      = globalControllerBindings;
}

void OdysseyEmulation::configureKeyBinding(const std::string& action, SDL_Keycode key) {
    if (action == "p1_up"  ) globalKeyBindings.p1Up   = key;
    if (action == "p1_down") globalKeyBindings.p1Down = key;
    if (action == "p2_up"  ) globalKeyBindings.p2Up   = key;
    if (action == "p2_down") globalKeyBindings.p2Down = key;
    if (action == "serve"  ) globalKeyBindings.serve  = key;
}

void OdysseyEmulation::configureControllerBinding(const std::string& action,
                                                   SDL_GameControllerButton btn) {
    if (action == "p1_up"  ) globalControllerBindings.p1Up   = btn;
    if (action == "p1_down") globalControllerBindings.p1Down = btn;
    if (action == "p2_up"  ) globalControllerBindings.p2Up   = btn;
    if (action == "p2_down") globalControllerBindings.p2Down = btn;
    if (action == "serve"  ) globalControllerBindings.serve  = btn;
}

void OdysseyEmulation::printCurrentSettings() const {
    auto modeName = [](InputMode m) -> const char* {
        return m == InputMode::KeyboardOnly    ? "Keyboard Only"
             : m == InputMode::ControllerOnly  ? "Controller Only"
             :                                   "Keyboard + Controller";
    };
    std::cout << "Magnavox Odyssey Input:\n"
              << "  Mode:    " << modeName(inputMode_) << "\n"
              << "  P1 Up:   " << SDL_GetKeyName(keys_.p1Up)   << "\n"
              << "  P1 Down: " << SDL_GetKeyName(keys_.p1Down) << "\n"
              << "  P2 Up:   " << SDL_GetKeyName(keys_.p2Up)   << "\n"
              << "  P2 Down: " << SDL_GetKeyName(keys_.p2Down) << "\n"
              << "  Serve:   " << SDL_GetKeyName(keys_.serve)  << "\n";
}
