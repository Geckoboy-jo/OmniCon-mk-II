#ifndef GAMEBOY_COLOR_HPP
#define GAMEBOY_COLOR_HPP

#include "GameBoy.hpp"
#include <memory>

// GameBoyColorEmulation provides a dedicated Game Boy Color system entry point.
// It delegates to GameBoyEmulation (backed by SameBoy) and forces the GBC
// window title. SameBoy automatically selects GB_MODEL_CGB_E when the ROM's CGB
// flag (header offset 0x143 == 0x80 or 0xC0) is set, or when the file has a
// .gbc extension – so full hardware-accurate GBC emulation is provided
// automatically.
//
// GB (DMG) ROMs loaded through this class run in GBC compatibility mode,
// exactly as they would on real Game Boy Color hardware.
//
// Interface mirrors GameBoyEmulation so it can be used identically in main.cpp.

class GameBoyColorEmulation {
public:
  GameBoyColorEmulation();
  ~GameBoyColorEmulation() = default;

  // Load a .gb, .gbc, or .sgb ROM file. Returns true on success.
  bool loadRom(const string &path);

  void unloadRom();
  std::vector<unsigned char> dumpSaveData() const;
  bool restoreSaveData(const std::vector<unsigned char> &data);
  void nextFrame();
  void setInput(const sameboy_input_t &input);
  sameboy_color_t getPixel(int x, int y) const;
  void displayFrame() const;
  void runInteractive();

  // Shared with GameBoyEmulation – GBC uses the same button layout.
  static void configureGlobalControls();

  bool isLoaded() const;
  bool isEmulatorRunning() const;
  string getRomPath() const;

private:
  GameBoyEmulation impl;
};

#endif
