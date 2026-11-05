#include "Atari2600.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

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
  if (name == "BACK")
    return SDL_CONTROLLER_BUTTON_BACK;
  if (name == "START")
    return SDL_CONTROLLER_BUTTON_START;
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

class Atari2600Backend {
public:
  virtual ~Atari2600Backend() = default;
  virtual bool loadRom(const std::string &path) = 0;
  virtual bool nextFrame() = 0;
  virtual void setInput(const atari2600_input_t &input) = 0;
  virtual std::vector<unsigned char> dumpSaveData() const = 0;
  virtual bool restoreSaveData(const std::vector<unsigned char> &data) = 0;
  virtual atari2600_color_t getPixel(int x, int y) const = 0;
  virtual int getFrameWidth() const { return 160; }
  virtual int getFrameHeight() const { return 192; }
  virtual double getAspectRatio() const { return 4.0 / 3.0; }
  virtual double getNominalFps() const { return 60.0; }
  virtual bool initializeAudio() { return true; }
};

class Atari2600UnavailableBackend final : public Atari2600Backend {
public:
  bool loadRom(const std::string &path) override {
    (void)path;
    std::cerr << "Atari 2600 libretro core is not available in this build.\n";
    std::cerr << "Install stella_libretro.so or set OMNICON_ATARI2600_CORE.\n";
    return false;
  }

  bool nextFrame() override { return false; }
  void setInput(const atari2600_input_t &input) override { (void)input; }
  std::vector<unsigned char> dumpSaveData() const override { return {}; }
  bool restoreSaveData(const std::vector<unsigned char> &data) override {
    (void)data;
    return false;
  }
  atari2600_color_t getPixel(int x, int y) const override {
    (void)x;
    (void)y;
    return {0, 0, 0, 255};
  }
};

namespace libretro {
static constexpr unsigned ENV_SET_PIXEL_FORMAT = 10;
static constexpr unsigned ENV_GET_VARIABLE = 15;
static constexpr unsigned ENV_SET_VARIABLES = 16;
static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
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
static constexpr unsigned JOYPAD_X = 9;

struct game_info {
  const char *path;
  const void *data;
  size_t size;
  const char *meta;
};

struct game_geometry {
  unsigned base_width;
  unsigned base_height;
  unsigned max_width;
  unsigned max_height;
  float aspect_ratio;
};

struct variable {
  const char *key;
  const char *value;
};

struct av_info {
  game_geometry geometry;
  struct {
    double fps;
    double sample_rate;
  } timing;
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

class LibretroAtari2600Backend final : public Atari2600Backend {
public:
  static constexpr const char *kWorkspaceCorePathStella2014 =
      "third_party/libretro/stella2014_libretro.so";
  static constexpr const char *kWorkspaceCorePathProsystem =
      "third_party/libretro/prosystem_libretro.so";
  static constexpr const char *kWorkspaceCorePathStella =
      "third_party/libretro/stella_libretro.so";
  static constexpr const char *kSystemCorePathStella2014 =
      "/usr/lib/x86_64-linux-gnu/libretro/stella2014_libretro.so";
  static constexpr const char *kSystemCorePathProsystem =
      "/usr/lib/x86_64-linux-gnu/libretro/prosystem_libretro.so";
  static constexpr const char *kSystemCorePathStella =
      "/usr/lib/x86_64-linux-gnu/libretro/stella_libretro.so";

  LibretroAtari2600Backend();
  ~LibretroAtari2600Backend() override;

  bool isAvailable() const { return coreHandle != nullptr; }
  bool loadRom(const std::string &path) override;
  bool nextFrame() override;
  void setInput(const atari2600_input_t &input) override;
  std::vector<unsigned char> dumpSaveData() const override;
  bool restoreSaveData(const std::vector<unsigned char> &data) override;
  atari2600_color_t getPixel(int x, int y) const override;
  int getFrameWidth() const override { return frameWidth; }
  int getFrameHeight() const override { return frameHeight; }
  double getAspectRatio() const override { return aspectRatio; }
  double getNominalFps() const override { return nominalFps; }

  bool onEnvironment(unsigned cmd, void *data);
  void onVideoRefresh(const void *data, unsigned width, unsigned height,
                      size_t pitch);

private:
  static std::string resolveCorePath();
  bool loadSymbols();

  std::string corePath;
  void *coreHandle = nullptr;
  void (*fn_init)() = nullptr;
  void (*fn_deinit)() = nullptr;
  void (*fn_set_environment)(libretro::environment_t) = nullptr;
  void (*fn_set_video_refresh)(libretro::video_refresh_t) = nullptr;
  void (*fn_set_audio_sample)(libretro::audio_sample_t) = nullptr;
  void (*fn_set_audio_sample_batch)(libretro::audio_sample_batch_t) = nullptr;
  void (*fn_set_input_poll)(libretro::input_poll_t) = nullptr;
  void (*fn_set_input_state)(libretro::input_state_t) = nullptr;
  void (*fn_get_system_av_info)(libretro::av_info *) = nullptr;
  bool (*fn_load_game)(const libretro::game_info *) = nullptr;
  void (*fn_unload_game)() = nullptr;
  void (*fn_run)() = nullptr;
  size_t (*fn_serialize_size)() = nullptr;
  bool (*fn_serialize)(void *, size_t) = nullptr;
  bool (*fn_unserialize)(const void *, size_t) = nullptr;

  std::vector<unsigned char> framePixels;
  std::vector<unsigned char> romData;
  std::string loadedRomPath;
  std::string systemDirectory;
  std::string saveDirectory;
  std::string contentDirectory;
  std::string optColorDepth = "24bit";
  std::string optMixFrames = "disabled";
  std::string optLowPassFilter = "disabled";
  std::string optLowPassRange = "60";
  atari2600_input_t currentInput = {};
  bool gameLoaded = false;
  unsigned pixelFormat = libretro::PIXEL_0RGB1555;
  int frameWidth = 160;
  int frameHeight = 192;
  double aspectRatio = 4.0 / 3.0;
  double nominalFps = 60.0;
  SDL_AudioDeviceID audioDevice = 0;
  int audioSampleRate = 48000;
  int coreSampleRate = 48000;
  size_t maxQueuedAudioBytes = 0;

  static LibretroAtari2600Backend *s_instance;
  static bool s_envCb(unsigned cmd, void *data);
  static void s_videoCb(const void *data, unsigned width, unsigned height,
                        size_t pitch);
  static void s_audioCb(int16_t left, int16_t right);
  static size_t s_audioBatchCb(const int16_t *data, size_t frames);
  static void s_inputPollCb();
  static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index,
                                unsigned id);

  bool ensureAudioDevice(int sampleRate);
  void closeAudioDevice();
  void queueAudioFrames(const int16_t *data, size_t frames);
  void ensureLibretroDirectories();
  bool initializeAudio() override;
  bool setOptionValue(const char *key, const char *value);
};

LibretroAtari2600Backend *LibretroAtari2600Backend::s_instance = nullptr;

std::string LibretroAtari2600Backend::resolveCorePath() {
  const char *envCorePath = std::getenv("OMNICON_ATARI2600_CORE");
  if (envCorePath && *envCorePath && std::filesystem::exists(envCorePath)) {
    return envCorePath;
  }

  // Prefer more broadly compatible Atari 2600 cores first.
  const char *candidates[] = {
      kWorkspaceCorePathStella2014, kWorkspaceCorePathProsystem,
      kWorkspaceCorePathStella,     kSystemCorePathStella2014,
      kSystemCorePathProsystem,     kSystemCorePathStella,
  };
  for (const char *path : candidates) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  return "";
}

LibretroAtari2600Backend::LibretroAtari2600Backend()
    : corePath(resolveCorePath()) {
  if (corePath.empty()) {
    return;
  }

  coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
  if (!coreHandle) {
    std::cerr << "[Atari2600] Failed to load core from " << corePath << ": "
              << dlerror() << "\n";
    return;
  }

  if (!loadSymbols()) {
    dlclose(coreHandle);
    coreHandle = nullptr;
    return;
  }

  s_instance = this;
  ensureLibretroDirectories();
  fn_set_environment(s_envCb);
  fn_set_video_refresh(s_videoCb);
  fn_set_audio_sample(s_audioCb);
  fn_set_audio_sample_batch(s_audioBatchCb);
  fn_set_input_poll(s_inputPollCb);
  fn_set_input_state(s_inputStateCb);
  fn_init();
  framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
}

LibretroAtari2600Backend::~LibretroAtari2600Backend() {
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

bool LibretroAtari2600Backend::loadSymbols() {
  auto load = [this](const char *name) -> void * {
    void *symbol = dlsym(coreHandle, name);
    if (!symbol) {
      std::cerr << "[Atari2600] Missing symbol: " << name << "\n";
    }
    return symbol;
  };

  fn_init = reinterpret_cast<void (*)()>(load("retro_init"));
  fn_deinit = reinterpret_cast<void (*)()>(load("retro_deinit"));
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
  fn_get_system_av_info = reinterpret_cast<void (*)(libretro::av_info *)>(
      load("retro_get_system_av_info"));
  fn_load_game = reinterpret_cast<bool (*)(const libretro::game_info *)>(
      load("retro_load_game"));
  fn_unload_game = reinterpret_cast<void (*)()>(load("retro_unload_game"));
  fn_run = reinterpret_cast<void (*)()>(load("retro_run"));

  fn_serialize_size =
      reinterpret_cast<size_t (*)()>(dlsym(coreHandle, "retro_serialize_size"));
  fn_serialize = reinterpret_cast<bool (*)(void *, size_t)>(
      dlsym(coreHandle, "retro_serialize"));
  fn_unserialize = reinterpret_cast<bool (*)(const void *, size_t)>(
      dlsym(coreHandle, "retro_unserialize"));

  return fn_init && fn_deinit && fn_set_environment && fn_set_video_refresh &&
         fn_set_audio_sample && fn_set_audio_sample_batch &&
         fn_set_input_poll && fn_set_input_state && fn_get_system_av_info &&
         fn_load_game && fn_unload_game && fn_run;
}

bool LibretroAtari2600Backend::setOptionValue(const char *key,
                                              const char *value) {
  if (!key || !value) {
    return false;
  }

  if (std::strcmp(key, "stella2014_color_depth") == 0) {
    optColorDepth = value;
    return true;
  }

  if (std::strcmp(key, "stella2014_mix_frames") == 0) {
    optMixFrames = value;
    return true;
  }

  if (std::strcmp(key, "stella2014_low_pass_filter") == 0) {
    optLowPassFilter = value;
    return true;
  }

  if (std::strcmp(key, "stella2014_low_pass_range") == 0) {
    optLowPassRange = value;
    return true;
  }

  return false;
}

void LibretroAtari2600Backend::ensureLibretroDirectories() {
  const std::filesystem::path rootDir =
      std::filesystem::current_path() / "third_party" / "libretro";
  const std::filesystem::path systemDir = rootDir / "system";
  const std::filesystem::path saveDir = rootDir / "save";
  const std::filesystem::path contentDir = rootDir / "content";

  std::error_code ec;
  std::filesystem::create_directories(systemDir, ec);
  std::filesystem::create_directories(saveDir, ec);
  std::filesystem::create_directories(contentDir, ec);

  systemDirectory = systemDir.string();
  saveDirectory = saveDir.string();
  contentDirectory = contentDir.string();
}

bool LibretroAtari2600Backend::ensureAudioDevice(int sampleRate) {
  if (audioDevice != 0 && audioSampleRate == sampleRate) {
    return true;
  }

  closeAudioDevice();

  SDL_AudioSpec desired{};
  desired.freq = sampleRate;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 1024;
  desired.callback = nullptr;

  SDL_AudioSpec obtained{};
  audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (audioDevice == 0) {
    std::cerr << "[Atari2600] Failed to open audio device: " << SDL_GetError()
              << "\n";
    return false;
  }

  audioSampleRate = obtained.freq > 0 ? obtained.freq : sampleRate;
  maxQueuedAudioBytes = static_cast<size_t>(
      audioSampleRate * obtained.channels * sizeof(int16_t) * 12 / 100);
  SDL_PauseAudioDevice(audioDevice, 0);
  std::cerr << "[Atari2600] Audio initialized: " << audioSampleRate << " Hz, "
            << static_cast<int>(obtained.channels) << " channels\n";
  return true;
}

void LibretroAtari2600Backend::closeAudioDevice() {
  if (audioDevice != 0) {
    SDL_ClearQueuedAudio(audioDevice);
    SDL_CloseAudioDevice(audioDevice);
    audioDevice = 0;
  }
}

void LibretroAtari2600Backend::queueAudioFrames(const int16_t *data,
                                                size_t frames) {
  if (!data || frames == 0 || audioDevice == 0) {
    return;
  }

  const size_t bytes = frames * 2 * sizeof(int16_t);
  if (bytes == 0) {
    return;
  }

  const Uint32 queued = SDL_GetQueuedAudioSize(audioDevice);
  if (queued > maxQueuedAudioBytes) {
    SDL_ClearQueuedAudio(audioDevice);
  }

  SDL_QueueAudio(audioDevice, data, static_cast<Uint32>(bytes));
}

bool LibretroAtari2600Backend::initializeAudio() {
  return ensureAudioDevice(coreSampleRate);
}

bool LibretroAtari2600Backend::loadRom(const std::string &path) {
  if (!coreHandle) {
    return false;
  }

  ensureLibretroDirectories();

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "[Atari2600] Cannot open ROM: " << path << "\n";
    return false;
  }

  const auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  romData.assign(size, 0);
  if (!file.read(reinterpret_cast<char *>(romData.data()),
                 static_cast<std::streamsize>(size))) {
    std::cerr << "[Atari2600] Failed to read ROM: " << path << "\n";
    romData.clear();
    return false;
  }

  if (gameLoaded) {
    fn_unload_game();
    gameLoaded = false;
  }

  loadedRomPath = path;
  const std::filesystem::path romFsPath(path);
  const std::filesystem::path parentDir = romFsPath.parent_path();
  if (!parentDir.empty()) {
    std::error_code ec;
    if (std::filesystem::exists(parentDir, ec)) {
      contentDirectory = parentDir.string();
    }
  }
  libretro::game_info gameInfo{};
  gameInfo.path = loadedRomPath.c_str();
  gameInfo.data = romData.data();
  gameInfo.size = romData.size();
  gameInfo.meta = nullptr;

  if (!fn_load_game(&gameInfo)) {
    std::cerr << "[Atari2600] Core rejected ROM: " << path << "\n";
    return false;
  }

  gameLoaded = true;

  if (fn_get_system_av_info) {
    libretro::av_info avInfo{};
    fn_get_system_av_info(&avInfo);
    frameWidth = static_cast<int>(avInfo.geometry.base_width == 0
                                      ? frameWidth
                                      : avInfo.geometry.base_width);
    frameHeight = static_cast<int>(avInfo.geometry.base_height == 0
                                       ? frameHeight
                                       : avInfo.geometry.base_height);
    aspectRatio = avInfo.geometry.aspect_ratio > 0.0f
                      ? avInfo.geometry.aspect_ratio
                      : aspectRatio;
    nominalFps = avInfo.timing.fps > 1.0 ? avInfo.timing.fps : nominalFps;
    coreSampleRate = avInfo.timing.sample_rate > 1000.0
                         ? static_cast<int>(avInfo.timing.sample_rate)
                         : coreSampleRate;
  }
  framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
  return true;
}

bool LibretroAtari2600Backend::nextFrame() {
  if (!coreHandle || !gameLoaded) {
    return false;
  }
  fn_run();
  return true;
}

void LibretroAtari2600Backend::setInput(const atari2600_input_t &input) {
  currentInput = input;
}

std::vector<unsigned char> LibretroAtari2600Backend::dumpSaveData() const {
  if (!fn_serialize_size || !fn_serialize) {
    return {};
  }

  const size_t size = fn_serialize_size();
  if (size == 0) {
    return {};
  }

  std::vector<unsigned char> state(size, 0);
  if (!fn_serialize(state.data(), state.size())) {
    return {};
  }

  return state;
}

bool LibretroAtari2600Backend::restoreSaveData(
    const std::vector<unsigned char> &data) {
  if (!fn_unserialize) {
    return false;
  }
  return fn_unserialize(data.data(), data.size());
}

atari2600_color_t LibretroAtari2600Backend::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) {
    return {0, 0, 0, 255};
  }
  const size_t idx = static_cast<size_t>(y * frameWidth + x) * 4;
  if (idx + 3 >= framePixels.size()) {
    return {0, 0, 0, 255};
  }
  return {framePixels[idx + 0], framePixels[idx + 1], framePixels[idx + 2],
          framePixels[idx + 3]};
}

bool LibretroAtari2600Backend::onEnvironment(unsigned cmd, void *data) {
  if (!data && cmd != 17 && cmd != 18) {
    return false;
  }

  if (cmd == 3) {
    *static_cast<bool *>(data) = true;
    return true;
  }

  if (cmd == 9) {
    *static_cast<const char **>(data) = systemDirectory.c_str();
    return true;
  }

  if (cmd == libretro::ENV_GET_VARIABLE) {
    auto *variable = static_cast<libretro::variable *>(data);
    if (!variable || !variable->key) {
      return false;
    }

    variable->value = nullptr;
    if (std::strcmp(variable->key, "stella2014_color_depth") == 0) {
      variable->value = optColorDepth.c_str();
      return true;
    }
    if (std::strcmp(variable->key, "stella2014_mix_frames") == 0) {
      variable->value = optMixFrames.c_str();
      return true;
    }
    if (std::strcmp(variable->key, "stella2014_low_pass_filter") == 0) {
      variable->value = optLowPassFilter.c_str();
      return true;
    }
    if (std::strcmp(variable->key, "stella2014_low_pass_range") == 0) {
      variable->value = optLowPassRange.c_str();
      return true;
    }
    return false;
  }

  if (cmd == libretro::ENV_SET_VARIABLES) {
    return true;
  }

  if (cmd == libretro::ENV_GET_VARIABLE_UPDATE) {
    *static_cast<bool *>(data) = false;
    return true;
  }

  if (cmd == libretro::ENV_SET_PIXEL_FORMAT && data) {
    pixelFormat = *reinterpret_cast<unsigned *>(data);
    std::cerr << "[Atari2600] Pixel format requested: " << pixelFormat << "\n";
    return pixelFormat == libretro::PIXEL_0RGB1555 ||
           pixelFormat == libretro::PIXEL_XRGB8888 ||
           pixelFormat == libretro::PIXEL_RGB565;
  }

  if (cmd == 18) {
    return true;
  }

  if (cmd == 30) {
    *static_cast<const char **>(data) = contentDirectory.c_str();
    return true;
  }

  if (cmd == 31) {
    *static_cast<const char **>(data) = saveDirectory.c_str();
    return true;
  }

  if (cmd == libretro::ENV_SET_GEOMETRY && data) {
    const auto *geometry =
        reinterpret_cast<const libretro::game_geometry *>(data);
    frameWidth = static_cast<int>(
        geometry->base_width == 0 ? frameWidth : geometry->base_width);
    frameHeight = static_cast<int>(
        geometry->base_height == 0 ? frameHeight : geometry->base_height);
    if (geometry->aspect_ratio > 0.0f) {
      aspectRatio = geometry->aspect_ratio;
    }
    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    return true;
  }

  return false;
}

void LibretroAtari2600Backend::onVideoRefresh(const void *data, unsigned width,
                                              unsigned height, size_t pitch) {
  if (!data || width == 0 || height == 0) {
    return;
  }

  frameWidth = static_cast<int>(width);
  frameHeight = static_cast<int>(height);
  framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);

  for (unsigned y = 0; y < height; ++y) {
    const auto *row = reinterpret_cast<const uint8_t *>(data) + y * pitch;
    for (unsigned x = 0; x < width; ++x) {
      const size_t out = static_cast<size_t>(y * width + x) * 4;

      if (pixelFormat == libretro::PIXEL_XRGB8888) {
        const uint32_t pixel = reinterpret_cast<const uint32_t *>(row)[x];
        framePixels[out + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
        framePixels[out + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
        framePixels[out + 2] = static_cast<uint8_t>(pixel & 0xFF);
        framePixels[out + 3] = 255;
      } else if (pixelFormat == libretro::PIXEL_RGB565) {
        const uint16_t pixel = reinterpret_cast<const uint16_t *>(row)[x];
        framePixels[out + 0] =
            static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
        framePixels[out + 1] =
            static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
        framePixels[out + 2] = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
        framePixels[out + 3] = 255;
      } else {
        const uint16_t pixel = reinterpret_cast<const uint16_t *>(row)[x];
        framePixels[out + 0] =
            static_cast<uint8_t>(((pixel >> 10) & 0x1F) * 255 / 31);
        framePixels[out + 1] =
            static_cast<uint8_t>(((pixel >> 5) & 0x1F) * 255 / 31);
        framePixels[out + 2] = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
        framePixels[out + 3] = 255;
      }
    }
  }
}

bool LibretroAtari2600Backend::s_envCb(unsigned cmd, void *data) {
  return s_instance ? s_instance->onEnvironment(cmd, data) : false;
}

void LibretroAtari2600Backend::s_videoCb(const void *data, unsigned width,
                                         unsigned height, size_t pitch) {
  if (s_instance) {
    s_instance->onVideoRefresh(data, width, height, pitch);
  }
}

void LibretroAtari2600Backend::s_audioCb(int16_t left, int16_t right) {
  if (!s_instance) {
    return;
  }

  const int16_t sample[2] = {left, right};
  s_instance->queueAudioFrames(sample, 1);
}

size_t LibretroAtari2600Backend::s_audioBatchCb(const int16_t *data,
                                                size_t frames) {
  if (!s_instance || !data || frames == 0) {
    return 0;
  }

  s_instance->queueAudioFrames(data, frames);
  return frames;
}

void LibretroAtari2600Backend::s_inputPollCb() {}

int16_t LibretroAtari2600Backend::s_inputStateCb(unsigned port, unsigned device,
                                                 unsigned index, unsigned id) {
  (void)index;
  if (!s_instance || port != 0 || device != libretro::DEVICE_JOYPAD) {
    return 0;
  }

  const atari2600_input_t &input = s_instance->currentInput;
  switch (id) {
  case libretro::JOYPAD_UP:
    return input.up ? 1 : 0;
  case libretro::JOYPAD_DOWN:
    return input.down ? 1 : 0;
  case libretro::JOYPAD_LEFT:
    return input.left ? 1 : 0;
  case libretro::JOYPAD_RIGHT:
    return input.right ? 1 : 0;
  case libretro::JOYPAD_B:
    return input.fire ? 1 : 0;
  case libretro::JOYPAD_A:
    return input.fire ? 1 : 0;
  case libretro::JOYPAD_SELECT:
    return input.select ? 1 : 0;
  case libretro::JOYPAD_START:
    return input.start ? 1 : 0;
  case libretro::JOYPAD_X:
    return input.reset ? 1 : 0;
  default:
    return 0;
  }
}

Atari2600Emulation::InputMode Atari2600Emulation::globalInputMode =
    Atari2600Emulation::InputMode::KeyboardAndController;
Atari2600Emulation::KeyBindings Atari2600Emulation::globalKeyBindings = {};
Atari2600Emulation::ControllerBindings
    Atari2600Emulation::globalControllerBindings = {};
bool Atari2600Emulation::globalInputConfigured = false;

Atari2600Emulation::Atari2600Emulation()
    : backend(nullptr), isRunning(false), window(nullptr), renderer(nullptr),
      screenTexture(nullptr), controller(nullptr), workerInput{},
      inputMode(InputMode::KeyboardAndController) {
  setDefaultBindings();

  if (globalInputConfigured) {
    inputMode = globalInputMode;
    keyBindings = globalKeyBindings;
    controllerBindings = globalControllerBindings;
  } else {
    saveGlobalInputSettings();
  }
}

Atari2600Emulation::~Atari2600Emulation() {
  unloadRom();
  cleanupSDL();
}

void Atari2600Emulation::setDefaultBindings() {
  keyBindings.up = SDLK_UP;
  keyBindings.down = SDLK_DOWN;
  keyBindings.left = SDLK_LEFT;
  keyBindings.right = SDLK_RIGHT;
  keyBindings.fire = SDLK_z;
  keyBindings.select = SDLK_RSHIFT;
  keyBindings.start = SDLK_RETURN;
  keyBindings.reset = SDLK_BACKSPACE;

  controllerBindings.up = SDL_CONTROLLER_BUTTON_DPAD_UP;
  controllerBindings.down = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  controllerBindings.left = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  controllerBindings.right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  controllerBindings.fire = SDL_CONTROLLER_BUTTON_A;
  controllerBindings.select = SDL_CONTROLLER_BUTTON_BACK;
  controllerBindings.start = SDL_CONTROLLER_BUTTON_START;
  controllerBindings.reset = SDL_CONTROLLER_BUTTON_X;
}

void Atari2600Emulation::saveGlobalInputSettings() {
  globalInputMode = inputMode;
  globalKeyBindings = keyBindings;
  globalControllerBindings = controllerBindings;
  globalInputConfigured = true;
}

void Atari2600Emulation::configureInputMenu() {
  while (true) {
    std::cout << "\n=== Atari 2600 Input Configuration ===\n";
    std::cout << "1) Input mode: Keyboard only\n";
    std::cout << "2) Input mode: Controller only\n";
    std::cout << "3) Input mode: Keyboard + Controller\n";
    std::cout << "4) Remap keyboard keys\n";
    std::cout << "5) Select controller\n";
    std::cout << "6) Remap controller buttons\n";
    std::cout << "0) Save and exit\n";
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
          {"UP", &keyBindings.up},       {"DOWN", &keyBindings.down},
          {"LEFT", &keyBindings.left},   {"RIGHT", &keyBindings.right},
          {"FIRE", &keyBindings.fire},   {"SELECT", &keyBindings.select},
          {"START", &keyBindings.start}, {"RESET", &keyBindings.reset}};

      std::cout << "Enter key names recognized by SDL (e.g. Up, Down, Left, "
                   "Right, Z, Return, Backspace).\n";
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
                                    {"FIRE", &controllerBindings.fire},
                                    {"SELECT", &controllerBindings.select},
                                    {"START", &controllerBindings.start},
                                    {"RESET", &controllerBindings.reset}};

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
  std::cout << "Atari 2600 controls saved.\n";
}

void Atari2600Emulation::configureGlobalControls() {
  Atari2600Emulation temp;
  temp.configureInputMenu();
}

bool Atari2600Emulation::loadRom(const string &path) {
  unloadRom();

  auto libretroBackend = std::make_unique<LibretroAtari2600Backend>();
  if (libretroBackend->isAvailable()) {
    backend = std::move(libretroBackend);
  } else {
    backend = std::make_unique<Atari2600UnavailableBackend>();
  }

  if (!backend->loadRom(path)) {
    backend.reset();
    return false;
  }

  romPath = path;
  isRunning = true;
  return true;
}

void Atari2600Emulation::unloadRom() {
  isRunning = false;
  backend.reset();
  romPath.clear();
}

std::vector<unsigned char> Atari2600Emulation::dumpSaveData() const {
  if (!backend) {
    return {};
  }
  return backend->dumpSaveData();
}

bool Atari2600Emulation::restoreSaveData(
    const std::vector<unsigned char> &data) {
  if (!backend) {
    return false;
  }
  return backend->restoreSaveData(data);
}

void Atari2600Emulation::nextFrame() {
  if (backend && !backend->nextFrame()) {
    isRunning = false;
  }
}

void Atari2600Emulation::setInput(const atari2600_input_t &input) {
  if (!backend) {
    return;
  }

  std::lock_guard<std::mutex> lock(inputMutex);
  workerInput = input;
  backend->setInput(input);
}

atari2600_color_t Atari2600Emulation::getPixel(int x, int y) const {
  if (!backend) {
    return {0, 0, 0, 255};
  }
  return backend->getPixel(x, y);
}

void Atari2600Emulation::displayFrame() const {
  if (!backend) {
    return;
  }

  const int width = backend->getFrameWidth();
  const int height = backend->getFrameHeight();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const atari2600_color_t pixel = backend->getPixel(x, y);
      if (pixel.r > 180 || pixel.g > 180 || pixel.b > 180) {
        std::cout << "#";
      } else {
        std::cout << " ";
      }
    }
    std::cout << "\n";
  }
}

void Atari2600Emulation::initSDL() {
  if (window && renderer && screenTexture) {
    return;
  }

  if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) ==
      0) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) !=
        0) {
      std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
      return;
    }
  }

  const int width = backend ? backend->getFrameWidth() : 160;
  const int height = backend ? backend->getFrameHeight() : 192;
  window = SDL_CreateWindow("OmniCon Atari 2600", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 960, 720,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (!window) {
    std::cerr << "SDL window failed: " << SDL_GetError() << "\n";
    return;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

  renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    std::cerr << "SDL renderer failed: " << SDL_GetError() << "\n";
    return;
  }

  if (backend && !backend->initializeAudio()) {
    std::cerr << "Atari 2600 audio initialization failed.\n";
  }

  screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!screenTexture) {
    std::cerr << "SDL texture failed: " << SDL_GetError() << "\n";
  } else {
    SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
  }

  openControllerByIndex(0);
}

void Atari2600Emulation::cleanupSDL() {
  if (controller) {
    SDL_GameControllerClose(controller);
    controller = nullptr;
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
}

bool Atari2600Emulation::openControllerByIndex(int index) {
  if (controller) {
    SDL_GameControllerClose(controller);
    controller = nullptr;
  }

  const int count = SDL_NumJoysticks();
  if (count <= 0 || index < 0 || index >= count ||
      !SDL_IsGameController(index)) {
    return false;
  }

  controller = SDL_GameControllerOpen(index);
  return controller != nullptr;
}

void Atari2600Emulation::listControllers() const {
  const int count = SDL_NumJoysticks();
  std::cout << "Controllers: " << count << "\n";
  for (int i = 0; i < count; ++i) {
    std::cout << "  [" << i << "] "
              << (SDL_IsGameController(i) ? "game controller" : "joystick")
              << " - " << SDL_JoystickNameForIndex(i) << "\n";
  }
}

void Atari2600Emulation::renderFrame() {
  if (!backend || !renderer || !screenTexture) {
    return;
  }

  const int width = backend->getFrameWidth();
  const int height = backend->getFrameHeight();
  int textureWidth = 0;
  int textureHeight = 0;
  if (SDL_QueryTexture(screenTexture, nullptr, nullptr, &textureWidth,
                       &textureHeight) == 0) {
    if (textureWidth != width || textureHeight != height) {
      SDL_DestroyTexture(screenTexture);
      screenTexture =
          SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                            SDL_TEXTUREACCESS_STREAMING, width, height);
      if (!screenTexture) {
        std::cerr << "SDL texture resize failed: " << SDL_GetError() << "\n";
        return;
      }
      SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
    }
  }

  const size_t pixelCount = static_cast<size_t>(width * height);
  frameBuffer.resize(pixelCount * 4, 0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const atari2600_color_t pixel = backend->getPixel(x, y);
      const size_t out = static_cast<size_t>(y * width + x) * 4;
      frameBuffer[out + 0] = pixel.r;
      frameBuffer[out + 1] = pixel.g;
      frameBuffer[out + 2] = pixel.b;
      frameBuffer[out + 3] = pixel.a;
    }
  }

  SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), width * 4);

  int outputWidth = 0;
  int outputHeight = 0;
  SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);
  SDL_Rect viewport{0, 0, outputWidth, outputHeight};

  const double targetAspect = backend->getAspectRatio();
  const double windowAspect =
      outputHeight > 0 ? static_cast<double>(outputWidth) / outputHeight
                       : targetAspect;

  if (windowAspect > targetAspect) {
    viewport.w = static_cast<int>(outputHeight * targetAspect);
    viewport.h = outputHeight;
    viewport.x = (outputWidth - viewport.w) / 2;
    viewport.y = 0;
  } else {
    viewport.w = outputWidth;
    viewport.h = static_cast<int>(outputWidth / targetAspect);
    viewport.x = 0;
    viewport.y = (outputHeight - viewport.h) / 2;
  }

  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, screenTexture, nullptr, &viewport);
  SDL_RenderPresent(renderer);
}

void Atari2600Emulation::handleKeyboardEvent(SDL_Keycode key, bool pressed,
                                             atari2600_input_t &input,
                                             bool &running) {
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
  if (key == keyBindings.fire)
    input.fire = pressed;
  if (key == keyBindings.select)
    input.select = pressed;
  if (key == keyBindings.start)
    input.start = pressed;
  if (key == keyBindings.reset)
    input.reset = pressed;
}

void Atari2600Emulation::handleControllerButtonEvent(Uint8 button, bool pressed,
                                                     atari2600_input_t &input,
                                                     bool &running) {
  if (button == SDL_CONTROLLER_BUTTON_GUIDE && pressed) {
    running = false;
    return;
  }

  if (button == controllerBindings.up)
    input.up = pressed;
  if (button == controllerBindings.down)
    input.down = pressed;
  if (button == controllerBindings.left)
    input.left = pressed;
  if (button == controllerBindings.right)
    input.right = pressed;
  if (button == controllerBindings.fire)
    input.fire = pressed;
  if (button == controllerBindings.select)
    input.select = pressed;
  if (button == controllerBindings.start)
    input.start = pressed;
  if (button == controllerBindings.reset)
    input.reset = pressed;
}

void Atari2600Emulation::runInteractive() {
  if (!backend) {
    std::cerr << "No Atari 2600 backend loaded.\n";
    return;
  }

  initSDL();
  if (!window || !renderer || !screenTexture) {
    std::cerr << "Failed to initialize SDL runtime for Atari 2600.\n";
    return;
  }

  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i)) {
      openControllerByIndex(i);
      break;
    }
  if (controller && inputMode == InputMode::KeyboardOnly)
    inputMode = InputMode::KeyboardAndController;

  bool running = true;
  atari2600_input_t input{};

  const double fps = std::max(1.0, backend->getNominalFps());
  const auto frameDuration = std::chrono::duration<double>(1.0 / fps);

  while (running && isRunning.load()) {
    const auto frameStart = std::chrono::steady_clock::now();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        if (inputMode != InputMode::ControllerOnly) {
          handleKeyboardEvent(event.key.keysym.sym, true, input, running);
        }
      } else if (event.type == SDL_KEYUP) {
        if (inputMode != InputMode::ControllerOnly) {
          handleKeyboardEvent(event.key.keysym.sym, false, input, running);
        }
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        if (inputMode != InputMode::KeyboardOnly) {
          handleControllerButtonEvent(event.cbutton.button, true, input,
                                      running);
        }
      } else if (event.type == SDL_CONTROLLERBUTTONUP) {
        if (inputMode != InputMode::KeyboardOnly) {
          handleControllerButtonEvent(event.cbutton.button, false, input,
                                      running);
        }
      } else if (event.type == SDL_CONTROLLERDEVICEADDED &&
                 controller == nullptr) {
        openControllerByIndex(event.cdevice.which);
        if (controller && inputMode == InputMode::KeyboardOnly)
          inputMode = InputMode::KeyboardAndController;
      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED &&
                 controller != nullptr) {
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) ==
            event.cdevice.which) {
          SDL_GameControllerClose(controller);
          controller = nullptr;
        }
      }
    }

    setInput(input);
    nextFrame();
    renderFrame();

    const auto frameEnd = std::chrono::steady_clock::now();
    const auto elapsed = frameEnd - frameStart;
    if (elapsed < frameDuration) {
      std::this_thread::sleep_for(frameDuration - elapsed);
    }
  }

  cleanupSDL();
  isRunning = false;
}