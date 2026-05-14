#ifndef ATARI2600_HPP
#define ATARI2600_HPP

#include "top.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

class Atari2600Backend;

struct atari2600_input_t {
    bool up;
    bool down;
    bool left;
    bool right;
    bool fire;
    bool select;
    bool start;
    bool reset;
};

struct atari2600_color_t {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

class Atari2600Emulation
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
        SDL_GameControllerButton select;
        SDL_GameControllerButton start;
        SDL_GameControllerButton reset;
    };

    static InputMode globalInputMode;
    static KeyBindings globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool globalInputConfigured;

private:

    std::unique_ptr<Atari2600Backend> backend;
    string romPath;
    std::atomic<bool> isRunning;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* screenTexture;
    SDL_GameController* controller;
    std::vector<unsigned char> frameBuffer;
    std::mutex inputMutex;
    atari2600_input_t workerInput;
    InputMode inputMode;
    KeyBindings keyBindings;
    ControllerBindings controllerBindings;

    void initSDL();
    void cleanupSDL();
    void renderFrame();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, atari2600_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, atari2600_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;

public:
    Atari2600Emulation();
    ~Atari2600Emulation();

    bool loadRom(const string& path);
    void unloadRom();
    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
    void nextFrame();
    void setInput(const atari2600_input_t& input);
    atari2600_color_t getPixel(int x, int y) const;
    void displayFrame() const;
    void runInteractive();

    static void configureGlobalControls();

    bool isLoaded() const { return backend != nullptr; }
    bool isEmulatorRunning() const { return isRunning.load(); }
    string getRomPath() const { return romPath; }
};

#endif