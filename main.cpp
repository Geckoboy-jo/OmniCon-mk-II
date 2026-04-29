#include "top.hpp"

#include <set>
#include <regex>

namespace {

struct DatabaseConfig {
    std::string host;
    std::string port;
    std::string user;
    std::string password;
    std::string database;
};

bool isSupportedUrl(const std::string& url);

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

std::string readLineWithDefault(const std::string& prompt, const std::string& defaultValue)
{
    const std::string value = readLine(prompt + " [" + defaultValue + "]: ");
    return value.empty() ? defaultValue : value;
}

std::string getEnvOrDefault(const char* key, const std::string& defaultValue)
{
    const char* value = std::getenv(key);
    return value == nullptr ? defaultValue : std::string(value);
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

int nextGameId()
{
    static int gameIdSeed = 100000;
    return gameIdSeed++;
}

DatabaseConfig promptDatabaseConfig()
{
    DatabaseConfig config;
    config.host = readLineWithDefault("Database host", getEnvOrDefault("OMNICON_DB_HOST", "localhost"));
    config.port = readLineWithDefault("Database port", getEnvOrDefault("OMNICON_DB_PORT", "3306"));
    config.user = readLineWithDefault("Database user", getEnvOrDefault("OMNICON_DB_USER", "block_app"));
    config.database = readLineWithDefault("Database name", getEnvOrDefault("OMNICON_DB_NAME", "omnicon_blocks"));

    const std::string envPassword = getEnvOrDefault("OMNICON_DB_PASSWORD", "");
    if (envPassword.empty()) {
        config.password = readLine("Database password: ");
    } else {
        config.password = envPassword;
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

bool looksLikeFileLink(const std::string& url)
{
    if (url.empty()) {
        return false;
    }

    const std::size_t queryStart = url.find_first_of("?#");
    const std::string cleanUrl = queryStart == std::string::npos ? url : url.substr(0, queryStart);

    if (cleanUrl.empty() || cleanUrl.back() == '/') {
        return false;
    }

    const std::string name = fileNameFromUrl(cleanUrl);
    return name.find('.') != std::string::npos;
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
        if (!isSupportedUrl(resolved) || !looksLikeFileLink(resolved)) {
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
        "INSERT INTO blocks (game_id, name, game, save_data, image) VALUES ("
        + std::to_string(gameId) + ", '" + sqlEscape(fileName) + "', X'" + gameHex + "', X'', X'');\n";

    std::string command;
    if (!config.password.empty()) {
        command += "MYSQL_PWD=" + shellEscape(config.password) + " ";
    }

    command += "mysql --host=" + shellEscape(config.host)
        + " --port=" + shellEscape(config.port)
        + " --user=" + shellEscape(config.user)
        + " --database=" + shellEscape(config.database);

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
              << "  config          Re-enter database connection settings\n"
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
    if (mysqlAvailable) {
        config = promptDatabaseConfig();
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

        const std::string downloadCommand = "download ";
        const std::string downloadAllCommand = "download-all ";
        const bool isSingleDownload = input.rfind(downloadCommand, 0) == 0;
        const bool isBatchDownload = input.rfind(downloadAllCommand, 0) == 0;

        if (!isSingleDownload && !isBatchDownload) {
            std::cout << "Unknown command. Type 'help' for usage.\n";
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
            const int gameId = nextGameId();
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