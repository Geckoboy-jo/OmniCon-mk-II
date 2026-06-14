#include "NDS.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <chrono>
#include <dlfcn.h>

// ─── Abstract backend ────────────────────────────────────────────────────────

class NDSBackend {
public:
    virtual ~NDSBackend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const nds_input_t& input) = 0;
    virtual nds_color_t getPixel(int x, int y) const = 0;
    virtual int    getFrameWidth()  const { return 256; }
    virtual int    getFrameHeight() const { return 384; }
    virtual double getAspectRatio() const { return 256.0 / 384.0; }
    virtual double getNominalFps()  const { return 59.82; }
    virtual bool   initializeAudio() { return true; }
    virtual std::vector<unsigned char> dumpSaveData() const { return {}; }
    virtual bool restoreSaveData(const std::vector<unsigned char>&) { return false; }
};

// ─── Unavailable stub ────────────────────────────────────────────────────────

class NDSUnavailableBackend final : public NDSBackend {
public:
    bool loadRom(const std::string&) override {
        std::cerr << "[NDS] DeSmuME libretro core is not available.\n";
        std::cerr << "      Install with: sudo apt-get install libretro-desmume\n";
        std::cerr << "      Or set OMNICON_NDS_CORE to the path of desmume_libretro.so\n";
        return false;
    }
    bool nextFrame() override { return false; }
    void setInput(const nds_input_t&) override {}
    nds_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }
};

// ─── libretro helpers ────────────────────────────────────────────────────────

namespace libretroNDS {
    static constexpr unsigned ENV_SET_PIXEL_FORMAT    = 10;
    static constexpr unsigned ENV_GET_VARIABLE        = 15;
    static constexpr unsigned ENV_SET_VARIABLES       = 16;
    static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
    static constexpr unsigned ENV_SET_GEOMETRY        = 37;
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

    struct game_info    { const char* path; const void* data; size_t size; const char* meta; };
    struct game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
    struct variable     { const char* key; const char* value; };
    struct av_info {
        game_geometry geometry;
        struct { double fps; double sample_rate; } timing;
    };
    using environment_t        = bool (*)(unsigned, void*);
    using video_refresh_t      = void (*)(const void*, unsigned, unsigned, size_t);
    using audio_sample_t       = void (*)(int16_t, int16_t);
    using audio_sample_batch_t = size_t (*)(const int16_t*, size_t);
    using input_poll_t         = void (*)(void);
    using input_state_t        = int16_t (*)(unsigned, unsigned, unsigned, unsigned);
}

// ─── Libretro backend ────────────────────────────────────────────────────────

class LibretroNDSBackend final : public NDSBackend {
public:
    static constexpr const char* kSystemCorePath    = "/usr/lib/x86_64-linux-gnu/libretro/desmume_libretro.so";
    static constexpr const char* kWorkspaceCorePath = "third_party/libretro/desmume_libretro.so";

    LibretroNDSBackend();
    ~LibretroNDSBackend() override;

    bool isAvailable() const { return coreHandle != nullptr; }
    bool loadRom(const std::string& path) override;
    bool nextFrame() override;
    void setInput(const nds_input_t& input) override;
    nds_color_t getPixel(int x, int y) const override;
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
    void   (*fn_init)()                                          = nullptr;
    void   (*fn_deinit)()                                        = nullptr;
    void   (*fn_set_environment)(libretroNDS::environment_t)     = nullptr;
    void   (*fn_set_video_refresh)(libretroNDS::video_refresh_t) = nullptr;
    void   (*fn_set_audio_sample)(libretroNDS::audio_sample_t)   = nullptr;
    void   (*fn_set_audio_sample_batch)(libretroNDS::audio_sample_batch_t) = nullptr;
    void   (*fn_set_input_poll)(libretroNDS::input_poll_t)       = nullptr;
    void   (*fn_set_input_state)(libretroNDS::input_state_t)     = nullptr;
    void   (*fn_get_system_av_info)(libretroNDS::av_info*)       = nullptr;
    bool   (*fn_load_game)(const libretroNDS::game_info*)        = nullptr;
    void   (*fn_unload_game)()                                   = nullptr;
    void   (*fn_run)()                                           = nullptr;
    size_t (*fn_serialize_size)()                                = nullptr;
    bool   (*fn_serialize)(void*, size_t)                        = nullptr;
    bool   (*fn_unserialize)(const void*, size_t)                = nullptr;

    std::vector<unsigned char> framePixels;
    std::vector<unsigned char> romData;
    std::string loadedRomPath;
    std::string systemDirectory;
    std::string saveDirectory;
    std::string contentDirectory;
    nds_input_t currentInput{};
    bool gameLoaded = false;
    unsigned pixelFormat = libretroNDS::PIXEL_0RGB1555;
    int    frameWidth   = 256;
    int    frameHeight  = 384;
    double aspectRatio  = 256.0 / 384.0;
    double nominalFps   = 59.82;
    SDL_AudioDeviceID audioDevice = 0;
    int audioSampleRate  = 48000;
    int coreSampleRate   = 48000;
    size_t maxQueuedAudioBytes = 0;

    static LibretroNDSBackend* s_instance;
    static bool    s_envCb(unsigned cmd, void* data);
    static void    s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    s_audioCb(int16_t left, int16_t right);
    static size_t  s_audioBatchCb(const int16_t* data, size_t frames);
    static void    s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
};

LibretroNDSBackend* LibretroNDSBackend::s_instance = nullptr;

std::string LibretroNDSBackend::resolveCorePath()
{
    const char* envPath = std::getenv("OMNICON_NDS_CORE");
    if (envPath && *envPath && std::filesystem::exists(envPath)) return envPath;
    if (std::filesystem::exists(kSystemCorePath))    return kSystemCorePath;
    if (std::filesystem::exists(kWorkspaceCorePath)) return kWorkspaceCorePath;
    return "";
}

LibretroNDSBackend::LibretroNDSBackend()
    : corePath(resolveCorePath())
{
    if (corePath.empty()) return;

    std::cout << "[NDS] Using core: " << corePath << "\n";

    coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[NDS] Failed to load core " << corePath << ": " << dlerror() << "\n";
        return;
    }
    if (!loadSymbols()) { dlclose(coreHandle); coreHandle = nullptr; return; }

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

LibretroNDSBackend::~LibretroNDSBackend()
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

bool LibretroNDSBackend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) std::cerr << "[NDS] Missing symbol: " << name << "\n";
        return sym;
    };
    fn_init                   = reinterpret_cast<void(*)()>(load("retro_init"));
    fn_deinit                 = reinterpret_cast<void(*)()>(load("retro_deinit"));
    fn_set_environment        = reinterpret_cast<void(*)(libretroNDS::environment_t)>(load("retro_set_environment"));
    fn_set_video_refresh      = reinterpret_cast<void(*)(libretroNDS::video_refresh_t)>(load("retro_set_video_refresh"));
    fn_set_audio_sample       = reinterpret_cast<void(*)(libretroNDS::audio_sample_t)>(load("retro_set_audio_sample"));
    fn_set_audio_sample_batch = reinterpret_cast<void(*)(libretroNDS::audio_sample_batch_t)>(load("retro_set_audio_sample_batch"));
    fn_set_input_poll         = reinterpret_cast<void(*)(libretroNDS::input_poll_t)>(load("retro_set_input_poll"));
    fn_set_input_state        = reinterpret_cast<void(*)(libretroNDS::input_state_t)>(load("retro_set_input_state"));
    fn_get_system_av_info     = reinterpret_cast<void(*)(libretroNDS::av_info*)>(load("retro_get_system_av_info"));
    fn_load_game              = reinterpret_cast<bool(*)(const libretroNDS::game_info*)>(load("retro_load_game"));
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

void LibretroNDSBackend::ensureLibretroDirectories()
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

bool LibretroNDSBackend::ensureAudioDevice(int sampleRate)
{
    if (audioDevice != 0 && audioSampleRate == sampleRate) return true;
    closeAudioDevice();
    SDL_AudioSpec desired{}, obtained{};
    desired.freq     = sampleRate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 2048;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) { std::cerr << "[NDS] Audio open failed: " << SDL_GetError() << "\n"; return false; }
    audioSampleRate      = obtained.freq > 0 ? obtained.freq : sampleRate;
    maxQueuedAudioBytes  = static_cast<size_t>(audioSampleRate * obtained.channels * sizeof(int16_t) * 20 / 100);
    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void LibretroNDSBackend::closeAudioDevice()
{
    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
}

void LibretroNDSBackend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0 || audioDevice == 0) return;
    const size_t bytes = frames * 2 * sizeof(int16_t);
    // Drop frames silently when the queue is full — avoids the pop/click that
    // SDL_ClearQueuedAudio causes by discarding already-buffered audio.
    if (SDL_GetQueuedAudioSize(audioDevice) > maxQueuedAudioBytes) return;
    SDL_QueueAudio(audioDevice, data, static_cast<Uint32>(bytes));
}

bool LibretroNDSBackend::initializeAudio()
{
    return ensureAudioDevice(coreSampleRate);
}

bool LibretroNDSBackend::loadRom(const std::string& path)
{
    if (!coreHandle) return false;
    ensureLibretroDirectories();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { std::cerr << "[NDS] Cannot open ROM: " << path << "\n"; return false; }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    romData.assign(size, 0);
    if (!file.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
        std::cerr << "[NDS] Failed to read ROM: " << path << "\n";
        romData.clear(); return false;
    }

    if (gameLoaded) { fn_unload_game(); gameLoaded = false; }

    loadedRomPath = path;
    const std::filesystem::path parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(parentDir, ec)) contentDirectory = parentDir.string();
    }

    libretroNDS::game_info info{ loadedRomPath.c_str(), romData.data(), romData.size(), nullptr };
    if (!fn_load_game(&info)) { std::cerr << "[NDS] Core rejected ROM: " << path << "\n"; return false; }
    gameLoaded = true;

    libretroNDS::av_info av{};
    fn_get_system_av_info(&av);
    if (av.geometry.base_width  > 0) frameWidth  = static_cast<int>(av.geometry.base_width);
    if (av.geometry.base_height > 0) frameHeight = static_cast<int>(av.geometry.base_height);
    if (av.geometry.aspect_ratio > 0.0f)
        aspectRatio = av.geometry.aspect_ratio;
    else if (frameWidth > 0 && frameHeight > 0)
        aspectRatio = static_cast<double>(frameWidth) / frameHeight;
    if (av.timing.fps > 1.0)             nominalFps  = av.timing.fps;
    if (av.timing.sample_rate > 1000.0)  coreSampleRate = static_cast<int>(av.timing.sample_rate);
    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    return true;
}

bool LibretroNDSBackend::nextFrame()
{
    if (!coreHandle || !gameLoaded) return false;
    fn_run();
    return true;
}

void LibretroNDSBackend::setInput(const nds_input_t& input) { currentInput = input; }

nds_color_t LibretroNDSBackend::getPixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) return {0, 0, 0, 255};
    const size_t idx = static_cast<size_t>(y * frameWidth + x) * 4;
    if (idx + 3 >= framePixels.size()) return {0, 0, 0, 255};
    return { framePixels[idx], framePixels[idx+1], framePixels[idx+2], framePixels[idx+3] };
}

bool LibretroNDSBackend::onEnvironment(unsigned cmd, void* data)
{
    if (cmd == 3)  { *static_cast<bool*>(data) = true; return true; }
    if (cmd == 9)  { *static_cast<const char**>(data) = systemDirectory.c_str(); return true; }
    if (cmd == 30) { *static_cast<const char**>(data) = contentDirectory.c_str(); return true; }
    if (cmd == 31) { *static_cast<const char**>(data) = saveDirectory.c_str(); return true; }
    if (cmd == 18) { return true; }
    if (cmd == libretroNDS::ENV_SET_VARIABLES)       { return true; }
    if (cmd == libretroNDS::ENV_GET_VARIABLE_UPDATE) { if (data) *static_cast<bool*>(data) = false; return true; }
    if (cmd == libretroNDS::ENV_GET_VARIABLE) {
        auto* v = static_cast<libretroNDS::variable*>(data);
        if (!v || !v->key) return false;
        v->value = nullptr;
        return false;
    }
    if (cmd == libretroNDS::ENV_SET_PIXEL_FORMAT && data) {
        pixelFormat = *reinterpret_cast<unsigned*>(data);
        return pixelFormat == libretroNDS::PIXEL_0RGB1555
            || pixelFormat == libretroNDS::PIXEL_XRGB8888
            || pixelFormat == libretroNDS::PIXEL_RGB565;
    }
    if (cmd == libretroNDS::ENV_SET_GEOMETRY && data) {
        const auto* g = reinterpret_cast<const libretroNDS::game_geometry*>(data);
        if (g->base_width  > 0) frameWidth  = static_cast<int>(g->base_width);
        if (g->base_height > 0) frameHeight = static_cast<int>(g->base_height);
        if (g->aspect_ratio > 0.0f) aspectRatio = g->aspect_ratio;
        framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
        return true;
    }
    return false;
}

void LibretroNDSBackend::onVideoRefresh(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0) return;
    frameWidth  = static_cast<int>(w);
    frameHeight = static_cast<int>(h);
    framePixels.resize(static_cast<size_t>(w * h) * 4, 0);

    for (unsigned y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint8_t*>(data) + y * pitch;
        for (unsigned x = 0; x < w; ++x) {
            const size_t out = static_cast<size_t>(y * w + x) * 4;
            if (pixelFormat == libretroNDS::PIXEL_XRGB8888) {
                const uint32_t p = reinterpret_cast<const uint32_t*>(row)[x];
                framePixels[out+0] = static_cast<uint8_t>((p >> 16) & 0xFF);
                framePixels[out+1] = static_cast<uint8_t>((p >>  8) & 0xFF);
                framePixels[out+2] = static_cast<uint8_t>( p        & 0xFF);
                framePixels[out+3] = 255;
            } else if (pixelFormat == libretroNDS::PIXEL_RGB565) {
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

bool   LibretroNDSBackend::s_envCb(unsigned cmd, void* data) { return s_instance ? s_instance->onEnvironment(cmd, data) : false; }
void   LibretroNDSBackend::s_videoCb(const void* d, unsigned w, unsigned h, size_t p) { if (s_instance) s_instance->onVideoRefresh(d,w,h,p); }
void   LibretroNDSBackend::s_audioCb(int16_t l, int16_t r) { if (s_instance) { const int16_t s[2]={l,r}; s_instance->queueAudioFrames(s,1); } }
size_t LibretroNDSBackend::s_audioBatchCb(const int16_t* d, size_t f) { if (s_instance && d && f) s_instance->queueAudioFrames(d,f); return f; }
void   LibretroNDSBackend::s_inputPollCb() {}
int16_t LibretroNDSBackend::s_inputStateCb(unsigned port, unsigned device, unsigned /*index*/, unsigned id)
{
    if (!s_instance || port != 0) return 0;
    const nds_input_t& in = s_instance->currentInput;

    if (device == libretroNDS::DEVICE_POINTER) {
        switch (id) {
            case libretroNDS::POINTER_X:       return in.touchPressed ? in.touchX : 0;
            case libretroNDS::POINTER_Y:       return in.touchPressed ? in.touchY : 0;
            case libretroNDS::POINTER_PRESSED: return in.touchPressed ? 1 : 0;
            case libretroNDS::POINTER_COUNT:   return in.touchPressed ? 1 : 0;
            default: return 0;
        }
    }

    if (device != libretroNDS::DEVICE_JOYPAD) return 0;
    switch (id) {
        case libretroNDS::JOYPAD_A:      return in.a      ? 1 : 0;
        case libretroNDS::JOYPAD_B:      return in.b      ? 1 : 0;
        case libretroNDS::JOYPAD_X:      return in.x      ? 1 : 0;
        case libretroNDS::JOYPAD_Y:      return in.y      ? 1 : 0;
        case libretroNDS::JOYPAD_START:  return in.start  ? 1 : 0;
        case libretroNDS::JOYPAD_SELECT: return in.select ? 1 : 0;
        case libretroNDS::JOYPAD_UP:     return in.up     ? 1 : 0;
        case libretroNDS::JOYPAD_DOWN:   return in.down   ? 1 : 0;
        case libretroNDS::JOYPAD_LEFT:   return in.left   ? 1 : 0;
        case libretroNDS::JOYPAD_RIGHT:  return in.right  ? 1 : 0;
        case libretroNDS::JOYPAD_L:      return in.l      ? 1 : 0;
        case libretroNDS::JOYPAD_R:      return in.r      ? 1 : 0;
        default: return 0;
    }
}

// ─── Local helpers ────────────────────────────────────────────────────────────

namespace {

std::string trimNDS(const std::string& value)
{
    const std::string ws = " \t\r\n";
    const std::size_t s = value.find_first_not_of(ws);
    if (s == std::string::npos) return "";
    return value.substr(s, value.find_last_not_of(ws) - s + 1);
}

std::string toUpperNDS(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

SDL_GameControllerButton parseControllerButtonNDS(const std::string& input)
{
    const std::string n = toUpperNDS(trimNDS(input));
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
    return SDL_CONTROLLER_BUTTON_INVALID;
}

const char* controllerButtonNameNDS(SDL_GameControllerButton b)
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
        default:                                 return "UNKNOWN";
    }
}

} // namespace

// ─── Static members ───────────────────────────────────────────────────────────

NDSEmulation::InputMode         NDSEmulation::globalInputMode       = NDSEmulation::InputMode::KeyboardAndController;
NDSEmulation::KeyBindings       NDSEmulation::globalKeyBindings      = {};
NDSEmulation::ControllerBindings NDSEmulation::globalControllerBindings = {};
bool                            NDSEmulation::globalInputConfigured  = false;

// ─── NDSEmulation ─────────────────────────────────────────────────────────────

NDSEmulation::NDSEmulation()
    : romLoaded(false), isRunning(false),
      window(nullptr), renderer(nullptr), screenTexture(nullptr), controller(nullptr),
      lastLetterboxRect{0, 0, 512, 768}
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

NDSEmulation::~NDSEmulation()
{
    unloadRom();
    cleanupSDL();
}

void NDSEmulation::setDefaultBindings()
{
    inputMode = InputMode::KeyboardAndController;

    keyBindings.up     = SDLK_UP;
    keyBindings.down   = SDLK_DOWN;
    keyBindings.left   = SDLK_LEFT;
    keyBindings.right  = SDLK_RIGHT;
    keyBindings.a      = SDLK_x;       // NDS A = X key
    keyBindings.b      = SDLK_z;       // NDS B = Z key
    keyBindings.x      = SDLK_s;       // NDS X = S key
    keyBindings.y      = SDLK_a;       // NDS Y = A key
    keyBindings.start  = SDLK_RETURN;
    keyBindings.select = SDLK_RSHIFT;
    keyBindings.l      = SDLK_q;
    keyBindings.r      = SDLK_w;

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
}

void NDSEmulation::saveGlobalInputSettings()
{
    globalInputMode        = inputMode;
    globalKeyBindings      = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured  = true;
}

void NDSEmulation::initSDL()
{
    if (window && renderer && screenTexture) return;

    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
            return;
        }
    }

    // NDS native combined screen: 256×384 (top 256×192 + bottom 256×192)
    const int nativeWidth  = 256;
    const int nativeHeight = 384;

    window = SDL_CreateWindow("OmniCon Nintendo DS",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              512, 768,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
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

void NDSEmulation::cleanupSDL()
{
    if (controller)    { SDL_GameControllerClose(controller); controller   = nullptr; }
    if (screenTexture) { SDL_DestroyTexture(screenTexture);   screenTexture = nullptr; }
    if (renderer)      { SDL_DestroyRenderer(renderer);       renderer     = nullptr; }
    if (window)        { SDL_DestroyWindow(window);           window       = nullptr; }
}

void NDSEmulation::renderFrameSDL()
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
            const nds_color_t px = backendPtr->getPixel(x, y);
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
    const double targetAspect  = backendPtr->getAspectRatio();
    const double windowAspect  = outH > 0 ? static_cast<double>(outW) / outH : targetAspect;
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

void NDSEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== Nintendo DS Input Configuration ===\n";
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
        choice = trimNDS(choice);

        if (choice == "0") break;

        if (choice == "1") { inputMode = InputMode::KeyboardOnly;         std::cout << "Keyboard only.\n";           continue; }
        if (choice == "2") { inputMode = InputMode::ControllerOnly;       std::cout << "Controller only.\n";         continue; }
        if (choice == "3") { inputMode = InputMode::KeyboardAndController; std::cout << "Keyboard + Controller.\n";  continue; }

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
            };
            std::cout << "Enter SDL key names (e.g. Up, Z, X, Return). Press Enter to keep.\n";
            for (KA& a : actions) {
                std::cout << a.name << " [" << SDL_GetKeyName(*a.key) << "]: ";
                std::string keyName; std::getline(std::cin, keyName); keyName = trimNDS(keyName);
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
            std::string idx; std::getline(std::cin, idx); idx = trimNDS(idx);
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
            };
            std::cout << "Enter button names (A,B,X,Y,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT,LEFTSHOULDER,RIGHTSHOULDER).\n";
            std::cout << "Press Enter to keep current binding.\n";
            for (CA& a : actions) {
                std::cout << a.name << " [" << controllerButtonNameNDS(*a.button) << "]: ";
                std::string btnName; std::getline(std::cin, btnName); btnName = trimNDS(btnName);
                if (btnName.empty()) continue;
                SDL_GameControllerButton parsed = parseControllerButtonNDS(btnName);
                if (parsed == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "  Invalid button, keeping existing.\n"; continue; }
                *a.button = parsed;
            }
            std::cout << "Controller bindings updated.\n";
            continue;
        }

        std::cout << "Unknown option.\n";
    }

    saveGlobalInputSettings();
    std::cout << "Nintendo DS controls saved.\n";
}

void NDSEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, nds_input_t& input, bool& running)
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
}

void NDSEmulation::handleControllerButtonEvent(Uint8 button, bool pressed, nds_input_t& input, bool& running)
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
}

bool NDSEmulation::openControllerByIndex(int index)
{
    if (controller) { SDL_GameControllerClose(controller); controller = nullptr; }
    const int count = SDL_NumJoysticks();
    if (count <= 0 || index < 0 || index >= count || !SDL_IsGameController(index)) return false;
    controller = SDL_GameControllerOpen(index);
    return controller != nullptr;
}

void NDSEmulation::listControllers() const
{
    const int count = SDL_NumJoysticks();
    std::cout << "Controllers: " << count << "\n";
    for (int i = 0; i < count; ++i) {
        std::cout << "  [" << i << "] "
                  << (SDL_IsGameController(i) ? "game controller" : "joystick")
                  << " - " << SDL_JoystickNameForIndex(i) << "\n";
    }
}

std::vector<unsigned char> LibretroNDSBackend::dumpSaveData() const
{
    if (!coreHandle || !gameLoaded || !fn_serialize_size || !fn_serialize) return {};
    const size_t sz = fn_serialize_size();
    if (sz == 0) return {};
    std::vector<unsigned char> state(sz, 0);
    if (!fn_serialize(state.data(), sz)) return {};
    return state;
}

bool LibretroNDSBackend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) return false;
    return fn_unserialize(data.data(), data.size());
}

std::vector<unsigned char> NDSEmulation::dumpSaveData() const
{
    if (!backendPtr || !isLoaded()) return {};
    return backendPtr->dumpSaveData();
}

bool NDSEmulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!backendPtr || !isLoaded()) return false;
    return backendPtr->restoreSaveData(data);
}

bool NDSEmulation::loadRom(const string& path)
{
    unloadRom();

    std::filesystem::path fsPath(path);
    std::string ext = fsPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext != ".nds") {
        std::cerr << "[NDS] Invalid file extension. Nintendo DS ROMs use .nds\n";
        return false;
    }

    auto libretroBackend = std::make_unique<LibretroNDSBackend>();
    if (libretroBackend->isAvailable()) {
        backendPtr = std::move(libretroBackend);
    } else {
        backendPtr = std::make_unique<NDSUnavailableBackend>();
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

void NDSEmulation::unloadRom()
{
    isRunning = false;
    romLoaded = false;
    backendPtr.reset();
    romPath.clear();
}

void NDSEmulation::runInteractive()
{
    if (!romLoaded || !backendPtr) {
        std::cerr << "[NDS] No Nintendo DS ROM loaded.\n";
        return;
    }

    initSDL();
    if (!window || !renderer || !screenTexture) {
        std::cerr << "[NDS] Failed to initialize SDL.\n";
        return;
    }

    if (!backendPtr->initializeAudio()) {
        std::cerr << "[NDS] Audio initialization failed.\n";
    }

    std::cout << "Nintendo DS: " << romPath << "\n";
    std::cout << "Press Escape or close the window to exit.\n";
    std::cout << "Default keys: Arrows=D-pad, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter=Start, RShift=Select\n";

    bool running = true;
    nds_input_t input{};

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

void NDSEmulation::configureGlobalControls()
{
    NDSEmulation tmp;
    tmp.configureInputMenu();
    tmp.saveGlobalInputSettings();
}

void NDSEmulation::handleMouseEvent(int winX, int winY, bool pressed, nds_input_t& input)
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
