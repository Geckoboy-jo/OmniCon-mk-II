#include "3DS.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <chrono>
#include <dlfcn.h>

// ─── Abstract backend ─────────────────────────────────────────────────────────

class N3DSBackend {
public:
    virtual ~N3DSBackend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const n3ds_input_t& input) = 0;
    virtual n3ds_color_t getPixel(int x, int y) const = 0;
    virtual int    getFrameWidth()  const { return 400; }
    virtual int    getFrameHeight() const { return 480; }
    virtual double getAspectRatio() const { return 400.0 / 480.0; }
    virtual double getNominalFps()  const { return 59.83; }
    virtual bool   initializeAudio() { return true; }
    virtual std::vector<unsigned char> dumpSaveData() const { return {}; }
    virtual bool restoreSaveData(const std::vector<unsigned char>&) { return false; }
};

// ─── Unavailable stub ─────────────────────────────────────────────────────────

class N3DSUnavailableBackend final : public N3DSBackend {
public:
    bool loadRom(const std::string&) override {
        std::cerr << "[3DS] Citra libretro core is not available.\n";
        std::cerr << "      Place citra_libretro.so in third_party/libretro/\n";
        std::cerr << "      Or set OMNICON_3DS_CORE to the path of citra_libretro.so\n";
        return false;
    }
    bool nextFrame() override { return false; }
    void setInput(const n3ds_input_t&) override {}
    n3ds_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }
};

// ─── libretro helpers ─────────────────────────────────────────────────────────

namespace libretro3DS {
    static constexpr unsigned ENV_SET_PIXEL_FORMAT    = 10;
    static constexpr unsigned ENV_GET_VARIABLE        = 15;
    static constexpr unsigned ENV_SET_VARIABLES       = 16;
    static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
    static constexpr unsigned ENV_GET_LOG_INTERFACE   = 27;
    static constexpr unsigned ENV_SET_GEOMETRY        = 37;

    enum class log_level { DEBUG = 0, INFO, WARN, ERROR, DUMMY = INT_MAX };
    using log_printf_t = void (*)(log_level level, const char* fmt, ...);
    struct log_callback { log_printf_t log; };
    static constexpr unsigned PIXEL_0RGB1555 = 0;
    static constexpr unsigned PIXEL_XRGB8888 = 1;
    static constexpr unsigned PIXEL_RGB565   = 2;
    static constexpr unsigned DEVICE_JOYPAD  = 1;
    static constexpr unsigned DEVICE_POINTER = 6;
    static constexpr unsigned POINTER_X       = 0;
    static constexpr unsigned POINTER_Y       = 1;
    static constexpr unsigned POINTER_PRESSED = 2;
    static constexpr unsigned POINTER_COUNT   = 3;
    static constexpr unsigned JOYPAD_B       = 0;
    static constexpr unsigned JOYPAD_Y       = 1;
    static constexpr unsigned JOYPAD_SELECT  = 2;
    static constexpr unsigned JOYPAD_START   = 3;
    static constexpr unsigned JOYPAD_UP      = 4;
    static constexpr unsigned JOYPAD_DOWN    = 5;
    static constexpr unsigned JOYPAD_LEFT    = 6;
    static constexpr unsigned JOYPAD_RIGHT   = 7;
    static constexpr unsigned JOYPAD_A       = 8;
    static constexpr unsigned JOYPAD_X       = 9;
    static constexpr unsigned JOYPAD_L       = 10;
    static constexpr unsigned JOYPAD_R       = 11;
    static constexpr unsigned JOYPAD_L2      = 14;   // ZL
    static constexpr unsigned JOYPAD_R2      = 15;   // ZR

    struct game_info    { const char* path; const void* data; size_t size; const char* meta; };
    struct game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
    struct variable     { const char* key; const char* value; };
    struct av_info {
        game_geometry geometry;
        struct { double fps; double sample_rate; } timing;
    };
    struct system_info {
        const char* library_name;
        const char* library_version;
        const char* valid_extensions;
        bool        need_fullpath;
        bool        block_extract;
    };
    // Hardware-rendering (OpenGL) support
    static constexpr unsigned ENV_SET_HW_RENDER = 14;
    enum class hw_context_type : unsigned {
        NONE=0, OPENGL=1, OPENGLES2=2, OPENGL_CORE=3, OPENGLES3=4
    };
    using hw_context_reset_t           = void (*)();
    using hw_get_current_framebuffer_t = uintptr_t (*)();
    using hw_get_proc_address_t        = void* (*)(const char*);
    struct hw_render_callback {
        hw_context_type              context_type;
        hw_context_reset_t           context_reset;
        hw_get_current_framebuffer_t get_current_framebuffer;
        hw_get_proc_address_t        get_proc_address;
        bool     depth, stencil, bottom_left_origin;
        unsigned version_major, version_minor;
        bool     cache_context;
        hw_context_reset_t           context_destroy;
        bool     debug_context;
    };
    using environment_t        = bool (*)(unsigned, void*);
    using video_refresh_t      = void (*)(const void*, unsigned, unsigned, size_t);
    using audio_sample_t       = void (*)(int16_t, int16_t);
    using audio_sample_batch_t = size_t (*)(const int16_t*, size_t);
    using input_poll_t         = void (*)(void);
    using input_state_t        = int16_t (*)(unsigned, unsigned, unsigned, unsigned);
}

// ─── Libretro backend ─────────────────────────────────────────────────────────

class Libretro3DSBackend final : public N3DSBackend {
public:
    static constexpr const char* kSystemCorePath    = "/usr/lib/x86_64-linux-gnu/libretro/citra_libretro.so";
    static constexpr const char* kWorkspaceCorePath = "third_party/libretro/citra_libretro.so";

    Libretro3DSBackend();
    ~Libretro3DSBackend() override;

    bool isAvailable() const { return coreHandle != nullptr; }
    bool loadRom(const std::string& path) override;
    bool nextFrame() override;
    void setInput(const n3ds_input_t& input) override;
    n3ds_color_t getPixel(int x, int y) const override;
    int    getFrameWidth()  const override { return frameWidth; }
    int    getFrameHeight() const override { return frameHeight; }
    double getAspectRatio() const override { return aspectRatio; }
    double getNominalFps()  const override { return nominalFps; }

    bool onEnvironment(unsigned cmd, void* data);
    void onVideoRefresh(const void* data, unsigned w, unsigned h, size_t pitch);
    std::vector<unsigned char> dumpSaveData() const override;
    bool restoreSaveData(const std::vector<unsigned char>& data) override;

private:
    static std::string resolveCorePath();
    bool loadSymbols();
    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioFrames(const int16_t* data, size_t frames);
    bool initializeAudio() override;
    void ensureLibretroDirectories();

    std::string corePath;
    void* coreHandle = nullptr;
    void   (*fn_init)()                                            = nullptr;
    void   (*fn_deinit)()                                          = nullptr;
    void   (*fn_set_environment)(libretro3DS::environment_t)       = nullptr;
    void   (*fn_set_video_refresh)(libretro3DS::video_refresh_t)   = nullptr;
    void   (*fn_set_audio_sample)(libretro3DS::audio_sample_t)     = nullptr;
    void   (*fn_set_audio_sample_batch)(libretro3DS::audio_sample_batch_t) = nullptr;
    void   (*fn_set_input_poll)(libretro3DS::input_poll_t)         = nullptr;
    void   (*fn_set_input_state)(libretro3DS::input_state_t)       = nullptr;
    void   (*fn_get_system_av_info)(libretro3DS::av_info*)         = nullptr;
    void   (*fn_get_system_info)(libretro3DS::system_info*)        = nullptr;
    bool   (*fn_load_game)(const libretro3DS::game_info*)          = nullptr;
    void   (*fn_unload_game)()                                     = nullptr;
    void   (*fn_run)()                                             = nullptr;
    size_t (*fn_serialize_size)()                                  = nullptr;
    bool   (*fn_serialize)(void*, size_t)                          = nullptr;
    bool   (*fn_unserialize)(const void*, size_t)                  = nullptr;

    std::vector<unsigned char> framePixels;
    std::vector<unsigned char> romData;
    std::string loadedRomPath;
    std::string systemDirectory;
    std::string saveDirectory;
    std::string contentDirectory;
    n3ds_input_t currentInput{};
    bool gameLoaded = false;
    unsigned pixelFormat = libretro3DS::PIXEL_0RGB1555;
    int    frameWidth   = 400;
    int    frameHeight  = 480;
    double aspectRatio  = 400.0 / 480.0;
    double nominalFps   = 59.83;
    SDL_AudioDeviceID audioDevice = 0;
    int audioSampleRate  = 48000;
    int coreSampleRate   = 48000;
    size_t maxQueuedAudioBytes = 0;

    // OpenGL HW rendering (required by Citra)
    SDL_Window*    hwWindow       = nullptr;
    SDL_GLContext  hwGLCtx        = nullptr;
    unsigned       hwFbo          = 0;
    unsigned       hwColorTex     = 0;
    unsigned       hwDepthRb      = 0;
    libretro3DS::hw_context_reset_t hwContextReset   = nullptr;
    libretro3DS::hw_context_reset_t hwContextDestroy = nullptr;
    bool           hwRenderActive = false;
    // Cached GL function pointers (loaded once after context creation)
    void     (*glGenFramebuffers_)(int, unsigned*)                        = nullptr;
    void     (*glBindFramebuffer_)(unsigned, unsigned)                    = nullptr;
    void     (*glGenTextures_)(int, unsigned*)                            = nullptr;
    void     (*glBindTexture_)(unsigned, unsigned)                        = nullptr;
    void     (*glTexImage2D_)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*) = nullptr;
    void     (*glFramebufferTexture2D_)(unsigned,unsigned,unsigned,unsigned,int) = nullptr;
    void     (*glGenRenderbuffers_)(int, unsigned*)                       = nullptr;
    void     (*glBindRenderbuffer_)(unsigned, unsigned)                   = nullptr;
    void     (*glRenderbufferStorage_)(unsigned,unsigned,int,int)         = nullptr;
    void     (*glFramebufferRenderbuffer_)(unsigned,unsigned,unsigned,unsigned) = nullptr;
    void     (*glDeleteFramebuffers_)(int, const unsigned*)               = nullptr;
    void     (*glDeleteTextures_)(int, const unsigned*)                   = nullptr;
    void     (*glDeleteRenderbuffers_)(int, const unsigned*)              = nullptr;
    void     (*glReadPixels_)(int,int,int,int,unsigned,unsigned,void*)    = nullptr;
    void     (*glViewport_)(int,int,int,int)                              = nullptr;

    bool initGLContext();
    void cleanupGLContext();
    void createFbo();
    void readGLPixels();

    static Libretro3DSBackend* s_instance;
    static bool    s_envCb(unsigned cmd, void* data);
    static void    s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    s_audioCb(int16_t left, int16_t right);
    static size_t  s_audioBatchCb(const int16_t* data, size_t frames);
    static void    s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
    static void    s_logCb(libretro3DS::log_level level, const char* fmt, ...);
    static uintptr_t s_getCurrentFramebuffer();
    static void*     s_getProcAddress(const char* sym);
};

Libretro3DSBackend* Libretro3DSBackend::s_instance = nullptr;

std::string Libretro3DSBackend::resolveCorePath()
{
    const char* envPath = std::getenv("OMNICON_3DS_CORE");
    if (envPath && *envPath && std::filesystem::exists(envPath)) return envPath;
    if (std::filesystem::exists(kWorkspaceCorePath)) return kWorkspaceCorePath;
    if (std::filesystem::exists(kSystemCorePath))    return kSystemCorePath;
    return "";
}

Libretro3DSBackend::Libretro3DSBackend()
    : corePath(resolveCorePath())
{
    if (corePath.empty()) return;

    coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[3DS] Failed to load core " << corePath << ": " << dlerror() << "\n";
        return;
    }
    if (!loadSymbols()) { dlclose(coreHandle); coreHandle = nullptr; return; }

    s_instance = this;
    ensureLibretroDirectories();
    // Force Mesa's llvmpipe software renderer so Citra's #version 430 shaders
    // compile on hosts whose GPU driver only supports GLSL 4.20.  llvmpipe
    // supports GLSL 4.60 natively, so this is sufficient.
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    initGLContext();   // Must happen before fn_init so SET_HW_RENDER can succeed
    fn_set_environment(s_envCb);
    fn_set_video_refresh(s_videoCb);
    fn_set_audio_sample(s_audioCb);
    fn_set_audio_sample_batch(s_audioBatchCb);
    fn_set_input_poll(s_inputPollCb);
    fn_set_input_state(s_inputStateCb);
    fn_init();
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
}

Libretro3DSBackend::~Libretro3DSBackend()
{
    closeAudioDevice();
    if (coreHandle) {
        if (gameLoaded) fn_unload_game();
        fn_deinit();
        dlclose(coreHandle);
        coreHandle = nullptr;
    }
    cleanupGLContext();
    if (s_instance == this) s_instance = nullptr;
}

bool Libretro3DSBackend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) std::cerr << "[3DS] Missing symbol: " << name << "\n";
        return sym;
    };
    fn_init                   = reinterpret_cast<void(*)()>(load("retro_init"));
    fn_deinit                 = reinterpret_cast<void(*)()>(load("retro_deinit"));
    fn_set_environment        = reinterpret_cast<void(*)(libretro3DS::environment_t)>(load("retro_set_environment"));
    fn_set_video_refresh      = reinterpret_cast<void(*)(libretro3DS::video_refresh_t)>(load("retro_set_video_refresh"));
    fn_set_audio_sample       = reinterpret_cast<void(*)(libretro3DS::audio_sample_t)>(load("retro_set_audio_sample"));
    fn_set_audio_sample_batch = reinterpret_cast<void(*)(libretro3DS::audio_sample_batch_t)>(load("retro_set_audio_sample_batch"));
    fn_set_input_poll         = reinterpret_cast<void(*)(libretro3DS::input_poll_t)>(load("retro_set_input_poll"));
    fn_set_input_state        = reinterpret_cast<void(*)(libretro3DS::input_state_t)>(load("retro_set_input_state"));
    fn_get_system_av_info     = reinterpret_cast<void(*)(libretro3DS::av_info*)>(load("retro_get_system_av_info"));
    // Optional: used to check need_fullpath before passing ROM data
    fn_get_system_info        = reinterpret_cast<void(*)(libretro3DS::system_info*)>(dlsym(coreHandle, "retro_get_system_info"));
    fn_load_game              = reinterpret_cast<bool(*)(const libretro3DS::game_info*)>(load("retro_load_game"));
    fn_unload_game            = reinterpret_cast<void(*)()>(load("retro_unload_game"));
    fn_run                    = reinterpret_cast<void(*)()>(load("retro_run"));
    // Optional — not all cores support savestates
    fn_serialize_size = reinterpret_cast<size_t(*)()>(dlsym(coreHandle, "retro_serialize_size"));
    fn_serialize      = reinterpret_cast<bool(*)(void*,size_t)>(dlsym(coreHandle, "retro_serialize"));
    fn_unserialize    = reinterpret_cast<bool(*)(const void*,size_t)>(dlsym(coreHandle, "retro_unserialize"));
    return fn_init && fn_deinit && fn_set_environment && fn_set_video_refresh
        && fn_set_audio_sample && fn_set_audio_sample_batch
        && fn_set_input_poll && fn_set_input_state
        && fn_get_system_av_info && fn_load_game && fn_unload_game && fn_run;
}

void Libretro3DSBackend::ensureLibretroDirectories()
{
    const std::filesystem::path root    = std::filesystem::current_path() / "third_party" / "libretro";
    const std::filesystem::path sysDir  = root / "system";
    const std::filesystem::path saveDir = root / "save";
    const std::filesystem::path contDir = root / "content";
    std::error_code ec;
    std::filesystem::create_directories(sysDir,  ec);
    std::filesystem::create_directories(saveDir, ec);
    std::filesystem::create_directories(contDir, ec);
    systemDirectory  = sysDir.string();
    saveDirectory    = saveDir.string();
    contentDirectory = contDir.string();
}

// ─── OpenGL context helpers ───────────────────────────────────────────────────

bool Libretro3DSBackend::initGLContext()
{
    // Init all SDL subsystems we need so initSDL() in N3DSEmulation doesn't skip audio/gamecontroller
    const Uint32 needed  = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER;
    const Uint32 missing = needed & ~SDL_WasInit(0);
    if (missing) SDL_InitSubSystem(missing);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    hwWindow = SDL_CreateWindow("OmniCon3DS_GL", 0, 0, frameWidth, frameHeight,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!hwWindow) {
        std::cerr << "[3DS] Failed to create GL window: " << SDL_GetError() << "\n";
        return false;
    }
    hwGLCtx = SDL_GL_CreateContext(hwWindow);
    if (!hwGLCtx) {
        std::cerr << "[3DS] Failed to create GL context: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(hwWindow); hwWindow = nullptr;
        return false;
    }
    SDL_GL_MakeCurrent(hwWindow, hwGLCtx);

    // Load all GL function pointers we need
    auto load = [](const char* sym) { return SDL_GL_GetProcAddress(sym); };
    glGenFramebuffers_        = (void(*)(int,unsigned*))                             load("glGenFramebuffers");
    glBindFramebuffer_        = (void(*)(unsigned,unsigned))                         load("glBindFramebuffer");
    glGenTextures_            = (void(*)(int,unsigned*))                             load("glGenTextures");
    glBindTexture_            = (void(*)(unsigned,unsigned))                         load("glBindTexture");
    glTexImage2D_             = (void(*)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*)) load("glTexImage2D");
    glFramebufferTexture2D_   = (void(*)(unsigned,unsigned,unsigned,unsigned,int))   load("glFramebufferTexture2D");
    glGenRenderbuffers_       = (void(*)(int,unsigned*))                             load("glGenRenderbuffers");
    glBindRenderbuffer_       = (void(*)(unsigned,unsigned))                         load("glBindRenderbuffer");
    glRenderbufferStorage_    = (void(*)(unsigned,unsigned,int,int))                 load("glRenderbufferStorage");
    glFramebufferRenderbuffer_= (void(*)(unsigned,unsigned,unsigned,unsigned))       load("glFramebufferRenderbuffer");
    glDeleteFramebuffers_     = (void(*)(int,const unsigned*))                       load("glDeleteFramebuffers");
    glDeleteTextures_         = (void(*)(int,const unsigned*))                       load("glDeleteTextures");
    glDeleteRenderbuffers_    = (void(*)(int,const unsigned*))                       load("glDeleteRenderbuffers");
    glReadPixels_             = (void(*)(int,int,int,int,unsigned,unsigned,void*))   load("glReadPixels");
    glViewport_               = (void(*)(int,int,int,int))                           load("glViewport");

    const bool ok = glGenFramebuffers_ && glBindFramebuffer_ && glGenTextures_ &&
                    glBindTexture_ && glTexImage2D_ && glFramebufferTexture2D_ &&
                    glGenRenderbuffers_ && glBindRenderbuffer_ && glRenderbufferStorage_ &&
                    glFramebufferRenderbuffer_ && glReadPixels_;
    if (!ok) std::cerr << "[3DS] Some GL functions unavailable; HW rendering may fail.\n";
    return ok;
}

void Libretro3DSBackend::cleanupGLContext()
{
    if (hwGLCtx && hwWindow) {
        SDL_GL_MakeCurrent(hwWindow, hwGLCtx);
        if (hwContextDestroy) hwContextDestroy();
        if (hwFbo      && glDeleteFramebuffers_)  glDeleteFramebuffers_(1,  &hwFbo);
        if (hwColorTex && glDeleteTextures_)       glDeleteTextures_(1,      &hwColorTex);
        if (hwDepthRb  && glDeleteRenderbuffers_)  glDeleteRenderbuffers_(1, &hwDepthRb);
        hwFbo = hwColorTex = hwDepthRb = 0;
    }
    if (hwGLCtx)  { SDL_GL_DeleteContext(hwGLCtx); hwGLCtx  = nullptr; }
    if (hwWindow) { SDL_DestroyWindow(hwWindow);   hwWindow = nullptr; }
}

void Libretro3DSBackend::createFbo()
{
    if (!hwGLCtx || !glGenFramebuffers_ || !glBindFramebuffer_ ||
        !glGenTextures_ || !glBindTexture_ || !glTexImage2D_ ||
        !glFramebufferTexture2D_ || !glGenRenderbuffers_ ||
        !glBindRenderbuffer_ || !glRenderbufferStorage_ || !glFramebufferRenderbuffer_)
        return;

    // Delete old FBO resources if resizing
    if (hwFbo      && glDeleteFramebuffers_)  glDeleteFramebuffers_(1,  &hwFbo);
    if (hwColorTex && glDeleteTextures_)       glDeleteTextures_(1,      &hwColorTex);
    if (hwDepthRb  && glDeleteRenderbuffers_)  glDeleteRenderbuffers_(1, &hwDepthRb);
    hwFbo = hwColorTex = hwDepthRb = 0;

    constexpr unsigned GL_FRAMEBUFFER           = 0x8D40;
    constexpr unsigned GL_COLOR_ATTACHMENT0     = 0x8CE0;
    constexpr unsigned GL_RENDERBUFFER          = 0x8D41;
    constexpr unsigned GL_DEPTH_STENCIL_ATTACH  = 0x821A;
    constexpr unsigned GL_DEPTH24_STENCIL8      = 0x88F0;
    constexpr unsigned GL_TEXTURE_2D            = 0x0DE1;
    constexpr unsigned GL_RGBA8                 = 0x8058;
    constexpr unsigned GL_RGBA                  = 0x1908;
    constexpr unsigned GL_UNSIGNED_BYTE         = 0x1401;

    glGenFramebuffers_(1, &hwFbo);
    glBindFramebuffer_(GL_FRAMEBUFFER, hwFbo);

    glGenTextures_(1, &hwColorTex);
    glBindTexture_(GL_TEXTURE_2D, hwColorTex);
    glTexImage2D_(GL_TEXTURE_2D, 0, static_cast<int>(GL_RGBA8),
                  frameWidth, frameHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hwColorTex, 0);

    glGenRenderbuffers_(1, &hwDepthRb);
    glBindRenderbuffer_(GL_RENDERBUFFER, hwDepthRb);
    glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, frameWidth, frameHeight);
    glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACH, GL_RENDERBUFFER, hwDepthRb);

    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
}

void Libretro3DSBackend::readGLPixels()
{
    if (!hwGLCtx || !glReadPixels_ || !glBindFramebuffer_) return;
    constexpr unsigned GL_READ_FRAMEBUFFER = 0x8CA8;
    constexpr unsigned GL_RGBA         = 0x1908;
    constexpr unsigned GL_UNSIGNED_BYTE= 0x1401;

    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4);
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, hwFbo ? hwFbo : 0u);
    glReadPixels_(0, 0, frameWidth, frameHeight, GL_RGBA, GL_UNSIGNED_BYTE, framePixels.data());
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, 0);

    // GL origin is bottom-left; flip rows to get top-left origin
    const size_t rowBytes = static_cast<size_t>(frameWidth) * 4;
    std::vector<unsigned char> tmp(rowBytes);
    for (int y = 0; y < frameHeight / 2; ++y) {
        unsigned char* top = framePixels.data() + static_cast<size_t>(y)                   * rowBytes;
        unsigned char* bot = framePixels.data() + static_cast<size_t>(frameHeight - 1 - y) * rowBytes;
        std::memcpy(tmp.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, tmp.data(), rowBytes);
    }
}

bool Libretro3DSBackend::ensureAudioDevice(int sampleRate)
{
    if (audioDevice != 0 && audioSampleRate == sampleRate) return true;
    closeAudioDevice();
    SDL_AudioSpec desired{}, obtained{};
    desired.freq     = sampleRate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 2048;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) { std::cerr << "[3DS] Audio open failed: " << SDL_GetError() << "\n"; return false; }
    audioSampleRate      = obtained.freq > 0 ? obtained.freq : sampleRate;
    maxQueuedAudioBytes  = static_cast<size_t>(audioSampleRate * obtained.channels * sizeof(int16_t) * 20 / 100);
    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void Libretro3DSBackend::closeAudioDevice()
{
    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
}

void Libretro3DSBackend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0 || audioDevice == 0) return;
    const size_t bytes = frames * 2 * sizeof(int16_t);
    if (SDL_GetQueuedAudioSize(audioDevice) > maxQueuedAudioBytes) return;
    SDL_QueueAudio(audioDevice, data, static_cast<Uint32>(bytes));
}

bool Libretro3DSBackend::initializeAudio()
{
    return ensureAudioDevice(coreSampleRate);
}

bool Libretro3DSBackend::loadRom(const std::string& path)
{
    if (!coreHandle) return false;
    ensureLibretroDirectories();

    // Always use absolute path so the core can locate the file on disk
    const std::string absPath = std::filesystem::absolute(path).string();

    // Check if the core requires a full-path load (Citra does: need_fullpath=true).
    // When need_fullpath is true the core reads the file itself; passing a data
    // buffer causes retro_load_game to reject the ROM.
    bool needFullPath = true; // safe default for Citra
    if (fn_get_system_info) {
        libretro3DS::system_info sinfo{};
        fn_get_system_info(&sinfo);
        needFullPath = sinfo.need_fullpath;
    }

    if (!needFullPath) {
        std::ifstream file(absPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) { std::cerr << "[3DS] Cannot open ROM: " << absPath << "\n"; return false; }
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        romData.assign(size, 0);
        if (!file.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
            std::cerr << "[3DS] Failed to read ROM: " << absPath << "\n";
            romData.clear(); return false;
        }
    } else {
        romData.clear(); // core reads the file itself
    }

    if (gameLoaded) { fn_unload_game(); gameLoaded = false; }

    loadedRomPath = absPath;
    const std::filesystem::path parentDir = std::filesystem::path(absPath).parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(parentDir, ec)) contentDirectory = parentDir.string();
    }

    libretro3DS::game_info info{ loadedRomPath.c_str(),
                                 needFullPath ? nullptr : romData.data(),
                                 needFullPath ? 0       : romData.size(),
                                 nullptr };
    if (!fn_load_game(&info)) { std::cerr << "[3DS] Core rejected ROM: " << absPath << "\n"; return false; }
    gameLoaded = true;

    libretro3DS::av_info av{};
    fn_get_system_av_info(&av);
    if (av.geometry.base_width  > 0) frameWidth  = static_cast<int>(av.geometry.base_width);
    if (av.geometry.base_height > 0) frameHeight = static_cast<int>(av.geometry.base_height);
    if (av.geometry.aspect_ratio > 0.0f)
        aspectRatio = av.geometry.aspect_ratio;
    else if (frameWidth > 0 && frameHeight > 0)
        aspectRatio = static_cast<double>(frameWidth) / frameHeight;
    if (av.timing.fps > 1.0)             nominalFps     = av.timing.fps;
    if (av.timing.sample_rate > 1000.0)  coreSampleRate = static_cast<int>(av.timing.sample_rate);
    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);

    // Create the FBO at the reported resolution, then notify the core that the
    // GL context is ready (context_reset).  This must happen after av_info so
    // the FBO dimensions match what the core expects.
    if (hwRenderActive && hwGLCtx) {
        SDL_GL_MakeCurrent(hwWindow, hwGLCtx);
        if (glViewport_) glViewport_(0, 0, frameWidth, frameHeight);
        createFbo();
        if (hwContextReset) hwContextReset();
    }
    return true;
}

bool Libretro3DSBackend::nextFrame()
{
    if (!coreHandle || !gameLoaded) return false;
    if (hwRenderActive && hwWindow && hwGLCtx) {
        // Make our GL context current before the core renders.
        // The core calls get_current_framebuffer() itself inside fn_run()
        // to obtain and bind hwFbo; we must not pre-bind it.
        SDL_GL_MakeCurrent(hwWindow, hwGLCtx);
        if (glViewport_) glViewport_(0, 0, frameWidth, frameHeight);
    }
    fn_run();
    if (hwRenderActive && hwGLCtx) {
        readGLPixels();
    }
    return true;
}

void Libretro3DSBackend::setInput(const n3ds_input_t& input) { currentInput = input; }

n3ds_color_t Libretro3DSBackend::getPixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) return {0, 0, 0, 255};
    const size_t idx = static_cast<size_t>(y * frameWidth + x) * 4;
    if (idx + 3 >= framePixels.size()) return {0, 0, 0, 255};
    return { framePixels[idx], framePixels[idx+1], framePixels[idx+2], framePixels[idx+3] };
}

bool Libretro3DSBackend::onEnvironment(unsigned cmd, void* data)
{
    if (cmd == 3)  { *static_cast<bool*>(data) = true; return true; }
    if (cmd == 9)  { *static_cast<const char**>(data) = systemDirectory.c_str(); return true; }
    if (cmd == 30) { *static_cast<const char**>(data) = contentDirectory.c_str(); return true; }
    if (cmd == 31) { *static_cast<const char**>(data) = saveDirectory.c_str(); return true; }
    if (cmd == 18) { return true; }
    if (cmd == libretro3DS::ENV_GET_LOG_INTERFACE && data) {
        static_cast<libretro3DS::log_callback*>(data)->log = s_logCb;
        return true;
    }
    if (cmd == libretro3DS::ENV_SET_HW_RENDER && data) {
        if (!hwGLCtx) {
            std::cerr << "[3DS] Core requested HW render but GL context is unavailable.\n";
            return false;
        }
        auto* hw = static_cast<libretro3DS::hw_render_callback*>(data);
        hw->get_current_framebuffer = s_getCurrentFramebuffer;
        hw->get_proc_address        = s_getProcAddress;
        hwContextReset   = hw->context_reset;
        hwContextDestroy = hw->context_destroy;
        hwRenderActive   = true;
        return true;
    }
    if (cmd == libretro3DS::ENV_SET_VARIABLES)       { return true; }
    if (cmd == libretro3DS::ENV_GET_VARIABLE_UPDATE) { if (data) *static_cast<bool*>(data) = false; return true; }
    if (cmd == libretro3DS::ENV_GET_VARIABLE) {
        auto* v = static_cast<libretro3DS::variable*>(data);
        if (!v || !v->key) return false;
        // Provide sensible defaults for all known Citra variables so Citra
        // initialises its subsystems properly rather than using unchecked defaults.
        const char* k = v->key;
        if      (!std::strcmp(k,"citra_use_cpu_jit"))          v->value = "enabled";
        else if (!std::strcmp(k,"citra_cpu_scale"))             v->value = "100%";
        else if (!std::strcmp(k,"citra_use_hw_shaders"))        v->value = "disabled";
        else if (!std::strcmp(k,"citra_use_shader_jit"))        v->value = "enabled";
        else if (!std::strcmp(k,"citra_use_acc_mul"))           v->value = "enabled";
        else if (!std::strcmp(k,"citra_use_virtual_sd"))        v->value = "enabled";
        else if (!std::strcmp(k,"citra_is_new_3ds"))            v->value = "enabled";
        else if (!std::strcmp(k,"citra_swap_screen"))           v->value = "false";
        else if (!std::strcmp(k,"citra_swap_screen_mode"))      v->value = "toggle";
        else if (!std::strcmp(k,"citra_use_gdbstub"))           v->value = "disabled";
        else if (!std::strcmp(k,"citra_texture_filter"))        v->value = "none";
        else if (!std::strcmp(k,"citra_texture_sampling"))      v->value = "game_specified";
        else if (!std::strcmp(k,"citra_dump_textures"))         v->value = "false";
        else if (!std::strcmp(k,"citra_custom_textures"))       v->value = "false";
        else if (!std::strcmp(k,"citra_use_hw_shader_cache"))   v->value = "false";
        else if (!std::strcmp(k,"citra_mouse_touchscreen"))     v->value = "enabled";
        else if (!std::strcmp(k,"citra_touch_touchscreen"))     v->value = "enabled";
        else if (!std::strcmp(k,"citra_render_touchscreen"))    v->value = "enabled";
        else if (!std::strcmp(k,"citra_resolution_factor"))     v->value = "1x";
        else if (!std::strcmp(k,"citra_layout_option"))         v->value = "Default Top-Bottom Screen";
        else if (!std::strcmp(k,"citra_deadzone"))              v->value = "15";
        else if (!std::strcmp(k,"citra_analog_function"))       v->value = "C-Stick";
        else if (!std::strcmp(k,"citra_region_value"))          v->value = "USA";
        else if (!std::strcmp(k,"citra_language"))              v->value = "English";
        else if (!std::strcmp(k,"citra_use_libretro_save_path"))v->value = "enabled";
        else { v->value = nullptr; return false; }
        return true;
    }
    if (cmd == libretro3DS::ENV_SET_PIXEL_FORMAT && data) {
        pixelFormat = *reinterpret_cast<unsigned*>(data);
        return pixelFormat == libretro3DS::PIXEL_0RGB1555
            || pixelFormat == libretro3DS::PIXEL_XRGB8888
            || pixelFormat == libretro3DS::PIXEL_RGB565;
    }
    if (cmd == libretro3DS::ENV_SET_GEOMETRY && data) {
        const auto* g = reinterpret_cast<const libretro3DS::game_geometry*>(data);
        if (g->base_width  > 0) frameWidth  = static_cast<int>(g->base_width);
        if (g->base_height > 0) frameHeight = static_cast<int>(g->base_height);
        if (g->aspect_ratio > 0.0f) aspectRatio = g->aspect_ratio;
        framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
        return true;
    }
    return false;
}

void Libretro3DSBackend::onVideoRefresh(const void* data, unsigned w, unsigned h, size_t pitch)
{
    // RETRO_HW_FRAME_BUFFER_VALID = (void*)-1 signals that the core rendered
    // into our FBO via HW rendering.  We must NOT dereference this pointer;
    // readGLPixels() in nextFrame() reads the result from hwFbo instead.
    if (data == reinterpret_cast<const void*>(static_cast<uintptr_t>(-1))) {
        if (w > 0) frameWidth  = static_cast<int>(w);
        if (h > 0) frameHeight = static_cast<int>(h);
        return;
    }
    if (!data || w == 0 || h == 0) return;
    frameWidth  = static_cast<int>(w);
    frameHeight = static_cast<int>(h);
    framePixels.resize(static_cast<size_t>(w * h) * 4, 0);

    for (unsigned y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint8_t*>(data) + y * pitch;
        for (unsigned x = 0; x < w; ++x) {
            const size_t out = static_cast<size_t>(y * w + x) * 4;
            if (pixelFormat == libretro3DS::PIXEL_XRGB8888) {
                const uint32_t p = reinterpret_cast<const uint32_t*>(row)[x];
                framePixels[out+0] = static_cast<uint8_t>((p >> 16) & 0xFF);
                framePixels[out+1] = static_cast<uint8_t>((p >>  8) & 0xFF);
                framePixels[out+2] = static_cast<uint8_t>( p        & 0xFF);
                framePixels[out+3] = 255;
            } else if (pixelFormat == libretro3DS::PIXEL_RGB565) {
                const uint16_t p = reinterpret_cast<const uint16_t*>(row)[x];
                framePixels[out+0] = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
                framePixels[out+1] = static_cast<uint8_t>(((p >>  5) & 0x3F) * 255 / 63);
                framePixels[out+2] = static_cast<uint8_t>(( p        & 0x1F) * 255 / 31);
                framePixels[out+3] = 255;
            } else { // 0RGB1555
                const uint16_t p = reinterpret_cast<const uint16_t*>(row)[x];
                framePixels[out+0] = static_cast<uint8_t>(((p >> 10) & 0x1F) * 255 / 31);
                framePixels[out+1] = static_cast<uint8_t>(((p >>  5) & 0x1F) * 255 / 31);
                framePixels[out+2] = static_cast<uint8_t>(( p        & 0x1F) * 255 / 31);
                framePixels[out+3] = 255;
            }
        }
    }
}

bool   Libretro3DSBackend::s_envCb(unsigned cmd, void* data) { return s_instance ? s_instance->onEnvironment(cmd, data) : false; }
void   Libretro3DSBackend::s_videoCb(const void* d, unsigned w, unsigned h, size_t p) { if (s_instance) s_instance->onVideoRefresh(d,w,h,p); }
void   Libretro3DSBackend::s_audioCb(int16_t l, int16_t r) { if (s_instance) { const int16_t s[2]={l,r}; s_instance->queueAudioFrames(s,1); } }
size_t Libretro3DSBackend::s_audioBatchCb(const int16_t* d, size_t f) { if (s_instance && d && f) s_instance->queueAudioFrames(d,f); return f; }
void   Libretro3DSBackend::s_inputPollCb() {}
void   Libretro3DSBackend::s_logCb(libretro3DS::log_level level, const char* fmt, ...)
{
    static const char* const prefixes[] = { "[3DS:DBG] ", "[3DS:INF] ", "[3DS:WRN] ", "[3DS:ERR] " };
    const int lvl = static_cast<int>(level);
    va_list ap;
    va_start(ap, fmt);
    // Only forward WARN and ERROR to stderr to avoid flooding; change to lvl>=0 for full verbose
    if (lvl >= static_cast<int>(libretro3DS::log_level::WARN)) {
        std::string msg(256, '\0');
        const int needed = vsnprintf(msg.data(), msg.size(), fmt, ap);
        if (needed > 0) {
            if (static_cast<size_t>(needed) >= msg.size()) {
                msg.resize(static_cast<size_t>(needed) + 1);
                va_end(ap); va_start(ap, fmt);
                vsnprintf(msg.data(), msg.size(), fmt, ap);
            }
            const char* prefix = (lvl >= 0 && lvl <= 3) ? prefixes[lvl] : "[3DS] ";
            std::cerr << prefix << msg.data();
            if (msg.data()[0] && msg[static_cast<size_t>(needed) - 1] != '\n') std::cerr << '\n';
        }
    }
    va_end(ap);
}
uintptr_t Libretro3DSBackend::s_getCurrentFramebuffer()
{
    return s_instance ? static_cast<uintptr_t>(s_instance->hwFbo) : 0;
}
void* Libretro3DSBackend::s_getProcAddress(const char* sym)
{
    return SDL_GL_GetProcAddress(sym);
}
int16_t Libretro3DSBackend::s_inputStateCb(unsigned port, unsigned device, unsigned /*index*/, unsigned id)
{
    if (!s_instance || port != 0) return 0;
    const n3ds_input_t& in = s_instance->currentInput;

    if (device == libretro3DS::DEVICE_POINTER) {
        switch (id) {
            case libretro3DS::POINTER_X:       return in.touchPressed ? in.touchX : 0;
            case libretro3DS::POINTER_Y:       return in.touchPressed ? in.touchY : 0;
            case libretro3DS::POINTER_PRESSED: return in.touchPressed ? 1 : 0;
            case libretro3DS::POINTER_COUNT:   return in.touchPressed ? 1 : 0;
            default: return 0;
        }
    }

    if (device != libretro3DS::DEVICE_JOYPAD) return 0;
    switch (id) {
        case libretro3DS::JOYPAD_A:      return in.a      ? 1 : 0;
        case libretro3DS::JOYPAD_B:      return in.b      ? 1 : 0;
        case libretro3DS::JOYPAD_X:      return in.x      ? 1 : 0;
        case libretro3DS::JOYPAD_Y:      return in.y      ? 1 : 0;
        case libretro3DS::JOYPAD_START:  return in.start  ? 1 : 0;
        case libretro3DS::JOYPAD_SELECT: return in.select ? 1 : 0;
        case libretro3DS::JOYPAD_UP:     return in.up     ? 1 : 0;
        case libretro3DS::JOYPAD_DOWN:   return in.down   ? 1 : 0;
        case libretro3DS::JOYPAD_LEFT:   return in.left   ? 1 : 0;
        case libretro3DS::JOYPAD_RIGHT:  return in.right  ? 1 : 0;
        case libretro3DS::JOYPAD_L:      return in.l      ? 1 : 0;
        case libretro3DS::JOYPAD_R:      return in.r      ? 1 : 0;
        case libretro3DS::JOYPAD_L2:     return in.zl     ? 1 : 0;
        case libretro3DS::JOYPAD_R2:     return in.zr     ? 1 : 0;
        default: return 0;
    }
}

// ─── Local helpers ────────────────────────────────────────────────────────────

namespace {

std::string trim3DS(const std::string& value)
{
    const std::string ws = " \t\r\n";
    const std::size_t s = value.find_first_not_of(ws);
    if (s == std::string::npos) return "";
    return value.substr(s, value.find_last_not_of(ws) - s + 1);
}

std::string toUpper3DS(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

SDL_GameControllerButton parseControllerButton3DS(const std::string& input)
{
    const std::string n = toUpper3DS(trim3DS(input));
    if (n == "A")            return SDL_CONTROLLER_BUTTON_A;
    if (n == "B")            return SDL_CONTROLLER_BUTTON_B;
    if (n == "X")            return SDL_CONTROLLER_BUTTON_X;
    if (n == "Y")            return SDL_CONTROLLER_BUTTON_Y;
    if (n == "BACK")         return SDL_CONTROLLER_BUTTON_BACK;
    if (n == "START")        return SDL_CONTROLLER_BUTTON_START;
    if (n == "DPAD_UP")      return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (n == "DPAD_DOWN")    return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (n == "DPAD_LEFT")    return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (n == "DPAD_RIGHT")   return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (n == "LEFTSHOULDER") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (n == "RIGHTSHOULDER")return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (n == "LEFTSTICK")    return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    if (n == "RIGHTSTICK")   return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

const char* controllerButtonName3DS(SDL_GameControllerButton b)
{
    switch (b) {
        case SDL_CONTROLLER_BUTTON_A:            return "A";
        case SDL_CONTROLLER_BUTTON_B:            return "B";
        case SDL_CONTROLLER_BUTTON_X:            return "X";
        case SDL_CONTROLLER_BUTTON_Y:            return "Y";
        case SDL_CONTROLLER_BUTTON_BACK:         return "BACK";
        case SDL_CONTROLLER_BUTTON_START:        return "START";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:      return "DPAD_UP";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:    return "DPAD_DOWN";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:    return "DPAD_LEFT";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:   return "DPAD_RIGHT";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LEFTSHOULDER";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return "RIGHTSHOULDER";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:    return "LEFTSTICK";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:   return "RIGHTSTICK";
        default:                                 return "UNKNOWN";
    }
}

} // namespace

// ─── Static members ───────────────────────────────────────────────────────────

N3DSEmulation::InputMode          N3DSEmulation::globalInputMode        = N3DSEmulation::InputMode::KeyboardAndController;
N3DSEmulation::KeyBindings        N3DSEmulation::globalKeyBindings       = {};
N3DSEmulation::ControllerBindings N3DSEmulation::globalControllerBindings = {};
bool                              N3DSEmulation::globalInputConfigured   = false;

// ─── N3DSEmulation ────────────────────────────────────────────────────────────

N3DSEmulation::N3DSEmulation()
    : romLoaded(false), isRunning(false),
      window(nullptr), renderer(nullptr), screenTexture(nullptr), controller(nullptr),
      lastLetterboxRect{0, 0, 400, 480}
{
    setDefaultBindings();

    if (globalInputConfigured) {
        inputMode          = globalInputMode;
        keyBindings        = globalKeyBindings;
        controllerBindings = globalControllerBindings;
    } else {
        saveGlobalInputSettings();
    }
}

N3DSEmulation::~N3DSEmulation()
{
    unloadRom();
    cleanupSDL();
}

void N3DSEmulation::setDefaultBindings()
{
    inputMode = InputMode::KeyboardAndController;

    keyBindings.up     = SDLK_UP;
    keyBindings.down   = SDLK_DOWN;
    keyBindings.left   = SDLK_LEFT;
    keyBindings.right  = SDLK_RIGHT;
    keyBindings.a      = SDLK_x;       // 3DS A = X key
    keyBindings.b      = SDLK_z;       // 3DS B = Z key
    keyBindings.x      = SDLK_s;       // 3DS X = S key
    keyBindings.y      = SDLK_a;       // 3DS Y = A key
    keyBindings.start  = SDLK_RETURN;
    keyBindings.select = SDLK_RSHIFT;
    keyBindings.l      = SDLK_q;
    keyBindings.r      = SDLK_w;
    keyBindings.zl     = SDLK_e;
    keyBindings.zr     = SDLK_r;

    controllerBindings.up     = SDL_CONTROLLER_BUTTON_DPAD_UP;
    controllerBindings.down   = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    controllerBindings.left   = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    controllerBindings.right  = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    controllerBindings.a      = SDL_CONTROLLER_BUTTON_A;
    controllerBindings.b      = SDL_CONTROLLER_BUTTON_B;
    controllerBindings.x      = SDL_CONTROLLER_BUTTON_X;
    controllerBindings.y      = SDL_CONTROLLER_BUTTON_Y;
    controllerBindings.start  = SDL_CONTROLLER_BUTTON_START;
    controllerBindings.select = SDL_CONTROLLER_BUTTON_BACK;
    controllerBindings.l      = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    controllerBindings.r      = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    controllerBindings.zl     = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    controllerBindings.zr     = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
}

void N3DSEmulation::saveGlobalInputSettings()
{
    globalInputMode          = inputMode;
    globalKeyBindings        = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured    = true;
}

void N3DSEmulation::initSDL()
{
    if (window && renderer && screenTexture) return;

    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
            return;
        }
    }

    // 3DS native combined screen: 400×480 (top 400×240 + bottom 400×240)
    const int nativeWidth  = 400;
    const int nativeHeight = 480;

    window = SDL_CreateWindow("OmniCon Nintendo 3DS",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              800, 960,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL window failed: " << SDL_GetError() << "\n"; return; }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { std::cerr << "SDL renderer failed: " << SDL_GetError() << "\n"; return; }

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING, nativeWidth, nativeHeight);
    if (!screenTexture) { std::cerr << "SDL texture failed: " << SDL_GetError() << "\n"; return; }
    SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);

    openControllerByIndex(0);
}

void N3DSEmulation::cleanupSDL()
{
    if (controller)    { SDL_GameControllerClose(controller); controller    = nullptr; }
    if (screenTexture) { SDL_DestroyTexture(screenTexture);   screenTexture = nullptr; }
    if (renderer)      { SDL_DestroyRenderer(renderer);       renderer      = nullptr; }
    if (window)        { SDL_DestroyWindow(window);           window        = nullptr; }
}

void N3DSEmulation::renderFrameSDL()
{
    if (!renderer || !screenTexture) return;

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
        if (!screenTexture) return;
        SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
    }

    frameBuffer.resize(static_cast<size_t>(w * h) * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const n3ds_color_t px = backendPtr->getPixel(x, y);
            const size_t idx = static_cast<size_t>(y * w + x) * 4;
            frameBuffer[idx+0] = px.r;
            frameBuffer[idx+1] = px.g;
            frameBuffer[idx+2] = px.b;
            frameBuffer[idx+3] = px.a;
        }
    }
    SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), w * 4);

    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(renderer, &outW, &outH);
    const double targetAspect = backendPtr->getAspectRatio();
    const double windowAspect = outH > 0 ? static_cast<double>(outW) / outH : targetAspect;
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

void N3DSEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== Nintendo 3DS Input Configuration ===\n";
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
        choice = trim3DS(choice);

        if (choice == "0") break;

        if (choice == "1") { inputMode = InputMode::KeyboardOnly;          std::cout << "Keyboard only.\n";           continue; }
        if (choice == "2") { inputMode = InputMode::ControllerOnly;        std::cout << "Controller only.\n";         continue; }
        if (choice == "3") { inputMode = InputMode::KeyboardAndController;  std::cout << "Keyboard + Controller.\n";  continue; }

        if (choice == "4") {
            struct KA { const char* name; SDL_Keycode* key; };
            KA actions[] = {
                {"UP",     &keyBindings.up},
                {"DOWN",   &keyBindings.down},
                {"LEFT",   &keyBindings.left},
                {"RIGHT",  &keyBindings.right},
                {"A",      &keyBindings.a},
                {"B",      &keyBindings.b},
                {"X",      &keyBindings.x},
                {"Y",      &keyBindings.y},
                {"START",  &keyBindings.start},
                {"SELECT", &keyBindings.select},
                {"L",      &keyBindings.l},
                {"R",      &keyBindings.r},
                {"ZL",     &keyBindings.zl},
                {"ZR",     &keyBindings.zr},
            };
            std::cout << "Enter SDL key names (e.g. Up, Z, X, Return). Press Enter to keep.\n";
            for (KA& a : actions) {
                std::cout << a.name << " [" << SDL_GetKeyName(*a.key) << "]: ";
                std::string keyName; std::getline(std::cin, keyName); keyName = trim3DS(keyName);
                if (keyName.empty()) continue;
                SDL_Keycode parsed = SDL_GetKeyFromName(keyName.c_str());
                if (parsed == SDLK_UNKNOWN) { std::cout << "  Invalid key, keeping existing.\n"; continue; }
                *a.key = parsed;
            }
            std::cout << "Keyboard bindings updated.\n";
            continue;
        }

        if (choice == "5") {
            listControllers();
            std::cout << "Enter controller index to use: ";
            std::string idx; std::getline(std::cin, idx); idx = trim3DS(idx);
            if (idx.empty()) continue;
            try { openControllerByIndex(std::stoi(idx)); }
            catch (...) { std::cout << "Invalid controller index.\n"; }
            continue;
        }

        if (choice == "6") {
            struct CA { const char* name; SDL_GameControllerButton* button; };
            CA actions[] = {
                {"UP",     &controllerBindings.up},
                {"DOWN",   &controllerBindings.down},
                {"LEFT",   &controllerBindings.left},
                {"RIGHT",  &controllerBindings.right},
                {"A",      &controllerBindings.a},
                {"B",      &controllerBindings.b},
                {"X",      &controllerBindings.x},
                {"Y",      &controllerBindings.y},
                {"START",  &controllerBindings.start},
                {"SELECT", &controllerBindings.select},
                {"L",      &controllerBindings.l},
                {"R",      &controllerBindings.r},
                {"ZL",     &controllerBindings.zl},
                {"ZR",     &controllerBindings.zr},
            };
            std::cout << "Enter button names (A,B,X,Y,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT,\n";
            std::cout << "  LEFTSHOULDER,RIGHTSHOULDER,LEFTSTICK,RIGHTSTICK).\n";
            std::cout << "Press Enter to keep current binding.\n";
            for (CA& a : actions) {
                std::cout << a.name << " [" << controllerButtonName3DS(*a.button) << "]: ";
                std::string btnName; std::getline(std::cin, btnName); btnName = trim3DS(btnName);
                if (btnName.empty()) continue;
                SDL_GameControllerButton parsed = parseControllerButton3DS(btnName);
                if (parsed == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "  Invalid button, keeping existing.\n"; continue; }
                *a.button = parsed;
            }
            std::cout << "Controller bindings updated.\n";
            continue;
        }

        std::cout << "Unknown option.\n";
    }

    saveGlobalInputSettings();
    std::cout << "Nintendo 3DS controls saved.\n";
}

void N3DSEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, n3ds_input_t& input, bool& running)
{
    if (key == SDLK_ESCAPE && pressed) { running = false; return; }
    if      (key == keyBindings.up)     input.up     = pressed;
    else if (key == keyBindings.down)   input.down   = pressed;
    else if (key == keyBindings.left)   input.left   = pressed;
    else if (key == keyBindings.right)  input.right  = pressed;
    else if (key == keyBindings.a)      input.a      = pressed;
    else if (key == keyBindings.b)      input.b      = pressed;
    else if (key == keyBindings.x)      input.x      = pressed;
    else if (key == keyBindings.y)      input.y      = pressed;
    else if (key == keyBindings.start)  input.start  = pressed;
    else if (key == keyBindings.select) input.select = pressed;
    else if (key == keyBindings.l)      input.l      = pressed;
    else if (key == keyBindings.r)      input.r      = pressed;
    else if (key == keyBindings.zl)     input.zl     = pressed;
    else if (key == keyBindings.zr)     input.zr     = pressed;
}

void N3DSEmulation::handleControllerButtonEvent(Uint8 button, bool pressed, n3ds_input_t& input, bool& running)
{
    const auto b = static_cast<SDL_GameControllerButton>(button);
    if (b == SDL_CONTROLLER_BUTTON_BACK && pressed) { running = false; return; }
    if      (b == controllerBindings.up)     input.up     = pressed;
    else if (b == controllerBindings.down)   input.down   = pressed;
    else if (b == controllerBindings.left)   input.left   = pressed;
    else if (b == controllerBindings.right)  input.right  = pressed;
    else if (b == controllerBindings.a)      input.a      = pressed;
    else if (b == controllerBindings.b)      input.b      = pressed;
    else if (b == controllerBindings.x)      input.x      = pressed;
    else if (b == controllerBindings.y)      input.y      = pressed;
    else if (b == controllerBindings.start)  input.start  = pressed;
    else if (b == controllerBindings.select) input.select = pressed;
    else if (b == controllerBindings.l)      input.l      = pressed;
    else if (b == controllerBindings.r)      input.r      = pressed;
    else if (b == controllerBindings.zl)     input.zl     = pressed;
    else if (b == controllerBindings.zr)     input.zr     = pressed;
}

bool N3DSEmulation::openControllerByIndex(int index)
{
    if (controller) { SDL_GameControllerClose(controller); controller = nullptr; }
    const int count = SDL_NumJoysticks();
    if (count <= 0 || index < 0 || index >= count || !SDL_IsGameController(index)) return false;
    controller = SDL_GameControllerOpen(index);
    return controller != nullptr;
}

void N3DSEmulation::listControllers() const
{
    const int count = SDL_NumJoysticks();
    std::cout << "Controllers: " << count << "\n";
    for (int i = 0; i < count; ++i) {
        std::cout << "  [" << i << "] "
                  << (SDL_IsGameController(i) ? "game controller" : "joystick")
                  << " - " << SDL_JoystickNameForIndex(i) << "\n";
    }
}

std::vector<unsigned char> Libretro3DSBackend::dumpSaveData() const
{
    if (!coreHandle || !gameLoaded || !fn_serialize_size || !fn_serialize) return {};
    const size_t sz = fn_serialize_size();
    if (sz == 0) return {};
    std::vector<unsigned char> state(sz, 0);
    if (!fn_serialize(state.data(), sz)) return {};
    return state;
}

bool Libretro3DSBackend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) return false;
    return fn_unserialize(data.data(), data.size());
}

std::vector<unsigned char> N3DSEmulation::dumpSaveData() const
{
    if (!backendPtr || !isLoaded()) return {};
    return backendPtr->dumpSaveData();
}

bool N3DSEmulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!backendPtr || !isLoaded()) return false;
    return backendPtr->restoreSaveData(data);
}

bool N3DSEmulation::loadRom(const string& path)
{
    unloadRom();

    std::filesystem::path fsPath(path);
    std::string ext = fsPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext != ".3ds" && ext != ".cci" && ext != ".cxi" && ext != ".3dsx" && ext != ".cia") {
        std::cerr << "[3DS] Invalid file extension. Nintendo 3DS ROMs use .3ds, .cci, .cxi, .3dsx, or .cia\n";
        return false;
    }

    auto libretroBackend = std::make_unique<Libretro3DSBackend>();
    if (libretroBackend->isAvailable()) {
        backendPtr = std::move(libretroBackend);
    } else {
        backendPtr = std::make_unique<N3DSUnavailableBackend>();
    }

    if (!backendPtr->loadRom(path)) {
        backendPtr.reset();
        return false;
    }

    romPath   = path;
    romLoaded = true;
    isRunning = true;
    return true;
}

void N3DSEmulation::unloadRom()
{
    isRunning = false;
    romLoaded = false;
    backendPtr.reset();
    romPath.clear();
}

void N3DSEmulation::runInteractive()
{
    if (!romLoaded || !backendPtr) {
        std::cerr << "[3DS] No Nintendo 3DS ROM loaded.\n";
        return;
    }

    initSDL();
    if (!window || !renderer || !screenTexture) {
        std::cerr << "[3DS] Failed to initialize SDL.\n";
        return;
    }

    if (!backendPtr->initializeAudio()) {
        std::cerr << "[3DS] Audio initialization failed.\n";
    }

    std::cout << "Nintendo 3DS: " << romPath << "\n";
    std::cout << "Press Escape or close the window to exit.\n";
    std::cout << "Default keys: Arrows=D-pad, X=A, Z=B, S=X, A=Y, Q=L, W=R, E=ZL, R=ZR, Enter=Start, RShift=Select\n";

    bool running = true;
    n3ds_input_t input{};

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
                    handleControllerButtonEvent(event.cbutton.button, true, input, running);
            } else if (event.type == SDL_CONTROLLERBUTTONUP) {
                if (inputMode != InputMode::KeyboardOnly)
                    handleControllerButtonEvent(event.cbutton.button, false, input, running);
            } else if (event.type == SDL_CONTROLLERDEVICEADDED && controller == nullptr) {
                openControllerByIndex(event.cdevice.which);
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller != nullptr) {
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                    SDL_GameControllerClose(controller); controller = nullptr;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                handleMouseEvent(event.button.x, event.button.y, true, input);
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                handleMouseEvent(event.button.x, event.button.y, false, input);
            } else if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK)) {
                handleMouseEvent(event.motion.x, event.motion.y, true, input);
            }
        }

        backendPtr->setInput(input);
        backendPtr->nextFrame();
        renderFrameSDL();

        const auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameDuration)
            std::this_thread::sleep_for(frameDuration - elapsed);
    }

    cleanupSDL();
    isRunning = false;
}

void N3DSEmulation::configureGlobalControls()
{
    N3DSEmulation tmp;
    tmp.configureInputMenu();
    tmp.saveGlobalInputSettings();
}

void N3DSEmulation::handleMouseEvent(int winX, int winY, bool pressed, n3ds_input_t& input)
{
    input.touchPressed = pressed;
    if (!pressed || !backendPtr) return;
    if (lastLetterboxRect.w <= 0 || lastLetterboxRect.h <= 0) return;

    const int fbW = backendPtr->getFrameWidth();
    const int fbH = backendPtr->getFrameHeight();
    if (fbW <= 1 || fbH <= 1) return;

    // Map window pixel → framebuffer pixel
    const double fbX = static_cast<double>(winX - lastLetterboxRect.x)
                       * fbW / lastLetterboxRect.w;
    const double fbY = static_cast<double>(winY - lastLetterboxRect.y)
                       * fbH / lastLetterboxRect.h;

    const double cx = std::max(0.0, std::min(static_cast<double>(fbW - 1), fbX));
    const double cy = std::max(0.0, std::min(static_cast<double>(fbH - 1), fbY));

    // Map framebuffer pixel → libretro pointer range [-0x7fff, +0x7fff]
    input.touchX = static_cast<int16_t>(cx / (fbW - 1) * 65534.0 - 32767.0);
    input.touchY = static_cast<int16_t>(cy / (fbH - 1) * 65534.0 - 32767.0);
}
