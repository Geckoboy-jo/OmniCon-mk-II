#include "Atari800.hpp"
#include "audio_clock.hpp"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
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

bool hasOptionValue(const char* definition, const std::string& needle)
{
    if (!definition || needle.empty()) {
        return false;
    }

    const std::string def(definition);
    const std::size_t semicolonPos = def.find(';');
    if (semicolonPos == std::string::npos || semicolonPos + 1 >= def.size()) {
        return false;
    }

    std::string values = def.substr(semicolonPos + 1);
    std::size_t start = 0;
    while (start < values.size()) {
        const std::size_t pipePos = values.find('|', start);
        const std::size_t tokenLen = (pipePos == std::string::npos)
            ? (values.size() - start)
            : (pipePos - start);
        const std::string token = trim(values.substr(start, tokenLen));
        if (token == needle) {
            return true;
        }
        if (pipePos == std::string::npos) {
            break;
        }
        start = pipePos + 1;
    }

    return false;
}

std::string toUpperCopy(const std::string& value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string chooseOptionValueContains(const char* definition, const std::string& needle)
{
    if (!definition || needle.empty()) {
        return "";
    }

    const std::string def(definition);
    const std::size_t semicolonPos = def.find(';');
    if (semicolonPos == std::string::npos || semicolonPos + 1 >= def.size()) {
        return "";
    }

    const std::string needleUpper = toUpperCopy(needle);
    std::string values = def.substr(semicolonPos + 1);
    std::size_t start = 0;
    while (start < values.size()) {
        const std::size_t pipePos = values.find('|', start);
        const std::size_t tokenLen = (pipePos == std::string::npos)
            ? (values.size() - start)
            : (pipePos - start);
        const std::string token = trim(values.substr(start, tokenLen));
        if (!token.empty()) {
            const std::string tokenUpper = toUpperCopy(token);
            if (tokenUpper.find(needleUpper) != std::string::npos) {
                return token;
            }
        }
        if (pipePos == std::string::npos) {
            break;
        }
        start = pipePos + 1;
    }

    return "";
}

std::string firstOptionValue(const char* definition)
{
    if (!definition) {
        return "";
    }

    const std::string def(definition);
    const std::size_t semicolonPos = def.find(';');
    if (semicolonPos == std::string::npos || semicolonPos + 1 >= def.size()) {
        return "";
    }

    std::string values = def.substr(semicolonPos + 1);
    const std::size_t pipePos = values.find('|');
    if (pipePos != std::string::npos) {
        values = values.substr(0, pipePos);
    }
    return trim(values);
}

std::string chooseOptionValue(const char* definition, const std::string& preferred)
{
    if (hasOptionValue(definition, preferred)) {
        return preferred;
    }
    return firstOptionValue(definition);
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

class Atari800Backend {
public:
    virtual ~Atari800Backend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const atari800_input_t& input) = 0;
    virtual std::vector<unsigned char> dumpSaveData() const = 0;
    virtual bool restoreSaveData(const std::vector<unsigned char>& data) = 0;
    virtual atari800_color_t getPixel(int x, int y) const = 0;
    virtual int getAudioSampleRate() const { return 48000; }
    virtual double getNominalFps() const { return 60.0; }
    virtual double getDisplayAspectRatio() const { return 4.0 / 3.0; }
    virtual int getFrameWidth() const { return 400; }
    virtual int getFrameHeight() const { return 300; }
};

class Atari800UnavailableBackend final : public Atari800Backend {
public:
    bool loadRom(const std::string& path) override
    {
        (void)path;
        std::cerr << "Atari800 libretro core is not available in this build. Atari 800 emulation is disabled.\n";
        std::cerr << "Install atari800_libretro.so and restart OmniCon.\n";
        return false;
    }

    bool nextFrame() override { return false; }
    void setInput(const atari800_input_t& input) override { (void)input; }
    std::vector<unsigned char> dumpSaveData() const override { return {}; }
    bool restoreSaveData(const std::vector<unsigned char>& data) override { (void)data; return false; }
    atari800_color_t getPixel(int x, int y) const override
    {
        (void)x;
        (void)y;
        return {0, 0, 0, 255};
    }
};

namespace libretro {
    static constexpr unsigned ENV_GET_CAN_DUPE        = 3;
    static constexpr unsigned ENV_GET_SYSTEM_DIRECTORY = 9;
    static constexpr unsigned ENV_SET_PIXEL_FORMAT    = 10;
    static constexpr unsigned ENV_GET_VARIABLE        = 15;
    static constexpr unsigned ENV_SET_VARIABLES       = 16;
    static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
    static constexpr unsigned ENV_SET_SUPPORT_NO_GAME = 18;
    static constexpr unsigned ENV_GET_LOG_INTERFACE   = 27;
    static constexpr unsigned ENV_GET_CONTENT_DIRECTORY = 30;
    static constexpr unsigned ENV_GET_SAVE_DIRECTORY  = 31;
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
    struct variable { const char* key; const char* value; };

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

class LibretroAtari800Backend final : public Atari800Backend {
public:
    static constexpr const char* kDefaultSystemCorePath =
        "/usr/lib/x86_64-linux-gnu/libretro/atari800_libretro.so";
    static constexpr const char* kWorkspaceCorePath =
        "third_party/libretro/atari800_libretro.so";

    LibretroAtari800Backend();
    ~LibretroAtari800Backend() override;

    bool isAvailable() const { return coreHandle != nullptr; }

    bool loadRom(const std::string& path) override;
    bool nextFrame() override;
    void setInput(const atari800_input_t& input) override;
    std::vector<unsigned char> dumpSaveData() const override;
    bool restoreSaveData(const std::vector<unsigned char>& data) override;
    atari800_color_t getPixel(int x, int y) const override;
    int getAudioSampleRate() const override { return audioSampleRate; }
    double getNominalFps() const override { return coreFps; }
    double getDisplayAspectRatio() const override { return displayAspectRatio; }
    int getFrameWidth() const override { return frameWidth; }
    int getFrameHeight() const override { return frameHeight; }

    bool onEnvironment(unsigned cmd, void* data);
    void onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);

private:
    static std::string resolveCorePath();
    bool loadSymbols();
    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioFrames(const int16_t* data, size_t frames);

    std::string corePath;
    void* coreHandle = nullptr;
    void    (*fn_init)() = nullptr;
    void    (*fn_deinit)() = nullptr;
    void    (*fn_get_system_info)(libretro::system_info*) = nullptr;
    void    (*fn_get_system_av_info)(libretro::av_info*) = nullptr;
    void    (*fn_set_environment)(libretro::environment_t) = nullptr;
    void    (*fn_set_video_refresh)(libretro::video_refresh_t) = nullptr;
    void    (*fn_set_audio_sample)(libretro::audio_sample_t) = nullptr;
    void    (*fn_set_audio_sample_batch)(libretro::audio_sample_batch_t) = nullptr;
    void    (*fn_set_input_poll)(libretro::input_poll_t) = nullptr;
    void    (*fn_set_input_state)(libretro::input_state_t) = nullptr;
    size_t  (*fn_serialize_size)() = nullptr;
    bool    (*fn_serialize)(void*, size_t) = nullptr;
    bool    (*fn_unserialize)(const void*, size_t) = nullptr;
    bool    (*fn_load_game)(const libretro::game_info*) = nullptr;
    void    (*fn_unload_game)() = nullptr;
    void    (*fn_run)() = nullptr;

    std::vector<uint8_t> framePixels;
    int frameWidth = 384;
    int frameHeight = 240;
    atari800_input_t currentInput = {};
    unsigned pixelFormat = libretro::PIXEL_0RGB1555;
    bool gameLoaded = false;
    std::vector<uint8_t> romData;
    std::string loadedRomPath;  // Keep path string alive for libretro core
    std::string systemDirectory;
    std::string saveDirectory;
    std::string contentDirectory;
    SDL_AudioDeviceID audioDevice = 0;
    SDL_AudioStream* audioStream = nullptr;
    int coreSampleRate = 48000;
    int audioSampleRate = 48000;
    double coreFps = 60.0;
    double displayAspectRatio = 4.0 / 3.0;
    std::vector<uint8_t> audioConvertBuffer;
    std::string optSystem = "800XL (64K)";
    std::string optVideoStandard = "NTSC";
    std::string optResolution = "320x240";
    std::string optArtifactingMode = "none";

    static LibretroAtari800Backend* s_instance;
    static bool s_envCb(unsigned cmd, void* data);
    static void s_videoCb(const void* data, unsigned width, unsigned height, size_t pitch);
    static void s_audioCb(int16_t left, int16_t right);
    static size_t s_audioBatchCb(const int16_t* data, size_t frames);
    static void s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
};

LibretroAtari800Backend* LibretroAtari800Backend::s_instance = nullptr;

std::string LibretroAtari800Backend::resolveCorePath()
{
    const char* envPath = std::getenv("OMNICON_ATARI800_CORE");
    if (envPath && *envPath && std::filesystem::exists(envPath)) {
        std::cerr << "[Atari800] Using Atari800 core from environment: " << envPath << "\n";
        return envPath;
    }

    if (std::filesystem::exists(kWorkspaceCorePath)) {
        std::cerr << "[Atari800] Using Atari800 core from workspace: " << kWorkspaceCorePath << "\n";
        return kWorkspaceCorePath;
    }

    std::cerr << "[Atari800] Using Atari800 core from system: " << kDefaultSystemCorePath << "\n";
    return kDefaultSystemCorePath;
}

LibretroAtari800Backend::LibretroAtari800Backend()
{
    corePath = resolveCorePath();

    const std::filesystem::path workspaceRoot = std::filesystem::current_path();
    const std::filesystem::path frontendDataRoot = workspaceRoot / ".omnicon" / "atari800";
    std::error_code ec;
    std::filesystem::create_directories(frontendDataRoot / "system", ec);
    std::filesystem::create_directories(frontendDataRoot / "save", ec);
    std::filesystem::create_directories(frontendDataRoot / "content", ec);
    systemDirectory = (frontendDataRoot / "system").string();
    saveDirectory = (frontendDataRoot / "save").string();
    contentDirectory = (frontendDataRoot / "content").string();

    coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[Atari800] Failed to load core: " << corePath << ": " << dlerror() << "\n";
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

LibretroAtari800Backend::~LibretroAtari800Backend()
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
    loadedRomPath.clear();
    romData.clear();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool LibretroAtari800Backend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) {
            std::cerr << "[Atari800] Missing symbol: " << name << "\n";
        }
        return sym;
    };

    fn_init = reinterpret_cast<void(*)()>(load("retro_init"));
    fn_deinit = reinterpret_cast<void(*)()>(load("retro_deinit"));
    fn_get_system_info = reinterpret_cast<void(*)(libretro::system_info*)>(load("retro_get_system_info"));
    fn_get_system_av_info = reinterpret_cast<void(*)(libretro::av_info*)>(load("retro_get_system_av_info"));
    fn_set_environment = reinterpret_cast<void(*)(libretro::environment_t)>(load("retro_set_environment"));
    fn_set_video_refresh = reinterpret_cast<void(*)(libretro::video_refresh_t)>(load("retro_set_video_refresh"));
    fn_set_audio_sample = reinterpret_cast<void(*)(libretro::audio_sample_t)>(load("retro_set_audio_sample"));
    fn_set_audio_sample_batch = reinterpret_cast<void(*)(libretro::audio_sample_batch_t)>(load("retro_set_audio_sample_batch"));
    fn_set_input_poll = reinterpret_cast<void(*)(libretro::input_poll_t)>(load("retro_set_input_poll"));
    fn_set_input_state = reinterpret_cast<void(*)(libretro::input_state_t)>(load("retro_set_input_state"));
    fn_serialize_size = reinterpret_cast<size_t(*)()>(load("retro_serialize_size"));
    fn_serialize = reinterpret_cast<bool(*)(void*, size_t)>(load("retro_serialize"));
    fn_unserialize = reinterpret_cast<bool(*)(const void*, size_t)>(load("retro_unserialize"));
    fn_load_game = reinterpret_cast<bool(*)(const libretro::game_info*)>(load("retro_load_game"));
    fn_unload_game = reinterpret_cast<void(*)()>(load("retro_unload_game"));
    fn_run = reinterpret_cast<void(*)()>(load("retro_run"));

    return fn_init && fn_deinit && fn_get_system_info && fn_get_system_av_info &&
           fn_set_environment && fn_set_video_refresh && fn_set_audio_sample &&
           fn_set_audio_sample_batch && fn_set_input_poll && fn_set_input_state &&
           fn_serialize_size && fn_serialize && fn_unserialize &&
           fn_load_game && fn_unload_game && fn_run;
}

bool LibretroAtari800Backend::onEnvironment(unsigned cmd, void* data)
{
    switch (cmd) {
        case libretro::ENV_GET_CAN_DUPE:
            *static_cast<bool*>(data) = true;
            return true;
        case libretro::ENV_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = systemDirectory.c_str();
            return true;
        case libretro::ENV_SET_PIXEL_FORMAT:
            pixelFormat = *static_cast<unsigned*>(data);
            return true;
        case libretro::ENV_GET_VARIABLE:
            if (!data) {
                return false;
            }
            {
                auto* variable = static_cast<libretro::variable*>(data);
                if (!variable->key) {
                    return false;
                }

                variable->value = nullptr;
                if (std::strcmp(variable->key, "atari800_system") == 0) {
                    variable->value = optSystem.c_str();
                    return true;
                }
                if (std::strcmp(variable->key, "atari800_ntscpal") == 0) {
                    variable->value = optVideoStandard.c_str();
                    return true;
                }
                if (std::strcmp(variable->key, "atari800_resolution") == 0) {
                    variable->value = optResolution.c_str();
                    return true;
                }
                if (std::strcmp(variable->key, "atari800_artifacting_mode") == 0) {
                    variable->value = optArtifactingMode.c_str();
                    return true;
                }
            }
            return false;
        case libretro::ENV_SET_VARIABLES:
            if (data) {
                const auto* variable = static_cast<const libretro::variable*>(data);
                for (; variable && variable->key; ++variable) {
                    if (std::strcmp(variable->key, "atari800_system") == 0) {
                        std::string value = chooseOptionValue(variable->value, "800XL (64K)");
                        if (value.empty()) {
                            value = chooseOptionValueContains(variable->value, "800XL");
                        }
                        if (!value.empty()) {
                            optSystem = value;
                        }
                    } else if (std::strcmp(variable->key, "atari800_ntscpal") == 0) {
                        std::string value = chooseOptionValue(variable->value, "NTSC");
                        if (value.empty()) {
                            value = chooseOptionValueContains(variable->value, "NTSC");
                        }
                        if (value.empty()) {
                            value = chooseOptionValue(variable->value, "PAL");
                        }
                        if (!value.empty()) {
                            optVideoStandard = value;
                        }
                    } else if (std::strcmp(variable->key, "atari800_resolution") == 0) {
                        std::string value = chooseOptionValue(variable->value, "320x240");
                        if (value.empty()) {
                            value = chooseOptionValue(variable->value, "336x240");
                        }
                        if (value.empty()) {
                            value = chooseOptionValueContains(variable->value, "320x240");
                        }
                        if (value.empty()) {
                            value = chooseOptionValueContains(variable->value, "336x240");
                        }
                        if (!value.empty()) {
                            optResolution = value;
                        }
                    } else if (std::strcmp(variable->key, "atari800_artifacting_mode") == 0) {
                        std::string value = chooseOptionValue(variable->value, "none");
                        if (value.empty()) {
                            value = chooseOptionValueContains(variable->value, "none");
                        }
                        if (value.empty()) {
                            value = chooseOptionValue(variable->value, "blue/brown 1");
                        }
                        if (value.empty()) {
                            value = chooseOptionValue(variable->value, "blue/brown 2");
                        }
                        if (!value.empty()) {
                            optArtifactingMode = value;
                        }
                    }
                }
            }
            return true;
        case libretro::ENV_GET_VARIABLE_UPDATE:
            if (data) {
                *static_cast<bool*>(data) = false;
            }
            return true;
        case libretro::ENV_SET_SUPPORT_NO_GAME:
            return true;
        case libretro::ENV_GET_LOG_INTERFACE:
            return false;
        case libretro::ENV_GET_CONTENT_DIRECTORY:
            *static_cast<const char**>(data) = contentDirectory.c_str();
            return true;
        case libretro::ENV_GET_SAVE_DIRECTORY:
            *static_cast<const char**>(data) = saveDirectory.c_str();
            return true;
        case libretro::ENV_SET_GEOMETRY: {
            const auto* geom = static_cast<const libretro::game_geometry*>(data);
            frameWidth = static_cast<int>(geom->base_width);
            frameHeight = static_cast<int>(geom->base_height);
            if (geom->aspect_ratio > 0.1f) {
                displayAspectRatio = geom->aspect_ratio;
            }
            framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
            return true;
        }
        default:
            return false;
    }
}

void LibretroAtari800Backend::onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) {
        return;
    }

    // Clamp dimensions to reasonable limits to prevent huge allocations
    if (width == 0 || height == 0 || width > 2048 || height > 2048) {
        std::cerr << "[Atari800] Rejecting invalid video dimensions: " << width << "x" << height << "\n";
        return;
    }

    if (static_cast<int>(width) != frameWidth || static_cast<int>(height) != frameHeight) {
        frameWidth = static_cast<int>(width);
        frameHeight = static_cast<int>(height);
        framePixels.clear();
        framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    }

    if (framePixels.empty()) {
        std::cerr << "[Atari800] ERROR: framePixels buffer is empty during video refresh\n";
        return;
    }

    const size_t expectedBufferSize = static_cast<size_t>(frameWidth * frameHeight) * 4;
    if (framePixels.size() != expectedBufferSize) {
        std::cerr << "[Atari800] ERROR: framePixels size mismatch. Got " << framePixels.size() 
                  << ", expected " << expectedBufferSize << "\n";
        return;
    }

    const auto* src = static_cast<const uint8_t*>(data);
    for (unsigned row = 0; row < height; ++row) {
        const uint8_t* srcRow = src + row * pitch;
        const size_t dstRowOffset = row * static_cast<size_t>(frameWidth) * 4;
        
        if (dstRowOffset + static_cast<size_t>(frameWidth) * 4 > framePixels.size()) {
            std::cerr << "[Atari800] ERROR: Attempting to write beyond framePixels buffer at row " << row << "\n";
            return;
        }

        uint8_t* dstRow = framePixels.data() + dstRowOffset;

        if (pixelFormat == libretro::PIXEL_XRGB8888) {
            for (unsigned col = 0; col < width; ++col) {
                const size_t dstIdx = col * 4;
                if (dstIdx + 3 >= framePixels.size() - dstRowOffset) {
                    std::cerr << "[Atari800] ERROR: Buffer overflow in XRGB8888 conversion\n";
                    return;
                }
                uint32_t px;
                std::memcpy(&px, srcRow + col * 4, 4);
                dstRow[dstIdx + 0] = static_cast<uint8_t>((px >> 16) & 0xFF);
                dstRow[dstIdx + 1] = static_cast<uint8_t>((px >> 8) & 0xFF);
                dstRow[dstIdx + 2] = static_cast<uint8_t>(px & 0xFF);
                dstRow[dstIdx + 3] = 255;
            }
        } else if (pixelFormat == libretro::PIXEL_RGB565) {
            for (unsigned col = 0; col < width; ++col) {
                const size_t dstIdx = col * 4;
                if (dstIdx + 3 >= framePixels.size() - dstRowOffset) {
                    std::cerr << "[Atari800] ERROR: Buffer overflow in RGB565 conversion\n";
                    return;
                }
                uint16_t px;
                std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[dstIdx + 0] = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
                dstRow[dstIdx + 1] = static_cast<uint8_t>(((px >> 5) & 0x3F) * 255 / 63);
                dstRow[dstIdx + 2] = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
                dstRow[dstIdx + 3] = 255;
            }
        } else {
            for (unsigned col = 0; col < width; ++col) {
                const size_t dstIdx = col * 4;
                if (dstIdx + 3 >= framePixels.size() - dstRowOffset) {
                    std::cerr << "[Atari800] ERROR: Buffer overflow in 0RGB1555 conversion\n";
                    return;
                }
                uint16_t px;
                std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[dstIdx + 0] = static_cast<uint8_t>(((px >> 10) & 0x1F) * 255 / 31);
                dstRow[dstIdx + 1] = static_cast<uint8_t>(((px >> 5) & 0x1F) * 255 / 31);
                dstRow[dstIdx + 2] = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
                dstRow[dstIdx + 3] = 255;
            }
        }
    }
}

bool LibretroAtari800Backend::loadRom(const std::string& path)
{
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

    // Store path as member variable to keep it alive for libretro core
    loadedRomPath = path;
    std::error_code ec;
    const std::filesystem::path romPath(path);
    const std::filesystem::path romParent = romPath.parent_path();
    if (!romParent.empty() && std::filesystem::exists(romParent, ec)) {
        contentDirectory = romParent.string();
    }
    
    libretro::game_info gameInfo = {};
    gameInfo.path = loadedRomPath.c_str();
    gameInfo.meta = nullptr;

    if (!sysInfo.need_fullpath) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[Atari800] Cannot open ROM: " << path << "\n";
            return false;
        }

        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        romData.resize(size);
        if (!file.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
            std::cerr << "[Atari800] Failed to read ROM: " << path << "\n";
            romData.clear();
            return false;
        }
        gameInfo.data = romData.data();
        gameInfo.size = romData.size();
    }

    if (!fn_load_game(&gameInfo)) {
        std::cerr << "[Atari800] Core rejected ROM at: " << path << "\n";
        std::cerr << "[Atari800] Possible causes: file not found, unsupported format, or corrupted file\n";
        if (sysInfo.need_fullpath) {
            std::cerr << "[Atari800] Core attempted to load from file path (need_fullpath=true)\n";
        } else {
            std::cerr << "[Atari800] ROM data was loaded in memory (" << gameInfo.size << " bytes)\n";
        }
        loadedRomPath.clear();
        romData.clear();
        return false;
    }

    gameLoaded = true;
    libretro::av_info avInfo = {};
    fn_get_system_av_info(&avInfo);
    frameWidth = static_cast<int>(avInfo.geometry.base_width);
    frameHeight = static_cast<int>(avInfo.geometry.base_height);
    if (frameWidth <= 0) {
        frameWidth = 384;
    }
    if (frameHeight <= 0) {
        frameHeight = 240;
    }
    if (avInfo.timing.fps > 1.0) {
        coreFps = avInfo.timing.fps;
    }
    if (avInfo.geometry.aspect_ratio > 0.1f) {
        displayAspectRatio = avInfo.geometry.aspect_ratio;
    }
    std::cerr << "[Atari800] ROM loaded successfully: " << loadedRomPath << " (" << frameWidth << "x" << frameHeight << " @ " << coreFps << " fps)\n";
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    ensureAudioDevice(static_cast<int>(avInfo.timing.sample_rate));
    return true;
}

bool LibretroAtari800Backend::nextFrame()
{
    if (!coreHandle || !gameLoaded) {
        return false;
    }

    // Verify framePixels is properly sized before running core
    if (framePixels.empty()) {
        std::cerr << "[Atari800] Error: framePixels buffer not initialized before nextFrame()\n";
        return false;
    }

    try {
        fn_run();
    } catch (...) {
        std::cerr << "[Atari800] Exception caught during core execution\n";
        return false;
    }
    return true;
}

void LibretroAtari800Backend::setInput(const atari800_input_t& input)
{
    currentInput = input;
}

std::vector<unsigned char> LibretroAtari800Backend::dumpSaveData() const
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

bool LibretroAtari800Backend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) {
        return false;
    }

    return fn_unserialize(data.data(), data.size());
}

atari800_color_t LibretroAtari800Backend::getPixel(int x, int y) const
{
    if (x < 0 || x >= frameWidth || y < 0 || y >= frameHeight) {
        return {0, 0, 0, 255};
    }

    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(frameWidth) + static_cast<size_t>(x)) * 4;
    const size_t requiredSize = offset + 4;
    
    // Defensive check: ensure framePixels is large enough
    if (requiredSize > framePixels.size()) {
        return {0, 0, 0, 255};
    }
    
    return {framePixels[offset], framePixels[offset + 1], framePixels[offset + 2], framePixels[offset + 3]};
}

bool LibretroAtari800Backend::ensureAudioDevice(int sampleRate)
{
    if (sampleRate <= 0) {
        sampleRate = 48000;
    }

    if (audioDevice != 0 && sampleRate == coreSampleRate) {
        return true;
    }

    closeAudioDevice();

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[Atari800] Failed to initialize SDL audio subsystem: " << SDL_GetError() << "\n";
            return false;
        }
    }

#ifdef SDL_HINT_AUDIO_RESAMPLING_MODE
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_RESAMPLING_MODE, "best", SDL_HINT_OVERRIDE);
#endif

    SDL_AudioSpec desired = {};
    desired.freq = sampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;

    SDL_AudioSpec obtained = {};
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) {
        std::cerr << "[Atari800] Failed to open SDL audio device: " << SDL_GetError() << "\n";
        return false;
    }

    coreSampleRate = sampleRate;
    audioSampleRate = obtained.freq > 0 ? obtained.freq : sampleRate;
    audioStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, coreSampleRate, AUDIO_S16SYS, 2, audioSampleRate);
    if (!audioStream) {
        std::cerr << "[Atari800] Failed to create SDL audio stream: " << SDL_GetError() << "\n";
        closeAudioDevice();
        return false;
    }

    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void LibretroAtari800Backend::closeAudioDevice()
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

void LibretroAtari800Backend::queueAudioFrames(const int16_t* data, size_t frames)
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

    const std::size_t inputBytes = frames * 2 * sizeof(int16_t);
    if (SDL_AudioStreamPut(audioStream, data, static_cast<int>(inputBytes)) < 0) {
        return;
    }

    const std::size_t maxQueuedBytes = static_cast<std::size_t>(audioSampleRate) * 2 * sizeof(int16_t) / 5;
    const std::size_t queuedBytes = static_cast<std::size_t>(SDL_GetQueuedAudioSize(audioDevice));
    if (queuedBytes >= maxQueuedBytes) {
        return;
    }

    int available = SDL_AudioStreamAvailable(audioStream);
    if (available <= 0) {
        return;
    }

    int toQueue = static_cast<int>(maxQueuedBytes - queuedBytes);
    toQueue = std::min(toQueue, available);
    toQueue -= toQueue % static_cast<int>(2 * sizeof(int16_t));
    if (toQueue <= 0) {
        return;
    }

    audioConvertBuffer.resize(static_cast<std::size_t>(toQueue));
    const int got = SDL_AudioStreamGet(audioStream, audioConvertBuffer.data(), toQueue);
    if (got > 0) {
        SDL_QueueAudio(audioDevice, audioConvertBuffer.data(), static_cast<Uint32>(got));
    }
}

bool LibretroAtari800Backend::s_envCb(unsigned cmd, void* data)
{
    return s_instance ? s_instance->onEnvironment(cmd, data) : false;
}

void LibretroAtari800Backend::s_videoCb(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (s_instance) {
        s_instance->onVideoRefresh(data, width, height, pitch);
    }
}

void LibretroAtari800Backend::s_audioCb(int16_t left, int16_t right)
{
    if (!s_instance) {
        return;
    }

    const int16_t frame[2] = {left, right};
    s_instance->queueAudioFrames(frame, 1);
}

size_t LibretroAtari800Backend::s_audioBatchCb(const int16_t* data, size_t frames)
{
    if (s_instance && data && frames > 0) {
        s_instance->queueAudioFrames(data, frames);
    }
    return frames;
}

void LibretroAtari800Backend::s_inputPollCb()
{
}

int16_t LibretroAtari800Backend::s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (!s_instance || port != 0 || device != libretro::DEVICE_JOYPAD || index != 0) {
        return 0;
    }

    const atari800_input_t& in = s_instance->currentInput;
    switch (id) {
        case libretro::JOYPAD_UP: return in.up ? 1 : 0;
        case libretro::JOYPAD_DOWN: return in.down ? 1 : 0;
        case libretro::JOYPAD_LEFT: return in.left ? 1 : 0;
        case libretro::JOYPAD_RIGHT: return in.right ? 1 : 0;
        case libretro::JOYPAD_A: return in.fire ? 1 : 0;
        case libretro::JOYPAD_B: return in.secondary ? 1 : 0;
        case libretro::JOYPAD_SELECT: return in.select ? 1 : 0;
        case libretro::JOYPAD_START: return in.start ? 1 : 0;
        case libretro::JOYPAD_X: return in.option ? 1 : 0;
        default: return 0;
    }
}

Atari800Emulation::InputMode Atari800Emulation::globalInputMode = Atari800Emulation::InputMode::KeyboardOnly;
Atari800Emulation::KeyBindings Atari800Emulation::globalKeyBindings = {};
Atari800Emulation::ControllerBindings Atari800Emulation::globalControllerBindings = {};
bool Atari800Emulation::globalInputConfigured = false;

Atari800Emulation::Atari800Emulation()
        : isRunning(false), window(nullptr), renderer(nullptr), screenTexture(nullptr), controller(nullptr),
            controllerDeviceIndex(-1), scaleX(2), scaleY(2), displayAspectRatio(4.0 / 3.0), frameWidth(kDefaultAtari800ScreenWidth),
            frameHeight(kDefaultAtari800ScreenHeight), workerInput{},
      emulationThreadStopRequested(false), emulationThreadRunning(false),
      emulationThreadFailed(false), frameReady(false), inputMode(InputMode::KeyboardOnly)
{
    auto libretroBackend = std::make_unique<LibretroAtari800Backend>();
    if (libretroBackend->isAvailable()) {
        backend = std::move(libretroBackend);
    } else {
        backend = std::make_unique<Atari800UnavailableBackend>();
    }

    frameBuffer.resize(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);
    latestFrame.resize(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);
    setDefaultBindings();

    if (globalInputConfigured) {
        inputMode = globalInputMode;
        keyBindings = globalKeyBindings;
        controllerBindings = globalControllerBindings;
    } else {
        saveGlobalInputSettings();
    }
}

Atari800Emulation::~Atari800Emulation()
{
    unloadRom();
    cleanupSDL();
}

bool Atari800Emulation::loadRom(const string& path)
{
    if (!backend) {
        std::cerr << "Atari 800 backend not initialized.\n";
        return false;
    }

    // Validate file exists
    if (!std::filesystem::exists(path)) {
        std::cerr << "ROM file does not exist: " << path << "\n";
        return false;
    }

    const std::string lowerPath = [&path]() {
        std::string out = path;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }();
    const auto endsWith = [&lowerPath](const char* suffix) {
        const std::size_t suffixLength = std::strlen(suffix);
        return lowerPath.size() >= suffixLength &&
               lowerPath.compare(lowerPath.size() - suffixLength, suffixLength, suffix) == 0;
    };
    const bool isSupported = lowerPath.size() >= 4 && (
        endsWith(".atr") ||
        endsWith(".xfd") ||
        endsWith(".atx") ||
        endsWith(".xex") ||
        endsWith(".car") ||
        endsWith(".rom") ||
        endsWith(".cas") ||
        endsWith(".com"));
    if (!isSupported) {
        std::cerr << "Invalid file extension. Supported Atari 800 formats: .atr, .xfd, .atx, .xex, .car, .rom, .cas, .com.\n";
        return false;
    }

    // Convert to absolute path to ensure libretro core can locate the file
    const std::string absolutePath = std::filesystem::absolute(path).string();
    if (!backend->loadRom(absolutePath)) {
        return false;
    }

    frameWidth = std::max(1, backend->getFrameWidth());
    frameHeight = std::max(1, backend->getFrameHeight());
    const double backendAspectRatio = backend->getDisplayAspectRatio();
    if (backendAspectRatio > 0.1) {
        displayAspectRatio = backendAspectRatio;
    }
    frameBuffer.assign(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);
    latestFrame.assign(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);
    frameReady = false;

    romPath = path;
    isRunning = true;
    std::cout << "Successfully loaded Atari 800 ROM: " << path << "\n";
    return true;
}

void Atari800Emulation::unloadRom()
{
    stopEmulationWorker();
    isRunning = false;
    romPath.clear();
}

std::vector<unsigned char> Atari800Emulation::dumpSaveData() const
{
    return backend ? backend->dumpSaveData() : std::vector<unsigned char>{};
}

bool Atari800Emulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    return backend ? backend->restoreSaveData(data) : false;
}

void Atari800Emulation::calculateScale(int screenWidth, int screenHeight)
{
    const int displayWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(frameHeight) * displayAspectRatio)));
    int maxScaleX = (screenWidth - 50) / displayWidth;
    int maxScaleY = (screenHeight - 50) / std::max(1, frameHeight);
    int scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;
    scaleX = (scale < 1) ? 1 : scale;
    scaleY = scaleX;
}

void Atari800Emulation::syncFrameGeometry()
{
    if (!backend) {
        return;
    }

    const int backendFrameWidth = std::max(1, backend->getFrameWidth());
    const int backendFrameHeight = std::max(1, backend->getFrameHeight());
    const double backendAspectRatio = backend->getDisplayAspectRatio();
    if (backendAspectRatio > 0.1) {
        displayAspectRatio = backendAspectRatio;
    }
    if (backendFrameWidth == frameWidth && backendFrameHeight == frameHeight) {
        return;
    }

    frameWidth = backendFrameWidth;
    frameHeight = backendFrameHeight;
    frameBuffer.assign(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame.assign(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);
        frameReady = false;
    }

    if (screenTexture) {
        SDL_DestroyTexture(screenTexture);
        screenTexture = nullptr;
    }

    if (renderer) {
        screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
            frameWidth, frameHeight);
    }

    if (window) {
        SDL_DisplayMode displayMode;
        if (SDL_GetCurrentDisplayMode(0, &displayMode) == 0) {
            calculateScale(displayMode.w, displayMode.h);
        }
        const int displayWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(frameHeight) * displayAspectRatio)));
        SDL_SetWindowSize(window, displayWidth * scaleX, frameHeight * scaleY);
    }
}

void Atari800Emulation::initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    calculateScale(displayMode.w, displayMode.h);

    const int displayWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(frameHeight) * displayAspectRatio)));
    const int windowWidth = displayWidth * scaleX;
    const int windowHeight = frameHeight * scaleY;

    window = SDL_CreateWindow(
        "Atari 800 Emulator - libretro",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowWidth,
        windowHeight,
        SDL_WINDOW_SHOWN
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

    screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
        frameWidth, frameHeight);
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

void Atari800Emulation::cleanupSDL()
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

void Atari800Emulation::setDefaultBindings()
{
    keyBindings = {
        SDLK_UP,
        SDLK_DOWN,
        SDLK_LEFT,
        SDLK_RIGHT,
        SDLK_z,
        SDLK_x,
        SDLK_RSHIFT,
        SDLK_RETURN,
        SDLK_TAB
    };

    controllerBindings = {
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_X
    };
}

void Atari800Emulation::saveGlobalInputSettings()
{
    globalInputMode = inputMode;
    globalKeyBindings = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured = true;
}

void Atari800Emulation::listControllers() const
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

bool Atari800Emulation::openControllerByIndex(int index)
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

void Atari800Emulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== Atari 800 Input Configuration ===\n";
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
            struct KeyboardAction { const char* name; SDL_Keycode* key; };
            KeyboardAction actions[] = {
                {"UP", &keyBindings.up},
                {"DOWN", &keyBindings.down},
                {"LEFT", &keyBindings.left},
                {"RIGHT", &keyBindings.right},
                {"FIRE", &keyBindings.fire},
                {"SECONDARY", &keyBindings.secondary},
                {"SELECT", &keyBindings.select},
                {"START", &keyBindings.start},
                {"OPTION", &keyBindings.option}
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
            std::cout << "Enter controller button names (A,B,X,Y,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT).\n";
            std::cout << "Press Enter to keep current binding.\n";

            struct ControllerAction { const char* name; SDL_GameControllerButton* button; };
            ControllerAction actions[] = {
                {"UP", &controllerBindings.up},
                {"DOWN", &controllerBindings.down},
                {"LEFT", &controllerBindings.left},
                {"RIGHT", &controllerBindings.right},
                {"FIRE", &controllerBindings.fire},
                {"SECONDARY", &controllerBindings.secondary},
                {"SELECT", &controllerBindings.select},
                {"START", &controllerBindings.start},
                {"OPTION", &controllerBindings.option}
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
    std::cout << "Global Atari 800 controls updated.\n";
}

void Atari800Emulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, atari800_input_t& input, bool& running)
{
    if (key == SDLK_ESCAPE && pressed) {
        running = false;
        return;
    }

    if (key == keyBindings.up) input.up = pressed;
    if (key == keyBindings.down) input.down = pressed;
    if (key == keyBindings.left) input.left = pressed;
    if (key == keyBindings.right) input.right = pressed;
    if (key == keyBindings.fire) input.fire = pressed;
    if (key == keyBindings.secondary) input.secondary = pressed;
    if (key == keyBindings.select) input.select = pressed;
    if (key == keyBindings.start) input.start = pressed;
    if (key == keyBindings.option) input.option = pressed;
}

void Atari800Emulation::handleControllerButtonEvent(Uint8 button, bool pressed, atari800_input_t& input, bool& running)
{
    (void)running;

    if (button == static_cast<Uint8>(controllerBindings.up)) input.up = pressed;
    if (button == static_cast<Uint8>(controllerBindings.down)) input.down = pressed;
    if (button == static_cast<Uint8>(controllerBindings.left)) input.left = pressed;
    if (button == static_cast<Uint8>(controllerBindings.right)) input.right = pressed;
    if (button == static_cast<Uint8>(controllerBindings.fire)) input.fire = pressed;
    if (button == static_cast<Uint8>(controllerBindings.secondary)) input.secondary = pressed;
    if (button == static_cast<Uint8>(controllerBindings.select)) input.select = pressed;
    if (button == static_cast<Uint8>(controllerBindings.start)) input.start = pressed;
    if (button == static_cast<Uint8>(controllerBindings.option)) input.option = pressed;
}

void Atari800Emulation::setInput(const atari800_input_t& input)
{
    if (!backend || !isRunning.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(inputMutex);
    workerInput = input;
}

void Atari800Emulation::nextFrame()
{
    if (!backend || !isRunning.load()) {
        return;
    }

    if (!backend->nextFrame()) {
        std::cerr << "Atari 800 emulator failed to advance frame.\n";
        isRunning = false;
    }
}

atari800_color_t Atari800Emulation::getPixel(int x, int y) const
{
    if (!backend || !isRunning.load()) {
        return {0, 0, 0, 255};
    }

    if (x < 0 || x >= frameWidth || y < 0 || y >= frameHeight) {
        return {0, 0, 0, 255};
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (frameReady.load() && latestFrame.size() >= static_cast<std::size_t>(frameWidth * frameHeight * 4)) {
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(frameWidth) + static_cast<std::size_t>(x)) * 4;
            return {
                latestFrame[offset],
                latestFrame[offset + 1],
                latestFrame[offset + 2],
                latestFrame[offset + 3]
            };
        }
    }

    return {0, 0, 0, 255};
}

void Atari800Emulation::renderFrame()
{
    if (!renderer || !screenTexture) {
        return;
    }

    syncFrameGeometry();

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!frameReady.load()) {
            return;
        }
        frameBuffer = latestFrame;
    }

    if (SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), frameWidth * 4) < 0) {
        std::cerr << "Failed to update texture: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    int outputWidth = frameWidth * scaleX;
    int outputHeight = frameHeight * scaleY;
    if (SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) < 0 || outputWidth <= 0 || outputHeight <= 0) {
        outputWidth = frameWidth * scaleX;
        outputHeight = frameHeight * scaleY;
    }

    const double targetAspect = (displayAspectRatio > 0.1)
        ? displayAspectRatio
        : static_cast<double>(frameWidth) / static_cast<double>(std::max(1, frameHeight));
    const double outputAspect = static_cast<double>(outputWidth) / static_cast<double>(std::max(1, outputHeight));

    SDL_Rect destRect = {0, 0, outputWidth, outputHeight};
    if (outputAspect > targetAspect) {
        destRect.h = outputHeight;
        destRect.w = std::max(1, static_cast<int>(std::lround(static_cast<double>(outputHeight) * targetAspect)));
        destRect.x = (outputWidth - destRect.w) / 2;
    } else {
        destRect.w = outputWidth;
        destRect.h = std::max(1, static_cast<int>(std::lround(static_cast<double>(outputWidth) / targetAspect)));
        destRect.y = (outputHeight - destRect.h) / 2;
    }

    SDL_RenderCopy(renderer, screenTexture, nullptr, &destRect);
    SDL_RenderPresent(renderer);
}

void Atari800Emulation::displayFrame() const
{
    if (!backend || !isRunning) {
        return;
    }

    for (int y = 0; y < frameHeight; y += 8) {
        for (int x = 0; x < frameWidth; x += 12) {
            atari800_color_t pixel = getPixel(x, y);
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

void Atari800Emulation::configureGlobalControls()
{
    Atari800Emulation configSession;

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

void Atari800Emulation::runInteractive()
{
    if (!backend || !isRunning.load()) {
        std::cerr << "No Atari 800 ROM loaded.\n";
        return;
    }

    // Verify backend is actually available before starting emulation
    const LibretroAtari800Backend* libretroBackend = 
        dynamic_cast<const LibretroAtari800Backend*>(backend.get());
    if (libretroBackend && !libretroBackend->isAvailable()) {
        std::cerr << "Atari 800 libretro core is not available. Cannot run emulation.\n";
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

    std::cout << "\n=== Atari 800 Emulator Running ===\n";
    std::cout << "ROM: " << romPath << "\n";
    std::cout << "Window opened. Close it to exit.\n";
    std::cout << "Press ESC to exit emulator.\n\n";

    atari800_input_t input = {};
    bool running = true;
    bool windowClosed = false;

    if (!startEmulationWorker()) {
        std::cerr << "Failed to start Atari 800 emulation worker thread.\n";
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
    std::cout << "\nAtari 800 emulation stopped.\n";
}

bool Atari800Emulation::startEmulationWorker()
{
    if (emulationThreadRunning.load() || !backend || !isRunning.load()) {
        return false;
    }

    emulationThreadStopRequested = false;
    emulationThreadFailed = false;
    emulationThread = std::thread(&Atari800Emulation::emulationWorkerLoop, this);
    return true;
}

void Atari800Emulation::stopEmulationWorker()
{
    emulationThreadStopRequested = true;
    if (emulationThread.joinable()) {
        emulationThread.join();
    }
}

void Atari800Emulation::emulationWorkerLoop()
{
    emulationThreadRunning = true;

    if (!backend) {
        std::cerr << "[Atari800] Backend is null in emulation worker loop\n";
        emulationThreadRunning = false;
        emulationThreadFailed = true;
        return;
    }

    std::vector<unsigned char> localFrame(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);

    AudioMasterClock clock;
    clock.configure(backend->getAudioSampleRate(), backend->getNominalFps(), 120);

    while (!emulationThreadStopRequested.load() && isRunning.load()) {
        try {
            atari800_input_t inputSnapshot = {};
            {
                std::lock_guard<std::mutex> lock(inputMutex);
                inputSnapshot = workerInput;
            }

            backend->setInput(inputSnapshot);
            if (!backend->nextFrame()) {
                std::cerr << "[Atari800] Emulator failed to advance frame.\n";
                isRunning = false;
                emulationThreadFailed = true;
                break;
            }

            const int backendFrameWidth = std::max(1, backend->getFrameWidth());
            const int backendFrameHeight = std::max(1, backend->getFrameHeight());
            if (backendFrameWidth != frameWidth || backendFrameHeight != frameHeight) {
                frameWidth = backendFrameWidth;
                frameHeight = backendFrameHeight;
                localFrame.assign(static_cast<std::size_t>(frameWidth * frameHeight) * 4, 0);
            }
        } catch (const std::exception& e) {
            std::cerr << "[Atari800] Exception in emulation worker: " << e.what() << "\n";
            isRunning = false;
            emulationThreadFailed = true;
            break;
        } catch (...) {
            std::cerr << "[Atari800] Unknown exception in emulation worker\n";
            isRunning = false;
            emulationThreadFailed = true;
            break;
        }

        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const atari800_color_t color = backend->getPixel(x, y);
                const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(frameWidth) + static_cast<std::size_t>(x)) * 4;
                if (offset + 3 < localFrame.size()) {
                    localFrame[offset + 0] = color.r;
                    localFrame[offset + 1] = color.g;
                    localFrame[offset + 2] = color.b;
                    localFrame[offset + 3] = 255;
                }
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