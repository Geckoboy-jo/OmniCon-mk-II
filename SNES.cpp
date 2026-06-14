#include "SNES.hpp"
#include "audio_clock.hpp"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <memory>
#include <dlfcn.h>

namespace {

std::string trim(const std::string& value)
{
    const std::string whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string toUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

SDL_GameControllerButton parseControllerButtonName(const std::string& input)
{
    const std::string name = toUpper(trim(input));
    if (name == "A") return SDL_CONTROLLER_BUTTON_A;
    if (name == "B") return SDL_CONTROLLER_BUTTON_B;
    if (name == "X") return SDL_CONTROLLER_BUTTON_X;
    if (name == "Y") return SDL_CONTROLLER_BUTTON_Y;
    if (name == "BACK") return SDL_CONTROLLER_BUTTON_BACK;
    if (name == "START") return SDL_CONTROLLER_BUTTON_START;
    if (name == "LEFTSHOULDER") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (name == "RIGHTSHOULDER") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (name == "DPAD_UP") return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (name == "DPAD_DOWN") return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (name == "DPAD_LEFT") return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (name == "DPAD_RIGHT") return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

const char* controllerButtonName(SDL_GameControllerButton button)
{
    const char* name = SDL_GameControllerGetStringForButton(button);
    return name ? name : "unknown";
}

} // namespace

class SuperPyBackend {
public:
    virtual ~SuperPyBackend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const superpy_input_t& input) = 0;
    virtual std::vector<unsigned char> dumpSaveData() const = 0;
    virtual bool restoreSaveData(const std::vector<unsigned char>& data) = 0;
    virtual superpy_color_t getPixel(int x, int y) const = 0;
    virtual int getAudioSampleRate() const { return 48000; }
    virtual double getNominalFps() const { return 60.0; }
};

class SuperPyUnavailableBackend final : public SuperPyBackend {
public:
    bool loadRom(const std::string& path) override
    {
        (void)path;
        std::cerr << "SuperPy API is not available in this build. SNES emulation is disabled.\n";
        std::cerr << "Add SuperPy SDK bindings and replace SuperPyUnavailableBackend in SNES.cpp.\n";
        return false;
    }

    bool nextFrame() override
    {
        return false;
    }

    void setInput(const superpy_input_t& input) override
    {
        (void)input;
    }

    std::vector<unsigned char> dumpSaveData() const override
    {
        return {};
    }

    bool restoreSaveData(const std::vector<unsigned char>& data) override
    {
        (void)data;
        return false;
    }

    superpy_color_t getPixel(int x, int y) const override
    {
        (void)x;
        (void)y;
        return {0, 0, 0, 255};
    }
};

// ============================================================
// Libretro SNES9x backend — loaded dynamically from .so
// ============================================================

namespace libretro {
    static constexpr unsigned ENV_GET_CAN_DUPE        = 3;
    static constexpr unsigned ENV_SET_PIXEL_FORMAT    = 10;
    static constexpr unsigned ENV_GET_VARIABLE        = 15;
    static constexpr unsigned ENV_SET_VARIABLES       = 16;
    static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
    static constexpr unsigned ENV_SET_SUPPORT_NO_GAME = 18;
    static constexpr unsigned ENV_GET_LOG_INTERFACE   = 27;
    static constexpr unsigned ENV_SET_GEOMETRY        = 37;

    static constexpr unsigned PIXEL_0RGB1555 = 0;
    static constexpr unsigned PIXEL_XRGB8888 = 1;
    static constexpr unsigned PIXEL_RGB565   = 2;

    static constexpr unsigned DEVICE_JOYPAD  = 1;
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

    struct game_info {
        const char* path;
        const void* data;
        size_t      size;
        const char* meta;
    };
    struct system_info {
        const char* library_name;
        const char* library_version;
        const char* valid_extensions;
        bool        need_fullpath;
        bool        block_extract;
    };
    struct game_geometry {
        unsigned base_width;
        unsigned base_height;
        unsigned max_width;
        unsigned max_height;
        float    aspect_ratio;
    };
    struct system_timing { double fps; double sample_rate; };
    struct av_info { game_geometry geometry; system_timing timing; };

    using environment_t        = bool    (*)(unsigned cmd, void* data);
    using video_refresh_t      = void    (*)(const void* data, unsigned width, unsigned height, size_t pitch);
    using audio_sample_t       = void    (*)(int16_t left, int16_t right);
    using audio_sample_batch_t = size_t  (*)(const int16_t* data, size_t frames);
    using input_poll_t         = void    (*)(void);
    using input_state_t        = int16_t (*)(unsigned port, unsigned device, unsigned index, unsigned id);
    using serialize_size_t     = size_t  (*)(void);
    using serialize_t          = bool    (*)(void* data, size_t size);
    using unserialize_t        = bool    (*)(const void* data, size_t size);
} // namespace libretro

class LibretroSnes9xBackend final : public SuperPyBackend {
public:
    static std::string resolveCorePath() {
        const char* env = std::getenv("OMNICON_SNES_CORE");
        if (env && *env && std::filesystem::exists(env)) return env;
        const char* candidates[] = {
            "third_party/libretro/snes9x_libretro.so",
            "/usr/lib/x86_64-linux-gnu/libretro/snes9x_libretro.so",
            "/usr/lib/libretro/snes9x_libretro.so",
        };
        for (const char* c : candidates)
            if (std::filesystem::exists(c)) return c;
        return {};
    }

    LibretroSnes9xBackend();
    ~LibretroSnes9xBackend() override;

    bool isAvailable() const { return coreHandle != nullptr; }

    bool            loadRom(const std::string& path) override;
    bool            nextFrame() override;
    void            setInput(const superpy_input_t& input) override;
    superpy_color_t getPixel(int x, int y) const override;
    int getAudioSampleRate() const override { return audioSampleRate; }
    double getNominalFps() const override { return coreFps; }

    bool onEnvironment(unsigned cmd, void* data);
    void onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);
    std::vector<unsigned char> dumpSaveData() const override;
    bool restoreSaveData(const std::vector<unsigned char>& data) override;

private:
    bool loadSymbols();
    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioFrames(const int16_t* data, size_t frames);

    void* coreHandle = nullptr;

    void    (*fn_init)()                                              = nullptr;
    void    (*fn_deinit)()                                            = nullptr;
    void    (*fn_get_system_info)(libretro::system_info*)             = nullptr;
    void    (*fn_get_system_av_info)(libretro::av_info*)              = nullptr;
    void    (*fn_set_environment)(libretro::environment_t)            = nullptr;
    void    (*fn_set_video_refresh)(libretro::video_refresh_t)        = nullptr;
    void    (*fn_set_audio_sample)(libretro::audio_sample_t)          = nullptr;
    void    (*fn_set_audio_sample_batch)(libretro::audio_sample_batch_t) = nullptr;
    void    (*fn_set_input_poll)(libretro::input_poll_t)              = nullptr;
    void    (*fn_set_input_state)(libretro::input_state_t)            = nullptr;
    size_t  (*fn_serialize_size)()                                    = nullptr;
    bool    (*fn_serialize)(void*, size_t)                            = nullptr;
    bool    (*fn_unserialize)(const void*, size_t)                    = nullptr;
    bool    (*fn_load_game)(const libretro::game_info*)               = nullptr;
    void    (*fn_unload_game)()                                       = nullptr;
    void    (*fn_run)()                                               = nullptr;

    std::vector<uint8_t> framePixels;
    int frameWidth  = 256;
    int frameHeight = 224;

    superpy_input_t  currentInput = {};
    unsigned         pixelFormat  = libretro::PIXEL_0RGB1555;
    bool             gameLoaded   = false;
    std::vector<uint8_t> romData;
    SDL_AudioDeviceID audioDevice = 0;
    SDL_AudioStream* audioStream = nullptr;
    int coreSampleRate = 48000;
    int audioSampleRate = 48000;
    double coreFps = 60.0;
    std::vector<uint8_t> audioConvertBuffer;
    bool audioDeviceFailed = false;
    std::string corePath;

    static LibretroSnes9xBackend* s_instance;
    static bool    s_envCb(unsigned cmd, void* data);
    static void    s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    s_audioCb(int16_t l, int16_t r);
    static size_t  s_audioBatchCb(const int16_t* data, size_t frames);
    static void    s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
};

LibretroSnes9xBackend* LibretroSnes9xBackend::s_instance = nullptr;

LibretroSnes9xBackend::LibretroSnes9xBackend()
{
    corePath = resolveCorePath();
    if (corePath.empty()) {
        std::cerr << "[SNES] No snes9x libretro core found.\n";
        std::cerr << "       Place snes9x_libretro.so in third_party/libretro/\n";
        return;
    }
    coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[SNES] Failed to load core: " << corePath << ": " << dlerror() << "\n";
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

LibretroSnes9xBackend::~LibretroSnes9xBackend()
{
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

bool LibretroSnes9xBackend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) std::cerr << "[SNES] Missing symbol: " << name << "\n";
        return sym;
    };
    fn_init               = reinterpret_cast<void(*)()>(load("retro_init"));
    fn_deinit             = reinterpret_cast<void(*)()>(load("retro_deinit"));
    fn_get_system_info    = reinterpret_cast<void(*)(libretro::system_info*)>(load("retro_get_system_info"));
    fn_get_system_av_info = reinterpret_cast<void(*)(libretro::av_info*)>(load("retro_get_system_av_info"));
    fn_set_environment    = reinterpret_cast<void(*)(libretro::environment_t)>(load("retro_set_environment"));
    fn_set_video_refresh  = reinterpret_cast<void(*)(libretro::video_refresh_t)>(load("retro_set_video_refresh"));
    fn_set_audio_sample   = reinterpret_cast<void(*)(libretro::audio_sample_t)>(load("retro_set_audio_sample"));
    fn_set_audio_sample_batch = reinterpret_cast<void(*)(libretro::audio_sample_batch_t)>(load("retro_set_audio_sample_batch"));
    fn_set_input_poll     = reinterpret_cast<void(*)(libretro::input_poll_t)>(load("retro_set_input_poll"));
    fn_set_input_state    = reinterpret_cast<void(*)(libretro::input_state_t)>(load("retro_set_input_state"));
        fn_serialize_size     = reinterpret_cast<size_t(*)()>(load("retro_serialize_size"));
        fn_serialize          = reinterpret_cast<bool(*)(void*, size_t)>(load("retro_serialize"));
        fn_unserialize        = reinterpret_cast<bool(*)(const void*, size_t)>(load("retro_unserialize"));
    fn_load_game          = reinterpret_cast<bool(*)(const libretro::game_info*)>(load("retro_load_game"));
    fn_unload_game        = reinterpret_cast<void(*)()>(load("retro_unload_game"));
    fn_run                = reinterpret_cast<void(*)()>(load("retro_run"));
    return fn_init && fn_deinit && fn_get_system_info && fn_get_system_av_info &&
           fn_set_environment && fn_set_video_refresh && fn_set_audio_sample &&
            fn_set_audio_sample_batch && fn_set_input_poll && fn_set_input_state &&
            fn_serialize_size && fn_serialize && fn_unserialize &&
           fn_load_game && fn_unload_game && fn_run;
}

bool LibretroSnes9xBackend::onEnvironment(unsigned cmd, void* data)
{
    switch (cmd) {
        case libretro::ENV_GET_CAN_DUPE:
            *static_cast<bool*>(data) = true;
            return true;
        case libretro::ENV_SET_PIXEL_FORMAT:
            pixelFormat = *static_cast<unsigned*>(data);
            return true;
        case libretro::ENV_GET_VARIABLE:
            return false;
        case libretro::ENV_SET_VARIABLES:
            return true;
        case libretro::ENV_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;
        case libretro::ENV_SET_SUPPORT_NO_GAME:
            return true;
        case libretro::ENV_GET_LOG_INTERFACE:
            return false;
        case libretro::ENV_SET_GEOMETRY: {
            const auto* geom = static_cast<const libretro::game_geometry*>(data);
            frameWidth  = static_cast<int>(geom->base_width);
            frameHeight = static_cast<int>(geom->base_height);
            framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
            return true;
        }
        default:
            return false;
    }
}

void LibretroSnes9xBackend::onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    if (static_cast<int>(width) != frameWidth || static_cast<int>(height) != frameHeight) {
        frameWidth  = static_cast<int>(width);
        frameHeight = static_cast<int>(height);
        framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    }
    const auto* src = static_cast<const uint8_t*>(data);
    for (unsigned row = 0; row < height; ++row) {
        const uint8_t* srcRow = src + row * pitch;
        uint8_t*       dstRow = framePixels.data() + row * static_cast<size_t>(frameWidth) * 4;
        if (pixelFormat == libretro::PIXEL_XRGB8888) {
            for (unsigned col = 0; col < width; ++col) {
                uint32_t px;
                std::memcpy(&px, srcRow + col * 4, 4);
                dstRow[col * 4 + 0] = static_cast<uint8_t>((px >> 16) & 0xFF);
                dstRow[col * 4 + 1] = static_cast<uint8_t>((px >>  8) & 0xFF);
                dstRow[col * 4 + 2] = static_cast<uint8_t>( px        & 0xFF);
                dstRow[col * 4 + 3] = 255;
            }
        } else if (pixelFormat == libretro::PIXEL_RGB565) {
            for (unsigned col = 0; col < width; ++col) {
                uint16_t px;
                std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[col * 4 + 0] = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
                dstRow[col * 4 + 1] = static_cast<uint8_t>(((px >>  5) & 0x3F) * 255 / 63);
                dstRow[col * 4 + 2] = static_cast<uint8_t>(( px        & 0x1F) * 255 / 31);
                dstRow[col * 4 + 3] = 255;
            }
        } else {
            // PIXEL_0RGB1555: bits 14-10=R, 9-5=G, 4-0=B
            for (unsigned col = 0; col < width; ++col) {
                uint16_t px;
                std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[col * 4 + 0] = static_cast<uint8_t>(((px >> 10) & 0x1F) * 255 / 31);
                dstRow[col * 4 + 1] = static_cast<uint8_t>(((px >>  5) & 0x1F) * 255 / 31);
                dstRow[col * 4 + 2] = static_cast<uint8_t>(( px        & 0x1F) * 255 / 31);
                dstRow[col * 4 + 3] = 255;
            }
        }
    }
}

std::vector<unsigned char> LibretroSnes9xBackend::dumpSaveData() const
{
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

bool LibretroSnes9xBackend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) {
        return false;
    }

    return fn_unserialize(data.data(), data.size());
}

bool LibretroSnes9xBackend::loadRom(const std::string& path)
{
    if (!coreHandle) return false;
    if (gameLoaded) {
        fn_unload_game();
        gameLoaded = false;
        romData.clear();
    }
    libretro::system_info sysInfo = {};
    fn_get_system_info(&sysInfo);

    libretro::game_info gameInfo = {};
    gameInfo.path = path.c_str();
    gameInfo.meta = nullptr;

    if (!sysInfo.need_fullpath) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[SNES] Cannot open ROM: " << path << "\n";
            return false;
        }
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        romData.resize(size);
        if (!file.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
            std::cerr << "[SNES] Failed to read ROM: " << path << "\n";
            romData.clear();
            return false;
        }
        gameInfo.data = romData.data();
        gameInfo.size = romData.size();
    }
    if (!fn_load_game(&gameInfo)) {
        std::cerr << "[SNES] Core rejected ROM: " << path << "\n";
        romData.clear();
        return false;
    }
    gameLoaded = true;
    libretro::av_info avInfo = {};
    fn_get_system_av_info(&avInfo);
    if (avInfo.timing.fps > 1.0) {
        coreFps = avInfo.timing.fps;
    }
    frameWidth  = static_cast<int>(avInfo.geometry.base_width);
    frameHeight = static_cast<int>(avInfo.geometry.base_height);
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    ensureAudioDevice(static_cast<int>(avInfo.timing.sample_rate));
    return true;
}

bool LibretroSnes9xBackend::nextFrame()
{
    if (!coreHandle || !gameLoaded) return false;
    fn_run();
    return true;
}

void LibretroSnes9xBackend::setInput(const superpy_input_t& input)
{
    currentInput = input;
}

std::vector<unsigned char> SNESEmulation::dumpSaveData() const
{
    if (!backend) {
        return {};
    }

    return backend->dumpSaveData();
}

bool SNESEmulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!backend) {
        return false;
    }

    return backend->restoreSaveData(data);
}

superpy_color_t LibretroSnes9xBackend::getPixel(int x, int y) const
{
    if (x < 0 || x >= frameWidth || y < 0 || y >= frameHeight) return {0, 0, 0, 255};
    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(frameWidth) + static_cast<size_t>(x)) * 4;
    return {framePixels[offset], framePixels[offset + 1], framePixels[offset + 2], framePixels[offset + 3]};
}

bool LibretroSnes9xBackend::s_envCb(unsigned cmd, void* data)
{
    return s_instance ? s_instance->onEnvironment(cmd, data) : false;
}
void LibretroSnes9xBackend::s_videoCb(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (s_instance) s_instance->onVideoRefresh(data, w, h, pitch);
}
void LibretroSnes9xBackend::s_audioCb(int16_t l, int16_t r)
{
    if (!s_instance) {
        return;
    }

    int16_t frame[2] = {l, r};
    s_instance->queueAudioFrames(frame, 1);
}

size_t LibretroSnes9xBackend::s_audioBatchCb(const int16_t* data, size_t frames)
{
    if (s_instance && data && frames > 0) {
        s_instance->queueAudioFrames(data, frames);
    }
    return frames;
}

bool LibretroSnes9xBackend::ensureAudioDevice(int sampleRate)
{
    if (audioDeviceFailed) {
        return false;
    }

    if (sampleRate <= 0) {
        sampleRate = 48000;
    }

    if (audioDevice != 0 && sampleRate == coreSampleRate) {
        return true;
    }

    closeAudioDevice();

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[SNES] Failed to initialize SDL audio subsystem: " << SDL_GetError() << "\n";
            return false;
        }
    }

#ifdef SDL_HINT_AUDIO_RESAMPLING_MODE
    // Prefer quality over speed to reduce fuzzy resampling artifacts.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_RESAMPLING_MODE, "best", SDL_HINT_OVERRIDE);
#endif

    SDL_AudioSpec desired = {};
    desired.freq = sampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = nullptr;

    SDL_AudioSpec obtained = {};
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) {
        std::cerr << "[SNES] Failed to open SDL audio device: " << SDL_GetError() << "\n";
        audioDeviceFailed = true;
        return false;
    }

    coreSampleRate = sampleRate;
    audioSampleRate = obtained.freq;
    audioStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, coreSampleRate, AUDIO_S16SYS, 2, audioSampleRate);
    if (!audioStream) {
        std::cerr << "[SNES] Failed to create SDL audio stream: " << SDL_GetError() << "\n";
        closeAudioDevice();
        return false;
    }

    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void LibretroSnes9xBackend::closeAudioDevice()
{
    if (audioStream) {
        SDL_FreeAudioStream(audioStream);
        audioStream = nullptr;
    }

    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
}

void LibretroSnes9xBackend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0) {
        return;
    }

    if (audioDevice == 0 && !ensureAudioDevice(audioSampleRate)) {
        return;
    }

    if (!audioStream) {
        return;
    }

    const int frameBytes = 2 * static_cast<int>(sizeof(int16_t));
    const size_t inputBytes = frames * static_cast<size_t>(frameBytes);
    if (SDL_AudioStreamPut(audioStream, data, static_cast<int>(inputBytes)) < 0) {
        return;
    }

    // Keep a bounded device queue and leave excess in audioStream to avoid hard clears/pop noise.
    const std::size_t maxQueuedBytes = static_cast<std::size_t>(audioSampleRate) * 2 * sizeof(int16_t) / 5;
    const std::size_t queuedBytes = static_cast<std::size_t>(SDL_GetQueuedAudioSize(audioDevice));
    if (queuedBytes >= maxQueuedBytes) {
        return;
    }

    int available = SDL_AudioStreamAvailable(audioStream);
    if (available <= 0) {
        return;
    }

    std::size_t toQueue = std::min<std::size_t>(static_cast<std::size_t>(available), maxQueuedBytes - queuedBytes);
    toQueue -= toQueue % static_cast<std::size_t>(frameBytes);
    if (toQueue == 0) {
        return;
    }

    audioConvertBuffer.resize(toQueue);
    const int got = SDL_AudioStreamGet(audioStream, audioConvertBuffer.data(), static_cast<int>(toQueue));
    if (got > 0) {
        SDL_QueueAudio(audioDevice, audioConvertBuffer.data(), static_cast<Uint32>(got));
    }
}

void LibretroSnes9xBackend::s_inputPollCb() {}
int16_t LibretroSnes9xBackend::s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (!s_instance || port != 0 || device != libretro::DEVICE_JOYPAD || index != 0) return 0;
    const superpy_input_t& in = s_instance->currentInput;
    switch (id) {
        case libretro::JOYPAD_UP:     return in.up     ? 1 : 0;
        case libretro::JOYPAD_DOWN:   return in.down   ? 1 : 0;
        case libretro::JOYPAD_LEFT:   return in.left   ? 1 : 0;
        case libretro::JOYPAD_RIGHT:  return in.right  ? 1 : 0;
        case libretro::JOYPAD_A:      return in.a      ? 1 : 0;
        case libretro::JOYPAD_B:      return in.b      ? 1 : 0;
        case libretro::JOYPAD_X:      return in.x      ? 1 : 0;
        case libretro::JOYPAD_Y:      return in.y      ? 1 : 0;
        case libretro::JOYPAD_L:      return in.l      ? 1 : 0;
        case libretro::JOYPAD_R:      return in.r      ? 1 : 0;
        case libretro::JOYPAD_SELECT: return in.select ? 1 : 0;
        case libretro::JOYPAD_START:  return in.start  ? 1 : 0;
        default: return 0;
    }
}

SNESEmulation::InputMode SNESEmulation::globalInputMode = SNESEmulation::InputMode::KeyboardOnly;
SNESEmulation::KeyBindings SNESEmulation::globalKeyBindings = {};
SNESEmulation::ControllerBindings SNESEmulation::globalControllerBindings = {};
bool SNESEmulation::globalInputConfigured = false;

SNESEmulation::SNESEmulation()
    : isRunning(false), window(nullptr), renderer(nullptr),
    screenTexture(nullptr), controller(nullptr), controllerDeviceIndex(-1), scaleX(2), scaleY(2), workerInput{},
    emulationThreadStopRequested(false), emulationThreadRunning(false),
    emulationThreadFailed(false), frameReady(false),
      inputMode(InputMode::KeyboardOnly)
{
    {
        auto libretroBackend = std::make_unique<LibretroSnes9xBackend>();
        if (libretroBackend->isAvailable()) {
            backend = std::move(libretroBackend);
        } else {
            backend = std::make_unique<SuperPyUnavailableBackend>();
        }
    }
    frameBuffer.resize(kSNESScreenWidth * kSNESScreenHeight * 4, 0);
    latestFrame.resize(kSNESScreenWidth * kSNESScreenHeight * 4, 0);
    setDefaultBindings();

    if (globalInputConfigured) {
        inputMode = globalInputMode;
        keyBindings = globalKeyBindings;
        controllerBindings = globalControllerBindings;
    } else {
        saveGlobalInputSettings();
    }
}

SNESEmulation::~SNESEmulation()
{
    unloadRom();
    cleanupSDL();
}

bool SNESEmulation::loadRom(const string& path)
{
    if (!backend) {
        std::cerr << "SNES backend not initialized.\n";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open ROM file: " << path << "\n";
        return false;
    }

    const bool hasSmc = path.length() >= 4 && path.substr(path.length() - 4) == ".smc";
    const bool hasSfc = path.length() >= 4 && path.substr(path.length() - 4) == ".sfc";
    if (!hasSmc && !hasSfc) {
        std::cerr << "Invalid file extension. Only .smc/.sfc files are supported for SNES.\n";
        file.close();
        return false;
    }

    file.close();

    if (!backend->loadRom(path)) {
        return false;
    }

    romPath = path;
    isRunning = true;
    std::cout << "Successfully loaded SNES ROM: " << path << "\n";
    return true;
}

void SNESEmulation::unloadRom()
{
    stopEmulationWorker();
    isRunning = false;
    romPath.clear();
}

void SNESEmulation::calculateScale(int screenWidth, int screenHeight)
{
    int maxScaleX = (screenWidth - 50) / kSNESScreenWidth;
    int maxScaleY = (screenHeight - 50) / kSNESScreenHeight;
    int scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;

    scaleX = (scale < 1) ? 1 : scale;
    scaleY = scaleX;
}

void SNESEmulation::initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    calculateScale(displayMode.w, displayMode.h);

    const int windowWidth = kSNESScreenWidth * scaleX;
    const int windowHeight = kSNESScreenHeight * scaleY;

    window = SDL_CreateWindow(
        "SNES Emulator - SuperPy",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowWidth,
        windowHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Failed to create SDL renderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return;
    }

    screenTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        kSNESScreenWidth,
        kSNESScreenHeight
    );

    if (!screenTexture) {
        std::cerr << "Failed to create SDL texture: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = nullptr;
        window = nullptr;
        SDL_Quit();
        return;
    }
}

void SNESEmulation::cleanupSDL()
{
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

void SNESEmulation::setDefaultBindings()
{
    keyBindings = {
        SDLK_UP,
        SDLK_DOWN,
        SDLK_LEFT,
        SDLK_RIGHT,
        SDLK_z,
        SDLK_x,
        SDLK_c,
        SDLK_v,
        SDLK_a,
        SDLK_s,
        SDLK_RSHIFT,
        SDLK_RETURN
    };

    controllerBindings = {
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START
    };
}

void SNESEmulation::saveGlobalInputSettings()
{
    globalInputMode = inputMode;
    globalKeyBindings = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured = true;
}

void SNESEmulation::listControllers() const
{
    const int joystickCount = SDL_NumJoysticks();
    if (joystickCount <= 0) {
        std::cout << "No controllers detected.\n";
        return;
    }

    std::cout << "Detected controllers:\n";
    for (int i = 0; i < joystickCount; ++i) {
        if (SDL_IsGameController(i)) {
            const char* name = SDL_GameControllerNameForIndex(i);
            std::cout << "  [" << i << "] " << (name ? name : "Unknown controller") << "\n";
        }
    }
}

bool SNESEmulation::openControllerByIndex(int index)
{
    if (index < 0 || index >= SDL_NumJoysticks() || !SDL_IsGameController(index)) {
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
    const char* controllerName = SDL_GameControllerName(controller);
    std::cout << "Using controller: " << (controllerName ? controllerName : "Unknown") << "\n";
    return true;
}

void SNESEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== SNES Input Configuration ===\n";
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
                const char* name;
                SDL_Keycode* key;
            };

            KeyboardAction actions[] = {
                {"UP", &keyBindings.up},
                {"DOWN", &keyBindings.down},
                {"LEFT", &keyBindings.left},
                {"RIGHT", &keyBindings.right},
                {"A", &keyBindings.a},
                {"B", &keyBindings.b},
                {"X", &keyBindings.x},
                {"Y", &keyBindings.y},
                {"L", &keyBindings.l},
                {"R", &keyBindings.r},
                {"SELECT", &keyBindings.select},
                {"START", &keyBindings.start}
            };

            std::cout << "Enter key names recognized by SDL. Press Enter to keep existing binding.\n";
            for (KeyboardAction& action : actions) {
                std::cout << action.name << " [" << SDL_GetKeyName(*action.key) << "]: ";
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
            std::cout << "Enter controller button names (A,B,X,Y,BACK,START,LEFTSHOULDER,RIGHTSHOULDER,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT).\n";
            std::cout << "Press Enter to keep current binding.\n";

            struct ControllerAction {
                const char* name;
                SDL_GameControllerButton* button;
            };

            ControllerAction actions[] = {
                {"UP", &controllerBindings.up},
                {"DOWN", &controllerBindings.down},
                {"LEFT", &controllerBindings.left},
                {"RIGHT", &controllerBindings.right},
                {"A", &controllerBindings.a},
                {"B", &controllerBindings.b},
                {"X", &controllerBindings.x},
                {"Y", &controllerBindings.y},
                {"L", &controllerBindings.l},
                {"R", &controllerBindings.r},
                {"SELECT", &controllerBindings.select},
                {"START", &controllerBindings.start}
            };

            for (ControllerAction& action : actions) {
                std::cout << action.name << " [" << controllerButtonName(*action.button) << "]: ";
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
    std::cout << "Global SNES controls updated.\n";
}

void SNESEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, superpy_input_t& input, bool& running)
{
    if (key == SDLK_ESCAPE && pressed) {
        running = false;
        return;
    }

    if (key == keyBindings.up) input.up = pressed;
    if (key == keyBindings.down) input.down = pressed;
    if (key == keyBindings.left) input.left = pressed;
    if (key == keyBindings.right) input.right = pressed;
    if (key == keyBindings.a) input.a = pressed;
    if (key == keyBindings.b) input.b = pressed;
    if (key == keyBindings.x) input.x = pressed;
    if (key == keyBindings.y) input.y = pressed;
    if (key == keyBindings.l) input.l = pressed;
    if (key == keyBindings.r) input.r = pressed;
    if (key == keyBindings.select) input.select = pressed;
    if (key == keyBindings.start) input.start = pressed;
}

void SNESEmulation::handleControllerButtonEvent(Uint8 button, bool pressed, superpy_input_t& input, bool& running)
{
    (void)running;

    if (button == static_cast<Uint8>(controllerBindings.up)) input.up = pressed;
    if (button == static_cast<Uint8>(controllerBindings.down)) input.down = pressed;
    if (button == static_cast<Uint8>(controllerBindings.left)) input.left = pressed;
    if (button == static_cast<Uint8>(controllerBindings.right)) input.right = pressed;
    if (button == static_cast<Uint8>(controllerBindings.a)) input.a = pressed;
    if (button == static_cast<Uint8>(controllerBindings.b)) input.b = pressed;
    if (button == static_cast<Uint8>(controllerBindings.x)) input.x = pressed;
    if (button == static_cast<Uint8>(controllerBindings.y)) input.y = pressed;
    if (button == static_cast<Uint8>(controllerBindings.l)) input.l = pressed;
    if (button == static_cast<Uint8>(controllerBindings.r)) input.r = pressed;
    if (button == static_cast<Uint8>(controllerBindings.select)) input.select = pressed;
    if (button == static_cast<Uint8>(controllerBindings.start)) input.start = pressed;
}

void SNESEmulation::setInput(const superpy_input_t& input)
{
    if (!backend || !isRunning.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(inputMutex);
    workerInput = input;
}

void SNESEmulation::nextFrame()
{
    if (!backend || !isRunning.load()) {
        return;
    }

    if (!backend->nextFrame()) {
        std::cerr << "SNES emulator failed to advance frame.\n";
        isRunning = false;
    }
}

superpy_color_t SNESEmulation::getPixel(int x, int y) const
{
    if (!backend || !isRunning.load()) {
        return {0, 0, 0, 255};
    }

    if (x < 0 || x >= kSNESScreenWidth || y < 0 || y >= kSNESScreenHeight) {
        return {0, 0, 0, 255};
    }

    std::lock_guard<std::mutex> lock(frameMutex);
    if (!frameReady.load() || latestFrame.size() < static_cast<std::size_t>(kSNESScreenWidth * kSNESScreenHeight * 4)) {
        return {0, 0, 0, 255};
    }

    const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kSNESScreenWidth) + static_cast<std::size_t>(x)) * 4;
    return {
        latestFrame[offset + 0],
        latestFrame[offset + 1],
        latestFrame[offset + 2],
        latestFrame[offset + 3]
    };
}

void SNESEmulation::renderFrame()
{
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

    if (SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), kSNESScreenWidth * 4) < 0) {
        std::cerr << "Failed to update texture: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Rect destRect = {0, 0, kSNESScreenWidth * scaleX, kSNESScreenHeight * scaleY};
    SDL_RenderCopy(renderer, screenTexture, nullptr, &destRect);

    SDL_RenderPresent(renderer);
}

void SNESEmulation::displayFrame() const
{
    if (!backend || !isRunning) {
        return;
    }

    for (int y = 0; y < kSNESScreenHeight; y += 8) {
        for (int x = 0; x < kSNESScreenWidth; x += 8) {
            superpy_color_t pixel = getPixel(x, y);
            int gray = (pixel.r + pixel.g + pixel.b) / 3;

            if (gray > 192) {
                std::cout << "#";
            } else if (gray > 128) {
                std::cout << "*";
            } else if (gray > 64) {
                std::cout << "+";
            } else {
                std::cout << ".";
            }
        }
        std::cout << "\n";
    }
}

void SNESEmulation::configureGlobalControls()
{
    SNESEmulation configSession;

    const bool hadControllerSubsystem = (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0);
    if (!hadControllerSubsystem && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize controller subsystem: " << SDL_GetError() << "\n";
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

void SNESEmulation::runInteractive()
{
    if (!backend || !isRunning.load()) {
        std::cerr << "No SNES ROM loaded.\n";
        return;
    }

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize controller subsystem: " << SDL_GetError() << "\n";
    }

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
        std::cout << "Controller-only mode selected, but no controller is active. Falling back to keyboard.\n";
        inputMode = InputMode::KeyboardOnly;
    }

    std::cout << "\n=== SNES Emulator Running ===\n";
    std::cout << "ROM: " << romPath << "\n";
    std::cout << "Window opened. Close it to exit.\n";
    std::cout << "Press ESC to exit emulator.\n\n";

    superpy_input_t input = {};
    bool running = true;
    bool windowClosed = false;

    if (!startEmulationWorker()) {
        std::cerr << "Failed to start SNES emulation worker thread.\n";
        cleanupSDL();
        return;
    }

    while (running && !windowClosed) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    windowClosed = true;
                    break;

                case SDL_KEYDOWN:
                    if (inputMode == InputMode::KeyboardOnly || inputMode == InputMode::KeyboardAndController) {
                        handleKeyboardEvent(event.key.keysym.sym, true, input, running);
                    }
                    break;

                case SDL_KEYUP:
                    if (inputMode == InputMode::KeyboardOnly || inputMode == InputMode::KeyboardAndController) {
                        handleKeyboardEvent(event.key.keysym.sym, false, input, running);
                    }
                    break;

                case SDL_CONTROLLERBUTTONDOWN:
                    if (inputMode == InputMode::ControllerOnly || inputMode == InputMode::KeyboardAndController) {
                        handleControllerButtonEvent(event.cbutton.button, true, input, running);
                    }
                    break;

                case SDL_CONTROLLERBUTTONUP:
                    if (inputMode == InputMode::ControllerOnly || inputMode == InputMode::KeyboardAndController) {
                        handleControllerButtonEvent(event.cbutton.button, false, input, running);
                    }
                    break;

                case SDL_CONTROLLERDEVICEADDED:
                    if (!controller) {
                        openControllerByIndex(event.cdevice.which);
                    }
                    break;

                case SDL_CONTROLLERDEVICEREMOVED:
                    if (controller) {
                        SDL_Joystick* joy = SDL_GameControllerGetJoystick(controller);
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

        setInput(input);

        if (emulationThreadFailed.load() || !isRunning.load()) {
            running = false;
            break;
        }

        renderFrame();

        SDL_Delay(1);
    }

    stopEmulationWorker();
    cleanupSDL();
    std::cout << "\nSNES emulation stopped.\n";
}

bool SNESEmulation::startEmulationWorker()
{
    if (emulationThreadRunning.load() || !backend || !isRunning.load()) {
        return false;
    }

    emulationThreadStopRequested = false;
    emulationThreadFailed = false;
    frameReady = false;
    emulationThread = std::thread(&SNESEmulation::emulationWorkerLoop, this);
    return true;
}

void SNESEmulation::stopEmulationWorker()
{
    emulationThreadStopRequested = true;
    if (emulationThread.joinable()) {
        emulationThread.join();
    }
}

void SNESEmulation::emulationWorkerLoop()
{
    emulationThreadRunning = true;

    std::vector<unsigned char> localFrame(static_cast<std::size_t>(kSNESScreenWidth * kSNESScreenHeight) * 4, 0);
    AudioMasterClock clock;
    clock.configure(backend ? backend->getAudioSampleRate() : 48000, backend ? backend->getNominalFps() : 60.0, 80);

    while (!emulationThreadStopRequested.load() && isRunning.load()) {
        superpy_input_t inputSnapshot = {};
        {
            std::lock_guard<std::mutex> lock(inputMutex);
            inputSnapshot = workerInput;
        }

        backend->setInput(inputSnapshot);
        if (!backend->nextFrame()) {
            std::cerr << "SNES emulator failed to advance frame.\n";
            isRunning = false;
            emulationThreadFailed = true;
            break;
        }

        for (int y = 0; y < kSNESScreenHeight; ++y) {
            for (int x = 0; x < kSNESScreenWidth; ++x) {
                const superpy_color_t color = backend->getPixel(x, y);
                const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kSNESScreenWidth) + static_cast<std::size_t>(x)) * 4;
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
