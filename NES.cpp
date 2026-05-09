#include "NES.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cctype>

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
    if (name == "GUIDE") return SDL_CONTROLLER_BUTTON_GUIDE;
    if (name == "START") return SDL_CONTROLLER_BUTTON_START;
    if (name == "LEFTSTICK") return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    if (name == "RIGHTSTICK") return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
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

NESEmulation::NESEmulation()
        : emulator(nullptr), isRunning(false), window(nullptr), renderer(nullptr),
          screenTexture(nullptr), controller(nullptr), controllerDeviceIndex(-1),
          scaleX(2), scaleY(2), inputMode(InputMode::KeyboardOnly)
{
    frameBuffer.resize(AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT * 4, 0);
    emulator = agnes_make();
    setDefaultBindings();
    if (!emulator) {
        std::cerr << "Failed to create Agnes emulator instance.\n";
    }
}

NESEmulation::~NESEmulation()
{
    unloadRom();
    cleanupSDL();
    if (emulator) {
        agnes_destroy(emulator);
        emulator = nullptr;
    }
}

bool NESEmulation::loadRom(const string& path)
{
    if (!emulator) {
        std::cerr << "Emulator not initialized.\n";
        return false;
    }

    // Check if file exists
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open ROM file: " << path << "\n";
        return false;
    }

    // Check for .nes extension
    if (path.length() < 4 || path.substr(path.length() - 4) != ".nes") {
        std::cerr << "Invalid file extension. Only .nes files are supported.\n";
        file.close();
        return false;
    }

    // Read entire file into memory
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> fileData(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    // Validate iNES header (first 4 bytes should be "NES\x1a")
    if (fileSize < 16 || fileData[0] != 'N' || fileData[1] != 'E' || 
        fileData[2] != 'S' || fileData[3] != 0x1a) {
        std::cerr << "Invalid NES ROM file format (missing or corrupted iNES header).\n";
        return false;
    }

    // Try to load into Agnes
    if (!agnes_load_ines_data(emulator, fileData.data(), fileSize)) {
        std::cerr << "Agnes emulator failed to load ROM.\n";
        return false;
    }

    romPath = path;
    isRunning = true;
    std::cout << "Successfully loaded ROM: " << path << "\n";
    return true;
}

void NESEmulation::unloadRom()
{
    isRunning = false;
    romPath.clear();
}

void NESEmulation::calculateScale(int screenWidth, int screenHeight)
{
    // Calculate maximum scale that fits on screen
    // Leave 50px margin for window decorations
    int maxScaleX = (screenWidth - 50) / AGNES_SCREEN_WIDTH;
    int maxScaleY = (screenHeight - 50) / AGNES_SCREEN_HEIGHT;
    
    // Use the smaller of the two to maintain aspect ratio
    int scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;
    
    // Minimum scale of 1, ensure we have reasonable scaling
    scaleX = (scale < 1) ? 1 : scale;
    scaleY = scaleX;
    
    std::cout << "Screen scale: " << scaleX << "x\n";
}

void NESEmulation::initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
        return;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // Get display dimensions
    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    
    std::cout << "Display resolution: " << displayMode.w << "x" << displayMode.h << "\n";
    
    // Calculate optimal scaling
    calculateScale(displayMode.w, displayMode.h);
    
    // Create window
    int windowWidth = AGNES_SCREEN_WIDTH * scaleX;
    int windowHeight = AGNES_SCREEN_HEIGHT * scaleY;
    
    window = SDL_CreateWindow(
        "NES Emulator - Agnes",
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
    
    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Failed to create SDL renderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return;
    }
    
    // RGBA32 texture upload is robust across platforms and driver backends.
    screenTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        AGNES_SCREEN_WIDTH,
        AGNES_SCREEN_HEIGHT
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
    
    std::cout << "SDL2 initialized successfully. Window: " << windowWidth << "x" << windowHeight << "\n";
}

void NESEmulation::cleanupSDL()
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

void NESEmulation::setDefaultBindings()
{
    keyBindings = {
        SDLK_UP,
        SDLK_DOWN,
        SDLK_LEFT,
        SDLK_RIGHT,
        SDLK_z,
        SDLK_x,
        SDLK_a,
        SDLK_s
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

void NESEmulation::listControllers() const
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

bool NESEmulation::openControllerByIndex(int index)
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

void NESEmulation::configureInputMenu()
{
    while (true) {
        std::cout << "\n=== Input Configuration ===\n";
        std::cout << "1) Input mode: Keyboard only\n";
        std::cout << "2) Input mode: Controller only\n";
        std::cout << "3) Input mode: Keyboard + Controller\n";
        std::cout << "4) Remap keyboard keys\n";
        std::cout << "5) Select controller\n";
        std::cout << "6) Remap controller buttons\n";
        std::cout << "0) Start game\n";
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
                {"SELECT", &keyBindings.select},
                {"START", &keyBindings.start}
            };

            std::cout << "Enter key names recognized by SDL (e.g. Up, Down, Left, Right, Z, X, A, S).\n";
            std::cout << "Press Enter to keep current binding.\n";

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
}

void NESEmulation::handleKeyboardEvent(SDL_Keycode key, bool pressed, agnes_input_t& input, bool& running)
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

void NESEmulation::handleControllerButtonEvent(Uint8 button, bool pressed, agnes_input_t& input, bool& running)
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

void NESEmulation::renderFrame()
{
    if (!renderer || !screenTexture) {
        return;
    }

    for (int y = 0; y < AGNES_SCREEN_HEIGHT; y++) {
        for (int x = 0; x < AGNES_SCREEN_WIDTH; x++) {
            agnes_color_t color = agnes_get_screen_pixel(emulator, x, y);

            const int offset = (y * AGNES_SCREEN_WIDTH + x) * 4;
            frameBuffer[offset + 0] = color.r;
            frameBuffer[offset + 1] = color.g;
            frameBuffer[offset + 2] = color.b;
            frameBuffer[offset + 3] = 255;
        }
    }

    if (SDL_UpdateTexture(screenTexture, nullptr, frameBuffer.data(), AGNES_SCREEN_WIDTH * 4) < 0) {
        std::cerr << "Failed to update texture: " << SDL_GetError() << "\n";
        return;
    }
    
    // Clear and render
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    // Create scaled rectangle
    SDL_Rect destRect = {0, 0, AGNES_SCREEN_WIDTH * scaleX, AGNES_SCREEN_HEIGHT * scaleY};
    SDL_RenderCopy(renderer, screenTexture, nullptr, &destRect);
    
    SDL_RenderPresent(renderer);
}

void NESEmulation::nextFrame()
{
    if (!emulator || !isRunning) {
        return;
    }

    if (!agnes_next_frame(emulator)) {
        std::cerr << "Emulator failed to advance frame. This ROM may not be supported by Agnes.\n";
        isRunning = false;
    }
}

void NESEmulation::setInput(const agnes_input_t& input)
{
    if (!emulator || !isRunning) {
        return;
    }

    agnes_set_input(emulator, &input, nullptr);
}

agnes_color_t NESEmulation::getPixel(int x, int y) const
{
    if (!emulator || !isRunning) {
        return {0, 0, 0, 0};
    }

    if (x < 0 || x >= AGNES_SCREEN_WIDTH || y < 0 || y >= AGNES_SCREEN_HEIGHT) {
        return {0, 0, 0, 0};
    }

    return agnes_get_screen_pixel(emulator, x, y);
}

// Helper function to display NES frame as ASCII art (simple visualization)
void NESEmulation::displayFrame() const
{
    if (!emulator || !isRunning) {
        return;
    }

    // Simple ASCII representation - sample every 8th pixel
    for (int y = 0; y < AGNES_SCREEN_HEIGHT; y += 8) {
        for (int x = 0; x < AGNES_SCREEN_WIDTH; x += 8) {
            agnes_color_t pixel = agnes_get_screen_pixel(emulator, x, y);
            
            // Convert RGB to grayscale
            int gray = (pixel.r + pixel.g + pixel.b) / 3;
            
            if (gray > 192) {
                std::cout << "█";
            } else if (gray > 128) {
                std::cout << "▓";
            } else if (gray > 64) {
                std::cout << "▒";
            } else {
                std::cout << "░";
            }
        }
        std::cout << "\n";
    }
}

void NESEmulation::runInteractive()
{
    if (!emulator || !isRunning) {
        std::cerr << "No ROM loaded.\n";
        return;
    }

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "Failed to initialize controller subsystem: " << SDL_GetError() << "\n";
    }

    // Try to auto-open the first compatible controller.
    if (SDL_NumJoysticks() > 0) {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                openControllerByIndex(i);
                break;
            }
        }
    }

    configureInputMenu();

    initSDL();

    if (!window || !renderer) {
        std::cerr << "Failed to initialize SDL graphics.\n";
        return;
    }

    if (inputMode == InputMode::ControllerOnly && !controller) {
        std::cout << "Controller-only mode selected, but no controller is active. Falling back to keyboard.\n";
        inputMode = InputMode::KeyboardOnly;
    }
    
    std::cout << "\n=== NES Emulator Running ===\n";
    std::cout << "ROM: " << romPath << "\n";
    std::cout << "Window opened. Close it to exit.\n";
    std::cout << "Press ESC to exit emulator.\n\n";

    agnes_input_t input = {};
    bool running = true;
    bool windowClosed = false;

    while (running && !windowClosed) {
        // Handle SDL events
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
        
        // Update input
        setInput(input);
        
        // Emulate next frame
        nextFrame();

        if (!isRunning) {
            running = false;
            break;
        }
        
        // Render frame
        renderFrame();
    }
    
    cleanupSDL();
    std::cout << "\nEmulation stopped.\n";
}
