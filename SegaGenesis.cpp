#include "SegaGenesis.hpp"
#include "audio_clock.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

// ─── Anonymous helpers ─────────────────────────────────────────────────────────

namespace {

static std::string trimStr(const std::string& s) {
    const std::string ws = " \t\r\n";
    const auto start = s.find_first_not_of(ws);
    if (start == std::string::npos) return "";
    return s.substr(start, s.find_last_not_of(ws) - start + 1);
}

static std::string toUpperStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

static SDL_GameControllerButton parseBtn(const std::string& input) {
    const std::string n = toUpperStr(trimStr(input));
    if (n == "A")             return SDL_CONTROLLER_BUTTON_A;
    if (n == "B")             return SDL_CONTROLLER_BUTTON_B;
    if (n == "X")             return SDL_CONTROLLER_BUTTON_X;
    if (n == "Y")             return SDL_CONTROLLER_BUTTON_Y;
    if (n == "BACK")          return SDL_CONTROLLER_BUTTON_BACK;
    if (n == "START")         return SDL_CONTROLLER_BUTTON_START;
    if (n == "LEFTSHOULDER")  return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (n == "RIGHTSHOULDER") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (n == "DPAD_UP")       return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (n == "DPAD_DOWN")     return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (n == "DPAD_LEFT")     return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (n == "DPAD_RIGHT")    return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

} // namespace

// ─── libretro interface types ──────────────────────────────────────────────────

namespace libretro_gen {

static constexpr unsigned ENV_GET_CAN_DUPE           =  3;
static constexpr unsigned ENV_GET_SYSTEM_DIRECTORY   =  9;
static constexpr unsigned ENV_SET_PIXEL_FORMAT       = 10;
static constexpr unsigned ENV_GET_VARIABLE           = 15;
static constexpr unsigned ENV_SET_VARIABLES          = 16;
static constexpr unsigned ENV_GET_VARIABLE_UPDATE    = 17;
static constexpr unsigned ENV_SET_SUPPORT_NO_GAME    = 18;
static constexpr unsigned ENV_GET_LOG_INTERFACE      = 27;
static constexpr unsigned ENV_GET_CONTENT_DIRECTORY  = 30;
static constexpr unsigned ENV_GET_SAVE_DIRECTORY     = 31;
static constexpr unsigned ENV_SET_GEOMETRY           = 37;

static constexpr unsigned PIXEL_0RGB1555 = 0;
static constexpr unsigned PIXEL_XRGB8888 = 1;
static constexpr unsigned PIXEL_RGB565   = 2;

static constexpr unsigned DEVICE_JOYPAD = 1;
static constexpr unsigned JOYPAD_B      = 0;
static constexpr unsigned JOYPAD_Y      = 1;
static constexpr unsigned JOYPAD_SELECT = 2;
static constexpr unsigned JOYPAD_START  = 3;
static constexpr unsigned JOYPAD_UP     = 4;
static constexpr unsigned JOYPAD_DOWN   = 5;
static constexpr unsigned JOYPAD_LEFT   = 6;
static constexpr unsigned JOYPAD_RIGHT  = 7;
static constexpr unsigned JOYPAD_A      = 8;
static constexpr unsigned JOYPAD_X      = 9;
static constexpr unsigned JOYPAD_L      = 10;
static constexpr unsigned JOYPAD_R      = 11;

struct game_info   { const char* path; const void* data; size_t size; const char* meta; };
struct system_info { const char* library_name; const char* library_version; const char* valid_extensions;
                     bool need_fullpath; bool block_extract; };
struct game_geometry { unsigned base_width; unsigned base_height;
                       unsigned max_width; unsigned max_height; float aspect_ratio; };
struct system_timing { double fps; double sample_rate; };
struct av_info { game_geometry geometry; system_timing timing; };
struct variable    { const char* key; const char* value; };

using environment_t        = bool    (*)(unsigned cmd, void* data);
using video_refresh_t      = void    (*)(const void* data, unsigned width, unsigned height, size_t pitch);
using audio_sample_t       = void    (*)(int16_t left, int16_t right);
using audio_sample_batch_t = size_t  (*)(const int16_t* data, size_t frames);
using input_poll_t         = void    (*)(void);
using input_state_t        = int16_t (*)(unsigned port, unsigned device, unsigned index, unsigned id);
using serialize_size_t     = size_t  (*)(void);
using serialize_t          = bool    (*)(void* data, size_t size);
using unserialize_t        = bool    (*)(const void* data, size_t size);

} // namespace libretro_gen

// ─── Abstract backend ──────────────────────────────────────────────────────────

class GenesisBackend {
public:
    virtual ~GenesisBackend() = default;
    virtual bool loadRom(const std::string& path)                                  = 0;
    virtual bool nextFrame()                                                        = 0;
    virtual void setInput(const genesis_input_t& input)                            = 0;
    virtual std::vector<unsigned char> dumpSaveData() const                        = 0;
    virtual bool restoreSaveData(const std::vector<unsigned char>& data)           = 0;
    virtual genesis_color_t getPixel(int x, int y) const                           = 0;
    virtual int  getFrameWidth()  const                                             = 0;
    virtual int  getFrameHeight() const                                             = 0;
    virtual int  getAudioSampleRate() const { return 48000;  }
    virtual double getNominalFps()    const { return 60.0;   }
};

// ─── Unavailable stub ──────────────────────────────────────────────────────────

class GenesisUnavailableBackend final : public GenesisBackend {
public:
    bool loadRom(const std::string& p) override {
        (void)p;
        std::cerr << "[Genesis] Core not found. Install with:\n"
                     "  sudo apt-get install libretro-genesisplusgx\n";
        return false;
    }
    bool nextFrame() override { return false; }
    void setInput(const genesis_input_t& i) override { (void)i; }
    std::vector<unsigned char> dumpSaveData() const override { return {}; }
    bool restoreSaveData(const std::vector<unsigned char>& d) override { (void)d; return false; }
    genesis_color_t getPixel(int x, int y) const override { (void)x; (void)y; return {0,0,0,255}; }
    int getFrameWidth()  const override { return 320; }
    int getFrameHeight() const override { return 224; }
};

// ─── Libretro Genesis Plus GX backend ─────────────────────────────────────────

class LibretroGenesisPlusGxBackend final : public GenesisBackend {
public:
    static constexpr const char* kCorePrimary  =
        "/usr/lib/x86_64-linux-gnu/libretro/genesis_plus_gx_libretro.so";
    static constexpr const char* kCoreFallback =
        "third_party/libretro/genesis_plus_gx_libretro.so";

    LibretroGenesisPlusGxBackend();
    ~LibretroGenesisPlusGxBackend() override;

    bool isAvailable() const { return coreHandle != nullptr; }

    bool loadRom(const std::string& path) override;
    bool nextFrame()                       override;
    void setInput(const genesis_input_t& input) override;
    std::vector<unsigned char> dumpSaveData() const override;
    bool restoreSaveData(const std::vector<unsigned char>& data) override;
    genesis_color_t getPixel(int x, int y) const override;
    int  getFrameWidth()        const override { return frameWidth;       }
    int  getFrameHeight()       const override { return frameHeight;      }
    int  getAudioSampleRate()   const override { return audioSampleRate;  }
    double getNominalFps()      const override { return coreFps;          }

    bool onEnvironment(unsigned cmd, void* data);
    void onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);

private:
    bool loadSymbols();
    void ensureLibretroDirectories();
    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioFrames(const int16_t* data, size_t frames);

    void*  coreHandle = nullptr;

    void   (*fn_init)()                                                     = nullptr;
    void   (*fn_deinit)()                                                   = nullptr;
    void   (*fn_get_system_info)(libretro_gen::system_info*)                = nullptr;
    void   (*fn_get_system_av_info)(libretro_gen::av_info*)                 = nullptr;
    void   (*fn_set_environment)(libretro_gen::environment_t)               = nullptr;
    void   (*fn_set_video_refresh)(libretro_gen::video_refresh_t)           = nullptr;
    void   (*fn_set_audio_sample)(libretro_gen::audio_sample_t)             = nullptr;
    void   (*fn_set_audio_sample_batch)(libretro_gen::audio_sample_batch_t) = nullptr;
    void   (*fn_set_input_poll)(libretro_gen::input_poll_t)                 = nullptr;
    void   (*fn_set_input_state)(libretro_gen::input_state_t)               = nullptr;
    size_t (*fn_serialize_size)()                                           = nullptr;
    bool   (*fn_serialize)(void*, size_t)                                   = nullptr;
    bool   (*fn_unserialize)(const void*, size_t)                           = nullptr;
    bool   (*fn_load_game)(const libretro_gen::game_info*)                  = nullptr;
    void   (*fn_unload_game)()                                              = nullptr;
    void   (*fn_run)()                                                      = nullptr;

    std::vector<uint8_t> framePixels;
    int   frameWidth      = 320;
    int   frameHeight     = 224;
    unsigned pixelFormat  = libretro_gen::PIXEL_0RGB1555;

    genesis_input_t currentInput = {};
    bool  gameLoaded              = false;
    std::vector<uint8_t> romData;
    std::string loadedRomPath;
    std::string systemDirectory;
    std::string saveDirectory;
    std::string contentDirectory;

    SDL_AudioDeviceID audioDevice   = 0;
    SDL_AudioStream*  audioStream   = nullptr;
    int   coreSampleRate            = 48000;
    int   audioSampleRate           = 48000;
    double coreFps                  = 60.0;
    std::vector<uint8_t> audioConvertBuffer;

    static LibretroGenesisPlusGxBackend* s_instance;
    static bool    s_envCb(unsigned cmd, void* data);
    static void    s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    s_audioCb(int16_t l, int16_t r);
    static size_t  s_audioBatchCb(const int16_t* data, size_t frames);
    static void    s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
};

LibretroGenesisPlusGxBackend* LibretroGenesisPlusGxBackend::s_instance = nullptr;

// ─── Constructor / Destructor ─────────────────────────────────────────────────

LibretroGenesisPlusGxBackend::LibretroGenesisPlusGxBackend()
{
    coreHandle = dlopen(kCorePrimary, RTLD_LAZY);
    if (!coreHandle)
        coreHandle = dlopen(kCoreFallback, RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[Genesis] " << dlerror() << "\n"
                     "  Install: sudo apt-get install libretro-genesisplusgx\n";
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

LibretroGenesisPlusGxBackend::~LibretroGenesisPlusGxBackend()
{
    closeAudioDevice();
    if (coreHandle) {
        if (gameLoaded) fn_unload_game();
        fn_deinit();
        dlclose(coreHandle);
        coreHandle = nullptr;
    }
    if (s_instance == this) s_instance = nullptr;
}

// ─── Directory helpers ─────────────────────────────────────────────────────────

void LibretroGenesisPlusGxBackend::ensureLibretroDirectories()
{
    const std::filesystem::path rootDir = std::filesystem::current_path() / "third_party" / "libretro";
    const std::filesystem::path sysDir  = rootDir / "system";
    const std::filesystem::path savDir  = std::filesystem::current_path() / "saves" / "genesis";
    const std::filesystem::path conDir  = rootDir / "content";

    std::error_code ec;
    std::filesystem::create_directories(sysDir, ec);
    std::filesystem::create_directories(savDir, ec);
    std::filesystem::create_directories(conDir, ec);

    systemDirectory  = sysDir.string();
    saveDirectory    = savDir.string();
    contentDirectory = conDir.string();
}

// ─── Symbol loading ────────────────────────────────────────────────────────────

bool LibretroGenesisPlusGxBackend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) std::cerr << "[Genesis] Missing symbol: " << name << "\n";
        return sym;
    };
    fn_init               = reinterpret_cast<void(*)()>(load("retro_init"));
    fn_deinit             = reinterpret_cast<void(*)()>(load("retro_deinit"));
    fn_get_system_info    = reinterpret_cast<void(*)(libretro_gen::system_info*)>(load("retro_get_system_info"));
    fn_get_system_av_info = reinterpret_cast<void(*)(libretro_gen::av_info*)>(load("retro_get_system_av_info"));
    fn_set_environment    = reinterpret_cast<void(*)(libretro_gen::environment_t)>(load("retro_set_environment"));
    fn_set_video_refresh  = reinterpret_cast<void(*)(libretro_gen::video_refresh_t)>(load("retro_set_video_refresh"));
    fn_set_audio_sample   = reinterpret_cast<void(*)(libretro_gen::audio_sample_t)>(load("retro_set_audio_sample"));
    fn_set_audio_sample_batch = reinterpret_cast<void(*)(libretro_gen::audio_sample_batch_t)>(load("retro_set_audio_sample_batch"));
    fn_set_input_poll     = reinterpret_cast<void(*)(libretro_gen::input_poll_t)>(load("retro_set_input_poll"));
    fn_set_input_state    = reinterpret_cast<void(*)(libretro_gen::input_state_t)>(load("retro_set_input_state"));
    fn_serialize_size     = reinterpret_cast<size_t(*)()>(load("retro_serialize_size"));
    fn_serialize          = reinterpret_cast<bool(*)(void*, size_t)>(load("retro_serialize"));
    fn_unserialize        = reinterpret_cast<bool(*)(const void*, size_t)>(load("retro_unserialize"));
    fn_load_game          = reinterpret_cast<bool(*)(const libretro_gen::game_info*)>(load("retro_load_game"));
    fn_unload_game        = reinterpret_cast<void(*)()>(load("retro_unload_game"));
    fn_run                = reinterpret_cast<void(*)()>(load("retro_run"));
    return fn_init && fn_deinit && fn_get_system_info && fn_get_system_av_info &&
           fn_set_environment && fn_set_video_refresh && fn_set_audio_sample &&
           fn_set_audio_sample_batch && fn_set_input_poll && fn_set_input_state &&
           fn_serialize_size && fn_serialize && fn_unserialize &&
           fn_load_game && fn_unload_game && fn_run;
}

// ─── Environment callback ──────────────────────────────────────────────────────

bool LibretroGenesisPlusGxBackend::onEnvironment(unsigned cmd, void* data)
{
    switch (cmd) {
        case libretro_gen::ENV_GET_CAN_DUPE:
            *static_cast<bool*>(data) = true;
            return true;
        case libretro_gen::ENV_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = systemDirectory.c_str();
            return true;
        case libretro_gen::ENV_SET_PIXEL_FORMAT:
            pixelFormat = *static_cast<unsigned*>(data);
            return true;
        case libretro_gen::ENV_GET_VARIABLE: {
            auto* v = static_cast<libretro_gen::variable*>(data);
            if (v) v->value = nullptr;
            return false;
        }
        case libretro_gen::ENV_SET_VARIABLES:
            return true;
        case libretro_gen::ENV_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;
        case libretro_gen::ENV_SET_SUPPORT_NO_GAME:
            return true;
        case libretro_gen::ENV_GET_LOG_INTERFACE:
            return false;
        case libretro_gen::ENV_GET_CONTENT_DIRECTORY:
            *static_cast<const char**>(data) = contentDirectory.c_str();
            return true;
        case libretro_gen::ENV_GET_SAVE_DIRECTORY:
            *static_cast<const char**>(data) = saveDirectory.c_str();
            return true;
        case libretro_gen::ENV_SET_GEOMETRY: {
            const auto* g = static_cast<const libretro_gen::game_geometry*>(data);
            frameWidth  = static_cast<int>(g->base_width);
            frameHeight = static_cast<int>(g->base_height);
            framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
            return true;
        }
        default:
            return false;
    }
}

// ─── Video callback ────────────────────────────────────────────────────────────

void LibretroGenesisPlusGxBackend::onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    if (width == 0 || height == 0 || width > 2048 || height > 2048) {
        std::cerr << "[Genesis] Rejecting invalid video dimensions: " << width << "x" << height << "\n";
        return;
    }
    if (static_cast<int>(width) != frameWidth || static_cast<int>(height) != frameHeight) {
        frameWidth  = static_cast<int>(width);
        frameHeight = static_cast<int>(height);
        framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    }
    const auto* src = static_cast<const uint8_t*>(data);
    for (unsigned row = 0; row < height; ++row) {
        const uint8_t* srcRow = src + row * pitch;
        uint8_t*       dstRow = framePixels.data() + row * static_cast<size_t>(frameWidth) * 4;
        if (pixelFormat == libretro_gen::PIXEL_XRGB8888) {
            for (unsigned col = 0; col < width; ++col) {
                uint32_t px; std::memcpy(&px, srcRow + col * 4, 4);
                dstRow[col*4+0] = static_cast<uint8_t>((px >> 16) & 0xFF);
                dstRow[col*4+1] = static_cast<uint8_t>((px >>  8) & 0xFF);
                dstRow[col*4+2] = static_cast<uint8_t>( px        & 0xFF);
                dstRow[col*4+3] = 255;
            }
        } else if (pixelFormat == libretro_gen::PIXEL_RGB565) {
            for (unsigned col = 0; col < width; ++col) {
                uint16_t px; std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[col*4+0] = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
                dstRow[col*4+1] = static_cast<uint8_t>(((px >>  5) & 0x3F) * 255 / 63);
                dstRow[col*4+2] = static_cast<uint8_t>(( px        & 0x1F) * 255 / 31);
                dstRow[col*4+3] = 255;
            }
        } else {
            // PIXEL_0RGB1555
            for (unsigned col = 0; col < width; ++col) {
                uint16_t px; std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[col*4+0] = static_cast<uint8_t>(((px >> 10) & 0x1F) * 255 / 31);
                dstRow[col*4+1] = static_cast<uint8_t>(((px >>  5) & 0x1F) * 255 / 31);
                dstRow[col*4+2] = static_cast<uint8_t>(( px        & 0x1F) * 255 / 31);
                dstRow[col*4+3] = 255;
            }
        }
    }
}

// ─── Save data ─────────────────────────────────────────────────────────────────

std::vector<unsigned char> LibretroGenesisPlusGxBackend::dumpSaveData() const
{
    if (!coreHandle || !gameLoaded || !fn_serialize_size || !fn_serialize) return {};
    const size_t sz = fn_serialize_size();
    if (sz == 0) return {};
    std::vector<unsigned char> buf(sz, 0);
    if (!fn_serialize(buf.data(), sz)) return {};
    return buf;
}

bool LibretroGenesisPlusGxBackend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) return false;
    return fn_unserialize(data.data(), data.size());
}

// ─── loadRom ───────────────────────────────────────────────────────────────────

bool LibretroGenesisPlusGxBackend::loadRom(const std::string& path)
{
    if (!coreHandle) return false;
    if (gameLoaded) { fn_unload_game(); gameLoaded = false; romData.clear(); }

    ensureLibretroDirectories();

    libretro_gen::system_info sysInfo = {};
    fn_get_system_info(&sysInfo);

    libretro_gen::game_info gameInfo = {};
    loadedRomPath = path;
    gameInfo.path = loadedRomPath.c_str();
    gameInfo.meta = nullptr;

    if (!sysInfo.need_fullpath) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) { std::cerr << "[Genesis] Cannot open ROM: " << path << "\n"; return false; }
        const auto size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);
        romData.resize(size);
        if (!f.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
            std::cerr << "[Genesis] Failed to read ROM: " << path << "\n";
            romData.clear(); return false;
        }
        gameInfo.data = romData.data();
        gameInfo.size = romData.size();
    }

    if (!fn_load_game(&gameInfo)) {
        std::cerr << "[Genesis] Core rejected ROM: " << path << "\n";
        romData.clear(); return false;
    }
    gameLoaded = true;

    libretro_gen::av_info avInfo = {};
    fn_get_system_av_info(&avInfo);
    if (avInfo.timing.fps > 1.0) coreFps = avInfo.timing.fps;
    frameWidth  = static_cast<int>(avInfo.geometry.base_width);
    frameHeight = static_cast<int>(avInfo.geometry.base_height);
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    ensureAudioDevice(static_cast<int>(avInfo.timing.sample_rate));
    return true;
}

bool LibretroGenesisPlusGxBackend::nextFrame()
{
    if (!coreHandle || !gameLoaded) return false;
    fn_run();
    return true;
}

void LibretroGenesisPlusGxBackend::setInput(const genesis_input_t& input)
{
    currentInput = input;
}

genesis_color_t LibretroGenesisPlusGxBackend::getPixel(int x, int y) const
{
    if (x < 0 || x >= frameWidth || y < 0 || y >= frameHeight) return {0,0,0,255};
    const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(frameWidth) + static_cast<size_t>(x)) * 4;
    return {framePixels[off], framePixels[off+1], framePixels[off+2], framePixels[off+3]};
}

// ─── Audio helpers ─────────────────────────────────────────────────────────────

bool LibretroGenesisPlusGxBackend::ensureAudioDevice(int sampleRate)
{
    if (sampleRate <= 0) sampleRate = 48000;
    if (audioDevice != 0 && sampleRate == coreSampleRate) return true;
    closeAudioDevice();
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[Genesis] SDL audio init failed: " << SDL_GetError() << "\n";
            return false;
        }
    }
    SDL_AudioSpec desired = {};
    desired.freq     = sampleRate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 1024;
    desired.callback = nullptr;
    SDL_AudioSpec obtained = {};
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) {
        std::cerr << "[Genesis] Failed to open audio: " << SDL_GetError() << "\n";
        return false;
    }
    coreSampleRate  = sampleRate;
    audioSampleRate = obtained.freq;
    audioStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, coreSampleRate, AUDIO_S16SYS, 2, audioSampleRate);
    if (!audioStream) {
        std::cerr << "[Genesis] Failed to create audio stream: " << SDL_GetError() << "\n";
        closeAudioDevice(); return false;
    }
    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void LibretroGenesisPlusGxBackend::closeAudioDevice()
{
    if (audioStream) { SDL_FreeAudioStream(audioStream); audioStream = nullptr; }
    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
}

void LibretroGenesisPlusGxBackend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0) return;
    if (audioDevice == 0 && !ensureAudioDevice(audioSampleRate)) return;
    if (!audioStream) return;
    const int frameBytes  = 2 * static_cast<int>(sizeof(int16_t));
    const size_t inBytes  = frames * static_cast<size_t>(frameBytes);
    if (SDL_AudioStreamPut(audioStream, data, static_cast<int>(inBytes)) < 0) return;
    const size_t maxQueued = static_cast<size_t>(audioSampleRate) * 2 * sizeof(int16_t) / 5;
    const size_t queued    = static_cast<size_t>(SDL_GetQueuedAudioSize(audioDevice));
    if (queued >= maxQueued) return;
    int avail = SDL_AudioStreamAvailable(audioStream);
    if (avail <= 0) return;
    size_t toQueue = std::min<size_t>(static_cast<size_t>(avail), maxQueued - queued);
    toQueue -= toQueue % static_cast<size_t>(frameBytes);
    if (toQueue == 0) return;
    audioConvertBuffer.resize(toQueue);
    const int got = SDL_AudioStreamGet(audioStream, audioConvertBuffer.data(), static_cast<int>(toQueue));
    if (got > 0) SDL_QueueAudio(audioDevice, audioConvertBuffer.data(), static_cast<Uint32>(got));
}

// ─── Static callbacks ──────────────────────────────────────────────────────────

bool    LibretroGenesisPlusGxBackend::s_envCb(unsigned cmd, void* data)
    { return s_instance ? s_instance->onEnvironment(cmd, data) : false; }
void    LibretroGenesisPlusGxBackend::s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch)
    {
        if (s_instance) s_instance->onVideoRefresh(data, w, h, pitch);
    }
void    LibretroGenesisPlusGxBackend::s_audioCb(int16_t l, int16_t r)
    { if (s_instance) { int16_t f[2]={l,r}; s_instance->queueAudioFrames(f, 1); } }
size_t  LibretroGenesisPlusGxBackend::s_audioBatchCb(const int16_t* data, size_t frames)
    { if (s_instance && data && frames > 0) s_instance->queueAudioFrames(data, frames); return frames; }
void    LibretroGenesisPlusGxBackend::s_inputPollCb() {}
int16_t LibretroGenesisPlusGxBackend::s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (!s_instance || port != 0 || device != libretro_gen::DEVICE_JOYPAD || index != 0) return 0;
    const genesis_input_t& in = s_instance->currentInput;
    // Genesis Plus GX 6-button pad mapping
    switch (id) {
        case libretro_gen::JOYPAD_UP:     return in.up    ? 1 : 0;
        case libretro_gen::JOYPAD_DOWN:   return in.down  ? 1 : 0;
        case libretro_gen::JOYPAD_LEFT:   return in.left  ? 1 : 0;
        case libretro_gen::JOYPAD_RIGHT:  return in.right ? 1 : 0;
        case libretro_gen::JOYPAD_B:      return in.b     ? 1 : 0;  // Genesis B
        case libretro_gen::JOYPAD_Y:      return in.a     ? 1 : 0;  // Genesis A
        case libretro_gen::JOYPAD_A:      return in.c     ? 1 : 0;  // Genesis C
        case libretro_gen::JOYPAD_X:      return in.y     ? 1 : 0;  // Genesis Y
        case libretro_gen::JOYPAD_L:      return in.x     ? 1 : 0;  // Genesis X
        case libretro_gen::JOYPAD_R:      return in.z     ? 1 : 0;  // Genesis Z
        case libretro_gen::JOYPAD_SELECT: return in.mode  ? 1 : 0;  // Mode
        case libretro_gen::JOYPAD_START:  return in.start ? 1 : 0;  // Start
        default: return 0;
    }
}

// ─── SegaGenesisEmulation static statics ──────────────────────────────────────

SegaGenesisEmulation::InputMode          SegaGenesisEmulation::globalInputMode         = SegaGenesisEmulation::InputMode::KeyboardOnly;
SegaGenesisEmulation::KeyBindings        SegaGenesisEmulation::globalKeyBindings        = {};
SegaGenesisEmulation::ControllerBindings SegaGenesisEmulation::globalControllerBindings = {};
bool                                     SegaGenesisEmulation::globalInputConfigured    = false;

// ─── Constructor / Destructor ─────────────────────────────────────────────────

SegaGenesisEmulation::SegaGenesisEmulation()
    : isRunning(false), window(nullptr), renderer(nullptr),
      screenTexture(nullptr), controller(nullptr), controllerDeviceIndex(-1),
      scaleX(2), scaleY(2), workerInput{},
      emulationThreadStopRequested(false), emulationThreadRunning(false),
      emulationThreadFailed(false), frameReady(false),
      inputMode(InputMode::KeyboardOnly)
{
    auto lib = std::make_unique<LibretroGenesisPlusGxBackend>();
    if (lib->isAvailable())
        backend = std::move(lib);
    else
        backend = std::make_unique<GenesisUnavailableBackend>();

    const int w = backend->getFrameWidth();
    const int h = backend->getFrameHeight();
    frameBuffer.resize(static_cast<size_t>(w * h) * 4, 0);
    latestFrame.resize(static_cast<size_t>(w * h) * 4, 0);

    setDefaultBindings();
    if (globalInputConfigured) {
        inputMode          = globalInputMode;
        keyBindings        = globalKeyBindings;
        controllerBindings = globalControllerBindings;
    } else {
        saveGlobalInputSettings();
    }
}

SegaGenesisEmulation::~SegaGenesisEmulation()
{
    unloadRom();
    cleanupSDL();
}

// ─── ROM loading ───────────────────────────────────────────────────────────────

bool SegaGenesisEmulation::loadRom(const std::string& path)
{
    if (!backend) { std::cerr << "[Genesis] Backend not initialised.\n"; return false; }
    std::ifstream check(path, std::ios::binary);
    if (!check.is_open()) { std::cerr << "[Genesis] Cannot open: " << path << "\n"; return false; }
    check.close();
    if (!backend->loadRom(path)) return false;
    romPath   = path;
    isRunning = true;
    const int w = backend->getFrameWidth();
    const int h = backend->getFrameHeight();
    frameBuffer.resize(static_cast<size_t>(w * h) * 4, 0);
    latestFrame.resize(static_cast<size_t>(w * h) * 4, 0);
    std::cout << "[Genesis] Loaded: " << path << "\n";
    return true;
}

void SegaGenesisEmulation::unloadRom()
{
    stopEmulationWorker();
    isRunning = false;
    romPath.clear();
}

std::vector<unsigned char> SegaGenesisEmulation::dumpSaveData() const
    { return backend ? backend->dumpSaveData() : std::vector<unsigned char>{}; }

bool SegaGenesisEmulation::restoreSaveData(const std::vector<unsigned char>& data)
    { return backend ? backend->restoreSaveData(data) : false; }

// ─── SDL helpers ───────────────────────────────────────────────────────────────

void SegaGenesisEmulation::calculateScale(int sw, int sh, int ew, int eh)
{
    int maxX = (sw - 50) / std::max(ew, 1);
    int maxY = (sh - 50) / std::max(eh, 1);
    int scale = std::max(1, std::min(maxX, maxY));
    scaleX = scale; scaleY = scale;
}

void SegaGenesisEmulation::initSDL(int emuW, int emuH)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "[Genesis] SDL init failed: " << SDL_GetError() << "\n"; return;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    calculateScale(dm.w, dm.h, emuW, emuH);

    window = SDL_CreateWindow("Sega Genesis — Genesis Plus GX",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        emuW * scaleX, emuH * scaleY,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) { std::cerr << "[Genesis] Window failed: " << SDL_GetError() << "\n"; SDL_Quit(); return; }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "[Genesis] Renderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window); window = nullptr; SDL_Quit(); return;
    }

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, emuW, emuH);
    if (!screenTexture) {
        std::cerr << "[Genesis] Texture failed: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer); renderer = nullptr;
        SDL_DestroyWindow(window);    window   = nullptr; SDL_Quit();
    }
}

void SegaGenesisEmulation::cleanupSDL()
{
    if (controller)    { SDL_GameControllerClose(controller); controller = nullptr; controllerDeviceIndex = -1; }
    if (screenTexture) { SDL_DestroyTexture(screenTexture);   screenTexture = nullptr; }
    if (renderer)      { SDL_DestroyRenderer(renderer);       renderer      = nullptr; }
    if (window)        { SDL_DestroyWindow(window);           window        = nullptr; }
    SDL_Quit();
}

// ─── Input bindings ────────────────────────────────────────────────────────────

void SegaGenesisEmulation::setDefaultBindings()
{
    keyBindings = {
        SDLK_UP,     SDLK_DOWN,   SDLK_LEFT,   SDLK_RIGHT,
        SDLK_z,      // A
        SDLK_x,      // B
        SDLK_c,      // C
        SDLK_a,      // X (6-button)
        SDLK_s,      // Y (6-button)
        SDLK_d,      // Z (6-button)
        SDLK_RETURN, // Start
        SDLK_RSHIFT, // Mode
    };
    controllerBindings = {
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_Y,             // A
        SDL_CONTROLLER_BUTTON_B,             // B
        SDL_CONTROLLER_BUTTON_A,             // C
        SDL_CONTROLLER_BUTTON_X,             // X
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  // Y
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, // Z
        SDL_CONTROLLER_BUTTON_START,         // Start
        SDL_CONTROLLER_BUTTON_BACK,          // Mode
    };
}

void SegaGenesisEmulation::saveGlobalInputSettings()
{
    globalInputMode          = inputMode;
    globalKeyBindings        = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured    = true;
}

// ─── Controller helpers ────────────────────────────────────────────────────────

void SegaGenesisEmulation::listControllers() const
{
    const int n = SDL_NumJoysticks();
    if (n <= 0) { std::cout << "No controllers detected.\n"; return; }
    std::cout << "Detected controllers:\n";
    for (int i = 0; i < n; ++i)
        if (SDL_IsGameController(i))
            std::cout << "  [" << i << "] " << (SDL_GameControllerNameForIndex(i) ? SDL_GameControllerNameForIndex(i) : "Unknown") << "\n";
}

bool SegaGenesisEmulation::openControllerByIndex(int index)
{
    if (index < 0 || index >= SDL_NumJoysticks() || !SDL_IsGameController(index)) {
        std::cout << "Invalid controller index.\n"; return false;
    }
    if (controller) { SDL_GameControllerClose(controller); controller = nullptr; }
    controller = SDL_GameControllerOpen(index);
    if (!controller) { std::cout << "Failed to open controller: " << SDL_GetError() << "\n"; controllerDeviceIndex = -1; return false; }
    controllerDeviceIndex = index;
    std::cout << "Using controller: " << (SDL_GameControllerName(controller) ? SDL_GameControllerName(controller) : "Unknown") << "\n";
    return true;
}

// ─── Input menu ────────────────────────────────────────────────────────────────

void SegaGenesisEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== Sega Genesis Input Configuration ===\n"
                  << "1) Keyboard only\n2) Controller only\n3) Keyboard + Controller\n"
                  << "4) Remap keyboard\n5) Select controller\n6) Remap controller\n0) Save\nChoice: ";
        std::string ch; std::getline(std::cin, ch); ch = trimStr(ch);
        if (ch == "0") break;
        if (ch == "1") { inputMode = InputMode::KeyboardOnly;          std::cout << "Keyboard only.\n"; continue; }
        if (ch == "2") { inputMode = InputMode::ControllerOnly;        std::cout << "Controller only.\n"; continue; }
        if (ch == "3") { inputMode = InputMode::KeyboardAndController; std::cout << "Keyboard + Controller.\n"; continue; }
        if (ch == "4") {
            struct KbA { const char* name; SDL_Keycode* key; };
            KbA actions[] = {
                {"UP",    &keyBindings.up},    {"DOWN",  &keyBindings.down},
                {"LEFT",  &keyBindings.left},  {"RIGHT", &keyBindings.right},
                {"A",     &keyBindings.a},     {"B",     &keyBindings.b},
                {"C",     &keyBindings.c},     {"X",     &keyBindings.x},
                {"Y",     &keyBindings.y},     {"Z",     &keyBindings.z},
                {"START", &keyBindings.start}, {"MODE",  &keyBindings.mode},
            };
            for (KbA& a : actions) {
                std::cout << a.name << " [" << SDL_GetKeyName(*a.key) << "]: ";
                std::string kn; std::getline(std::cin, kn); kn = trimStr(kn);
                if (kn.empty()) continue;
                SDL_Keycode k = SDL_GetKeyFromName(kn.c_str());
                if (k == SDLK_UNKNOWN) { std::cout << "  Unknown key.\n"; continue; }
                *a.key = k;
            }
            std::cout << "Keyboard bindings updated.\n"; continue;
        }
        if (ch == "5") {
            listControllers();
            std::cout << "Controller index: ";
            std::string idx; std::getline(std::cin, idx); idx = trimStr(idx);
            if (idx.empty()) continue;
            try { openControllerByIndex(std::stoi(idx)); } catch (...) { std::cout << "Invalid index.\n"; }
            continue;
        }
        if (ch == "6") {
            struct CbA { const char* name; SDL_GameControllerButton* btn; };
            CbA actions[] = {
                {"UP",    &controllerBindings.up},    {"DOWN",  &controllerBindings.down},
                {"LEFT",  &controllerBindings.left},  {"RIGHT", &controllerBindings.right},
                {"A",     &controllerBindings.a},     {"B",     &controllerBindings.b},
                {"C",     &controllerBindings.c},     {"X",     &controllerBindings.x},
                {"Y",     &controllerBindings.y},     {"Z",     &controllerBindings.z},
                {"START", &controllerBindings.start}, {"MODE",  &controllerBindings.mode},
            };
            for (CbA& a : actions) {
                const char* nm = SDL_GameControllerGetStringForButton(*a.btn);
                std::cout << a.name << " [" << (nm ? nm : "?") << "]: ";
                std::string bn; std::getline(std::cin, bn); bn = trimStr(bn);
                if (bn.empty()) continue;
                SDL_GameControllerButton b = parseBtn(bn);
                if (b == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "  Unknown button.\n"; continue; }
                *a.btn = b;
            }
            std::cout << "Controller bindings updated.\n"; continue;
        }
        std::cout << "Unknown option.\n";
    }
    saveGlobalInputSettings();
    std::cout << "Sega Genesis controls saved.\n";
}

// ─── Event handlers ────────────────────────────────────────────────────────────

void SegaGenesisEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, genesis_input_t& input, bool& running)
{
    if (key == SDLK_ESCAPE && pressed) { running = false; return; }
    if (key == keyBindings.up)    input.up    = pressed;
    if (key == keyBindings.down)  input.down  = pressed;
    if (key == keyBindings.left)  input.left  = pressed;
    if (key == keyBindings.right) input.right = pressed;
    if (key == keyBindings.a)     input.a     = pressed;
    if (key == keyBindings.b)     input.b     = pressed;
    if (key == keyBindings.c)     input.c     = pressed;
    if (key == keyBindings.x)     input.x     = pressed;
    if (key == keyBindings.y)     input.y     = pressed;
    if (key == keyBindings.z)     input.z     = pressed;
    if (key == keyBindings.start) input.start = pressed;
    if (key == keyBindings.mode)  input.mode  = pressed;
}

void SegaGenesisEmulation::handleControllerButtonEvent(Uint8 button, bool pressed, genesis_input_t& input, bool& running)
{
    (void)running;
    if (button == static_cast<Uint8>(controllerBindings.up))    input.up    = pressed;
    if (button == static_cast<Uint8>(controllerBindings.down))  input.down  = pressed;
    if (button == static_cast<Uint8>(controllerBindings.left))  input.left  = pressed;
    if (button == static_cast<Uint8>(controllerBindings.right)) input.right = pressed;
    if (button == static_cast<Uint8>(controllerBindings.a))     input.a     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.b))     input.b     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.c))     input.c     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.x))     input.x     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.y))     input.y     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.z))     input.z     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.start)) input.start = pressed;
    if (button == static_cast<Uint8>(controllerBindings.mode))  input.mode  = pressed;
}

// ─── Render ────────────────────────────────────────────────────────────────────

void SegaGenesisEmulation::renderFrame()
{
    if (!renderer || !backend) return;
    const int fw = backend->getFrameWidth();
    const int fh = backend->getFrameHeight();
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!frameReady.load()) return;
        frameBuffer = latestFrame;
    }
    // Recreate texture if frame dimensions changed
    if (screenTexture) {
        int tw = 0, th = 0;
        SDL_QueryTexture(screenTexture, nullptr, nullptr, &tw, &th);
        if (tw != fw || th != fh) {
            SDL_DestroyTexture(screenTexture);
            screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING, fw, fh);
            if (!screenTexture) return;
        }
    }
    if (SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), fw * 4) < 0) return;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    const SDL_Rect dst = {0, 0, winW, winH};
    SDL_RenderCopy(renderer, screenTexture, nullptr, &dst);
    SDL_RenderPresent(renderer);
}

// ─── configureGlobalControls ──────────────────────────────────────────────────

void SegaGenesisEmulation::configureGlobalControls()
{
    SegaGenesisEmulation cfg;
    const bool had = (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0);
    if (!had && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to init controller subsystem: " << SDL_GetError() << "\n"; return;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { cfg.openControllerByIndex(i); break; }
    cfg.configureInputMenu();
    if (!had) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

// ─── runInteractive ────────────────────────────────────────────────────────────

void SegaGenesisEmulation::runInteractive()
{
    if (!backend || !isRunning.load()) { std::cerr << "[Genesis] No ROM loaded.\n"; return; }

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0)
        std::cerr << "[Genesis] Controller init: " << SDL_GetError() << "\n";
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { openControllerByIndex(i); break; }

    const int emuW = backend->getFrameWidth();
    const int emuH = backend->getFrameHeight();
    initSDL(emuW, emuH);
    if (!window || !renderer) { std::cerr << "[Genesis] SDL init failed.\n"; return; }

    if (inputMode == InputMode::ControllerOnly && !controller) {
        std::cout << "No controller; falling back to keyboard.\n";
        inputMode = InputMode::KeyboardOnly;
    }

    std::cout << "\n=== Sega Genesis ===\n"
              << "ROM: " << romPath << "\n"
              << "Press ESC to exit.\n\n";

    genesis_input_t input = {};
    bool running = true;

    if (!startEmulationWorker()) {
        std::cerr << "[Genesis] Failed to start worker.\n";
        cleanupSDL(); return;
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN:
                    if (inputMode != InputMode::ControllerOnly)
                        handleKeyboardEvent(ev.key.keysym.sym, true, input, running);
                    break;
                case SDL_KEYUP:
                    if (inputMode != InputMode::ControllerOnly)
                        handleKeyboardEvent(ev.key.keysym.sym, false, input, running);
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (inputMode != InputMode::KeyboardOnly)
                        handleControllerButtonEvent(ev.cbutton.button, true, input, running);
                    break;
                case SDL_CONTROLLERBUTTONUP:
                    if (inputMode != InputMode::KeyboardOnly)
                        handleControllerButtonEvent(ev.cbutton.button, false, input, running);
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!controller) openControllerByIndex(ev.cdevice.which);
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (controller) {
                        SDL_Joystick* j = SDL_GameControllerGetJoystick(controller);
                        if (j && SDL_JoystickInstanceID(j) == ev.cdevice.which) {
                            SDL_GameControllerClose(controller);
                            controller = nullptr; controllerDeviceIndex = -1;
                            std::cout << "Controller disconnected.\n";
                        }
                    }
                    break;
            }
        }
        { std::lock_guard<std::mutex> lock(inputMutex); workerInput = input; }
        if (emulationThreadFailed.load() || !isRunning.load()) break;
        renderFrame();
        SDL_Delay(1);
    }

    stopEmulationWorker();
    cleanupSDL();
    std::cout << "[Genesis] Emulation stopped.\n";
}

// ─── Worker thread ─────────────────────────────────────────────────────────────

bool SegaGenesisEmulation::startEmulationWorker()
{
    if (emulationThreadRunning.load() || !backend || !isRunning.load()) return false;
    emulationThreadStopRequested = false;
    emulationThreadFailed        = false;
    frameReady                   = false;
    emulationThread = std::thread(&SegaGenesisEmulation::emulationWorkerLoop, this);
    return true;
}

void SegaGenesisEmulation::stopEmulationWorker()
{
    emulationThreadStopRequested = true;
    if (emulationThread.joinable()) emulationThread.join();
}

void SegaGenesisEmulation::emulationWorkerLoop()
{
    emulationThreadRunning = true;

    int fw = backend->getFrameWidth();
    int fh = backend->getFrameHeight();
    std::vector<unsigned char> localFrame(static_cast<size_t>(fw * fh) * 4, 0);

    AudioMasterClock clock;
    clock.configure(backend->getAudioSampleRate(), backend->getNominalFps(), 80);

    while (!emulationThreadStopRequested.load() && isRunning.load()) {
        genesis_input_t snap = {};
        { std::lock_guard<std::mutex> lock(inputMutex); snap = workerInput; }

        backend->setInput(snap);
        if (!backend->nextFrame()) {
            std::cerr << "[Genesis] Frame failed.\n";
            isRunning = false; emulationThreadFailed = true; break;
        }

        const int nw = backend->getFrameWidth();
        const int nh = backend->getFrameHeight();
        if (nw != fw || nh != fh) {
            fw = nw; fh = nh;
            localFrame.resize(static_cast<size_t>(fw * fh) * 4, 0);
        }

        for (int y = 0; y < fh; ++y) {
            for (int x = 0; x < fw; ++x) {
                const genesis_color_t c = backend->getPixel(x, y);
                const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(fw) + static_cast<size_t>(x)) * 4;
                localFrame[off+0] = c.r;
                localFrame[off+1] = c.g;
                localFrame[off+2] = c.b;
                localFrame[off+3] = 255;
            }
        }

        { std::lock_guard<std::mutex> lock(frameMutex); latestFrame = localFrame; frameReady = true; }

        clock.onFrameProduced();
        clock.throttle();
    }

    emulationThreadRunning = false;
}
