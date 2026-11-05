#ifndef GAMECUBE_HPP
#define GAMECUBE_HPP

#include "gamecube_color_t.hpp"
#include "gamecube_input_t.hpp"
#include "top.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class GameCubeEmulation {
public:
  enum class InputMode { KeyboardOnly, ControllerOnly, KeyboardAndController };

  struct KeyBindings {
    SDL_Keycode up;
    SDL_Keycode down;
    SDL_Keycode left;
    SDL_Keycode right;
    SDL_Keycode cross;
    SDL_Keycode circle;
    SDL_Keycode square;
    SDL_Keycode triangle;
    SDL_Keycode start;
    SDL_Keycode select;
    SDL_Keycode l1;
    SDL_Keycode r1;
    SDL_Keycode l2;
    SDL_Keycode r2;
  };

  struct ControllerBindings {
    SDL_GameControllerButton up;
    SDL_GameControllerButton down;
    SDL_GameControllerButton left;
    SDL_GameControllerButton right;
    SDL_GameControllerButton cross;
    SDL_GameControllerButton circle;
    SDL_GameControllerButton square;
    SDL_GameControllerButton triangle;
    SDL_GameControllerButton start;
    SDL_GameControllerButton select;
    SDL_GameControllerButton l1;
    SDL_GameControllerButton r1;
  };

  static InputMode globalInputMode;
  static KeyBindings globalKeyBindings;
  static ControllerBindings globalControllerBindings;
  static bool globalInputConfigured;

private:
  bool romLoaded;
  std::unique_ptr<class GameCubeBackend> backendPtr;
  string romPath;
  std::atomic<bool> isRunning;

  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *screenTexture;
  SDL_GameController *controller;
  std::vector<unsigned char> frameBuffer;
  SDL_Rect lastLetterboxRect;
  std::mutex inputMutex;
  gamecube_input_t workerInput;
  InputMode inputMode;
  KeyBindings keyBindings;
  ControllerBindings controllerBindings;

  void initSDL();
  void cleanupSDL();
  void renderFrameSDL();
  void setDefaultBindings();
  void saveGlobalInputSettings();
  void configureInputMenu();
  void handleKeyboardEvent(SDL_Keycode key, bool pressed,
                           gamecube_input_t &input, bool &running);
  void handleControllerButtonEvent(Uint8 button, bool pressed,
                                   gamecube_input_t &input, bool &running);
  bool openControllerByIndex(int index);
  void listControllers() const;

public:
  GameCubeEmulation();
  ~GameCubeEmulation();

  bool loadRom(const string &path);
  void unloadRom();
  void runInteractive();

  static void configureGlobalControls();

  bool isLoaded() const { return romLoaded && backendPtr != nullptr; }
  string getRomPath() const { return romPath; }

  std::vector<unsigned char> dumpSaveData() const;
  bool restoreSaveData(const std::vector<unsigned char> &data);
};

#endif // GAMECUBE_HPP
