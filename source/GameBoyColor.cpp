#include "GameBoyColor.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace {

std::string gbcToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

} // namespace

GameBoyColorEmulation::GameBoyColorEmulation() {
  // Override the default "Game Boy Emulator" window title to identify this as
  // the Game Boy Color entry point.
  impl.setWindowTitle("Game Boy Color Emulator - SameBoy");
}

bool GameBoyColorEmulation::loadRom(const string &path) {
  if (path.empty()) {
    std::cerr << "Game Boy Color: ROM path is empty.\n";
    return false;
  }

  if (!std::filesystem::exists(path)) {
    std::cerr << "Game Boy Color: ROM file not found: " << path << "\n";
    return false;
  }

  // Accept .gb, .gbc, and .sgb – GBC hardware runs all three formats.
  const std::string ext =
      gbcToLower(std::filesystem::path(path).extension().string());
  if (ext != ".gb" && ext != ".gbc" && ext != ".sgb") {
    std::cerr << "Game Boy Color: unsupported file extension '" << ext
              << "'. Expected .gb, .gbc, or .sgb.\n";
    return false;
  }

  const bool ok = impl.loadRom(path);
  return ok;
}

void GameBoyColorEmulation::unloadRom() { impl.unloadRom(); }

std::vector<unsigned char> GameBoyColorEmulation::dumpSaveData() const {
  return impl.dumpSaveData();
}

bool GameBoyColorEmulation::restoreSaveData(
    const std::vector<unsigned char> &data) {
  return impl.restoreSaveData(data);
}

void GameBoyColorEmulation::nextFrame() { impl.nextFrame(); }

void GameBoyColorEmulation::setInput(const sameboy_input_t &input) {
  impl.setInput(input);
}

sameboy_color_t GameBoyColorEmulation::getPixel(int x, int y) const {
  return impl.getPixel(x, y);
}

void GameBoyColorEmulation::displayFrame() const { impl.displayFrame(); }

void GameBoyColorEmulation::runInteractive() { impl.runInteractive(); }

void GameBoyColorEmulation::configureGlobalControls() {
  // GBC uses the same control layout as DMG Game Boy – share one config.
  GameBoyEmulation::configureGlobalControls();
}

bool GameBoyColorEmulation::isLoaded() const { return impl.isLoaded(); }

bool GameBoyColorEmulation::isEmulatorRunning() const {
  return impl.isEmulatorRunning();
}

string GameBoyColorEmulation::getRomPath() const { return impl.getRomPath(); }
