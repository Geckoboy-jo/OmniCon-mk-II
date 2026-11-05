#include "GameCube.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

// ─── Abstract backend ────────────────────────────────────────────────────────

class GameCubeBackend {
public:
  virtual ~GameCubeBackend() = default;
  virtual bool loadRom(const std::string &path) = 0;
  virtual bool nextFrame() = 0;
  virtual void setInput(const gamecube_input_t &input) = 0;
  virtual gamecube_color_t getPixel(int x, int y) const = 0;
  virtual int getFrameWidth() const { return 320; }
  virtual int getFrameHeight() const { return 240; }
  virtual double getAspectRatio() const { return 4.0 / 3.0; }
  virtual double getNominalFps() const { return 60.0; }
  virtual bool initializeAudio() { return true; }
  virtual std::vector<unsigned char> dumpSaveData() const { return {}; }
  virtual bool restoreSaveData(const std::vector<unsigned char> &) {
    return false;
  }
};

// ─── Unavailable stub ────────────────────────────────────────────────────────

class GameCubeUnavailableBackend final : public GameCubeBackend {
public:
  bool loadRom(const std::string &) override {
    std::cerr << "[GameCube] Dolphin libretro core is not available.\n";
    std::cerr << "      Install with: sudo apt-get install libretro-dolphin\n";
    std::cerr
        << "      Or place dolphin_libretro.so in third_party/libretro/\n";
    std::cerr
        << "      Or set OMNICON_GAMECUBE_CORE to the path of the core .so\n";
    return false;
  }
  bool nextFrame() override { return false; }
  void setInput(const gamecube_input_t &) override {}
  gamecube_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }
};

// ─── libretro helpers ────────────────────────────────────────────────────────

namespace libretroGameCube {
static constexpr unsigned ENV_GET_LOG_INTERFACE = 27;
static constexpr unsigned ENV_SET_HW_RENDER = 14;
static constexpr unsigned ENV_SET_PIXEL_FORMAT = 10;
static constexpr unsigned ENV_GET_VARIABLE = 15;
static constexpr unsigned ENV_SET_VARIABLES = 16;
static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
static constexpr unsigned ENV_SET_GEOMETRY = 37;
static constexpr unsigned PIXEL_0RGB1555 = 0;
static constexpr unsigned PIXEL_XRGB8888 = 1;
static constexpr unsigned PIXEL_RGB565 = 2;
static constexpr unsigned DEVICE_JOYPAD = 1;
static constexpr unsigned JOYPAD_B = 0; // Cross
static constexpr unsigned JOYPAD_Y = 1; // Square
static constexpr unsigned JOYPAD_SELECT = 2;
static constexpr unsigned JOYPAD_START = 3;
static constexpr unsigned JOYPAD_UP = 4;
static constexpr unsigned JOYPAD_DOWN = 5;
static constexpr unsigned JOYPAD_LEFT = 6;
static constexpr unsigned JOYPAD_RIGHT = 7;
static constexpr unsigned JOYPAD_A = 8;  // Circle
static constexpr unsigned JOYPAD_X = 9;  // Triangle
static constexpr unsigned JOYPAD_L = 10; // L1
static constexpr unsigned JOYPAD_R = 11; // R1
static constexpr unsigned JOYPAD_L2 = 12;
static constexpr unsigned JOYPAD_R2 = 13;
static constexpr unsigned JOYPAD_L3 = 14;
static constexpr unsigned JOYPAD_R3 = 15;

struct game_info {
  const char *path;
  const void *data;
  size_t size;
  const char *meta;
};
struct game_geometry {
  unsigned base_width, base_height, max_width, max_height;
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
enum class hw_context_type : unsigned {
  NONE = 0,
  OPENGL = 1,
  OPENGLES2 = 2,
  OPENGL_CORE = 3,
  OPENGLES3 = 4
};
using hw_context_reset_t = void (*)();
using hw_get_current_framebuffer_t = uintptr_t (*)();
using hw_get_proc_address_t = void *(*)(const char *);
struct hw_render_callback {
  hw_context_type context_type;
  hw_context_reset_t context_reset;
  hw_get_current_framebuffer_t get_current_framebuffer;
  hw_get_proc_address_t get_proc_address;
  bool depth, stencil, bottom_left_origin;
  unsigned version_major, version_minor;
  bool cache_context;
  hw_context_reset_t context_destroy;
  bool debug_context;
};
using environment_t = bool (*)(unsigned, void *);
using video_refresh_t = void (*)(const void *, unsigned, unsigned, size_t);
using audio_sample_t = void (*)(int16_t, int16_t);
using audio_sample_batch_t = size_t (*)(const int16_t *, size_t);
using input_poll_t = void (*)(void);
using input_state_t = int16_t (*)(unsigned, unsigned, unsigned, unsigned);
using log_printf_t = void (*)(int, const char *, ...);
struct log_callback {
  log_printf_t log;
};
} // namespace libretroGameCube

// ─── Libretro backend ────────────────────────────────────────────────────────

class LibretroGameCubeBackend final : public GameCubeBackend {
public:
  // Standard install paths for common GameCube libretro cores
  static constexpr const char *kCorePath1 =
      "/usr/lib/x86_64-linux-gnu/libretro/dolphin_libretro.so";
  static constexpr const char *kCorePath2 =
      "/usr/lib/x86_64-linux-gnu/libretro/dolphin_libretro.so";
  static constexpr const char *kCorePath3 =
      "/usr/lib/x86_64-linux-gnu/libretro/dolphin_libretro.so";
  static constexpr const char *kWorkspacePath1 =
      "third_party/libretro/dolphin_libretro.so";
  static constexpr const char *kWorkspacePath2 =
      "third_party/libretro/dolphin_libretro.so";

  LibretroGameCubeBackend();
  ~LibretroGameCubeBackend() override;

  bool isAvailable() const { return coreHandle != nullptr; }
  bool loadRom(const std::string &path) override;
  bool nextFrame() override;
  void setInput(const gamecube_input_t &input) override;
  gamecube_color_t getPixel(int x, int y) const override;
  int getFrameWidth() const override { return frameWidth; }
  int getFrameHeight() const override { return frameHeight; }
  double getAspectRatio() const override { return aspectRatio; }
  double getNominalFps() const override { return nominalFps; }

  bool onEnvironment(unsigned cmd, void *data);
  void onVideoRefresh(const void *data, unsigned w, unsigned h, size_t pitch);
  std::vector<unsigned char> dumpSaveData() const override;
  bool restoreSaveData(const std::vector<unsigned char> &data) override;

private:
  static std::string resolveCorePath();
  bool loadSymbols();
  bool ensureAudioDevice(int sampleRate);
  void closeAudioDevice();
  void queueAudioFrames(const int16_t *data, size_t frames);
  bool initializeAudio() override;
  void ensureLibretroDirectories();
  bool initGLContext();
  void cleanupGLContext();
  void createFbo();
  void readGLPixels();

  std::string corePath;
  void *coreHandle = nullptr;
  void (*fn_init)() = nullptr;
  void (*fn_deinit)() = nullptr;
  void (*fn_set_environment)(libretroGameCube::environment_t) = nullptr;
  void (*fn_set_video_refresh)(libretroGameCube::video_refresh_t) = nullptr;
  void (*fn_set_audio_sample)(libretroGameCube::audio_sample_t) = nullptr;
  void (*fn_set_audio_sample_batch)(libretroGameCube::audio_sample_batch_t) =
      nullptr;
  void (*fn_set_input_poll)(libretroGameCube::input_poll_t) = nullptr;
  void (*fn_set_input_state)(libretroGameCube::input_state_t) = nullptr;
  void (*fn_get_system_av_info)(libretroGameCube::av_info *) = nullptr;
  bool (*fn_load_game)(const libretroGameCube::game_info *) = nullptr;
  void (*fn_unload_game)() = nullptr;
  void (*fn_run)() = nullptr;
  size_t (*fn_serialize_size)() = nullptr;
  bool (*fn_serialize)(void *, size_t) = nullptr;
  bool (*fn_unserialize)(const void *, size_t) = nullptr;

  std::vector<unsigned char> framePixels;
  std::string loadedRomPath;
  std::string systemDirectory;
  std::string saveDirectory;
  std::string contentDirectory;
  gamecube_input_t currentInput{};
  bool gameLoaded = false;
  bool hwRenderRequested = false;
  SDL_Window *hwWindow = nullptr;
  SDL_GLContext hwGLCtx = nullptr;
  unsigned hwFbo = 0;
  unsigned hwColorTex = 0;
  unsigned hwDepthRb = 0;
  libretroGameCube::hw_context_reset_t hwContextReset = nullptr;
  libretroGameCube::hw_context_reset_t hwContextDestroy = nullptr;
  bool hwRenderActive = false;
  void (*glGenFramebuffers_)(int, unsigned *) = nullptr;
  void (*glBindFramebuffer_)(unsigned, unsigned) = nullptr;
  void (*glGenTextures_)(int, unsigned *) = nullptr;
  void (*glBindTexture_)(unsigned, unsigned) = nullptr;
  void (*glTexImage2D_)(unsigned, int, int, int, int, int, unsigned, unsigned,
                        const void *) = nullptr;
  void (*glFramebufferTexture2D_)(unsigned, unsigned, unsigned, unsigned,
                                  int) = nullptr;
  void (*glGenRenderbuffers_)(int, unsigned *) = nullptr;
  void (*glBindRenderbuffer_)(unsigned, unsigned) = nullptr;
  void (*glRenderbufferStorage_)(unsigned, unsigned, int, int) = nullptr;
  void (*glFramebufferRenderbuffer_)(unsigned, unsigned, unsigned,
                                     unsigned) = nullptr;
  void (*glDeleteFramebuffers_)(int, const unsigned *) = nullptr;
  void (*glDeleteTextures_)(int, const unsigned *) = nullptr;
  void (*glDeleteRenderbuffers_)(int, const unsigned *) = nullptr;
  void (*glReadPixels_)(int, int, int, int, unsigned, unsigned,
                        void *) = nullptr;
  void (*glViewport_)(int, int, int, int) = nullptr;
  unsigned pixelFormat = libretroGameCube::PIXEL_0RGB1555;
  int frameWidth = 320;
  int frameHeight = 240;
  double aspectRatio = 4.0 / 3.0;
  double nominalFps = 60.0;
  SDL_AudioDeviceID audioDevice = 0;
  int audioSampleRate = 44100;
  int coreSampleRate = 44100;
  size_t maxQueuedAudioBytes = 0;

  static LibretroGameCubeBackend *s_instance;
  static bool s_envCb(unsigned cmd, void *data);
  static void s_videoCb(const void *data, unsigned w, unsigned h, size_t pitch);
  static void s_audioCb(int16_t left, int16_t right);
  static size_t s_audioBatchCb(const int16_t *data, size_t frames);
  static void s_inputPollCb();
  static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index,
                                unsigned id);
  static void s_logNoop(int, const char *, ...);
  static uintptr_t s_getCurrentFramebuffer();
  static void *s_getProcAddress(const char *sym);
};

LibretroGameCubeBackend *LibretroGameCubeBackend::s_instance = nullptr;

std::string LibretroGameCubeBackend::resolveCorePath() {
  const char *envPath = std::getenv("OMNICON_GAMECUBE_CORE");
  if (envPath && *envPath && std::filesystem::exists(envPath))
    return envPath;
  if (std::filesystem::exists(kWorkspacePath1))
    return kWorkspacePath1;
  if (std::filesystem::exists(kWorkspacePath2))
    return kWorkspacePath2;
  if (std::filesystem::exists(kCorePath1))
    return kCorePath1;
  if (std::filesystem::exists(kCorePath2))
    return kCorePath2;
  if (std::filesystem::exists(kCorePath3))
    return kCorePath3;
  return "";
}

LibretroGameCubeBackend::LibretroGameCubeBackend()
    : corePath(resolveCorePath()) {
  if (corePath.empty())
    return;

  coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
  if (!coreHandle) {
    std::cerr << "[GameCube] Failed to load core " << corePath << ": "
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
  setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
  initGLContext();
  fn_set_environment(s_envCb);
  fn_set_video_refresh(s_videoCb);
  fn_set_audio_sample(s_audioCb);
  fn_set_audio_sample_batch(s_audioBatchCb);
  fn_set_input_poll(s_inputPollCb);
  fn_set_input_state(s_inputStateCb);
  fn_init();
  framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
}

LibretroGameCubeBackend::~LibretroGameCubeBackend() {
  closeAudioDevice();
  if (coreHandle) {
    if (gameLoaded)
      fn_unload_game();
    fn_deinit();
    dlclose(coreHandle);
    coreHandle = nullptr;
  }
  cleanupGLContext();
  if (s_instance == this)
    s_instance = nullptr;
}

bool LibretroGameCubeBackend::loadSymbols() {
  auto load = [this](const char *name) -> void * {
    void *sym = dlsym(coreHandle, name);
    if (!sym)
      std::cerr << "[GameCube] Missing symbol: " << name << "\n";
    return sym;
  };
  fn_init = reinterpret_cast<void (*)()>(load("retro_init"));
  fn_deinit = reinterpret_cast<void (*)()>(load("retro_deinit"));
  fn_set_environment =
      reinterpret_cast<void (*)(libretroGameCube::environment_t)>(
          load("retro_set_environment"));
  fn_set_video_refresh =
      reinterpret_cast<void (*)(libretroGameCube::video_refresh_t)>(
          load("retro_set_video_refresh"));
  fn_set_audio_sample =
      reinterpret_cast<void (*)(libretroGameCube::audio_sample_t)>(
          load("retro_set_audio_sample"));
  fn_set_audio_sample_batch =
      reinterpret_cast<void (*)(libretroGameCube::audio_sample_batch_t)>(
          load("retro_set_audio_sample_batch"));
  fn_set_input_poll =
      reinterpret_cast<void (*)(libretroGameCube::input_poll_t)>(
          load("retro_set_input_poll"));
  fn_set_input_state =
      reinterpret_cast<void (*)(libretroGameCube::input_state_t)>(
          load("retro_set_input_state"));
  fn_get_system_av_info =
      reinterpret_cast<void (*)(libretroGameCube::av_info *)>(
          load("retro_get_system_av_info"));
  fn_load_game =
      reinterpret_cast<bool (*)(const libretroGameCube::game_info *)>(
          load("retro_load_game"));
  fn_unload_game = reinterpret_cast<void (*)()>(load("retro_unload_game"));
  fn_run = reinterpret_cast<void (*)()>(load("retro_run"));
  // Optional savestates
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

void LibretroGameCubeBackend::ensureLibretroDirectories() {
  const std::filesystem::path root =
      std::filesystem::current_path() / "third_party" / "libretro";
  const std::filesystem::path sysDir = root / "system";
  const std::filesystem::path saveDir = root / "save";
  const std::filesystem::path contDir = root / "content";
  std::error_code ec;
  std::filesystem::create_directories(sysDir, ec);
  std::filesystem::create_directories(saveDir, ec);
  std::filesystem::create_directories(contDir, ec);
  // Also ensure gamecube saves directory exists
  std::filesystem::create_directories(
      std::filesystem::current_path() / "saves" / "gamecube", ec);
  systemDirectory = sysDir.string();
  saveDirectory = saveDir.string();
  contentDirectory = contDir.string();
}

bool LibretroGameCubeBackend::initGLContext() {
  const Uint32 needed =
      SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER;
  const Uint32 missing = needed & ~SDL_WasInit(0);
  if (missing && SDL_InitSubSystem(missing) != 0) {
    std::cerr << "[GameCube] SDL init for GL failed: " << SDL_GetError()
              << "\n";
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  hwWindow =
      SDL_CreateWindow("OmniConGameCube_GL", 0, 0, frameWidth, frameHeight,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!hwWindow) {
    std::cerr << "[GameCube] Failed to create GL window: " << SDL_GetError()
              << "\n";
    return false;
  }
  hwGLCtx = SDL_GL_CreateContext(hwWindow);
  if (!hwGLCtx) {
    std::cerr << "[GameCube] Failed to create GL context: " << SDL_GetError()
              << "\n";
    SDL_DestroyWindow(hwWindow);
    hwWindow = nullptr;
    return false;
  }
  SDL_GL_MakeCurrent(hwWindow, hwGLCtx);

  auto load = [](const char *sym) { return SDL_GL_GetProcAddress(sym); };
  glGenFramebuffers_ = (void (*)(int, unsigned *))load("glGenFramebuffers");
  glBindFramebuffer_ = (void (*)(unsigned, unsigned))load("glBindFramebuffer");
  glGenTextures_ = (void (*)(int, unsigned *))load("glGenTextures");
  glBindTexture_ = (void (*)(unsigned, unsigned))load("glBindTexture");
  glTexImage2D_ = (void (*)(unsigned, int, int, int, int, int, unsigned,
                            unsigned, const void *))load("glTexImage2D");
  glFramebufferTexture2D_ = (void (*)(unsigned, unsigned, unsigned, unsigned,
                                      int))load("glFramebufferTexture2D");
  glGenRenderbuffers_ = (void (*)(int, unsigned *))load("glGenRenderbuffers");
  glBindRenderbuffer_ =
      (void (*)(unsigned, unsigned))load("glBindRenderbuffer");
  glRenderbufferStorage_ =
      (void (*)(unsigned, unsigned, int, int))load("glRenderbufferStorage");
  glFramebufferRenderbuffer_ = (void (*)(
      unsigned, unsigned, unsigned, unsigned))load("glFramebufferRenderbuffer");
  glDeleteFramebuffers_ =
      (void (*)(int, const unsigned *))load("glDeleteFramebuffers");
  glDeleteTextures_ = (void (*)(int, const unsigned *))load("glDeleteTextures");
  glDeleteRenderbuffers_ =
      (void (*)(int, const unsigned *))load("glDeleteRenderbuffers");
  glReadPixels_ = (void (*)(int, int, int, int, unsigned, unsigned,
                            void *))load("glReadPixels");
  glViewport_ = (void (*)(int, int, int, int))load("glViewport");

  const bool ok = glGenFramebuffers_ && glBindFramebuffer_ && glGenTextures_ &&
                  glBindTexture_ && glTexImage2D_ && glFramebufferTexture2D_ &&
                  glGenRenderbuffers_ && glBindRenderbuffer_ &&
                  glRenderbufferStorage_ && glFramebufferRenderbuffer_ &&
                  glReadPixels_;
  if (!ok)
    std::cerr
        << "[GameCube] Some GL functions unavailable; HW rendering may fail.\n";
  return ok;
}

void LibretroGameCubeBackend::cleanupGLContext() {
  if (hwGLCtx && hwWindow) {
    SDL_GL_MakeCurrent(hwWindow, hwGLCtx);
    if (hwContextDestroy)
      hwContextDestroy();
    if (hwFbo && glDeleteFramebuffers_)
      glDeleteFramebuffers_(1, &hwFbo);
    if (hwColorTex && glDeleteTextures_)
      glDeleteTextures_(1, &hwColorTex);
    if (hwDepthRb && glDeleteRenderbuffers_)
      glDeleteRenderbuffers_(1, &hwDepthRb);
    hwFbo = hwColorTex = hwDepthRb = 0;
  }
  if (hwGLCtx) {
    SDL_GL_DeleteContext(hwGLCtx);
    hwGLCtx = nullptr;
  }
  if (hwWindow) {
    SDL_DestroyWindow(hwWindow);
    hwWindow = nullptr;
  }
}

void LibretroGameCubeBackend::createFbo() {
  if (!hwGLCtx || !glGenFramebuffers_ || !glBindFramebuffer_ ||
      !glGenTextures_ || !glBindTexture_ || !glTexImage2D_ ||
      !glFramebufferTexture2D_ || !glGenRenderbuffers_ ||
      !glBindRenderbuffer_ || !glRenderbufferStorage_ ||
      !glFramebufferRenderbuffer_)
    return;

  if (hwFbo && glDeleteFramebuffers_)
    glDeleteFramebuffers_(1, &hwFbo);
  if (hwColorTex && glDeleteTextures_)
    glDeleteTextures_(1, &hwColorTex);
  if (hwDepthRb && glDeleteRenderbuffers_)
    glDeleteRenderbuffers_(1, &hwDepthRb);
  hwFbo = hwColorTex = hwDepthRb = 0;

  constexpr unsigned GL_FRAMEBUFFER = 0x8D40;
  constexpr unsigned GL_COLOR_ATTACHMENT0 = 0x8CE0;
  constexpr unsigned GL_RENDERBUFFER = 0x8D41;
  constexpr unsigned GL_DEPTH_STENCIL_ATTACH = 0x821A;
  constexpr unsigned GL_DEPTH24_STENCIL8 = 0x88F0;
  constexpr unsigned GL_TEXTURE_2D = 0x0DE1;
  constexpr unsigned GL_RGBA8 = 0x8058;
  constexpr unsigned GL_RGBA = 0x1908;
  constexpr unsigned GL_UNSIGNED_BYTE = 0x1401;

  glGenFramebuffers_(1, &hwFbo);
  glBindFramebuffer_(GL_FRAMEBUFFER, hwFbo);

  glGenTextures_(1, &hwColorTex);
  glBindTexture_(GL_TEXTURE_2D, hwColorTex);
  glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, frameWidth, frameHeight, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          hwColorTex, 0);

  glGenRenderbuffers_(1, &hwDepthRb);
  glBindRenderbuffer_(GL_RENDERBUFFER, hwDepthRb);
  glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, frameWidth,
                         frameHeight);
  glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACH,
                             GL_RENDERBUFFER, hwDepthRb);
}

void LibretroGameCubeBackend::readGLPixels() {
  if (!hwGLCtx || !hwFbo || !glBindFramebuffer_ || !glReadPixels_)
    return;

  constexpr unsigned GL_FRAMEBUFFER = 0x8D40;
  constexpr unsigned GL_RGBA = 0x1908;
  constexpr unsigned GL_UNSIGNED_BYTE = 0x1401;

  SDL_GL_MakeCurrent(hwWindow, hwGLCtx);
  glBindFramebuffer_(GL_FRAMEBUFFER, hwFbo);
  if (glViewport_)
    glViewport_(0, 0, frameWidth, frameHeight);

  const size_t sz = static_cast<size_t>(frameWidth * frameHeight) * 4;
  framePixels.resize(sz);
  glReadPixels_(0, 0, frameWidth, frameHeight, GL_RGBA, GL_UNSIGNED_BYTE,
                framePixels.data());

  // OpenGL readback origin is bottom-left; flip rows so SDL displays upright.
  const size_t rowBytes = static_cast<size_t>(frameWidth) * 4;
  std::vector<unsigned char> tmp(rowBytes);
  for (int y = 0; y < frameHeight / 2; ++y) {
    unsigned char *top = framePixels.data() + static_cast<size_t>(y) * rowBytes;
    unsigned char *bot = framePixels.data() +
                         static_cast<size_t>(frameHeight - 1 - y) * rowBytes;
    std::memcpy(tmp.data(), top, rowBytes);
    std::memcpy(top, bot, rowBytes);
    std::memcpy(bot, tmp.data(), rowBytes);
  }
}

bool LibretroGameCubeBackend::ensureAudioDevice(int sampleRate) {
  if (audioDevice != 0 && audioSampleRate == sampleRate)
    return true;
  closeAudioDevice();
  SDL_AudioSpec desired{}, obtained{};
  desired.freq = sampleRate;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 2048;
  audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (audioDevice == 0) {
    std::cerr << "[GameCube] Audio open failed: " << SDL_GetError() << "\n";
    return false;
  }
  audioSampleRate = obtained.freq > 0 ? obtained.freq : sampleRate;
  maxQueuedAudioBytes = static_cast<size_t>(
      audioSampleRate * obtained.channels * sizeof(int16_t) * 20 / 100);
  SDL_PauseAudioDevice(audioDevice, 0);
  return true;
}

void LibretroGameCubeBackend::closeAudioDevice() {
  if (audioDevice != 0) {
    SDL_ClearQueuedAudio(audioDevice);
    SDL_CloseAudioDevice(audioDevice);
    audioDevice = 0;
  }
}

void LibretroGameCubeBackend::queueAudioFrames(const int16_t *data,
                                               size_t frames) {
  if (!data || frames == 0 || audioDevice == 0)
    return;
  const size_t bytes = frames * 2 * sizeof(int16_t);
  if (SDL_GetQueuedAudioSize(audioDevice) > maxQueuedAudioBytes)
    return;
  SDL_QueueAudio(audioDevice, data, static_cast<Uint32>(bytes));
}

bool LibretroGameCubeBackend::initializeAudio() {
  return ensureAudioDevice(coreSampleRate);
}

bool LibretroGameCubeBackend::loadRom(const std::string &path) {
  if (!coreHandle)
    return false;
  ensureLibretroDirectories();

  // GameCube disc images must be loaded by path — do not copy into memory
  loadedRomPath = std::filesystem::absolute(path).string();

  const std::filesystem::path parentDir =
      std::filesystem::path(path).parent_path();
  if (!parentDir.empty()) {
    std::error_code ec;
    if (std::filesystem::exists(parentDir, ec))
      contentDirectory = std::filesystem::absolute(parentDir).string();
  }

  if (gameLoaded) {
    fn_unload_game();
    gameLoaded = false;
  }
  hwRenderRequested = false;

  // Pass path + NULL data; GameCube cores require need_fullpath
  libretroGameCube::game_info info{loadedRomPath.c_str(), nullptr, 0, nullptr};
  if (!fn_load_game(&info)) {
    std::cerr << "[GameCube] Core rejected ROM: " << path << "\n";
    std::cerr << "      Ensure the Dolphin core can initialize GPU/audio on "
                 "this host.\n";
    return false;
  }

  if (hwRenderRequested && !hwRenderActive) {
    std::cerr << "[GameCube] Core requested HW rendering but backend failed to "
                 "activate it.\n";
    fn_unload_game();
    gameLoaded = false;
    return false;
  }
  gameLoaded = true;

  libretroGameCube::av_info av{};
  fn_get_system_av_info(&av);
  if (av.geometry.base_width > 0)
    frameWidth = static_cast<int>(av.geometry.base_width);
  if (av.geometry.base_height > 0)
    frameHeight = static_cast<int>(av.geometry.base_height);
  if (av.geometry.aspect_ratio > 0.0f)
    aspectRatio = av.geometry.aspect_ratio;
  else if (frameWidth > 0 && frameHeight > 0)
    aspectRatio = static_cast<double>(frameWidth) / frameHeight;
  if (av.timing.fps > 1.0)
    nominalFps = av.timing.fps;
  if (av.timing.sample_rate > 1000.0)
    coreSampleRate = static_cast<int>(av.timing.sample_rate);
  framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
  return true;
}

bool LibretroGameCubeBackend::nextFrame() {
  if (!coreHandle || !gameLoaded)
    return false;
  if (hwGLCtx && hwWindow)
    SDL_GL_MakeCurrent(hwWindow, hwGLCtx);

  if (hwRenderActive && hwContextReset) {
    createFbo();
    hwContextReset();
    hwContextReset = nullptr;
  }

  fn_run();

  if (hwRenderActive)
    readGLPixels();
  return true;
}

void LibretroGameCubeBackend::setInput(const gamecube_input_t &input) {
  currentInput = input;
}

gamecube_color_t LibretroGameCubeBackend::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight)
    return {0, 0, 0, 255};
  const size_t idx = static_cast<size_t>(y * frameWidth + x) * 4;
  if (idx + 3 >= framePixels.size())
    return {0, 0, 0, 255};
  return {framePixels[idx], framePixels[idx + 1], framePixels[idx + 2],
          framePixels[idx + 3]};
}

bool LibretroGameCubeBackend::onEnvironment(unsigned cmd, void *data) {
  if (cmd == 3) {
    if (data)
      *static_cast<bool *>(data) = true;
    return true;
  } // GET_OVERSCAN
  if (cmd == 9) {
    if (data)
      *static_cast<const char **>(data) = systemDirectory.c_str();
    return true;
  } // GET_SYSTEM_DIRECTORY
  if (cmd == 30) {
    if (data)
      *static_cast<const char **>(data) = contentDirectory.c_str();
    return true;
  } // GET_CONTENT_DIRECTORY
  if (cmd == 31) {
    if (data)
      *static_cast<const char **>(data) = saveDirectory.c_str();
    return true;
  } // GET_SAVE_DIRECTORY
  if (cmd == 18) {
    return true;
  } // SET_SUPPORT_NO_GAME
  if (cmd == libretroGameCube::ENV_GET_LOG_INTERFACE && data) {
    static_cast<libretroGameCube::log_callback *>(data)->log = s_logNoop;
    return true;
  }
  if (cmd == libretroGameCube::ENV_SET_HW_RENDER && data) {
    hwRenderRequested = true;
    if (!hwGLCtx) {
      std::cerr << "[GameCube] Core requested HW render but GL context is "
                   "unavailable.\n";
      return false;
    }
    auto *hw = static_cast<libretroGameCube::hw_render_callback *>(data);
    hw->get_current_framebuffer = s_getCurrentFramebuffer;
    hw->get_proc_address = s_getProcAddress;
    hwContextReset = hw->context_reset;
    hwContextDestroy = hw->context_destroy;
    hwRenderActive = true;
    return true;
  }
  if (cmd == libretroGameCube::ENV_SET_VARIABLES) {
    return true;
  }
  if (cmd == libretroGameCube::ENV_GET_VARIABLE_UPDATE) {
    if (data)
      *static_cast<bool *>(data) = false;
    return true;
  }
  if (cmd == libretroGameCube::ENV_GET_VARIABLE) {
    auto *v = static_cast<libretroGameCube::variable *>(data);
    if (!v || !v->key)
      return false;
    v->value = nullptr;
    return false;
  }
  if (cmd == libretroGameCube::ENV_SET_PIXEL_FORMAT && data) {
    pixelFormat = *reinterpret_cast<unsigned *>(data);
    return pixelFormat == libretroGameCube::PIXEL_0RGB1555 ||
           pixelFormat == libretroGameCube::PIXEL_XRGB8888 ||
           pixelFormat == libretroGameCube::PIXEL_RGB565;
  }
  if (cmd == libretroGameCube::ENV_SET_GEOMETRY && data) {
    const auto *g =
        reinterpret_cast<const libretroGameCube::game_geometry *>(data);
    if (g->base_width > 0)
      frameWidth = static_cast<int>(g->base_width);
    if (g->base_height > 0)
      frameHeight = static_cast<int>(g->base_height);
    if (g->aspect_ratio > 0.0f)
      aspectRatio = g->aspect_ratio;
    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    return true;
  }
  return false;
}

void LibretroGameCubeBackend::onVideoRefresh(const void *data, unsigned w,
                                             unsigned h, size_t pitch) {
  if (data == reinterpret_cast<const void *>(static_cast<uintptr_t>(-1))) {
    if (w > 0)
      frameWidth = static_cast<int>(w);
    if (h > 0)
      frameHeight = static_cast<int>(h);
    return;
  }
  // HW-rendered libretro cores may pass RETRO_HW_FRAME_BUFFER_VALID (-1)
  // instead of a CPU pixel buffer.
  if (!data || data == reinterpret_cast<const void *>(-1) || w == 0 || h == 0)
    return;
  frameWidth = static_cast<int>(w);
  frameHeight = static_cast<int>(h);
  framePixels.resize(static_cast<size_t>(w * h) * 4, 0);

  for (unsigned y = 0; y < h; ++y) {
    const auto *row = reinterpret_cast<const uint8_t *>(data) + y * pitch;
    for (unsigned x = 0; x < w; ++x) {
      const size_t out = static_cast<size_t>(y * w + x) * 4;
      if (pixelFormat == libretroGameCube::PIXEL_XRGB8888) {
        const uint32_t p = reinterpret_cast<const uint32_t *>(row)[x];
        framePixels[out + 0] = static_cast<uint8_t>((p >> 16) & 0xFF);
        framePixels[out + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
        framePixels[out + 2] = static_cast<uint8_t>(p & 0xFF);
        framePixels[out + 3] = 255;
      } else if (pixelFormat == libretroGameCube::PIXEL_RGB565) {
        const uint16_t p = reinterpret_cast<const uint16_t *>(row)[x];
        framePixels[out + 0] =
            static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
        framePixels[out + 1] =
            static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63);
        framePixels[out + 2] = static_cast<uint8_t>((p & 0x1F) * 255 / 31);
        framePixels[out + 3] = 255;
      } else { // 0RGB1555
        const uint16_t p = reinterpret_cast<const uint16_t *>(row)[x];
        framePixels[out + 0] =
            static_cast<uint8_t>(((p >> 10) & 0x1F) * 255 / 31);
        framePixels[out + 1] =
            static_cast<uint8_t>(((p >> 5) & 0x1F) * 255 / 31);
        framePixels[out + 2] = static_cast<uint8_t>((p & 0x1F) * 255 / 31);
        framePixels[out + 3] = 255;
      }
    }
  }
}

bool LibretroGameCubeBackend::s_envCb(unsigned cmd, void *data) {
  return s_instance ? s_instance->onEnvironment(cmd, data) : false;
}
void LibretroGameCubeBackend::s_videoCb(const void *d, unsigned w, unsigned h,
                                        size_t p) {
  if (s_instance)
    s_instance->onVideoRefresh(d, w, h, p);
}
void LibretroGameCubeBackend::s_audioCb(int16_t l, int16_t r) {
  if (s_instance) {
    const int16_t s[2] = {l, r};
    s_instance->queueAudioFrames(s, 1);
  }
}
size_t LibretroGameCubeBackend::s_audioBatchCb(const int16_t *d, size_t f) {
  if (s_instance && d && f)
    s_instance->queueAudioFrames(d, f);
  return f;
}
void LibretroGameCubeBackend::s_inputPollCb() {}
int16_t LibretroGameCubeBackend::s_inputStateCb(unsigned port, unsigned device,
                                                unsigned /*index*/,
                                                unsigned id) {
  if (!s_instance || port != 0)
    return 0;
  if (device != libretroGameCube::DEVICE_JOYPAD)
    return 0;
  const gamecube_input_t &in = s_instance->currentInput;
  switch (id) {
  case libretroGameCube::JOYPAD_B:
    return in.cross ? 1 : 0;
  case libretroGameCube::JOYPAD_A:
    return in.circle ? 1 : 0;
  case libretroGameCube::JOYPAD_Y:
    return in.square ? 1 : 0;
  case libretroGameCube::JOYPAD_X:
    return in.triangle ? 1 : 0;
  case libretroGameCube::JOYPAD_START:
    return in.start ? 1 : 0;
  case libretroGameCube::JOYPAD_SELECT:
    return in.select ? 1 : 0;
  case libretroGameCube::JOYPAD_UP:
    return in.up ? 1 : 0;
  case libretroGameCube::JOYPAD_DOWN:
    return in.down ? 1 : 0;
  case libretroGameCube::JOYPAD_LEFT:
    return in.left ? 1 : 0;
  case libretroGameCube::JOYPAD_RIGHT:
    return in.right ? 1 : 0;
  case libretroGameCube::JOYPAD_L:
    return in.l1 ? 1 : 0;
  case libretroGameCube::JOYPAD_R:
    return in.r1 ? 1 : 0;
  case libretroGameCube::JOYPAD_L2:
    return in.l2 ? 1 : 0;
  case libretroGameCube::JOYPAD_R2:
    return in.r2 ? 1 : 0;
  case libretroGameCube::JOYPAD_L3:
    return in.l3 ? 1 : 0;
  case libretroGameCube::JOYPAD_R3:
    return in.r3 ? 1 : 0;
  default:
    return 0;
  }
}

void LibretroGameCubeBackend::s_logNoop(int, const char *, ...) {}

uintptr_t LibretroGameCubeBackend::s_getCurrentFramebuffer() {
  return s_instance ? s_instance->hwFbo : 0;
}

void *LibretroGameCubeBackend::s_getProcAddress(const char *sym) {
  return SDL_GL_GetProcAddress(sym);
}

std::vector<unsigned char> LibretroGameCubeBackend::dumpSaveData() const {
  if (!coreHandle || !gameLoaded || !fn_serialize_size || !fn_serialize)
    return {};
  const size_t sz = fn_serialize_size();
  if (sz == 0)
    return {};
  std::vector<unsigned char> state(sz, 0);
  if (!fn_serialize(state.data(), sz))
    return {};
  return state;
}

bool LibretroGameCubeBackend::restoreSaveData(
    const std::vector<unsigned char> &data) {
  if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty())
    return false;
  return fn_unserialize(data.data(), data.size());
}

// ─── Local helpers
// ────────────────────────────────────────────────────────────

namespace {

std::string trimGameCube(const std::string &value) {
  const std::string ws = " \t\r\n";
  const std::size_t s = value.find_first_not_of(ws);
  if (s == std::string::npos)
    return "";
  return value.substr(s, value.find_last_not_of(ws) - s + 1);
}

std::string toUpperGameCube(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

SDL_GameControllerButton
parseControllerButtonGameCube(const std::string &input) {
  const std::string n = toUpperGameCube(trimGameCube(input));
  if (n == "A")
    return SDL_CONTROLLER_BUTTON_A;
  if (n == "B")
    return SDL_CONTROLLER_BUTTON_B;
  if (n == "X")
    return SDL_CONTROLLER_BUTTON_X;
  if (n == "Y")
    return SDL_CONTROLLER_BUTTON_Y;
  if (n == "BACK")
    return SDL_CONTROLLER_BUTTON_BACK;
  if (n == "START")
    return SDL_CONTROLLER_BUTTON_START;
  if (n == "DPAD_UP")
    return SDL_CONTROLLER_BUTTON_DPAD_UP;
  if (n == "DPAD_DOWN")
    return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  if (n == "DPAD_LEFT")
    return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  if (n == "DPAD_RIGHT")
    return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  if (n == "LEFTSHOULDER")
    return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  if (n == "RIGHTSHOULDER")
    return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  return SDL_CONTROLLER_BUTTON_INVALID;
}

const char *controllerButtonNameGameCube(SDL_GameControllerButton b) {
  switch (b) {
  case SDL_CONTROLLER_BUTTON_A:
    return "A";
  case SDL_CONTROLLER_BUTTON_B:
    return "B";
  case SDL_CONTROLLER_BUTTON_X:
    return "X";
  case SDL_CONTROLLER_BUTTON_Y:
    return "Y";
  case SDL_CONTROLLER_BUTTON_BACK:
    return "BACK";
  case SDL_CONTROLLER_BUTTON_START:
    return "START";
  case SDL_CONTROLLER_BUTTON_DPAD_UP:
    return "DPAD_UP";
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
    return "DPAD_DOWN";
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
    return "DPAD_LEFT";
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
    return "DPAD_RIGHT";
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
    return "LEFTSHOULDER";
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
    return "RIGHTSHOULDER";
  default:
    return "UNKNOWN";
  }
}

} // namespace

// ─── Static members
// ───────────────────────────────────────────────────────────

GameCubeEmulation::InputMode GameCubeEmulation::globalInputMode =
    GameCubeEmulation::InputMode::KeyboardAndController;
GameCubeEmulation::KeyBindings GameCubeEmulation::globalKeyBindings = {};
GameCubeEmulation::ControllerBindings
    GameCubeEmulation::globalControllerBindings = {};
bool GameCubeEmulation::globalInputConfigured = false;

// ─── GameCubeEmulation
// ─────────────────────────────────────────────────────────────

GameCubeEmulation::GameCubeEmulation()
    : romLoaded(false), isRunning(false), window(nullptr), renderer(nullptr),
      screenTexture(nullptr), controller(nullptr),
      lastLetterboxRect{0, 0, 640, 480} {
  setDefaultBindings();

  if (globalInputConfigured) {
    inputMode = globalInputMode;
    keyBindings = globalKeyBindings;
    controllerBindings = globalControllerBindings;
  } else {
    saveGlobalInputSettings();
  }
}

GameCubeEmulation::~GameCubeEmulation() {
  unloadRom();
  cleanupSDL();
}

void GameCubeEmulation::setDefaultBindings() {
  inputMode = InputMode::KeyboardAndController;

  // Arrows = D-pad, Z=Cross, X=Circle, A=Square, S=Triangle
  // Enter=Start, RShift=Select, Q=L1, W=R1, E=L2, R=R2
  keyBindings.up = SDLK_UP;
  keyBindings.down = SDLK_DOWN;
  keyBindings.left = SDLK_LEFT;
  keyBindings.right = SDLK_RIGHT;
  keyBindings.cross = SDLK_z;
  keyBindings.circle = SDLK_x;
  keyBindings.square = SDLK_a;
  keyBindings.triangle = SDLK_s;
  keyBindings.start = SDLK_RETURN;
  keyBindings.select = SDLK_RSHIFT;
  keyBindings.l1 = SDLK_q;
  keyBindings.r1 = SDLK_w;
  keyBindings.l2 = SDLK_e;
  keyBindings.r2 = SDLK_r;

  controllerBindings.up = SDL_CONTROLLER_BUTTON_DPAD_UP;
  controllerBindings.down = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  controllerBindings.left = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  controllerBindings.right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  controllerBindings.cross =
      SDL_CONTROLLER_BUTTON_B; // Xbox B = GameCube Cross (common mapping)
  controllerBindings.circle =
      SDL_CONTROLLER_BUTTON_A; // Xbox A = GameCube Circle
  controllerBindings.square =
      SDL_CONTROLLER_BUTTON_X; // Xbox X = GameCube Square
  controllerBindings.triangle =
      SDL_CONTROLLER_BUTTON_Y; // Xbox Y = GameCube Triangle
  controllerBindings.start = SDL_CONTROLLER_BUTTON_START;
  controllerBindings.select = SDL_CONTROLLER_BUTTON_BACK;
  controllerBindings.l1 = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  controllerBindings.r1 = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
}

void GameCubeEmulation::saveGlobalInputSettings() {
  globalInputMode = inputMode;
  globalKeyBindings = keyBindings;
  globalControllerBindings = controllerBindings;
  globalInputConfigured = true;
}

void GameCubeEmulation::initSDL() {
  if (window && renderer && screenTexture)
    return;

  const Uint32 wanted =
      SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER;
  const Uint32 have = SDL_WasInit(wanted);
  const Uint32 missing = wanted & ~have;
  if (missing != 0) {
    if (SDL_InitSubSystem(missing) != 0) {
      std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
      return;
    }
  }

  const int nativeWidth = 320;
  const int nativeHeight = 240;

  window = SDL_CreateWindow("OmniCon GameCube", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 640, 480,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (!window) {
    std::cerr << "SDL window failed: " << SDL_GetError() << "\n";
    return;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    std::cerr << "SDL renderer failed: " << SDL_GetError() << "\n";
    return;
  }

  screenTexture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                        SDL_TEXTUREACCESS_STREAMING, nativeWidth, nativeHeight);
  if (!screenTexture) {
    std::cerr << "SDL texture failed: " << SDL_GetError() << "\n";
    return;
  }

  openControllerByIndex(0);
}

void GameCubeEmulation::cleanupSDL() {
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

void GameCubeEmulation::renderFrameSDL() {
  if (!renderer || !screenTexture)
    return;

  if (!backendPtr) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    return;
  }

  const int w = backendPtr->getFrameWidth();
  const int h = backendPtr->getFrameHeight();

  int texW = 0, texH = 0;
  SDL_QueryTexture(screenTexture, nullptr, nullptr, &texW, &texH);
  if (texW != w || texH != h) {
    SDL_DestroyTexture(screenTexture);
    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!screenTexture)
      return;
  }

  frameBuffer.resize(static_cast<size_t>(w * h) * 4, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const gamecube_color_t px = backendPtr->getPixel(x, y);
      const size_t idx = static_cast<size_t>(y * w + x) * 4;
      frameBuffer[idx + 0] = px.r;
      frameBuffer[idx + 1] = px.g;
      frameBuffer[idx + 2] = px.b;
      frameBuffer[idx + 3] = px.a;
    }
  }
  SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), w * 4);

  int outW = 0, outH = 0;
  SDL_GetRendererOutputSize(renderer, &outW, &outH);
  const double targetAspect = backendPtr->getAspectRatio();
  const double windowAspect =
      outH > 0 ? static_cast<double>(outW) / outH : targetAspect;
  SDL_Rect dst{0, 0, outW, outH};
  if (windowAspect > targetAspect) {
    dst.w = static_cast<int>(outH * targetAspect);
    dst.h = outH;
    dst.x = (outW - dst.w) / 2;
    dst.y = 0;
  } else {
    dst.w = outW;
    dst.h = static_cast<int>(outW / targetAspect);
    dst.x = 0;
    dst.y = (outH - dst.h) / 2;
  }
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  lastLetterboxRect = dst;
  SDL_RenderCopy(renderer, screenTexture, nullptr, &dst);
  SDL_RenderPresent(renderer);
}

void GameCubeEmulation::configureInputMenu() {
  while (true) {
    std::cout << "\n=== GameCube Input Configuration ===\n";
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
    choice = trimGameCube(choice);

    if (choice == "0")
      break;

    if (choice == "1") {
      inputMode = InputMode::KeyboardOnly;
      std::cout << "Keyboard only.\n";
      continue;
    }
    if (choice == "2") {
      inputMode = InputMode::ControllerOnly;
      std::cout << "Controller only.\n";
      continue;
    }
    if (choice == "3") {
      inputMode = InputMode::KeyboardAndController;
      std::cout << "Keyboard + Controller.\n";
      continue;
    }

    if (choice == "4") {
      struct KA {
        const char *name;
        SDL_Keycode *key;
      };
      KA actions[] = {
          {"UP", &keyBindings.up},         {"DOWN", &keyBindings.down},
          {"LEFT", &keyBindings.left},     {"RIGHT", &keyBindings.right},
          {"CROSS", &keyBindings.cross},   {"CIRCLE", &keyBindings.circle},
          {"SQUARE", &keyBindings.square}, {"TRIANGLE", &keyBindings.triangle},
          {"START", &keyBindings.start},   {"SELECT", &keyBindings.select},
          {"L1", &keyBindings.l1},         {"R1", &keyBindings.r1},
          {"L2", &keyBindings.l2},         {"R2", &keyBindings.r2},
      };
      std::cout << "Enter SDL key names (e.g. Up, Z, X, Return). Press Enter "
                   "to keep.\n";
      for (KA &a : actions) {
        std::cout << a.name << " [" << SDL_GetKeyName(*a.key) << "]: ";
        std::string keyName;
        std::getline(std::cin, keyName);
        keyName = trimGameCube(keyName);
        if (keyName.empty())
          continue;
        SDL_Keycode parsed = SDL_GetKeyFromName(keyName.c_str());
        if (parsed == SDLK_UNKNOWN) {
          std::cout << "  Invalid key, keeping existing.\n";
          continue;
        }
        *a.key = parsed;
      }
      std::cout << "Keyboard bindings updated.\n";
      continue;
    }

    if (choice == "5") {
      listControllers();
      std::cout << "Enter controller index to use: ";
      std::string idx;
      std::getline(std::cin, idx);
      idx = trimGameCube(idx);
      if (idx.empty())
        continue;
      try {
        openControllerByIndex(std::stoi(idx));
      } catch (...) {
        std::cout << "Invalid controller index.\n";
      }
      continue;
    }

    if (choice == "6") {
      struct CA {
        const char *name;
        SDL_GameControllerButton *button;
      };
      CA actions[] = {
          {"UP", &controllerBindings.up},
          {"DOWN", &controllerBindings.down},
          {"LEFT", &controllerBindings.left},
          {"RIGHT", &controllerBindings.right},
          {"CROSS", &controllerBindings.cross},
          {"CIRCLE", &controllerBindings.circle},
          {"SQUARE", &controllerBindings.square},
          {"TRIANGLE", &controllerBindings.triangle},
          {"START", &controllerBindings.start},
          {"SELECT", &controllerBindings.select},
          {"L1", &controllerBindings.l1},
          {"R1", &controllerBindings.r1},
      };
      std::cout << "Enter button names "
                   "(A,B,X,Y,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT,"
                   "LEFTSHOULDER,RIGHTSHOULDER).\n";
      std::cout << "Press Enter to keep current binding.\n";
      for (CA &a : actions) {
        std::cout << a.name << " [" << controllerButtonNameGameCube(*a.button)
                  << "]: ";
        std::string btnName;
        std::getline(std::cin, btnName);
        btnName = trimGameCube(btnName);
        if (btnName.empty())
          continue;
        SDL_GameControllerButton parsed =
            parseControllerButtonGameCube(btnName);
        if (parsed == SDL_CONTROLLER_BUTTON_INVALID) {
          std::cout << "  Invalid button, keeping existing.\n";
          continue;
        }
        *a.button = parsed;
      }
      std::cout << "Controller bindings updated.\n";
      continue;
    }

    std::cout << "Unknown option.\n";
  }

  saveGlobalInputSettings();
  std::cout << "GameCube controls saved.\n";
}

void GameCubeEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed,
                                            gamecube_input_t &input,
                                            bool &running) {
  if (key == SDLK_ESCAPE && pressed) {
    running = false;
    return;
  }
  if (key == keyBindings.up)
    input.up = pressed;
  else if (key == keyBindings.down)
    input.down = pressed;
  else if (key == keyBindings.left)
    input.left = pressed;
  else if (key == keyBindings.right)
    input.right = pressed;
  else if (key == keyBindings.cross)
    input.cross = pressed;
  else if (key == keyBindings.circle)
    input.circle = pressed;
  else if (key == keyBindings.square)
    input.square = pressed;
  else if (key == keyBindings.triangle)
    input.triangle = pressed;
  else if (key == keyBindings.start)
    input.start = pressed;
  else if (key == keyBindings.select)
    input.select = pressed;
  else if (key == keyBindings.l1)
    input.l1 = pressed;
  else if (key == keyBindings.r1)
    input.r1 = pressed;
  else if (key == keyBindings.l2)
    input.l2 = pressed;
  else if (key == keyBindings.r2)
    input.r2 = pressed;
}

void GameCubeEmulation::handleControllerButtonEvent(Uint8 button, bool pressed,
                                                    gamecube_input_t &input,
                                                    bool &running) {
  const auto b = static_cast<SDL_GameControllerButton>(button);
  if (b == SDL_CONTROLLER_BUTTON_BACK && pressed) {
    running = false;
    return;
  }
  if (b == controllerBindings.up)
    input.up = pressed;
  else if (b == controllerBindings.down)
    input.down = pressed;
  else if (b == controllerBindings.left)
    input.left = pressed;
  else if (b == controllerBindings.right)
    input.right = pressed;
  else if (b == controllerBindings.cross)
    input.cross = pressed;
  else if (b == controllerBindings.circle)
    input.circle = pressed;
  else if (b == controllerBindings.square)
    input.square = pressed;
  else if (b == controllerBindings.triangle)
    input.triangle = pressed;
  else if (b == controllerBindings.start)
    input.start = pressed;
  else if (b == controllerBindings.select)
    input.select = pressed;
  else if (b == controllerBindings.l1)
    input.l1 = pressed;
  else if (b == controllerBindings.r1)
    input.r1 = pressed;
}

bool GameCubeEmulation::openControllerByIndex(int index) {
  if (controller) {
    SDL_GameControllerClose(controller);
    controller = nullptr;
  }
  const int count = SDL_NumJoysticks();
  if (count <= 0 || index < 0 || index >= count || !SDL_IsGameController(index))
    return false;
  controller = SDL_GameControllerOpen(index);
  return controller != nullptr;
}

void GameCubeEmulation::listControllers() const {
  const int count = SDL_NumJoysticks();
  std::cout << "Controllers: " << count << "\n";
  for (int i = 0; i < count; ++i) {
    std::cout << "  [" << i << "] "
              << (SDL_IsGameController(i) ? "game controller" : "joystick")
              << " - " << SDL_JoystickNameForIndex(i) << "\n";
  }
}

bool GameCubeEmulation::loadRom(const string &path) {
  unloadRom();

  std::filesystem::path fsPath(path);
  std::string ext = fsPath.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  static const char *const kValidExts[] = {".iso", ".gcm",  ".gcz",
                                           ".rvz", ".ciso", nullptr};
  bool validExt = false;
  for (int i = 0; kValidExts[i]; ++i) {
    if (ext == kValidExts[i]) {
      validExt = true;
      break;
    }
  }
  if (!validExt) {
    std::cerr << "[GameCube] Unsupported extension '" << ext << "'.\n";
    std::cerr << "      GameCube ROMs use: .iso, .gcm, .gcz, .rvz, .ciso\n";
    return false;
  }

  auto libretroBackend = std::make_unique<LibretroGameCubeBackend>();
  if (libretroBackend->isAvailable()) {
    backendPtr = std::move(libretroBackend);
  } else {
    backendPtr = std::make_unique<GameCubeUnavailableBackend>();
  }

  if (!backendPtr->loadRom(path)) {
    backendPtr.reset();
    return false;
  }

  romPath = path;
  romLoaded = true;
  isRunning = true;
  return true;
}

void GameCubeEmulation::unloadRom() {
  isRunning = false;
  romLoaded = false;
  backendPtr.reset();
  romPath.clear();
}

void GameCubeEmulation::runInteractive() {
  if (!romLoaded || !backendPtr) {
    std::cerr << "[GameCube] No GameCube ROM loaded.\n";
    return;
  }

  initSDL();
  if (!window || !renderer || !screenTexture) {
    std::cerr << "[GameCube] Failed to initialize SDL.\n";
    return;
  }

  if (!backendPtr->initializeAudio()) {
    std::cerr << "[GameCube] Audio initialization failed.\n";
  }

  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i)) {
      openControllerByIndex(i);
      break;
    }
  if (controller && inputMode == InputMode::KeyboardOnly)
    inputMode = InputMode::KeyboardAndController;

  std::cout << "GameCube: " << romPath << "\n";
  std::cout << "Press Escape or close the window to exit.\n";
  std::cout << "Default keys: Arrows=D-pad, Z=B, X=A, A=X, S=Y\n";
  std::cout
      << "              Enter=Start, RShift=Select, Q=L1, W=R1, E=L2, R=R2\n";

  bool running = true;
  gamecube_input_t input{};

  const double fps = std::max(1.0, backendPtr->getNominalFps());
  const auto frameDuration = std::chrono::duration<double>(1.0 / fps);

  while (running && isRunning.load()) {
    const auto frameStart = std::chrono::steady_clock::now();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        if (inputMode != InputMode::ControllerOnly)
          handleKeyboardEvent(event.key.keysym.sym, true, input, running);
      } else if (event.type == SDL_KEYUP) {
        if (inputMode != InputMode::ControllerOnly)
          handleKeyboardEvent(event.key.keysym.sym, false, input, running);
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        if (inputMode != InputMode::KeyboardOnly)
          handleControllerButtonEvent(event.cbutton.button, true, input,
                                      running);
      } else if (event.type == SDL_CONTROLLERBUTTONUP) {
        if (inputMode != InputMode::KeyboardOnly)
          handleControllerButtonEvent(event.cbutton.button, false, input,
                                      running);
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

    backendPtr->setInput(input);
    if (!backendPtr->nextFrame()) {
      std::cerr << "[GameCube] Frame execution failed; stopping emulation.\n";
      break;
    }
    renderFrameSDL();

    const auto elapsed = std::chrono::steady_clock::now() - frameStart;
    if (elapsed < frameDuration)
      std::this_thread::sleep_for(frameDuration - elapsed);
  }

  cleanupSDL();
  isRunning = false;
}

void GameCubeEmulation::configureGlobalControls() {
  GameCubeEmulation tmp;
  tmp.configureInputMenu();
  tmp.saveGlobalInputSettings();
}

std::vector<unsigned char> GameCubeEmulation::dumpSaveData() const {
  if (!backendPtr || !isLoaded())
    return {};
  return backendPtr->dumpSaveData();
}

bool GameCubeEmulation::restoreSaveData(
    const std::vector<unsigned char> &data) {
  if (!backendPtr || !isLoaded())
    return false;
  return backendPtr->restoreSaveData(data);
}
