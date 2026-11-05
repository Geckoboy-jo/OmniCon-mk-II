#include "PS2.hpp"
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
#include <sys/wait.h>
#include <unistd.h>

// ─── Abstract backend ────────────────────────────────────────────────────────

class PS2Backend {
public:
    virtual ~PS2Backend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const ps2_input_t& input) = 0;
    virtual ps2_color_t getPixel(int x, int y) const = 0;
    virtual int    getFrameWidth()  const { return 640; }
    virtual int    getFrameHeight() const { return 448; }
    virtual double getAspectRatio() const { return 4.0 / 3.0; }
    virtual double getNominalFps()  const { return 60.0; }
    virtual bool   initializeAudio() { return true; }
    virtual bool   isStandalone()    { return false; }
    virtual std::vector<unsigned char> dumpSaveData() const { return {}; }
    virtual bool restoreSaveData(const std::vector<unsigned char>&) { return false; }
};

// ─── Unavailable stub ────────────────────────────────────────────────────────

class PS2UnavailableBackend final : public PS2Backend {
public:
    bool loadRom(const std::string&) override {
        std::cerr << "[PS2] lrps2/PCSX2 libretro core is not available.\n";
        std::cerr << "      Install with: sudo apt-get install libretro-pcsx2\n";
        std::cerr << "      Or place lrps2_libretro.so in third_party/libretro/\n";
        std::cerr << "      Or set OMNICON_PS2_CORE to the path of the core .so\n";
        return false;
    }
    bool nextFrame() override { return false; }
    void setInput(const ps2_input_t&) override {}
    ps2_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }
};

// ─── Standalone PCSX2 backend ───────────────────────────────────────────────

class StandalonePS2Backend final : public PS2Backend {
public:
    static constexpr const char* kSnapBin  = "/snap/bin/pcsx2-emulator";
    static constexpr const char* kLocalBin = "/usr/bin/pcsx2";
    static constexpr const char* kLocalBin2 = "/usr/local/bin/pcsx2";

    static std::string resolveBinary() {
        if (std::filesystem::exists(kSnapBin))   return kSnapBin;
        if (std::filesystem::exists(kLocalBin))  return kLocalBin;
        if (std::filesystem::exists(kLocalBin2)) return kLocalBin2;
        return "";
    }

    bool isAvailable() const { return !binary_.empty(); }
    bool isStandalone() override { return true; }

    StandalonePS2Backend() : binary_(resolveBinary()) {}

    bool loadRom(const std::string& path) override {
        if (binary_.empty()) return false;
        std::cout << "[PS2] Launching PCSX2: " << binary_ << " \"" << path << "\"\n";
        pid_ = fork();
        if (pid_ < 0) {
            std::cerr << "[PS2] fork() failed\n";
            return false;
        }
        if (pid_ == 0) {
            // child
            execlp(binary_.c_str(), binary_.c_str(), path.c_str(), nullptr);
            std::cerr << "[PS2] exec failed\n";
            _exit(1);
        }
        return true;
    }

    bool nextFrame() override {
        // Check whether PCSX2 is still running.
        int status = 0;
        int r = waitpid(pid_, &status, WNOHANG);
        return (r == 0);  // 0 = still running
    }

    void waitForExit() {
        if (pid_ > 0) { waitpid(pid_, nullptr, 0); pid_ = -1; }
    }

    void setInput(const ps2_input_t&) override {}
    ps2_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }

private:
    std::string binary_;
    pid_t pid_ = -1;
};

// ─── libretro helpers ────────────────────────────────────────────────────────

namespace libretroPS2 {
    static constexpr unsigned ENV_SET_PIXEL_FORMAT    = 10;
    static constexpr unsigned ENV_GET_VARIABLE        = 15;
    static constexpr unsigned ENV_SET_VARIABLES       = 16;
    static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
    static constexpr unsigned ENV_SET_GEOMETRY        = 37;
    static constexpr unsigned PIXEL_0RGB1555 = 0;
    static constexpr unsigned PIXEL_XRGB8888 = 1;
    static constexpr unsigned PIXEL_RGB565   = 2;
    static constexpr unsigned DEVICE_JOYPAD  = 1;
    static constexpr unsigned JOYPAD_B       = 0;   // Cross
    static constexpr unsigned JOYPAD_Y       = 1;   // Square
    static constexpr unsigned JOYPAD_SELECT  = 2;
    static constexpr unsigned JOYPAD_START   = 3;
    static constexpr unsigned JOYPAD_UP      = 4;
    static constexpr unsigned JOYPAD_DOWN    = 5;
    static constexpr unsigned JOYPAD_LEFT    = 6;
    static constexpr unsigned JOYPAD_RIGHT   = 7;
    static constexpr unsigned JOYPAD_A       = 8;   // Circle
    static constexpr unsigned JOYPAD_X       = 9;   // Triangle
    static constexpr unsigned JOYPAD_L       = 10;  // L1
    static constexpr unsigned JOYPAD_R       = 11;  // R1
    static constexpr unsigned JOYPAD_L2      = 12;
    static constexpr unsigned JOYPAD_R2      = 13;
    static constexpr unsigned JOYPAD_L3      = 14;
    static constexpr unsigned JOYPAD_R3      = 15;

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

class LibretroPS2Backend final : public PS2Backend {
public:
    // Standard install paths for PS2 libretro cores
    static constexpr const char* kCorePath1 = "/usr/lib/x86_64-linux-gnu/libretro/lrps2_libretro.so";
    static constexpr const char* kCorePath2 = "/usr/lib/x86_64-linux-gnu/libretro/pcsx2_libretro.so";
    static constexpr const char* kWorkspacePath1 = "third_party/libretro/lrps2_libretro.so";
    static constexpr const char* kWorkspacePath2 = "third_party/libretro/pcsx2_libretro.so";

    LibretroPS2Backend();
    ~LibretroPS2Backend() override;

    bool isAvailable() const { return coreHandle != nullptr; }
    bool loadRom(const std::string& path) override;
    bool nextFrame() override;
    void setInput(const ps2_input_t& input) override;
    ps2_color_t getPixel(int x, int y) const override;
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
    void   (*fn_init)()                                           = nullptr;
    void   (*fn_deinit)()                                         = nullptr;
    void   (*fn_set_environment)(libretroPS2::environment_t)      = nullptr;
    void   (*fn_set_video_refresh)(libretroPS2::video_refresh_t)  = nullptr;
    void   (*fn_set_audio_sample)(libretroPS2::audio_sample_t)    = nullptr;
    void   (*fn_set_audio_sample_batch)(libretroPS2::audio_sample_batch_t) = nullptr;
    void   (*fn_set_input_poll)(libretroPS2::input_poll_t)        = nullptr;
    void   (*fn_set_input_state)(libretroPS2::input_state_t)      = nullptr;
    void   (*fn_get_system_av_info)(libretroPS2::av_info*)        = nullptr;
    bool   (*fn_load_game)(const libretroPS2::game_info*)         = nullptr;
    void   (*fn_unload_game)()                                    = nullptr;
    void   (*fn_run)()                                            = nullptr;
    size_t (*fn_serialize_size)()                                 = nullptr;
    bool   (*fn_serialize)(void*, size_t)                         = nullptr;
    bool   (*fn_unserialize)(const void*, size_t)                 = nullptr;

    std::vector<unsigned char> framePixels;
    std::string loadedRomPath;
    std::string systemDirectory;
    std::string saveDirectory;
    std::string contentDirectory;
    ps2_input_t currentInput{};
    bool gameLoaded = false;
    unsigned pixelFormat = libretroPS2::PIXEL_0RGB1555;
    int    frameWidth   = 640;
    int    frameHeight  = 448;
    double aspectRatio  = 4.0 / 3.0;
    double nominalFps   = 60.0;
    SDL_AudioDeviceID audioDevice = 0;
    int audioSampleRate  = 44100;
    int coreSampleRate   = 44100;
    size_t maxQueuedAudioBytes = 0;

    static LibretroPS2Backend* s_instance;
    static bool    s_envCb(unsigned cmd, void* data);
    static void    s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    s_audioCb(int16_t left, int16_t right);
    static size_t  s_audioBatchCb(const int16_t* data, size_t frames);
    static void    s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
};

LibretroPS2Backend* LibretroPS2Backend::s_instance = nullptr;

std::string LibretroPS2Backend::resolveCorePath()
{
    const char* envPath = std::getenv("OMNICON_PS2_CORE");
    if (envPath && *envPath && std::filesystem::exists(envPath)) return envPath;
    if (std::filesystem::exists(kWorkspacePath1)) return kWorkspacePath1;
    if (std::filesystem::exists(kWorkspacePath2)) return kWorkspacePath2;
    if (std::filesystem::exists(kCorePath1))      return kCorePath1;
    if (std::filesystem::exists(kCorePath2))      return kCorePath2;
    return "";
}

LibretroPS2Backend::LibretroPS2Backend()
    : corePath(resolveCorePath())
{
    if (corePath.empty()) return;

    coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[PS2] Failed to load core " << corePath << ": " << dlerror() << "\n";
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

LibretroPS2Backend::~LibretroPS2Backend()
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

bool LibretroPS2Backend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) std::cerr << "[PS2] Missing symbol: " << name << "\n";
        return sym;
    };
    fn_init                   = reinterpret_cast<void(*)()>(load("retro_init"));
    fn_deinit                 = reinterpret_cast<void(*)()>(load("retro_deinit"));
    fn_set_environment        = reinterpret_cast<void(*)(libretroPS2::environment_t)>(load("retro_set_environment"));
    fn_set_video_refresh      = reinterpret_cast<void(*)(libretroPS2::video_refresh_t)>(load("retro_set_video_refresh"));
    fn_set_audio_sample       = reinterpret_cast<void(*)(libretroPS2::audio_sample_t)>(load("retro_set_audio_sample"));
    fn_set_audio_sample_batch = reinterpret_cast<void(*)(libretroPS2::audio_sample_batch_t)>(load("retro_set_audio_sample_batch"));
    fn_set_input_poll         = reinterpret_cast<void(*)(libretroPS2::input_poll_t)>(load("retro_set_input_poll"));
    fn_set_input_state        = reinterpret_cast<void(*)(libretroPS2::input_state_t)>(load("retro_set_input_state"));
    fn_get_system_av_info     = reinterpret_cast<void(*)(libretroPS2::av_info*)>(load("retro_get_system_av_info"));
    fn_load_game              = reinterpret_cast<bool(*)(const libretroPS2::game_info*)>(load("retro_load_game"));
    fn_unload_game            = reinterpret_cast<void(*)()>(load("retro_unload_game"));
    fn_run                    = reinterpret_cast<void(*)()>(load("retro_run"));
    // Optional savestates
    fn_serialize_size = reinterpret_cast<size_t(*)()>(dlsym(coreHandle, "retro_serialize_size"));
    fn_serialize      = reinterpret_cast<bool(*)(void*,size_t)>(dlsym(coreHandle, "retro_serialize"));
    fn_unserialize    = reinterpret_cast<bool(*)(const void*,size_t)>(dlsym(coreHandle, "retro_unserialize"));
    return fn_init && fn_deinit && fn_set_environment && fn_set_video_refresh
        && fn_set_audio_sample && fn_set_audio_sample_batch
        && fn_set_input_poll && fn_set_input_state
        && fn_get_system_av_info && fn_load_game && fn_unload_game && fn_run;
}

void LibretroPS2Backend::ensureLibretroDirectories()
{
    const std::filesystem::path root    = std::filesystem::current_path() / "third_party" / "libretro";
    const std::filesystem::path bundledSysDir = root / "system";
    const std::filesystem::path miscBiosDir = std::filesystem::current_path() / "Misc BIOS";
    const std::filesystem::path sysDir =
        std::filesystem::is_directory(miscBiosDir) ? miscBiosDir : bundledSysDir;
    const std::filesystem::path saveDir = root / "save";
    const std::filesystem::path contDir = root / "content";
    std::error_code ec;
    std::filesystem::create_directories(bundledSysDir, ec);
    std::filesystem::create_directories(saveDir, ec);
    std::filesystem::create_directories(contDir, ec);
    // Also ensure ps2 saves directory exists
    std::filesystem::create_directories(std::filesystem::current_path() / "saves" / "ps2", ec);
    systemDirectory  = sysDir.string();
    saveDirectory    = saveDir.string();
    contentDirectory = contDir.string();
}

bool LibretroPS2Backend::ensureAudioDevice(int sampleRate)
{
    if (audioDevice != 0 && audioSampleRate == sampleRate) return true;
    closeAudioDevice();
    SDL_AudioSpec desired{}, obtained{};
    desired.freq     = sampleRate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 2048;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) { std::cerr << "[PS2] Audio open failed: " << SDL_GetError() << "\n"; return false; }
    audioSampleRate     = obtained.freq > 0 ? obtained.freq : sampleRate;
    maxQueuedAudioBytes = static_cast<size_t>(audioSampleRate * obtained.channels * sizeof(int16_t) * 20 / 100);
    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void LibretroPS2Backend::closeAudioDevice()
{
    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
}

void LibretroPS2Backend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0 || audioDevice == 0) return;
    const size_t bytes = frames * 2 * sizeof(int16_t);
    if (SDL_GetQueuedAudioSize(audioDevice) > maxQueuedAudioBytes) return;
    SDL_QueueAudio(audioDevice, data, static_cast<Uint32>(bytes));
}

bool LibretroPS2Backend::initializeAudio()
{
    return ensureAudioDevice(coreSampleRate);
}

bool LibretroPS2Backend::loadRom(const std::string& path)
{
    if (!coreHandle) return false;
    ensureLibretroDirectories();

    // PS2 disc images must be loaded by path — do not copy into memory
    loadedRomPath = std::filesystem::absolute(path).string();

    const std::filesystem::path parentDir = std::filesystem::path(path).parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(parentDir, ec))
            contentDirectory = std::filesystem::absolute(parentDir).string();
    }

    if (gameLoaded) { fn_unload_game(); gameLoaded = false; }

    // Pass path + NULL data; PS2 cores require need_fullpath
    libretroPS2::game_info info{ loadedRomPath.c_str(), nullptr, 0, nullptr };
    if (!fn_load_game(&info)) {
        std::cerr << "[PS2] Core rejected ROM: " << path << "\n";
        std::cerr << "      Ensure PS2 BIOS files are in "
                  << systemDirectory << "\n";
        return false;
    }
    gameLoaded = true;

    libretroPS2::av_info av{};
    fn_get_system_av_info(&av);
    if (av.geometry.base_width  > 0) frameWidth  = static_cast<int>(av.geometry.base_width);
    if (av.geometry.base_height > 0) frameHeight = static_cast<int>(av.geometry.base_height);
    if (av.geometry.aspect_ratio > 0.0f)
        aspectRatio = av.geometry.aspect_ratio;
    else if (frameWidth > 0 && frameHeight > 0)
        aspectRatio = static_cast<double>(frameWidth) / frameHeight;
    if (av.timing.fps > 1.0)            nominalFps     = av.timing.fps;
    if (av.timing.sample_rate > 1000.0) coreSampleRate = static_cast<int>(av.timing.sample_rate);
    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    return true;
}

bool LibretroPS2Backend::nextFrame()
{
    if (!coreHandle || !gameLoaded) return false;
    fn_run();
    return true;
}

void LibretroPS2Backend::setInput(const ps2_input_t& input) { currentInput = input; }

ps2_color_t LibretroPS2Backend::getPixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) return {0, 0, 0, 255};
    const size_t idx = static_cast<size_t>(y * frameWidth + x) * 4;
    if (idx + 3 >= framePixels.size()) return {0, 0, 0, 255};
    return { framePixels[idx], framePixels[idx+1], framePixels[idx+2], framePixels[idx+3] };
}

bool LibretroPS2Backend::onEnvironment(unsigned cmd, void* data)
{
    if (cmd == 3)  { *static_cast<bool*>(data) = true; return true; }  // GET_OVERSCAN
    if (cmd == 9)  { *static_cast<const char**>(data) = systemDirectory.c_str(); return true; }   // GET_SYSTEM_DIRECTORY
    if (cmd == 30) { *static_cast<const char**>(data) = contentDirectory.c_str(); return true; }  // GET_CONTENT_DIRECTORY
    if (cmd == 31) { *static_cast<const char**>(data) = saveDirectory.c_str(); return true; }     // GET_SAVE_DIRECTORY
    if (cmd == 18) { return true; }   // SET_SUPPORT_NO_GAME
    if (cmd == libretroPS2::ENV_SET_VARIABLES)       { return true; }
    if (cmd == libretroPS2::ENV_GET_VARIABLE_UPDATE) { if (data) *static_cast<bool*>(data) = false; return true; }
    if (cmd == libretroPS2::ENV_GET_VARIABLE) {
        auto* v = static_cast<libretroPS2::variable*>(data);
        if (!v || !v->key) return false;
        v->value = nullptr;
        return false;
    }
    if (cmd == libretroPS2::ENV_SET_PIXEL_FORMAT && data) {
        pixelFormat = *reinterpret_cast<unsigned*>(data);
        return pixelFormat == libretroPS2::PIXEL_0RGB1555
            || pixelFormat == libretroPS2::PIXEL_XRGB8888
            || pixelFormat == libretroPS2::PIXEL_RGB565;
    }
    if (cmd == libretroPS2::ENV_SET_GEOMETRY && data) {
        const auto* g = reinterpret_cast<const libretroPS2::game_geometry*>(data);
        if (g->base_width  > 0) frameWidth  = static_cast<int>(g->base_width);
        if (g->base_height > 0) frameHeight = static_cast<int>(g->base_height);
        if (g->aspect_ratio > 0.0f) aspectRatio = g->aspect_ratio;
        framePixels.assign(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
        return true;
    }
    return false;
}

void LibretroPS2Backend::onVideoRefresh(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0) return;
    frameWidth  = static_cast<int>(w);
    frameHeight = static_cast<int>(h);
    framePixels.resize(static_cast<size_t>(w * h) * 4, 0);

    for (unsigned y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint8_t*>(data) + y * pitch;
        for (unsigned x = 0; x < w; ++x) {
            const size_t out = static_cast<size_t>(y * w + x) * 4;
            if (pixelFormat == libretroPS2::PIXEL_XRGB8888) {
                const uint32_t p = reinterpret_cast<const uint32_t*>(row)[x];
                framePixels[out+0] = static_cast<uint8_t>((p >> 16) & 0xFF);
                framePixels[out+1] = static_cast<uint8_t>((p >>  8) & 0xFF);
                framePixels[out+2] = static_cast<uint8_t>( p        & 0xFF);
                framePixels[out+3] = 255;
            } else if (pixelFormat == libretroPS2::PIXEL_RGB565) {
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

bool   LibretroPS2Backend::s_envCb(unsigned cmd, void* data) { return s_instance ? s_instance->onEnvironment(cmd, data) : false; }
void   LibretroPS2Backend::s_videoCb(const void* d, unsigned w, unsigned h, size_t p) { if (s_instance) s_instance->onVideoRefresh(d,w,h,p); }
void   LibretroPS2Backend::s_audioCb(int16_t l, int16_t r) { if (s_instance) { const int16_t s[2]={l,r}; s_instance->queueAudioFrames(s,1); } }
size_t LibretroPS2Backend::s_audioBatchCb(const int16_t* d, size_t f) { if (s_instance && d && f) s_instance->queueAudioFrames(d,f); return f; }
void   LibretroPS2Backend::s_inputPollCb() {}
int16_t LibretroPS2Backend::s_inputStateCb(unsigned port, unsigned device, unsigned /*index*/, unsigned id)
{
    if (!s_instance || port != 0) return 0;
    if (device != libretroPS2::DEVICE_JOYPAD) return 0;
    const ps2_input_t& in = s_instance->currentInput;
    switch (id) {
        case libretroPS2::JOYPAD_B:      return in.cross    ? 1 : 0;
        case libretroPS2::JOYPAD_A:      return in.circle   ? 1 : 0;
        case libretroPS2::JOYPAD_Y:      return in.square   ? 1 : 0;
        case libretroPS2::JOYPAD_X:      return in.triangle ? 1 : 0;
        case libretroPS2::JOYPAD_START:  return in.start    ? 1 : 0;
        case libretroPS2::JOYPAD_SELECT: return in.select   ? 1 : 0;
        case libretroPS2::JOYPAD_UP:     return in.up       ? 1 : 0;
        case libretroPS2::JOYPAD_DOWN:   return in.down     ? 1 : 0;
        case libretroPS2::JOYPAD_LEFT:   return in.left     ? 1 : 0;
        case libretroPS2::JOYPAD_RIGHT:  return in.right    ? 1 : 0;
        case libretroPS2::JOYPAD_L:      return in.l1       ? 1 : 0;
        case libretroPS2::JOYPAD_R:      return in.r1       ? 1 : 0;
        case libretroPS2::JOYPAD_L2:     return in.l2       ? 1 : 0;
        case libretroPS2::JOYPAD_R2:     return in.r2       ? 1 : 0;
        case libretroPS2::JOYPAD_L3:     return in.l3       ? 1 : 0;
        case libretroPS2::JOYPAD_R3:     return in.r3       ? 1 : 0;
        default: return 0;
    }
}

std::vector<unsigned char> LibretroPS2Backend::dumpSaveData() const
{
    if (!coreHandle || !gameLoaded || !fn_serialize_size || !fn_serialize) return {};
    const size_t sz = fn_serialize_size();
    if (sz == 0) return {};
    std::vector<unsigned char> state(sz, 0);
    if (!fn_serialize(state.data(), sz)) return {};
    return state;
}

bool LibretroPS2Backend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) return false;
    return fn_unserialize(data.data(), data.size());
}

// ─── Local helpers ────────────────────────────────────────────────────────────

namespace {

std::string trimPS2(const std::string& value)
{
    const std::string ws = " \t\r\n";
    const std::size_t s = value.find_first_not_of(ws);
    if (s == std::string::npos) return "";
    return value.substr(s, value.find_last_not_of(ws) - s + 1);
}

std::string toUpperPS2(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

SDL_GameControllerButton parseControllerButtonPS2(const std::string& input)
{
    const std::string n = toUpperPS2(trimPS2(input));
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

const char* controllerButtonNamePS2(SDL_GameControllerButton b)
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

PS2Emulation::InputMode         PS2Emulation::globalInputMode          = PS2Emulation::InputMode::KeyboardAndController;
PS2Emulation::KeyBindings       PS2Emulation::globalKeyBindings         = {};
PS2Emulation::ControllerBindings PS2Emulation::globalControllerBindings = {};
bool                            PS2Emulation::globalInputConfigured     = false;

// ─── PS2Emulation ─────────────────────────────────────────────────────────────

PS2Emulation::PS2Emulation()
    : romLoaded(false), isRunning(false),
      window(nullptr), renderer(nullptr), screenTexture(nullptr), controller(nullptr),
      lastLetterboxRect{0, 0, 640, 480}
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

PS2Emulation::~PS2Emulation()
{
    unloadRom();
    cleanupSDL();
}

void PS2Emulation::setDefaultBindings()
{
    inputMode = InputMode::KeyboardAndController;

    // Arrows = D-pad, Z=Cross, X=Circle, A=Square, S=Triangle
    // Enter=Start, RShift=Select, Q=L1, W=R1, E=L2, R=R2
    keyBindings.up       = SDLK_UP;
    keyBindings.down     = SDLK_DOWN;
    keyBindings.left     = SDLK_LEFT;
    keyBindings.right    = SDLK_RIGHT;
    keyBindings.cross    = SDLK_z;
    keyBindings.circle   = SDLK_x;
    keyBindings.square   = SDLK_a;
    keyBindings.triangle = SDLK_s;
    keyBindings.start    = SDLK_RETURN;
    keyBindings.select   = SDLK_RSHIFT;
    keyBindings.l1       = SDLK_q;
    keyBindings.r1       = SDLK_w;
    keyBindings.l2       = SDLK_e;
    keyBindings.r2       = SDLK_r;

    controllerBindings.up       = SDL_CONTROLLER_BUTTON_DPAD_UP;
    controllerBindings.down     = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    controllerBindings.left     = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    controllerBindings.right    = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    controllerBindings.cross    = SDL_CONTROLLER_BUTTON_B;
    controllerBindings.circle   = SDL_CONTROLLER_BUTTON_A;
    controllerBindings.square   = SDL_CONTROLLER_BUTTON_X;
    controllerBindings.triangle = SDL_CONTROLLER_BUTTON_Y;
    controllerBindings.start    = SDL_CONTROLLER_BUTTON_START;
    controllerBindings.select   = SDL_CONTROLLER_BUTTON_BACK;
    controllerBindings.l1       = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    controllerBindings.r1       = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
}

void PS2Emulation::saveGlobalInputSettings()
{
    globalInputMode          = inputMode;
    globalKeyBindings        = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured    = true;
}

void PS2Emulation::initSDL()
{
    if (window && renderer && screenTexture) return;

    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
            return;
        }
    }

    const int nativeWidth  = 640;
    const int nativeHeight = 448;

    window = SDL_CreateWindow("OmniCon PlayStation 2",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              640, 480,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) { std::cerr << "SDL window failed: " << SDL_GetError() << "\n"; return; }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { std::cerr << "SDL renderer failed: " << SDL_GetError() << "\n"; return; }

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING, nativeWidth, nativeHeight);
    if (!screenTexture) { std::cerr << "SDL texture failed: " << SDL_GetError() << "\n"; return; }

    openControllerByIndex(0);
}

void PS2Emulation::cleanupSDL()
{
    if (controller)    { SDL_GameControllerClose(controller); controller    = nullptr; }
    if (screenTexture) { SDL_DestroyTexture(screenTexture);   screenTexture = nullptr; }
    if (renderer)      { SDL_DestroyRenderer(renderer);       renderer      = nullptr; }
    if (window)        { SDL_DestroyWindow(window);           window        = nullptr; }
}

void PS2Emulation::renderFrameSDL()
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
    }

    frameBuffer.resize(static_cast<size_t>(w * h) * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const ps2_color_t px = backendPtr->getPixel(x, y);
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

void PS2Emulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== PlayStation 2 Input Configuration ===\n";
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
        choice = trimPS2(choice);

        if (choice == "0") break;

        if (choice == "1") { inputMode = InputMode::KeyboardOnly;         std::cout << "Keyboard only.\n";          continue; }
        if (choice == "2") { inputMode = InputMode::ControllerOnly;       std::cout << "Controller only.\n";        continue; }
        if (choice == "3") { inputMode = InputMode::KeyboardAndController; std::cout << "Keyboard + Controller.\n"; continue; }

        if (choice == "4") {
            struct KA { const char* name; SDL_Keycode* key; };
            KA actions[] = {
                {"UP",       &keyBindings.up},
                {"DOWN",     &keyBindings.down},
                {"LEFT",     &keyBindings.left},
                {"RIGHT",    &keyBindings.right},
                {"CROSS",    &keyBindings.cross},
                {"CIRCLE",   &keyBindings.circle},
                {"SQUARE",   &keyBindings.square},
                {"TRIANGLE", &keyBindings.triangle},
                {"START",    &keyBindings.start},
                {"SELECT",   &keyBindings.select},
                {"L1",       &keyBindings.l1},
                {"R1",       &keyBindings.r1},
                {"L2",       &keyBindings.l2},
                {"R2",       &keyBindings.r2},
            };
            std::cout << "Enter SDL key names (e.g. Up, Z, X, Return). Press Enter to keep.\n";
            for (KA& a : actions) {
                std::cout << a.name << " [" << SDL_GetKeyName(*a.key) << "]: ";
                std::string keyName; std::getline(std::cin, keyName); keyName = trimPS2(keyName);
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
            std::string idx; std::getline(std::cin, idx); idx = trimPS2(idx);
            if (idx.empty()) continue;
            try { openControllerByIndex(std::stoi(idx)); }
            catch (...) { std::cout << "Invalid controller index.\n"; }
            continue;
        }

        if (choice == "6") {
            struct CA { const char* name; SDL_GameControllerButton* button; };
            CA actions[] = {
                {"UP",       &controllerBindings.up},
                {"DOWN",     &controllerBindings.down},
                {"LEFT",     &controllerBindings.left},
                {"RIGHT",    &controllerBindings.right},
                {"CROSS",    &controllerBindings.cross},
                {"CIRCLE",   &controllerBindings.circle},
                {"SQUARE",   &controllerBindings.square},
                {"TRIANGLE", &controllerBindings.triangle},
                {"START",    &controllerBindings.start},
                {"SELECT",   &controllerBindings.select},
                {"L1",       &controllerBindings.l1},
                {"R1",       &controllerBindings.r1},
            };
            std::cout << "Enter button names (A,B,X,Y,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT,LEFTSHOULDER,RIGHTSHOULDER).\n";
            std::cout << "Press Enter to keep current binding.\n";
            for (CA& a : actions) {
                std::cout << a.name << " [" << controllerButtonNamePS2(*a.button) << "]: ";
                std::string btnName; std::getline(std::cin, btnName); btnName = trimPS2(btnName);
                if (btnName.empty()) continue;
                SDL_GameControllerButton parsed = parseControllerButtonPS2(btnName);
                if (parsed == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "  Invalid button, keeping existing.\n"; continue; }
                *a.button = parsed;
            }
            std::cout << "Controller bindings updated.\n";
            continue;
        }

        std::cout << "Unknown option.\n";
    }

    saveGlobalInputSettings();
    std::cout << "PlayStation 2 controls saved.\n";
}

void PS2Emulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, ps2_input_t& input, bool& running)
{
    if (key == SDLK_ESCAPE && pressed) { running = false; return; }
    if      (key == keyBindings.up)       input.up       = pressed;
    else if (key == keyBindings.down)     input.down     = pressed;
    else if (key == keyBindings.left)     input.left     = pressed;
    else if (key == keyBindings.right)    input.right    = pressed;
    else if (key == keyBindings.cross)    input.cross    = pressed;
    else if (key == keyBindings.circle)   input.circle   = pressed;
    else if (key == keyBindings.square)   input.square   = pressed;
    else if (key == keyBindings.triangle) input.triangle = pressed;
    else if (key == keyBindings.start)    input.start    = pressed;
    else if (key == keyBindings.select)   input.select   = pressed;
    else if (key == keyBindings.l1)       input.l1       = pressed;
    else if (key == keyBindings.r1)       input.r1       = pressed;
    else if (key == keyBindings.l2)       input.l2       = pressed;
    else if (key == keyBindings.r2)       input.r2       = pressed;
}

void PS2Emulation::handleControllerButtonEvent(Uint8 button, bool pressed, ps2_input_t& input, bool& running)
{
    const auto b = static_cast<SDL_GameControllerButton>(button);
    if (b == SDL_CONTROLLER_BUTTON_BACK && pressed) { running = false; return; }
    if      (b == controllerBindings.up)       input.up       = pressed;
    else if (b == controllerBindings.down)     input.down     = pressed;
    else if (b == controllerBindings.left)     input.left     = pressed;
    else if (b == controllerBindings.right)    input.right    = pressed;
    else if (b == controllerBindings.cross)    input.cross    = pressed;
    else if (b == controllerBindings.circle)   input.circle   = pressed;
    else if (b == controllerBindings.square)   input.square   = pressed;
    else if (b == controllerBindings.triangle) input.triangle = pressed;
    else if (b == controllerBindings.start)    input.start    = pressed;
    else if (b == controllerBindings.select)   input.select   = pressed;
    else if (b == controllerBindings.l1)       input.l1       = pressed;
    else if (b == controllerBindings.r1)       input.r1       = pressed;
}

bool PS2Emulation::openControllerByIndex(int index)
{
    if (controller) { SDL_GameControllerClose(controller); controller = nullptr; }
    const int count = SDL_NumJoysticks();
    if (count <= 0 || index < 0 || index >= count || !SDL_IsGameController(index)) return false;
    controller = SDL_GameControllerOpen(index);
    return controller != nullptr;
}

void PS2Emulation::listControllers() const
{
    const int count = SDL_NumJoysticks();
    std::cout << "Controllers: " << count << "\n";
    for (int i = 0; i < count; ++i) {
        std::cout << "  [" << i << "] "
                  << (SDL_IsGameController(i) ? "game controller" : "joystick")
                  << " - " << SDL_JoystickNameForIndex(i) << "\n";
    }
}

bool PS2Emulation::loadRom(const string& path)
{
    unloadRom();

    std::filesystem::path fsPath(path);
    std::string ext = fsPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    static const char* const kValidExts[] = {
        ".bin", ".cue", ".iso", ".img", ".mdf", ".chd", ".gz", ".cso", nullptr
    };
    bool validExt = false;
    for (int i = 0; kValidExts[i]; ++i) {
        if (ext == kValidExts[i]) { validExt = true; break; }
    }
    if (!validExt) {
        std::cerr << "[PS2] Unsupported extension '" << ext << "'.\n";
        std::cerr << "      PlayStation 2 ROMs use: .bin/.cue, .iso, .chd, .gz, .cso\n";
        return false;
    }

    auto libretroBackend = std::make_unique<LibretroPS2Backend>();
    if (libretroBackend->isAvailable()) {
        backendPtr = std::move(libretroBackend);
    } else {
        auto standaloneBackend = std::make_unique<StandalonePS2Backend>();
        if (standaloneBackend->isAvailable()) {
            backendPtr = std::move(standaloneBackend);
        } else {
            backendPtr = std::make_unique<PS2UnavailableBackend>();
        }
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

void PS2Emulation::unloadRom()
{
    isRunning = false;
    romLoaded = false;
    backendPtr.reset();
    romPath.clear();
}

void PS2Emulation::runInteractive()
{
    if (!romLoaded || !backendPtr) {
        std::cerr << "[PS2] No PlayStation 2 ROM loaded.\n";
        return;
    }

    initSDL();
    if (!window || !renderer || !screenTexture) {
        std::cerr << "[PS2] Failed to initialize SDL.\n";
        return;
    }

    if (!backendPtr->initializeAudio()) {
        std::cerr << "[PS2] Audio initialization failed.\n";
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { openControllerByIndex(i); break; }
    if (controller && inputMode == InputMode::KeyboardOnly)
        inputMode = InputMode::KeyboardAndController;

    std::cout << "PlayStation 2: " << romPath << "\n";
    std::cout << "Press Escape or close the window to exit.\n";
    std::cout << "Default keys: Arrows=D-pad, Z=Cross, X=Circle, A=Square, S=Triangle\n";
    std::cout << "              Enter=Start, RShift=Select, Q=L1, W=R1, E=L2, R=R2\n";
    std::cout << "Note: PS2 BIOS files must be in third_party/libretro/system/\n";

    // Standalone backends (e.g. PCSX2 snap) manage their own window.
    // Just wait for the external process to exit.
    if (backendPtr->isStandalone()) {
        auto* sa = static_cast<StandalonePS2Backend*>(backendPtr.get());
        sa->waitForExit();
        isRunning = false;
        return;
    }

    bool running = true;
    ps2_input_t input{};

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
                if (controller && inputMode == InputMode::KeyboardOnly)
                    inputMode = InputMode::KeyboardAndController;
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller != nullptr) {
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                    SDL_GameControllerClose(controller); controller = nullptr;
                }
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

void PS2Emulation::configureGlobalControls()
{
    PS2Emulation tmp;
    tmp.configureInputMenu();
    tmp.saveGlobalInputSettings();
}

std::vector<unsigned char> PS2Emulation::dumpSaveData() const
{
    if (!backendPtr || !isLoaded()) return {};
    return backendPtr->dumpSaveData();
}

bool PS2Emulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!backendPtr || !isLoaded()) return false;
    return backendPtr->restoreSaveData(data);
}
