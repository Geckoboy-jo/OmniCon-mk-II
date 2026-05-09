#include "top.hpp"
#include "agnes.h"
#include <thread>
#include <chrono>
#include <array>
#include <vector>
#include <SDL2/SDL.h>

class NESEmulation
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
        SDL_GameControllerButton select;
        SDL_GameControllerButton start;
    };

    agnes_t* emulator;
    string romPath;
    string savePath;
    bool isRunning;
    
    // SDL2 components
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* screenTexture;
    SDL_GameController* controller;
    int controllerDeviceIndex;
    int scaleX;
    int scaleY;
    std::vector<unsigned char> frameBuffer;
    InputMode inputMode;
    KeyBindings keyBindings;
    ControllerBindings controllerBindings;
    
    void initSDL();
    void cleanupSDL();
    void calculateScale(int screenWidth, int screenHeight);
    void renderFrame();
    void setDefaultBindings();
    void configureInputMenu();
    void handleKeyboardEvent(SDL_Keycode key, bool pressed, agnes_input_t& input, bool& running);
    void handleControllerButtonEvent(Uint8 button, bool pressed, agnes_input_t& input, bool& running);
    bool openControllerByIndex(int index);
    void listControllers() const;

public:
    NESEmulation();
    ~NESEmulation();
    
    bool loadRom(const string& path);
    void unloadRom();
    void nextFrame();
    void setInput(const agnes_input_t& input);
    agnes_color_t getPixel(int x, int y) const;
    void displayFrame() const;
    void runInteractive();
    
    bool isLoaded() const { return emulator != nullptr; }
    bool isEmulatorRunning() const { return isRunning; }
    string getRomPath() const { return romPath; }
};