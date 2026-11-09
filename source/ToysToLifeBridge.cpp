#include "ToysToLifeBridge.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr const char *kDefaultPort = "8399";

int bridgePort() {
  const char *env = std::getenv("OMNICON_TTL_PORT");
  if (env && *env) {
    const int port = std::atoi(env);
    if (port > 0 && port < 65536)
      return port;
  }
  return std::atoi(kDefaultPort);
}

std::string jsonEscape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (const char c : value) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

// Sends one JSON line to the bridge app and reads one response line.
bool tcpRequest(const std::string &requestJson, std::string &responseLine,
                int timeoutMs = 1500) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;

  const timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(bridgePort()));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return false;
  }

  const std::string payload = requestJson + "\n";
  size_t sent = 0;
  while (sent < payload.size()) {
    const ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, 0);
    if (n <= 0) {
      close(fd);
      return false;
    }
    sent += static_cast<size_t>(n);
  }

  responseLine.clear();
  char ch;
  while (responseLine.size() < 65536) {
    const ssize_t n = recv(fd, &ch, 1, 0);
    if (n <= 0)
      break;
    if (ch == '\n')
      break;
    responseLine += ch;
  }
  close(fd);
  return !responseLine.empty();
}

bool responseOk(const std::string &response) {
  return response.find("\"ok\": true") != std::string::npos ||
         response.find("\"ok\":true") != std::string::npos;
}

// Extracts a simple "key": "value" string field from a flat JSON object.
std::string jsonStringField(const std::string &json, const std::string &key) {
  const std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos)
    return "";
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos)
    return "";
  pos = json.find('"', pos + 1);
  if (pos == std::string::npos)
    return "";
  const size_t end = json.find('"', pos + 1);
  if (end == std::string::npos)
    return "";
  return json.substr(pos + 1, end - pos - 1);
}

bool spawnBridgeApp() {
  const fs::path app = fs::absolute(fs::path("toys_bridge") / "bridge_app.py");
  if (!fs::exists(app)) {
    std::cerr << "[ToysToLife] Bridge app not found: " << app << "\n";
    return false;
  }
  const pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    setsid();
    const int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
    }
    execlp("python3", "python3", app.c_str(), "--headless",
           static_cast<char *>(nullptr));
    _exit(127);
  }
  // Wait for the server to accept connections (up to ~3 s).
  for (int attempt = 0; attempt < 30; ++attempt) {
    std::string reply;
    if (tcpRequest("{\"cmd\":\"hello\"}", reply, 250) && responseOk(reply))
      return true;
    usleep(100000);
  }
  std::cerr << "[ToysToLife] Bridge app did not come up on port "
            << bridgePort() << ".\n";
  return false;
}

bool filenameMatches(const std::string &path,
                     const std::initializer_list<const char *> &keywords,
                     std::string &matched) {
  std::string name = fs::path(path).filename().string();
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::replace(name.begin(), name.end(), '_', ' ');
  for (const char *keyword : keywords) {
    if (name.find(keyword) != std::string::npos) {
      matched = keyword;
      return true;
    }
  }
  return false;
}

bool systemSupportsSkylanders(SystemType system) {
  switch (system) {
  case SystemType::Wii:
  case SystemType::PS3:
  case SystemType::WiiU:
  case SystemType::Xbox360:
  case SystemType::NintendoSwitch:
    return true;
  default:
    return false;
  }
}

} // namespace

ToysToLifeProfile detectToysToLifeProfile(SystemType system,
                                          const std::string &contentPath) {
  ToysToLifeProfile profile;
  if (systemSupportsSkylanders(system)) {
    profile.skylanders = filenameMatches(
        contentPath,
        {"skylander", "spyro's adventure", "spyros adventure", "swap force",
         "swap-force", "trap team", "superchargers", "super chargers",
         "imaginators"},
        profile.keyword);
    profile.infinity = filenameMatches(
        contentPath,
        {"disney infinity", "infinity 1.0", "infinity 2.0", "infinity 3.0"},
        profile.keyword);
  }
  profile.amiiboCapable = system == SystemType::WiiU ||
                          system == SystemType::NintendoSwitch ||
                          system == SystemType::ThreeDS;
  return profile;
}

bool ToysToLifeBridge::isRunning() {
  std::string reply;
  return tcpRequest("{\"cmd\":\"hello\"}", reply, 300) && responseOk(reply);
}

void ToysToLifeBridge::prepareForLaunch(SystemType system,
                                        const std::string &contentPath) {
  const ToysToLifeProfile profile =
      detectToysToLifeProfile(system, contentPath);
  if (!profile.any())
    return;

  // Portal/base titles need the app; start it on demand. amiibo-capable
  // titles only use an app the user already started, so launching an
  // ordinary Wii U/Switch/3DS game never spawns extra processes.
  if (!isRunning()) {
    if (!profile.requiresDevice())
      return;
    std::cout << "[ToysToLife] Starting the toys-to-life bridge app for "
              << fs::path(contentPath).filename().string() << "...\n";
    if (!spawnBridgeApp())
      return;
  }

  const std::string title = fs::path(contentPath).stem().string();
  const char *systemName = system == SystemType::Wii     ? "wii"
                           : system == SystemType::PS3  ? "ps3"
                           : system == SystemType::WiiU ? "wiiu"
                           : system == SystemType::NintendoSwitch ? "switch"
                           : system == SystemType::ThreeDS        ? "3ds"
                                                                  : "misc";

  std::string reply;
  const std::string announce = std::string("{\"cmd\":\"game\",\"system\":\"") +
                               systemName + "\",\"title\":\"" +
                               jsonEscape(title) + "\"}";
  if (!tcpRequest(announce, reply) || !responseOk(reply)) {
    std::cerr << "[ToysToLife] Bridge app did not accept the game announce.\n";
    return;
  }

  const std::string stage =
      std::string("{\"cmd\":\"stage\",\"system\":\"") + systemName + "\"}";
  if (tcpRequest(stage, reply) && responseOk(reply)) {
    const std::string dir = jsonStringField(reply, "dir");
    if (!dir.empty()) {
      setenv("OMNICON_TTL_FIGURES_DIR", dir.c_str(), 1);
      std::cout << "[ToysToLife] Active figures staged in " << dir << "\n";
    }
  }

  if (profile.skylanders)
    std::cout << "[ToysToLife] Skylanders title detected. In the emulator, "
                 "enable the emulated Portal of Power (RPCS3: I/O settings; "
                 "Dolphin standalone: emulated USB devices) and load figures "
                 "from the staged folder. Run toys_bridge/bridge_app.py to "
                 "place or swap figures.\n";
  if (profile.infinity)
    std::cout << "[ToysToLife] Disney Infinity title detected. Enable the "
                 "emulated Infinity Base in the emulator and load tokens "
                 "from the staged folder.\n";
}
