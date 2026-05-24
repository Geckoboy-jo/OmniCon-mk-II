#ifndef GBA_HPP
#define GBA_HPP

#include "top.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

struct mCore;

struct gba_input_t {
    bool a;
    bool b;
    bool select;
    bool start;
    bool right;
    bool left;
    bool up;
    bool down;
    bool r;
    bool l;
};

class GBAEmulation
{
public:
    enum class InputMode {
        KeyboardOnly,
        ControllerOnly,
        KeyboardAndController
    };

    struct KeyBindings {
        SDL_Keycode a;
        SDL_Keycode b;
        SDL_Keycode select;
        SDL_Keycode start;
        SDL_Keycode right;
        SDL_Keycode left;
        SDL_Keycode up;
        SDL_Keycode down;
        SDL_Keycode r;
        SDL_Keycode l;
    };

    struct ControllerBindings {
        SDL_GameControllerButton a;
        SDL_GameControllerButton b;
        SDL_GameControllerButton select;
        SDL_GameControllerButton start;
        SDL_GameControllerButton right;
        SDL_GameControllerButton left;
        SDL_GameControllerButton up;
        SDL_GameControllerButton down;
        SDL_GameControllerButton r;
        SDL_GameControllerButton l;
    };

    static InputMode globalInputMode;
    static KeyBindings globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool globalInputConfigured;

private:

    struct mCore* core = nullptr;
    std::vector<uint32_t> mgbaFrameBuffer;  // mGBA writes here each frame
    std::string romPath;
    std::string windowTitle = "Game Boy Advance - mGBA";
    std::atomic<bool> isRunning{false};

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* screenTexture = nullptr;
    SDL_GameController* controller = nullptr;
    int controllerDeviceIndex = -1;
    int scaleX = 3;
    int scaleY = 3;

    SDL_AudioDeviceID audioDevice = 0;
    int audioSampleRate = 48000;
    bool audioPrimed = false;
    int targetPrebufferMs = 120;
    int underrunEvents = 0;
    int stablePulls = 0;
    std::vector<int16_t> audioRing;
    size_t audioRingRead = 0;
    size_t audioRingWrite = 0;
    size_t audioRingCount = 0;
    int16_t lastAudioLeft = 0;
    int16_t lastAudioRight = 0;
    std::vector<int16_t> rateAdjustedBatch;
    double rateControlPhase = 0.0;
    double rateControlRatio = 1.0;
    std::vector<int16_t> pendingAudioSamples;

    std::vector<uint32_t> latestFrame;
    std::thread emulationThread;
    std::mutex inputMutex;
    mutable std::mutex frameMutex;
    gba_input_t workerInput{};
    std::atomic<bool> emulationThreadStopRequested{false};
    std::atomic<bool> emulationThreadRunning{false};
    std::atomic<bool> emulationThreadFailed{false};
    std::atomic<bool> frameReady{false};

    InputMode inputMode = InputMode::KeyboardOnly;
    KeyBindings keyBindings{};
    ControllerBindings controllerBindings{};

    static constexpr int kScreenWidth  = 240;
    static constexpr int kScreenHeight = 160;
    static constexpr double kNominalFps = 59.7275;

    void initSDL();
    void cleanupSDL();
    void calculateScale(int screenWidth, int screenHeight);
    void renderFrame();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, gba_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, gba_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;
    bool startEmulationWorker();
    void stopEmulationWorker();
    void emulationWorkerLoop();

    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioSamples(const int16_t* data, size_t frames);
    void onAudioPull(Uint8* stream, int len);
    const int16_t* applyRateControl(const int16_t* data, size_t frames, size_t& outFrames, int bufferedFramesBefore);
    static void sdlAudioPullCb(void* userdata, Uint8* stream, int len);

public:
    GBAEmulation();
    ~GBAEmulation();

    bool loadRom(const std::string& path);
    void unloadRom();
    void runInteractive();

    static void configureGlobalControls();

    bool isLoaded() const { return core != nullptr; }
    std::string getRomPath() const { return romPath; }
    void setWindowTitle(const std::string& title) { windowTitle = title; }

    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
};

#endif
