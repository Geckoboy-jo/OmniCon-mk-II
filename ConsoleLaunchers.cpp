#include "top.hpp"
#include "RetroArchConsoleLauncher.hpp"
#include "ShellMessage.hpp"

#include <cstring>
#include <sstream>


namespace fs = std::filesystem;

static bool launchExternalChecked(const std::vector<std::string>& args) {
    if (args.empty()) return false;

    int statusPipe[2] = {};
    if (pipe(statusPipe) != 0) {
        std::cerr << "[ConsoleLauncher] Could not create launcher status pipe.\n";
        return false;
    }
    fcntl(statusPipe[1], F_SETFD, fcntl(statusPipe[1], F_GETFD) | FD_CLOEXEC);

    const pid_t pid = fork();
    if (pid < 0) {
        close(statusPipe[0]);
        close(statusPipe[1]);
        std::cerr << "[ConsoleLauncher] Could not fork external launcher.\n";
        return false;
    }
    if (pid == 0) {
        close(statusPipe[0]);
        setsid();
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        const int error = errno;
        (void)!write(statusPipe[1], &error, sizeof(error));
        _exit(127);
    }

    close(statusPipe[1]);
    int error = 0;
    const ssize_t bytesRead = read(statusPipe[0], &error, sizeof(error));
    close(statusPipe[0]);
    if (bytesRead == sizeof(error)) {
        waitpid(pid, nullptr, 0);
        std::cerr << "[ConsoleLauncher] Could not start " << args.front() << ": "
                  << std::strerror(error) << "\n";
        return false;
    }
    return true;
}

bool ConsoleLauncher::launchExternalApplication(
        const std::string& launcher,
        const std::vector<std::string>& arguments) {
    std::vector<std::string> args;
    args.reserve(arguments.size() + 1);
    args.push_back(launcher);
    args.insert(args.end(), arguments.begin(), arguments.end());
    return launchExternalChecked(args);
}

bool RetroArchConsoleLauncher::launchRetroArchWithSystemFiles(
        const std::string& systemName,
        const char* coreEnvironmentVariable,
        const char* defaultCore,
        const std::string& contentPath,
        const char* requiredFiles) {
    static const char* const kSystemDirs[] = {"Misc BIOS", "third_party/libretro/system"};
    auto present = [&](const std::string& name) {
        for (const char* dir : kSystemDirs)
            if (fs::exists(fs::path(dir) / name)) return true;
        return false;
    };

    std::stringstream required(requiredFiles ? requiredFiles : "");
    std::string entry;
    while (std::getline(required, entry, ';')) {
        if (entry.empty()) continue;
        bool found = false;
        std::stringstream alternatives(entry);
        std::string name;
        while (std::getline(alternatives, name, '|'))
            found = found || present(name);
        if (!found) {
            std::cerr << "[" << systemName << "] Missing system file: "
                      << entry << " (looked in Misc BIOS/ and third_party/libretro/system/)."
                         " The game may not start without it.\n";
            postShellMessage(systemName + ": missing BIOS/system file " + entry +
                             " (put it in Misc BIOS/). The game may not start.");
        }
    }
    return launchRetroArch(systemName, coreEnvironmentVariable, defaultCore, contentPath);
}

bool RetroArchConsoleLauncher::launchRetroArch(
        const std::string& systemName,
        const char* coreEnvironmentVariable,
        const char* defaultCore,
        const std::string& contentPath) {
    const char* configuredCore = std::getenv(coreEnvironmentVariable);
    const std::string core = configuredCore && *configuredCore
                           ? configuredCore
                           : (fs::path("third_party/libretro") / defaultCore).string();
    if (!fs::exists(core)) {
        std::cerr << "[" << systemName << "] RetroArch core not found: " << core << "\n"
                  << "Set " << coreEnvironmentVariable << " to the core's full path.\n";
        postShellMessage(systemName + ": libretro core not found (" +
                         fs::path(core).filename().string() + "). Run scripts/fetch_emulators.sh.");
        return false;
    }

    const char* configuredLauncher = std::getenv("OMNICON_RETROARCH_LAUNCHER");
    const std::string launcher = configuredLauncher && *configuredLauncher
                               ? configuredLauncher : "retroarch";
    // The RetroArch AppImage bundles an old libwayland-client that lacks
    // wl_proxy_marshal_flags, which the system NVIDIA EGL library needs. Its
    // AppRun overrides LD_LIBRARY_PATH, so preload the system copy instead.
    std::vector<std::string> command = {"env"};
    const std::string systemWayland = "/usr/lib/x86_64-linux-gnu/libwayland-client.so.0";
    if (fs::exists(systemWayland))
        command.push_back("LD_PRELOAD=" + systemWayland);
    command.push_back(launcher);
    // RetroArch has no autoconfig profile for the WSL controller bridge pad, so
    // supply the player 1 bindings explicitly.
    const fs::path controllerConfig = fs::absolute("retroarch_controller.cfg");
    if (fs::exists(controllerConfig)) {
        command.push_back("--appendconfig");
        command.push_back(controllerConfig.string());
    }
    command.insert(command.end(), {"-L", fs::absolute(core).string(),
                                   fs::absolute(contentPath).string()});
    if (!launchExternalChecked(command)) {
        std::cerr << "[" << systemName << "] Failed to start RetroArch. Set "
                     "OMNICON_RETROARCH_LAUNCHER to its executable.\n";
        return false;
    }
    return true;
}
