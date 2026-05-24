#ifndef SAVE_MANAGER_HPP
#define SAVE_MANAGER_HPP

#include <string>
#include <vector>
#include <map>

class SaveManager {
public:
    static SaveManager& instance();

    // Initialize with an on-disk path for the database file. Returns true on success.
    bool init(const std::string& dbPath);

    // Retrieve a save blob for a key (rom path). Returns true if a blob was found.
    bool get(const std::string& key, std::vector<unsigned char>& out) const;

    // Store a save blob for a key. Returns true on success.
    bool put(const std::string& key, const std::vector<unsigned char>& data);

    // Force flush to disk.
    void flush();

private:
    SaveManager() = default;
    ~SaveManager();

    // In-memory B+ tree substitute: ordered map for efficient lookup.
    std::map<std::string, std::vector<unsigned char>> tree_;
    std::string dbPath_;
    bool dirty_ = false;

    void loadFromDisk();
    void saveToDisk();
};

#endif
