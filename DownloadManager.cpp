#include "DownloadManager.hpp"
#include "GameLibrary.hpp"
#include "ExtraSystems.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <filesystem>
#include <sys/stat.h>

namespace fs = std::filesystem;

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>

#define close _close
#define pclose _pclose
#define popen _popen
#define unlink _unlink

static std::tm* gmtime_r(const std::time_t* source, std::tm* destination) {
    return gmtime_s(destination, source) == 0 ? destination : nullptr;
}

static int mkstemp(char* pathTemplate) {
    const std::string original(pathTemplate);
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::memcpy(pathTemplate, original.c_str(), original.size() + 1);
        if (_mktemp_s(pathTemplate, original.size() + 1) != 0) return -1;
        const int file = _open(pathTemplate, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                               _S_IREAD | _S_IWRITE);
        if (file >= 0) return file;
    }
    return -1;
}

static char* mkdtemp(char* pathTemplate) {
    const std::string original(pathTemplate);
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::memcpy(pathTemplate, original.c_str(), original.size() + 1);
        if (_mktemp_s(pathTemplate, original.size() + 1) != 0) return nullptr;
        std::error_code error;
        if (fs::create_directory(pathTemplate, error)) return pathTemplate;
        if (error && error != std::errc::file_exists) return nullptr;
    }
    return nullptr;
}
#endif

// Allowed ROM extensions (lower-cased, without leading dot)
static const std::vector<std::string> kRomExts = {
    "nes","sfc","smc","gb","gbc","sgb","gba","nds","3ds","cci","cxi","3dsx","cia",
    "z64","n64","v64","a26","a52","a78","atr","xfd","atx","xex","car","cas","com",
    "lnx","j64","jag","md","gen","smd","68k","sgd","sms","gg","pce","sgx","col",
    "int","cso","pbp","vpk","wbfs","wad","dol","wud","wux","rpx","wua","nsp","xci","nsz","xcz",
    "xbe","xiso","gcm","gcz","rvz","ciso","iso","cue","chd","gdi","cdi","img","mdf","pkg",
    "bin","exe","odyssey"
};

bool DownloadManager::isPcSystem(const std::string& system) {
    std::string normalized = system;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    return normalized == "pc" || normalized == "pc games";
}

bool DownloadManager::ensurePcCue(const std::string& path) {
    fs::path binPath(path);
    std::string ext = binPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".bin" || !fs::is_regular_file(binPath)) return true;

    fs::path cuePath = binPath;
    cuePath.replace_extension(".cue");
    if (fs::exists(cuePath)) return fs::is_regular_file(cuePath);

    std::ofstream cue(cuePath, std::ios::trunc);
    if (!cue) {
        std::cerr << "[DL] Could not create PC cue descriptor: " << cuePath << "\n";
        return false;
    }
    cue << "FILE \"" << binPath.filename().string() << "\" BINARY\n"
        << "  TRACK 01 MODE1/2352\n"
        << "    INDEX 01 00:00:00\n";
    if (!cue) {
        std::cerr << "[DL] Could not write PC cue descriptor: " << cuePath << "\n";
        return false;
    }
    std::cout << "[DL] Created PC cue descriptor: " << cuePath << "\n";
    return true;
}

static std::string shellQuote(const std::string& s) {
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\"'\"'";
        else q += c;
    }
    return q + "'";
}

static std::string cookieOption(const std::string& cookieFile,
                                const std::string& runtimeCookieFile) {
    std::string options;
    if (!cookieFile.empty()) {
        options += " --cookie " + shellQuote(cookieFile);
    }
    if (!runtimeCookieFile.empty()) {
        options += " --cookie-jar " + shellQuote(runtimeCookieFile);
    }
    return options;
}

// ─── URL helpers ─────────────────────────────────────────────────────────────
static std::string urlDecode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            isxdigit((unsigned char)s[i+1]) && isxdigit((unsigned char)s[i+2])) {
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

static std::string urlEncode(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            oss << c;
        else
            oss << '%' << std::uppercase << std::setw(2) << std::setfill('0')
                << std::hex << static_cast<int>(c);
    }
    return oss.str();
}

// ─── Archive.org fallback ─────────────────────────────────────────────────────
// If an archive.org download URL returns 404, attempt to recover a working URL.
// Case 1 — /download/IDENTIFIER/FILE : file was renamed; query metadata API.
// Case 2 — /download/FILENAME (no identifier) : use archive.org search to find
//           an item, then query its metadata for the ROM file.
// Returns "" if unresolvable.
static std::string archiveOrgFixUrl(const std::string& url,
                                    const std::string& cookieFile,
                                    const std::string& runtimeCookieFile) {
    static const std::set<std::string> kAllowed(kRomExts.begin(), kRomExts.end());
    static const std::set<std::string> kArchives = {"zip","7z","gz","bz2","xz","rar"};

    // Run a curl GET and return stdout.
    auto curlGet = [&](const std::string& curlUrl) -> std::string {
        std::string cmd = "curl --silent --fail --location --max-time 15"
                          " --user-agent 'Mozilla/5.0'" +
                          cookieOption(cookieFile, runtimeCookieFile) +
                          " " + shellQuote(curlUrl);
        FILE* f = popen(cmd.c_str(), "r");
        if (!f) return "";
        std::ostringstream ss;
        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) ss << buf;
        pclose(f);
        return ss.str();
    };

    // Scan archive.org metadata JSON for the first ROM or archive filename.
    auto firstRomFile = [&](const std::string& metaJson) -> std::string {
        std::regex nameRe(R"x("name"\s*:\s*"([^"]+)")x");
        for (auto it = std::sregex_iterator(metaJson.begin(), metaJson.end(), nameRe);
             it != std::sregex_iterator(); ++it) {
            std::string name = (*it)[1].str();
            std::string ext = fs::path(name).extension().string();
            if (ext.size() > 1) ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (kAllowed.count(ext) || kArchives.count(ext))
                return name;
        }
        return "";
    };

    // Build a corrected archive.org download URL from identifier + filename.
    auto makeUrl = [](const std::string& id, const std::string& file) -> std::string {
        std::string encoded;
        for (unsigned char c : file) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' ||
                c == '(' || c == ')' || c == ',' || c == '\'')
                encoded += static_cast<char>(c);
            else if (c == ' ')
                encoded += "%20";
            else {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%%%02X", c);
                encoded += buf;
            }
        }
        return "https://archive.org/download/" + id + "/" + encoded;
    };

    // ── Case 1: /download/IDENTIFIER/FILENAME ─────────────────────────────────
    {
        std::regex re(R"(https?://archive\.org/download/([^/?#]+)/(.+))");
        std::smatch m;
        if (std::regex_match(url, m, re)) {
            std::string json = curlGet("https://archive.org/metadata/" + m[1].str());
            if (json.empty()) return "";
            std::string file = firstRomFile(json);
            if (file.empty()) return "";
            std::string newUrl = makeUrl(m[1].str(), file);
            std::cout << "[DL] archive.org fallback (renamed): " << newUrl << "\n";
            return newUrl;
        }
    }

    // ── Case 2: /download/FILENAME (no identifier) ────────────────────────────
    {
        std::regex re(R"(https?://archive\.org/download/([^/?#]+))");
        std::smatch m;
        if (!std::regex_match(url, m, re)) return "";

        // rawSeg is still percent-encoded, e.g. "10-Yard%20Fight%20%28USA%2C%20Europe%29.zip"
        std::string rawSeg   = m[1].str();
        std::string filename = urlDecode(rawSeg);                       // "10-Yard Fight (USA, Europe).zip"
        std::string gameName = fs::path(filename).stem().string();      // "10-Yard Fight (USA, Europe)"

        // Percent-encode a filename for use in the URL path segment.
        auto encodeFile = [](const std::string& s) -> std::string {
            std::string enc;
            for (unsigned char c : s) {
                if (isalnum(c) || c == '-' || c == '_' || c == '.' ||
                    c == '(' || c == ')' || c == ',' || c == '\'')
                    enc += static_cast<char>(c);
                else if (c == ' ')
                    enc += "%20";
                else {
                    char buf[4];
                    std::snprintf(buf, sizeof(buf), "%%%02X", c);
                    enc += buf;
                }
            }
            return enc;
        };

        // Strip parenthesized tags to get a core title for matching.
        auto stripTags = [](const std::string& s) -> std::string {
            std::string r = std::regex_replace(s, std::regex(R"(\s*\([^)]*\))"), "");
            size_t f = r.find_first_not_of(" \t");
            if (f == std::string::npos) return "";
            size_t e = r.find_last_not_of(" \t");
            return r.substr(f, e - f + 1);
        };
        std::string coreTarget = stripTags(gameName);
        std::string coreTargetLower = coreTarget;
        std::transform(coreTargetLower.begin(), coreTargetLower.end(),
                       coreTargetLower.begin(), ::tolower);

        // Collect all ROM/archive files from a metadata JSON blob.
        auto romFilesIn = [&](const std::string& metaJson) -> std::vector<std::string> {
            std::regex nameRe(R"x("name"\s*:\s*"([^"]+)")x");
            std::vector<std::string> out;
            for (auto it = std::sregex_iterator(metaJson.begin(), metaJson.end(), nameRe);
                 it != std::sregex_iterator(); ++it) {
                std::string name = (*it)[1].str();
                std::string ext = fs::path(name).extension().string();
                if (ext.size() > 1) ext = ext.substr(1);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (kAllowed.count(ext) || kArchives.count(ext))
                    out.push_back(name);
            }
            return out;
        };

        // Pick the candidate whose core name best matches coreTarget.
        auto bestMatch = [&](const std::vector<std::string>& files) -> std::string {
            for (const auto& f : files) {
                std::string core = stripTags(fs::path(f).stem().string());
                std::transform(core.begin(), core.end(), core.begin(), ::tolower);
                if (core.find(coreTargetLower) != std::string::npos ||
                    coreTargetLower.find(core) != std::string::npos)
                    return f;
            }
            return files.empty() ? "" : files.front();
        };

        // ── Step 1: Direct derivation ─────────────────────────────────────────
        // Many archive.org items use the No-Intro filename-stem as their
        // identifier, so try metadata/ENCODED_STEM before touching the search API.
        {
            // Strip the extension from the already-encoded segment.
            std::string encodedStem = rawSeg;
            auto dotPos = encodedStem.rfind('.');
            if (dotPos != std::string::npos)
                encodedStem = encodedStem.substr(0, dotPos);

            std::string metaJson = curlGet("https://archive.org/metadata/" + encodedStem);
            // archive.org returns {"error":"..."} when the item doesn't exist.
            if (!metaJson.empty() && metaJson.find("\"error\"") == std::string::npos) {
                auto candidates = romFilesIn(metaJson);
                if (!candidates.empty()) {
                    std::string file = bestMatch(candidates);
                    if (!file.empty()) {
                        std::string newUrl = "https://archive.org/download/" +
                                             encodedStem + "/" + encodeFile(file);
                        std::cout << "[DL] archive.org fallback (direct): " << newUrl << "\n";
                        return newUrl;
                    }
                }
            }
        }

        // ── Step 2: Filename search ───────────────────────────────────────────
        // Direct derivation failed. The URL already encodes the exact filename
        // we want, so ask archive.org which item hosts that specific file —
        // far more precise than a title or keyword search.
        std::cout << "[DL] archive.org: searching for file: " << filename << "\n";
        std::string searchJson = curlGet(
            "https://archive.org/advancedsearch.php?q=" +
            urlEncode("filename:\"" + filename + "\"") +
            "&fl[]=identifier&rows=5&output=json");
        if (searchJson.empty()) return "";

        std::regex idRe(R"x("identifier"\s*:\s*"([^"]+)")x");
        std::smatch idm;
        if (!std::regex_search(searchJson, idm, idRe)) return "";
        std::string identifier = idm[1].str();
        std::cout << "[DL] archive.org found identifier: " << identifier << "\n";

        // Reconstruct the URL using the original percent-encoded filename.
        std::string newUrl = "https://archive.org/download/" + identifier + "/" + rawSeg;
        std::cout << "[DL] archive.org fallback (filename search): " << newUrl << "\n";
        return newUrl;
    }
}


static std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c == '\t') r += "\\t";
        else r += c;
    }
    return r;
}

// Read a string value for a given key from a flat JSON object
static std::string jsonGetStr(const std::string& obj, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch m;
    if (std::regex_search(obj, m, re)) {
        std::string v = m[1].str();
        // unescape basic sequences
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '\\' && i+1 < v.size()) {
                ++i;
                switch (v[i]) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    default:   out += v[i]; break;
                }
            } else {
                out += v[i];
            }
        }
        return out;
    }
    return "";
}

static long long jsonGetLL(const std::string& obj, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch m;
    if (std::regex_search(obj, m, re)) {
        try { return std::stoll(m[1].str()); } catch (...) {}
    }
    return 0;
}

static std::string isoTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static fs::path uniqueDestinationPath(const fs::path& directory,
                                      const fs::path& filename) {
    fs::path candidate = directory / filename;
    if (!fs::exists(candidate)) return candidate;

    const std::string stem = filename.stem().string();
    const std::string extension = filename.extension().string();
    for (unsigned int suffix = 2; ; ++suffix) {
        candidate = directory / (stem + " (" + std::to_string(suffix) + ")" + extension);
        if (!fs::exists(candidate)) return candidate;
    }
}

static bool isSafeArchiveMember(const std::string& member) {
    const fs::path memberPath(member);
    if (memberPath.is_absolute() || memberPath.has_root_name() ||
        memberPath.has_root_directory())
        return false;
    for (const auto& component : memberPath)
        if (component == "..") return false;
    return true;
}

static std::string createTempFile(const fs::path& directory,
                                  const std::string& suffix) {
    std::string pattern = (directory / "_omnicon_XXXXXX").string();
    std::vector<char> writablePattern(pattern.begin(), pattern.end());
    writablePattern.push_back('\0');
    const int fd = mkstemp(writablePattern.data());
    if (fd < 0) return "";
    close(fd);

    std::string tempPath(writablePattern.data());
    if (suffix.empty()) return tempPath;

    const std::string suffixedPath = tempPath + suffix;
    if (rename(tempPath.c_str(), suffixedPath.c_str()) != 0) {
        unlink(tempPath.c_str());
        return "";
    }
    return suffixedPath;
}

static std::string createTempDirectory(const fs::path& directory) {
    std::string pattern = (directory / "_omnicon_extract_XXXXXX").string();
    std::vector<char> writablePattern(pattern.begin(), pattern.end());
    writablePattern.push_back('\0');
    char* createdPath = mkdtemp(writablePattern.data());
    return createdPath ? std::string(createdPath) : "";
}

static bool isWiiUSystem(const std::string& system) {
    return GameLibrary::systemFromName(system) == SystemType::WiiU;
}

static fs::path downloadDestinationDirectory(const std::string& romsDir,
                                             const std::string& system) {
    const SystemType systemType = GameLibrary::systemFromName(system);
    if (systemType == SystemType::Unknown || systemType == SystemType::Steam)
        return romsDir;

    const fs::path directory = fs::path(romsDir) /
                               GameLibrary::systemDirName(systemType);
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        std::cerr << "[DL] Could not create download directory " << directory
                  << ": " << error.message() << "\n";
        return {};
    }
    return directory;
}

// ─── Thread-pool helpers ───────────────────────────────────────────────────────────
namespace {
struct Semaphore {
    explicit Semaphore(int n) : count_(n) {}
    void acquire() {
        std::unique_lock<std::mutex> lk(mx_);
        cv_.wait(lk, [this]{ return count_ > 0; });
        --count_;
    }
    void release() {
        std::lock_guard<std::mutex> lk(mx_);
        ++count_;
        cv_.notify_one();
    }
private:
    std::mutex mx_;
    std::condition_variable cv_;
    int count_;
};
} // namespace
// ─── Singleton ───────────────────────────────────────────────────────────────

DownloadManager& DownloadManager::instance() {
    static DownloadManager inst;
    return inst;
}

void DownloadManager::init(const std::string& queuePath, const std::string& romsDir) {
    queuePath_     = queuePath;
    romsDir_       = romsDir;
    const char* configuredCookieFile = std::getenv("OMNICON_COOKIE_FILE");
    cookieFile_ = (configuredCookieFile && *configuredCookieFile)
        ? configuredCookieFile
        : (fs::path(queuePath).parent_path() / "archive_cookies.txt").string();
    runtimeCookieFile_ = (fs::path(queuePath).parent_path() /
                          "archive_cookies.runtime.txt").string();

    // downloaded.json lives next to the queue file
    fs::path qp(queuePath);
    downloadedPath_ = (qp.parent_path() / "downloaded.json").string();

    if (!qp.parent_path().empty())
        fs::create_directories(qp.parent_path());
    fs::create_directories(romsDir_);
    loadFromDisk();
}

// ─── Persistence ─────────────────────────────────────────────────────────────

void DownloadManager::loadFromDisk() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Load queue
    {
        std::ifstream f(queuePath_);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            std::string json = ss.str();
            // Parse array of objects with regex
            std::regex objRe(R"(\{[^{}]+\})");
            for (auto it = std::sregex_iterator(json.begin(), json.end(), objRe);
                 it != std::sregex_iterator(); ++it) {
                std::string obj = (*it).str();
                QueuedDownload qd;
                qd.id        = static_cast<int>(jsonGetLL(obj, "id"));
                qd.name      = jsonGetStr(obj, "name");
                qd.url       = jsonGetStr(obj, "url");
                qd.system    = jsonGetStr(obj, "system");
                qd.sizeBytes = jsonGetLL(obj, "sizeBytes");
                qd.queuedAt  = jsonGetStr(obj, "queuedAt");
                if (!qd.url.empty()) queue_.push_back(std::move(qd));
            }
        }
    }

    // Load downloaded map
    {
        std::ifstream f(downloadedPath_);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            std::string json = ss.str();
            std::regex pairRe(R"x("([^"]+)"\s*:\s*"([^"]*)")x");            for (auto it = std::sregex_iterator(json.begin(), json.end(), pairRe);
                 it != std::sregex_iterator(); ++it) {
                std::string url  = (*it)[1].str();
                std::string path = (*it)[2].str();
                if (url.empty()) continue;
                urlToLocal_[url] = path;
                std::string urlLower = url;
                std::transform(urlLower.begin(), urlLower.end(), urlLower.begin(), ::tolower);
                if (urlLower.find("pc_redump") != std::string::npos &&
                    !ensurePcCue(path)) {
                    std::cerr << "[DL] Existing PC download needs a valid cue descriptor: "
                              << path << "\n";
                }
            }
        }
    }
}

void DownloadManager::saveToDisk() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (queuePath_.empty()) return;
    // Save queue
    {
        std::ofstream f(queuePath_);
        f << "[\n";
        for (size_t i = 0; i < queue_.size(); ++i) {
            const auto& q = queue_[i];
            f << "  {\"id\":" << q.id
              << ",\"name\":\"" << jsonEscape(q.name) << "\""
              << ",\"url\":\"" << jsonEscape(q.url) << "\""
              << ",\"system\":\"" << jsonEscape(q.system) << "\""
              << ",\"sizeBytes\":" << q.sizeBytes
              << ",\"queuedAt\":\"" << jsonEscape(q.queuedAt) << "\"}";
            if (i + 1 < queue_.size()) f << ",";
            f << "\n";
        }
        f << "]\n";
    }
    // Save downloaded map
    {
        std::ofstream f(downloadedPath_);
        f << "{\n";
        bool first = true;
        for (const auto& kv : urlToLocal_) {
            if (!first) f << ",\n";
            first = false;
            f << "  \"" << jsonEscape(kv.first) << "\": \"" << jsonEscape(kv.second) << "\"";
        }
        f << "\n}\n";
    }
}

void DownloadManager::flush() {
    saveToDisk();
}

// ─── Public API ──────────────────────────────────────────────────────────────

std::string DownloadManager::localPath(const std::string& url) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = urlToLocal_.find(url);
    return (it != urlToLocal_.end()) ? it->second : "";
}

bool DownloadManager::isDownloaded(const std::string& url) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = urlToLocal_.find(url);
    if (it == urlToLocal_.end()) return false;
    return !it->second.empty() && fs::exists(it->second);
}

void DownloadManager::enqueue(const QueuedDownload& entry) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (entry.url.empty()) return;
    // Skip if already downloaded
    if (isDownloaded(entry.url)) return;
    // Skip if already in queue
    for (const auto& q : queue_)
        if (q.url == entry.url) return;

    QueuedDownload qd = entry;
    if (qd.queuedAt.empty()) qd.queuedAt = isoTimestamp();
    queue_.push_back(std::move(qd));
    saveToDisk();
}

void DownloadManager::dequeue(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Delete the local file if one exists
    auto it = urlToLocal_.find(url);
    if (it != urlToLocal_.end() && !it->second.empty()) {
        const std::string localFile = it->second;
        std::error_code ec;
        fs::remove(localFile, ec);
        if (!ec)
            std::cout << "[DL] Deleted local file: " << localFile << "\n";
        for (auto mapIt = urlToLocal_.begin(); mapIt != urlToLocal_.end();) {
            if (mapIt->second == localFile)
                mapIt = urlToLocal_.erase(mapIt);
            else
                ++mapIt;
        }
    }

    // Remove from the pending queue
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
        [&](const QueuedDownload& q){ return q.url == url; }), queue_.end());

    saveToDisk();
}

// ─── Storage management ───────────────────────────────────────────────────────

long long DownloadManager::usedStorageBytes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (romsDir_.empty() || !fs::exists(romsDir_)) return 0;
    long long total = 0;
    std::error_code ec;
    for (const auto& de : fs::recursive_directory_iterator(romsDir_, ec))
        if (!de.is_directory(ec))
            total += static_cast<long long>(de.file_size(ec));
    return total;
}

bool DownloadManager::ensureSpace(long long neededBytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    long long used = usedStorageBytes();
    if (used + neededBytes <= kMaxStorageBytes) return true;

    // Collect downloaded entries sorted by queuedAt (oldest first)
    struct CandidateFile {
        std::string url;
        std::string path;
        std::string queuedAt;
    };
    std::vector<CandidateFile> candidates;
    for (const auto& kv : urlToLocal_) {
        if (kv.second.empty() || !fs::exists(kv.second)) continue;
        // Find queuedAt in queue_
        std::string qa;
        for (const auto& q : queue_)
            if (q.url == kv.first) { qa = q.queuedAt; break; }
        candidates.push_back({kv.first, kv.second, qa});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const CandidateFile& a, const CandidateFile& b){ return a.queuedAt < b.queuedAt; });

    for (const auto& c : candidates) {
        if (used + neededBytes <= kMaxStorageBytes) break;
        std::error_code ec;
        long long fileSize = static_cast<long long>(fs::file_size(c.path, ec));
        if (!ec) {
            fs::remove(c.path, ec);
            if (!ec) {
                used -= fileSize;
                for (auto mapIt = urlToLocal_.begin(); mapIt != urlToLocal_.end();) {
                    if (mapIt->second == c.path)
                        mapIt = urlToLocal_.erase(mapIt);
                    else
                        ++mapIt;
                }
                // Do NOT remove from queue_ — the game entry stays in the library
                // and can be re-downloaded when selected again.
                std::cout << "[DL] Evicted " << c.path << " to free space.\n";
            }
        }
    }
    saveToDisk();
    return (used + neededBytes <= kMaxStorageBytes);
}

// ─── Extraction ──────────────────────────────────────────────────────────────

std::string DownloadManager::extractRom(const std::string& archivePath,
                                         const std::string& destDir,
                                         bool allowGbaFallback,
                                         bool preferPcContent) {
    std::string arcExt = fs::path(archivePath).extension().string();
    std::transform(arcExt.begin(), arcExt.end(), arcExt.begin(), ::tolower);

    std::string foundFile;
    auto isRomCandidate = [&](const std::string& fname) {
        std::string ext = fs::path(fname).extension().string();
        if (!ext.empty()) ext = ext.substr(1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (preferPcContent) return ext == "bin" || ext == "exe";
        if (std::find(kRomExts.begin(), kRomExts.end(), ext) != kRomExts.end()) {
            return true;
        }
        if (extraSystemKnowsExtension(ext)) return true;
        return allowGbaFallback && (ext.empty() || ext == "bin");
    };

    if (arcExt == ".7z" || arcExt == ".rar" || arcExt == ".gz" || arcExt == ".bz2" || arcExt == ".xz") {
        // ── 7z branch ──────────────────────────────────────────────────────
        // Prefer CLI tools if available; fall back to Python py7zr when absent.
        std::string sevenZipCmd;
        if (system("7z i >/dev/null 2>&1") == 0) { // NOLINT
            sevenZipCmd = "7z";
        } else if (system("7zz i >/dev/null 2>&1") == 0) { // NOLINT
            sevenZipCmd = "7zz";
        } else if (system("7zr i >/dev/null 2>&1") == 0) { // NOLINT
            sevenZipCmd = "7zr";
        }

        if (!sevenZipCmd.empty()) {
            // "7z l -ba -slt" emits one block per entry; "Path = <name>" lines
            // contain the member filename without extra column formatting.
            std::string listCmd = sevenZipCmd + " l -ba -slt " +
                                  shellQuote(archivePath) + " 2>/dev/null";
            FILE* f = popen(listCmd.c_str(), "r");
            if (!f) return "";
            char buf[2048];
            while (fgets(buf, sizeof(buf), f)) {
                std::string line(buf);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                    line.pop_back();
                if (line.rfind("Path = ", 0) != 0) continue;
                std::string fname = line.substr(7);
                if (fname.empty() || fname.back() == '/') continue;
                if (isRomCandidate(fname)) foundFile = fname;
                if (!foundFile.empty()) break;
            }
            pclose(f);
            if (foundFile.empty()) return "";
            if (!isSafeArchiveMember(foundFile)) {
                std::cerr << "[DL] Refusing unsafe archive member path: " << foundFile << "\n";
                return "";
            }

            // -aoa = overwrite all without prompt; -o<dir> has no space in 7z syntax
            std::string extractCmd = sevenZipCmd + " e -aoa -o" +
                                     shellQuote(destDir) + " " + shellQuote(archivePath) +
                                     " " + shellQuote(foundFile) + " 2>/dev/null";
            if (system(extractCmd.c_str()) != 0) return ""; // NOLINT
        } else {
            std::string extList;
            for (size_t i = 0; i < kRomExts.size(); ++i) {
                if (i) extList += ',';
                extList += kRomExts[i];
            }

            std::string pyList =
                "python3 -c \""
                "import sys,os;"
                "import py7zr;"
                "z=py7zr.SevenZipFile(sys.argv[1],mode='r');"
                "exts={e for e in sys.argv[2].split(',')};"
                "names=[n for n in z.getnames() if not n.endswith('/') and "
                "((sys.argv[4]=='1' and os.path.splitext(n)[1].lstrip('.').lower() in ('bin','exe')) or "
                "(sys.argv[4]!='1' and (os.path.splitext(n)[1].lstrip('.').lower() in exts or "
                "(sys.argv[3]=='1' and os.path.splitext(n)[1].lstrip('.').lower() in ('','bin')))))];"
                "z.close();"
                "print(names[0] if names else '')\" " + shellQuote(archivePath) + " '" + extList +
                "' '" + (allowGbaFallback ? "1" : "0") + "' '" +
                (preferPcContent ? "1" : "0") + "' 2>/dev/null";
            FILE* f = popen(pyList.c_str(), "r");
            if (!f) return "";
            char buf[2048] = {};
            if (fgets(buf, sizeof(buf), f)) {
                foundFile = buf;
                while (!foundFile.empty() &&
                       (foundFile.back() == '\n' || foundFile.back() == '\r'))
                    foundFile.pop_back();
            }
            pclose(f);
            if (foundFile.empty()) return "";
            if (!isSafeArchiveMember(foundFile)) {
                std::cerr << "[DL] Refusing unsafe archive member path: " << foundFile << "\n";
                return "";
            }

            std::string pyExtract =
                "python3 -c \""
                "import sys;"
                "import py7zr;"
                "z=py7zr.SevenZipFile(sys.argv[1],mode='r');"
                "z.extract(path=sys.argv[3], targets=[sys.argv[2]]);"
                "z.close()\" " + shellQuote(archivePath) + " " + shellQuote(foundFile) +
                " " + shellQuote(destDir) + " 2>/dev/null";
            if (system(pyExtract.c_str()) != 0) return ""; // NOLINT
        }
    } else {
        // ── zip branch (default) ────────────────────────────────────────────
        // Prefer unzip (fast C tool); fall back to Python zipfile if absent.
        bool haveUnzip = (system("unzip -v >/dev/null 2>&1") == 0); // NOLINT

        if (haveUnzip) {
            // Use "unzip -Z1" (zipinfo short format) to get one clean filename
            // per line, avoiding column-based parsing that breaks on names with spaces.
            std::string listCmd = "unzip -Z1 " + shellQuote(archivePath) + " 2>/dev/null";
            FILE* f = popen(listCmd.c_str(), "r");
            if (!f) return "";
            char buf[2048];
            while (fgets(buf, sizeof(buf), f)) {
                std::string fname(buf);
                while (!fname.empty() && (fname.back() == '\n' || fname.back() == '\r'))
                    fname.pop_back();
                if (fname.empty() || fname.back() == '/') continue;
                if (isRomCandidate(fname)) foundFile = fname;
                if (!foundFile.empty()) break;
            }
            pclose(f);
            if (foundFile.empty()) return "";
            if (!isSafeArchiveMember(foundFile)) {
                std::cerr << "[DL] Refusing unsafe archive member path: " << foundFile << "\n";
                return "";
            }

            std::string extractCmd = "unzip -j -o " + shellQuote(archivePath) + " " +
                                     shellQuote(foundFile) + " -d " + shellQuote(destDir) +
                                     " 2>/dev/null";
            if (system(extractCmd.c_str()) != 0) return ""; // NOLINT
        } else {
            // ── Python zipfile fallback ───────────────────────────────────────
            // Build a comma-separated list of allowed extensions for the script.
            std::string extList;
            for (size_t i = 0; i < kRomExts.size(); ++i) {
                if (i) extList += ',';
                extList += kRomExts[i];
            }
            // List zip members and pick the first ROM file.
            std::string pyList =
                "python3 -c \""
                "import zipfile,sys,os;"
                "z=zipfile.ZipFile(sys.argv[1]);"
                "exts={e for e in sys.argv[2].split(',')};"
                "names=[n for n in z.namelist() if not n.endswith('/') and "
                "((sys.argv[4]=='1' and os.path.splitext(n)[1].lstrip('.').lower() in ('bin','exe')) or "
                "(sys.argv[4]!='1' and (os.path.splitext(n)[1].lstrip('.').lower() in exts or "
                "(sys.argv[3]=='1' and os.path.splitext(n)[1].lstrip('.').lower() in ('','bin')))))];"
                "print(names[0] if names else '')\" " + shellQuote(archivePath) + " '" + extList +
                "' '" + (allowGbaFallback ? "1" : "0") + "' '" +
                (preferPcContent ? "1" : "0") + "' 2>/dev/null";
            FILE* f = popen(pyList.c_str(), "r");
            if (!f) return "";
            char buf[2048] = {};
            if (fgets(buf, sizeof(buf), f)) {
                foundFile = buf;
                while (!foundFile.empty() &&
                       (foundFile.back() == '\n' || foundFile.back() == '\r'))
                    foundFile.pop_back();
            }
            pclose(f);
            if (foundFile.empty()) return "";
            if (!isSafeArchiveMember(foundFile)) {
                std::cerr << "[DL] Refusing unsafe archive member path: " << foundFile << "\n";
                return "";
            }

            std::string pyExtract =
                "python3 -c \""
                "import zipfile,sys;"
                "z=zipfile.ZipFile(sys.argv[1]);"
                "z.extract(sys.argv[2],sys.argv[3])\" " + shellQuote(archivePath) +
                " " + shellQuote(foundFile) + " " + shellQuote(destDir) + " 2>/dev/null";
            if (system(pyExtract.c_str()) != 0) return ""; // NOLINT
        }
    }

    fs::path extractedPath = fs::path(destDir) / fs::path(foundFile).filename();
    if (!fs::is_regular_file(extractedPath)) {
        const fs::path nestedPath = fs::path(destDir) / fs::path(foundFile);
        if (fs::is_regular_file(nestedPath)) extractedPath = nestedPath;
    }
    if (!fs::is_regular_file(extractedPath)) {
        std::cerr << "[DL] Archive extraction completed but ROM was not found at "
                  << extractedPath << "\n";
        return "";
    }
    return extractedPath.string();
}

// ─── Download ────────────────────────────────────────────────────────────────

std::string DownloadManager::downloadBlocking(
        const QueuedDownload& entry,
        std::function<void(long long, long long)> progress,
        std::string* errorOut,
        std::string* resolvedUrlOut) {

    if (entry.url.empty()) return "";

    // ── Locked: check state, reserve space, build temp path ───────────────────
    std::string tmpPath;
    long long   spaceNeeded;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        // Already downloaded?
        if (isDownloaded(entry.url)) return urlToLocal_[entry.url];

        // Ensure destination directory exists
        if (!fs::exists(romsDir_)) fs::create_directories(romsDir_);

        // Build safe temp file path.
        // Strip query/fragment before extracting filename so extensions survive.
        std::string cleanUrl = entry.url;
        auto qpos = cleanUrl.find_first_of("?#");
        if (qpos != std::string::npos) cleanUrl.resize(qpos);
        std::string safeName;
        for (char c : fs::path(cleanUrl).filename().string()) {
            safeName += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') ? c : '_';
        }
        if (safeName.empty()) safeName = "download";
        // Reserve space (use sizeBytes hint if available, else 500 MB estimate)
        spaceNeeded = (entry.sizeBytes > 0) ? entry.sizeBytes : 500LL * 1024 * 1024;
        if (!ensureSpace(spaceNeeded)) {
            std::cerr << "[DL] Not enough disk space for " << entry.name << ".\n";
            return "";
        }

        const std::string extension = fs::path(safeName).extension().string();
        tmpPath = createTempFile(romsDir_, extension);
        if (tmpPath.empty()) {
            const std::string message = "Could not create a temporary download file in " + romsDir_;
            std::cerr << "[DL] " << message << "\n";
            if (errorOut) *errorOut = message;
            return "";
        }
    }

    // ── UNLOCKED: perform network download ───────────────────────────────

    // Build curl command — progress written to a temp file we poll
    char progressTmp[] = "/tmp/omnicon_curl_prog_XXXXXX";
    int pfd = mkstemp(progressTmp);
    if (pfd >= 0) close(pfd);

    // Derive the URL origin for use as a Referer (satisfies hotlink-protection checks).
    std::string referer = entry.url;
    {
        auto p = referer.find("://");
        if (p != std::string::npos) {
            auto s = referer.find('/', p + 3);
            referer = (s != std::string::npos) ? referer.substr(0, s + 1) : referer + "/";
        }
    }

    std::string downloadUrlUsed = entry.url;
    std::string curlCmd = "curl --location --fail --silent --show-error"
                          " --user-agent 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"
                          " (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36'"
                          + cookieOption(cookieFile_, runtimeCookieFile_) +
                          " --referer " + shellQuote(referer) +
                          " --connect-timeout 30 --speed-limit 1024 --speed-time 90"
                          " --retry 3 --retry-delay 2"
                          " -o " + shellQuote(tmpPath) +
                          " " + shellQuote(entry.url) +
                          " 2>" + std::string(progressTmp);

    // The database size is often missing or wrong (large Wii U archives are
    // several GB), which pinned the bar at 99%. Ask the server for the real size.
    {
        std::string headCmd = "curl --location --silent --head --max-time 20"
                              " --user-agent 'Mozilla/5.0 (X11; Linux x86_64)'"
                              + cookieOption(cookieFile_, runtimeCookieFile_) +
                              " " + shellQuote(entry.url) + " 2>/dev/null";
        if (FILE* hp = popen(headCmd.c_str(), "r")) {
            char line[1024];
            long long realSize = 0;
            while (fgets(line, sizeof line, hp)) {
                std::string l(line);
                for (auto& ch : l) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (l.rfind("content-length:", 0) == 0)
                    realSize = std::atoll(l.c_str() + 15); // last hop wins after redirects
            }
            pclose(hp);
            if (realSize > 0) spaceNeeded = realSize;
        }
    }

    if (progress) progress(0, spaceNeeded);

    // Run curl in a background thread so we can poll the partial file size
    // and report real download progress on every iteration.
    std::atomic<bool> curlDone{false};
    std::atomic<int>  curlExit{0};
    std::thread curlThread([&]() {
        curlExit.store(system(curlCmd.c_str())); // NOLINT
        curlDone.store(true);
    });

    if (progress) {
        while (!curlDone.load()) {
            std::error_code fsec;
            long long current = 0;
            if (fs::exists(tmpPath, fsec))
                current = static_cast<long long>(fs::file_size(tmpPath, fsec));
            // The database size is only a hint. Do not display 100% until curl
            // has exited successfully, even if the actual file is larger.
            if (spaceNeeded > 0 && current >= spaceNeeded)
                current = spaceNeeded - 1;
            progress(current, spaceNeeded);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }
    curlThread.join();
    int rc = curlExit.load();

    // Read curl's error output before deleting the temp file
    std::string curlError;
    if (pfd >= 0) {
        std::ifstream ef(progressTmp);
        if (ef) {
            std::ostringstream es; es << ef.rdbuf();
            curlError = es.str();
        }
        unlink(progressTmp);
    }

    if (rc != 0) {
        std::cerr << "[DL] curl failed for " << entry.url << " (exit " << rc << ")";
        if (!curlError.empty()) std::cerr << "\n[DL] curl error: " << curlError;
        std::cerr << "\n";

        // For archive.org URLs, try metadata-based recovery on any curl failure,
        // not just 404 (some mirrors fail with other status/error text).
        bool fromArchiveOrg = (entry.url.find("archive.org") != std::string::npos);
        if (fromArchiveOrg) {
            std::string altUrl = archiveOrgFixUrl(entry.url, cookieFile_, runtimeCookieFile_);
            if (!altUrl.empty() && altUrl != entry.url) {
                std::cout << "[DL] Retrying with corrected URL...\n";
                // Re-use the same tmpPath; rebuild the command with the new URL
                std::string altReferer = altUrl;
                {
                    auto p = altReferer.find("://");
                    if (p != std::string::npos) {
                        auto s = altReferer.find('/', p + 3);
                        altReferer = (s != std::string::npos)
                                     ? altReferer.substr(0, s + 1) : altReferer + "/";
                    }
                }
                char p2tmp[] = "/tmp/omnicon_curl_prog_XXXXXX";
                int p2fd = mkstemp(p2tmp); if (p2fd >= 0) close(p2fd);
                std::string altCmd = "curl --location --fail --silent --show-error"
                                     " --user-agent 'Mozilla/5.0 (X11; Linux x86_64)"
                                     " AppleWebKit/537.36 (KHTML, like Gecko)"
                                     " Chrome/124.0.0.0 Safari/537.36'"
                                     + cookieOption(cookieFile_, runtimeCookieFile_) +
                                     " --referer " + shellQuote(altReferer) +
                                     " --connect-timeout 30 --speed-limit 1024 --speed-time 90"
                                     " --retry 2 --retry-delay 2"
                                     " -o " + shellQuote(tmpPath) +
                                     " " + shellQuote(altUrl) +
                                     " 2>" + std::string(p2tmp);
                int rc2 = system(altCmd.c_str()); // NOLINT
                std::string altError;
                if (p2fd >= 0) {
                    std::ifstream ef2(p2tmp);
                    if (ef2) { std::ostringstream es; es << ef2.rdbuf(); altError = es.str(); }
                    unlink(p2tmp);
                }
                if (rc2 == 0) {
                    // Fallback succeeded — clear error state and let the
                    // extraction logic below handle the downloaded file.
                    rc = 0;
                    curlError.clear();
                    downloadUrlUsed = altUrl;
                    if (resolvedUrlOut) *resolvedUrlOut = altUrl;
                    goto download_succeeded; // NOLINT
                } else {
                    std::cerr << "[DL] Fallback also failed";
                    if (!altError.empty()) std::cerr << ": " << altError;
                    std::cerr << "\n";
                }
            }
        }

        if (errorOut) *errorOut = curlError;
        unlink(tmpPath.c_str());
        return "";
    }
    download_succeeded:

    if (progress) progress(spaceNeeded, spaceNeeded);

    // Determine if it's an archive
    std::string successfulUrlPath = downloadUrlUsed;
    const auto queryPos = successfulUrlPath.find_first_of("?#");
    if (queryPos != std::string::npos) successfulUrlPath.resize(queryPos);
    std::string ext = fs::path(successfulUrlPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string tempExt = fs::path(tmpPath).extension().string();
    std::transform(tempExt.begin(), tempExt.end(), tempExt.begin(), ::tolower);
    if (!ext.empty() && ext != tempExt) {
        fs::path renamedTemp(tmpPath);
        renamedTemp.replace_extension(ext);
        std::error_code renameError;
        fs::rename(tmpPath, renamedTemp, renameError);
        if (renameError) {
            unlink(tmpPath.c_str());
            const std::string message =
                "Could not prepare downloaded file for extraction: " + renameError.message();
            std::cerr << "[DL] " << message << "\n";
            if (errorOut) *errorOut = message;
            return "";
        }
        tmpPath = renamedTemp.string();
    }

    std::string finalPath;
    // Wii U NUS archives hold no ROM file; keep them intact so the launcher can
    // convert them for Cemu on first run. MAME romsets must also stay zipped.
    const SystemType entrySystem = GameLibrary::systemFromName(entry.system);
    if (!isWiiUSystem(entry.system) &&
        !(ext == ".zip" && extraSystemKeepsArchive(entrySystem)) &&
        (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".gz" || ext == ".bz2" || ext == ".xz")) {
        std::cout << "[DL] Extracting " << tmpPath << "...\n";
        const std::string stagingDir = createTempDirectory(romsDir_);
        if (stagingDir.empty()) {
            unlink(tmpPath.c_str());
            const std::string message = "Could not create a temporary extraction directory in " + romsDir_;
            std::cerr << "[DL] " << message << "\n";
            if (errorOut) *errorOut = message;
            return "";
        }
        std::string extractedPath;
        if (progress) {
            std::atomic<bool> extractionDone{false};
            std::thread extractionThread([&]() {
                extractedPath = extractRom(
                    tmpPath, stagingDir, entry.system == "GBA", isPcSystem(entry.system));
                extractionDone.store(true);
            });
            while (!extractionDone.load()) {
                progress(0, -1);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
            extractionThread.join();
        } else {
            extractedPath = extractRom(
                tmpPath, stagingDir, entry.system == "GBA", isPcSystem(entry.system));
        }
        unlink(tmpPath.c_str());
        if (extractedPath.empty()) {
            std::error_code cleanupError;
            fs::remove_all(stagingDir, cleanupError);
            std::cerr << "[DL] No ROM found inside archive.\n";
            if (errorOut) {
                *errorOut = "Downloaded archive could not be extracted or contained no supported ROM.";
            }
            return "";
        }
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            const fs::path extracted(extractedPath);
            const fs::path destinationDirectory =
                downloadDestinationDirectory(romsDir_, entry.system);
            if (destinationDirectory.empty()) {
                std::error_code cleanupError;
                fs::remove_all(stagingDir, cleanupError);
                if (errorOut) *errorOut = "Could not create the ROM system directory.";
                return "";
            }
            const fs::path destination =
                uniqueDestinationPath(destinationDirectory, extracted.filename());
            std::error_code moveError;
            fs::rename(extracted, destination, moveError);
            if (moveError) {
                std::error_code cleanupError;
                fs::remove_all(stagingDir, cleanupError);
                const std::string message = "Failed to move extracted ROM: " + moveError.message();
                std::cerr << "[DL] " << message << "\n";
                if (errorOut) *errorOut = message;
                return "";
            }
            finalPath = destination.string();
        }
        std::error_code cleanupError;
        fs::remove_all(stagingDir, cleanupError);
    } else {
        std::string safeName;
        std::string downloadedName = fs::path(successfulUrlPath).filename().string();
        if (isWiiUSystem(entry.system)) downloadedName = urlDecode(downloadedName);
        for (char c : downloadedName) {
            safeName += (std::isalnum(static_cast<unsigned char>(c)) ||
                         c == '.' || c == '-' || c == '_' ||
                         (isWiiUSystem(entry.system) && (c == ' ' || c == '[' || c == ']'))) ? c : '_';
        }
        if (safeName.empty()) safeName = "download";
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            const fs::path destinationDirectory =
                downloadDestinationDirectory(romsDir_, entry.system);
            if (destinationDirectory.empty()) {
                unlink(tmpPath.c_str());
                if (errorOut) *errorOut = "Could not create the ROM system directory.";
                return "";
            }
            const fs::path destination = uniqueDestinationPath(destinationDirectory, safeName);
            std::error_code moveError;
            fs::rename(tmpPath, destination, moveError);
            if (moveError) {
                unlink(tmpPath.c_str());
                const std::string message = "Failed to move downloaded file: " + moveError.message();
                std::cerr << "[DL] " << message << "\n";
                if (errorOut) *errorOut = message;
                return "";
            }
            finalPath = destination.string();
        }
    }

    if (isPcSystem(entry.system) && !ensurePcCue(finalPath)) {
        std::error_code cleanupError;
        fs::remove(finalPath, cleanupError);
        if (errorOut) *errorOut = "PC disc image downloaded, but its cue descriptor could not be created.";
        return "";
    }

    // ── Locked: record result ────────────────────────────────────────────
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        urlToLocal_[entry.url] = finalPath;
        if (downloadUrlUsed != entry.url)
            urlToLocal_[downloadUrlUsed] = finalPath;
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
            [&](const QueuedDownload& q){ return q.url == entry.url; }), queue_.end());
        saveToDisk();
    }

    std::cout << "[DL] Saved: " << finalPath << "\n";
    return finalPath;
}

// ─── Batch download ──────────────────────────────────────────────────────────────────
std::map<std::string, std::string> DownloadManager::downloadBatch(
        const std::vector<QueuedDownload>& entries,
        int maxWorkers,
        std::function<void(const std::string&, long long, long long)> progress) {

    if (entries.empty()) return {};

    int slots = (maxWorkers > 0) ? maxWorkers : static_cast<int>(entries.size());
    Semaphore sem(slots);

    std::vector<std::future<std::pair<std::string, std::string>>> futures;
    futures.reserve(entries.size());

    for (const auto& entry : entries) {
        sem.acquire(); // block until a worker slot is free
        futures.push_back(std::async(std::launch::async,
            [this, entry, &sem, progress]() -> std::pair<std::string, std::string> {
                std::function<void(long long, long long)> prog;
                if (progress) {
                    prog = [&](long long r, long long t) { progress(entry.url, r, t); };
                }
                std::string path = downloadBlocking(entry, prog);
                sem.release();
                return {entry.url, path};
            }));
    }

    std::map<std::string, std::string> results;
    for (auto& f : futures) {
        auto kv = f.get();
        results[kv.first] = kv.second;
    }
    return results;
}
