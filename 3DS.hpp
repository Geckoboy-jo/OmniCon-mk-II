#ifndef N3DS_HPP
#define N3DS_HPP

#include "top.hpp"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>

struct n3ds_input_t {
    bool a;
    bool b;
    bool x;
    bool y;
    bool start;
    bool select;
    bool up;
    bool down;
    bool left;
    bool right;
    bool l;
    bool r;
    bool zl;
    bool zr;
    bool    touchPressed;
    int16_t touchX;   // libretro pointer range: -0x7fff to +0x7fff
    int16_t touchY;
};

struct n3ds_color_t {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

class N3DSEmulation
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
        SDL_Keycode zl;
        SDL_Keycode zr;
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
        SDL_GameControllerButton zl;
        SDL_GameControllerButton zr;
    };

    static InputMode          globalInputMode;
    static KeyBindings        globalKeyBindings;
    static ControllerBindings globalControllerBindings;
    static bool               globalInputConfigured;

private:

    bool romLoaded;
    std::unique_ptr<class N3DSBackend> backendPtr;
    string romPath;
    std::atomic<bool> isRunning;

    SDL_Window*          window;
    SDL_Renderer*        renderer;
    SDL_Texture*         screenTexture;
    SDL_GameController*  controller;
    std::vector<unsigned char> frameBuffer;
    SDL_Rect lastLetterboxRect;
    std::mutex inputMutex;
    n3ds_input_t workerInput;
    InputMode    inputMode;
    KeyBindings  keyBindings;
    ControllerBindings controllerBindings;

    void initSDL();
    void cleanupSDL();
    void renderFrameSDL();
    void setDefaultBindings();
    void saveGlobalInputSettings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, n3ds_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, n3ds_input_t& input, bool& running);
    void handleMouseEvent(int winX, int winY, bool pressed, n3ds_input_t& input);
    bool openControllerByIndex(int index);
    void listControllers() const;

public:
    N3DSEmulation();
    ~N3DSEmulation();

    bool loadRom(const string& path);
    void unloadRom();
    void runInteractive();

    static void configureGlobalControls();

    bool   isLoaded()   const { return romLoaded && backendPtr != nullptr; }
    string getRomPath() const { return romPath; }
};

#endif // N3DS_HPP
