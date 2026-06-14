#include "GBA.hpp"
#include "audio_clock.hpp"

#include <mgba/gba/core.h>
#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/vfs.h>

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <climits>

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
    if (name == "A")          return SDL_CONTROLLER_BUTTON_A;
    if (name == "B")          return SDL_CONTROLLER_BUTTON_B;
    if (name == "X")          return SDL_CONTROLLER_BUTTON_X;
    if (name == "Y")          return SDL_CONTROLLER_BUTTON_Y;
    if (name == "BACK")       return SDL_CONTROLLER_BUTTON_BACK;
    if (name == "START")      return SDL_CONTROLLER_BUTTON_START;
    if (name == "LEFTSHOULDER")  return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (name == "RIGHTSHOULDER") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (name == "DPAD_UP")    return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (name == "DPAD_DOWN")  return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (name == "DPAD_LEFT")  return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (name == "DPAD_RIGHT") return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

const char* controllerButtonName(SDL_GameControllerButton button)
{
    const char* name = SDL_GameControllerGetStringForButton(button);
    return name ? name : "unknown";
}

} // namespace

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

GBAEmulation::InputMode GBAEmulation::globalInputMode = GBAEmulation::InputMode::KeyboardOnly;

GBAEmulation::KeyBindings GBAEmulation::globalKeyBindings = {
    SDLK_z,       // a
    SDLK_x,       // b
    SDLK_RSHIFT,  // select
    SDLK_RETURN,  // start
    SDLK_RIGHT,   // right
    SDLK_LEFT,    // left
    SDLK_UP,      // up
    SDLK_DOWN,    // down
    SDLK_s,       // r shoulder
    SDLK_a        // l shoulder
};

GBAEmulation::ControllerBindings GBAEmulation::globalControllerBindings = {
    SDL_CONTROLLER_BUTTON_A,            // a
    SDL_CONTROLLER_BUTTON_B,            // b
    SDL_CONTROLLER_BUTTON_BACK,         // select
    SDL_CONTROLLER_BUTTON_START,        // start
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,   // right
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,    // left
    SDL_CONTROLLER_BUTTON_DPAD_UP,      // up
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,    // down
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,// r
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER  // l
};

bool GBAEmulation::globalInputConfigured = false;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

GBAEmulation::GBAEmulation()
{
    setDefaultBindings();
    if (globalInputConfigured) {
        inputMode = globalInputMode;
        keyBindings = globalKeyBindings;
        controllerBindings = globalControllerBindings;
    } else {
        saveGlobalInputSettings();
    }
    pendingAudioSamples.reserve(8192);
}

GBAEmulation::~GBAEmulation()
{
    stopEmulationWorker();
    unloadRom();
    closeAudioDevice();
}

// ---------------------------------------------------------------------------
// ROM loading
// ---------------------------------------------------------------------------

bool GBAEmulation::loadRom(const std::string& path)
{
    unloadRom();

    struct mCore* newCore = GBACoreCreate();
    if (!newCore) {
        std::cerr << "[GBA] Failed to create mGBA core.\n";
        return false;
    }

    if (!newCore->init(newCore)) {
        std::cerr << "[GBA] Failed to initialise mGBA core.\n";
        free(newCore);
        return false;
    }

    // The installed libmgba may not call mCoreInitConfig inside init(), leaving
    // the config hash tables uninitialised and causing a crash on first config
    // access (e.g. inside mCoreLoadFile).  Call it explicitly here.
    mCoreInitConfig(newCore, "gba");

    // Set video buffer before loading so the core knows where to write pixels.
    mgbaFrameBuffer.assign(static_cast<std::size_t>(kScreenWidth * kScreenHeight), 0);
    newCore->setVideoBuffer(newCore, mgbaFrameBuffer.data(), static_cast<std::size_t>(kScreenWidth));

    // mCoreLoadFile sets up core->dirs (base, save, etc.) from the file path,
    // which is essential for games that use battery-backed save (SRAM/Flash/EEPROM).
    // Using raw VFileOpen + loadROM skips this and crashes on save access.
    if (!mCoreLoadFile(newCore, path.c_str())) {
        std::cerr << "[GBA] Failed to load ROM: " << path << "\n";
        newCore->deinit(newCore);
        free(newCore);
        return false;
    }

    // reset() must come after loadROM so the CPU and memory are fully initialised
    // before we query frequency or set up audio blip buffers.
    newCore->reset(newCore);

    if (!ensureAudioDevice(audioSampleRate)) {
        std::cerr << "[GBA] Continuing without audio output.\n";
    }

    newCore->setAudioBufferSize(newCore, 4096);

    blip_t* blipLeft  = newCore->getAudioChannel(newCore, 0);
    blip_t* blipRight = newCore->getAudioChannel(newCore, 1);
    if (blipLeft && blipRight) {
        const double cpuHz = static_cast<double>(newCore->frequency(newCore));
        blip_set_rates(blipLeft,  cpuHz, static_cast<double>(audioSampleRate));
        blip_set_rates(blipRight, cpuHz, static_cast<double>(audioSampleRate));
    }

    core    = newCore;
    romPath = path;
    isRunning = true;

    if (audioDevice != 0) {
        SDL_LockAudioDevice(audioDevice);
        audioRingRead  = 0;
        audioRingWrite = 0;
        audioRingCount = 0;
        lastAudioLeft  = 0;
        lastAudioRight = 0;
        audioPrimed        = false;
        targetPrebufferMs  = 120;
        underrunEvents     = 0;
        stablePulls        = 0;
        SDL_PauseAudioDevice(audioDevice, 1);
        SDL_UnlockAudioDevice(audioDevice);
    }

    char titleBuf[17] = {};
    core->getGameTitle(core, titleBuf);
    if (titleBuf[0] != '\0') {
        windowTitle = std::string("GBA - ") + titleBuf + " (mGBA)";
    }

    latestFrame.assign(static_cast<std::size_t>(kScreenWidth * kScreenHeight), 0);
    std::cout << "[GBA] Loaded: " << path << "\n";
    return true;
}

void GBAEmulation::unloadRom()
{
    isRunning = false;
    if (core) {
        core->deinit(core);  // deinit frees the core allocation internally
        core = nullptr;
    }
    romPath.clear();
    mgbaFrameBuffer.clear();
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

bool GBAEmulation::ensureAudioDevice(int sampleRate)
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
            std::cerr << "[GBA] Failed to init SDL audio: " << SDL_GetError() << "\n";
            return false;
        }
    }

    SDL_AudioSpec desired = {};
    desired.freq     = sampleRate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 2048;
    desired.callback = sdlAudioPullCb;
    desired.userdata = this;

    SDL_AudioSpec obtained = {};
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice == 0) {
        std::cerr << "[GBA] Failed to open SDL audio device: " << SDL_GetError() << "\n";
        return false;
    }

    audioSampleRate = obtained.freq > 0 ? obtained.freq : sampleRate;
    const std::size_t ringFrames = std::max<std::size_t>(4096, static_cast<std::size_t>(audioSampleRate) / 2);
    audioRing.assign(ringFrames * 2, 0);
    audioRingRead  = 0;
    audioRingWrite = 0;
    audioRingCount = 0;
    audioPrimed       = false;
    targetPrebufferMs = 120;
    underrunEvents    = 0;
    stablePulls       = 0;
    lastAudioLeft  = 0;
    lastAudioRight = 0;
    SDL_PauseAudioDevice(audioDevice, 1);
    return true;
}

void GBAEmulation::closeAudioDevice()
{
    audioRing.clear();
    audioRingRead  = 0;
    audioRingWrite = 0;
    audioRingCount = 0;
    audioPrimed        = false;
    lastAudioLeft  = 0;
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

const int16_t* GBAEmulation::applyRateControl(const int16_t* data, size_t frames,
                                               size_t& outFrames, int bufferedFramesBefore)
{
    outFrames = frames;
    if (!data || frames == 0) {
        return data;
    }

    const int targetFrames = std::max(1, (audioSampleRate * targetPrebufferMs) / 1000);
    const int errorFrames  = targetFrames - bufferedFramesBefore;
    const double normalizedError = std::clamp(
        static_cast<double>(errorFrames) / static_cast<double>(targetFrames),
        -1.0, 1.0);

    constexpr double kMaxDelta = 0.0015;
    const double targetRatio = std::clamp(1.0 + (normalizedError * kMaxDelta),
                                          1.0 - kMaxDelta, 1.0 + kMaxDelta);
    rateControlRatio = (rateControlRatio * 0.98) + (targetRatio * 0.02);

    if (std::abs(rateControlRatio - 1.0) < 0.0002) {
        outFrames = frames;
        rateControlPhase = 0.0;
        return data;
    }

    const std::size_t targetOutFrames = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(static_cast<double>(frames) * rateControlRatio)));
    const double srcStep = 1.0 / rateControlRatio;

    rateAdjustedBatch.resize(targetOutFrames * 2);
    double srcPos = rateControlPhase;
    for (std::size_t out = 0; out < targetOutFrames; ++out) {
        std::size_t idx  = static_cast<std::size_t>(srcPos);
        double frac = srcPos - static_cast<double>(idx);
        if (idx >= frames - 1) { idx = frames - 1; frac = 0.0; }
        const std::size_t nextIdx = std::min<std::size_t>(idx + 1, frames - 1);

        const double l = static_cast<double>(data[idx*2])
                       + (static_cast<double>(data[nextIdx*2]   - data[idx*2])   * frac);
        const double r = static_cast<double>(data[idx*2+1])
                       + (static_cast<double>(data[nextIdx*2+1] - data[idx*2+1]) * frac);

        rateAdjustedBatch[out*2]   = static_cast<int16_t>(std::clamp(l, -32768.0, 32767.0));
        rateAdjustedBatch[out*2+1] = static_cast<int16_t>(std::clamp(r, -32768.0, 32767.0));
        srcPos += srcStep;
    }

    rateControlPhase = srcPos - std::floor(srcPos);
    outFrames = targetOutFrames;
    return rateAdjustedBatch.data();
}

void GBAEmulation::queueAudioSamples(const int16_t* data, size_t frames)
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
        audioRing[audioRingWrite*2]   = adjustedData[i*2];
        audioRing[audioRingWrite*2+1] = adjustedData[i*2+1];
        audioRingWrite = (audioRingWrite + 1) % capacityFrames;
        ++audioRingCount;
    }

    if (underrunEvents > 0) {
        targetPrebufferMs = std::min(220, targetPrebufferMs + 20);
        underrunEvents = 0;
        stablePulls    = 0;
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

void GBAEmulation::onAudioPull(Uint8* stream, int len)
{
    std::memset(stream, 0, static_cast<std::size_t>(len));
    if (!audioPrimed || audioRing.empty()) {
        return;
    }

    const int frameBytes   = 2 * static_cast<int>(sizeof(int16_t));
    const std::size_t requestedFrames = static_cast<std::size_t>(len / frameBytes);
    auto* out = reinterpret_cast<int16_t*>(stream);
    const std::size_t capacityFrames = audioRing.size() / 2;
    const std::size_t toCopyFrames   = std::min<std::size_t>(requestedFrames, audioRingCount);

    for (std::size_t i = 0; i < toCopyFrames; ++i) {
        out[i*2]   = audioRing[audioRingRead*2];
        out[i*2+1] = audioRing[audioRingRead*2+1];
        lastAudioLeft  = out[i*2];
        lastAudioRight = out[i*2+1];
        audioRingRead = (audioRingRead + 1) % capacityFrames;
    }
    audioRingCount -= toCopyFrames;

    if (toCopyFrames < requestedFrames) {
        for (std::size_t i = toCopyFrames; i < requestedFrames; ++i) {
            out[i*2]   = lastAudioLeft;
            out[i*2+1] = lastAudioRight;
        }
        ++underrunEvents;
        stablePulls = 0;
        audioPrimed = false;
    } else if (stablePulls < 10000) {
        ++stablePulls;
    }
}

void GBAEmulation::sdlAudioPullCb(void* userdata, Uint8* stream, int len)
{
    auto* self = static_cast<GBAEmulation*>(userdata);
    if (self) {
        self->onAudioPull(stream, len);
    }
}

// ---------------------------------------------------------------------------
// SDL setup
// ---------------------------------------------------------------------------

void GBAEmulation::calculateScale(int screenWidth, int screenHeight)
{
    int maxScaleX = (screenWidth  - 50) / kScreenWidth;
    int maxScaleY = (screenHeight - 50) / kScreenHeight;
    int scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;
    scaleX = scaleY = (scale < 1) ? 1 : scale;
}

void GBAEmulation::initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "[GBA] Failed to init SDL: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    calculateScale(displayMode.w, displayMode.h);

    const int windowWidth  = kScreenWidth  * scaleX;
    const int windowHeight = kScreenHeight * scaleY;

    window = SDL_CreateWindow(windowTitle.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              windowWidth, windowHeight,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        std::cerr << "[GBA] Failed to create window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return;
    }

    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "[GBA] Failed to create renderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return;
    }

    // mGBA 32-bit pixel format: R in byte 0, G in byte 1, B in byte 2, A in byte 3
    // SDL_PIXELFORMAT_ABGR8888 matches this layout on little-endian platforms.
    screenTexture = SDL_CreateTexture(renderer,
                                      SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      kScreenWidth, kScreenHeight);
    if (!screenTexture) {
        std::cerr << "[GBA] Failed to create texture: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = nullptr;
        window   = nullptr;
        SDL_Quit();
        return;
    }
}

void GBAEmulation::cleanupSDL()
{
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
        controllerDeviceIndex = -1;
    }
    if (screenTexture) { SDL_DestroyTexture(screenTexture);   screenTexture = nullptr; }
    if (renderer)      { SDL_DestroyRenderer(renderer);       renderer      = nullptr; }
    if (window)        { SDL_DestroyWindow(window);           window        = nullptr; }
    SDL_Quit();
}

void GBAEmulation::renderFrame()
{
    if (!renderer || !screenTexture) {
        return;
    }

    std::vector<uint32_t> renderBuf;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!frameReady.load()) {
            return;
        }
        renderBuf = latestFrame;
    }

    SDL_UpdateTexture(screenTexture, nullptr, renderBuf.data(),
                      kScreenWidth * static_cast<int>(sizeof(uint32_t)));

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_Rect dest = {0, 0, kScreenWidth * scaleX, kScreenHeight * scaleY};
    SDL_RenderCopy(renderer, screenTexture, nullptr, &dest);
    SDL_RenderPresent(renderer);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void GBAEmulation::setDefaultBindings()
{
    keyBindings = {
        SDLK_z,       // a
        SDLK_x,       // b
        SDLK_RSHIFT,  // select
        SDLK_RETURN,  // start
        SDLK_RIGHT,   // right
        SDLK_LEFT,    // left
        SDLK_UP,      // up
        SDLK_DOWN,    // down
        SDLK_s,       // r
        SDLK_a        // l
    };

    controllerBindings = {
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER
    };
}

void GBAEmulation::saveGlobalInputSettings()
{
    globalInputMode          = inputMode;
    globalKeyBindings        = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured    = true;
}

void GBAEmulation::listControllers() const
{
    const int count = SDL_NumJoysticks();
    if (count <= 0) {
        std::cout << "No controllers detected.\n";
        return;
    }
    std::cout << "Detected controllers:\n";
    for (int i = 0; i < count; ++i) {
        if (SDL_IsGameController(i)) {
            const char* name = SDL_GameControllerNameForIndex(i);
            std::cout << "  [" << i << "] " << (name ? name : "Unknown") << "\n";
        }
    }
}

bool GBAEmulation::openControllerByIndex(int index)
{
    if (index < 0 || index >= SDL_NumJoysticks() || !SDL_IsGameController(index)) {
        std::cout << "Invalid controller index.\n";
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
    const char* name = SDL_GameControllerName(controller);
    std::cout << "Using controller: " << (name ? name : "Unknown") << "\n";
    return true;
}

void GBAEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== GBA Input Configuration ===\n";
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

        if (choice == "0") break;

        if (choice == "1") { inputMode = InputMode::KeyboardOnly;           std::cout << "Keyboard only.\n";           continue; }
        if (choice == "2") { inputMode = InputMode::ControllerOnly;          std::cout << "Controller only.\n";          continue; }
        if (choice == "3") { inputMode = InputMode::KeyboardAndController;   std::cout << "Keyboard + Controller.\n";    continue; }

        if (choice == "4") {
            struct KbdAction { const char* name; SDL_Keycode* key; };
            KbdAction actions[] = {
                {"A",      &keyBindings.a},
                {"B",      &keyBindings.b},
                {"SELECT", &keyBindings.select},
                {"START",  &keyBindings.start},
                {"RIGHT",  &keyBindings.right},
                {"LEFT",   &keyBindings.left},
                {"UP",     &keyBindings.up},
                {"DOWN",   &keyBindings.down},
                {"R",      &keyBindings.r},
                {"L",      &keyBindings.l}
            };
            std::cout << "Enter SDL key names. Press Enter to keep current binding.\n";
            for (KbdAction& a : actions) {
                std::cout << a.name << " [" << SDL_GetKeyName(*a.key) << "]: ";
                std::string name;
                std::getline(std::cin, name);
                name = trim(name);
                if (name.empty()) continue;
                SDL_Keycode parsed = SDL_GetKeyFromName(name.c_str());
                if (parsed == SDLK_UNKNOWN) { std::cout << "  Invalid key, keeping current.\n"; continue; }
                *a.key = parsed;
            }
            std::cout << "Keyboard bindings updated.\n";
            continue;
        }

        if (choice == "5") {
            listControllers();
            std::cout << "Enter controller index: ";
            std::string idxStr;
            std::getline(std::cin, idxStr);
            idxStr = trim(idxStr);
            if (idxStr.empty()) continue;
            try { openControllerByIndex(std::stoi(idxStr)); }
            catch (...) { std::cout << "Invalid index.\n"; }
            continue;
        }

        if (choice == "6") {
            struct CtrlAction { const char* name; SDL_GameControllerButton* button; };
            CtrlAction actions[] = {
                {"A",      &controllerBindings.a},
                {"B",      &controllerBindings.b},
                {"SELECT", &controllerBindings.select},
                {"START",  &controllerBindings.start},
                {"RIGHT",  &controllerBindings.right},
                {"LEFT",   &controllerBindings.left},
                {"UP",     &controllerBindings.up},
                {"DOWN",   &controllerBindings.down},
                {"R",      &controllerBindings.r},
                {"L",      &controllerBindings.l}
            };
            std::cout << "Valid names: A,B,X,Y,BACK,START,LEFTSHOULDER,RIGHTSHOULDER,DPAD_UP,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT\n";
            std::cout << "Press Enter to keep current binding.\n";
            for (CtrlAction& a : actions) {
                std::cout << a.name << " [" << controllerButtonName(*a.button) << "]: ";
                std::string name;
                std::getline(std::cin, name);
                name = trim(name);
                if (name.empty()) continue;
                SDL_GameControllerButton parsed = parseControllerButtonName(name);
                if (parsed == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "  Invalid button, keeping current.\n"; continue; }
                *a.button = parsed;
            }
            std::cout << "Controller bindings updated.\n";
            continue;
        }

        std::cout << "Unknown option.\n";
    }

    saveGlobalInputSettings();
    std::cout << "Global GBA controls updated.\n";
}

void GBAEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed,
                                       gba_input_t& input, bool& running)
{
    if (key == SDLK_ESCAPE && pressed) { running = false; return; }

    if (key == keyBindings.a)      input.a      = pressed;
    if (key == keyBindings.b)      input.b      = pressed;
    if (key == keyBindings.select) input.select = pressed;
    if (key == keyBindings.start)  input.start  = pressed;
    if (key == keyBindings.right)  input.right  = pressed;
    if (key == keyBindings.left)   input.left   = pressed;
    if (key == keyBindings.up)     input.up     = pressed;
    if (key == keyBindings.down)   input.down   = pressed;
    if (key == keyBindings.r)      input.r      = pressed;
    if (key == keyBindings.l)      input.l      = pressed;
}

void GBAEmulation::handleControllerButtonEvent(Uint8 button, bool pressed,
                                               gba_input_t& input, bool& running)
{
    (void)running;
    if (button == static_cast<Uint8>(controllerBindings.a))      input.a      = pressed;
    if (button == static_cast<Uint8>(controllerBindings.b))      input.b      = pressed;
    if (button == static_cast<Uint8>(controllerBindings.select)) input.select = pressed;
    if (button == static_cast<Uint8>(controllerBindings.start))  input.start  = pressed;
    if (button == static_cast<Uint8>(controllerBindings.right))  input.right  = pressed;
    if (button == static_cast<Uint8>(controllerBindings.left))   input.left   = pressed;
    if (button == static_cast<Uint8>(controllerBindings.up))     input.up     = pressed;
    if (button == static_cast<Uint8>(controllerBindings.down))   input.down   = pressed;
    if (button == static_cast<Uint8>(controllerBindings.r))      input.r      = pressed;
    if (button == static_cast<Uint8>(controllerBindings.l))      input.l      = pressed;
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

bool GBAEmulation::startEmulationWorker()
{
    if (emulationThreadRunning.load() || !core || !isRunning.load()) {
        return false;
    }
    emulationThreadStopRequested = false;
    emulationThreadFailed        = false;
    frameReady                   = false;
    emulationThread = std::thread(&GBAEmulation::emulationWorkerLoop, this);
    return true;
}

void GBAEmulation::stopEmulationWorker()
{
    emulationThreadStopRequested = true;
    if (emulationThread.joinable()) {
        emulationThread.join();
    }
}

void GBAEmulation::emulationWorkerLoop()
{
    emulationThreadRunning = true;

    AudioMasterClock clock;
    clock.configure(audioSampleRate, kNominalFps, 260);

    blip_t* blipLeft  = core ? core->getAudioChannel(core, 0) : nullptr;
    blip_t* blipRight = core ? core->getAudioChannel(core, 1) : nullptr;

    while (!emulationThreadStopRequested.load() && isRunning.load()) {
        // --- Input snapshot ---
        gba_input_t inputSnapshot = {};
        {
            std::lock_guard<std::mutex> lock(inputMutex);
            inputSnapshot = workerInput;
        }

        // Map gba_input_t to mGBA key bitmask (GBAKey enum bit positions)
        uint32_t keys = 0;
        if (inputSnapshot.a)      keys |= (1u << GBA_KEY_A);
        if (inputSnapshot.b)      keys |= (1u << GBA_KEY_B);
        if (inputSnapshot.select) keys |= (1u << GBA_KEY_SELECT);
        if (inputSnapshot.start)  keys |= (1u << GBA_KEY_START);
        if (inputSnapshot.right)  keys |= (1u << GBA_KEY_RIGHT);
        if (inputSnapshot.left)   keys |= (1u << GBA_KEY_LEFT);
        if (inputSnapshot.up)     keys |= (1u << GBA_KEY_UP);
        if (inputSnapshot.down)   keys |= (1u << GBA_KEY_DOWN);
        if (inputSnapshot.r)      keys |= (1u << GBA_KEY_R);
        if (inputSnapshot.l)      keys |= (1u << GBA_KEY_L);
        core->setKeys(core, keys);

        // --- Run one frame ---
        core->runFrame(core);

        // --- Audio: drain blip_t channels ---
        if (blipLeft && blipRight) {
            const int avail = blip_samples_avail(blipLeft);
            if (avail > 0) {
                pendingAudioSamples.resize(static_cast<std::size_t>(avail) * 2);
                blip_read_samples(blipLeft,  pendingAudioSamples.data(),     avail, 1);
                blip_read_samples(blipRight, pendingAudioSamples.data() + 1, avail, 1);
                queueAudioSamples(pendingAudioSamples.data(), static_cast<std::size_t>(avail));
            }
        }

        // --- Publish frame ---
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = mgbaFrameBuffer;
            frameReady  = true;
        }

        clock.onFrameProduced();
        clock.throttle();
    }

    emulationThreadRunning = false;
}

// ---------------------------------------------------------------------------
// configureGlobalControls (static)
// ---------------------------------------------------------------------------

void GBAEmulation::configureGlobalControls()
{
    GBAEmulation session;

    const bool hadController = (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0);
    if (!hadController && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "[GBA] Failed to init controller subsystem: " << SDL_GetError() << "\n";
        return;
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            session.openControllerByIndex(i);
            break;
        }
    }

    session.configureInputMenu();

    if (!hadController) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

// ---------------------------------------------------------------------------
// runInteractive
// ---------------------------------------------------------------------------

void GBAEmulation::runInteractive()
{
    if (!core || !isRunning.load()) {
        std::cerr << "[GBA] No ROM loaded.\n";
        return;
    }

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) >= 0) {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                openControllerByIndex(i);
                break;
            }
        }
    }

    initSDL();
    if (!window || !renderer) {
        std::cerr << "[GBA] Failed to init SDL graphics.\n";
        return;
    }

    if (inputMode == InputMode::ControllerOnly && !controller) {
        std::cout << "Controller-only mode but no controller active. Falling back to keyboard.\n";
        inputMode = InputMode::KeyboardOnly;
    }

    std::cout << "\n=== GBA Emulator Running ===\n";
    std::cout << "ROM: " << romPath << "\n";
    std::cout << "Controls: Arrow keys = D-pad, Z = A, X = B, Enter = Start, RShift = Select, A = L, S = R\n";
    std::cout << "Press ESC to exit.\n\n";

    gba_input_t input = {};
    bool running = true;

    if (!startEmulationWorker()) {
        std::cerr << "[GBA] Failed to start emulation worker.\n";
        cleanupSDL();
        return;
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (inputMode != InputMode::ControllerOnly) {
                        handleKeyboardEvent(event.key.keysym.sym, true, input, running);
                    }
                    break;
                case SDL_KEYUP:
                    if (inputMode != InputMode::ControllerOnly) {
                        handleKeyboardEvent(event.key.keysym.sym, false, input, running);
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (inputMode != InputMode::KeyboardOnly) {
                        handleControllerButtonEvent(event.cbutton.button, true, input, running);
                    }
                    break;
                case SDL_CONTROLLERBUTTONUP:
                    if (inputMode != InputMode::KeyboardOnly) {
                        handleControllerButtonEvent(event.cbutton.button, false, input, running);
                    }
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!controller) openControllerByIndex(event.cdevice.which);
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

        {
            std::lock_guard<std::mutex> lock(inputMutex);
            workerInput = input;
        }

        if (emulationThreadFailed.load() || !isRunning.load()) {
            running = false;
            break;
        }

        renderFrame();
        SDL_Delay(1);
    }

    stopEmulationWorker();
    cleanupSDL();
    std::cout << "\nGBA emulation stopped.\n";
}

std::vector<unsigned char> GBAEmulation::dumpSaveData() const
{
    if (!core) return {};
    void* sram = nullptr;
    const size_t size = core->savedataClone(core, &sram);
    if (!sram || size == 0) return {};
    std::vector<unsigned char> result(static_cast<unsigned char*>(sram),
                                      static_cast<unsigned char*>(sram) + size);
    free(sram);
    return result;
}

bool GBAEmulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!core || data.empty()) return false;
    return core->savedataRestore(core, data.data(), data.size(), true);
}
