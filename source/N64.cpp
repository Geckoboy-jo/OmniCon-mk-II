#include "N64.hpp"

#include <dlfcn.h>
#include <mupen64plus/m64p_common.h>
#include <mupen64plus/m64p_frontend.h>
#include <mupen64plus/m64p_plugin.h>
#include <mupen64plus/m64p_types.h>

#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ─── Callbacks ───────────────────────────────────────────────────────────────

static void n64DebugCb(void *, int level, const char *msg) {
  if (level <= M64MSG_WARNING)
    std::cerr << "[N64] " << msg << "\n";
}

static void n64StateCb(void *, m64p_core_param, int) {}

// ─── Backend ─────────────────────────────────────────────────────────────────

class Mupen64PlusBackend {
  static bool n64DebugEnabled() {
    static const bool v = (std::getenv("OMNICON_N64_DEBUG") != nullptr);
    return v;
  }
  void logState(const std::string &msg) const {
    if (n64DebugEnabled())
      std::cerr << "[N64][VERBOSE] " << msg << "\n";
  }
  static constexpr const char *CORE_SO =
      "/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2";
  static constexpr const char *VIDEO_SO =
      "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-video-glide64mk2.so";
  static constexpr const char *AUDIO_SO =
      "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-audio-sdl.so";
  static constexpr const char *INPUT_SO =
      "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-input-sdl.so";
  static constexpr const char *RSP_SO =
      "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-rsp-hle.so";
  static constexpr const char *DATA_DIR = "/usr/share/games/mupen64plus";
  static constexpr int API_VER = 0x020001;

  void *coreHandle = nullptr;
  void *videoHandle = nullptr;
  void *audioHandle = nullptr;
  void *inputHandle = nullptr;
  void *rspHandle = nullptr;

  ptr_CoreStartup fnCoreStartup = nullptr;
  ptr_CoreShutdown fnCoreShutdown = nullptr;
  ptr_CoreAttachPlugin fnCoreAttachPlugin = nullptr;
  ptr_CoreDetachPlugin fnCoreDetachPlugin = nullptr;
  ptr_CoreDoCommand fnCoreDoCommand = nullptr;

  std::atomic<bool> emulRunning{false};
  std::string configDir;

  bool coreInited = false;
  bool pluginsReady = false;
  bool romOpened = false;

public:
  ~Mupen64PlusBackend() { teardown(); }

  bool init(const std::string &cfgDir) {
    logState("init() called with cfgDir: " + cfgDir);
    configDir = cfgDir;
    fs::create_directories(configDir);

    coreHandle = dlopen(CORE_SO, RTLD_NOW | RTLD_GLOBAL);
    logState("dlopen(CORE_SO) returned: " +
             std::to_string((uintptr_t)coreHandle));
    if (!coreHandle) {
      logState("dlopen failed for CORE_SO");
      std::cerr << "[N64] Cannot load " << CORE_SO << ": " << dlerror() << "\n";
      return false;
    }

#define LOAD(sym, type)                                                        \
  fn##sym = reinterpret_cast<type>(dlsym(coreHandle, #sym));                   \
  logState(std::string("Loaded symbol: ") + #sym);                             \
  if (!fn##sym) {                                                              \
    std::cerr << "[N64] Missing symbol: " << #sym << "\n";                     \
    return false;                                                              \
  }

    LOAD(CoreStartup, ptr_CoreStartup)
    LOAD(CoreShutdown, ptr_CoreShutdown)
    LOAD(CoreAttachPlugin, ptr_CoreAttachPlugin)
    LOAD(CoreDetachPlugin, ptr_CoreDetachPlugin)
    LOAD(CoreDoCommand, ptr_CoreDoCommand)
#undef LOAD

    logState("Calling fnCoreStartup");
    m64p_error e = fnCoreStartup(API_VER, configDir.c_str(), DATA_DIR, nullptr,
                                 n64DebugCb, nullptr, n64StateCb);
    if (e != M64ERR_SUCCESS) {
      logState("fnCoreStartup failed with error code: " + std::to_string(e));
      std::cerr << "[N64] CoreStartup failed: " << e << "\n";
      return false;
    }

    // Print the config directory and expected config file path
    std::cerr << "[N64][DEBUG] Config directory: " << configDir << std::endl;
    std::cerr << "[N64][DEBUG] Expected config file: " << configDir
              << "/mupen64plus.cfg" << std::endl;

    // --- RUNTIME KEY REMAPPING VIA CONFIG API ---
    // This block uses the mupen64plus config API to set key bindings at
    // runtime. It must be done after CoreStartup but before loading plugins.
    typedef m64p_error (*ptr_ConfigGetSection)(const char *, m64p_handle *);
    typedef m64p_error (*ptr_ConfigSetString)(m64p_handle, const char *,
                                              const char *);
    typedef m64p_error (*ptr_ConfigSectionCount)(int *);
    typedef m64p_error (*ptr_ConfigGetSectionName)(int, const char **,
                                                   m64p_handle *);
    typedef m64p_error (*ptr_ConfigListParameters)(m64p_handle, const char ***,
                                                   int *);
    typedef m64p_error (*ptr_ConfigGetParameterType)(m64p_handle, const char *,
                                                     int *);
    typedef m64p_error (*ptr_ConfigGetParameter)(m64p_handle, const char *,
                                                 void *, int);
    typedef m64p_error (*ptr_ConfigSaveFile)(void);

    auto fnConfigGetSection =
        (ptr_ConfigGetSection)dlsym(coreHandle, "ConfigGetSection");
    auto fnConfigSetString =
        (ptr_ConfigSetString)dlsym(coreHandle, "ConfigSetParameter");
    auto fnConfigSectionCount =
        (ptr_ConfigSectionCount)dlsym(coreHandle, "ConfigSectionCount");
    auto fnConfigGetSectionName =
        (ptr_ConfigGetSectionName)dlsym(coreHandle, "ConfigGetSectionName");
    auto fnConfigListParameters =
        (ptr_ConfigListParameters)dlsym(coreHandle, "ConfigListParameters");
    auto fnConfigGetParameterType =
        (ptr_ConfigGetParameterType)dlsym(coreHandle, "ConfigGetParameterType");
    auto fnConfigGetParameter =
        (ptr_ConfigGetParameter)dlsym(coreHandle, "ConfigGetParameter");
    auto fnConfigSaveFile =
        (ptr_ConfigSaveFile)dlsym(coreHandle, "ConfigSaveFile");

    if (fnConfigGetSection && fnConfigSetString && fnConfigSectionCount &&
        fnConfigGetSectionName && fnConfigListParameters &&
        fnConfigGetParameterType && fnConfigGetParameter && fnConfigSaveFile) {
      int sectionCount = 0;
      if (fnConfigSectionCount(&sectionCount) == M64ERR_SUCCESS) {
        std::cerr << "[N64][DEBUG] Config section count: " << sectionCount
                  << std::endl;
        for (int i = 0; i < sectionCount; ++i) {
          const char *sectionName = nullptr;
          m64p_handle sectionHandle = nullptr;
          if (fnConfigGetSectionName(i, &sectionName, &sectionHandle) ==
                  M64ERR_SUCCESS &&
              sectionName) {
            std::cerr << "[N64][DEBUG] Section " << i << ": " << sectionName
                      << std::endl;
            // Only set keys for Input-SDL-Control* sections
            std::string secName(sectionName);
            if (secName.find("Input-SDL-Control") == 0) {
              // Print all keys in this section
              const char **paramList = nullptr;
              int paramCount = 0;
              if (fnConfigListParameters(sectionHandle, &paramList,
                                         &paramCount) == M64ERR_SUCCESS &&
                  paramList) {
                std::cerr << "[N64][DEBUG] Keys in section '" << secName
                          << "':" << std::endl;
                for (int p = 0; p < paramCount; ++p) {
                  std::cerr << "    " << paramList[p] << std::endl;
                }
              }
              // Set key bindings using key() format for mupen64plus-input-sdl
              struct KeyMap {
                const char *key;
                const char *value;
              } keymap[] = {{"A", "key(N)"},          {"B", "key(M)"},
                            {"Start", "key(Z)"},      {"DPad R", "key(D)"},
                            {"DPad L", "key(A)"},     {"DPad D", "key(S)"},
                            {"DPad U", "key(W)"},     {"C Button R", "key(K)"},
                            {"C Button L", "key(J)"}, {"C Button D", "key(4)"},
                            {"C Button U", "key(1)"}, {"L", "key(Q)"},
                            {"R", "key(E)"},          {"Z", "key(1)"},
                            {"Select", "key(X)"},     {"Home", "key(Tab)"},
                            {"L3", "key(P)"},         {"R3", "key(O)"},
                            {"Fire", "key(Space)"}};
              for (const auto &km : keymap) {
                m64p_error setRes =
                    fnConfigSetString(sectionHandle, km.key, km.value);
                std::cerr << "[N64][DEBUG] Set '" << km.key << "' = '"
                          << km.value << "' in section '" << secName
                          << "': " << (setRes == M64ERR_SUCCESS ? "OK" : "FAIL")
                          << std::endl;
              }
            }
          }
        }
        // ─── Audio buffer: increase to prevent periodic freezes ──────────────
        // ConfigGetSection fails for sections that don't exist yet;
        // ConfigOpenSection creates the section if absent, which is necessary
        // here because Video-General and Audio-SDL are only written by plugins
        // on their first run.
        typedef m64p_error (*ptr_ConfigOpenSection)(const char *,
                                                    m64p_handle *);
        typedef m64p_error (*ptr_ConfigSetParam)(m64p_handle, const char *,
                                                 m64p_type, const void *);
        auto fnConfigOpenSection =
            (ptr_ConfigOpenSection)dlsym(coreHandle, "ConfigOpenSection");
        auto fnConfigSetParam =
            (ptr_ConfigSetParam)dlsym(coreHandle, "ConfigSetParameter");
        if (fnConfigOpenSection && fnConfigSetParam) {
          // SECONDARY_BUFFER_SIZE = 2048 (default) causes the SDL audio thread
          // to stall the emulation thread every ~60ms. 8192 eliminates those
          // hitches.
          m64p_handle audioSection = nullptr;
          if (fnConfigOpenSection("Audio-SDL", &audioSection) ==
                  M64ERR_SUCCESS &&
              audioSection) {
            int bufSize = 8192;
            fnConfigSetParam(audioSection, "SECONDARY_BUFFER_SIZE", M64TYPE_INT,
                             &bufSize);
            logState("[N64] Audio-SDL SECONDARY_BUFFER_SIZE set to 8192");
          }
          // ─── Fullscreen
          // ───────────────────────────────────────────────────────
          m64p_handle videoGenSection = nullptr;
          if (fnConfigOpenSection("Video-General", &videoGenSection) ==
                  M64ERR_SUCCESS &&
              videoGenSection) {
            int trueVal = 1;
            fnConfigSetParam(videoGenSection, "Fullscreen", M64TYPE_BOOL,
                             &trueVal);
            logState("[N64] Video-General Fullscreen set to true");
          }
        }

        fnConfigSaveFile();
        logState("[N64] Runtime key remapping applied to all "
                 "Input-SDL-Control* sections via config API");
      } else {
        logState("[N64] Failed to enumerate config sections for runtime remap");
      }
    } else {
      logState("[N64] Mupen64Plus config API not available for runtime remap");
    }

    // ─── Performance & display config ─────────────────────────────────────
    // The block above is dead code (ConfigSectionCount is not a real symbol).
    // This block uses only standard mupen64plus API symbols and always runs.
    //
    // ConfigSetParameter only works on EXISTING parameters.
    // Audio-SDL params are registered by the audio plugin during PluginStartup
    // (which runs later), so they don't exist yet — ConfigSetParameter silently
    // fails.  The fix: call ConfigSetDefaultInt first (creates if absent), then
    // ConfigSetParameter (overrides the value if it already existed on disk).
    {
      typedef m64p_error (*ptr_ConfigOpenSection)(const char *, m64p_handle *);
      typedef m64p_error (*ptr_ConfigSetParam)(m64p_handle, const char *,
                                               m64p_type, const void *);
      typedef m64p_error (*ptr_ConfigSetDefInt)(m64p_handle, const char *, int,
                                                const char *);
      typedef m64p_error (*ptr_ConfigSetDefBool)(m64p_handle, const char *, int,
                                                 const char *);
      typedef m64p_error (*ptr_ConfigSaveFile)(void);
      auto fnOpen =
          (ptr_ConfigOpenSection)dlsym(coreHandle, "ConfigOpenSection");
      auto fnSet = (ptr_ConfigSetParam)dlsym(coreHandle, "ConfigSetParameter");
      auto fnSetDefInt =
          (ptr_ConfigSetDefInt)dlsym(coreHandle, "ConfigSetDefaultInt");
      auto fnSetDefBool =
          (ptr_ConfigSetDefBool)dlsym(coreHandle, "ConfigSetDefaultBool");
      auto fnSave = (ptr_ConfigSaveFile)dlsym(coreHandle, "ConfigSaveFile");

      // Helper lambdas: create-or-override for int and bool params.
      auto setInt = [&](m64p_handle h, const char *name, int val) {
        fnSetDefInt(h, name, val, ""); // creates with val if param is absent
        fnSet(h, name, M64TYPE_INT,
              &val); // overrides if param already existed on disk
      };
      auto setBool = [&](m64p_handle h, const char *name, int val) {
        fnSetDefBool(h, name, val, "");
        fnSet(h, name, M64TYPE_BOOL, &val);
      };

      if (fnOpen && fnSet && fnSetDefInt && fnSetDefBool && fnSave) {
        // ── Audio: disable emulation-thread stall ─────────────────────
        // mupen64plus-audio-sdl stalls the emulation thread in AiLenChanged
        // whenever primary_buffer_count > PRIMARY_BUFFER_TARGET.  On this
        // system audio routes through PulseAudio → RDP Sink, which can block
        // the audio callback for 10+ seconds.  Setting PRIMARY_BUFFER_TARGET
        // strictly above PRIMARY_BUFFER_SIZE makes the condition impossible:
        // the buffer can never exceed its own max size, so the stall never
        // fires.  The core's timer-based limiter maintains correct 60 fps.
        m64p_handle audio = nullptr;
        if (fnOpen("Audio-SDL", &audio) == M64ERR_SUCCESS && audio) {
          setInt(audio, "PRIMARY_BUFFER_SIZE", 16384);
          setInt(audio, "PRIMARY_BUFFER_TARGET",
                 32768); // > SIZE → stall never triggers
          setInt(audio, "SECONDARY_BUFFER_SIZE", 4096);
        }
        // ── Video: fullscreen at native display resolution ────────────────
        // Default ScreenWidth/ScreenHeight is 320×240 (N64 native). On a
        // 2560×1440 display that renders a tiny square in the corner.
        // SDL_GetCurrentDisplayMode gives us the real resolution at runtime.
        m64p_handle video = nullptr;
        if (fnOpen("Video-General", &video) == M64ERR_SUCCESS && video) {
          setBool(video, "Fullscreen", 1);
          setBool(video, "VerticalSync", 0);
          SDL_DisplayMode dm{};
          int w = 1920, h = 1080;
          if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            w = dm.w;
            h = dm.h;
          }
          setInt(video, "ScreenWidth", w);
          setInt(video, "ScreenHeight", h);
        }
        // ── glide64mk2: disable its own vsync flag ────────────────────────
        m64p_handle glide = nullptr;
        if (fnOpen("Video-Glide64mk2", &glide) == M64ERR_SUCCESS && glide) {
          setBool(glide, "vsync", 0);
        }
        fnSave();
        logState("[N64] Audio and video config applied");
      } else {
        logState("[N64] Standard config API not available");
      }
    }

    coreInited = true;
    logState("init() complete, coreInited = true");
    return true;
  }

  bool loadPlugins() {
    logState("loadPlugins() called");
    typedef m64p_error (*PluginStartupFn)(m64p_dynlib_handle, void *,
                                          void (*)(void *, int, const char *));

    // ── Audio driver: prevent SDL_LockAudio → RDP Sink deadlock ──────────────
    // mupen64plus-audio-sdl calls SDL_LockAudio() before every buffer write.
    // SDL_LockAudio blocks the calling thread until the audio CALLBACK returns.
    // On this system the callback writes to PulseAudio → RDP Sink → Windows
    // audio.  When the RDP pipeline stalls (latency spike), the callback hangs
    // for 10-30 s; SDL_LockAudio blocks the emulation thread for the same
    // duration — video and audio both freeze simultaneously.
    //
    // Forcing SDL_AUDIODRIVER=dummy makes the callback a no-op that returns
    // instantly, so SDL_LockAudio never blocks.  The game is silent but smooth.
    // Set OMNICON_N64_AUDIO=pulseaudio (or any SDL driver name) to restore
    // sound.
    {
      const char *n64audio = std::getenv("OMNICON_N64_AUDIO");
      setenv("SDL_AUDIODRIVER", n64audio ? n64audio : "dummy", 1);
      if (!n64audio) {
        std::cerr << "[N64] Audio disabled to prevent RDP Sink freeze. "
                     "Set OMNICON_N64_AUDIO=pulseaudio to enable sound.\n";
      }
    }

    // ── Mesa GL environment (must be set before GL context creation inside
    // PluginStartup) MESA_GLTHREAD: offloads GL command submission to a worker
    // thread so the main emulation thread never blocks on GPU-side work.
    setenv("MESA_GLTHREAD", "true", 0);
    // vblank_mode=0: belt-and-suspenders vsync disable at the Mesa driver
    // level.
    setenv("vblank_mode", "0", 0);
    // Ensure the Mesa shader/PSO disk cache lives on fast Linux-native storage.
    // Without this, Mesa may try to write the cache to a Windows 9P mount which
    // is slow.  Cached PSOs are reused on subsequent runs — only the first play
    // of each game incurs compilation stalls.
    if (!std::getenv("MESA_SHADER_CACHE_DIR")) {
      static const std::string sCacheDir = [] {
        std::string d = "/root/.cache/mesa_shader_cache_n64";
        fs::create_directories(d);
        return d;
      }();
      setenv("MESA_SHADER_CACHE_DIR", sCacheDir.c_str(), 1);
    }

    // ── Video plugin selection
    // ───────────────────────────────────────────────── glide64mk2 generates
    // one Mesa D3D12 PSO per N64 color-combiner mode. Paper Mario alone has
    // hundreds of unique modes; each PSO compiles synchronously on the
    // emulation thread and can block for 1-30+ seconds. Rice uses a small fixed
    // set of GLSL programs → far fewer stalls. After the first play session the
    // cache warms up; set
    //   OMNICON_N64_VIDEO=glide64mk2
    // to use the accurate renderer (stalls only on first run of each game).
    const char *videoChoice = std::getenv("OMNICON_N64_VIDEO");
    if (!videoChoice)
      videoChoice = "rice";
    const std::string dynamicVideoSO =
        std::string(
            "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-video-") +
        videoChoice + ".so";
    std::cerr << "[N64] Video: mupen64plus-video-" << videoChoice << ".so"
              << "  (OMNICON_N64_VIDEO=glide64mk2 for higher accuracy)\n";

    struct {
      const char *path;
      void **hdl;
      m64p_plugin_type type;
    } entries[] = {
        {dynamicVideoSO.c_str(), &videoHandle, M64PLUGIN_GFX},
        {AUDIO_SO, &audioHandle, M64PLUGIN_AUDIO},
        {INPUT_SO, &inputHandle, M64PLUGIN_INPUT},
        {RSP_SO, &rspHandle, M64PLUGIN_RSP},
    };

    for (auto &p : entries) {
      logState(std::string("dlopen(") + p.path + ")");
      *p.hdl = dlopen(p.path, RTLD_NOW | RTLD_LOCAL);
      if (!*p.hdl) {
        logState(std::string("dlopen failed for ") + p.path);
        std::cerr << "[N64] Cannot load plugin " << p.path << ": " << dlerror()
                  << "\n";
        return false;
      }
      auto startup =
          reinterpret_cast<PluginStartupFn>(dlsym(*p.hdl, "PluginStartup"));
      logState(std::string("dlsym(PluginStartup) for ") + p.path);
      if (!startup) {
        logState(std::string("No PluginStartup symbol in ") + p.path);
        std::cerr << "[N64] No PluginStartup in " << p.path << "\n";
        return false;
      }
      if (startup(coreHandle, nullptr, n64DebugCb) != M64ERR_SUCCESS) {
        logState(std::string("PluginStartup failed for ") + p.path);
        std::cerr << "[N64] PluginStartup failed: " << p.path << "\n";
        return false;
      }
      if (fnCoreAttachPlugin(p.type, *p.hdl) != M64ERR_SUCCESS) {
        logState(std::string("CoreAttachPlugin failed for type ") +
                 std::to_string(p.type));
        std::cerr << "[N64] CoreAttachPlugin failed, type=" << p.type << "\n";
        return false;
      }
    }
    pluginsReady = true;
    logState("loadPlugins() complete, pluginsReady = true");
    return true;
  }

  bool openRom(const std::string &path) {
    logState("openRom() called with path: " + path);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    logState("Attempting to open ROM file");
    if (!f) {
      logState("ROM file open failed");
      std::cerr << "[N64] Cannot open ROM: " << path << "\n";
      return false;
    }
    std::streamsize sz = f.tellg();
    logState("ROM file size: " + std::to_string(sz));
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char *>(buf.data()), sz);

    logState("Calling fnCoreDoCommand(M64CMD_ROM_OPEN)");
    if (fnCoreDoCommand(M64CMD_ROM_OPEN, static_cast<int>(buf.size()),
                        buf.data()) != M64ERR_SUCCESS) {
      logState("ROM_OPEN failed");
      std::cerr << "[N64] ROM_OPEN failed\n";
      return false;
    }
    romOpened = true;
    logState("openRom() complete, romOpened = true");
    return true;
  }

  // Run emulation synchronously on the calling thread (must be the main
  // thread). Mature frontends (mupen64plus-ui-console, RetroArch) all do this —
  // SDL event pumping and OpenGL rendering in the plugins must happen on the
  // main thread. Spawning a background thread caused the WSLg compositor to
  // starve of events and freeze for 10+ seconds periodically.
  void execute() {
    logState("execute() called — running M64CMD_EXECUTE on calling thread");
    if (!fnCoreDoCommand) {
      logState("execute() aborted: fnCoreDoCommand is null");
      return;
    }

    // Watchdog: prints a heartbeat every second to stderr.
    // During a freeze, watch the terminal output:
    //   heartbeats STOP     → whole process suspended (WSL2/Hyper-V stall —
    //   OS-level) heartbeats CONTINUE → main thread stuck in GL/emulation call
    //   (fixable in-process)
    std::atomic<bool> wdStop{false};
    std::thread watchdog([&]() {
      using namespace std::chrono;
      int tick = 0;
      while (!wdStop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(seconds(1));
        if (!wdStop.load(std::memory_order_relaxed)) {
          std::cerr << "[N64-watchdog] " << ++tick << "s\n";
          std::cerr.flush();
        }
      }
    });

    emulRunning = true;
    fnCoreDoCommand(M64CMD_EXECUTE, 0, nullptr);
    emulRunning = false;

    wdStop = true;
    watchdog.join();
    logState("execute() returned");
  }

  void requestStop() {
    logState("requestStop() called");
    if (emulRunning && fnCoreDoCommand) {
      logState("Calling fnCoreDoCommand(M64CMD_STOP)");
      fnCoreDoCommand(M64CMD_STOP, 0, nullptr);
    }
  }

  void teardown() {
    logState("teardown() called");
    requestStop();
    logState("requestStop() returned");
    // No waitForStop() needed: execute() is synchronous, so emulation has
    // already ended before teardown() is ever called.

    // Restore SDL audio driver so other emulators choose their own backend.
    unsetenv("SDL_AUDIODRIVER");

    if (romOpened && fnCoreDoCommand) {
      logState("Closing ROM via fnCoreDoCommand(M64CMD_ROM_CLOSE)");
      fnCoreDoCommand(M64CMD_ROM_CLOSE, 0, nullptr);
      romOpened = false;
    }

    if (pluginsReady && fnCoreDetachPlugin) {
      logState("Detaching plugins");
      fnCoreDetachPlugin(M64PLUGIN_RSP);
      fnCoreDetachPlugin(M64PLUGIN_INPUT);
      fnCoreDetachPlugin(M64PLUGIN_AUDIO);
      fnCoreDetachPlugin(M64PLUGIN_GFX);
      pluginsReady = false;
    }

    typedef m64p_error (*PlugShutFn)(void);
    void *plugHandles[] = {rspHandle, inputHandle, audioHandle, videoHandle};
    for (int i = 0; i < 4; ++i) {
      logState(std::string("Detaching plugin index ") + std::to_string(i));
      void *h = plugHandles[i];
      if (h) {
        auto fn = reinterpret_cast<PlugShutFn>(dlsym(h, "PluginShutdown"));
        if (fn)
          fn();
        // dlclose(h); // Disabled: unloading plugins can crash on some hosts
      }
    }
    rspHandle = inputHandle = audioHandle = videoHandle = nullptr;

    if (coreInited && fnCoreShutdown) {
      logState("Shutting down core via fnCoreShutdown");
      fnCoreShutdown();
      coreInited = false;
    }
    if (coreHandle) {
      logState("coreHandle non-null, would dlclose if enabled");
      // dlclose(coreHandle); // Disabled: see above
      coreHandle = nullptr;
    }
    logState("teardown() complete");
  }

  bool isRomOpen() const { return romOpened; }
  bool isRunning() const { return emulRunning.load(); }
};

// ─── Static member definitions ───────────────────────────────────────────────

N64Emulation::InputMode N64Emulation::globalInputMode =
    N64Emulation::InputMode::KeyboardOnly;

N64Emulation::KeyBindings N64Emulation::globalKeyBindings = {
    /* up      */ SDLK_w, // Up = W
    /* down    */ SDLK_s, // Down = S
    /* left    */ SDLK_a, // Left = A
    /* right   */ SDLK_d, // Right = D
    /* a       */ SDLK_n, // A = N
    /* b       */ SDLK_m, // B = M
    /* c_up    */ SDLK_j, // C-Up = J
    /* c_down  */ SDLK_k, // C-Down = K
    /* c_left  */ SDLK_q, // C-Left = Q
    /* c_right */ SDLK_e, // C-Right = E
    /* l       */ SDLK_q, // L = Q (l1)
    /* r       */ SDLK_e, // R = E (r1)
    /* z       */ SDLK_1, // Z = 1 (l2)
    /* start   */ SDLK_z, // Start = Z
};

N64Emulation::ControllerBindings N64Emulation::globalControllerBindings = {
    /* up      */ SDL_CONTROLLER_BUTTON_DPAD_UP,
    /* down    */ SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    /* left    */ SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    /* right   */ SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    /* a       */ SDL_CONTROLLER_BUTTON_A,
    /* b       */ SDL_CONTROLLER_BUTTON_B,
    /* c_up    */ SDL_CONTROLLER_BUTTON_Y,
    /* c_down  */ SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    /* c_left  */ SDL_CONTROLLER_BUTTON_LEFTSTICK,
    /* c_right */ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    /* l       */ SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    /* r       */ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    /* z       */ SDL_CONTROLLER_BUTTON_BACK,
    /* start   */ SDL_CONTROLLER_BUTTON_START,
};

bool N64Emulation::globalInputConfigured = false;

// ─── N64Emulation ────────────────────────────────────────────────────────────

N64Emulation::N64Emulation() : backend(std::make_unique<Mupen64PlusBackend>()) {
  static const bool n64Debug = (std::getenv("OMNICON_N64_DEBUG") != nullptr);
  if (n64Debug)
    std::cerr << "[N64][DEBUG] N64Emulation constructed\n";
}

N64Emulation::~N64Emulation() { unloadRom(); }

bool N64Emulation::isLoaded() const { return backend && backend->isRomOpen(); }

bool N64Emulation::loadRom(const std::string &path) {
  static const bool n64Debug = (std::getenv("OMNICON_N64_DEBUG") != nullptr);
  if (n64Debug)
    std::cerr << "[N64][DEBUG] loadRom called with path: " << path << "\n";
  if (!fs::exists(path)) {
    std::cerr << "[N64] File not found: " << path << "\n";
    return false;
  }

  fs::create_directories("saves/n64");
  const std::string cfgDir = fs::absolute("saves/n64/config").string();

  if (!backend->init(cfgDir)) {
    if (n64Debug)
      std::cerr << "[N64][DEBUG] backend->init failed\n";
    return false;
  }
  if (!backend->openRom(path)) {
    if (n64Debug)
      std::cerr << "[N64][DEBUG] backend->openRom failed\n";
    return false;
  }
  if (!backend->loadPlugins()) {
    if (n64Debug)
      std::cerr << "[N64][DEBUG] backend->loadPlugins failed\n";
    return false;
  }

  romPath = path;
  std::cout << "[N64] Loaded: " << path << "\n";
  if (n64Debug)
    std::cerr << "[N64][DEBUG] loadRom success for: " << path << "\n";
  return true;
}

void N64Emulation::unloadRom() {
  if (backend)
    backend->teardown();
  romPath.clear();
  isRunning = false;
}

void N64Emulation::runInteractive() {
  static const bool n64Debug = (std::getenv("OMNICON_N64_DEBUG") != nullptr);
  if (n64Debug)
    std::cerr << "[N64][DEBUG] runInteractive called\n";
  if (!isLoaded()) {
    if (n64Debug)
      std::cerr << "[N64][DEBUG] runInteractive: not loaded\n";
    return;
  }

  std::cout << "[N64] Press ESC or close the game window to exit.\n";
  isRunning = true;
  backend->execute(); // blocks on this thread until the user closes the game
  isRunning = false;
  std::cout << "[N64] Emulation stopped.\n";
  if (n64Debug)
    std::cerr << "[N64][DEBUG] runInteractive finished\n";
}

void N64Emulation::configureGlobalControls() {
  std::cout
      << "\n[N64] Default Key Bindings (mupen64plus-input-sdl):\n"
      << "  D-Pad:                Arrow Keys\n"
      << "  Analog Stick:         Arrow Keys / WASD\n"
      << "  A button:             X\n"
      << "  B button:             Z\n"
      << "  Z Trigger:            A\n"
      << "  L / R Shoulder:       Q / E\n"
      << "  Start:                Enter\n"
      << "  C-Up / Down / Left / Right:  I / K / J / L\n"
      << "\n"
      << "  To customise bindings edit: saves/n64/config/mupen64plus.cfg\n";
  globalInputConfigured = true;
}

// ─── Save data ───────────────────────────────────────────────────────────────
// Bundle format: [magic "N64S"(4)] [file_count(u32)] then for each file:
//   [name_len(u16)] [name bytes] [data_len(u64)] [data bytes]
// Saves live in saves/n64/config/save/ as written by mupen64plus.

static const fs::path kN64SaveDir = "saves/n64/config/save";

std::vector<unsigned char> N64Emulation::dumpSaveData() const {
  static const bool n64Debug = (std::getenv("OMNICON_N64_DEBUG") != nullptr);
  if (n64Debug)
    std::cerr << "[N64][DEBUG] dumpSaveData called for: " << romPath << "\n";
  std::vector<std::pair<std::string, std::vector<unsigned char>>> files;
  std::error_code ec;
  if (!fs::exists(kN64SaveDir, ec))
    return {};

  for (const auto &entry : fs::directory_iterator(kN64SaveDir, ec)) {
    if (!entry.is_regular_file())
      continue;
    std::ifstream f(entry.path(), std::ios::binary | std::ios::ate);
    if (!f)
      continue;
    const auto sz = static_cast<std::streamsize>(f.tellg());
    if (sz <= 0)
      continue;
    f.seekg(0);
    std::vector<unsigned char> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char *>(buf.data()), sz);
    if (f.gcount() == sz)
      files.emplace_back(entry.path().filename().string(), std::move(buf));
  }

  if (files.empty())
    return {};

  std::vector<unsigned char> out;
  // magic
  out.insert(out.end(), {'N', '6', '4', 'S'});
  // file count
  const uint32_t cnt = static_cast<uint32_t>(files.size());
  out.insert(out.end(), reinterpret_cast<const unsigned char *>(&cnt),
             reinterpret_cast<const unsigned char *>(&cnt) + 4);
  for (const auto &[name, data] : files) {
    const uint16_t nlen = static_cast<uint16_t>(name.size());
    out.insert(out.end(), reinterpret_cast<const unsigned char *>(&nlen),
               reinterpret_cast<const unsigned char *>(&nlen) + 2);
    out.insert(out.end(), name.begin(), name.end());
    const uint64_t dlen = static_cast<uint64_t>(data.size());
    out.insert(out.end(), reinterpret_cast<const unsigned char *>(&dlen),
               reinterpret_cast<const unsigned char *>(&dlen) + 8);
    out.insert(out.end(), data.begin(), data.end());
  }
  return out;
}

bool N64Emulation::restoreSaveData(const std::vector<unsigned char> &blob) {
  if (blob.size() < 8)
    return false;
  if (blob[0] != 'N' || blob[1] != '6' || blob[2] != '4' || blob[3] != 'S')
    return false;

  uint32_t cnt = 0;
  std::memcpy(&cnt, blob.data() + 4, 4);

  std::error_code ec;
  fs::create_directories(kN64SaveDir, ec);

  size_t pos = 8;
  for (uint32_t i = 0; i < cnt; ++i) {
    if (pos + 2 > blob.size())
      return false;
    uint16_t nlen = 0;
    std::memcpy(&nlen, blob.data() + pos, 2);
    pos += 2;
    if (pos + nlen > blob.size())
      return false;
    const std::string name(reinterpret_cast<const char *>(blob.data() + pos),
                           nlen);
    pos += nlen;
    if (pos + 8 > blob.size())
      return false;
    uint64_t dlen = 0;
    std::memcpy(&dlen, blob.data() + pos, 8);
    pos += 8;
    if (pos + dlen > blob.size())
      return false;
    const fs::path dest = kN64SaveDir / name;
    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (f)
      f.write(reinterpret_cast<const char *>(blob.data() + pos),
              static_cast<std::streamsize>(dlen));
    pos += dlen;
  }
  return true;
}
