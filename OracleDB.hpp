#pragma once

#include <string>
#include <vector>

struct OracleDBConfig {
    std::string host;
    int port = 13306;
    std::string service; // MySQL database name (default: omnicon)
    std::string user;
    std::string password;
};

struct OracleGameRow {
    int         id = 0;
    std::string name;
    std::string downloadUrl;
    std::string coverUrl;
    std::string systemName;
    std::string status;    // 'remote' | 'downloaded' | 'queued' | 'failed'
    std::string localPath;
};

// Load DB config from env vars / interactive prompt.
OracleDBConfig promptOracleConfig();

// Execute a single SQL statement. Returns true on success.
bool oracleExecute(const OracleDBConfig& cfg, const std::string& sql);

// Create the GAMES table if it doesn't exist.
bool createGamesTable(const OracleDBConfig& cfg);

// Insert a game metadata entry.
bool insertGameEntry(const OracleDBConfig& cfg,
                     const std::string& name,
                     const std::string& download_url,
                     const std::string& cover_url,
                     const std::string& system,
                     long long size_bytes);

// Update a game row's status and local_path after download.
bool updateGameLocalPath(const OracleDBConfig& cfg, int id,
                         const std::string& localPath);

// Query all games from the GAMES table (status != 'failed').
bool queryGames(const OracleDBConfig& cfg, std::vector<OracleGameRow>& rows);

// Mark a game row as permanently failed (e.g. 404 dead link).
bool markGameFailed(const OracleDBConfig& cfg, int id);

// Update the DOWNLOAD_URL for a game row (e.g. after a fallback URL is resolved).
bool updateGameDownloadUrl(const OracleDBConfig& cfg, int id, const std::string& newUrl);
