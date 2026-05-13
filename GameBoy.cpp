#include "GameBoy.hpp"
#include "audio_clock.hpp"
#include "third_party/SameBoy/Core/gb.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <memory>
#include <cmath>
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

class SameBoyBackend {
public:
    virtual ~SameBoyBackend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const sameboy_input_t& input) = 0;
    virtual std::vector<unsigned char> dumpSaveData() const = 0;
    virtual bool restoreSaveData(const std::vector<unsigned char>& data) = 0;
    virtual sameboy_color_t getPixel(int x, int y) const = 0;
    virtual int getAudioSampleRate() const { return 48000; }
    virtual double getNominalFps() const { return 59.7275; }
};

class SameBoyUnavailableBackend final : public SameBoyBackend {
public:
    bool loadRom(const std::string& path) override
    {
        (void)path;
        std::cerr << "SameBoy libretro core is not available in this build. Game Boy emulation is disabled.\n";
        std::cerr << "Install or provide sameboy_libretro.so and replace SameBoyUnavailableBackend in GameBoy.cpp.\n";
        return false;
    }

    bool nextFrame() override { return false; }
    void setInput(const sameboy_input_t& input) override { (void)input; }
    std::vector<unsigned char> dumpSaveData() const override { return {}; }
    bool restoreSaveData(const std::vector<unsigned char>& data) override { (void)data; return false; }
    sameboy_color_t getPixel(int x, int y) const override { (void)x; (void)y; return {0, 0, 0, 255}; }
};

GB_model_t selectSameBoyModel(const std::string& path, const std::vector<unsigned char>& romData)
{
    const auto endsWith = [&path](const char* suffix) {
        const std::size_t suffixLength = std::strlen(suffix);
        return path.size() >= suffixLength && path.compare(path.size() - suffixLength, suffixLength, suffix) == 0;
    };

    if (endsWith(".sgb")) {
        return GB_MODEL_SGB_NTSC_NO_SFC;
    }

    if (endsWith(".gbc")) {
        return GB_MODEL_CGB_E;
    }

    if (romData.size() > 0x143) {
        const uint8_t cgbFlag = romData[0x143];
        if (cgbFlag == 0x80 || cgbFlag == 0xC0) {
            return GB_MODEL_CGB_E;
        }
    }

    return GB_MODEL_DMG_B;
}

void loadNativeSameBoyBootRom(GB_gameboy_t* gb, GB_boot_rom_t type)
{
    if (!gb) {
        return;
    }

    const char* modelName = nullptr;
    switch (type) {
        case GB_BOOT_ROM_DMG_0:
        case GB_BOOT_ROM_DMG:
            modelName = "dmg";
            break;
        case GB_BOOT_ROM_MGB:
            modelName = "mgb";
            break;
        case GB_BOOT_ROM_SGB:
            modelName = "sgb";
            break;
        case GB_BOOT_ROM_SGB2:
            modelName = "sgb2";
            break;
        case GB_BOOT_ROM_CGB_0:
            modelName = "cgb0";
            break;
        case GB_BOOT_ROM_CGB:
        case GB_BOOT_ROM_CGB_E:
            modelName = "cgb";
            break;
        case GB_BOOT_ROM_AGB_0:
        case GB_BOOT_ROM_AGB:
            modelName = "agb";
            break;
        default:
            break;
    }

    if (!modelName) {
        return;
    }

    // Prefer the fast (no-animation) boot ROM for CGB models; fall back to
    // the standard boot ROM if the fast variant is not available.
    if (std::strcmp(modelName, "cgb") == 0) {
        const std::string fastPath = "third_party/SameBoy/build/bin/BootROMs/cgb_boot_fast.bin";
        if (GB_load_boot_rom(gb, fastPath.c_str()) == 0) {
            return;
        }
    }

    const std::string path = std::string("third_party/SameBoy/build/bin/BootROMs/") + modelName + "_boot.bin";
    if (GB_load_boot_rom(gb, path.c_str()) != 0) {
        std::cerr << "[GB] Failed to load SameBoy boot ROM: " << path << "\n";
    }
}

class NativeSameBoyBackend final : public SameBoyBackend {
public:
    NativeSameBoyBackend();
    ~NativeSameBoyBackend() override;

    bool loadRom(const std::string& path) override;
    bool nextFrame() override;
    void setInput(const sameboy_input_t& input) override;
    std::vector<unsigned char> dumpSaveData() const override;
    bool restoreSaveData(const std::vector<unsigned char>& data) override;
    sameboy_color_t getPixel(int x, int y) const override;
    int getAudioSampleRate() const override { return audioSampleRate; }
    double getNominalFps() const override { return coreFps; }

private:
    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioFrames(const int16_t* data, size_t frames);
    void onAudioPull(Uint8* stream, int len);
    void onAudioSample(const GB_sample_t& sample);
    const int16_t* applyRateControl(const int16_t* data, size_t frames, size_t& outFrames, int bufferedFramesBefore);

    static uint32_t s_rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b);
    static void s_audioSampleCb(GB_gameboy_t* gb, GB_sample_t* sample);
    static void sdlAudioPullCb(void* userdata, Uint8* stream, int len);

    GB_gameboy_t* gameBoy = nullptr;
    std::vector<unsigned char> romData;
    std::vector<uint32_t> framePixels;
    std::vector<int16_t> pendingAudioFrames;
    sameboy_input_t currentInput = {};
    bool gameLoaded = false;
    int frameWidth = 160;
    int frameHeight = 144;
    SDL_AudioDeviceID audioDevice = 0;
    int audioSampleRate = 48000;
    double coreFps = 59.7275;
    bool audioPrimed = false;
    int targetPrebufferMs = 120;
    int underrunEvents = 0;
    int stablePulls = 0;
    std::vector<int16_t> audioRing;
    size_t audioRingRead = 0;
    size_t audioRingWrite = 0;
    size_t audioRingCount = 0;
    int16_t lastAudioLeft = 0;
    int16_t lastAudioRight = 0;
    std::vector<int16_t> rateAdjustedBatch;
    double rateControlPhase = 0.0;
    double rateControlRatio = 1.0;
};

NativeSameBoyBackend::NativeSameBoyBackend()
{
    pendingAudioFrames.reserve(4096);
    ensureAudioDevice(audioSampleRate);
}

NativeSameBoyBackend::~NativeSameBoyBackend()
{
    closeAudioDevice();
    if (gameBoy) {
        GB_dealloc(gameBoy);
        gameBoy = nullptr;
    }
}

bool NativeSameBoyBackend::loadRom(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[GB] Cannot open ROM: " << path << "\n";
        return false;
    }

    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    romData.resize(size);
    if (!file.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
        std::cerr << "[GB] Failed to read ROM: " << path << "\n";
        romData.clear();
        return false;
    }

    if (gameBoy) {
        GB_dealloc(gameBoy);
        gameBoy = nullptr;
    }

    gameBoy = GB_init(GB_alloc(), selectSameBoyModel(path, romData));
    if (!gameBoy) {
        std::cerr << "[GB] Failed to initialize native SameBoy core.\n";
        romData.clear();
        return false;
    }

    if (!ensureAudioDevice(audioSampleRate)) {
        std::cerr << "[GB] Continuing without audio output.\n";
    }

    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight), 0);
    pendingAudioFrames.clear();
    currentInput = {};

    GB_set_user_data(gameBoy, this);
    GB_set_boot_rom_load_callback(gameBoy, loadNativeSameBoyBootRom);
    GB_set_rgb_encode_callback(gameBoy, s_rgbEncode);
    GB_set_pixels_output(gameBoy, framePixels.data());
    GB_set_border_mode(gameBoy, GB_BORDER_NEVER);
    GB_set_color_correction_mode(gameBoy, GB_COLOR_CORRECTION_DISABLED);
    GB_set_highpass_filter_mode(gameBoy, GB_HIGHPASS_REMOVE_DC_OFFSET);
    GB_set_interference_volume(gameBoy, 0.0);
    GB_set_sample_rate(gameBoy, static_cast<unsigned>(audioSampleRate));
    GB_apu_set_sample_callback(gameBoy, s_audioSampleCb);
    GB_load_rom_from_buffer(gameBoy, romData.data(), romData.size());

    frameWidth = static_cast<int>(GB_get_screen_width(gameBoy));
    frameHeight = static_cast<int>(GB_get_screen_height(gameBoy));
    coreFps = GB_get_usual_frame_rate(gameBoy);
    if (frameWidth <= 0 || frameHeight <= 0) {
        frameWidth = 160;
        frameHeight = 144;
    }

    framePixels.assign(static_cast<size_t>(frameWidth * frameHeight), 0);
    GB_set_pixels_output(gameBoy, framePixels.data());

    if (audioDevice != 0) {
        SDL_LockAudioDevice(audioDevice);
        audioRingRead = 0;
        audioRingWrite = 0;
        audioRingCount = 0;
        lastAudioLeft = 0;
        lastAudioRight = 0;
        audioPrimed = false;
        targetPrebufferMs = 120;
        underrunEvents = 0;
        stablePulls = 0;
        SDL_PauseAudioDevice(audioDevice, 1);
        SDL_UnlockAudioDevice(audioDevice);
    }

    gameLoaded = true;
    return true;
}

bool NativeSameBoyBackend::nextFrame()
{
    if (!gameBoy || !gameLoaded) {
        return false;
    }

    GB_set_key_state(gameBoy, GB_KEY_UP, currentInput.up);
    GB_set_key_state(gameBoy, GB_KEY_DOWN, currentInput.down);
    GB_set_key_state(gameBoy, GB_KEY_LEFT, currentInput.left);
    GB_set_key_state(gameBoy, GB_KEY_RIGHT, currentInput.right);
    GB_set_key_state(gameBoy, GB_KEY_A, currentInput.a);
    GB_set_key_state(gameBoy, GB_KEY_B, currentInput.b);
    GB_set_key_state(gameBoy, GB_KEY_SELECT, currentInput.select);
    GB_set_key_state(gameBoy, GB_KEY_START, currentInput.start);

    pendingAudioFrames.clear();
    GB_run_frame(gameBoy);
    queueAudioFrames(pendingAudioFrames.data(), pendingAudioFrames.size() / 2);
    return true;
}

void NativeSameBoyBackend::setInput(const sameboy_input_t& input)
{
    currentInput = input;
}

std::vector<unsigned char> NativeSameBoyBackend::dumpSaveData() const
{
    if (!gameBoy || !gameLoaded) {
        return {};
    }

    const size_t saveSize = GB_get_save_state_size(gameBoy);
    if (saveSize == 0) {
        return {};
    }

    std::vector<unsigned char> saveData(saveSize);
    GB_save_state_to_buffer(gameBoy, saveData.data());
    return saveData;
}

bool NativeSameBoyBackend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!gameBoy || !gameLoaded || data.empty()) {
        return false;
    }

    return GB_load_state_from_buffer(gameBoy, data.data(), data.size()) == 0;
}

sameboy_color_t NativeSameBoyBackend::getPixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= frameWidth || y >= frameHeight) {
        return {0, 0, 0, 255};
    }

    const uint32_t pixel = framePixels[static_cast<size_t>(y) * static_cast<size_t>(frameWidth) + static_cast<size_t>(x)];
    return {
        static_cast<unsigned char>(pixel & 0xFF),
        static_cast<unsigned char>((pixel >> 8) & 0xFF),
        static_cast<unsigned char>((pixel >> 16) & 0xFF),
        static_cast<unsigned char>((pixel >> 24) & 0xFF)
    };
}

bool NativeSameBoyBackend::ensureAudioDevice(int sampleRate)
{
    if (sampleRate <= 0) {
        sampleRate = 48000;
    }

    if (audioDevice != 0 && sampleRate == audioSampleRate) {
        return true;
    }

    closeAudioDevice();

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[GB] Failed to initialize SDL audio subsystem: " << SDL_GetError() << "\n";
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
    desired.samples = 2048;
    desired.callback = sdlAudioPullCb;
    desired.userdata = this;

    SDL_AudioSpec obtained = {};
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) {
        std::cerr << "[GB] Failed to open SDL audio device: " << SDL_GetError() << "\n";
        return false;
    }

    audioSampleRate = obtained.freq > 0 ? obtained.freq : sampleRate;
    const std::size_t ringFrames = std::max<std::size_t>(4096, static_cast<std::size_t>(audioSampleRate) / 2);
    audioRing.assign(ringFrames * 2, 0);
    audioRingRead = 0;
    audioRingWrite = 0;
    audioRingCount = 0;
    audioPrimed = false;
    targetPrebufferMs = 120;
    underrunEvents = 0;
    stablePulls = 0;
    lastAudioLeft = 0;
    lastAudioRight = 0;
    SDL_PauseAudioDevice(audioDevice, 1);
    return true;
}

void NativeSameBoyBackend::closeAudioDevice()
{
    audioRing.clear();
    audioRingRead = 0;
    audioRingWrite = 0;
    audioRingCount = 0;
    audioPrimed = false;
    lastAudioLeft = 0;
    lastAudioRight = 0;
    rateAdjustedBatch.clear();
    rateControlPhase = 0.0;
    rateControlRatio = 1.0;

    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }
}

const int16_t* NativeSameBoyBackend::applyRateControl(const int16_t* data, size_t frames, size_t& outFrames, int bufferedFramesBefore)
{
    outFrames = frames;
    if (!data || frames == 0) {
        return data;
    }

    const int targetFrames = std::max(1, (audioSampleRate * targetPrebufferMs) / 1000);
    const int errorFrames = targetFrames - bufferedFramesBefore;
    const double normalizedError = std::clamp(
        static_cast<double>(errorFrames) / static_cast<double>(targetFrames),
        -1.0,
        1.0);

    constexpr double kMaxDelta = 0.0015;
    const double targetRatio = std::clamp(1.0 + (normalizedError * kMaxDelta), 1.0 - kMaxDelta, 1.0 + kMaxDelta);
    rateControlRatio = (rateControlRatio * 0.98) + (targetRatio * 0.02);

    if (std::abs(rateControlRatio - 1.0) < 0.0002) {
        outFrames = frames;
        rateControlPhase = 0.0;
        return data;
    }

    const std::size_t targetOutFrames = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(static_cast<double>(frames) * rateControlRatio)));
    const double srcStep = 1.0 / rateControlRatio;

    rateAdjustedBatch.resize(targetOutFrames * 2);
    double srcPos = rateControlPhase;
    for (std::size_t out = 0; out < targetOutFrames; ++out) {
        std::size_t idx = static_cast<std::size_t>(srcPos);
        double frac = srcPos - static_cast<double>(idx);

        if (idx >= frames - 1) {
            idx = frames - 1;
            frac = 0.0;
        }

        const std::size_t nextIdx = std::min<std::size_t>(idx + 1, frames - 1);
        const int32_t l0 = data[idx * 2];
        const int32_t r0 = data[idx * 2 + 1];
        const int32_t l1 = data[nextIdx * 2];
        const int32_t r1 = data[nextIdx * 2 + 1];

        const double l = static_cast<double>(l0) + (static_cast<double>(l1 - l0) * frac);
        const double r = static_cast<double>(r0) + (static_cast<double>(r1 - r0) * frac);

        rateAdjustedBatch[out * 2] = static_cast<int16_t>(std::clamp(l, -32768.0, 32767.0));
        rateAdjustedBatch[out * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768.0, 32767.0));

        srcPos += srcStep;
    }

    rateControlPhase = srcPos - std::floor(srcPos);
    outFrames = targetOutFrames;
    return rateAdjustedBatch.data();
}

void NativeSameBoyBackend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0 || audioDevice == 0) {
        return;
    }

    SDL_LockAudioDevice(audioDevice);

    const std::size_t capacityFrames = audioRing.size() / 2;
    if (capacityFrames == 0) {
        SDL_UnlockAudioDevice(audioDevice);
        return;
    }

    const int bufferedFramesBefore = static_cast<int>(audioRingCount);
    size_t adjustedFrames = frames;
    const int16_t* adjustedData = applyRateControl(data, frames, adjustedFrames, bufferedFramesBefore);

    for (std::size_t i = 0; i < adjustedFrames; ++i) {
        if (audioRingCount >= capacityFrames) {
            audioRingRead = (audioRingRead + 1) % capacityFrames;
            --audioRingCount;
        }

        audioRing[audioRingWrite * 2] = adjustedData[i * 2];
        audioRing[audioRingWrite * 2 + 1] = adjustedData[i * 2 + 1];
        audioRingWrite = (audioRingWrite + 1) % capacityFrames;
        ++audioRingCount;
    }

    if (underrunEvents > 0) {
        targetPrebufferMs = std::min(220, targetPrebufferMs + 20);
        underrunEvents = 0;
        stablePulls = 0;
    } else if (stablePulls > 300 && targetPrebufferMs > 90) {
        targetPrebufferMs = std::max(90, targetPrebufferMs - 10);
        stablePulls = 0;
    }

    const int prebufferFrames = (audioSampleRate * targetPrebufferMs) / 1000;
    if (!audioPrimed && static_cast<int>(audioRingCount) >= prebufferFrames) {
        audioPrimed = true;
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    SDL_UnlockAudioDevice(audioDevice);
}

void NativeSameBoyBackend::onAudioPull(Uint8* stream, int len)
{
    std::memset(stream, 0, static_cast<std::size_t>(len));
    if (!audioPrimed || audioRing.empty()) {
        return;
    }

    const int frameBytes = 2 * static_cast<int>(sizeof(int16_t));
    const std::size_t requestedFrames = static_cast<std::size_t>(len / frameBytes);
    auto* out = reinterpret_cast<int16_t*>(stream);
    const std::size_t capacityFrames = audioRing.size() / 2;
    const std::size_t toCopyFrames = std::min<std::size_t>(requestedFrames, audioRingCount);

    for (std::size_t i = 0; i < toCopyFrames; ++i) {
        out[i * 2] = audioRing[audioRingRead * 2];
        out[i * 2 + 1] = audioRing[audioRingRead * 2 + 1];
        lastAudioLeft = out[i * 2];
        lastAudioRight = out[i * 2 + 1];
        audioRingRead = (audioRingRead + 1) % capacityFrames;
    }
    audioRingCount -= toCopyFrames;

    if (toCopyFrames < requestedFrames) {
        for (std::size_t i = toCopyFrames; i < requestedFrames; ++i) {
            out[i * 2] = lastAudioLeft;
            out[i * 2 + 1] = lastAudioRight;
        }
        ++underrunEvents;
        stablePulls = 0;
        audioPrimed = false;
    } else if (stablePulls < 10000) {
        ++stablePulls;
    }
}

void NativeSameBoyBackend::onAudioSample(const GB_sample_t& sample)
{
    pendingAudioFrames.push_back(sample.left);
    pendingAudioFrames.push_back(sample.right);
}

uint32_t NativeSameBoyBackend::s_rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           0xFF000000u;
}

void NativeSameBoyBackend::s_audioSampleCb(GB_gameboy_t* gb, GB_sample_t* sample)
{
    auto* backend = static_cast<NativeSameBoyBackend*>(GB_get_user_data(gb));
    if (backend && sample) {
        backend->onAudioSample(*sample);
    }
}

void NativeSameBoyBackend::sdlAudioPullCb(void* userdata, Uint8* stream, int len)
{
    auto* backend = static_cast<NativeSameBoyBackend*>(userdata);
    if (backend) {
        backend->onAudioPull(stream, len);
    }
}

namespace libretro {
    static constexpr unsigned ENV_GET_CAN_DUPE        = 3;
    static constexpr unsigned ENV_SET_PIXEL_FORMAT    = 10;
    static constexpr unsigned ENV_GET_VARIABLE        = 15;
    static constexpr unsigned ENV_SET_VARIABLES       = 16;
    static constexpr unsigned ENV_GET_VARIABLE_UPDATE = 17;
    static constexpr unsigned ENV_SET_SUPPORT_NO_GAME  = 18;
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

class LibretroSameBoyBackend final : public SameBoyBackend {
public:
    static constexpr const char* kCorePath = "/usr/lib/x86_64-linux-gnu/libretro/sameboy_libretro.so";

    LibretroSameBoyBackend();
    ~LibretroSameBoyBackend() override;

    bool isAvailable() const { return coreHandle != nullptr; }

    bool loadRom(const std::string& path) override;
    bool nextFrame() override;
    void setInput(const sameboy_input_t& input) override;
    std::vector<unsigned char> dumpSaveData() const override;
    bool restoreSaveData(const std::vector<unsigned char>& data) override;
    sameboy_color_t getPixel(int x, int y) const override;
    int getAudioSampleRate() const override { return audioSampleRate; }
    double getNominalFps() const override { return coreFps; }

private:
    bool loadSymbols();
    bool onEnvironment(unsigned cmd, void* data);
    void onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);
    bool ensureAudioDevice(int sampleRate);
    void closeAudioDevice();
    void queueAudioFrames(const int16_t* data, size_t frames);
    void onAudioPull(Uint8* stream, int len);
    void queueSingleSample(int16_t left, int16_t right);
    void flushSingleSampleBatch();
    const int16_t* applyRateControl(const int16_t* data, size_t frames, size_t& outFrames, int bufferedFramesBefore);

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

    std::vector<unsigned char> romData;
    std::vector<unsigned char> framePixels;
    sameboy_input_t currentInput = {};
    unsigned pixelFormat = libretro::PIXEL_0RGB1555;
    int frameWidth = 160;
    int frameHeight = 144;
    bool gameLoaded = false;
    SDL_AudioDeviceID audioDevice = 0;
    SDL_AudioStream* audioStream = nullptr;
    int coreSampleRate = 48000;
    int audioSampleRate = 48000;
    double coreFps = 59.7275;
    std::vector<unsigned char> audioConvertBuffer;
    bool audioPrimed = false;
    int targetPrebufferMs = 120;
    int underrunEvents = 0;
    int stablePulls = 0;
    std::vector<int16_t> singleSampleBatch;
    std::vector<int16_t> rateAdjustedBatch;
    double rateControlPhase = 0.0;
    double rateControlRatio = 1.0;
    bool useDirectRing = true;
    std::vector<int16_t> audioRing;
    size_t audioRingRead = 0;
    size_t audioRingWrite = 0;
    size_t audioRingCount = 0;
    int16_t lastAudioLeft = 0;
    int16_t lastAudioRight = 0;

    static LibretroSameBoyBackend* s_instance;
    static bool s_envCb(unsigned cmd, void* data);
    static void s_videoCb(const void* data, unsigned width, unsigned height, size_t pitch);
    static void s_audioCb(int16_t left, int16_t right);
    static size_t s_audioBatchCb(const int16_t* data, size_t frames);
    static void s_inputPollCb();
    static int16_t s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);
    static void sdlAudioPullCb(void* userdata, Uint8* stream, int len);
};

LibretroSameBoyBackend* LibretroSameBoyBackend::s_instance = nullptr;

LibretroSameBoyBackend::LibretroSameBoyBackend()
{
    coreHandle = dlopen(kCorePath, RTLD_LAZY);
    if (!coreHandle) {
        std::cerr << "[GB] Failed to load SameBoy core: " << dlerror() << "\n";
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

LibretroSameBoyBackend::~LibretroSameBoyBackend()
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

bool LibretroSameBoyBackend::loadSymbols()
{
    auto load = [this](const char* name) -> void* {
        void* sym = dlsym(coreHandle, name);
        if (!sym) {
            std::cerr << "[GB] Missing symbol: " << name << "\n";
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

bool LibretroSameBoyBackend::onEnvironment(unsigned cmd, void* data)
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
            frameWidth = static_cast<int>(geom->base_width);
            frameHeight = static_cast<int>(geom->base_height);
            framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
            return true;
        }
        default:
            return false;
    }
}

void LibretroSameBoyBackend::onVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) {
        return;
    }

    if (static_cast<int>(width) != frameWidth || static_cast<int>(height) != frameHeight) {
        frameWidth = static_cast<int>(width);
        frameHeight = static_cast<int>(height);
        framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    }

    const auto* src = static_cast<const uint8_t*>(data);
    for (unsigned row = 0; row < height; ++row) {
        const uint8_t* srcRow = src + row * pitch;
        uint8_t* dstRow = framePixels.data() + row * static_cast<size_t>(frameWidth) * 4;
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
                dstRow[col * 4 + 0] = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
                dstRow[col * 4 + 1] = static_cast<uint8_t>(((px >> 5) & 0x3F) * 255 / 63);
                dstRow[col * 4 + 2] = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
                dstRow[col * 4 + 3] = 255;
            }
        } else {
            for (unsigned col = 0; col < width; ++col) {
                uint16_t px;
                std::memcpy(&px, srcRow + col * 2, 2);
                dstRow[col * 4 + 0] = static_cast<uint8_t>(((px >> 10) & 0x1F) * 255 / 31);
                dstRow[col * 4 + 1] = static_cast<uint8_t>(((px >> 5) & 0x1F) * 255 / 31);
                dstRow[col * 4 + 2] = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
                dstRow[col * 4 + 3] = 255;
            }
        }
    }
}

bool LibretroSameBoyBackend::loadRom(const std::string& path)
{
    if (!coreHandle) {
        return false;
    }

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
            std::cerr << "[GB] Cannot open ROM: " << path << "\n";
            return false;
        }

        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        romData.resize(size);
        if (!file.read(reinterpret_cast<char*>(romData.data()), static_cast<std::streamsize>(size))) {
            std::cerr << "[GB] Failed to read ROM: " << path << "\n";
            romData.clear();
            return false;
        }
        gameInfo.data = romData.data();
        gameInfo.size = romData.size();
    }

    if (!fn_load_game(&gameInfo)) {
        std::cerr << "[GB] Core rejected ROM: " << path << "\n";
        romData.clear();
        return false;
    }

    gameLoaded = true;
    libretro::av_info avInfo = {};
    fn_get_system_av_info(&avInfo);
    frameWidth = static_cast<int>(avInfo.geometry.base_width);
    frameHeight = static_cast<int>(avInfo.geometry.base_height);
    if (avInfo.timing.fps > 1.0) {
        coreFps = avInfo.timing.fps;
    }
    framePixels.resize(static_cast<size_t>(frameWidth * frameHeight) * 4, 0);
    ensureAudioDevice(static_cast<int>(avInfo.timing.sample_rate));
    return true;
}

bool LibretroSameBoyBackend::nextFrame()
{
    if (!coreHandle || !gameLoaded) {
        return false;
    }

    fn_run();
    flushSingleSampleBatch();
    return true;
}

void LibretroSameBoyBackend::setInput(const sameboy_input_t& input)
{
    currentInput = input;
}

std::vector<unsigned char> LibretroSameBoyBackend::dumpSaveData() const
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

bool LibretroSameBoyBackend::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!coreHandle || !gameLoaded || !fn_unserialize || data.empty()) {
        return false;
    }

    return fn_unserialize(data.data(), data.size());
}

sameboy_color_t LibretroSameBoyBackend::getPixel(int x, int y) const
{
    if (x < 0 || x >= frameWidth || y < 0 || y >= frameHeight) {
        return {0, 0, 0, 255};
    }

    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(frameWidth) + static_cast<size_t>(x)) * 4;
    return {framePixels[offset], framePixels[offset + 1], framePixels[offset + 2], framePixels[offset + 3]};
}

bool LibretroSameBoyBackend::s_envCb(unsigned cmd, void* data)
{
    return s_instance ? s_instance->onEnvironment(cmd, data) : false;
}

void LibretroSameBoyBackend::s_videoCb(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (s_instance) {
        s_instance->onVideoRefresh(data, width, height, pitch);
    }
}

void LibretroSameBoyBackend::s_audioCb(int16_t left, int16_t right)
{
    if (!s_instance) {
        return;
    }

    s_instance->queueSingleSample(left, right);
}

size_t LibretroSameBoyBackend::s_audioBatchCb(const int16_t* data, size_t frames)
{
    if (s_instance && data && frames > 0) {
        s_instance->flushSingleSampleBatch();
        s_instance->queueAudioFrames(data, frames);
    }
    return frames;
}

void LibretroSameBoyBackend::queueSingleSample(int16_t left, int16_t right)
{
    singleSampleBatch.push_back(left);
    singleSampleBatch.push_back(right);

    // Batch tiny sample callbacks to reduce synchronization overhead.
    static constexpr std::size_t kBatchFrames = 256;
    if (singleSampleBatch.size() >= kBatchFrames * 2) {
        flushSingleSampleBatch();
    }
}

void LibretroSameBoyBackend::flushSingleSampleBatch()
{
    if (singleSampleBatch.empty()) {
        return;
    }

    queueAudioFrames(singleSampleBatch.data(), singleSampleBatch.size() / 2);
    singleSampleBatch.clear();
}

bool LibretroSameBoyBackend::ensureAudioDevice(int sampleRate)
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
            std::cerr << "[GB] Failed to initialize SDL audio subsystem: " << SDL_GetError() << "\n";
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
    desired.samples = 2048;
    desired.callback = sdlAudioPullCb;
    desired.userdata = this;

    SDL_AudioSpec obtained = {};
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) {
        std::cerr << "[GB] Failed to open SDL audio device: " << SDL_GetError() << "\n";
        return false;
    }

    coreSampleRate = sampleRate;
    audioSampleRate = obtained.freq;
    useDirectRing = (obtained.format == AUDIO_S16SYS) &&
                    (obtained.channels == 2) &&
                    (obtained.freq == coreSampleRate);

    if (useDirectRing) {
        const std::size_t ringFrames = std::max<std::size_t>(
            4096,
            static_cast<std::size_t>(audioSampleRate) / 2);
        audioRing.assign(ringFrames * 2, 0);
        audioRingRead = 0;
        audioRingWrite = 0;
        audioRingCount = 0;
    } else {
        audioStream = SDL_NewAudioStream(AUDIO_S16SYS, 2, coreSampleRate, AUDIO_S16SYS, 2, audioSampleRate);
        if (!audioStream) {
            std::cerr << "[GB] Failed to create SDL audio stream: " << SDL_GetError() << "\n";
            closeAudioDevice();
            return false;
        }
    }

    // Start paused and wait for a small prebuffer to avoid startup crackle.
    audioPrimed = false;
    targetPrebufferMs = 120;
    underrunEvents = 0;
    stablePulls = 0;
    singleSampleBatch.clear();
    lastAudioLeft = 0;
    lastAudioRight = 0;
    SDL_PauseAudioDevice(audioDevice, 1);
    return true;
}

void LibretroSameBoyBackend::closeAudioDevice()
{
    if (audioStream) {
        SDL_FreeAudioStream(audioStream);
        audioStream = nullptr;
    }

    audioRing.clear();
    audioRingRead = 0;
    audioRingWrite = 0;
    audioRingCount = 0;

    if (audioDevice != 0) {
        SDL_ClearQueuedAudio(audioDevice);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }

    audioPrimed = false;
    singleSampleBatch.clear();
    rateAdjustedBatch.clear();
    rateControlPhase = 0.0;
    rateControlRatio = 1.0;
    lastAudioLeft = 0;
    lastAudioRight = 0;
}

const int16_t* LibretroSameBoyBackend::applyRateControl(const int16_t* data, size_t frames, size_t& outFrames, int bufferedFramesBefore)
{
    outFrames = frames;
    if (!data || frames == 0) {
        return data;
    }

    const int targetFrames = std::max(1, (audioSampleRate * targetPrebufferMs) / 1000);
    const int errorFrames = targetFrames - bufferedFramesBefore;
    const double normalizedError = std::clamp(
        static_cast<double>(errorFrames) / static_cast<double>(targetFrames),
        -1.0,
        1.0);

    // Tiny PLL-like correction to keep buffer near target without audible pitch wobble.
    constexpr double kMaxDelta = 0.0015;
    const double targetRatio = std::clamp(1.0 + (normalizedError * kMaxDelta), 1.0 - kMaxDelta, 1.0 + kMaxDelta);
    // Smooth corrections to avoid short-term ratio flutter.
    rateControlRatio = (rateControlRatio * 0.98) + (targetRatio * 0.02);

    if (std::abs(rateControlRatio - 1.0) < 0.0002) {
        outFrames = frames;
        rateControlPhase = 0.0;
        return data;
    }

    const std::size_t targetOutFrames = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(static_cast<double>(frames) * rateControlRatio)));
    const double srcStep = 1.0 / rateControlRatio;

    rateAdjustedBatch.resize(targetOutFrames * 2);
    double srcPos = rateControlPhase;
    for (std::size_t out = 0; out < targetOutFrames; ++out) {
        std::size_t idx = static_cast<std::size_t>(srcPos);
        double frac = srcPos - static_cast<double>(idx);

        if (idx >= frames - 1) {
            idx = frames - 1;
            frac = 0.0;
        }

        const std::size_t nextIdx = std::min<std::size_t>(idx + 1, frames - 1);
        const int32_t l0 = data[idx * 2];
        const int32_t r0 = data[idx * 2 + 1];
        const int32_t l1 = data[nextIdx * 2];
        const int32_t r1 = data[nextIdx * 2 + 1];

        const double l = static_cast<double>(l0) + (static_cast<double>(l1 - l0) * frac);
        const double r = static_cast<double>(r0) + (static_cast<double>(r1 - r0) * frac);

        rateAdjustedBatch[out * 2] = static_cast<int16_t>(std::clamp(l, -32768.0, 32767.0));
        rateAdjustedBatch[out * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768.0, 32767.0));

        srcPos += srcStep;
    }

    rateControlPhase = srcPos - std::floor(srcPos);
    outFrames = targetOutFrames;
    return rateAdjustedBatch.data();
}

void LibretroSameBoyBackend::queueAudioFrames(const int16_t* data, size_t frames)
{
    if (!data || frames == 0) {
        return;
    }

    if (audioDevice == 0 && !ensureAudioDevice(audioSampleRate)) {
        return;
    }

    SDL_LockAudioDevice(audioDevice);

    const int frameBytes = 2 * static_cast<int>(sizeof(int16_t));
    const int bufferedFramesBefore = useDirectRing
        ? static_cast<int>(audioRingCount)
        : (audioStream ? (SDL_AudioStreamAvailable(audioStream) / frameBytes) : 0);

    size_t adjustedFrames = frames;
    const int16_t* adjustedData = applyRateControl(data, frames, adjustedFrames, bufferedFramesBefore);

    if (useDirectRing) {
        const std::size_t capacityFrames = audioRing.size() / 2;
        if (capacityFrames == 0) {
            SDL_UnlockAudioDevice(audioDevice);
            return;
        }

        for (std::size_t i = 0; i < adjustedFrames; ++i) {
            if (audioRingCount >= capacityFrames) {
                audioRingRead = (audioRingRead + 1) % capacityFrames;
                --audioRingCount;
            }

            audioRing[audioRingWrite * 2] = adjustedData[i * 2];
            audioRing[audioRingWrite * 2 + 1] = adjustedData[i * 2 + 1];
            audioRingWrite = (audioRingWrite + 1) % capacityFrames;
            ++audioRingCount;
        }
    } else {
        if (!audioStream) {
            SDL_UnlockAudioDevice(audioDevice);
            return;
        }

        const size_t inputBytes = adjustedFrames * static_cast<size_t>(frameBytes);
        if (SDL_AudioStreamPut(audioStream, adjustedData, static_cast<int>(inputBytes)) < 0) {
            SDL_UnlockAudioDevice(audioDevice);
            return;
        }
    }

    if (underrunEvents > 0) {
        // React quickly to crackle by increasing jitter buffer depth.
        targetPrebufferMs = std::min(240, targetPrebufferMs + 20);
        underrunEvents = 0;
        stablePulls = 0;
    } else if (stablePulls > 300 && targetPrebufferMs > 90) {
        // Relax latency slowly after sustained stable playback.
        targetPrebufferMs = std::max(90, targetPrebufferMs - 10);
        stablePulls = 0;
    }

    const int prebufferFrames = (audioSampleRate * targetPrebufferMs) / 1000;
    int bufferedFrames = useDirectRing
        ? static_cast<int>(audioRingCount)
        : (SDL_AudioStreamAvailable(audioStream) / frameBytes);

    if (!audioPrimed && bufferedFrames >= prebufferFrames) {
        audioPrimed = true;
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    const int maxBufferedMs = std::min(420, targetPrebufferMs + 180);
    const int maxBufferedFrames = (audioSampleRate * maxBufferedMs) / 1000;
    if (bufferedFrames > maxBufferedFrames) {
        const int trimTargetMs = std::min(maxBufferedMs - 20, targetPrebufferMs + 80);
        const int trimToFrames = (audioSampleRate * trimTargetMs) / 1000;
        int dropFrames = bufferedFrames - trimToFrames;
        if (dropFrames > 0) {
            if (useDirectRing) {
                const std::size_t capacityFrames = audioRing.size() / 2;
                const std::size_t clampedDrop = std::min<std::size_t>(
                    static_cast<std::size_t>(dropFrames),
                    audioRingCount);
                audioRingRead = (audioRingRead + clampedDrop) % capacityFrames;
                audioRingCount -= clampedDrop;
            } else {
                int dropBytes = dropFrames * frameBytes;
                dropBytes = (dropBytes / frameBytes) * frameBytes;
                if (dropBytes > 0) {
                    audioConvertBuffer.resize(static_cast<std::size_t>(dropBytes));
                    SDL_AudioStreamGet(audioStream, audioConvertBuffer.data(), dropBytes);
                }
            }
        }
    }

    SDL_UnlockAudioDevice(audioDevice);
}

void LibretroSameBoyBackend::onAudioPull(Uint8* stream, int len)
{
    if (!stream || len <= 0) {
        return;
    }

    std::memset(stream, 0, static_cast<std::size_t>(len));
    if (!audioPrimed) {
        return;
    }

    const int frameBytes = 2 * static_cast<int>(sizeof(int16_t));
    const int requested = len - (len % frameBytes);
    if (requested <= 0) {
        return;
    }

    if (useDirectRing) {
        const std::size_t requestedFrames = static_cast<std::size_t>(requested / frameBytes);
        if (audioRing.empty()) {
            ++underrunEvents;
            stablePulls = 0;
            audioPrimed = false;
            return;
        }

        const std::size_t capacityFrames = audioRing.size() / 2;
        std::size_t toCopyFrames = std::min<std::size_t>(requestedFrames, audioRingCount);
        auto* out = reinterpret_cast<int16_t*>(stream);
        for (std::size_t i = 0; i < toCopyFrames; ++i) {
            out[i * 2] = audioRing[audioRingRead * 2];
            out[i * 2 + 1] = audioRing[audioRingRead * 2 + 1];
            lastAudioLeft = out[i * 2];
            lastAudioRight = out[i * 2 + 1];
            audioRingRead = (audioRingRead + 1) % capacityFrames;
        }

        audioRingCount -= toCopyFrames;

        if (toCopyFrames < requestedFrames) {
            for (std::size_t i = toCopyFrames; i < requestedFrames; ++i) {
                out[i * 2] = lastAudioLeft;
                out[i * 2 + 1] = lastAudioRight;
            }
            ++underrunEvents;
            stablePulls = 0;
            // Any underrun means we're too close to starvation; force re-prime.
            audioPrimed = false;
        } else {
            if (stablePulls < 10000) {
                ++stablePulls;
            }
        }
        return;
    }

    if (!audioStream) {
        return;
    }

    const int got = SDL_AudioStreamGet(audioStream, stream, requested);
    if (got < requested && got > 0) {
        std::memset(stream + got, 0, static_cast<std::size_t>(len - got));
        // Partial reads still crackle in practice; re-prime to rebuild headroom.
        audioPrimed = false;
        ++underrunEvents;
        stablePulls = 0;
    } else if (got <= 0) {
        // Re-prime after starvation to prevent repeated tiny underruns.
        audioPrimed = false;
        ++underrunEvents;
        stablePulls = 0;
    } else {
        if (stablePulls < 10000) {
            ++stablePulls;
        }
    }
}

void LibretroSameBoyBackend::sdlAudioPullCb(void* userdata, Uint8* stream, int len)
{
    auto* backend = static_cast<LibretroSameBoyBackend*>(userdata);
    if (backend) {
        backend->onAudioPull(stream, len);
    }
}

void LibretroSameBoyBackend::s_inputPollCb()
{
}

int16_t LibretroSameBoyBackend::s_inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (!s_instance || port != 0 || device != libretro::DEVICE_JOYPAD || index != 0) {
        return 0;
    }

    const sameboy_input_t& in = s_instance->currentInput;
    switch (id) {
        case libretro::JOYPAD_UP: return in.up ? 1 : 0;
        case libretro::JOYPAD_DOWN: return in.down ? 1 : 0;
        case libretro::JOYPAD_LEFT: return in.left ? 1 : 0;
        case libretro::JOYPAD_RIGHT: return in.right ? 1 : 0;
        case libretro::JOYPAD_A: return in.a ? 1 : 0;
        case libretro::JOYPAD_B: return in.b ? 1 : 0;
        case libretro::JOYPAD_SELECT: return in.select ? 1 : 0;
        case libretro::JOYPAD_START: return in.start ? 1 : 0;
        default: return 0;
    }
}

GameBoyEmulation::InputMode GameBoyEmulation::globalInputMode = GameBoyEmulation::InputMode::KeyboardOnly;
GameBoyEmulation::KeyBindings GameBoyEmulation::globalKeyBindings = {};
GameBoyEmulation::ControllerBindings GameBoyEmulation::globalControllerBindings = {};
bool GameBoyEmulation::globalInputConfigured = false;

GameBoyEmulation::GameBoyEmulation()
    : isRunning(false), window(nullptr), renderer(nullptr), screenTexture(nullptr), controller(nullptr),
            controllerDeviceIndex(-1), scaleX(2), scaleY(2), workerInput{},
            emulationThreadStopRequested(false), emulationThreadRunning(false),
            emulationThreadFailed(false), frameReady(false), inputMode(InputMode::KeyboardOnly)
{
    backend = std::make_unique<NativeSameBoyBackend>();
    frameBuffer.resize(kGameBoyScreenWidth * kGameBoyScreenHeight * 4, 0);
        latestFrame.resize(kGameBoyScreenWidth * kGameBoyScreenHeight * 4, 0);
    setDefaultBindings();

    if (globalInputConfigured) {
        inputMode = globalInputMode;
        keyBindings = globalKeyBindings;
        controllerBindings = globalControllerBindings;
    } else {
        saveGlobalInputSettings();
    }
}

GameBoyEmulation::~GameBoyEmulation()
{
    unloadRom();
    cleanupSDL();
}

bool GameBoyEmulation::loadRom(const string& path)
{
    if (!backend) {
        std::cerr << "Game Boy backend not initialized.\n";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open ROM file: " << path << "\n";
        return false;
    }

    const bool isGb = path.length() >= 3 && path.substr(path.length() - 3) == ".gb";
    const bool isGbc = path.length() >= 4 && path.substr(path.length() - 4) == ".gbc";
    const bool isSgb = path.length() >= 4 && path.substr(path.length() - 4) == ".sgb";
    if (!isGb && !isGbc && !isSgb) {
        std::cerr << "Invalid file extension. Only .gb, .gbc, and .sgb files are supported.\n";
        return false;
    }

    file.close();
    if (!backend->loadRom(path)) {
        return false;
    }

    romPath = path;
    isRunning = true;
    std::cout << "Successfully loaded Game Boy ROM: " << path << "\n";
    return true;
}

void GameBoyEmulation::unloadRom()
{
    stopEmulationWorker();
    isRunning = false;
    romPath.clear();
}

std::vector<unsigned char> GameBoyEmulation::dumpSaveData() const
{
    return backend ? backend->dumpSaveData() : std::vector<unsigned char>{};
}

bool GameBoyEmulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    return backend ? backend->restoreSaveData(data) : false;
}

void GameBoyEmulation::calculateScale(int screenWidth, int screenHeight)
{
    int maxScaleX = (screenWidth - 50) / kGameBoyScreenWidth;
    int maxScaleY = (screenHeight - 50) / kGameBoyScreenHeight;
    int scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;
    scaleX = (scale < 1) ? 1 : scale;
    scaleY = scaleX;
}

void GameBoyEmulation::initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    calculateScale(displayMode.w, displayMode.h);

    const int windowWidth = kGameBoyScreenWidth * scaleX;
    const int windowHeight = kGameBoyScreenHeight * scaleY;

    window = SDL_CreateWindow(
        windowTitle.c_str(),
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
        kGameBoyScreenWidth, kGameBoyScreenHeight);
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

void GameBoyEmulation::cleanupSDL()
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

void GameBoyEmulation::setDefaultBindings()
{
    keyBindings = {
        SDLK_UP,
        SDLK_DOWN,
        SDLK_LEFT,
        SDLK_RIGHT,
        SDLK_z,
        SDLK_x,
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
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START
    };
}

void GameBoyEmulation::saveGlobalInputSettings()
{
    globalInputMode = inputMode;
    globalKeyBindings = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured = true;
}

void GameBoyEmulation::listControllers() const
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

bool GameBoyEmulation::openControllerByIndex(int index)
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

void GameBoyEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== Game Boy Input Configuration ===\n";
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
                {"A", &keyBindings.a},
                {"B", &keyBindings.b},
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
            std::cout << "Enter controller button names (A,B,BACK,START,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT).\n";
            std::cout << "Press Enter to keep current binding.\n";

            struct ControllerAction { const char* name; SDL_GameControllerButton* button; };
            ControllerAction actions[] = {
                {"UP", &controllerBindings.up},
                {"DOWN", &controllerBindings.down},
                {"LEFT", &controllerBindings.left},
                {"RIGHT", &controllerBindings.right},
                {"A", &controllerBindings.a},
                {"B", &controllerBindings.b},
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
    std::cout << "Global Game Boy controls updated.\n";
}

void GameBoyEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, sameboy_input_t& input, bool& running)
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
    if (key == keyBindings.select) input.select = pressed;
    if (key == keyBindings.start) input.start = pressed;
}

void GameBoyEmulation::handleControllerButtonEvent(Uint8 button, bool pressed, sameboy_input_t& input, bool& running)
{
    (void)running;

    if (button == static_cast<Uint8>(controllerBindings.up)) input.up = pressed;
    if (button == static_cast<Uint8>(controllerBindings.down)) input.down = pressed;
    if (button == static_cast<Uint8>(controllerBindings.left)) input.left = pressed;
    if (button == static_cast<Uint8>(controllerBindings.right)) input.right = pressed;
    if (button == static_cast<Uint8>(controllerBindings.a)) input.a = pressed;
    if (button == static_cast<Uint8>(controllerBindings.b)) input.b = pressed;
    if (button == static_cast<Uint8>(controllerBindings.select)) input.select = pressed;
    if (button == static_cast<Uint8>(controllerBindings.start)) input.start = pressed;
}

void GameBoyEmulation::setInput(const sameboy_input_t& input)
{
    if (!backend || !isRunning.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(inputMutex);
    workerInput = input;
}

void GameBoyEmulation::nextFrame()
{
    if (!backend || !isRunning.load()) {
        return;
    }

    if (!backend->nextFrame()) {
        std::cerr << "Game Boy emulator failed to advance frame.\n";
        isRunning = false;
    }
}

sameboy_color_t GameBoyEmulation::getPixel(int x, int y) const
{
    if (!backend || !isRunning.load()) {
        return {0, 0, 0, 255};
    }

    if (x < 0 || x >= kGameBoyScreenWidth || y < 0 || y >= kGameBoyScreenHeight) {
        return {0, 0, 0, 255};
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (frameReady.load() && latestFrame.size() >= static_cast<std::size_t>(kGameBoyScreenWidth * kGameBoyScreenHeight * 4)) {
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kGameBoyScreenWidth) + static_cast<std::size_t>(x)) * 4;
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

void GameBoyEmulation::renderFrame()
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

    if (SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), kGameBoyScreenWidth * 4) < 0) {
        std::cerr << "Failed to update texture: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Rect destRect = {0, 0, kGameBoyScreenWidth * scaleX, kGameBoyScreenHeight * scaleY};
    SDL_RenderCopy(renderer, screenTexture, nullptr, &destRect);
    SDL_RenderPresent(renderer);
}

void GameBoyEmulation::displayFrame() const
{
    if (!backend || !isRunning) {
        return;
    }

    for (int y = 0; y < kGameBoyScreenHeight; y += 8) {
        for (int x = 0; x < kGameBoyScreenWidth; x += 8) {
            sameboy_color_t pixel = getPixel(x, y);
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

void GameBoyEmulation::configureGlobalControls()
{
    GameBoyEmulation configSession;

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

void GameBoyEmulation::runInteractive()
{
    if (!backend || !isRunning.load()) {
        std::cerr << "No Game Boy ROM loaded.\n";
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

    std::cout << "\n=== Game Boy Emulator Running ===\n";
    std::cout << "ROM: " << romPath << "\n";
    std::cout << "Window opened. Close it to exit.\n";
    std::cout << "Press ESC to exit emulator.\n\n";

    sameboy_input_t input = {};
    bool running = true;
    bool windowClosed = false;

    if (!startEmulationWorker()) {
        std::cerr << "Failed to start Game Boy emulation worker thread.\n";
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

        // Keep the UI loop responsive while emulation runs on a worker core.
        SDL_Delay(1);
    }

    stopEmulationWorker();
    cleanupSDL();
    std::cout << "\nGame Boy emulation stopped.\n";
}

bool GameBoyEmulation::startEmulationWorker()
{
    if (emulationThreadRunning.load() || !backend || !isRunning.load()) {
        return false;
    }

    emulationThreadStopRequested = false;
    emulationThreadFailed = false;
    frameReady = false;
    emulationThread = std::thread(&GameBoyEmulation::emulationWorkerLoop, this);
    return true;
}

void GameBoyEmulation::stopEmulationWorker()
{
    emulationThreadStopRequested = true;
    if (emulationThread.joinable()) {
        emulationThread.join();
    }
}

void GameBoyEmulation::emulationWorkerLoop()
{
    emulationThreadRunning = true;

    std::vector<unsigned char> localFrame(static_cast<std::size_t>(kGameBoyScreenWidth * kGameBoyScreenHeight) * 4, 0);
    AudioMasterClock clock;
    // Keep enough lead to satisfy adaptive GB audio prebuffer (up to ~240 ms).
    clock.configure(backend ? backend->getAudioSampleRate() : 48000, backend ? backend->getNominalFps() : 59.7275, 260);

    while (!emulationThreadStopRequested.load() && isRunning.load()) {
        sameboy_input_t inputSnapshot = {};
        {
            std::lock_guard<std::mutex> lock(inputMutex);
            inputSnapshot = workerInput;
        }

        backend->setInput(inputSnapshot);
        if (!backend->nextFrame()) {
            std::cerr << "Game Boy emulator failed to advance frame.\n";
            isRunning = false;
            emulationThreadFailed = true;
            break;
        }

        for (int y = 0; y < kGameBoyScreenHeight; ++y) {
            for (int x = 0; x < kGameBoyScreenWidth; ++x) {
                const sameboy_color_t color = backend->getPixel(x, y);
                const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kGameBoyScreenWidth) + static_cast<std::size_t>(x)) * 4;
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
