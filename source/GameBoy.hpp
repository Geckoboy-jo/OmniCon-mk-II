#ifndef GAMEBOY_HPP
#define GAMEBOY_HPP

#include "sameboy_color_t.hpp"
#include "sameboy_input_t.hpp"
#include "top.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class SameBoyBackend;

class GameBoyEmulation {
public:
  enum class InputMode { KeyboardOnly, ControllerOnly, KeyboardAndController };

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

  static InputMode globalInputMode;
  static KeyBindings globalKeyBindings;
  static ControllerBindings globalControllerBindings;
  static bool globalInputConfigured;

private:
  std::unique_ptr<SameBoyBackend> backend;
  string romPath;
  string windowTitle = "Game Boy Emulator - SameBoy";
  std::atomic<bool> isRunning;

  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *screenTexture;
  SDL_GameController *controller;
  int controllerDeviceIndex;
  int scaleX;
  int scaleY;
  std::vector<unsigned char> frameBuffer;
  std::vector<unsigned char> latestFrame;
  std::thread emulationThread;
  std::mutex inputMutex;
  mutable std::mutex frameMutex;
  sameboy_input_t workerInput;
  std::atomic<bool> emulationThreadStopRequested;
  std::atomic<bool> emulationThreadRunning;
  std::atomic<bool> emulationThreadFailed;
  std::atomic<bool> frameReady;
  InputMode inputMode;
  KeyBindings keyBindings;
  ControllerBindings controllerBindings;

  static constexpr int kGameBoyScreenWidth = 160;
  static constexpr int kGameBoyScreenHeight = 144;

  void initSDL();
  void cleanupSDL();
  void calculateScale(int screenWidth, int screenHeight);
  void renderFrame();
  void setDefaultBindings();
  void saveGlobalInputSettings();
  void configureInputMenu();
  void handleKeyboardEvent(SDL_Keycode key, bool pressed,
                           sameboy_input_t &input, bool &running);
  void handleControllerButtonEvent(Uint8 button, bool pressed,
                                   sameboy_input_t &input, bool &running);
  bool openControllerByIndex(int index);
  void listControllers() const;
  bool startEmulationWorker();
  void stopEmulationWorker();
  void emulationWorkerLoop();

public:
  GameBoyEmulation();
  ~GameBoyEmulation();

  bool loadRom(const string &path);
  void unloadRom();
  std::vector<unsigned char> dumpSaveData() const;
  bool restoreSaveData(const std::vector<unsigned char> &data);
  void nextFrame();
  void setInput(const sameboy_input_t &input);
  sameboy_color_t getPixel(int x, int y) const;
  void displayFrame() const;
  void runInteractive();

  static void configureGlobalControls();

  bool isLoaded() const { return backend != nullptr; }
  bool isEmulatorRunning() const { return isRunning.load(); }
  string getRomPath() const { return romPath; }
  void setWindowTitle(const string &title) { windowTitle = title; }
};

#endif
