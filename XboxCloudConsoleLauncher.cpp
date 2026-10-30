#include "ConsoleLaunchers.hpp"
#include "XboxCloud.hpp"
#include "ShellMessage.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace {

bool runningInWSL() {
    std::ifstream version("/proc/version");
    std::string line;
    if (!std::getline(version, line)) return false;
    return line.find("Microsoft") != std::string::npos ||
           line.find("microsoft") != std::string::npos;
}

// Reads one key from ~/.omnicon/xbox.conf (key=value lines).
std::string xboxConfValue(const std::string& wanted) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    std::ifstream conf(std::string(home) + "/.omnicon/xbox.conf");
    std::string line;
    while (std::getline(conf, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.find('#') == 0) continue;
        auto trim = [](std::string s) {
            const char* ws = " \t\r\n";
            s.erase(0, s.find_first_not_of(ws) == std::string::npos ? s.size() : s.find_first_not_of(ws));
            s.erase(s.find_last_not_of(ws) == std::string::npos ? 0 : s.find_last_not_of(ws) + 1);
            return s;
        };
        std::string key = trim(line.substr(0, eq));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key == wanted) return trim(line.substr(eq + 1));
    }
    return {};
}

}  // namespace

bool XboxCloudConsoleLauncher::launch(const std::string& contentPath) const {
    // contentPath is "<productId>" (streamable), "<productId>:store" (owned, not
    // streamable) or kAddGamesId (the "+" tile). The id goes into a URL, so
    // refuse anything that is not a plain alphanumeric product id.
    std::string id = contentPath;
    bool storeOnly = false;
    if (id.size() > 6 && id.compare(id.size() - 6, 6, ":store") == 0) {
        storeOnly = true;
        id.erase(id.size() - 6);
    }
    if (id.empty() || !std::all_of(id.begin(), id.end(),
                                   [](unsigned char c) { return std::isalnum(c) != 0; })) {
        std::cerr << "[Xbox Cloud] Invalid product id: " << contentPath << "\n";
        return false;
    }
    std::string url;
    if (id == kAddGamesId)
        url = "https://www.xbox.com/play";
    else if (storeOnly)
        url = "https://www.xbox.com/games/store/-/" + id;
    else
        url = "https://www.xbox.com/play/launch/game/" + id;

    const char* envLauncher = std::getenv("OMNICON_XBOX_CLOUD_LAUNCHER");
    const std::string configured = envLauncher && *envLauncher
                                 ? std::string(envLauncher) : xboxConfValue("launcher");
    bool started = false;
    if (!configured.empty()) {
        started = launchExternalApplication(configured, {url});
    } else if (runningInWSL()) {
        // The browser lives on Windows; the empty argument is start's window title.
        started = launchExternalApplication("cmd.exe", {"/c", "start", "", url});
    } else {
        started = launchExternalApplication("xdg-open", {url});
    }

    if (!started) {
        std::cerr << "[Xbox Cloud] Could not open a browser. Set "
                     "OMNICON_XBOX_CLOUD_LAUNCHER to a browser command.\n";
        postShellMessage("Xbox Cloud: could not open a browser for " + url);
    }
    return started;
}
