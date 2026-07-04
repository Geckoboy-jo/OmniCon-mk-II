#include "PS3.hpp"
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

class PS3Backend {
public:
    virtual ~PS3Backend() = default;
    virtual bool loadRom(const std::string& path) = 0;
    virtual bool nextFrame() = 0;
    virtual void setInput(const ps3_input_t& input) = 0;
    virtual ps3_color_t getPixel(int x, int y) const = 0;
    virtual int    getFrameWidth()  const { return 1280; }
    virtual int    getFrameHeight() const { return 720; }
    virtual double getAspectRatio() const { return 16.0 / 9.0; }
    virtual double getNominalFps()  const { return 60.0; }
    virtual bool   initializeAudio() { return true; }
    virtual bool   isStandalone()    { return false; }
    virtual std::vector<unsigned char> dumpSaveData() const { return {}; }
    virtual bool restoreSaveData(const std::vector<unsigned char>&) { return false; }
};

// ─── Unavailable stub ────────────────────────────────────────────────────────

class PS3UnavailableBackend final : public PS3Backend {
public:
    bool loadRom(const std::string&) override {
        std::cerr << "[PS3] RPCS3 is not available.\n";
        std::cerr << "      Install with: sudo snap install rpcs3-emulator\n";
        std::cerr << "      Or place rpcs3 binary in /usr/bin/rpcs3\n";
        std::cerr << "      Or set OMNICON_PS3_BIN to the path of the RPCS3 binary\n";
        return false;
    }
    bool nextFrame() override { return false; }
    void setInput(const ps3_input_t&) override {}
    ps3_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }
};

// ─── Standalone RPCS3 backend ────────────────────────────────────────────────

class StandalonePS3Backend final : public PS3Backend {
public:
    static constexpr const char* kSnapBin   = "/snap/bin/rpcs3-emulator";
    static constexpr const char* kLocalBin  = "/usr/bin/rpcs3";
    static constexpr const char* kLocalBin2 = "/usr/local/bin/rpcs3";

    static std::string resolveBinary() {
        const char* envBin = std::getenv("OMNICON_PS3_BIN");
        if (envBin && *envBin && std::filesystem::exists(envBin)) return envBin;
        if (std::filesystem::exists(kSnapBin))   return kSnapBin;
        if (std::filesystem::exists(kLocalBin))  return kLocalBin;
        if (std::filesystem::exists(kLocalBin2)) return kLocalBin2;
        return "";
    }

    bool isAvailable() const { return !binary_.empty(); }
    bool isStandalone() override { return true; }

    StandalonePS3Backend() : binary_(resolveBinary()) {}

    ~StandalonePS3Backend() { unmountIso(); }

    bool loadRom(const std::string& path) override {
        if (binary_.empty()) return false;

        std::string gamePath = path;

        // RPCS3 cannot open ISO files directly; mount it and pass the folder.
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (ext == ".iso" || ext == ".bin") {
            gamePath = mountAndResolve(path);
            if (gamePath.empty()) {
                std::cerr << "[PS3] Failed to mount disc image: " << path << "\n";
                return false;
            }
        }

        std::cout << "[PS3] Launching RPCS3: " << binary_ << " \"" << gamePath << "\"\n";
        pid_ = fork();
        if (pid_ < 0) {
            std::cerr << "[PS3] fork() failed\n";
            unmountIso();
            return false;
        }
        if (pid_ == 0) {
            execlp(binary_.c_str(), binary_.c_str(), "--no-gui", gamePath.c_str(), nullptr);
            std::cerr << "[PS3] exec failed\n";
            _exit(1);
        }
        return true;
    }

    bool nextFrame() override {
        int status = 0;
        int r = waitpid(pid_, &status, WNOHANG);
        return (r == 0);
    }

    void waitForExit() {
        if (pid_ > 0) { waitpid(pid_, nullptr, 0); pid_ = -1; }
        unmountIso();
    }

    void setInput(const ps3_input_t&) override {}
    ps3_color_t getPixel(int, int) const override { return {0, 0, 0, 255}; }

private:
    std::string binary_;
    pid_t       pid_        = -1;
    std::string mountPoint_;   // non-empty when we own a loop-mounted ISO

    // Mount the disc image at a temporary directory and return the EBOOT.BIN
    // path that RPCS3 expects.  The mount base must be under $HOME (/root/)
    // because the rpcs3-emulator snap only has the 'home' interface connected
    // (removable-media is not connected, so /tmp/ and /mnt/ are inaccessible).
    std::string mountAndResolve(const std::string& isoPath) {
        // Use saves/ps3/ as the mount base so the path sits under /root/
        std::filesystem::path mountBase =
            std::filesystem::current_path() / "saves" / "ps3";
        std::error_code ec;
        std::filesystem::create_directories(mountBase, ec);

        // mkdtemp needs a mutable char array
        std::string tmplStr = (mountBase / "mnt_XXXXXX").string();
        std::vector<char> tmpl(tmplStr.begin(), tmplStr.end());
        tmpl.push_back('\0');

        if (!mkdtemp(tmpl.data())) {
            std::cerr << "[PS3] mkdtemp failed\n";
            return "";
        }
        mountPoint_ = tmpl.data();

        // mount -o loop,ro <iso> <mountPoint>
        pid_t mpid = fork();
        if (mpid == 0) {
            execlp("mount", "mount", "-o", "loop,ro",
                   isoPath.c_str(), mountPoint_.c_str(), nullptr);
            _exit(1);
        }
        int st = 0;
        waitpid(mpid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            std::cerr << "[PS3] mount failed (requires root / loop device)\n";
            rmdir(mountPoint_.c_str());
            mountPoint_.clear();
            return "";
        }

        // Locate EBOOT.BIN — RPCS3 accepts the explicit file path most reliably
        std::string eboot = mountPoint_ + "/PS3_GAME/USRDIR/EBOOT.BIN";
        if (std::filesystem::exists(eboot, ec))
            return eboot;

        // Some discs nest one level deeper
        for (const auto& entry :
             std::filesystem::directory_iterator(mountPoint_, ec)) {
            std::string nested = entry.path().string()
                               + "/PS3_GAME/USRDIR/EBOOT.BIN";
            if (std::filesystem::exists(nested, ec))
                return nested;
        }

        std::cerr << "[PS3] EBOOT.BIN not found in mounted image\n";
        unmountIso();
        return "";
    }

    void unmountIso() {
        if (mountPoint_.empty()) return;
        // umount <mountPoint>
        pid_t upid = fork();
        if (upid == 0) {
            execlp("umount", "umount", mountPoint_.c_str(), nullptr);
            _exit(1);
        }
        if (upid > 0) waitpid(upid, nullptr, 0);
        rmdir(mountPoint_.c_str());
        mountPoint_.clear();
    }
};

// ─── Local helpers ────────────────────────────────────────────────────────────

namespace {

std::string trimPS3(const std::string& value)
{
    const std::string ws = " \t\r\n";
    const std::size_t s = value.find_first_not_of(ws);
    if (s == std::string::npos) return "";
    return value.substr(s, value.find_last_not_of(ws) - s + 1);
}

std::string toUpperPS3(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

SDL_GameControllerButton parseControllerButtonPS3(const std::string& input)
{
    const std::string n = toUpperPS3(trimPS3(input));
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

const char* controllerButtonNamePS3(SDL_GameControllerButton b)
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

PS3Emulation::InputMode         PS3Emulation::globalInputMode          = PS3Emulation::InputMode::KeyboardAndController;
PS3Emulation::KeyBindings       PS3Emulation::globalKeyBindings         = {};
PS3Emulation::ControllerBindings PS3Emulation::globalControllerBindings = {};
bool                            PS3Emulation::globalInputConfigured     = false;

// ─── PS3Emulation ─────────────────────────────────────────────────────────────

PS3Emulation::PS3Emulation()
    : romLoaded(false), isRunning(false),
      window(nullptr), renderer(nullptr), screenTexture(nullptr), controller(nullptr),
      lastLetterboxRect{0, 0, 1280, 720}
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

PS3Emulation::~PS3Emulation()
{
    unloadRom();
    cleanupSDL();
}

void PS3Emulation::setDefaultBindings()
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

void PS3Emulation::saveGlobalInputSettings()
{
    globalInputMode          = inputMode;
    globalKeyBindings        = keyBindings;
    globalControllerBindings = controllerBindings;
    globalInputConfigured    = true;
}

void PS3Emulation::initSDL()
{
    if (window && renderer && screenTexture) return;

    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
            return;
        }
    }

    const int nativeWidth  = 1280;
    const int nativeHeight = 720;

    window = SDL_CreateWindow("OmniCon PlayStation 3",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1280, 720,
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

void PS3Emulation::cleanupSDL()
{
    if (controller)    { SDL_GameControllerClose(controller); controller    = nullptr; }
    if (screenTexture) { SDL_DestroyTexture(screenTexture);   screenTexture = nullptr; }
    if (renderer)      { SDL_DestroyRenderer(renderer);       renderer      = nullptr; }
    if (window)        { SDL_DestroyWindow(window);           window        = nullptr; }
}

void PS3Emulation::renderFrameSDL()
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
            const ps3_color_t px = backendPtr->getPixel(x, y);
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

void PS3Emulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== PlayStation 3 Input Configuration ===\n";
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
        choice = trimPS3(choice);

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
                std::string keyName; std::getline(std::cin, keyName); keyName = trimPS3(keyName);
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
            std::string idx; std::getline(std::cin, idx); idx = trimPS3(idx);
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
                std::cout << a.name << " [" << controllerButtonNamePS3(*a.button) << "]: ";
                std::string btnName; std::getline(std::cin, btnName); btnName = trimPS3(btnName);
                if (btnName.empty()) continue;
                SDL_GameControllerButton parsed = parseControllerButtonPS3(btnName);
                if (parsed == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "  Invalid button, keeping existing.\n"; continue; }
                *a.button = parsed;
            }
            std::cout << "Controller bindings updated.\n";
            continue;
        }

        std::cout << "Unknown option.\n";
    }

    saveGlobalInputSettings();
    std::cout << "PlayStation 3 controls saved.\n";
}

void PS3Emulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, ps3_input_t& input, bool& running)
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

void PS3Emulation::handleControllerButtonEvent(Uint8 button, bool pressed, ps3_input_t& input, bool& running)
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

bool PS3Emulation::openControllerByIndex(int index)
{
    if (controller) { SDL_GameControllerClose(controller); controller = nullptr; }
    const int count = SDL_NumJoysticks();
    if (count <= 0 || index < 0 || index >= count || !SDL_IsGameController(index)) return false;
    controller = SDL_GameControllerOpen(index);
    return controller != nullptr;
}

void PS3Emulation::listControllers() const
{
    const int count = SDL_NumJoysticks();
    std::cout << "Controllers: " << count << "\n";
    for (int i = 0; i < count; ++i) {
        std::cout << "  [" << i << "] "
                  << (SDL_IsGameController(i) ? "game controller" : "joystick")
                  << " - " << SDL_JoystickNameForIndex(i) << "\n";
    }
}

bool PS3Emulation::loadRom(const string& path)
{
    unloadRom();

    std::filesystem::path fsPath(path);
    std::string ext = fsPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    static const char* const kValidExts[] = {
        ".pkg", ".iso", ".bin", ".chd", nullptr
    };
    bool validExt = false;
    for (int i = 0; kValidExts[i]; ++i) {
        if (ext == kValidExts[i]) { validExt = true; break; }
    }
    if (!validExt) {
        std::cerr << "[PS3] Unsupported extension '" << ext << "'.\n";
        std::cerr << "      PlayStation 3 games use: .pkg, .iso, .bin, .chd\n";
        return false;
    }

    auto standaloneBackend = std::make_unique<StandalonePS3Backend>();
    if (standaloneBackend->isAvailable()) {
        backendPtr = std::move(standaloneBackend);
    } else {
        backendPtr = std::make_unique<PS3UnavailableBackend>();
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

void PS3Emulation::unloadRom()
{
    isRunning = false;
    romLoaded = false;
    backendPtr.reset();
    romPath.clear();
}

void PS3Emulation::runInteractive()
{
    if (!romLoaded || !backendPtr) {
        std::cerr << "[PS3] No PlayStation 3 game loaded.\n";
        return;
    }

    initSDL();
    if (!window || !renderer || !screenTexture) {
        std::cerr << "[PS3] Failed to initialize SDL.\n";
        return;
    }

    if (!backendPtr->initializeAudio()) {
        std::cerr << "[PS3] Audio initialization failed.\n";
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { openControllerByIndex(i); break; }
    if (controller && inputMode == InputMode::KeyboardOnly)
        inputMode = InputMode::KeyboardAndController;

    std::cout << "PlayStation 3: " << romPath << "\n";
    std::cout << "Press Escape or close the window to exit.\n";
    std::cout << "Default keys: Arrows=D-pad, Z=Cross, X=Circle, A=Square, S=Triangle\n";
    std::cout << "              Enter=Start, RShift=Select, Q=L1, W=R1, E=L2, R=R2\n";
    std::cout << "Note: RPCS3 manages its own save data in its user directory.\n";

    // Standalone RPCS3 manages its own window — just wait for exit.
    if (backendPtr->isStandalone()) {
        auto* sa = static_cast<StandalonePS3Backend*>(backendPtr.get());
        sa->waitForExit();
        isRunning = false;
        return;
    }

    bool running = true;
    ps3_input_t input{};

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

void PS3Emulation::configureGlobalControls()
{
    PS3Emulation tmp;
    tmp.configureInputMenu();
    tmp.saveGlobalInputSettings();
}

std::vector<unsigned char> PS3Emulation::dumpSaveData() const
{
    if (!backendPtr || !isLoaded()) return {};
    return backendPtr->dumpSaveData();
}

bool PS3Emulation::restoreSaveData(const std::vector<unsigned char>& data)
{
    if (!backendPtr || !isLoaded()) return false;
    return backendPtr->restoreSaveData(data);
}
