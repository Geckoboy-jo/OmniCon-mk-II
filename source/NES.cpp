#include "NES.hpp"
#include "SaveManager.hpp"
#include "audio_clock.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {

std::string trim(const std::string &value) {
  const std::string whitespace = " \t\r\n";
  const std::size_t start = value.find_first_not_of(whitespace);
  if (start == std::string::npos) {
    return "";
  }
  const std::size_t end = value.find_last_not_of(whitespace);
  return value.substr(start, end - start + 1);
}

std::string toUpper(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

SDL_GameControllerButton parseControllerButtonName(const std::string &input) {
  const std::string name = toUpper(trim(input));
  if (name == "A")
    return SDL_CONTROLLER_BUTTON_A;
  if (name == "B")
    return SDL_CONTROLLER_BUTTON_B;
  if (name == "X")
    return SDL_CONTROLLER_BUTTON_X;
  if (name == "Y")
    return SDL_CONTROLLER_BUTTON_Y;
  if (name == "BACK")
    return SDL_CONTROLLER_BUTTON_BACK;
  if (name == "GUIDE")
    return SDL_CONTROLLER_BUTTON_GUIDE;
  if (name == "START")
    return SDL_CONTROLLER_BUTTON_START;
  if (name == "LEFTSTICK")
    return SDL_CONTROLLER_BUTTON_LEFTSTICK;
  if (name == "RIGHTSTICK")
    return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
  if (name == "LEFTSHOULDER")
    return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  if (name == "RIGHTSHOULDER")
    return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  if (name == "DPAD_UP")
    return SDL_CONTROLLER_BUTTON_DPAD_UP;
  if (name == "DPAD_DOWN")
    return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  if (name == "DPAD_LEFT")
    return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  if (name == "DPAD_RIGHT")
    return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  return SDL_CONTROLLER_BUTTON_INVALID;
}

const char *controllerButtonName(SDL_GameControllerButton button) {
  const char *name = SDL_GameControllerGetStringForButton(button);
  return name ? name : "unknown";
}

} // namespace

class NesBackend {
public:
  virtual ~NesBackend() = default;
  virtual bool loadRom(const std::string &path) = 0;
  virtual bool nextFrame() = 0;
  virtual void setInput(const agnes_input_t &input) = 0;
  virtual std::vector<unsigned char> dumpSaveData() const = 0;
  virtual bool restoreSaveData(const std::vector<unsigned char> &data) = 0;
  virtual agnes_color_t getPixel(int x, int y) const = 0;
  virtual int getAudioSampleRate() const { return 48000; }
  virtual double getNominalFps() const { return 60.0988; }
};

class AgnesBackend final : public NesBackend {
public:
  AgnesBackend() : emulator(agnes_make()) {}

  ~AgnesBackend() override {
    if (emulator) {
      agnes_destroy(emulator);
      emulator = nullptr;
    }
  }

  bool isReady() const { return emulator != nullptr; }

  bool loadRom(const std::string &path) override {
    if (!emulator) {
      return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      return false;
    }

    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    romData.assign(size, 0);
    if (!file.read(reinterpret_cast<char *>(romData.data()),
                   static_cast<std::streamsize>(size))) {
      romData.clear();
      return false;
    }

    return agnes_load_ines_data(emulator, romData.data(), romData.size());
  }

  bool nextFrame() override { return emulator && agnes_next_frame(emulator); }

  void setInput(const agnes_input_t &input) override {
    if (emulator) {
      agnes_set_input(emulator, &input, nullptr);
    }
  }

  std::vector<unsigned char> dumpSaveData() const override {
    if (!emulator) {
      return {};
    }

    const size_t stateSize = agnes_state_size();
    if (stateSize == 0) {
      return {};
    }

    std::vector<unsigned char> state(stateSize, 0);
    agnes_state_t *statePtr = reinterpret_cast<agnes_state_t *>(state.data());
    agnes_dump_state(emulator, statePtr);
    return state;
  }

  bool restoreSaveData(const std::vector<unsigned char> &data) override {
    if (!emulator) {
      return false;
    }

    const size_t expectedSize = agnes_state_size();
    if (data.size() != expectedSize || expectedSize == 0) {
      return false;
    }

    const agnes_state_t *statePtr =
        reinterpret_cast<const agnes_state_t *>(data.data());
    return agnes_restore_state(emulator, statePtr);
  }

  agnes_color_t getPixel(int x, int y) const override {
    if (!emulator || x < 0 || x >= AGNES_SCREEN_WIDTH || y < 0 ||
        y >= AGNES_SCREEN_HEIGHT) {
      return {0, 0, 0, 255};
    }

    return agnes_get_screen_pixel(emulator, x, y);
  }

private:
  agnes_t *emulator = nullptr;
  std::vector<unsigned char> romData;
};

namespace libretro {
static constexpr unsigned ENV_GET_CAN_DUPE = 3;
static constexpr unsigned ENV_SET_PIXEL_FORMAT = 10;
static constexpr unsigned ENV_GET_VARIABLE = 15;
static constexpr unsigned ENV_SET_VARIABLES = 16;
static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
static constexpr unsigned ENV_SET_SUPPORT_NO_GAME = 18;
static constexpr unsigned ENV_GET_LOG_INTERFACE = 27;
static constexpr unsigned ENV_SET_GEOMETRY = 37;

static constexpr unsigned PIXEL_0RGB1555 = 0;
static constexpr unsigned PIXEL_XRGB8888 = 1;
static constexpr unsigned PIXEL_RGB565 = 2;

static constexpr unsigned DEVICE_JOYPAD = 1;
static constexpr unsigned JOYPAD_B = 0;
static constexpr unsigned JOYPAD_SELECT = 2;
static constexpr unsigned JOYPAD_START = 3;
static constexpr unsigned JOYPAD_UP = 4;
static constexpr unsigned JOYPAD_DOWN = 5;
static constexpr unsigned JOYPAD_LEFT = 6;
static constexpr unsigned JOYPAD_RIGHT = 7;
static constexpr unsigned JOYPAD_A = 8;

struct game_info {
  const char *path;
  const void *data;
  size_t size;
  const char *meta;
};
struct system_info {
  const char *library_name;
  const char *library_version;
  const char *valid_extensions;
  bool need_fullpath;
  bool block_extract;
};
struct game_geometry {
  unsigned base_width;
  unsigned base_height;
  unsigned max_width;
  unsigned max_height;
  float aspect_ratio;
};
struct system_timing {
  double fps;
  double sample_rate;
};
struct av_info {
  game_geometry geometry;
  system_timing timing;
};

using environment_t = bool (*)(unsigned cmd, void *data);
using video_refresh_t = void (*)(const void *data, unsigned width,
                                 unsigned height, size_t pitch);
using audio_sample_t = void (*)(int16_t left, int16_t right);
using audio_sample_batch_t = size_t (*)(const int16_t *data, size_t frames);
using input_poll_t = void (*)(void);
using input_state_t = int16_t (*)(unsigned port, unsigned device,
                                  unsigned index, unsigned id);
} // namespace libretro

class LibretroNesBackend final : public NesBackend {
public:
  LibretroNesBackend();
  ~LibretroNesBackend() override;

  bool isAvailable() const { return coreHandle != nullptr; }

  bool loadRom(const std::string &path) override;
  bool nextFrame() override;
  void setInput(const agnes_input_t &input) override;
  std::vector<unsigned char> dumpSaveData() const override;
  bool restoreSaveData(const std::vector<unsigned char> &data) override;
  agnes_color_t getPixel(int x, int y) const override;
  int getAudioSampleRate() const override { return audioSampleRate; }
  double getNominalFps() const override { return coreFps; }

  bool onEnvironment(unsigned cmd, void *data);
  void onVideoRefresh(const void *data, unsigned width, unsigned height,
                      size_t pitch);

private:
  static std::string resolveCorePath();
  bool loadSymbols();
  bool ensureAudioDevice(int sampleRate);
  void closeAudioDevice();
  void queueAudioFrames(const int16_t *data, size_t frames);

  std::string corePath;
  void *coreHandle = nullptr;
  void (*fn_init)() = nullptr;
  void (*fn_deinit)() = nullptr;
  void (*fn_get_system_info)(libretro::system_info *) = nullptr;
  void (*fn_get_system_av_info)(libretro::av_info *) = nullptr;
  void (*fn_set_environment)(libretro::environment_t) = nullptr;
  void (*fn_set_video_refresh)(libretro::video_refresh_t) = nullptr;
  void (*fn_set_audio_sample)(libretro::audio_sample_t) = nullptr;
  void (*fn_set_audio_sample_batch)(libretro::audio_sample_batch_t) = nullptr;
  void (*fn_set_input_poll)(libretro::input_poll_t) = nullptr;
  void (*fn_set_input_state)(libretro::input_state_t) = nullptr;
  size_t (*fn_serialize_size)() = nullptr;
  bool (*fn_serialize)(void *, size_t) = nullptr;
  bool (*fn_unserialize)(const void *, size_t) = nullptr;
  bool (*fn_load_game)(const libretro::game_info *) = nullptr;
  void (*fn_unload_game)() = nullptr;
  void (*fn_run)() = nullptr;

  std::vector<uint8_t> framePixels;
  int frameWidth = AGNES_SCREEN_WIDTH;
  int frameHeight = AGNES_SCREEN_HEIGHT;
  agnes_input_t currentInput = {};
  unsigned pixelFormat = libretro::PIXEL_0RGB1555;
  bool gameLoaded = false;
  std::vector<uint8_t> romData;
  std::string loadedRomPath;

  SDL_AudioDeviceID audioDevice = 0;
  SDL_AudioStream *audioStream = nullptr;
  bool devicePausedUntilFirstSamples = false;
  int coreSampleRate = 48000;
  int audioSampleRate = 48000;
  double coreFps = 60.0;
  std::vector<uint8_t> audioConvertBuffer;
  std::mutex audioBufferMutex;
  std::vector<int16_t> audioBuffer;
  size_t audioReadPos = 0;
  size_t maxBufferedSamples = 0;

  static LibretroNesBackend *s_instance;
  static bool s_envCb(unsigned cmd, void *data);
  static void s_videoCb(const void *data, unsigned width, unsigned height,
                        size_t pitch);
  static void s_audioCb(int16_t left, int16_t right);
  static size_t s_audioBatchCb(const int16_t *data, size_t frames);
  static void s_inputPollCb();
  static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index,
                                unsigned id);
  static void s_audioCallback(void *userdata, Uint8 *stream, int len);
};

LibretroNesBackend *LibretroNesBackend::s_instance = nullptr;

std::string LibretroNesBackend::resolveCorePath() {
  const char *envPath = std::getenv("OMNICON_NES_CORE");
  if (envPath && *envPath && std::filesystem::exists(envPath)) {
    return envPath;
  }

  const std::array<const char *, 3> workspaceCandidates = {
      "third_party/libretro/fceumm_libretro.so",
      "third_party/libretro/nestopia_libretro.so",
      "third_party/libretro/quicknes_libretro.so"};
  for (const char *candidate : workspaceCandidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  const std::array<const char *, 3> systemCandidates = {
      "/usr/lib/x86_64-linux-gnu/libretro/fceumm_libretro.so",
      "/usr/lib/x86_64-linux-gnu/libretro/nestopia_libretro.so",
      "/usr/lib/x86_64-linux-gnu/libretro/quicknes_libretro.so"};
  for (const char *candidate : systemCandidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  return std::string();
}

LibretroNesBackend::LibretroNesBackend() {
  corePath = resolveCorePath();
  if (corePath.empty()) {
    std::cerr << "[NES] No libretro NES core found. Set OMNICON_NES_CORE to "
                 "enable NES audio.\n";
    return;
  }

  coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
  if (!coreHandle) {
    std::cerr << "[NES] Failed to load core: " << corePath << ": " << dlerror()
              << "\n";
    return;
  }

  if (!loadSymbols()) {
    dlclose(coreHandle);
    coreHandle = nullptr;
    return;
  }

  s_instance = this;
  fn_set_environment(s_envCb);
  fn_set_video_refresh(s_videoCb);
  fn_set_audio_sample(s_audioCb);
  fn_set_audio_sample_batch(s_audioBatchCb);
  fn_set_input_poll(s_inputPollCb);
  fn_set_input_state(s_inputStateCb);
  fn_init();
  framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
}

LibretroNesBackend::~LibretroNesBackend() {
  closeAudioDevice();
  if (coreHandle) {
    if (gameLoaded) {
      fn_unload_game();
    }
    fn_deinit();
    dlclose(coreHandle);
    coreHandle = nullptr;
  }
  if (s_instance == this) {
    s_instance = nullptr;
  }
}

bool LibretroNesBackend::loadSymbols() {
  auto load = [this](const char *name) -> void * {
    void *sym = dlsym(coreHandle, name);
    if (!sym) {
      std::cerr << "[NES] Missing symbol: " << name << "\n";
    }
    return sym;
  };

  fn_init = reinterpret_cast<void (*)()>(load("retro_init"));
  fn_deinit = reinterpret_cast<void (*)()>(load("retro_deinit"));
  fn_get_system_info = reinterpret_cast<void (*)(libretro::system_info *)>(
      load("retro_get_system_info"));
  fn_get_system_av_info = reinterpret_cast<void (*)(libretro::av_info *)>(
      load("retro_get_system_av_info"));
  fn_set_environment = reinterpret_cast<void (*)(libretro::environment_t)>(
      load("retro_set_environment"));
  fn_set_video_refresh = reinterpret_cast<void (*)(libretro::video_refresh_t)>(
      load("retro_set_video_refresh"));
  fn_set_audio_sample = reinterpret_cast<void (*)(libretro::audio_sample_t)>(
      load("retro_set_audio_sample"));
  fn_set_audio_sample_batch =
      reinterpret_cast<void (*)(libretro::audio_sample_batch_t)>(
          load("retro_set_audio_sample_batch"));
  fn_set_input_poll = reinterpret_cast<void (*)(libretro::input_poll_t)>(
      load("retro_set_input_poll"));
  fn_set_input_state = reinterpret_cast<void (*)(libretro::input_state_t)>(
      load("retro_set_input_state"));
  fn_serialize_size =
      reinterpret_cast<size_t (*)()>(load("retro_serialize_size"));
  fn_serialize =
      reinterpret_cast<bool (*)(void *, size_t)>(load("retro_serialize"));
  fn_unserialize = reinterpret_cast<bool (*)(const void *, size_t)>(
      load("retro_unserialize"));
  fn_load_game = reinterpret_cast<bool (*)(const libretro::game_info *)>(
      load("retro_load_game"));
  fn_unload_game = reinterpret_cast<void (*)()>(load("retro_unload_game"));
  fn_run = reinterpret_cast<void (*)()>(load("retro_run"));

  return fn_init && fn_deinit && fn_get_system_info && fn_get_system_av_info &&
         fn_set_environment && fn_set_video_refresh && fn_set_audio_sample &&
         fn_set_audio_sample_batch && fn_set_input_poll && fn_set_input_state &&
         fn_serialize_size && fn_serialize && fn_unserialize && fn_load_game &&
         fn_unload_game && fn_run;
}

bool LibretroNesBackend::onEnvironment(unsigned cmd, void *data) {
  switch (cmd) {
  case libretro::ENV_GET_CAN_DUPE:
    *static_cast<bool *>(data) = true;
    return true;
  case libretro::ENV_SET_PIXEL_FORMAT:
    pixelFormat = *static_cast<unsigned *>(data);
    return true;
  case libretro::ENV_GET_VARIABLE:
    return false;
  case libretro::ENV_SET_VARIABLES:
    return true;
  case libretro::ENV_GET_VARIABLE_UPDATE:
    *static_cast<bool *>(data) = false;
    return true;
  case libretro::ENV_SET_SUPPORT_NO_GAME:
    return true;
  case libretro::ENV_GET_LOG_INTERFACE:
    return false;
  case libretro::ENV_SET_GEOMETRY: {
    const auto *geom = static_cast<const libretro::game_geometry *>(data);
    frameWidth = static_cast<int>(geom->base_width);
    frameHeight = static_cast<int>(geom->base_height);
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    return true;
  }
  default:
    return false;
  }
}

void LibretroNesBackend::onVideoRefresh(const void *data, unsigned width,
                                        unsigned height, size_t pitch) {
  if (!data) {
    return;
  }

  if (static_cast<int>(width) != frameWidth ||
      static_cast<int>(height) != frameHeight) {
    frameWidth = static_cast<int>(width);
    frameHeight = static_cast<int>(height);
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
  }

  const auto *src = static_cast<const uint8_t *>(data);
  for (unsigned row = 0; row < height; ++row) {
    const uint8_t *srcRow = src + row * pitch;
    uint8_t *dstRow =
        framePixels.data() + row * static_cast<size_t>(frameWidth) * 4;
    if (pixelFormat == libretro::PIXEL_XRGB8888) {
      for (unsigned col = 0; col < width; ++col) {
        uint32_t px;
        std::memcpy(&px, srcRow + col * 4, 4);
        dstRow[col * 4 + 0] = static_cast<uint8_t>((px >> 16) & 0xFF);
        dstRow[col * 4 + 1] = static_cast<uint8_t>((px >> 8) & 0xFF);
        dstRow[col * 4 + 2] = static_cast<uint8_t>(px & 0xFF);
        dstRow[col * 4 + 3] = 255;
      }
    } else if (pixelFormat == libretro::PIXEL_RGB565) {
      for (unsigned col = 0; col < width; ++col) {
        uint16_t px;
        std::memcpy(&px, srcRow + col * 2, 2);
        dstRow[col * 4 + 0] =
            static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
        dstRow[col * 4 + 1] =
            static_cast<uint8_t>(((px >> 5) & 0x3F) * 255 / 63);
        dstRow[col * 4 + 2] = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
        dstRow[col * 4 + 3] = 255;
      }
    } else {
      for (unsigned col = 0; col < width; ++col) {
        uint16_t px;
        std::memcpy(&px, srcRow + col * 2, 2);
        dstRow[col * 4 + 0] =
            static_cast<uint8_t>(((px >> 10) & 0x1F) * 255 / 31);
        dstRow[col * 4 + 1] =
            static_cast<uint8_t>(((px >> 5) & 0x1F) * 255 / 31);
        dstRow[col * 4 + 2] = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
        dstRow[col * 4 + 3] = 255;
      }
    }
  }
}

bool LibretroNesBackend::loadRom(const std::string &path) {
  if (!coreHandle) {
    return false;
  }

  if (gameLoaded) {
    fn_unload_game();
    gameLoaded = false;
    romData.clear();
    loadedRomPath.clear();
  }

  libretro::system_info sysInfo = {};
  fn_get_system_info(&sysInfo);

  loadedRomPath = path;
  libretro::game_info gameInfo = {};
  gameInfo.path = loadedRomPath.c_str();

  if (!sysInfo.need_fullpath) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      return false;
    }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    romData.resize(size);
    if (!file.read(reinterpret_cast<char *>(romData.data()),
                   static_cast<std::streamsize>(size))) {
      romData.clear();
      return false;
    }
    gameInfo.data = romData.data();
    gameInfo.size = romData.size();
  }

  if (!fn_load_game(&gameInfo)) {
    romData.clear();
    loadedRomPath.clear();
    return false;
  }

  gameLoaded = true;
  libretro::av_info avInfo = {};
  fn_get_system_av_info(&avInfo);
  if (avInfo.timing.fps > 1.0) {
    coreFps = avInfo.timing.fps;
  }
  frameWidth = static_cast<int>(avInfo.geometry.base_width);
  frameHeight = static_cast<int>(avInfo.geometry.base_height);
  if (frameWidth <= 0) {
    frameWidth = AGNES_SCREEN_WIDTH;
  }
  if (frameHeight <= 0) {
    frameHeight = AGNES_SCREEN_HEIGHT;
  }
  framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
  ensureAudioDevice(static_cast<int>(avInfo.timing.sample_rate));
  return true;
}

bool LibretroNesBackend::nextFrame() {
  if (!coreHandle || !gameLoaded) {
    return false;
  }
  fn_run();
  return true;
}

void LibretroNesBackend::setInput(const agnes_input_t &input) {
  currentInput = input;
}

std::vector<unsigned char> LibretroNesBackend::dumpSaveData() const {
  if (!coreHandle || !gameLoaded || !fn_serialize_size || !fn_serialize) {
    return {};
  }

  const size_t stateSize = fn_serialize_size();
  if (stateSize == 0) {
    return {};
  }

  std::vector<unsigned char> state(stateSize, 0);
  if (!fn_serialize(state.data(), stateSize)) {
    return {};
  }
  return state;
}

bool LibretroNesBackend::restoreSaveData(
    const std::vector<unsigned char> &data) {
  if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) {
    return false;
  }
  return fn_unserialize(data.data(), data.size());
}

agnes_color_t LibretroNesBackend::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) {
    return {0, 0, 0, 255};
  }
  const size_t offset =
      (static_cast<size_t>(y) * static_cast<size_t>(frameWidth) +
       static_cast<size_t>(x)) *
      4;
  if (offset + 3 >= framePixels.size()) {
    return {0, 0, 0, 255};
  }
  return {framePixels[offset], framePixels[offset + 1], framePixels[offset + 2],
          framePixels[offset + 3]};
}

bool LibretroNesBackend::ensureAudioDevice(int sampleRate) {
  if (sampleRate <= 0) {
    sampleRate = 48000;
  }

  if (audioDevice != 0 && sampleRate == coreSampleRate) {
    return true;
  }

  closeAudioDevice();

  if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      std::cerr << "[NES] Failed to initialize SDL audio subsystem: "
                << SDL_GetError() << "\n";
      return false;
    }
  }

  SDL_AudioSpec desired = {};
  desired.freq = sampleRate;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 512;
  desired.callback = s_audioCallback;
  desired.userdata = this;

  SDL_AudioSpec obtained = {};
  audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (audioDevice == 0) {
    std::cerr << "[NES] Failed to open SDL audio device: " << SDL_GetError()
              << "\n";
    return false;
  }

  coreSampleRate = sampleRate;
  audioSampleRate = obtained.freq > 0 ? obtained.freq : sampleRate;

  {
    std::lock_guard<std::mutex> lock(audioBufferMutex);
    audioBuffer.clear();
    audioBuffer.reserve(static_cast<size_t>(audioSampleRate) * 2 / 4);
    audioReadPos = 0;
    // Cap queued audio to ~120ms to avoid runaway AV lag.
    maxBufferedSamples = static_cast<size_t>(audioSampleRate) * 2 * 120 / 1000;
  }

  // Keep device PAUSED until first real samples arrive (avoid silence on
  // startup)
  SDL_PauseAudioDevice(audioDevice, 1);
  devicePausedUntilFirstSamples = true;
  return true;
}

void LibretroNesBackend::closeAudioDevice() {
  if (audioStream) {
    SDL_FreeAudioStream(audioStream);
    audioStream = nullptr;
  }

  if (audioDevice != 0) {
    SDL_PauseAudioDevice(audioDevice, 1); // Pause before closing
    SDL_CloseAudioDevice(audioDevice);
    audioDevice = 0;
  }

  {
    std::lock_guard<std::mutex> lock(audioBufferMutex);
    audioBuffer.clear();
    audioReadPos = 0;
  }
}

void LibretroNesBackend::queueAudioFrames(const int16_t *data, size_t frames) {
  if (!data || frames == 0 || audioDevice == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(audioBufferMutex);
  const size_t samples = frames * 2; // stereo = 2 samples per frame

  // Unpause device on first real audio samples to avoid silence startup
  if (devicePausedUntilFirstSamples) {
    SDL_PauseAudioDevice(audioDevice, 0);
    devicePausedUntilFirstSamples = false;
  }

  if (maxBufferedSamples > 0 && audioBuffer.size() > maxBufferedSamples) {
    const size_t drop = audioBuffer.size() - maxBufferedSamples;
    audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + drop);
  }

  audioBuffer.insert(audioBuffer.end(), data, data + samples);

  if (maxBufferedSamples > 0 && audioBuffer.size() > maxBufferedSamples) {
    const size_t drop = audioBuffer.size() - maxBufferedSamples;
    audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + drop);
  }
}

bool LibretroNesBackend::s_envCb(unsigned cmd, void *data) {
  return s_instance ? s_instance->onEnvironment(cmd, data) : false;
}

void LibretroNesBackend::s_videoCb(const void *data, unsigned width,
                                   unsigned height, size_t pitch) {
  if (s_instance) {
    s_instance->onVideoRefresh(data, width, height, pitch);
  }
}

void LibretroNesBackend::s_audioCb(int16_t left, int16_t right) {
  if (!s_instance) {
    return;
  }
  const int16_t frame[2] = {left, right};
  s_instance->queueAudioFrames(frame, 1);
}

size_t LibretroNesBackend::s_audioBatchCb(const int16_t *data, size_t frames) {
  if (s_instance && data && frames > 0) {
    s_instance->queueAudioFrames(data, frames);
  }
  return frames;
}

void LibretroNesBackend::s_inputPollCb() {}

int16_t LibretroNesBackend::s_inputStateCb(unsigned port, unsigned device,
                                           unsigned index, unsigned id) {
  if (!s_instance || port != 0 || device != libretro::DEVICE_JOYPAD ||
      index != 0) {
    return 0;
  }

  const agnes_input_t &in = s_instance->currentInput;
  switch (id) {
  case libretro::JOYPAD_UP:
    return in.up ? 1 : 0;
  case libretro::JOYPAD_DOWN:
    return in.down ? 1 : 0;
  case libretro::JOYPAD_LEFT:
    return in.left ? 1 : 0;
  case libretro::JOYPAD_RIGHT:
    return in.right ? 1 : 0;
  case libretro::JOYPAD_A:
    return in.a ? 1 : 0;
  case libretro::JOYPAD_B:
    return in.b ? 1 : 0;
  case libretro::JOYPAD_SELECT:
    return in.select ? 1 : 0;
  case libretro::JOYPAD_START:
    return in.start ? 1 : 0;
  default:
    return 0;
  }
}

void LibretroNesBackend::s_audioCallback(void *userdata, Uint8 *stream,
                                         int len) {
  LibretroNesBackend *backend =
      reinterpret_cast<LibretroNesBackend *>(userdata);
  if (!backend) {
    std::memset(stream, 0, len);
    return;
  }

  int16_t *out = reinterpret_cast<int16_t *>(stream);
  int samples = len / sizeof(int16_t); // Number of int16_t samples needed

  std::lock_guard<std::mutex> lock(backend->audioBufferMutex);

  for (int i = 0; i < samples; ++i) {
    if (backend->audioReadPos < backend->audioBuffer.size()) {
      out[i] = backend->audioBuffer[backend->audioReadPos++];
    } else {
      out[i] = 0; // No more audio, output silence
    }
  }

  // Remove consumed samples from buffer
  if (backend->audioReadPos > 0) {
    backend->audioBuffer.erase(backend->audioBuffer.begin(),
                               backend->audioBuffer.begin() +
                                   backend->audioReadPos);
    backend->audioReadPos = 0;
  }
}

std::unique_ptr<NesBackend> makeNesBackend() {
  auto libretroBackend = std::make_unique<LibretroNesBackend>();
  if (libretroBackend->isAvailable()) {
    return libretroBackend;
  }

  auto agnesBackend = std::make_unique<AgnesBackend>();
  if (agnesBackend->isReady()) {
    return agnesBackend;
  }

  return {};
}

NESEmulation::InputMode NESEmulation::globalInputMode =
    NESEmulation::InputMode::KeyboardOnly;
NESEmulation::KeyBindings NESEmulation::globalKeyBindings = {};
NESEmulation::ControllerBindings NESEmulation::globalControllerBindings = {};
bool NESEmulation::globalInputConfigured = false;

NESEmulation::NESEmulation()
    : isRunning(false), window(nullptr), renderer(nullptr),
      screenTexture(nullptr), controller(nullptr), controllerDeviceIndex(-1),
      scaleX(2), scaleY(2), workerInput{}, emulationThreadStopRequested(false),
      emulationThreadRunning(false), emulationThreadFailed(false),
      frameReady(false), inputMode(InputMode::KeyboardOnly) {
  backend = makeNesBackend();
  frameBuffer.resize(AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT * 4, 0);
  latestFrame.resize(AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT * 4, 0);
  setDefaultBindings();
  if (globalInputConfigured) {
    inputMode = globalInputMode;
    keyBindings = globalKeyBindings;
    controllerBindings = globalControllerBindings;
  } else {
    saveGlobalInputSettings();
  }
  if (!backend) {
    std::cerr << "Failed to initialize NES backend.\n";
  }
}

NESEmulation::~NESEmulation() {
  unloadRom();
  cleanupSDL();
}

bool NESEmulation::loadRom(const string &path) {
  if (!backend) {
    std::cerr << "Emulator not initialized.\n";
    return false;
  }

  // Check if file exists
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open ROM file: " << path << "\n";
    return false;
  }

  // Check for .nes extension
  if (path.length() < 4 || path.substr(path.length() - 4) != ".nes") {
    std::cerr << "Invalid file extension. Only .nes files are supported.\n";
    file.close();
    return false;
  }

  // Read header bytes to validate iNES format before sending to backend.
  file.seekg(0, std::ios::end);
  size_t fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  romData.assign(std::min<size_t>(fileSize, 16), 0);
  file.read(reinterpret_cast<char *>(romData.data()),
            static_cast<std::streamsize>(romData.size()));
  file.close();

  if (!file) {
    std::cerr << "Failed to read ROM file contents.\n";
    romData.clear();
    return false;
  }

  // Validate iNES header (first 4 bytes should be "NES\x1a").
  if (fileSize < 16 || romData[0] != 'N' || romData[1] != 'E' ||
      romData[2] != 'S' || romData[3] != 0x1a) {
    std::cerr
        << "Invalid NES ROM file format (missing or corrupted iNES header).\n";
    romData.clear();
    return false;
  }

  if (!backend->loadRom(path)) {
    std::cerr << "NES backend failed to load ROM.\n";
    romData.clear();
    return false;
  }

  romPath = path;
  isRunning = true;
  std::cout << "Successfully loaded ROM: " << path << "\n";
  return true;
}

void NESEmulation::unloadRom() {
  stopEmulationWorker();
  isRunning = false;
  romPath.clear();
  romData.clear();
}

std::vector<unsigned char> NESEmulation::dumpSaveData() const {
  if (!backend || !isRunning) {
    return {};
  }

  return backend->dumpSaveData();
}

bool NESEmulation::restoreSaveData(const std::vector<unsigned char> &data) {
  if (!backend || !isRunning) {
    return false;
  }

  return backend->restoreSaveData(data);
}

void NESEmulation::calculateScale(int screenWidth, int screenHeight) {
  // Calculate maximum scale that fits on screen
  // Leave 50px margin for window decorations
  int maxScaleX = (screenWidth - 50) / AGNES_SCREEN_WIDTH;
  int maxScaleY = (screenHeight - 50) / AGNES_SCREEN_HEIGHT;

  // Use the smaller of the two to maintain aspect ratio
  int scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;

  // Minimum scale of 1, ensure we have reasonable scaling
  scaleX = (scale < 1) ? 1 : scale;
  scaleY = scaleX;

  std::cout << "Screen scale: " << scaleX << "x\n";
}

void NESEmulation::initSDL() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
    return;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

  // Get display dimensions
  SDL_DisplayMode displayMode;
  SDL_GetCurrentDisplayMode(0, &displayMode);

  std::cout << "Display resolution: " << displayMode.w << "x" << displayMode.h
            << "\n";

  // Calculate optimal scaling
  calculateScale(displayMode.w, displayMode.h);

  // Create window
  int windowWidth = AGNES_SCREEN_WIDTH * scaleX;
  int windowHeight = AGNES_SCREEN_HEIGHT * scaleY;

  window = SDL_CreateWindow("NES Emulator - Agnes", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);

  if (!window) {
    std::cerr << "Failed to create SDL window: " << SDL_GetError() << "\n";
    SDL_Quit();
    return;
  }

  // Create renderer
  renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    std::cerr << "Failed to create SDL renderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
    return;
  }

  // RGBA32 texture upload is robust across platforms and driver backends.
  screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STATIC,
                                    AGNES_SCREEN_WIDTH, AGNES_SCREEN_HEIGHT);

  if (!screenTexture) {
    std::cerr << "Failed to create SDL texture: " << SDL_GetError() << "\n";
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    renderer = nullptr;
    window = nullptr;
    SDL_Quit();
    return;
  }

  std::cout << "SDL2 initialized successfully. Window: " << windowWidth << "x"
            << windowHeight << "\n";
}

void NESEmulation::cleanupSDL() {
  if (controller) {
    SDL_GameControllerClose(controller);
    controller = nullptr;
    controllerDeviceIndex = -1;
  }

  if (screenTexture) {
    SDL_DestroyTexture(screenTexture);
    screenTexture = nullptr;
  }
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }
  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }
  SDL_Quit();
}

void NESEmulation::setDefaultBindings() {
  keyBindings = {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT,
                 SDLK_z,  SDLK_x,    SDLK_a,    SDLK_s};

  controllerBindings = {
      SDL_CONTROLLER_BUTTON_DPAD_UP,   SDL_CONTROLLER_BUTTON_DPAD_DOWN,
      SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
      SDL_CONTROLLER_BUTTON_A,         SDL_CONTROLLER_BUTTON_B,
      SDL_CONTROLLER_BUTTON_BACK,      SDL_CONTROLLER_BUTTON_START};
}

void NESEmulation::saveGlobalInputSettings() {
  globalInputMode = inputMode;
  globalKeyBindings = keyBindings;
  globalControllerBindings = controllerBindings;
  globalInputConfigured = true;
}

void NESEmulation::listControllers() const {
  const int joystickCount = SDL_NumJoysticks();
  if (joystickCount <= 0) {
    std::cout << "No controllers detected.\n";
    return;
  }

  std::cout << "Detected controllers:\n";
  for (int i = 0; i < joystickCount; ++i) {
    if (SDL_IsGameController(i)) {
      const char *name = SDL_GameControllerNameForIndex(i);
      std::cout << "  [" << i << "] " << (name ? name : "Unknown controller")
                << "\n";
    }
  }
}

bool NESEmulation::openControllerByIndex(int index) {
  if (index < 0 || index >= SDL_NumJoysticks() ||
      !SDL_IsGameController(index)) {
    std::cout << "Controller index is not valid.\n";
    return false;
  }

  if (controller) {
    SDL_GameControllerClose(controller);
    controller = nullptr;
  }

  controller = SDL_GameControllerOpen(index);
  if (!controller) {
    std::cout << "Failed to open controller: " << SDL_GetError() << "\n";
    controllerDeviceIndex = -1;
    return false;
  }

  controllerDeviceIndex = index;
  const char *controllerName = SDL_GameControllerName(controller);
  std::cout << "Using controller: "
            << (controllerName ? controllerName : "Unknown") << "\n";
  return true;
}

void NESEmulation::configureInputMenu() {
  while (true) {
    std::cout << "\n=== Input Configuration ===\n";
    std::cout << "1) Input mode: Keyboard only\n";
    std::cout << "2) Input mode: Controller only\n";
    std::cout << "3) Input mode: Keyboard + Controller\n";
    std::cout << "4) Remap keyboard keys\n";
    std::cout << "5) Select controller\n";
    std::cout << "6) Remap controller buttons\n";
    std::cout << "0) Start game\n";
    std::cout << "Choice: ";

    std::string choice;
    std::getline(std::cin, choice);
    choice = trim(choice);

    if (choice == "0") {
      break;
    }

    if (choice == "1") {
      inputMode = InputMode::KeyboardOnly;
      std::cout << "Input mode set to Keyboard only.\n";
      continue;
    }

    if (choice == "2") {
      inputMode = InputMode::ControllerOnly;
      std::cout << "Input mode set to Controller only.\n";
      continue;
    }

    if (choice == "3") {
      inputMode = InputMode::KeyboardAndController;
      std::cout << "Input mode set to Keyboard + Controller.\n";
      continue;
    }

    if (choice == "4") {
      struct KeyboardAction {
        const char *name;
        SDL_Keycode *key;
      };

      KeyboardAction actions[] = {
          {"UP", &keyBindings.up},         {"DOWN", &keyBindings.down},
          {"LEFT", &keyBindings.left},     {"RIGHT", &keyBindings.right},
          {"A", &keyBindings.a},           {"B", &keyBindings.b},
          {"SELECT", &keyBindings.select}, {"START", &keyBindings.start}};

      std::cout << "Enter key names recognized by SDL (e.g. Up, Down, Left, "
                   "Right, Z, X, A, S).\n";
      std::cout << "Press Enter to keep current binding.\n";

      for (KeyboardAction &action : actions) {
        std::cout << action.name << " [" << SDL_GetKeyName(*action.key)
                  << "]: ";
        std::string keyName;
        std::getline(std::cin, keyName);
        keyName = trim(keyName);
        if (keyName.empty()) {
          continue;
        }

        SDL_Keycode parsed = SDL_GetKeyFromName(keyName.c_str());
        if (parsed == SDLK_UNKNOWN) {
          std::cout << "  Invalid key name, keeping existing binding.\n";
          continue;
        }
        *action.key = parsed;
      }

      std::cout << "Keyboard bindings updated.\n";
      continue;
    }

    if (choice == "5") {
      listControllers();
      std::cout << "Enter controller index to use: ";
      std::string indexInput;
      std::getline(std::cin, indexInput);
      indexInput = trim(indexInput);
      if (indexInput.empty()) {
        continue;
      }

      try {
        const int index = std::stoi(indexInput);
        openControllerByIndex(index);
      } catch (...) {
        std::cout << "Invalid controller index.\n";
      }
      continue;
    }

    if (choice == "6") {
      std::cout
          << "Enter controller button names "
             "(A,B,X,Y,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT).\n";
      std::cout << "Press Enter to keep current binding.\n";

      struct ControllerAction {
        const char *name;
        SDL_GameControllerButton *button;
      };

      ControllerAction actions[] = {{"UP", &controllerBindings.up},
                                    {"DOWN", &controllerBindings.down},
                                    {"LEFT", &controllerBindings.left},
                                    {"RIGHT", &controllerBindings.right},
                                    {"A", &controllerBindings.a},
                                    {"B", &controllerBindings.b},
                                    {"SELECT", &controllerBindings.select},
                                    {"START", &controllerBindings.start}};

      for (ControllerAction &action : actions) {
        std::cout << action.name << " [" << controllerButtonName(*action.button)
                  << "]: ";
        std::string buttonName;
        std::getline(std::cin, buttonName);
        buttonName = trim(buttonName);
        if (buttonName.empty()) {
          continue;
        }

        SDL_GameControllerButton parsed = parseControllerButtonName(buttonName);
        if (parsed == SDL_CONTROLLER_BUTTON_INVALID) {
          std::cout << "  Invalid button name, keeping existing binding.\n";
          continue;
        }
        *action.button = parsed;
      }

      std::cout << "Controller bindings updated.\n";
      continue;
    }

    std::cout << "Unknown menu option.\n";
  }

  saveGlobalInputSettings();
  std::cout << "Global controls updated.\n";
}

void NESEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed,
                                       agnes_input_t &input, bool &running) {
  if (key == SDLK_ESCAPE && pressed) {
    running = false;
    return;
  }

  if (key == keyBindings.up)
    input.up = pressed;
  if (key == keyBindings.down)
    input.down = pressed;
  if (key == keyBindings.left)
    input.left = pressed;
  if (key == keyBindings.right)
    input.right = pressed;
  if (key == keyBindings.a)
    input.a = pressed;
  if (key == keyBindings.b)
    input.b = pressed;
  if (key == keyBindings.select)
    input.select = pressed;
  if (key == keyBindings.start)
    input.start = pressed;
}

void NESEmulation::handleControllerButtonEvent(Uint8 button, bool pressed,
                                               agnes_input_t &input,
                                               bool &running) {
  (void)running;

  if (button == static_cast<Uint8>(controllerBindings.up))
    input.up = pressed;
  if (button == static_cast<Uint8>(controllerBindings.down))
    input.down = pressed;
  if (button == static_cast<Uint8>(controllerBindings.left))
    input.left = pressed;
  if (button == static_cast<Uint8>(controllerBindings.right))
    input.right = pressed;
  if (button == static_cast<Uint8>(controllerBindings.a))
    input.a = pressed;
  if (button == static_cast<Uint8>(controllerBindings.b))
    input.b = pressed;
  if (button == static_cast<Uint8>(controllerBindings.select))
    input.select = pressed;
  if (button == static_cast<Uint8>(controllerBindings.start))
    input.start = pressed;
}

void NESEmulation::renderFrame() {
  if (!renderer || !screenTexture) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!frameReady.load()) {
      return;
    }
    frameBuffer = latestFrame;
  }

  if (SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(),
                        AGNES_SCREEN_WIDTH * 4) < 0) {
    std::cerr << "Failed to update texture: " << SDL_GetError() << "\n";
    return;
  }

  // Clear and render
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  // Create scaled rectangle
  SDL_Rect destRect = {0, 0, AGNES_SCREEN_WIDTH * scaleX,
                       AGNES_SCREEN_HEIGHT * scaleY};
  SDL_RenderCopy(renderer, screenTexture, nullptr, &destRect);

  SDL_RenderPresent(renderer);
}

void NESEmulation::nextFrame() {
  if (!backend || !isRunning.load()) {
    return;
  }

  if (!backend->nextFrame()) {
    std::cerr << "NES backend failed to advance frame.\n";
    isRunning = false;
  }
}

void NESEmulation::setInput(const agnes_input_t &input) {
  if (!backend || !isRunning.load()) {
    return;
  }

  std::lock_guard<std::mutex> lock(inputMutex);
  workerInput = input;
}

agnes_color_t NESEmulation::getPixel(int x, int y) const {
  if (!backend || !isRunning.load()) {
    return {0, 0, 0, 0};
  }

  if (x < 0 || x >= AGNES_SCREEN_WIDTH || y < 0 || y >= AGNES_SCREEN_HEIGHT) {
    return {0, 0, 0, 0};
  }

  std::lock_guard<std::mutex> lock(frameMutex);
  if (!frameReady.load() ||
      latestFrame.size() < static_cast<std::size_t>(AGNES_SCREEN_WIDTH *
                                                    AGNES_SCREEN_HEIGHT * 4)) {
    return {0, 0, 0, 0};
  }

  const std::size_t offset = (static_cast<std::size_t>(y) *
                                  static_cast<std::size_t>(AGNES_SCREEN_WIDTH) +
                              static_cast<std::size_t>(x)) *
                             4;
  return {latestFrame[offset + 0], latestFrame[offset + 1],
          latestFrame[offset + 2], latestFrame[offset + 3]};
}

// Helper function to display NES frame as ASCII art (simple visualization)
void NESEmulation::displayFrame() const {
  if (!backend || !isRunning) {
    return;
  }

  // Simple ASCII representation - sample every 8th pixel
  for (int y = 0; y < AGNES_SCREEN_HEIGHT; y += 8) {
    for (int x = 0; x < AGNES_SCREEN_WIDTH; x += 8) {
      agnes_color_t pixel = backend->getPixel(x, y);

      // Convert RGB to grayscale
      int gray = (pixel.r + pixel.g + pixel.b) / 3;

      if (gray > 192) {
        std::cout << "█";
      } else if (gray > 128) {
        std::cout << "▓";
      } else if (gray > 64) {
        std::cout << "▒";
      } else {
        std::cout << "░";
      }
    }
    std::cout << "\n";
  }
}

void NESEmulation::configureGlobalControls() {
  NESEmulation configSession;

  const bool hadControllerSubsystem =
      (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0);
  if (!hadControllerSubsystem &&
      SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
    std::cerr << "Failed to initialize controller subsystem: " << SDL_GetError()
              << "\n";
    return;
  }

  if (SDL_NumJoysticks() > 0) {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      if (SDL_IsGameController(i)) {
        configSession.openControllerByIndex(i);
        break;
      }
    }
  }

  configSession.configureInputMenu();

  if (!hadControllerSubsystem) {
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
  }
}

void NESEmulation::runInteractive() {
  if (!backend || !isRunning.load()) {
    std::cerr << "No ROM loaded.\n";
    return;
  }

  if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
    std::cerr << "Failed to initialize controller subsystem: " << SDL_GetError()
              << "\n";
  }

  // Try to auto-open the first compatible controller.
  if (SDL_NumJoysticks() > 0) {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      if (SDL_IsGameController(i)) {
        openControllerByIndex(i);
        break;
      }
    }
  }

  initSDL();

  if (!window || !renderer) {
    std::cerr << "Failed to initialize SDL graphics.\n";
    return;
  }

  if (inputMode == InputMode::ControllerOnly && !controller) {
    std::cout << "Controller-only mode selected, but no controller is active. "
                 "Falling back to keyboard.\n";
    inputMode = InputMode::KeyboardOnly;
  }
  if (controller && inputMode == InputMode::KeyboardOnly)
    inputMode = InputMode::KeyboardAndController;

  std::cout << "\n=== NES Emulator Running ===\n";
  std::cout << "ROM: " << romPath << "\n";
  std::cout << "Window opened. Close it to exit.\n";
  std::cout << "Press 1 to load the save state, 2 to save, or ESC to exit.\n\n";

  agnes_input_t input = {};
  bool running = true;
  bool windowClosed = false;

  if (!startEmulationWorker()) {
    std::cerr << "Failed to start NES emulation worker thread.\n";
    cleanupSDL();
    return;
  }

  auto resumeWorker = [&]() {
    if (running && isRunning.load() && !startEmulationWorker()) {
      std::cerr << "Failed to restart NES emulation worker after save-state "
                   "operation.\n";
      running = false;
    }
  };
  auto saveState = [&]() {
    stopEmulationWorker();
    const std::vector<unsigned char> data = dumpSaveData();
    if (data.empty()) {
      std::cerr << "Could not create NES save state.\n";
    } else if (SaveManager::instance().put(romPath, data)) {
      std::cout << "NES save state saved.\n";
    } else {
      std::cerr << "Could not write NES save state.\n";
    }
    resumeWorker();
  };
  auto loadState = [&]() {
    std::vector<unsigned char> data;
    if (!SaveManager::instance().get(romPath, data)) {
      std::cout << "No NES save state found for this game.\n";
      return;
    }

    stopEmulationWorker();
    if (restoreSaveData(data)) {
      std::cout << "NES save state loaded.\n";
    } else {
      std::cerr << "Could not load NES save state.\n";
    }
    resumeWorker();
  };

  while (running && !windowClosed) {
    // Handle SDL events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        running = false;
        windowClosed = true;
        break;

      case SDL_KEYDOWN:
        if (event.key.repeat == 0) {
          if (event.key.keysym.sym == SDLK_1) {
            loadState();
            break;
          }
          if (event.key.keysym.sym == SDLK_2) {
            saveState();
            break;
          }
        }
        if (inputMode == InputMode::KeyboardOnly ||
            inputMode == InputMode::KeyboardAndController) {
          handleKeyboardEvent(event.key.keysym.sym, true, input, running);
        }
        break;

      case SDL_KEYUP:
        if (inputMode == InputMode::KeyboardOnly ||
            inputMode == InputMode::KeyboardAndController) {
          handleKeyboardEvent(event.key.keysym.sym, false, input, running);
        }
        break;

      case SDL_CONTROLLERBUTTONDOWN:
        if (inputMode == InputMode::ControllerOnly ||
            inputMode == InputMode::KeyboardAndController) {
          handleControllerButtonEvent(event.cbutton.button, true, input,
                                      running);
        }
        break;

      case SDL_CONTROLLERBUTTONUP:
        if (inputMode == InputMode::ControllerOnly ||
            inputMode == InputMode::KeyboardAndController) {
          handleControllerButtonEvent(event.cbutton.button, false, input,
                                      running);
        }
        break;

      case SDL_CONTROLLERDEVICEADDED:
        if (!controller) {
          openControllerByIndex(event.cdevice.which);
        }
        break;

      case SDL_CONTROLLERDEVICEREMOVED:
        if (controller) {
          SDL_Joystick *joy = SDL_GameControllerGetJoystick(controller);
          if (joy && SDL_JoystickInstanceID(joy) == event.cdevice.which) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
            controllerDeviceIndex = -1;
            std::cout << "Controller disconnected.\n";
          }
        }
        break;
      }
    }

    // Update input
    setInput(input);

    if (emulationThreadFailed.load() || !isRunning.load()) {
      running = false;
      break;
    }

    // Render frame
    renderFrame();

    SDL_Delay(1);
  }

  stopEmulationWorker();
  cleanupSDL();
  std::cout << "\nEmulation stopped.\n";
}

bool NESEmulation::startEmulationWorker() {
  if (emulationThreadRunning.load() || !backend || !isRunning.load()) {
    return false;
  }

  emulationThreadStopRequested = false;
  emulationThreadFailed = false;
  frameReady = false;
  emulationThread = std::thread(&NESEmulation::emulationWorkerLoop, this);
  return true;
}

void NESEmulation::stopEmulationWorker() {
  emulationThreadStopRequested = true;
  if (emulationThread.joinable()) {
    emulationThread.join();
  }
}

void NESEmulation::emulationWorkerLoop() {
  emulationThreadRunning = true;

  std::vector<unsigned char> localFrame(
      static_cast<std::size_t>(AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT) * 4,
      0);
  AudioMasterClock clock;
  clock.configure(backend ? backend->getAudioSampleRate() : 48000,
                  backend ? backend->getNominalFps() : 60.0988, 80);

  while (!emulationThreadStopRequested.load() && isRunning.load()) {
    agnes_input_t inputSnapshot = {};
    {
      std::lock_guard<std::mutex> lock(inputMutex);
      inputSnapshot = workerInput;
    }

    backend->setInput(inputSnapshot);
    if (!backend->nextFrame()) {
      std::cerr << "NES backend failed to advance frame.\n";
      isRunning = false;
      emulationThreadFailed = true;
      break;
    }

    for (int y = 0; y < AGNES_SCREEN_HEIGHT; ++y) {
      for (int x = 0; x < AGNES_SCREEN_WIDTH; ++x) {
        const agnes_color_t color = backend->getPixel(x, y);
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(AGNES_SCREEN_WIDTH) +
             static_cast<std::size_t>(x)) *
            4;
        localFrame[offset + 0] = color.r;
        localFrame[offset + 1] = color.g;
        localFrame[offset + 2] = color.b;
        localFrame[offset + 3] = 255;
      }
    }

    {
      std::lock_guard<std::mutex> lock(frameMutex);
      latestFrame.swap(localFrame);
      frameReady = true;
    }

    clock.onFrameProduced();
    clock.throttle();
  }

  emulationThreadRunning = false;
}
