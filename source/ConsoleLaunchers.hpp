#pragma once

#include "top.hpp"
class ConsoleLauncher {
public:
    virtual ~ConsoleLauncher() = default;
    virtual bool launch(const std::string& contentPath) const = 0;

protected:
    static bool launchExternalApplication(const std::string& launcher,
                                          const std::vector<std::string>& arguments);
};

// PID of the most recently launched external game process.  The child calls
// setsid(), so the pid is also the process-group id: pause the whole emulator
// tree with kill(-pid, SIGSTOP) and resume it with kill(-pid, SIGCONT).
// -1 when no external game has been launched this session.
pid_t lastLaunchedGamePid();
void  setLastLaunchedGamePid(pid_t pid);

#include "RetroArchConsoleLauncher.hpp"