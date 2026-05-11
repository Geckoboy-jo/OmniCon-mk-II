#include "top.hpp"
#include "NES.hpp"
#include "GameBoy.hpp"
#include "SNES.hpp"
#include "save_tree.hpp"

#include <cstdlib>
#include <cctype>
#include <set>
#include <regex>
#include <chrono>
#include <cstdint>
#include <sstream>

namespace {

struct DatabaseConfig {
    std::string host;
    std::string port;
    std::string user;
    std::string password;
    std::string database;
};

bool isSupportedUrl(const std::string& url);

constexpr const char* kDatabaseHost = "127.0.0.1";
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

std::string shellEscape(const std::string& value)
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

bool hexDecode(const std::string& hex, std::vector<unsigned char>& bytes)
{
    if (hex.size() % 2 != 0) {
        return false;
    }

    auto hexValue = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };

    bytes.clear();
    bytes.reserve(hex.size() / 2);

    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const int high = hexValue(hex[index]);
        const int low = hexValue(hex[index + 1]);
        if (high < 0 || low < 0) {
            bytes.clear();
            return false;
        }

        bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }

    return true;
}

std::uint64_t fnv1a64(const unsigned char* data, std::size_t size, std::uint64_t seed = 1469598103934665603ULL)
{
    std::uint64_t hash = seed;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(data[index]);
        hash *= 1099511628211ULL;
    }

    return hash;
}

std::uint64_t stableSaveGameId(const std::string& platform, const std::vector<unsigned char>& romBytes)
{
    std::uint64_t hash = fnv1a64(reinterpret_cast<const unsigned char*>(platform.data()), platform.size());
    hash = fnv1a64(romBytes.data(), romBytes.size(), hash);
    if (hash == 0) {
        hash = 1;
    }

    return hash;
}

std::string buildMysqlCommand(const DatabaseConfig& config)
{
    std::string command;
    command += "mysql --protocol=TCP --batch --raw --skip-column-names --host=" + shellEscape(config.host)
        + " --port=" + shellEscape(config.port)
        + " --user=" + shellEscape(config.user)
        + " --password=" + shellEscape(config.password)
        + " --database=" + shellEscape(config.database)
        + " 2>/dev/null";
    return command;
}

bool runMysqlQuery(const DatabaseConfig& config, const std::string& query, std::string& output)
{
    const std::string command =
        "mysql --protocol=TCP --batch --raw --skip-column-names --host=" + shellEscape(config.host)
        + " --port=" + shellEscape(config.port)
        + " --user=" + shellEscape(config.user)
        + " --password=" + shellEscape(config.password)
        + " --database=" + shellEscape(config.database)
        + " --execute=" + shellEscape(query)
        + " 2>/dev/null";

    FILE* mysqlPipe = popen(command.c_str(), "r");
    if (mysqlPipe == nullptr) {
        return false;
    }

    char buffer[4096];
    output.clear();
    while (true) {
        const std::size_t readCount = fread(buffer, 1, sizeof(buffer), mysqlPipe);
        if (readCount > 0) {
            output.append(buffer, buffer + readCount);
        }

        if (readCount < sizeof(buffer)) {
            if (feof(mysqlPipe)) {
                break;
            }
            if (ferror(mysqlPipe)) {
                pclose(mysqlPipe);
                return false;
            }
        }
    }

    return pclose(mysqlPipe) == 0;
}

bool readLocalFileToBytes(const std::filesystem::path& filePath, std::vector<unsigned char>& bytes);

bool loadSaveIndexFromDatabase(const DatabaseConfig& config, SaveBPlusTree& saveIndex)
{
    std::string output;
    if (!runMysqlQuery(config,
            "SELECT game_id, HEX(save_data) FROM blocks WHERE save_data IS NOT NULL AND OCTET_LENGTH(save_data) > 0;",
            output)) {
        return false;
    }

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }

        const std::string keyText = trim(line.substr(0, tab));
        const std::string hexText = trim(line.substr(tab + 1));
        if (keyText.empty() || hexText.empty()) {
            continue;
        }

        try {
            const std::uint64_t key = static_cast<std::uint64_t>(std::stoull(keyText));
            std::vector<unsigned char> saveBytes;
            if (hexDecode(hexText, saveBytes)) {
                saveIndex.upsert(key, std::move(saveBytes));
            }
        } catch (...) {
            continue;
        }
    }

    return true;
}

bool persistSaveData(const DatabaseConfig& config,
    std::uint64_t gameId,
    const std::string& fileName,
    const std::vector<unsigned char>& romData,
    const std::vector<unsigned char>& saveData)
{
    if (saveData.empty()) {
        return false;
    }

    const std::string romHex = hexEncode(romData);
    const std::string saveHex = hexEncode(saveData);
    const std::string sql =
        "INSERT INTO blocks (game_id, name, game, save_data, image) VALUES ("
        + std::to_string(gameId) + ", '" + sqlEscape(fileName) + "', X'" + romHex + "', X'" + saveHex + "', X'') "
        "ON DUPLICATE KEY UPDATE name = VALUES(name), game = VALUES(game), save_data = VALUES(save_data);\n";

    const std::string command = buildMysqlCommand(config);
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

template <typename Emulator>
void runGameSession(const DatabaseConfig& config,
    SaveBPlusTree& saveIndex,
    const std::string& platform,
    const std::string& romPath,
    bool persistToDatabase)
{
    std::vector<unsigned char> romBytes;
    if (!readLocalFileToBytes(romPath, romBytes)) {
        std::cout << "Failed to read ROM bytes for save lookup.\n";
        return;
    }

    const std::uint64_t gameId = stableSaveGameId(platform, romBytes);
    std::vector<unsigned char> saveBytes;
    const bool hasSave = saveIndex.find(gameId, saveBytes);

    Emulator emulator;
    if (!emulator.loadRom(romPath)) {
        return;
    }

    if (hasSave && !saveBytes.empty()) {
        if (emulator.restoreSaveData(saveBytes)) {
            std::cout << "Loaded save data for this ROM.\n";
        } else {
            std::cout << "Found save data, but it could not be restored.\n";
        }
    }

    emulator.runInteractive();

    const std::vector<unsigned char> newSave = emulator.dumpSaveData();
    if (newSave.empty()) {
        return;
    }

    saveIndex.upsert(gameId, newSave);
    const std::string fileName = std::filesystem::path(romPath).filename().string();
    if (persistToDatabase) {
        if (persistSaveData(config, gameId, fileName, romBytes, newSave)) {
            std::cout << "Saved emulator state for " << fileName << "\n";
        } else {
            std::cout << "Save state updated in memory, but database persistence failed.\n";
        }
    }
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

std::uint64_t nextGameId()
{
    static std::uint64_t gameIdSeed = static_cast<std::uint64_t>(
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

    static const std::regex hrefRegex(R"(href\s*=\s*[\"']([^\"']+)[\"'])", std::regex::icase);
    std::sregex_iterator iter(html.begin(), html.end(), hrefRegex);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
        const std::string href = trim((*iter)[1].str());
        if (shouldSkipHref(href)) {
            continue;
        }

        const std::string resolved = resolveUrl(pageUrl, href);
        if (!isSupportedUrl(resolved) || !looksLikeDownloadableFileLink(resolved)) {
            continue;
        }

        if (unique.insert(resolved).second) {
            links.push_back(resolved);
        }
    }

    return links;
}

bool insertBlock(const DatabaseConfig& config, std::uint64_t gameId, const std::string& fileName, const std::vector<unsigned char>& gameData)
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

void printHelp()
{
    std::cout << "Commands:\n"
              << "  download <url>  Download one file and store it as a block\n"
              << "  download-all <url>  Download file links from a page and store each as a block\n"
              << "  load-folder <path>  Upload every regular file in a local folder\n"
              << "  play <path>     Load and run a ROM file (.nes for NES, .gb/.gbc/.sgb for Game Boy, .smc/.sfc for SNES)\n"
              << "                  (Supports Windows paths via WSL: C:\\Users\\...\\file.nes)\n"
              << "  controls-gb     Change global Game Boy controls\n"
              << "  controls        Change global NES controls (applies to all ROMs)\n"
              << "  controls-snes   Change global SNES controls\n"
              << "  config          Re-enter database password\n"
              << "  help            Show this message\n"
              << "  quit            Exit the REPL\n";
}

bool isSupportedUrl(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

} // namespace

int main()
{
    if (!hasCommand("curl")) {
        std::cerr << "curl is required but was not found in PATH.\n";
        return 1;
    }

    bool mysqlAvailable = hasCommand("mysql");
    if (!mysqlAvailable) {
        std::cerr << "mysql client was not found in PATH. Download is disabled until it is installed.\n";
    }

    std::cout << "OmniCon downloader REPL\n";
    DatabaseConfig config{};
    SaveBPlusTree saveIndex;
    if (mysqlAvailable) {
        config = promptDatabaseConfig();
        if (loadSaveIndexFromDatabase(config, saveIndex)) {
            std::cout << "Loaded cached save data index.\n";
        } else {
            std::cout << "Save data index could not be loaded; play sessions will still work, but cached save lookup is disabled.\n";
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
            if (mysqlAvailable) {
                loadSaveIndexFromDatabase(config, saveIndex);
            }
            continue;
        }

        if (input == "controls" || input == "change controls" || input == "change-controls") {
            NESEmulation::configureGlobalControls();
            continue;
        }

        if (input == "controls-snes" || input == "change snes controls" || input == "change-snes-controls") {
            SNESEmulation::configureGlobalControls();
            continue;
        }

        if (input == "controls-gb" || input == "change gb controls" || input == "change-gb-controls") {
            GameBoyEmulation::configureGlobalControls();
            continue;
        }

        const std::string playCommand = "play ";
        const bool isPlayCommand = input.rfind(playCommand, 0) == 0;

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

            std::string ext = std::filesystem::path(romPath).extension().string();
            ext = toLowerCopy(ext);

            const bool isSnes = (ext == ".smc" || ext == ".sfc");
            const bool isGameBoy = (ext == ".gb" || ext == ".gbc" || ext == ".sgb");
            const bool isNes  = (ext == ".nes");

            if (isGameBoy) {
                runGameSession<GameBoyEmulation>(config, saveIndex, "gb", romPath, mysqlAvailable);
            } else if (isSnes) {
                runGameSession<SNESEmulation>(config, saveIndex, "snes", romPath, mysqlAvailable);
            } else if (isNes) {
                runGameSession<NESEmulation>(config, saveIndex, "nes", romPath, mysqlAvailable);
            } else {
                std::cout << "Unsupported ROM format. Use .nes (NES), .gb/.gbc/.sgb (Game Boy), or .smc/.sfc (SNES).\n";
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

                const std::uint64_t gameId = nextGameId();
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
            const std::uint64_t gameId = nextGameId();
            const std::string fileName = fileNameFromUrl(url);
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
            const std::uint64_t gameId = nextGameId();
            const std::string fileName = fileNameFromUrl(link);
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