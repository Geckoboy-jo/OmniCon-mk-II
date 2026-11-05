#ifndef ATARI5200_HPP
#define ATARI5200_HPP

#include "top.hpp"
#include "atari5200_input_t.hpp"
#include "atari5200_color_t.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

class Atari5200Emulation
{
public:
    enum class InputMode {
        KeyboardOnly,
        ControllerOnly,
        KeyboardAndController
    };

    struct KeyBindings {
        SDL_Keycode up;
        SDL_Keycode down;
        SDL_Keycode left;
        SDL_Keycode right;
        SDL_Keycode fire;
        SDL_Keycode secondary;
        SDL_Keycode select;
        SDL_Keycode start;
        SDL_Keycode reset;
    };

    struct ControllerBindings {
        SDL_GameControllerButton up;
        SDL_GameControllerButton down;
        SDL_GameControllerButton left;
        SDL_GameControllerButton right;
        SDL_GameControllerButton fire;
        SDL_GameControllerButton secondary;
        SDL_GameControllerButton select;
        SDL_GameControllerButton start;
        SDL_GameControllerButton reset;
    };

    static InputMode globalInputMode;
    static KeyBindings globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool globalInputConfigured;

private:

    bool romLoaded;
    std::unique_ptr<class Atari5200Backend> backendPtr;
    string romPath;
    std::atomic<bool> isRunning;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* screenTexture;
    SDL_GameController* controller;
    std::vector<unsigned char> frameBuffer;
    std::mutex inputMutex;
    atari5200_input_t workerInput;
    InputMode inputMode;
    KeyBindings keyBindings;
    ControllerBindings controllerBindings;

    void initSDL();
    void cleanupSDL();
    void renderFrameSDL();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, atari5200_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, atari5200_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;

public:
    Atari5200Emulation();
    ~Atari5200Emulation();

    bool loadRom(const string& path);
    void unloadRom();
    void runInteractive();

    static void configureGlobalControls();

    bool isLoaded() const { return romLoaded && backendPtr != nullptr; }
    bool isEmulatorRunning() const { return isRunning.load(); }
    string getRomPath() const { return romPath; }

    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
};

#endif // ATARI5200_HPP