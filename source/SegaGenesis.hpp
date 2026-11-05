#pragma once

#include "top.hpp"
#include "genesis_input_t.hpp"
#include "genesis_color_t.hpp"


class GenesisBackend;

class SegaGenesisEmulation {
public:
    enum class InputMode {
        KeyboardOnly,
        ControllerOnly,
        KeyboardAndController,
    };

    struct KeyBindings {
        SDL_Keycode up;
        SDL_Keycode down;
        SDL_Keycode left;
        SDL_Keycode right;
        SDL_Keycode a;
        SDL_Keycode b;
        SDL_Keycode c;
        SDL_Keycode x;
        SDL_Keycode y;
        SDL_Keycode z;
        SDL_Keycode start;
        SDL_Keycode mode;
    };

    struct ControllerBindings {
        SDL_GameControllerButton up;
        SDL_GameControllerButton down;
        SDL_GameControllerButton left;
        SDL_GameControllerButton right;
        SDL_GameControllerButton a;
        SDL_GameControllerButton b;
        SDL_GameControllerButton c;
        SDL_GameControllerButton x;
        SDL_GameControllerButton y;
        SDL_GameControllerButton z;
        SDL_GameControllerButton start;
        SDL_GameControllerButton mode;
    };

    static InputMode         globalInputMode;
    static KeyBindings       globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool              globalInputConfigured;

private:
    std::unique_ptr<GenesisBackend> backend;
    std::string   romPath;
    std::atomic<bool> isRunning;

    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  screenTexture;
    SDL_GameController* controller;
    int           controllerDeviceIndex;
    int           scaleX;
    int           scaleY;

    std::vector<unsigned char> frameBuffer;
    std::vector<unsigned char> latestFrame;
    std::thread   emulationThread;
    std::mutex    inputMutex;
    mutable std::mutex frameMutex;
    genesis_input_t workerInput;

    std::atomic<bool> emulationThreadStopRequested;
    std::atomic<bool> emulationThreadRunning;
    std::atomic<bool> emulationThreadFailed;
    std::atomic<bool> frameReady;

    InputMode        inputMode;
    KeyBindings      keyBindings;
    ControllerBindings controllerBindings;

    static constexpr int kDefaultWidth  = 320;
    static constexpr int kDefaultHeight = 224;

    void initSDL(int emuWidth, int emuHeight);
    void cleanupSDL();
    void calculateScale(int screenW, int screenH, int emuW, int emuH);
    void renderFrame();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, genesis_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, genesis_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;
    bool startEmulationWorker();
    void stopEmulationWorker();
    void emulationWorkerLoop();

public:
    SegaGenesisEmulation();
    ~SegaGenesisEmulation();

    bool loadRom(const std::string& path);
    void unloadRom();
    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
    void runInteractive();

    static void configureGlobalControls();

    bool isLoaded()          const { return backend != nullptr; }
    bool isEmulatorRunning() const { return isRunning.load();   }
    std::string getRomPath() const { return romPath;            }
};
