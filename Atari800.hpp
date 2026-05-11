#ifndef ATARI800_HPP
#define ATARI800_HPP

#include "top.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

class Atari800Backend;

struct atari800_input_t {
    bool up;
    bool down;
    bool left;
    bool right;
    bool fire;
    bool secondary;
    bool select;
    bool start;
    bool option;
};

struct atari800_color_t {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

class Atari800Emulation
{
private:
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
        SDL_Keycode option;
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
        SDL_GameControllerButton option;
    };

    std::unique_ptr<Atari800Backend> backend;
    string romPath;
    std::atomic<bool> isRunning;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* screenTexture;
    SDL_GameController* controller;
    int controllerDeviceIndex;
    int scaleX;
    int scaleY;
    std::vector<unsigned char> frameBuffer;
    std::vector<unsigned char> latestFrame;
    std::thread emulationThread;
    std::mutex inputMutex;
    mutable std::mutex frameMutex;
    atari800_input_t workerInput;
    std::atomic<bool> emulationThreadStopRequested;
    std::atomic<bool> emulationThreadRunning;
    std::atomic<bool> emulationThreadFailed;
    std::atomic<bool> frameReady;
    InputMode inputMode;
    KeyBindings keyBindings;
    ControllerBindings controllerBindings;

    static InputMode globalInputMode;
    static KeyBindings globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool globalInputConfigured;

    static constexpr int kAtari800ScreenWidth = 384;
    static constexpr int kAtari800ScreenHeight = 240;

    void initSDL();
    void cleanupSDL();
    void calculateScale(int screenWidth, int screenHeight);
    void renderFrame();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, atari800_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, atari800_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;
    bool startEmulationWorker();
    void stopEmulationWorker();
    void emulationWorkerLoop();

public:
    Atari800Emulation();
    ~Atari800Emulation();

    bool loadRom(const string& path);
    void unloadRom();
    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
    void nextFrame();
    void setInput(const atari800_input_t& input);
    atari800_color_t getPixel(int x, int y) const;
    void displayFrame() const;
    void runInteractive();

    static void configureGlobalControls();

    bool isLoaded() const { return backend != nullptr; }
    bool isEmulatorRunning() const { return isRunning.load(); }
    string getRomPath() const { return romPath; }
};

#endif