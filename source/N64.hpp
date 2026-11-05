#pragma once

#include "top.hpp"
#include "n64_input_t.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <SDL2/SDL.h>

class Mupen64PlusBackend;

class N64Emulation {
public:
    enum class InputMode { KeyboardOnly, ControllerOnly, KeyboardAndController };

    struct KeyBindings {
        SDL_Keycode up, down, left, right;
        SDL_Keycode a, b;
        SDL_Keycode c_up, c_down, c_left, c_right;
        SDL_Keycode l, r, z;
        SDL_Keycode start;
    };

    struct ControllerBindings {
        SDL_GameControllerButton up, down, left, right;
        SDL_GameControllerButton a, b;
        SDL_GameControllerButton c_up, c_down, c_left, c_right;
        SDL_GameControllerButton l, r, z;
        SDL_GameControllerButton start;
    };

    static InputMode          globalInputMode;
    static KeyBindings        globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool               globalInputConfigured;

private:
    std::unique_ptr<Mupen64PlusBackend> backend;
    std::string       romPath;
    std::atomic<bool> isRunning{false};

public:
    N64Emulation();
    ~N64Emulation();

    bool loadRom(const std::string& path);
    void unloadRom();
    void runInteractive();
    static void configureGlobalControls();

    bool        isLoaded()   const;
    std::string getRomPath() const { return romPath; }

    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
};
