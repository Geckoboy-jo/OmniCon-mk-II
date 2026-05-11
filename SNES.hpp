#ifndef SNES_HPP
#define SNES_HPP

#include "top.hpp"
#include <array>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

class SuperPyBackend;

struct superpy_input_t {
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;
    bool b;
    bool x;
    bool y;
    bool l;
    bool r;
    bool select;
    bool start;
};

struct superpy_color_t {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

class SNESEmulation
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
        SDL_Keycode a;
        SDL_Keycode b;
        SDL_Keycode x;
        SDL_Keycode y;
        SDL_Keycode l;
        SDL_Keycode r;
        SDL_Keycode select;
        SDL_Keycode start;
    };

    struct ControllerBindings {
        SDL_GameControllerButton up;
        SDL_GameControllerButton down;
        SDL_GameControllerButton left;
        SDL_GameControllerButton right;
        SDL_GameControllerButton a;
        SDL_GameControllerButton b;
        SDL_GameControllerButton x;
        SDL_GameControllerButton y;
        SDL_GameControllerButton l;
        SDL_GameControllerButton r;
        SDL_GameControllerButton select;
        SDL_GameControllerButton start;
    };

    std::unique_ptr<SuperPyBackend> backend;
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
    superpy_input_t workerInput;
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

    static constexpr int kSNESScreenWidth = 256;
    static constexpr int kSNESScreenHeight = 224;

    void initSDL();
    void cleanupSDL();
    void calculateScale(int screenWidth, int screenHeight);
    void renderFrame();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, superpy_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, superpy_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;
    bool startEmulationWorker();
    void stopEmulationWorker();
    void emulationWorkerLoop();

public:
    SNESEmulation();
    ~SNESEmulation();

    bool loadRom(const string& path);
    void unloadRom();
    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
    void nextFrame();
    void setInput(const superpy_input_t& input);
    superpy_color_t getPixel(int x, int y) const;
    void displayFrame() const;
    void runInteractive();

    static void configureGlobalControls();

    bool isLoaded() const { return backend != nullptr; }
    bool isEmulatorRunning() const { return isRunning.load(); }
    string getRomPath() const { return romPath; }
};

#endif
