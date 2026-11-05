#pragma once

#include <string>
#include <vector>

// ─── Macro data model ─────────────────────────────────────────────────────────
// Macros are stored as JSON files in saves/macros/ and executed by
// scripts/macro_engine.py (PyAutoGUI).  Because input is injected at the OS
// level, the same macro works with any console — built-in emulator backends
// and external launchers alike.
//
// Two kinds of macros are supported:
//   * Timeline — a list of actions that play back in order (optionally looping).
//   * Trigger  — a condition (pixel at (x,y) matches a colour, or a keyboard/
//                mouse button is pressed) that fires a list of actions.

struct MacroAction {
    enum class Type {
        Key,         // press and release key
        KeyDown,     // hold key
        KeyUp,       // release key
        Click,       // click mouse button at (x, y); x/y < 0 = current position
        Move,        // move mouse to (x, y)
        Wait,        // sleep for ms milliseconds
        TypeText     // type text
    };

    Type        type   = Type::Key;
    std::string key;            // Key/KeyDown/KeyUp: key name; Click: button name
    std::string text;           // TypeText payload
    int         x      = -1;
    int         y      = -1;
    int         ms     = 0;     // Wait duration; also used as settle delay

    static const char* typeName(Type t);
    static Type        typeFromName(const std::string& n, Type fallback = Type::Key);
};

struct MacroTrigger {
    enum class Type { None, PixelColor, Button };

    Type        type       = Type::None;
    int         x          = 0;     // PixelColor: screen coordinate
    int         y          = 0;
    int         r          = 255;   // PixelColor: expected colour
    int         g          = 255;
    int         b          = 255;
    int         tolerance  = 10;    // per-channel tolerance
    std::string button;             // Button: key name ("space") or "left"/"right"/"middle"
    int         intervalMs = 250;   // poll interval
    int         cooldownMs = 1000;  // minimum time between firings
    bool        once       = false; // fire a single time, then stop

    static const char* typeName(Type t);
    static Type        typeFromName(const std::string& n, Type fallback = Type::None);
};

struct Macro {
    enum class Mode { Timeline, Trigger };

    std::string              name     = "New Macro";
    Mode                     mode     = Mode::Timeline;
    bool                     enabled  = true;
    // Scope: "any" system matches every console; empty game matches the whole
    // system.  Both are matched against GameLibrary::systemName / displayName.
    std::string              system   = "any";
    std::string              game;
    bool                     loop        = false;  // Timeline: repeat forever
    int                      loopDelayMs = 0;      // delay between loop passes
    MacroTrigger             trigger;
    std::vector<MacroAction> actions;

    static const char* modeName(Mode m);
    static Mode        modeFromName(const std::string& n, Mode fallback = Mode::Timeline);

    // True when this macro applies to the given system/game names.
    bool matches(const std::string& systemName, const std::string& gameName) const;
};

// ─── Persistent store ─────────────────────────────────────────────────────────

class MacroStore {
public:
    explicit MacroStore(std::string dir = "saves/macros") : dir_(std::move(dir)) {}

    const std::string& dir() const { return dir_; }

    std::vector<Macro> loadAll() const;
    bool               save(const Macro& macro) const;   // creates dir as needed
    bool               remove(const std::string& name) const;
    // Enabled macros applying to this system/game ("any"/"" wildcards honoured).
    std::vector<Macro> forGame(const std::string& systemName,
                               const std::string& gameName) const;

    static std::string fileNameFor(const std::string& macroName);

private:
    std::string dir_;
};

// ─── Serialisation (exposed for the engine hand-off and tests) ───────────────
std::string macroToJson(const Macro& macro);
bool        macroFromJson(const std::string& json, Macro& out);
