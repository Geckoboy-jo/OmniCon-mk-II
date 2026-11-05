#ifndef NDS_HPP
#define NDS_HPP

#include "top.hpp"
#include "nds_input_t.hpp"
#include "nds_color_t.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

class NDSEmulation
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
        SDL_Keycode a;
        SDL_Keycode b;
        SDL_Keycode x;
        SDL_Keycode y;
        SDL_Keycode start;
        SDL_Keycode select;
        SDL_Keycode l;
        SDL_Keycode r;
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
        SDL_GameControllerButton start;
        SDL_GameControllerButton select;
        SDL_GameControllerButton l;
        SDL_GameControllerButton r;
    };

    static InputMode         globalInputMode;
    static KeyBindings       globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool              globalInputConfigured;

private:

    bool romLoaded;
    std::unique_ptr<class NDSBackend> backendPtr;
    string romPath;
    std::atomic<bool> isRunning;

    SDL_Window*          window;
    SDL_Renderer*        renderer;
    SDL_Texture*         screenTexture;
    SDL_GameController*  controller;
    std::vector<unsigned char> frameBuffer;
    SDL_Rect lastLetterboxRect;
    std::mutex inputMutex;
    nds_input_t workerInput;
    InputMode   inputMode;
    KeyBindings keyBindings;
    ControllerBindings controllerBindings;

    void initSDL();
    void cleanupSDL();
    void renderFrameSDL();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, nds_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, nds_input_t& input, bool& running);
    void handleMouseEvent(int winX, int winY, bool pressed, nds_input_t& input);
    bool openControllerByIndex(int index);
    void listControllers() const;

public:
    NDSEmulation();
    ~NDSEmulation();

    bool loadRom(const string& path);
    void unloadRom();
    void runInteractive();

    static void configureGlobalControls();

    bool   isLoaded()   const { return romLoaded && backendPtr != nullptr; }
    string getRomPath() const { return romPath; }

    std::vector<unsigned char> dumpSaveData() const;
    bool restoreSaveData(const std::vector<unsigned char>& data);
};

#endif // NDS_HPP
