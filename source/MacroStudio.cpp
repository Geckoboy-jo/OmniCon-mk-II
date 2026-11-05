// MacroStudio.cpp — the "Create Macro" studio: a modal screen in the game
// shell for building PyAutoGUI macros that work with any console.
//
// Pause behaviour: opening the studio SIGSTOPs the most recently launched
// external game (its whole process group — the launchers call setsid()) and
// closing the studio SIGCONTs it.  Built-in emulator backends block the shell
// while they run, so for those the studio opens before/after a session.
//
// Colour safety: the studio draws only inside the shell's own window — never
// over the game — and the colour-matcher tool captures pixels with no overlay
// at all, so game colours are never affected by this UI.

#include "GameShell.hpp"
#include "Macro.hpp"
#include "ConsoleLaunchers.hpp"
#include "GameLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

// Global hotkey support (X11 / XWayland).  On pure Wayland the compositor
// owns global shortcuts, so the listener simply stays inactive there and the
// in-shell F2 / Ctrl+M bindings still work.
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/XKBlib.h>

// X11 defines None/Status/Success etc. as macros; None collides with
// MacroTrigger::Type::None.  Undefine the offenders after the includes.
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif

namespace fs = std::filesystem;

namespace {

// ─── Studio palette (shell-window only; never drawn over the game) ───────────
const SDL_Color kStudioBg     = { 11,  13,  28, 255};
const SDL_Color kStudioPanel  = { 16,  18,  38, 255};
const SDL_Color kStudioSel    = { 14,  42,  88, 255};
const SDL_Color kStudioBorder = { 60, 150, 255, 255};
const SDL_Color kStudioText   = {220, 230, 255, 255};
const SDL_Color kStudioDim    = {110, 120, 165, 255};
const SDL_Color kStudioAccent = { 90, 200, 250, 255};
const SDL_Color kStudioGood   = { 90, 220, 130, 255};
const SDL_Color kStudioWarn   = {240, 190,  90, 255};

// One editable row in a field-driven editor screen.
struct StudioField {
    enum class Kind { Text, Int, Enum, Button } kind = Kind::Text;
    std::string                         label;
    std::function<std::string()>        get;
    std::function<void(const std::string&)> commit;   // Text / Int
    std::function<void(int)>            cycle;        // Enum: -1 / +1
    std::function<void()>               activate;     // Button
    int                                 intMin  = INT_MIN;
    int                                 intMax  = INT_MAX;
    int                                 intStep = 1;
};

std::string onOff(bool v) { return v ? "on" : "off"; }

std::string clampIntText(const std::string& text, int fallback, int lo, int hi) {
    try {
        const int v = std::stoi(text);
        return std::to_string(std::max(lo, std::min(hi, v)));
    } catch (...) {
        return std::to_string(fallback);
    }
}

// Cycle helper for enum-like fields backed by an index into a list.
void cycleIndex(int& idx, int dir, int count) {
    idx = (idx + dir % count + count) % count;
}

std::string describeAction(const MacroAction& a) {
    std::ostringstream ss;
    ss << MacroAction::typeName(a.type);
    switch (a.type) {
        case MacroAction::Type::Key:
        case MacroAction::Type::KeyDown:
        case MacroAction::Type::KeyUp:
            ss << " '" << a.key << "'";
            break;
        case MacroAction::Type::Click:
            ss << " " << (a.key.empty() ? "left" : a.key)
               << " @ (" << a.x << ", " << a.y << ")";
            break;
        case MacroAction::Type::Move:
            ss << " -> (" << a.x << ", " << a.y << ")";
            break;
        case MacroAction::Type::Wait:
            ss << " " << a.ms << " ms";
            break;
        case MacroAction::Type::TypeText:
            ss << " \"" << a.text << "\"";
            break;
    }
    return ss.str();
}

std::string describeTrigger(const MacroTrigger& t) {
    std::ostringstream ss;
    switch (t.type) {
        case MacroTrigger::Type::None:
            ss << "none";
            break;
        case MacroTrigger::Type::PixelColor:
            ss << "pixel (" << t.x << ", " << t.y << ") = rgb("
               << t.r << ", " << t.g << ", " << t.b << ") ±" << t.tolerance;
            break;
        case MacroTrigger::Type::Button:
            ss << "button '" << t.button << "'";
            break;
    }
    return ss.str();
}

} // namespace

// ─── Macro engine process management ─────────────────────────────────────────

void GameShell::stopMacroEngine() {
    MacroStore store;
    const fs::path pidPath = fs::path(store.dir()) / "engine.pid";
    long pid = -1;
    {
        std::ifstream in(pidPath);
        if (in) in >> pid;
    }
    if (pid > 0 && kill(static_cast<pid_t>(pid), 0) == 0)
        kill(static_cast<pid_t>(pid), SIGTERM);
    std::error_code ec;
    fs::remove(pidPath, ec);
}

void GameShell::startMacroEngine(const GameEntry& game) {
    MacroStore store;
    const std::string sysName = GameLibrary::systemName(game.system);
    if (store.forGame(sysName, game.displayName).empty())
        return;   // no macros apply to this game

    stopMacroEngine();   // only one engine at a time

    std::error_code ec;
    fs::create_directories(store.dir(), ec);
    const std::string logPath = (fs::path(store.dir()) / "engine.log").string();
    const std::string dir = store.dir();

    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[Macro] Could not fork macro engine.\n";
        return;
    }
    if (pid == 0) {
        setsid();
        const int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        }
        execlp("python3", "python3", "scripts/macro_engine.py", "run-all",
               dir.c_str(), "--system", sysName.c_str(),
               "--game", game.displayName.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    // Record the pid ourselves (race-free; the engine writes the same value).
    std::ofstream pidFile(fs::path(store.dir()) / "engine.pid", std::ios::trunc);
    pidFile << pid;
    std::cerr << "[Macro] Engine started (pid " << pid << ") for "
              << game.displayName << "\n";
}

// ─── Colour matcher ───────────────────────────────────────────────────────────
// Runs scripts/macro_engine.py pick with the shell window minimised.  The
// helper draws no overlay, so the captured colour is exactly what the game
// renders.  Blocks until the user clicks (or presses Enter in the terminal).

bool GameShell::runColorPicker(int& outX, int& outY,
                               int& outR, int& outG, int& outB) {
    return runColorPickerOn(window_, outX, outY, outR, outG, outB);
}

// Static-ish helper: minimise `win` (the studio popup when invoked from the
// studio, so the game stays fully visible), run the no-overlay picker, then
// restore the window.  The picker draws nothing, so colours are exact.
bool GameShell::runColorPickerOn(SDL_Window* win, int& outX, int& outY,
                                 int& outR, int& outG, int& outB) {
    int pipefd[2] = {};
    if (pipe(pipefd) != 0) return false;

    if (win) SDL_MinimizeWindow(win);

    const pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("python3", "python3", "scripts/macro_engine.py", "pick",
               static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buf[256];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        output.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);
    int status = 0;
    if (pid > 0) waitpid(pid, &status, 0);

    if (win) {
        SDL_RestoreWindow(win);
        SDL_RaiseWindow(win);
    }

    int x, y, r, g, b;
    if (std::sscanf(output.c_str(), "%d,%d,%d,%d,%d", &x, &y, &r, &g, &b) != 5)
        return false;
    outX = x; outY = y; outR = r; outG = g; outB = b;
    return true;
}

// ─── Global F2 hotkey ─────────────────────────────────────────────────────────
// Watches raw keyboard events via XInput2 so F2 opens the studio even while an
// emulator window has focus.  The listener thread only sets an atomic flag;
// the main loop opens the studio on the UI thread (SDL is not thread-safe).

void GameShell::startMacroHotkeyListener() {
    if (macroHotkeyThread_.joinable()) return;   // already running
    macroHotkeyStop_.store(false);
    macroHotkeyThread_ = std::thread([this]() {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return;   // no X11 (pure Wayland) — hotkey unavailable

        int xiOpcode = 0, xiEvent = 0, xiError = 0;
        if (XQueryExtension(dpy, "XInputExtension", &xiOpcode, &xiEvent, &xiError) == 0) {
            XCloseDisplay(dpy);
            return;
        }
        int major = 2, minor = 3;   // need XI 2.3+ for raw key events
        if (XIQueryVersion(dpy, &major, &minor) != Success || major < 2) {
            XCloseDisplay(dpy);
            return;
        }

        unsigned char mask[XIMaskLen(XI_RawKeyPress)] = {};
        XISetMask(mask, XI_RawKeyPress);
        XIEventMask evMask;
        evMask.deviceid = XIAllMasterDevices;
        evMask.mask_len = sizeof(mask);
        evMask.mask     = mask;
        XISelectEvents(dpy, DefaultRootWindow(dpy), &evMask, 1);

        const KeySym f2 = XStringToKeysym("F2");
        while (!macroHotkeyStop_.load()) {
            if (!XPending(dpy)) {
                // Poll with a short sleep so the stop flag is responsive.
                struct timeval tv {0, 100000};
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(ConnectionNumber(dpy), &fds);
                if (select(ConnectionNumber(dpy) + 1, &fds, nullptr, nullptr, &tv) <= 0)
                    continue;
            }
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.xcookie.type != GenericEvent || ev.xcookie.extension != xiOpcode)
                continue;
            if (!XGetEventData(dpy, &ev.xcookie))
                continue;
            const XIRawEvent* raw = static_cast<const XIRawEvent*>(ev.xcookie.data);
            const bool isPress = ev.xcookie.evtype == XI_RawKeyPress;
            const int  keycode = raw ? raw->detail : 0;
            XFreeEventData(dpy, &ev.xcookie);
            if (isPress && keycode > 0 &&
                XkbKeycodeToKeysym(dpy, static_cast<KeyCode>(keycode), 0, 0) == f2) {
                macroHotkeyPending_.store(true);
            }
        }
        XCloseDisplay(dpy);
    });
}

void GameShell::stopMacroHotkeyListener() {
    macroHotkeyStop_.store(true);
    if (macroHotkeyThread_.joinable())
        macroHotkeyThread_.join();
}

// ─── The studio modal ────────────────────────────────────────────────────────

// Pause the most recently launched external game.  Returns the pid that was
// stopped, or -1 if no running game could be found.  The launchers call
// setsid(), so the recorded pid is also the process-group id; signalling the
// group pauses the emulator and any children it spawned.
static pid_t pauseRunningGame() {
    pid_t pid = lastLaunchedGamePid();
    if (pid > 0) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid)
            pid = -1;   // already exited; reaped
    }
    if (pid > 0 && kill(pid, 0) == 0) {
        if (kill(-pid, SIGSTOP) == 0) {
            std::cerr << "[Macro] Paused game process group " << pid << "\n";
            return pid;
        }
        // Group signal failed (e.g. launcher didn't setsid); pause just the pid.
        if (kill(pid, SIGSTOP) == 0) {
            std::cerr << "[Macro] Paused game pid " << pid << " (no group)\n";
            return pid;
        }
        std::cerr << "[Macro] Could not pause pid " << pid
                  << ": " << std::strerror(errno) << "\n";
    } else if (pid > 0) {
        std::cerr << "[Macro] Recorded game pid " << pid << " is not running.\n";
    } else {
        std::cerr << "[Macro] No external game has been launched this session.\n";
    }
    return -1;
}

static void resumeGame(pid_t pid) {
    if (pid <= 0) return;
    if (kill(-pid, SIGCONT) != 0)
        kill(pid, SIGCONT);
    std::cerr << "[Macro] Resumed game pid " << pid << "\n";
}

void GameShell::runMacroStudio() {
    if (!renderer_) return;

    // ── Pause the running external game (whole process group) ────────────
    // Mutable so the studio's Pause/Resume toggle can let the game keep
    // running while the overlay stays open (live overlay mode).
    pid_t gamePid    = pauseRunningGame();
 

    // ── Create a borderless always-on-top overlay window ──────────────────
    // The studio floats over the game as a semi-transparent overlay: the game
    // (and shell) stay open and visible behind it.  SDL_WINDOW_ALWAYS_ON_TOP
    // keeps it above the game; SDL_WINDOW_BORDERLESS removes the title bar so
    // it reads as an overlay panel.  SDL_SetWindowOpacity makes the whole
    // panel translucent (X11 _NET_WM_WINDOW_OPACITY), so the game shows
    // through while you build triggers.  Press O to cycle opacity.
    constexpr int kPopW = 980;
    constexpr int kPopH = 640;
    SDL_Window* pop = SDL_CreateWindow(
        "OmniCon — Macro Studio",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kPopW, kPopH,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_BORDERLESS);
    if (!pop) {
        std::cerr << "[Macro] Could not create studio window: " << SDL_GetError() << "\n";
        resumeGame(gamePid);
        return;
    }
    SDL_Renderer* r = SDL_CreateRenderer(pop, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(pop, -1, SDL_RENDERER_SOFTWARE);
    if (!r) {
        std::cerr << "[Macro] Could not create studio renderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(pop);
        resumeGame(gamePid);
        return;
    }
    // Fixed logical layout regardless of popup size.
    SDL_RenderSetLogicalSize(r, kPopW, kPopH);
    SDL_RaiseWindow(pop);

    // Overlay opacity (whole-window translucency).  Start slightly transparent
    // so the game is visible behind the panel; O cycles through the levels.
    static const float kOpacityLevels[] = {1.0f, 0.92f, 0.80f, 0.65f};
    int   opacityIdx = 1;               // 0.92 — mostly opaque, game faintly visible
    float opacity    = kOpacityLevels[opacityIdx];
    SDL_SetWindowOpacity(pop, opacity);

    // Local text renderer bound to the popup (renderText uses renderer_).
    auto rText = [&](const std::string& s, int x, int y, SDL_Color c,
                     TTF_Font* f, int maxW = 0) {
        if (!f || s.empty()) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        int w = surf->w, h = surf->h;
        SDL_FreeSurface(surf);
        if (!tex) return;
        if (maxW > 0 && w > maxW) w = maxW;
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    };
    auto rCentered = [&](const std::string& s, int x, int y, int w,
                         SDL_Color c, TTF_Font* f) {
        if (!f || s.empty()) return;
        int tw = 0, th = 0;
        TTF_SizeUTF8(f, s.c_str(), &tw, &th);
        rText(s, x + (w - tw) / 2, y, c, f, w);
    };

    MacroStore store;
    std::error_code ec;
    fs::create_directories(store.dir(), ec);

    // System options for the scope enum: "any" + every known console.
    std::vector<std::string> systemOptions = {"any"};
    for (int i = 0; i < kNumSystems; ++i)
        systemOptions.push_back(GameLibrary::systemName(kSystems[i]));

    enum class Screen { List, Edit, Actions, Trigger };
    Screen screen = Screen::List;

    std::vector<Macro> macros = store.loadAll();
    int  listSel  = 0;
    Macro editing;
    std::string originalName;      // for rename-on-save
    bool        isNew     = false;
    int  editSel   = 0;
    int  actionSel = 0;
    bool actionEditing = false;
    int  actionFieldSel  = 0;
    int  trigSel   = 0;

    // Text-edit state (shared by every screen).
    bool                                  textEditing = false;
    std::string                           textBuf;
    std::function<void(const std::string&)> activeCommit;

    std::string statusMsg;
    Uint32      statusUntil = 0;
    auto setStatus = [&](const std::string& msg, Uint32 ms = 4000) {
        statusMsg   = msg;
        statusUntil = SDL_GetTicks() + ms;
    };

    // Live pause state (toggled with the G key).  The game starts paused.
    bool paused = gamePid > 0;
    auto togglePause = [&]() {
        if (gamePid <= 0) {
            setStatus("No external game is running to pause/resume.");
            return;
        }
        if (paused) {
            resumeGame(gamePid);
            paused = false;
            setStatus("Game resumed — overlay stays open (live mode).");
        } else {
            if (kill(-gamePid, SIGSTOP) == 0 || kill(gamePid, SIGSTOP) == 0) {
                paused = true;
                setStatus("Game paused.");
            } else {
                setStatus("Could not pause the game.");
            }
        }
    };
    if (paused)
        setStatus("Game paused — press G to resume while keeping the overlay open.", 6000);

    auto reload = [&]() {
        macros = store.loadAll();
        if (listSel >= static_cast<int>(macros.size()))
            listSel = std::max(0, static_cast<int>(macros.size()) - 1);
    };

    auto defaultNewMacro = [&]() {
        Macro m;
        m.name = "New Macro";
        if (selectedIdx_ >= 0 && selectedIdx_ < static_cast<int>(filteredGames_.size())) {
            m.system = GameLibrary::systemName(filteredGames_[selectedIdx_]->system);
            m.game   = filteredGames_[selectedIdx_]->displayName;
        }
        return m;
    };

    auto testRun = [&](const Macro& m) {
        const std::string tmp = "saves/.macro_test.json";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << macroToJson(m);
        }
        const pid_t pid = fork();
        if (pid == 0) {
            setsid();
            const int logFd = open("saves/macros/engine.log",
                                   O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (logFd >= 0) {
                dup2(logFd, STDOUT_FILENO);
                dup2(logFd, STDERR_FILENO);
                close(logFd);
            }
            execlp("python3", "python3", "scripts/macro_engine.py", "run",
                   tmp.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        if (pid > 0)
            setStatus("Test run started — close the studio (Esc) to watch it"
                      + std::string(paused ? " (game is paused)." : "."));
        else
            setStatus("Could not start the test run.");
    };

    // ── Field builders (rebuilt every frame; cheap) ───────────────────────

    auto buildEditFields = [&]() {
        std::vector<StudioField> f;

        {
            StudioField s; s.label = "Name"; s.kind = StudioField::Kind::Text;
            s.get = [&]() { return editing.name; };
            s.commit = [&](const std::string& v) { if (!v.empty()) editing.name = v; };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Mode"; s.kind = StudioField::Kind::Enum;
            s.get = [&]() { return Macro::modeName(editing.mode); };
            s.cycle = [&](int dir) {
                editing.mode = editing.mode == Macro::Mode::Timeline
                             ? Macro::Mode::Trigger : Macro::Mode::Timeline;
                (void)dir;
            };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Enabled"; s.kind = StudioField::Kind::Enum;
            s.get = [&]() { return onOff(editing.enabled); };
            s.cycle = [&](int) { editing.enabled = !editing.enabled; };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "System"; s.kind = StudioField::Kind::Enum;
            s.get = [&]() { return editing.system; };
            s.cycle = [&](int dir) {
                int idx = 0;
                for (size_t i = 0; i < systemOptions.size(); ++i)
                    if (systemOptions[i] == editing.system) { idx = static_cast<int>(i); break; }
                cycleIndex(idx, dir, static_cast<int>(systemOptions.size()));
                editing.system = systemOptions[idx];
            };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Game (blank = whole system)";
            s.kind = StudioField::Kind::Text;
            s.get = [&]() { return editing.game; };
            s.commit = [&](const std::string& v) { editing.game = v; };
            f.push_back(s);
        }
        if (editing.mode == Macro::Mode::Timeline) {
            {
                StudioField s; s.label = "Loop"; s.kind = StudioField::Kind::Enum;
                s.get = [&]() { return onOff(editing.loop); };
                s.cycle = [&](int) { editing.loop = !editing.loop; };
                f.push_back(s);
            }
            {
                StudioField s; s.label = "Loop delay (ms)"; s.kind = StudioField::Kind::Int;
                s.intMin = 0; s.intMax = 3600000; s.intStep = 100;
                s.get = [&]() { return std::to_string(editing.loopDelayMs); };
                s.commit = [&](const std::string& v) {
                    editing.loopDelayMs = std::stoi(clampIntText(v, editing.loopDelayMs, 0, 3600000));
                };
                f.push_back(s);
            }
        }
        {
            StudioField s; s.label = "Actions"; s.kind = StudioField::Kind::Button;
            s.get = [&]() {
                return std::to_string(editing.actions.size()) + " action(s)  →";
            };
            s.activate = [&]() { screen = Screen::Actions; actionSel = 0; actionEditing = false; };
            f.push_back(s);
        }
        if (editing.mode == Macro::Mode::Trigger) {
            StudioField s; s.label = "Trigger"; s.kind = StudioField::Kind::Button;
            s.get = [&]() { return describeTrigger(editing.trigger) + "  →"; };
            s.activate = [&]() { screen = Screen::Trigger; trigSel = 0; };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Test run"; s.kind = StudioField::Kind::Button;
            s.get = []() { return "play once now"; };
            s.activate = [&]() { testRun(editing); };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Save"; s.kind = StudioField::Kind::Button;
            s.get = []() { return std::string(); };
            s.activate = [&]() {
                if (editing.name.empty()) { setStatus("Name the macro first."); return; }
                if (!isNew && originalName != editing.name)
                    store.remove(originalName);
                if (store.save(editing)) {
                    setStatus("Saved macro '" + editing.name + "'.");
                    reload();
                    screen = Screen::List;
                } else {
                    setStatus("Save failed — check saves/macros/ permissions.");
                }
            };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Cancel"; s.kind = StudioField::Kind::Button;
            s.get = []() { return std::string(); };
            s.activate = [&]() { screen = Screen::List; };
            f.push_back(s);
        }
        return f;
    };

    auto buildTriggerFields = [&]() {
        std::vector<StudioField> f;
        MacroTrigger& t = editing.trigger;
        {
            StudioField s; s.label = "Trigger type"; s.kind = StudioField::Kind::Enum;
            s.get = [&]() { return MacroTrigger::typeName(t.type); };
            s.cycle = [&](int dir) {
                int idx = static_cast<int>(t.type);
                cycleIndex(idx, dir, 3);
                t.type = static_cast<MacroTrigger::Type>(idx);
            };
            f.push_back(s);
        }
        if (t.type == MacroTrigger::Type::PixelColor) {
            auto intField = [&](const char* label, int& ref, int lo, int hi) {
                StudioField s; s.label = label; s.kind = StudioField::Kind::Int;
                s.intMin = lo; s.intMax = hi;
                s.get = [&]() { return std::to_string(ref); };
                s.commit = [&](const std::string& v) {
                    ref = std::stoi(clampIntText(v, ref, lo, hi));
                };
                return s;
            };
            f.push_back(intField("Pixel X", t.x, 0, 16384));
            f.push_back(intField("Pixel Y", t.y, 0, 16384));
            f.push_back(intField("Colour R", t.r, 0, 255));
            f.push_back(intField("Colour G", t.g, 0, 255));
            f.push_back(intField("Colour B", t.b, 0, 255));
            f.push_back(intField("Tolerance", t.tolerance, 0, 255));
            {
                StudioField s; s.label = "Pick pixel && colour";
                s.kind = StudioField::Kind::Button;
                s.get = []() { return "from screen (no overlay)"; };
                s.activate = [&]() {
                    // Resume the game while picking so the pixel shows its live
                    // colour; restore the previous pause state afterwards.
                    const bool wasPaused = paused;
                    if (wasPaused) { resumeGame(gamePid); paused = false; }
                    int x, y, r, g, b;
                    const bool ok = runColorPickerOn(pop, x, y, r, g, b);
                    if (wasPaused && gamePid > 0 &&
                        (kill(-gamePid, SIGSTOP) == 0 || kill(gamePid, SIGSTOP) == 0))
                        paused = true;
                    if (ok) {
                        t.x = x; t.y = y; t.r = r; t.g = g; t.b = b;
                        setStatus("Picked (" + std::to_string(x) + ", " +
                                  std::to_string(y) + ") rgb(" + std::to_string(r) +
                                  ", " + std::to_string(g) + ", " + std::to_string(b) + ").");
                    } else {
                        setStatus("Picker failed — is pyautogui installed? "
                                  "(pip install pyautogui pynput)", 6000);
                    }
                };
                f.push_back(s);
            }
        }
        if (t.type == MacroTrigger::Type::Button) {
            StudioField s; s.label = "Button (key or left/middle/right)";
            s.kind = StudioField::Kind::Text;
            s.get = [&]() { return t.button; };
            s.commit = [&](const std::string& v) { t.button = v; };
            f.push_back(s);
        }
        if (t.type != MacroTrigger::Type::None) {
            auto intField = [&](const char* label, int& ref, int lo, int hi, int step) {
                StudioField s; s.label = label; s.kind = StudioField::Kind::Int;
                s.intMin = lo; s.intMax = hi; s.intStep = step;
                s.get = [&]() { return std::to_string(ref); };
                s.commit = [&](const std::string& v) {
                    ref = std::stoi(clampIntText(v, ref, lo, hi));
                };
                return s;
            };
            f.push_back(intField("Poll interval (ms)", t.intervalMs, 10, 60000, 10));
            f.push_back(intField("Cooldown (ms)", t.cooldownMs, 0, 600000, 100));
            {
                StudioField s; s.label = "Fire once"; s.kind = StudioField::Kind::Enum;
                s.get = [&]() { return onOff(t.once); };
                s.cycle = [&](int) { t.once = !t.once; };
                f.push_back(s);
            }
        }
        {
            StudioField s; s.label = "Back"; s.kind = StudioField::Kind::Button;
            s.get = []() { return std::string(); };
            s.activate = [&]() { screen = Screen::Edit; };
            f.push_back(s);
        }
        return f;
    };

    auto buildActionFields = [&]() {
        std::vector<StudioField> f;
        MacroAction& a = editing.actions[actionSel];
        {
            StudioField s; s.label = "Action type"; s.kind = StudioField::Kind::Enum;
            s.get = [&]() { return MacroAction::typeName(a.type); };
            s.cycle = [&](int dir) {
                int idx = static_cast<int>(a.type);
                cycleIndex(idx, dir, 7);
                a.type = static_cast<MacroAction::Type>(idx);
            };
            f.push_back(s);
        }
        switch (a.type) {
            case MacroAction::Type::Key:
            case MacroAction::Type::KeyDown:
            case MacroAction::Type::KeyUp: {
                StudioField s; s.label = "Key name (e.g. space, enter, a)";
                s.kind = StudioField::Kind::Text;
                s.get = [&]() { return a.key; };
                s.commit = [&](const std::string& v) { a.key = v; };
                f.push_back(s);
                break;
            }
            case MacroAction::Type::Click: {
                StudioField s; s.label = "Mouse button"; s.kind = StudioField::Kind::Enum;
                s.get = [&]() { return a.key.empty() ? "left" : a.key; };
                s.cycle = [&](int dir) {
                    static const char* const kButtons[] = {"left", "middle", "right"};
                    int idx = 0;
                    for (int i = 0; i < 3; ++i)
                        if (a.key == kButtons[i]) { idx = i; break; }
                    cycleIndex(idx, dir, 3);
                    a.key = kButtons[idx];
                };
                f.push_back(s);
                break;
            }
            case MacroAction::Type::Move:
                break;
            case MacroAction::Type::Wait:
                break;
            case MacroAction::Type::TypeText: {
                StudioField s; s.label = "Text"; s.kind = StudioField::Kind::Text;
                s.get = [&]() { return a.text; };
                s.commit = [&](const std::string& v) { a.text = v; };
                f.push_back(s);
                break;
            }
        }
        if (a.type == MacroAction::Type::Click || a.type == MacroAction::Type::Move) {
            auto intField = [&](const char* label, int& ref) {
                StudioField s; s.label = label; s.kind = StudioField::Kind::Int;
                s.intMin = -1; s.intMax = 16384; s.intStep = 10;
                s.get = [&]() { return std::to_string(ref); };
                s.commit = [&](const std::string& v) {
                    ref = std::stoi(clampIntText(v, ref, -1, 16384));
                };
                return s;
            };
            f.push_back(intField("X (-1 = current)", a.x));
            f.push_back(intField("Y (-1 = current)", a.y));
        }
        if (a.type == MacroAction::Type::Wait || a.type == MacroAction::Type::TypeText) {
            StudioField s; s.label = a.type == MacroAction::Type::Wait
                                   ? "Duration (ms)" : "Interval per char (ms)";
            s.kind = StudioField::Kind::Int;
            s.intMin = 0; s.intMax = 600000; s.intStep = 50;
            s.get = [&]() { return std::to_string(a.ms); };
            s.commit = [&](const std::string& v) {
                a.ms = std::stoi(clampIntText(v, a.ms, 0, 600000));
            };
            f.push_back(s);
        }
        {
            StudioField s; s.label = "Done"; s.kind = StudioField::Kind::Button;
            s.get = []() { return std::string(); };
            s.activate = [&]() { actionEditing = false; };
            f.push_back(s);
        }
        return f;
    };

    // ── Modal loop ────────────────────────────────────────────────────────

    bool done = false;
    while (!done) {
        std::vector<StudioField> fields;
        if (screen == Screen::Edit)         fields = buildEditFields();
        if (screen == Screen::Trigger)      fields = buildTriggerFields();
        if (screen == Screen::Actions && actionEditing) fields = buildActionFields();

        int& fieldSel = screen == Screen::Edit    ? editSel :
                        screen == Screen::Trigger ? trigSel : actionFieldSel;
        if (!fields.empty())
            fieldSel = std::max(0, std::min(fieldSel, static_cast<int>(fields.size()) - 1));

        const Uint32 popId = SDL_GetWindowID(pop);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // Only the popup's own close button closes the studio; ignore
            // events for the shell window so it keeps running behind us.
            if (ev.type == SDL_QUIT) { done = true; break; }
            if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.windowID != popId) continue;
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE) { done = true; break; }
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    ev.window.event == SDL_WINDOWEVENT_RESIZED)
                    SDL_RenderSetLogicalSize(r, kPopW, kPopH);
            }
            // Route keyboard input only when it targets the popup window.
            if (ev.type == SDL_KEYDOWN &&
                ev.key.windowID != 0 && ev.key.windowID != popId)
                continue;

            // ── Active text edit captures all keyboard input ──────────────
            if (textEditing) {
                if (ev.type == SDL_TEXTINPUT) {
                    textBuf += ev.text.text;
                } else if (ev.type == SDL_KEYDOWN) {
                    switch (ev.key.keysym.sym) {
                        case SDLK_RETURN: case SDLK_KP_ENTER:
                            if (activeCommit) activeCommit(textBuf);
                            textEditing = false;
                            SDL_StopTextInput();
                            break;
                        case SDLK_ESCAPE:
                            textEditing = false;
                            SDL_StopTextInput();
                            break;
                        case SDLK_BACKSPACE:
                            while (textBuf.size() > 1 &&
                                   (static_cast<unsigned char>(textBuf.back()) & 0xC0) == 0x80)
                                textBuf.pop_back();
                            if (!textBuf.empty()) textBuf.pop_back();
                            break;
                        default: break;
                    }
                }
                continue;
            }

            if (ev.type != SDL_KEYDOWN) continue;
            const SDL_Keycode key = ev.key.keysym.sym;

            // ── Global overlay keys (work on every screen) ────────────────
            if (key == SDLK_g) { togglePause(); continue; }
            if (key == SDLK_o) {
                opacityIdx = (opacityIdx + 1) % 4;
                opacity = kOpacityLevels[opacityIdx];
                SDL_SetWindowOpacity(pop, opacity);
                setStatus("Overlay opacity " +
                          std::to_string(static_cast<int>(opacity * 100)) + "%.");
                continue;
            }

            // ── Field-driven screens share their key handling ─────────────
            if (screen == Screen::Edit || screen == Screen::Trigger ||
                (screen == Screen::Actions && actionEditing)) {
                switch (key) {
                    case SDLK_UP:
                        if (fieldSel > 0) --fieldSel;
                        break;
                    case SDLK_DOWN:
                        if (fieldSel + 1 < static_cast<int>(fields.size())) ++fieldSel;
                        break;
                    case SDLK_LEFT: case SDLK_RIGHT: {
                        if (fields.empty()) break;
                        StudioField& f = fields[fieldSel];
                        const int dir = key == SDLK_LEFT ? -1 : 1;
                        if (f.kind == StudioField::Kind::Enum && f.cycle) f.cycle(dir);
                        if (f.kind == StudioField::Kind::Int && f.commit) {
                            int cur = 0;
                            try { cur = std::stoi(f.get()); } catch (...) {}
                            cur = std::max(f.intMin, std::min(f.intMax, cur + dir * f.intStep));
                            f.commit(std::to_string(cur));
                        }
                        break;
                    }
                    case SDLK_RETURN: case SDLK_KP_ENTER: {
                        if (fields.empty()) break;
                        StudioField& f = fields[fieldSel];
                        if (f.kind == StudioField::Kind::Button && f.activate) {
                            f.activate();
                        } else if ((f.kind == StudioField::Kind::Text ||
                                    f.kind == StudioField::Kind::Int) && f.commit) {
                            textBuf = f.get();
                            activeCommit = f.commit;
                            textEditing = true;
                            SDL_StartTextInput();
                        }
                        break;
                    }
                    case SDLK_ESCAPE:
                        if (screen == Screen::Actions) actionEditing = false;
                        else if (screen == Screen::Trigger) screen = Screen::Edit;
                        else screen = Screen::List;
                        break;
                    default: break;
                }
                continue;
            }

            // ── Macro list ────────────────────────────────────────────────
            if (screen == Screen::List) {
                switch (key) {
                    case SDLK_ESCAPE: done = true; break;
                    case SDLK_UP:
                        if (listSel > 0) --listSel;
                        break;
                    case SDLK_DOWN:
                        if (listSel + 1 < static_cast<int>(macros.size())) ++listSel;
                        break;
                    case SDLK_n:
                        editing = defaultNewMacro();
                        originalName.clear();
                        isNew = true;
                        editSel = 0;
                        screen = Screen::Edit;
                        break;
                    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_e:
                        if (!macros.empty()) {
                            editing = macros[listSel];
                            originalName = editing.name;
                            isNew = false;
                            editSel = 0;
                            screen = Screen::Edit;
                        }
                        break;
                    case SDLK_SPACE:
                        if (!macros.empty()) {
                            macros[listSel].enabled = !macros[listSel].enabled;
                            store.save(macros[listSel]);
                            reload();
                        }
                        break;
                    case SDLK_DELETE:
                        if (!macros.empty()) {
                            store.remove(macros[listSel].name);
                            setStatus("Deleted macro '" + macros[listSel].name + "'.");
                            reload();
                        }
                        break;
                    case SDLK_p:
                        if (!macros.empty()) testRun(macros[listSel]);
                        break;
                    case SDLK_s:
                        stopMacroEngine();
                        setStatus("Macro engine stopped.");
                        break;
                    default: break;
                }
                continue;
            }

            // ── Action list (not editing a single action) ─────────────────
            if (screen == Screen::Actions) {
                auto& acts = editing.actions;
                switch (key) {
                    case SDLK_ESCAPE: screen = Screen::Edit; break;
                    case SDLK_UP:
                        if (actionSel > 0) --actionSel;
                        break;
                    case SDLK_DOWN:
                        if (actionSel + 1 < static_cast<int>(acts.size())) ++actionSel;
                        break;
                    case SDLK_n:
                        acts.push_back(MacroAction{});
                        actionSel = static_cast<int>(acts.size()) - 1;
                        break;
                    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_e:
                        if (!acts.empty()) {
                            actionEditing = true;
                            actionFieldSel = 0;
                        }
                        break;
                    case SDLK_DELETE:
                        if (!acts.empty()) {
                            acts.erase(acts.begin() + actionSel);
                            actionSel = std::max(0, std::min(actionSel,
                                                 static_cast<int>(acts.size()) - 1));
                        }
                        break;
                    case SDLK_PAGEUP:
                        if (actionSel > 0) {
                            std::swap(acts[actionSel], acts[actionSel - 1]);
                            --actionSel;
                        }
                        break;
                    case SDLK_PAGEDOWN:
                        if (actionSel + 1 < static_cast<int>(acts.size())) {
                            std::swap(acts[actionSel], acts[actionSel + 1]);
                            ++actionSel;
                        }
                        break;
                    default: break;
                }
            }
        }

        // ── Render (into the popup window) ────────────────────────────────
        SDL_RenderSetLogicalSize(r, kPopW, kPopH);
        SDL_SetRenderDrawColor(r, kStudioBg.r, kStudioBg.g, kStudioBg.b, 255);
        SDL_RenderClear(r);

        // Title bar
        SDL_SetRenderDrawColor(r, kStudioPanel.r, kStudioPanel.g, kStudioPanel.b, 255);
        SDL_Rect titleBar = {0, 0, kPopW, 56};
        SDL_RenderFillRect(r, &titleBar);
        SDL_SetRenderDrawColor(r, 30, 30, 65, 255);
        SDL_RenderDrawLine(r, 0, 56, kPopW - 1, 56);
        rText("MACRO STUDIO", 24, 14, kStudioAccent, fontLg_);
        rText("Create Macro — works with any console (PyAutoGUI)",
              260, 22, kStudioDim, fontMd_);
        if (paused)
            rText("GAME PAUSED (G)", kPopW - 220, 20, kStudioWarn, fontMd_);
        else if (gamePid > 0)
            rText("LIVE (G)", kPopW - 220, 20, kStudioGood, fontMd_);

        const int contentTop = 90;
        const int rowH = 30;

        auto renderFieldRows = [&](const std::vector<StudioField>& rows, int sel) {
            int y = contentTop;
            for (size_t i = 0; i < rows.size(); ++i) {
                const StudioField& f = rows[i];
                const bool selected = static_cast<int>(i) == sel;
                if (selected) {
                    SDL_SetRenderDrawColor(r, kStudioSel.r, kStudioSel.g, kStudioSel.b, 255);
                    SDL_Rect rc = {60, y - 5, kPopW - 120, rowH};
                    SDL_RenderFillRect(r, &rc);
                }
                rText(f.label, 80, y, selected ? kStudioText : kStudioDim, fontMd_, 300);
                std::string value = f.get ? f.get() : std::string();
                if (selected && textEditing) {
                    const bool cursor = (SDL_GetTicks() % 800) < 420;
                    value = textBuf + (cursor ? "|" : "");
                }
                if (!value.empty()) {
                    SDL_Color vc = f.kind == StudioField::Kind::Button ? kStudioAccent
                                 : selected ? kStudioText : kStudioGood;
                    rText(value, 400, y, vc, fontMd_, kPopW - 480);
                }
                y += rowH;
            }
            return y;
        };

        int hintY = kPopH - 60;
        if (screen == Screen::List) {
            if (macros.empty()) {
                rCentered("No macros yet — press N to create one.",
                          0, contentTop + 40, kPopW, kStudioDim, fontMd_);
            }
            int y = contentTop;
            for (size_t i = 0; i < macros.size(); ++i) {
                const Macro& m = macros[i];
                const bool selected = static_cast<int>(i) == listSel;
                if (selected) {
                    SDL_SetRenderDrawColor(r, kStudioSel.r, kStudioSel.g, kStudioSel.b, 255);
                    SDL_Rect rc = {60, y - 5, kPopW - 120, rowH};
                    SDL_RenderFillRect(r, &rc);
                }
                rText(m.enabled ? "[on] " : "[off]", 80, y,
                      m.enabled ? kStudioGood : kStudioDim, fontMd_, 60);
                rText(m.name, 150, y, selected ? kStudioText : kStudioDim, fontMd_, 220);
                rText(Macro::modeName(m.mode), 380, y, kStudioAccent, fontMd_, 90);
                std::string scope = m.system;
                if (!m.game.empty()) scope += " / " + m.game;
                rText(scope, 480, y, kStudioDim, fontMd_, 220);
                if (m.mode == Macro::Mode::Trigger)
                    rText(describeTrigger(m.trigger), 710, y, kStudioDim, fontSm_, 250);
                y += rowH;
            }
            rText("N New    Enter Edit    Space On/Off    P Test    Del Delete    "
                  "S Stop engine    G Pause/Resume    O Opacity    Esc Close",
                  80, hintY, kStudioDim, fontSm_);
        } else if (screen == Screen::Edit) {
            rText(isNew ? "New macro" : "Edit: " + originalName,
                  80, contentTop - 26, kStudioAccent, fontMd_);
            renderFieldRows(fields, editSel);
            rText("Up/Down Select    Left/Right Change    Enter Edit/Activate    "
                  "Esc Back",
                  80, hintY, kStudioDim, fontSm_);
        } else if (screen == Screen::Trigger) {
            rText("Trigger — fires the action list when the condition met",
                  80, contentTop - 26, kStudioAccent, fontMd_);
            renderFieldRows(fields, trigSel);
            rText("Pixel trigger: use 'Pick pixel & colour' — the picker draws "
                  "no overlay, so colours are exact.",
                  80, hintY - 22, kStudioDim, fontSm_);
            rText("Up/Down Select    Left/Right Change    Enter Edit/Activate    "
                  "Esc Back",
                  80, hintY, kStudioDim, fontSm_);
        } else if (screen == Screen::Actions) {
            if (actionEditing) {
                rText("Edit action " + std::to_string(actionSel + 1),
                      80, contentTop - 26, kStudioAccent, fontMd_);
                renderFieldRows(fields, actionFieldSel);
                rText("Up/Down Select    Left/Right Change    Enter Edit/Activate    "
                      "Esc Done",
                      80, hintY, kStudioDim, fontSm_);
            } else {
                rText("Actions — run in order", 80, contentTop - 26,
                      kStudioAccent, fontMd_);
                if (editing.actions.empty())
                    rCentered("No actions yet — press N to add one.",
                              0, contentTop + 40, kPopW, kStudioDim, fontMd_);
                int y = contentTop;
                for (size_t i = 0; i < editing.actions.size(); ++i) {
                    const bool selected = static_cast<int>(i) == actionSel;
                    if (selected) {
                        SDL_SetRenderDrawColor(r, kStudioSel.r, kStudioSel.g,
                                               kStudioSel.b, 255);
                        SDL_Rect rc = {60, y - 5, kPopW - 120, rowH};
                        SDL_RenderFillRect(r, &rc);
                    }
                    rText(std::to_string(i + 1) + ". " + describeAction(editing.actions[i]),
                          80, y, selected ? kStudioText : kStudioDim, fontMd_, kPopW - 160);
                    y += rowH;
                }
                rText("N Add    Enter Edit    Del Delete    PgUp/PgDn Reorder    "
                      "Esc Back",
                      80, hintY, kStudioDim, fontSm_);
            }
        }

        // Status line
        if (!statusMsg.empty() && SDL_GetTicks() < statusUntil) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 20, 60, 30, 235);
            SDL_Rect sr = {60, kPopH - 32, kPopW - 120, 24};
            SDL_RenderFillRect(r, &sr);
            rText(statusMsg, sr.x + 10, sr.y + 5, kStudioGood, fontSm_, sr.w - 20);
        }

        SDL_RenderPresent(r);
        SDL_Delay(16);
    }

    SDL_StopTextInput();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(pop);

    // ── Resume the game (only if we left it paused) ───────────────────────
    if (paused)
        resumeGame(gamePid);
}
