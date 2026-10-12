#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct QueuedDownload {
    int         id = 0;        // Oracle DB row id (0 if not from DB)
    std::string name;
    std::string url;
    std::string system;
    long long   sizeBytes = 0;
    std::string queuedAt;      // ISO-8601 timestamp
};

// Manages a persistent download queue and the on-disk ROM cache.
// - Queue persisted to saves/download_queue.json
// - Downloaded URL→path map persisted to saves/downloaded.json
// - 20 GB cap; evicts oldest downloaded files when space is needed
class DownloadManager {
public:
    static DownloadManager& instance();

    static constexpr long long kMaxStorageBytes = 20LL * 1024 * 1024 * 1024; // 20 GB

    // Call once at startup.
    void init(const std::string& queuePath, const std::string& romsDir);

    // Add a game to the download queue (no-op if already queued/downloaded).
    void enqueue(const QueuedDownload& entry);

    // Remove a game from the queue and delete its local file (if any).
    // The library entry is left intact so the game can be re-downloaded later.
    void dequeue(const std::string& url);

    // Download a specific entry right now, blocking until complete.
    // progress(received, total) is called periodically (total=0 if unknown;
    // total < 0 while a downloaded archive is being extracted).
    // errorOut (optional) is set to the curl error message on failure.
    // resolvedUrlOut (optional) is set to the URL that actually succeeded;
    //   differs from entry.url when an archive.org fallback was used.
    // Returns local ROM path on success, "" on failure.
    std::string downloadBlocking(const QueuedDownload& entry,
                                 std::function<void(long long, long long)> progress = nullptr,
                                 std::string* errorOut = nullptr,
                                 std::string* resolvedUrlOut = nullptr);

    // Return local ROM path for a URL, "" if not yet downloaded.
    std::string localPath(const std::string& url) const;
    bool        isDownloaded(const std::string& url) const;

    // Download multiple entries concurrently.
    // maxWorkers controls parallel downloads (0 = one per entry).
    // progress(url, received, total) is called from worker threads.
    // Returns url → local path map ("" on failure).
    std::map<std::string, std::string> downloadBatch(
        const std::vector<QueuedDownload>& entries,
        int maxWorkers = 8,
        std::function<void(const std::string&, long long, long long)> progress = nullptr);

    // Flush queue and downloaded map to disk.
    void flush();

    const std::vector<QueuedDownload>& queue() const { return queue_; }

private:
    DownloadManager() = default;
    void loadFromDisk();
    void saveToDisk();
    bool ensureSpace(long long neededBytes);
    std::string extractRom(const std::string& archivePath, const std::string& destDir,
                           bool allowGbaFallback, bool preferPcContent);
    long long   usedStorageBytes() const;
    static bool isPcSystem(const std::string& system);
    static bool ensurePcCue(const std::string& path);

    std::string queuePath_;
    std::string downloadedPath_;
    std::string romsDir_;
    std::string cookieFile_;
    std::string runtimeCookieFile_;
    std::vector<QueuedDownload>      queue_;
    std::map<std::string, std::string> urlToLocal_; // url → local rom path

    mutable std::recursive_mutex mutex_;
};
