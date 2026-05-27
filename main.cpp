#include "top.hpp"
#include "NES.hpp"
#include "SNES.hpp"
#include "GameBoy.hpp"
#include "GameBoyColor.hpp"
#include "Atari2600.hpp"
#include "Atari5200.hpp"
#include "Atari800.hpp"
#include "Odyssey.hpp"
#include "SegaGenesis.hpp"
#include "N64.hpp"
#include "GBA.hpp"
#include "NDS.hpp"
#include "PS1.hpp"
#include "GameLibrary.hpp"
#include "GameShell.hpp"
#include "OracleDB.hpp"
#include "DownloadManager.hpp"

#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <regex>
#include <chrono>


//./controller_bridge/start_bridge.sh
namespace {

struct DatabaseConfig {
    std::string host;
    std::string port;
    std::string user;
    std::string password;
    std::string database;
};

bool isSupportedUrl(const std::string& url);

constexpr const char* kDatabaseHost = "147.224.62.87";
constexpr const char* kDatabasePort = "13306";
constexpr const char* kDatabaseUser = "block_app";
constexpr const char* kDatabaseName = "omnicon_blocks";

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

std::string readLine(const std::string& prompt)
{
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return trim(value);
}

bool hasCommand(const std::string& command)
{
    const std::string probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

// Run scrape_covers.py for any games currently missing cover art, then
// re-resolve cover paths so the shell picks up newly downloaded images.
// Forward-declared here because scrapeCoversIfNeeded needs it before
// the full definition below.
static std::string shellEscape(const std::string& value);

void scrapeCoversIfNeeded(GameLibrary& lib, const std::string& coversDir)
{
    int missing = 0;
    for (const auto& g : lib.games())
        if (g.coverPath.empty()) ++missing;

    if (missing == 0) return;

    if (!hasCommand("python3")) {
        std::cerr << "[covers] python3 not found; skipping auto-scrape.\n";
        return;
    }
    if (!std::filesystem::exists("scrape_covers.py")) {
        std::cerr << "[covers] scrape_covers.py not found; skipping auto-scrape.\n";
        return;
    }

    std::cout << "[covers] " << missing
              << " game(s) missing cover art — fetching now...\n";
    std::cout.flush();

    // Pass --db so DB-only entries (no local ROM) are also scraped.
    // Pass --covers-dir matching whatever the caller uses.
    const std::string cmd = "python3 scrape_covers.py --db --covers-dir "
                            + shellEscape(coversDir);
    const int ret = std::system(cmd.c_str());
    if (ret != 0)
        std::cerr << "[covers] scraper exited with code " << ret << "\n";

    // Re-resolve so newly downloaded files are reflected in the library.
    lib.resolveCoverPaths(coversDir);
}

static std::string shellEscape(const std::string& value)
{
    std::string escaped = "'";
    for (const char character : value) {
        if (character == '\'') {
            escaped += "'\"'\"'";
        } else {
            escaped += character;
        }
    }
    escaped += "'";
    return escaped;
}

std::string sqlEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (const char character : value) {
        if (character == '\\') {
            escaped += "\\\\";
        } else if (character == '\'') {
            escaped += "''";
        } else {
            escaped += character;
        }
    }

    return escaped;
}

std::string hexEncode(const std::vector<unsigned char>& bytes)
{
    static const char* digits = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(bytes.size() * 2);

    for (const unsigned char byte : bytes) {
        hex += digits[(byte >> 4) & 0x0F];
        hex += digits[byte & 0x0F];
    }

    return hex;
}

std::string fileNameFromUrl(const std::string& url)
{
    std::size_t end = url.find_first_of("?#");
    if (end == std::string::npos) {
        end = url.size();
    }

    const std::size_t slash = url.rfind('/', end);
    if (slash == std::string::npos || slash + 1 >= end) {
        return "download.bin";
    }

    return url.substr(slash + 1, end - slash - 1);
}

// Percent-decode a URL-encoded string (e.g. "10-Yard%20Fight" -> "10-Yard Fight").
std::string urlDecode(const std::string& s)
{
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit(static_cast<unsigned char>(s[i+1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i+2]))) {
            r += static_cast<char>(std::stoi(s.substr(i+1, 2), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            r += ' ';
        } else {
            r += s[i];
        }
    }
    return r;
}

int nextGameId()
{
    static int gameIdSeed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    return gameIdSeed++;
}

std::string stripOuterQuotes(const std::string& value)
{
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }

    return value;
}

std::string normalizePath(const std::string& path)
{
    std::string normalized = path;
    
    // Convert Windows backslashes to forward slashes
    for (char& c : normalized) {
        if (c == '\\') {
            c = '/';
        }
    }
    
    // Check if this is a Windows path (e.g., C:/, D:/, etc.)
    if (normalized.size() >= 2 && normalized[1] == ':') {
        // Convert Windows path to WSL path: C:/Users/... -> /mnt/c/users/...
        char drive = std::tolower(normalized[0]);
        std::string wslPath = "/mnt/" + std::string(1, drive) + normalized.substr(2);
        
        // Convert to lowercase for case-insensitive matching on Windows filesystems
        for (char& c : wslPath) {
            c = std::tolower(c);
        }
        
        return wslPath;
    }
    
    return normalized;
}

bool readLocalFileToBytes(const std::filesystem::path& filePath, std::vector<unsigned char>& bytes)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return false;
    }

    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

DatabaseConfig promptDatabaseConfig()
{
    DatabaseConfig config;
    config.host = kDatabaseHost;
    config.port = kDatabasePort;
    config.user = kDatabaseUser;
    config.database = kDatabaseName;

    const char* envPassword = std::getenv("MYSQL_APP_PASSWORD");
    if (envPassword != nullptr && envPassword[0] != '\0') {
        config.password = envPassword;
    } else {
        config.password = readLine("Database password: ");
    }

    return config;
}

bool downloadToBytes(const std::string& url, std::vector<unsigned char>& bytes)
{
    const std::string command =
        "curl --location --fail --silent --show-error " + shellEscape(url);

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }

    bytes.clear();
    unsigned char buffer[16384];

    while (true) {
        const std::size_t readCount = fread(buffer, 1, sizeof(buffer), pipe);
        if (readCount > 0) {
            bytes.insert(bytes.end(), buffer, buffer + readCount);
        }

        if (readCount < sizeof(buffer)) {
            if (feof(pipe)) {
                break;
            }

            if (ferror(pipe)) {
                pclose(pipe);
                return false;
            }
        }
    }

    return pclose(pipe) == 0;
}

bool downloadToString(const std::string& url, std::string& text)
{
    std::vector<unsigned char> bytes;
    if (!downloadToBytes(url, bytes)) {
        return false;
    }

    text.assign(bytes.begin(), bytes.end());
    return true;
}

std::string urlOrigin(const std::string& url)
{
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return "";
    }

    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t hostEnd = url.find('/', hostStart);
    if (hostEnd == std::string::npos) {
        return url;
    }

    return url.substr(0, hostEnd);
}

std::string urlDirectory(const std::string& url)
{
    const std::size_t queryStart = url.find_first_of("?#");
    const std::string cleanUrl = queryStart == std::string::npos ? url : url.substr(0, queryStart);

    const std::size_t slash = cleanUrl.rfind('/');
    const std::size_t schemeEnd = cleanUrl.find("://");
    if (schemeEnd != std::string::npos && slash <= schemeEnd + 2) {
        return cleanUrl + "/";
    }

    if (slash == std::string::npos) {
        return cleanUrl + "/";
    }

    return cleanUrl.substr(0, slash + 1);
}

std::string schemeForUrl(const std::string& url)
{
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return "https";
    }

    return url.substr(0, schemeEnd);
}

std::string resolveUrl(const std::string& pageUrl, const std::string& href)
{
    if (href.empty()) {
        return "";
    }

    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        return href;
    }

    if (href.rfind("//", 0) == 0) {
        return schemeForUrl(pageUrl) + ":" + href;
    }

    if (href[0] == '/') {
        return urlOrigin(pageUrl) + href;
    }

    return urlDirectory(pageUrl) + href;
}

bool shouldSkipHref(const std::string& href)
{
    if (href.empty() || href[0] == '#') {
        return true;
    }

    return href.rfind("mailto:", 0) == 0 || href.rfind("javascript:", 0) == 0;
}

std::string toLowerCopy(std::string value)
{
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }

    return value;
}

std::string fileExtensionFromUrl(const std::string& url)
{
    if (url.empty()) {
        return "";
    }

    const std::size_t queryStart = url.find_first_of("?#");
    const std::string cleanUrl = queryStart == std::string::npos ? url : url.substr(0, queryStart);

    if (cleanUrl.empty() || cleanUrl.back() == '/') {
        return "";
    }

    const std::string name = fileNameFromUrl(cleanUrl);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) {
        return "";
    }

    return toLowerCopy(name.substr(dot + 1));
}

bool looksLikeDownloadableFileLink(const std::string& url)
{
    static const std::set<std::string> allowedExtensions = {
        "zip", "7z", "rar", "tar", "gz", "bz2", "xz",
        "iso", "bin", "cue", "rom", "nes", "sfc", "smc",
        "n64", "z64", "v64", "gba", "gb", "gbc", "nds",
        "chd", "wad", "cia", "rvz", "wbfs"
    };

    static const std::set<std::string> blockedExtensions = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp", "svg", "ico",
        "html", "htm", "php", "asp", "aspx", "jsp"
    };

    const std::string extension = fileExtensionFromUrl(url);
    if (extension.empty()) {
        return false;
    }

    if (blockedExtensions.find(extension) != blockedExtensions.end()) {
        return false;
    }

    return allowedExtensions.find(extension) != allowedExtensions.end();
}

std::vector<std::string> extractPageFileLinks(const std::string& pageUrl, const std::string& html)
{
    std::vector<std::string> links;
    std::set<std::string> unique;

    // If the page is an archive.org download/details URL, build the canonical
    // base so relative hrefs resolve to download/IDENTIFIER/FILENAME rather
    // than losing the identifier (e.g. archive.org/download/roms_nes/game.zip).
    std::string archiveBase;
    {
        static const std::regex arcRe(
            R"(https?://archive\.org/(?:download|details)/([^/?#]+))");
        std::smatch arcM;
        if (std::regex_search(pageUrl, arcM, arcRe))
            archiveBase = "https://archive.org/download/" + arcM[1].str() + "/";
    }

    // Matches a malformed archive.org URL that is missing the collection
    // identifier: https://archive.org/download/FILENAME.ext  (no sub-path).
    static const std::regex malformedArcRe(
        R"(https?://archive\.org/download/[^/?#]+\.[a-zA-Z0-9]+$)");

    static const std::regex hrefRegex(R"(href\s*=\s*[\"']([^\"']+)[\"'])", std::regex::icase);
    std::sregex_iterator iter(html.begin(), html.end(), hrefRegex);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
        const std::string href = trim((*iter)[1].str());
        if (shouldSkipHref(href)) {
            continue;
        }

        std::string resolved = resolveUrl(pageUrl, href);

        // Fix identifier-stripped URL caused by urlDirectory() treating the
        // collection name as a file when pageUrl has no trailing slash.
        if (!archiveBase.empty() && std::regex_match(resolved, malformedArcRe))
            resolved = archiveBase + fileNameFromUrl(resolved);

        if (!isSupportedUrl(resolved) || !looksLikeDownloadableFileLink(resolved)) {
            continue;
        }

        if (unique.insert(resolved).second) {
            links.push_back(resolved);
        }
    }

    return links;
}

bool insertBlock(const DatabaseConfig& config, int gameId, const std::string& fileName, const std::vector<unsigned char>& gameData)
{
    const std::string gameHex = hexEncode(gameData);
    const std::string sql =
        "INSERT IGNORE INTO blocks (game_id, name, game, save_data, image) VALUES ("
        + std::to_string(gameId) + ", '" + sqlEscape(fileName) + "', X'" + gameHex + "', X'', X'');\n";

    std::string command;
    command += "mysql --protocol=TCP --host=" + shellEscape(config.host)
        + " --port=" + shellEscape(config.port)
        + " --user=" + shellEscape(config.user)
        + " --password=" + shellEscape(config.password)
        + " --database=" + shellEscape(config.database)
        + " 2>/dev/null";

    FILE* mysqlPipe = popen(command.c_str(), "w");
    if (mysqlPipe == nullptr) {
        return false;
    }

    const std::size_t writeCount = fwrite(sql.data(), 1, sql.size(), mysqlPipe);
    if (writeCount != sql.size()) {
        pclose(mysqlPipe);
        return false;
    }

    return pclose(mysqlPipe) == 0;
}

// ─── Unified input configuration ─────────────────────────────────────────────

// Helper: parse SDL controller button name (same as per-system helpers)
static SDL_GameControllerButton parseButtonName(const std::string& raw)
{
    std::string name;
    name.reserve(raw.size());
    for (unsigned char c : raw) name += static_cast<char>(toupper(c));
    // strip whitespace
    name.erase(0, name.find_first_not_of(" \t\r\n"));
    auto last = name.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) name.erase(last + 1);

    if (name == "A")            return SDL_CONTROLLER_BUTTON_A;
    if (name == "B")            return SDL_CONTROLLER_BUTTON_B;
    if (name == "X")            return SDL_CONTROLLER_BUTTON_X;
    if (name == "Y")            return SDL_CONTROLLER_BUTTON_Y;
    if (name == "BACK")         return SDL_CONTROLLER_BUTTON_BACK;
    if (name == "GUIDE")        return SDL_CONTROLLER_BUTTON_GUIDE;
    if (name == "START")        return SDL_CONTROLLER_BUTTON_START;
    if (name == "LEFTSTICK")    return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    if (name == "RIGHTSTICK")   return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    if (name == "LEFTSHOULDER") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (name == "RIGHTSHOULDER")return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (name == "DPAD_UP")      return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (name == "DPAD_DOWN")    return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (name == "DPAD_LEFT")    return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (name == "DPAD_RIGHT")   return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

static const char* buttonDisplayName(SDL_GameControllerButton b)
{
    const char* n = SDL_GameControllerGetStringForButton(b);
    return n ? n : "unset";
}

// Prompt for a keyboard key; returns SDLK_UNKNOWN if user presses Enter (keep).
static SDL_Keycode promptKey(const char* label, SDL_Keycode current)
{
    std::cout << "  " << label << " [" << SDL_GetKeyName(current) << "]: ";
    std::string raw;
    std::getline(std::cin, raw);
    raw = trim(raw);
    if (raw.empty()) return SDLK_UNKNOWN;
    SDL_Keycode k = SDL_GetKeyFromName(raw.c_str());
    if (k == SDLK_UNKNOWN) { std::cout << "    Invalid key name — keeping current.\n"; return SDLK_UNKNOWN; }
    return k;
}

// Prompt for a controller button; returns SDL_CONTROLLER_BUTTON_INVALID on Enter.
static SDL_GameControllerButton promptButton(const char* label, SDL_GameControllerButton current)
{
    std::cout << "  " << label << " [" << buttonDisplayName(current) << "]: ";
    std::string raw;
    std::getline(std::cin, raw);
    raw = trim(raw);
    if (raw.empty()) return SDL_CONTROLLER_BUTTON_INVALID;
    SDL_GameControllerButton b = parseButtonName(raw);
    if (b == SDL_CONTROLLER_BUTTON_INVALID) { std::cout << "    Invalid button name — keeping current.\n"; }
    return b;
}

// Apply a key to a pointer, only if k != SDLK_UNKNOWN.
static void applyKey(SDL_Keycode k, SDL_Keycode& target) { if (k != SDLK_UNKNOWN) target = k; }
static void applyBtn(SDL_GameControllerButton b, SDL_GameControllerButton& target)
{
    if (b != SDL_CONTROLLER_BUTTON_INVALID) target = b;
}

void configureAllControls()
{
    // Ensure all systems have defaults in their global statics.
    // Constructors call setDefaultBindings() + saveGlobalInputSettings() when first created.
    {
        NESEmulation          nesInit;
        GameBoyEmulation      gbInit;
        SNESEmulation         snesInit;
        GBAEmulation          gbaInit;
        NDSEmulation          ndsInit;
        Atari2600Emulation    a2600Init;
        Atari5200Emulation    a5200Init;
        Atari800Emulation     a800Init;
        SegaGenesisEmulation  genesisInit;
        PS1Emulation          ps1Init;
        (void)nesInit; (void)gbInit; (void)snesInit; (void)gbaInit;
        (void)ndsInit; (void)a2600Init; (void)a5200Init; (void)a800Init; (void)genesisInit; (void)ps1Init;
    }

    const bool hadController = (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0);
    if (!hadController) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

    SDL_GameController* ctrl = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) { ctrl = SDL_GameControllerOpen(i); break; }
    }

    // Shadow copies we edit, then commit on exit.
    // D-pad + core face buttons — shared across all systems.
    SDL_Keycode kUp    = NESEmulation::globalKeyBindings.up;
    SDL_Keycode kDown  = NESEmulation::globalKeyBindings.down;
    SDL_Keycode kLeft  = NESEmulation::globalKeyBindings.left;
    SDL_Keycode kRight = NESEmulation::globalKeyBindings.right;
    SDL_Keycode kA     = NESEmulation::globalKeyBindings.a;
    SDL_Keycode kB     = NESEmulation::globalKeyBindings.b;
    SDL_Keycode kSel   = NESEmulation::globalKeyBindings.select;
    SDL_Keycode kStart = NESEmulation::globalKeyBindings.start;
    // Extended face (SNES / NDS)
    SDL_Keycode kX     = SNESEmulation::globalKeyBindings.x;
    SDL_Keycode kY     = SNESEmulation::globalKeyBindings.y;
    // Shoulder (SNES / GBA / NDS)
    SDL_Keycode kL     = SNESEmulation::globalKeyBindings.l;
    SDL_Keycode kR     = SNESEmulation::globalKeyBindings.r;
    // Atari-specific
    SDL_Keycode kFire  = Atari2600Emulation::globalKeyBindings.fire;
    SDL_Keycode kSec   = Atari5200Emulation::globalKeyBindings.secondary;
    SDL_Keycode kReset = Atari2600Emulation::globalKeyBindings.reset;
    SDL_Keycode kOpt   = Atari800Emulation::globalKeyBindings.option;

    SDL_GameControllerButton bUp    = NESEmulation::globalControllerBindings.up;
    SDL_GameControllerButton bDown  = NESEmulation::globalControllerBindings.down;
    SDL_GameControllerButton bLeft  = NESEmulation::globalControllerBindings.left;
    SDL_GameControllerButton bRight = NESEmulation::globalControllerBindings.right;
    SDL_GameControllerButton bA     = NESEmulation::globalControllerBindings.a;
    SDL_GameControllerButton bB     = NESEmulation::globalControllerBindings.b;
    SDL_GameControllerButton bSel   = NESEmulation::globalControllerBindings.select;
    SDL_GameControllerButton bStart = NESEmulation::globalControllerBindings.start;
    SDL_GameControllerButton bX     = SNESEmulation::globalControllerBindings.x;
    SDL_GameControllerButton bY     = SNESEmulation::globalControllerBindings.y;
    SDL_GameControllerButton bL     = SNESEmulation::globalControllerBindings.l;
    SDL_GameControllerButton bR     = SNESEmulation::globalControllerBindings.r;
    SDL_GameControllerButton bFire  = Atari2600Emulation::globalControllerBindings.fire;
    SDL_GameControllerButton bSec   = Atari5200Emulation::globalControllerBindings.secondary;
    SDL_GameControllerButton bReset = Atari2600Emulation::globalControllerBindings.reset;
    SDL_GameControllerButton bOpt   = Atari800Emulation::globalControllerBindings.option;

    // Use NES input mode as the shared mode (all systems share one mode).
    NESEmulation::InputMode mode = NESEmulation::globalInputMode;

    auto modeName = [](NESEmulation::InputMode m) -> const char* {
        switch (m) {
            case NESEmulation::InputMode::KeyboardOnly:          return "Keyboard only";
            case NESEmulation::InputMode::ControllerOnly:        return "Controller only";
            case NESEmulation::InputMode::KeyboardAndController: return "Keyboard + Controller";
        }
        return "Unknown";
    };

    while (true) {
        std::cout << "\n=== OmniCon Controller Settings ===\n";
        std::cout << "  Current mode: " << modeName(mode) << "\n\n";
        std::cout << "  Input Mode:\n";
        std::cout << "   1) Keyboard only\n";
        std::cout << "   2) Controller only\n";
        std::cout << "   3) Keyboard + Controller\n\n";
        std::cout << "  Remap Keyboard Bindings:\n";
        std::cout << "   4) D-Pad            Up=" << SDL_GetKeyName(kUp) << "  Down=" << SDL_GetKeyName(kDown) << "  Left=" << SDL_GetKeyName(kLeft) << "  Right=" << SDL_GetKeyName(kRight) << "\n";
        std::cout << "   5) A / B            A=" << SDL_GetKeyName(kA) << "  B=" << SDL_GetKeyName(kB) << "  (also: Atari Fire=" << SDL_GetKeyName(kFire) << ")\n";
        std::cout << "   6) X / Y            X=" << SDL_GetKeyName(kX) << "  Y=" << SDL_GetKeyName(kY) << "  (SNES, NDS, PS1 Square/Triangle)\n";
        std::cout << "   7) L / R Shoulders  L=" << SDL_GetKeyName(kL) << "  R=" << SDL_GetKeyName(kR) << "  (SNES, GBA, NDS, PS1 L1/R1)\n";
        std::cout << "   8) Start / Select   Start=" << SDL_GetKeyName(kStart) << "  Select=" << SDL_GetKeyName(kSel) << "\n";
        std::cout << "   9) Atari-specific   Secondary=" << SDL_GetKeyName(kSec) << "  Reset=" << SDL_GetKeyName(kReset) << "  Option=" << SDL_GetKeyName(kOpt) << "\n\n";
        std::cout << "  Remap Controller Bindings:\n";
        std::cout << "  10) D-Pad            Up=" << buttonDisplayName(bUp) << "  Down=" << buttonDisplayName(bDown) << "  Left=" << buttonDisplayName(bLeft) << "  Right=" << buttonDisplayName(bRight) << "\n";
        std::cout << "  11) A / B            A=" << buttonDisplayName(bA) << "  B=" << buttonDisplayName(bB) << "  (also: Atari Fire=" << buttonDisplayName(bFire) << ")\n";
        std::cout << "  12) X / Y            X=" << buttonDisplayName(bX) << "  Y=" << buttonDisplayName(bY) << "  (SNES, NDS, PS1 Square/Triangle)\n";
        std::cout << "  13) L / R Shoulders  L=" << buttonDisplayName(bL) << "  R=" << buttonDisplayName(bR) << "  (SNES, GBA, NDS, PS1 L1/R1)\n";
        std::cout << "  14) Start / Select   Start=" << buttonDisplayName(bStart) << "  Select=" << buttonDisplayName(bSel) << "\n";
        std::cout << "  15) Atari-specific   Secondary=" << buttonDisplayName(bSec) << "  Reset=" << buttonDisplayName(bReset) << "  Option=" << buttonDisplayName(bOpt) << "\n\n";
        std::cout << "  16) Select controller\n";
        std::cout << "   0) Save and exit\n";
        std::cout << "Choice: ";

        std::string ch;
        std::getline(std::cin, ch);
        ch = trim(ch);

        if (ch == "0") break;
        if (ch == "1") { mode = NESEmulation::InputMode::KeyboardOnly;          std::cout << "Keyboard only.\n"; continue; }
        if (ch == "2") { mode = NESEmulation::InputMode::ControllerOnly;         std::cout << "Controller only.\n"; continue; }
        if (ch == "3") { mode = NESEmulation::InputMode::KeyboardAndController;  std::cout << "Keyboard + Controller.\n"; continue; }

        if (ch == "4") {
            std::cout << "--- D-Pad keyboard (all systems) ---\n";
            std::cout << "Enter SDL key name, or press Enter to keep current.\n";
            applyKey(promptKey("UP",    kUp),    kUp);
            applyKey(promptKey("DOWN",  kDown),  kDown);
            applyKey(promptKey("LEFT",  kLeft),  kLeft);
            applyKey(promptKey("RIGHT", kRight), kRight);
            continue;
        }
        if (ch == "5") {
            std::cout << "--- A / B keyboard (NES, GB, SNES, GBA, NDS) + Atari Fire ---\n";
            std::cout << "Enter SDL key name, or press Enter to keep current.\n";
            applyKey(promptKey("A    (NES/GB/SNES/GBA/NDS)", kA), kA);
            applyKey(promptKey("B    (NES/GB/SNES/GBA/NDS)", kB), kB);
            applyKey(promptKey("FIRE (Atari 2600/5200/800)", kFire), kFire);
            continue;
        }
        if (ch == "6") {
            std::cout << "--- X / Y keyboard (SNES, NDS) ---\n";
            applyKey(promptKey("X", kX), kX);
            applyKey(promptKey("Y", kY), kY);
            continue;
        }
        if (ch == "7") {
            std::cout << "--- L / R shoulder keyboard (SNES, GBA, NDS) ---\n";
            applyKey(promptKey("L", kL), kL);
            applyKey(promptKey("R", kR), kR);
            continue;
        }
        if (ch == "8") {
            std::cout << "--- Start / Select keyboard (all systems) ---\n";
            applyKey(promptKey("START",  kStart), kStart);
            applyKey(promptKey("SELECT", kSel),   kSel);
            continue;
        }
        if (ch == "9") {
            std::cout << "--- Atari-specific keyboard ---\n";
            applyKey(promptKey("SECONDARY (Atari 5200/800)", kSec),   kSec);
            applyKey(promptKey("RESET     (Atari 2600/5200)", kReset), kReset);
            applyKey(promptKey("OPTION    (Atari 800)",       kOpt),   kOpt);
            continue;
        }
        if (ch == "10") {
            std::cout << "--- D-Pad controller (all systems) ---\n";
            std::cout << "Valid names: A B X Y BACK START LEFTSHOULDER RIGHTSHOULDER DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT\n";
            applyBtn(promptButton("UP",    bUp),    bUp);
            applyBtn(promptButton("DOWN",  bDown),  bDown);
            applyBtn(promptButton("LEFT",  bLeft),  bLeft);
            applyBtn(promptButton("RIGHT", bRight), bRight);
            continue;
        }
        if (ch == "11") {
            std::cout << "--- A / B controller + Atari Fire ---\n";
            applyBtn(promptButton("A    (NES/GB/SNES/GBA/NDS)", bA), bA);
            applyBtn(promptButton("B    (NES/GB/SNES/GBA/NDS)", bB), bB);
            applyBtn(promptButton("FIRE (Atari 2600/5200/800)", bFire), bFire);
            continue;
        }
        if (ch == "12") {
            std::cout << "--- X / Y controller (SNES, NDS) ---\n";
            applyBtn(promptButton("X", bX), bX);
            applyBtn(promptButton("Y", bY), bY);
            continue;
        }
        if (ch == "13") {
            std::cout << "--- L / R shoulder controller (SNES, GBA, NDS) ---\n";
            applyBtn(promptButton("L", bL), bL);
            applyBtn(promptButton("R", bR), bR);
            continue;
        }
        if (ch == "14") {
            std::cout << "--- Start / Select controller (all systems) ---\n";
            applyBtn(promptButton("START",  bStart), bStart);
            applyBtn(promptButton("SELECT", bSel),   bSel);
            continue;
        }
        if (ch == "15") {
            std::cout << "--- Atari-specific controller ---\n";
            applyBtn(promptButton("SECONDARY (Atari 5200/800)", bSec),   bSec);
            applyBtn(promptButton("RESET     (Atari 2600/5200)", bReset), bReset);
            applyBtn(promptButton("OPTION    (Atari 800)",        bOpt),  bOpt);
            continue;
        }
        if (ch == "16") {
            int count = SDL_NumJoysticks();
            if (count == 0) { std::cout << "No controllers detected.\n"; continue; }
            std::cout << "Controllers:\n";
            for (int i = 0; i < count; ++i)
                if (SDL_IsGameController(i))
                    std::cout << "  [" << i << "] " << SDL_GameControllerNameForIndex(i) << "\n";
            std::cout << "Enter index: ";
            std::string idx; std::getline(std::cin, idx); idx = trim(idx);
            if (!idx.empty()) {
                try {
                    int i = std::stoi(idx);
                    if (ctrl) SDL_GameControllerClose(ctrl);
                    ctrl = nullptr;
                    if (i >= 0 && i < count && SDL_IsGameController(i))
                        ctrl = SDL_GameControllerOpen(i);
                } catch (...) { std::cout << "Invalid index.\n"; }
            }
            continue;
        }
        std::cout << "Unknown option.\n";
    }

    // ── Commit to all systems ──────────────────────────────────────────────
    // Map our shared InputMode enum value to each system's equivalent.
    // (All InputMode enums have the same underlying values 0/1/2.)
    auto toNesMode   = [&]() { return static_cast<NESEmulation::InputMode>(static_cast<int>(mode)); };
    auto toGbMode    = [&]() { return static_cast<GameBoyEmulation::InputMode>(static_cast<int>(mode)); };
    auto toSnesMode  = [&]() { return static_cast<SNESEmulation::InputMode>(static_cast<int>(mode)); };
    auto toGbaMode   = [&]() { return static_cast<GBAEmulation::InputMode>(static_cast<int>(mode)); };
    auto toNdsMode   = [&]() { return static_cast<NDSEmulation::InputMode>(static_cast<int>(mode)); };
    auto toA2600Mode = [&]() { return static_cast<Atari2600Emulation::InputMode>(static_cast<int>(mode)); };
    auto toA5200Mode = [&]() { return static_cast<Atari5200Emulation::InputMode>(static_cast<int>(mode)); };
    auto toA800Mode  = [&]() { return static_cast<Atari800Emulation::InputMode>(static_cast<int>(mode)); };
    auto toPS1Mode   = [&]() { return static_cast<PS1Emulation::InputMode>(static_cast<int>(mode)); };

    // NES
    NESEmulation::globalInputMode = toNesMode();
    NESEmulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kA, kB, kSel, kStart};
    NESEmulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bA, bB, bSel, bStart};
    NESEmulation::globalInputConfigured    = true;

    // Game Boy (and Game Boy Color, which shares GameBoyEmulation statics)
    GameBoyEmulation::globalInputMode = toGbMode();
    GameBoyEmulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kA, kB, kSel, kStart};
    GameBoyEmulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bA, bB, bSel, bStart};
    GameBoyEmulation::globalInputConfigured    = true;

    // SNES
    SNESEmulation::globalInputMode = toSnesMode();
    SNESEmulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kA, kB, kX, kY, kL, kR, kSel, kStart};
    SNESEmulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bA, bB, bX, bY, bL, bR, bSel, bStart};
    SNESEmulation::globalInputConfigured    = true;

    // GBA (note: GBA KeyBindings struct is in order: a,b,select,start,right,left,up,down,r,l)
    GBAEmulation::globalInputMode = toGbaMode();
    GBAEmulation::globalKeyBindings        = {kA, kB, kSel, kStart, kRight, kLeft, kUp, kDown, kR, kL};
    GBAEmulation::globalControllerBindings = {bA, bB, bSel, bStart, bRight, bLeft, bUp, bDown, bR, bL};
    GBAEmulation::globalInputConfigured    = true;

    // NDS
    NDSEmulation::globalInputMode = toNdsMode();
    NDSEmulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kA, kB, kX, kY, kStart, kSel, kL, kR};
    NDSEmulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bA, bB, bX, bY, bStart, bSel, bL, bR};
    NDSEmulation::globalInputConfigured    = true;

    // Atari 2600
    Atari2600Emulation::globalInputMode = toA2600Mode();
    Atari2600Emulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kFire, kSel, kStart, kReset};
    Atari2600Emulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bFire, bSel, bStart, bReset};
    Atari2600Emulation::globalInputConfigured    = true;

    // Atari 5200
    Atari5200Emulation::globalInputMode = toA5200Mode();
    Atari5200Emulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kFire, kSec, kSel, kStart, kReset};
    Atari5200Emulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bFire, bSec, bSel, bStart, bReset};
    Atari5200Emulation::globalInputConfigured    = true;

    // Atari 800
    Atari800Emulation::globalInputMode = toA800Mode();
    Atari800Emulation::globalKeyBindings        = {kUp, kDown, kLeft, kRight, kFire, kSec, kSel, kStart, kOpt};
    Atari800Emulation::globalControllerBindings = {bUp, bDown, bLeft, bRight, bFire, bSec, bSel, bStart, bOpt};
    Atari800Emulation::globalInputConfigured    = true;

    // Magnavox Odyssey (P1 uses primary up/down; P2 uses arrow keys / right stick)
    OdysseyEmulation::globalInputMode         = OdysseyEmulation::InputMode::KeyboardAndController;
    OdysseyEmulation::globalKeyBindings       = {kUp, kDown, SDLK_UP, SDLK_DOWN, SDLK_SPACE};
    OdysseyEmulation::globalControllerBindings = {
        bUp, bDown,
        SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_START
    };
    OdysseyEmulation::globalInputConfigured   = true;

    // PlayStation
    // Cross=B, Circle=A, Square=X, Triangle=Y; L1=L, R1=R; L2/R2 keep defaults
    PS1Emulation::globalInputMode = toPS1Mode();
    PS1Emulation::globalKeyBindings = {
        kUp, kDown, kLeft, kRight,
        kB,  kA,    kX,    kY,     // cross, circle, square, triangle
        kStart, kSel,
        kL, kR,                    // l1, r1
        PS1Emulation::globalKeyBindings.l2, PS1Emulation::globalKeyBindings.r2  // keep defaults
    };
    PS1Emulation::globalControllerBindings = {
        bUp, bDown, bLeft, bRight,
        bB,  bA,    bX,    bY,     // cross, circle, square, triangle
        bStart, bSel,
        bL, bR                     // l1, r1
    };
    PS1Emulation::globalInputConfigured = true;

    if (ctrl) SDL_GameControllerClose(ctrl);
    if (!hadController) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);

    std::cout << "Controls saved for all systems.\n";
}

void printHelp()
{
    std::cout << "Commands:\n"
              << "  load url <url>  Fetch a page, parse ROM links, and add them to the Oracle DB\n"
              << "  download <url>  Download one file and store it as a block\n"
              << "  download-all <url>  Download file links from a page and store each as a block\n"
              << "  load-folder <path>  Upload every regular file in a local folder\n"
              << "  play <path>     Load and run a ROM by extension\n"
              << "                  .nes -> NES, .smc/.sfc -> SNES, .gb -> Game Boy\n"
              << "                  .gbc -> Game Boy Color, .a26 -> Atari 2600\n"
              << "                  .a52/.bin/.rom -> Atari 5200/2600\n"
              << "                  .md/.gen/.smd -> Sega Genesis\n"
              << "                  (Supports Windows paths: C:\\Users\\...\\file.gbc)\n"
              << "  play-gb <path>  Load and run a Game Boy ROM (.gb/.gbc/.sgb)\n"
              << "  play-gbc <path> Load and run a Game Boy Color ROM (.gbc/.gb/.sgb)\n"
              << "  play-snes <path>  Load and run a .smc/.sfc ROM file\n"
              << "  play-atari2600 <path>  Load and run an Atari 2600 ROM (.a26/.bin/.rom)\n"
              << "  play-atari5200 <path>  Load and run an Atari 5200 ROM (.a52/.bin/.rom)\n"
              << "  play-gba <path>  Load and run a GBA ROM (.gba)\n"
              << "  play-nds <path>  Load and run a Nintendo DS ROM (.nds)\n"
              << "  play-ps1 <path>  Load and run a PlayStation ROM (.cue/.bin/.iso/.pbp/.chd)\n"
              << "  play-genesis <path>  Load and run a Sega Genesis ROM (.md/.gen/.smd)\n"
              << "  play-n64 <path>  Load and run a Nintendo 64 ROM (.n64/.z64/.v64)\n"
              << "  shell           Open the graphical game library launcher\n"
              << "  scrape-covers   Download missing box art for all games in the library\n"
              << "  controls        Configure controls for all systems (keyboard + controller)\n"
              << "  controls-csv    Parse Controls.csv and set controls for all systems\n"
              << "  config          Re-enter database password\n"
              << "  help            Show this message\n"
              << "  quit            Exit the REPL\n";
}
// Parse Controls.csv and set controls for all systems

void setControlsFromCSV() {
    const std::string csvPath = "Controls.csv";
    std::ifstream file(csvPath);
    if (!file) {
        std::cerr << "Could not open " << csvPath << "\n";
        return;
    }

    std::string header, values;
    if (!std::getline(file, header) || !std::getline(file, values)) {
        std::cerr << "Controls.csv is missing header or values row\n";
        return;
    }

    // Remove quotes and split header and values
    auto split = [](const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        bool inQuotes = false;
        for (char c : line) {
            if (c == '"') { inQuotes = !inQuotes; continue; }
            if (c == ',' && !inQuotes) {
                out.push_back(trim(cur));
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) out.push_back(trim(cur));
        return out;
    };

    std::vector<std::string> keys = split(header);
    std::vector<std::string> vals = split(values);
    if (keys.size() != vals.size()) {
        std::cerr << "Controls.csv: header and value count mismatch\n";
        return;
    }

    // Map from key to value
    std::map<std::string, std::string> mapping;
    for (size_t i = 0; i < keys.size(); ++i) {
        mapping[trim(keys[i])] = trim(vals[i]);
    }

    // Helper to get keycode from string
    auto keycodeFromString = [](const std::string& s) -> SDL_Keycode {
        SDL_Keycode k = SDL_GetKeyFromName(s.c_str());
        if (k == SDLK_UNKNOWN && s == "Right Shift") return SDLK_RSHIFT;
        if (k == SDLK_UNKNOWN && s == "Enter") return SDLK_RETURN;
        if (k == SDLK_UNKNOWN && s == "Tab") return SDLK_TAB;
        if (k == SDLK_UNKNOWN && s == "Space") return SDLK_SPACE;
        return k;
    };

    // NES/GB core
    NESEmulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["a"]),
        keycodeFromString(mapping["b"]),
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["start"])
    };
    NESEmulation::globalInputConfigured = true;
    GameBoyEmulation::globalKeyBindings.up = keycodeFromString(mapping["Up"]);
    GameBoyEmulation::globalKeyBindings.down = keycodeFromString(mapping["Down"]);
    GameBoyEmulation::globalKeyBindings.left = keycodeFromString(mapping["Left"]);
    GameBoyEmulation::globalKeyBindings.right = keycodeFromString(mapping["Right"]);
    GameBoyEmulation::globalKeyBindings.a = keycodeFromString(mapping["a"]);
    GameBoyEmulation::globalKeyBindings.b = keycodeFromString(mapping["b"]);
    GameBoyEmulation::globalKeyBindings.select = keycodeFromString(mapping["select"]);
    GameBoyEmulation::globalKeyBindings.start = keycodeFromString(mapping["start"]);
    GameBoyEmulation::globalInputConfigured = true;

    // SNES
    SNESEmulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["a"]),
        keycodeFromString(mapping["b"]),
        keycodeFromString(mapping["x"]),
        keycodeFromString(mapping["y"]),
        keycodeFromString(mapping["l1"]),
        keycodeFromString(mapping["r1"]),
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["start"])
    };
    SNESEmulation::globalInputConfigured = true;

    // GBA
    GBAEmulation::globalKeyBindings = {
        keycodeFromString(mapping["a"]),
        keycodeFromString(mapping["b"]),
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["r1"]),
        keycodeFromString(mapping["l1"])
    };
    GBAEmulation::globalInputConfigured = true;

    // NDS
    NDSEmulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["a"]),
        keycodeFromString(mapping["b"]),
        keycodeFromString(mapping["x"]),
        keycodeFromString(mapping["y"]),
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["l1"]),
        keycodeFromString(mapping["r1"])
    };
    NDSEmulation::globalInputConfigured = true;

    // Atari 2600
    Atari2600Emulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["Fire"]),
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["l2"])
    };
    Atari2600Emulation::globalInputConfigured = true;

    // Atari 5200
    Atari5200Emulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["Fire"]),
        keycodeFromString(mapping["r2"]), // secondary
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["l2"])
    };
    Atari5200Emulation::globalInputConfigured = true;

    // Atari 800
    Atari800Emulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["Fire"]),
        keycodeFromString(mapping["r2"]), // secondary
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["home"])
    };
    Atari800Emulation::globalInputConfigured = true;

    // Sega Genesis (partial, only main 3 buttons and dpad)
    SegaGenesisEmulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["a"]),
        keycodeFromString(mapping["b"]),
        keycodeFromString(mapping["c"]),
        keycodeFromString(mapping["x"]),
        keycodeFromString(mapping["y"]),
        keycodeFromString(mapping["z"]),
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["mode"])
    };
    SegaGenesisEmulation::globalInputConfigured = true;

    // N64 (partial, only dpad and a/b)
    N64Emulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["a"]),
        keycodeFromString(mapping["b"]),
        SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, // c_up, c_down, c_left, c_right
        SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, // l, r, z
        keycodeFromString(mapping["start"])
    };
    N64Emulation::globalInputConfigured = true;

    // PS1 (only 14 fields)
    PS1Emulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["Left"]),
        keycodeFromString(mapping["Right"]),
        keycodeFromString(mapping["b"]), // cross
        keycodeFromString(mapping["a"]), // circle
        keycodeFromString(mapping["x"]), // square
        keycodeFromString(mapping["y"]), // triangle
        keycodeFromString(mapping["start"]),
        keycodeFromString(mapping["select"]),
        keycodeFromString(mapping["l1"]),
        keycodeFromString(mapping["r1"]),
        keycodeFromString(mapping["l2"]),
        keycodeFromString(mapping["r2"])
    };
    PS1Emulation::globalInputConfigured = true;

    // Odyssey (P1 up/down, P2 up/down, serve)
    OdysseyEmulation::globalKeyBindings = {
        keycodeFromString(mapping["Up"]),
        keycodeFromString(mapping["Down"]),
        keycodeFromString(mapping["l1"]), // P2 up
        keycodeFromString(mapping["r1"]), // P2 down
        keycodeFromString(mapping["Fire"])
    };
    OdysseyEmulation::globalInputConfigured = true;

    std::cout << "Controls set from " << csvPath << " for all systems.\n";
}

bool isSupportedUrl(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

} // namespace

int main()
{

    // Always set controls from Controls.csv at startup
    setControlsFromCSV();

    if (!hasCommand("curl")) {
        std::cerr << "curl is required but was not found in PATH.\n";
        return 1;
    }

    bool mysqlAvailable = hasCommand("mysql");
    if (!mysqlAvailable) {
        std::cerr << "mysql client was not found in PATH. Download is disabled until it is installed.\n";
    }

    DatabaseConfig config{};
    if (mysqlAvailable) {
        config = promptDatabaseConfig();
    }

    // Hardcoded OracleDBConfig
    OracleDBConfig oracleCfg;
    oracleCfg.host = "147.224.62.87"; // updated to public DB IP
    oracleCfg.port = 13306;
    oracleCfg.user = "omnicon";
    oracleCfg.password = "Geckoboy07!1word";
    oracleCfg.service = "omnicon";

    // Persistent download queue
    DownloadManager::instance().init("saves/download_queue.json", "Roms");

    // Auto-launch the game library shell on startup
    {
        GameLibrary lib;
        lib.scan("Roms");                    // local ROMs first
        lib.scanSteam();                     // installed Steam games
        lib.scanSteamOwned("~/.omnicon/steam.conf"); // all owned Steam games via Web API
        lib.loadFromDB(oracleCfg);          // append DB entries (deduplicates against local)
        lib.fetchSteamCovers("saves/covers"); // download missing Steam cover art from CDN
        lib.resolveCoverPaths("saves/covers");
        GameShell gameShell;
        if (gameShell.init()) {
            gameShell.setDBConfig(oracleCfg);
            gameShell.run(lib, "saves/covers");
            gameShell.cleanup();
        }
    }

    printHelp();

    while (std::cin.good()) {
        const std::string input = readLine("omnicon> ");
        if (input.empty()) {
            continue;
        }

        if (input == "quit" || input == "exit") {
            break;
        }

        if (input == "help") {
            printHelp();
            continue;
        }

        if (input == "config") {
            config = promptDatabaseConfig();
            continue;
        }

        if (input == "shell") {
            GameLibrary lib;
            lib.scan("Roms");
            lib.scanSteam();
            lib.scanSteamOwned("~/.omnicon/steam.conf");
            lib.loadFromDB(oracleCfg);
            lib.fetchSteamCovers("saves/covers"); // download missing Steam cover art from CDN
            lib.resolveCoverPaths("saves/covers");
            GameShell shell;
            if (shell.init()) {
                shell.setDBConfig(oracleCfg);
                shell.run(lib, "saves/covers");
                shell.cleanup();
            } else {
                std::cerr << "Failed to open game shell.\n";
            }
            continue;
        }

        if (input == "db-status") {
            std::vector<OracleGameRow> dbRows;
            if (queryGames(oracleCfg, dbRows)) {
                std::map<std::string, int> counts;
                for (const auto& r : dbRows) {
                    std::string key = r.systemName.empty() ? "(blank)" : r.systemName;
                    counts[key]++;
                }
                std::cout << "DB GAMES table — " << dbRows.size() << " total row(s):\n";
                for (const auto& [sys, cnt] : counts)
                    std::cout << "  " << sys << ": " << cnt << "\n";
            }
            continue;
        }

        if (input == "db-clear") {
            std::cout << "This will DELETE all rows from the GAMES table. Type YES to confirm: ";
            std::string confirm;
            std::getline(std::cin, confirm);
            if (confirm == "YES") {
                if (oracleExecute(oracleCfg, "DELETE FROM GAMES"))
                    std::cout << "All rows deleted.\n";
            } else {
                std::cout << "Aborted.\n";
            }
            continue;
        }

        // db-fix-systems: re-derive SYSTEM_NAME from download URL for rows stored as
        // 'Unknown', fixing data inserted before the dot-stripping bug was corrected.
        if (input == "db-fix-systems") {
            std::vector<OracleGameRow> dbRows;
            if (!queryGames(oracleCfg, dbRows)) { continue; }
            int fixed = 0;
            for (const auto& r : dbRows) {
                if (r.systemName != "Unknown" && !r.systemName.empty()) continue;
                const std::string& src = r.downloadUrl.empty() ? r.localPath : r.downloadUrl;
                std::size_t qpos = src.find_first_of("?#");
                std::string clean = (qpos != std::string::npos) ? src.substr(0, qpos) : src;
                // Strip archive wrappers: "game.nes.zip" -> ".nes"
                std::filesystem::path cp(clean);
                std::string ext = cp.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                static const char* const kArc2[] = {".zip",".7z",".gz",".bz2",".xz",".rar",nullptr};
                for (int ai = 0; kArc2[ai]; ++ai) {
                    if (ext == kArc2[ai]) {
                        std::string inner = cp.stem().extension().string();
                        if (!inner.empty()) ext = inner;
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        break;
                    }
                }
                std::string sysName = GameLibrary::systemName(
                    GameLibrary::systemFromExtension(ext));
                // URL-path fallback: "/roms_nes/" → NES, etc.
                if (sysName == "Unknown") {
                    SystemType urlSys = GameLibrary::systemFromUrl(r.downloadUrl);
                    if (urlSys != SystemType::Unknown)
                        sysName = GameLibrary::systemName(urlSys);
                }
                if (sysName == "Unknown") continue;
                std::string sql = "UPDATE GAMES SET SYSTEM_NAME='" + sysName
                                + "' WHERE ID=" + std::to_string(r.id);
                if (oracleExecute(oracleCfg, sql)) ++fixed;
            }
            std::cout << "db-fix-systems: updated " << fixed << " row(s).\n";
            continue;
        }

        // db-rename-titles: rename all DB rows' NAME to the file title without .zip/.7z
        if (input == "db-rename-titles") {
            std::vector<OracleGameRow> dbRows;
            if (!queryGames(oracleCfg, dbRows)) { continue; }
            int renamed = 0;
            for (const auto& r : dbRows) {
                // Use downloadUrl if present, else localPath
                const std::string& src = !r.downloadUrl.empty() ? r.downloadUrl : r.localPath;
                if (src.empty()) continue;
                std::size_t qpos = src.find_first_of("?#");
                std::string clean = (qpos != std::string::npos) ? src.substr(0, qpos) : src;
                std::filesystem::path cp(clean);
                std::string base = cp.filename().string();
                // Remove archive extensions
                static const char* const kArc[] = {".zip",".7z",nullptr};
                std::string ext = cp.extension().string();
                for (int ai = 0; kArc[ai]; ++ai) {
                    if (ext == kArc[ai]) {
                        base = cp.stem().filename().string();
                        break;
                    }
                }
                // Remove extension if still present
                std::size_t dot = base.rfind('.');
                if (dot != std::string::npos) base = base.substr(0, dot);
                // Only update if different
                if (base != r.name && !base.empty()) {
                    std::string sql = "UPDATE GAMES SET NAME='" + sqlEscape(base) + "' WHERE ID=" + std::to_string(r.id);
                    if (oracleExecute(oracleCfg, sql)) ++renamed;
                }
            }
            std::cout << "db-rename-titles: updated " << renamed << " row(s).\n";
            continue;
        }

        // scrape-unknown — download cover art for all Unknown-system library entries
        if (input == "scrape-unknown") {
            const std::string csvPath = "saves/unknown_games.csv";
            std::filesystem::create_directories("saves");
            // Build a fresh library so we have the latest DB state
            GameLibrary lib;
            lib.scan("Roms");
            lib.loadFromDB(oracleCfg);
            lib.resolveCoverPaths("saves/covers");
            std::ofstream csv(csvPath);
            if (!csv) {
                std::cerr << "scrape-unknown: could not write " << csvPath << "\n";
            } else {
                int count = 0;
                for (const auto& g : lib.games()) {
                    if (g.system != SystemType::Unknown) continue;
                    auto csvField = [](const std::string& s) -> std::string {
                        bool needs = s.find_first_of(",\"\n") != std::string::npos;
                        if (!needs) return s;
                        std::string out = "\"";
                        for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
                        out += '"'; return out;
                    };
                    csv << csvField(g.displayName) << "," << csvField(g.downloadUrl) << "\n";
                    ++count;
                }
                csv.close();
                if (count == 0) {
                    std::cout << "scrape-unknown: no Unknown-system games found.\n";
                } else {
                    std::cout << "scrape-unknown: exporting " << count << " game(s) and fetching covers...\n";
                    std::cout.flush();
                    const int ret = std::system(("python3 scrape_covers.py --unknown-csv " + csvPath).c_str());
                    if (ret != 0)
                        std::cerr << "scrape-unknown: scraper exited with code " << ret << "\n";
                    else
                        lib.resolveCoverPaths("saves/covers");
                }
            }
            continue;
        }

        if (input == "scrape-covers" || input == "scrape_covers") {
            GameLibrary lib;
            lib.scan("Roms");
            lib.scanSteam();
            lib.scanSteamOwned("~/.omnicon/steam.conf");
            lib.loadFromDB(oracleCfg);
            lib.resolveCoverPaths("saves/covers");
            scrapeCoversIfNeeded(lib, "saves/covers");
            continue;
        }

        if (input == "controls" || input == "change controls" || input == "change-controls") {
            configureAllControls();
            continue;
        }

        if (input == "controls-csv") {
            setControlsFromCSV();
            continue;
        }

        // Legacy per-system aliases — redirect to unified menu
        if (input == "controls-snes" || input == "controls-gb" || input == "controls-gbc" ||
            input == "controls-gba"  || input == "controls-nds" || input == "controls-ps1" ||
            input == "controls-atari2600" || input == "controls-atari5200" || input == "controls-atari800") {
            std::cout << "Per-system controls have been merged. Opening unified controls menu...\n";
            configureAllControls();
            continue;
        }

        // load url <url> — scrape page links and add them to the Oracle DB
        if (input.rfind("load url ", 0) == 0) {
            std::string url = input.substr(9);
            // trim leading/trailing whitespace
            while (!url.empty() && (url.front() == ' ' || url.front() == '\t')) url.erase(url.begin());
            while (!url.empty() && (url.back()  == ' ' || url.back()  == '\t' ||
                                    url.back()  == '\r'|| url.back()  == '\n')) url.pop_back();

            if (!isSupportedUrl(url)) {
                std::cerr << "Invalid URL: " << url << "\n";
            } else {
                std::string pageText;
                if (!downloadToString(url, pageText)) {
                    std::cerr << "Failed to fetch page: " << url << "\n";
                } else {
                    std::vector<std::string> links = extractPageFileLinks(url, pageText);
                    if (links.empty()) {
                        std::cout << "No ROM links found on page.\n";
                    } else {
                        createGamesTable(oracleCfg);

                        // Detect system from the page URL once for the whole batch
                        SystemType batchSys = GameLibrary::systemFromUrl(url);
                        std::string batchSysName;
                        if (batchSys != SystemType::Unknown) {
                            batchSysName = GameLibrary::systemName(batchSys);
                            std::cout << "Detected system: " << batchSysName << "\n";
                        } else {
                            std::cout << "Could not detect system from URL.\n"
                                      << "Enter system (NES, SNES, GBA, GBC, GameBoy, NDS, N64,\n"
                                      << "  SegaGenesis, Atari2600, Atari5200, Atari800, 3DS, Odyssey)\n"
                                      << "or leave blank to skip unknowns: ";
                            std::cout.flush();
                            std::getline(std::cin, batchSysName);
                            while (!batchSysName.empty() && (batchSysName.front()==' '||batchSysName.front()=='\t')) batchSysName.erase(batchSysName.begin());
                            while (!batchSysName.empty() && (batchSysName.back()==' '||batchSysName.back()=='\t'||batchSysName.back()=='\r'||batchSysName.back()=='\n')) batchSysName.pop_back();
                        }

                        int inserted = 0;
                        int total = static_cast<int>(links.size());
                        std::cout << "Loading " << total << " link(s) into Oracle DB...\n"; std::cout.flush();
                        for (int li = 0; li < total; ++li) {
                            const auto& link = links[li];
                            // Decode the percent-encoded filename, keep all info (region, etc.)
                            std::string rawFile = fileNameFromUrl(link);
                            std::string decoded = urlDecode(rawFile);
                            std::string name = decoded; // Use full filename (minus extension)
                            auto dotPos = name.rfind('.');
                            if (dotPos != std::string::npos) name = name.substr(0, dotPos);
                            std::cout << "[" << (li + 1) << "/" << total << "] " << name << "\n"; std::cout.flush();
                            // Strip archive wrappers: "game.nes.zip" -> ".nes"
                            std::filesystem::path namePath(name);
                            std::string extLower = namePath.extension().string();
                            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
                            static const char* const kArc[] = {".zip",".7z",".gz",".bz2",".xz",".rar",nullptr};
                            for (int ai = 0; kArc[ai]; ++ai) {
                                if (extLower == kArc[ai]) {
                                    std::string inner = namePath.stem().extension().string();
                                    if (!inner.empty()) extLower = inner;
                                    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
                                    break;
                                }
                            }
                            std::string sysName = GameLibrary::systemName(
                                GameLibrary::systemFromExtension(extLower));
                            // URL-path fallback: "/roms_nes/" → NES, etc.
                            if (sysName == "Unknown") {
                                SystemType urlSys = GameLibrary::systemFromUrl(link);
                                if (urlSys != SystemType::Unknown)
                                    sysName = GameLibrary::systemName(urlSys);
                            }
                            // User-supplied batch fallback
                            if (sysName == "Unknown" && !batchSysName.empty())
                                sysName = batchSysName;
                            if (insertGameEntry(oracleCfg, name, link, "", sysName, 0))
                                ++inserted;
                        }
                        std::cout << "Inserted " << inserted << " of " << links.size()
                                  << " links into Oracle DB.\n";
                    }
                }
            }
            continue;
        }

        const std::string playCommand = "play ";
        const std::string playSnesCommand = "play-snes ";
        const std::string playGbCommand = "play-gb ";
        const std::string playGbcCommand = "play-gbc ";
        const std::string playAtari2600Command = "play-atari2600 ";
        const std::string playAtari5200Command = "play-atari5200 ";
        const std::string playGbaCommand = "play-gba ";
        const std::string playNdsCommand = "play-nds ";
        const std::string playPs1Command = "play-ps1 ";
        const std::string playGenesisCommand = "play-genesis ";
        const std::string playN64Command = "play-n64 ";
        const bool isPlayCommand = input.rfind(playCommand, 0) == 0
            && input.rfind(playSnesCommand, 0) != 0
            && input.rfind(playGbCommand, 0) != 0
            && input.rfind(playGbcCommand, 0) != 0
            && input.rfind(playAtari2600Command, 0) != 0
            && input.rfind(playAtari5200Command, 0) != 0
            && input.rfind(playGbaCommand, 0) != 0
            && input.rfind(playNdsCommand, 0) != 0
            && input.rfind(playPs1Command, 0) != 0
            && input.rfind(playGenesisCommand, 0) != 0
            && input.rfind(playN64Command, 0) != 0;
        const bool isPlaySnesCommand       = input.rfind(playSnesCommand, 0)    == 0;
        const bool isPlayGbCommand      = input.rfind(playGbCommand, 0)      == 0;
        const bool isPlayGbcCommand     = input.rfind(playGbcCommand, 0)     == 0;
        const bool isPlayAtari2600Command = input.rfind(playAtari2600Command, 0) == 0;
        const bool isPlayAtari5200Command = input.rfind(playAtari5200Command, 0) == 0;
        const bool isPlayGbaCommand     = input.rfind(playGbaCommand, 0)     == 0;
        const bool isPlayNdsCommand     = input.rfind(playNdsCommand, 0)     == 0;
        const bool isPlayPs1Command     = input.rfind(playPs1Command, 0)     == 0;
        const bool isPlayGenesisCommand = input.rfind(playGenesisCommand, 0) == 0;
        const bool isPlayN64Command     = input.rfind(playN64Command, 0)     == 0;

        if (isPlayCommand) {
            std::string romPath = trim(input.substr(playCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            std::filesystem::path romFsPath(romPath);
            std::string extension = toLowerCopy(romFsPath.extension().string());

            if (extension == ".gba") {
                GBAEmulation gba;
                try {
                    if (gba.loadRom(romPath)) {
                        gba.runInteractive();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "GBA error: " << e.what() << "\n";
                }
            } else if (extension == ".gbc" || extension == ".sgb") {
                GameBoyColorEmulation gbc;
                if (gbc.loadRom(romPath)) {
                    gbc.runInteractive();
                }
            } else if (extension == ".gb") {
                GameBoyEmulation gb;
                if (gb.loadRom(romPath)) {
                    gb.runInteractive();
                }
            } else if (extension == ".smc" || extension == ".sfc") {
                SNESEmulation snes;
                if (snes.loadRom(romPath)) {
                    snes.runInteractive();
                }
            } else if (extension == ".a26") {
                Atari2600Emulation atari2600;
                if (atari2600.loadRom(romPath)) {
                    atari2600.runInteractive();
                }
            } else if (extension == ".nds") {
                NDSEmulation nds;
                try {
                    if (nds.loadRom(romPath)) {
                        nds.runInteractive();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "NDS error: " << e.what() << "\n";
                }
            } else if (extension == ".a52") {
                Atari5200Emulation atari5200;
                if (atari5200.loadRom(romPath)) {
                    atari5200.runInteractive();
                }
            } else if (extension == ".md" || extension == ".gen" || extension == ".smd" ||
                       extension == ".68k" || extension == ".sgd") {
                SegaGenesisEmulation gen;
                if (gen.loadRom(romPath)) {
                    gen.runInteractive();
                }
            } else if (extension == ".n64" || extension == ".z64" || extension == ".v64") {
                N64Emulation n64;
                if (n64.loadRom(romPath)) {
                    n64.runInteractive();
                }
            } else if (extension == ".cue" || extension == ".iso" ||
                       extension == ".pbp" || extension == ".chd" ||
                       extension == ".img" || extension == ".mdf") {
                PS1Emulation ps1;
                try {
                    if (ps1.loadRom(romPath)) {
                        ps1.runInteractive();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "PS1 error: " << e.what() << "\n";
                }
            } else if (extension == ".bin" || extension == ".rom") {
                // Ambiguous extension: try Atari 5200 first, then Atari 2600, then NES
                Atari5200Emulation atari5200;
                if (atari5200.loadRom(romPath)) {
                    atari5200.runInteractive();
                } else {
                    Atari2600Emulation atari2600;
                    if (atari2600.loadRom(romPath)) {
                        atari2600.runInteractive();
                    }
                }
            } else {
                NESEmulation nes;
                if (nes.loadRom(romPath)) {
                    nes.runInteractive();
                }
            }
            continue;
        }

        if (isPlayGbCommand) {
            std::string romPath = trim(input.substr(playGbCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            GameBoyEmulation gb;
            try {
                if (gb.loadRom(romPath)) {
                    gb.runInteractive();
                }
            } catch (const std::exception& e) {
                std::cerr << "Game Boy error: " << e.what() << "\n";
            }
            continue;
        }

        if (isPlayGbcCommand) {
            std::string romPath = trim(input.substr(playGbcCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            GameBoyColorEmulation gbc;
            try {
                if (gbc.loadRom(romPath)) {
                    gbc.runInteractive();
                }
            } catch (const std::exception& e) {
                std::cerr << "Game Boy Color error: " << e.what() << "\n";
            }
            continue;
        }

        if (isPlaySnesCommand) {
            std::string romPath = trim(input.substr(playSnesCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            SNESEmulation snes;
            if (snes.loadRom(romPath)) {
                snes.runInteractive();
            }
            continue;
        }

        if (isPlayAtari2600Command) {
            std::string romPath = trim(input.substr(playAtari2600Command.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            Atari2600Emulation atari2600;
            if (atari2600.loadRom(romPath)) {
                atari2600.runInteractive();
            }
            continue;
        }

        if (isPlayAtari5200Command) {
            std::string romPath = trim(input.substr(playAtari5200Command.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            Atari5200Emulation atari5200;
            if (atari5200.loadRom(romPath)) {
                atari5200.runInteractive();
            }
            continue;
        }

        if (isPlayGbaCommand) {
            std::string romPath = trim(input.substr(playGbaCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            GBAEmulation gba;
            try {
                if (gba.loadRom(romPath)) {
                    gba.runInteractive();
                }
            } catch (const std::exception& e) {
                std::cerr << "GBA error: " << e.what() << "\n";
            }
            continue;
        }

        if (isPlayNdsCommand) {
            std::string romPath = trim(input.substr(playNdsCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            NDSEmulation nds;
            try {
                if (nds.loadRom(romPath)) {
                    nds.runInteractive();
                }
            } catch (const std::exception& e) {
                std::cerr << "NDS error: " << e.what() << "\n";
            }
            continue;
        }

        if (isPlayPs1Command) {
            std::string romPath = trim(input.substr(playPs1Command.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            PS1Emulation ps1;
            try {
                if (ps1.loadRom(romPath)) {
                    ps1.runInteractive();
                }
            } catch (const std::exception& e) {
                std::cerr << "PS1 error: " << e.what() << "\n";
            }
            continue;
        }

        if (isPlayGenesisCommand) {
            std::string romPath = trim(input.substr(playGenesisCommand.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            SegaGenesisEmulation gen;
            if (gen.loadRom(romPath)) {
                gen.runInteractive();
            }
            continue;
        }

        if (isPlayN64Command) {
            std::string romPath = trim(input.substr(playN64Command.size()));
            romPath = stripOuterQuotes(romPath);
            romPath = normalizePath(romPath);

            if (romPath.empty()) {
                std::cout << "ROM path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(romPath)) {
                std::cout << "ROM file does not exist: " << romPath << "\n";
                continue;
            }

            N64Emulation n64;
            if (n64.loadRom(romPath)) {
                n64.runInteractive();
                // After session, dump save data and store in DB using full ROM path as key
                auto saveData = n64.dumpSaveData();
                if (!saveData.empty()) {
                    DatabaseConfig config;
                    config.host = kDatabaseHost;
                    config.port = kDatabasePort;
                    config.user = kDatabaseUser;
                    config.password = ""; // Set or prompt for password if needed
                    config.database = kDatabaseName;
                    // Use the full ROM path as the key, sanitize for SQL and shell
                    std::string romKey = romPath;
                    std::string romKeySql = sqlEscape(romKey);
                    std::string romKeyShell = shellEscape(romKey);
                    const std::string saveHex = hexEncode(saveData);
                    const std::string sql =
                        "UPDATE blocks SET save_data = X'" + saveHex + "' WHERE name = '" + romKeySql + "';\n";
                    std::string command;
                    command += "mysql --protocol=TCP --host=" + shellEscape(config.host)
                        + " --port=" + shellEscape(config.port)
                        + " --user=" + shellEscape(config.user)
                        + " --password=" + shellEscape(config.password)
                        + " --database=" + shellEscape(config.database)
                        + " 2>/dev/null";
                    std::cerr << "[N64] About to update DB for save: key='" << romKey << "' (sql-escaped: '" << romKeySql << "')\n";
                    FILE* mysqlPipe = popen(command.c_str(), "w");
                    if (mysqlPipe) {
                        size_t written = fwrite(sql.data(), 1, sql.size(), mysqlPipe);
                        std::cerr << "[N64] Wrote " << written << " bytes to mysql pipe\n";
                        int rc = pclose(mysqlPipe);
                        std::cerr << "[N64] mysql pipe closed, rc=" << rc << "\n";
                        std::cout << "[N64] Save data updated in DB for " << romKey << "\n";
                    } else {
                        std::cerr << "[N64] Failed to open mysql pipe for save data update\n";
                    }
                    std::cerr << "[N64] DB update for save complete\n";
                } else {
                    std::cout << "[N64] No save data to store for " << romPath << "\n";
                }
            }
            continue;
        }

        const std::string downloadCommand = "download ";
        const std::string downloadAllCommand = "download-all ";
        const std::string loadFolderCommand = "load-folder ";
        const bool isSingleDownload = input.rfind(downloadCommand, 0) == 0;
        const bool isBatchDownload = input.rfind(downloadAllCommand, 0) == 0;
        const bool isFolderCommand = input.rfind(loadFolderCommand, 0) == 0;

        const std::string rawPathInput = isFolderCommand ? trim(input.substr(loadFolderCommand.size())) : input;
        const std::filesystem::path folderPath = normalizePath(stripOuterQuotes(rawPathInput));
        const bool isDirectFolderPath = !isSingleDownload && !isBatchDownload && !isFolderCommand
            && !rawPathInput.empty() && std::filesystem::is_directory(folderPath);

        if (!isSingleDownload && !isBatchDownload && !isFolderCommand && !isDirectFolderPath) {
            std::cout << "Unknown command. Type 'help' for usage.\n";
            continue;
        }

        if (isFolderCommand || isDirectFolderPath) {
            if (rawPathInput.empty()) {
                std::cout << "Folder path is required.\n";
                continue;
            }

            if (!std::filesystem::exists(folderPath) || !std::filesystem::is_directory(folderPath)) {
                std::cout << "Folder path does not exist or is not a directory.\n";
                continue;
            }

            if (!mysqlAvailable) {
                mysqlAvailable = hasCommand("mysql");
                if (!mysqlAvailable) {
                    std::cout << "mysql client is required for upload storage. Install mysql and retry.\n";
                    continue;
                }
                config = promptDatabaseConfig();
            }

            int successCount = 0;
            int failureCount = 0;

            const std::filesystem::directory_options dirOptions = std::filesystem::directory_options::skip_permission_denied;
            std::error_code walkError;
            for (std::filesystem::recursive_directory_iterator it(folderPath, dirOptions, walkError), end; it != end; ++it) {
                if (walkError) {
                    ++failureCount;
                    walkError.clear();
                    continue;
                }

                if (!it->is_regular_file()) {
                    continue;
                }

                std::vector<unsigned char> bytes;
                if (!readLocalFileToBytes(it->path(), bytes)) {
                    ++failureCount;
                    std::cout << "  Failed to read " << it->path().string() << "\n";
                    continue;
                }

                const int gameId = nextGameId();
                const std::string fileName = it->path().filename().string();
                if (insertBlock(config, gameId, fileName, bytes)) {
                    ++successCount;
                    std::cout << "  Stored GameID=" << gameId << " Name=" << fileName << "\n";
                } else {
                    ++failureCount;
                    std::cout << "  Failed to store " << it->path().string() << "\n";
                }
            }

            std::cout << "Folder load complete. Stored=" << successCount << " Failed=" << failureCount << "\n";
            continue;
        }

        const std::string url = trim(input.substr(isBatchDownload ? downloadAllCommand.size() : downloadCommand.size()));
        if (!isSupportedUrl(url)) {
            std::cout << "Only http:// and https:// URLs are supported.\n";
            continue;
        }

        if (!mysqlAvailable) {
            mysqlAvailable = hasCommand("mysql");
            if (!mysqlAvailable) {
                std::cout << "mysql client is required for download storage. Install mysql and retry.\n";
                continue;
            }
            config = promptDatabaseConfig();
        }

        if (isSingleDownload) {
            const int gameId = nextGameId();
            std::string rawFile = fileNameFromUrl(url);
            std::string decoded = urlDecode(rawFile);
            std::string fileName = decoded;
            auto dotPos = fileName.rfind('.');
            if (dotPos != std::string::npos) fileName = fileName.substr(0, dotPos);
            std::vector<unsigned char> bytes;

            std::cout << "Downloading " << url << " ...\n";
            if (!downloadToBytes(url, bytes)) {
                std::cout << "Download failed.\n";
                continue;
            }

            if (insertBlock(config, gameId, fileName, bytes)) {
                std::cout << "Stored block GameID=" << gameId << " Name=" << fileName << "\n";
            } else {
                std::cout << "Database insert failed. Check your credentials and schema.\n";
            }
            continue;
        }

        std::cout << "Scanning page " << url << " ...\n";
        std::string html;
        if (!downloadToString(url, html)) {
            std::cout << "Failed to fetch page content.\n";
            continue;
        }

        const std::vector<std::string> links = extractPageFileLinks(url, html);
        if (links.empty()) {
            std::cout << "No file links found on page.\n";
            continue;
        }

        int successCount = 0;
        int failureCount = 0;
        for (const std::string& link : links) {
            const int gameId = nextGameId();
            std::string rawFile = fileNameFromUrl(link);
            std::string decoded = urlDecode(rawFile);
            std::string fileName = decoded;
            auto dotPos = fileName.rfind('.');
            if (dotPos != std::string::npos) fileName = fileName.substr(0, dotPos);
            std::vector<unsigned char> bytes;

            std::cout << "Downloading " << link << " ...\n";
            if (!downloadToBytes(link, bytes)) {
                ++failureCount;
                std::cout << "  Failed to download.\n";
                continue;
            }

            if (insertBlock(config, gameId, fileName, bytes)) {
                ++successCount;
                std::cout << "  Stored GameID=" << gameId << " Name=" << fileName << "\n";
            } else {
                ++failureCount;
                std::cout << "  Failed to store in database.\n";
            }
        }

        std::cout << "Batch complete. Stored=" << successCount << " Failed=" << failureCount << "\n";
    }

    return 0;
}
