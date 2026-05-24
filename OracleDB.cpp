#include "OracleDB.hpp"

#include <mysql/mysql.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

// ─── helpers ──────────────────────────────────────────────────────────────

// Parse ~/.oci/omnicon_env (export KEY=VALUE lines) and inject into process env.
static void loadOmniconEnv() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::ifstream f(std::string(home) + "/.oci/omnicon_env");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] == '#') continue;
        line = line.substr(p);
        if (line.substr(0, 7) == "export ") line = line.substr(7);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (val.size() >= 2 &&
            ((val.front() == '\'' && val.back() == '\'') ||
             (val.front() == '"'  && val.back() == '"')))
            val = val.substr(1, val.size() - 2);
        if (!std::getenv(key.c_str()))
            setenv(key.c_str(), val.c_str(), 0);
    }
}

// Returns true if host:port is TCP-reachable within timeout_sec seconds.
static bool mysqlPortOpen(const std::string& host, int port, int timeout_sec = 5) {
    std::string cmd = "timeout " + std::to_string(timeout_sec)
                    + " bash -c 'echo > /dev/tcp/" + host + "/" + std::to_string(port)
                    + "' 2>/dev/null";
    return std::system(cmd.c_str()) == 0;
}

// Returns true if s is non-empty and not a <placeholder>.
static bool isValidEnvValue(const char* s) {
    if (!s || s[0] == '\0') return false;
    std::string v(s);
    return !(v.front() == '<' && v.back() == '>');
}

// Open a MySQL connection. Caller must mysql_close() the result.
static MYSQL* mysqlConnect(const OracleDBConfig& cfg) {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) return nullptr;
    unsigned int timeout = 5;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(conn, MYSQL_OPT_READ_TIMEOUT,    &timeout);
    mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT,   &timeout);
    const char* db = cfg.service.empty() ? "omnicon" : cfg.service.c_str();
    if (!mysql_real_connect(conn, cfg.host.c_str(), cfg.user.c_str(),
                            cfg.password.c_str(), db,
                            static_cast<unsigned int>(cfg.port), nullptr, 0)) {
        std::cerr << "[MySQL] Connect failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

// ─── public API ───────────────────────────────────────────────────────────

OracleDBConfig promptOracleConfig() {
    loadOmniconEnv();

    OracleDBConfig cfg;
    const char* env_user    = std::getenv("ORACLE_APP_USER");
    const char* env_pass    = std::getenv("ORACLE_APP_PASSWORD");
    const char* env_host    = std::getenv("ORACLE_APP_HOST");
    const char* env_service = std::getenv("ORACLE_APP_SERVICE");

    const char* env_port = std::getenv("ORACLE_APP_PORT");

    std::cout << "MySQL DB configuration:\n";
    if (isValidEnvValue(env_host)) {
        cfg.host = env_host;
        std::cout << "  Host: " << cfg.host << " (from ORACLE_APP_HOST)\n";
    } else {
        std::cout << "  Host: ";
        std::getline(std::cin, cfg.host);
    }

    if (isValidEnvValue(env_port))
        cfg.port = std::stoi(env_port);

    if (isValidEnvValue(env_service) &&
        std::string(env_service) != "XE" && std::string(env_service) != "FREE")
        cfg.service = env_service;
    else
        cfg.service = "omnicon";

    if (isValidEnvValue(env_user))
        cfg.user = env_user;
    else {
        std::cout << "  DB user (default: omnicon): ";
        std::getline(std::cin, cfg.user);
        if (cfg.user.empty()) cfg.user = "omnicon";
    }

    if (isValidEnvValue(env_pass))
        cfg.password = env_pass;
    else {
        std::cout << "  DB password: ";
        std::getline(std::cin, cfg.password);
    }

    return cfg;
}

bool oracleExecute(const OracleDBConfig& cfg, const std::string& sql) {
    if (!mysqlPortOpen(cfg.host, cfg.port)) {
        std::cerr << "[MySQL] " << cfg.host << ":" << cfg.port << " unreachable — skipping.\n";
        return false;
    }
    MYSQL* conn = mysqlConnect(cfg);
    if (!conn) return false;
    bool ok = (mysql_real_query(conn, sql.c_str(),
                                static_cast<unsigned long>(sql.size())) == 0);
    if (!ok)
        std::cerr << "[MySQL] Execute failed: " << mysql_error(conn) << "\n";
    mysql_close(conn);
    return ok;
}

bool createGamesTable(const OracleDBConfig& cfg) {
    static const char* ddl =
        "CREATE TABLE IF NOT EXISTS GAMES ("
        "  ID INT AUTO_INCREMENT PRIMARY KEY,"
        "  NAME VARCHAR(255),"
        "  DOWNLOAD_URL TEXT,"
        "  COVER_URL TEXT,"
        "  SYSTEM_NAME VARCHAR(255),"
        "  STATUS VARCHAR(32),"
        "  LOCAL_PATH VARCHAR(1024),"
        "  SIZE_BYTES BIGINT,"
        "  CREATED_AT DATETIME DEFAULT NOW(),"
        "  LAST_PLAYED DATETIME"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    return oracleExecute(cfg, ddl);
}

bool insertGameEntry(const OracleDBConfig& cfg,
                     const std::string& name,
                     const std::string& download_url,
                     const std::string& cover_url,
                     const std::string& system,
                     long long size_bytes) {
    if (!mysqlPortOpen(cfg.host, cfg.port)) return false;
    MYSQL* conn = mysqlConnect(cfg);
    if (!conn) return false;

    auto esc = [&](const std::string& s) -> std::string {
        std::vector<char> buf(s.size() * 2 + 1);
        mysql_real_escape_string(conn, buf.data(), s.c_str(),
                                 static_cast<unsigned long>(s.size()));
        return std::string(buf.data());
    };

    std::ostringstream sql;
    sql << "INSERT INTO GAMES(NAME,DOWNLOAD_URL,COVER_URL,SYSTEM_NAME,STATUS,SIZE_BYTES)"
           " VALUES('"
        << esc(name) << "','" << esc(download_url) << "','"
        << esc(cover_url) << "','" << esc(system) << "','remote'," << size_bytes << ")";

    const std::string q = sql.str();
    bool ok = (mysql_real_query(conn, q.c_str(),
                                static_cast<unsigned long>(q.size())) == 0);
    if (!ok)
        std::cerr << "[MySQL] Insert failed: " << mysql_error(conn) << "\n";
    mysql_close(conn);
    return ok;
}

bool updateGameLocalPath(const OracleDBConfig& cfg, int id,
                         const std::string& localPath) {
    if (!mysqlPortOpen(cfg.host, cfg.port)) return false;
    MYSQL* conn = mysqlConnect(cfg);
    if (!conn) return false;

    auto esc = [&](const std::string& s) -> std::string {
        std::vector<char> buf(s.size() * 2 + 1);
        mysql_real_escape_string(conn, buf.data(), s.c_str(),
                                 static_cast<unsigned long>(s.size()));
        return std::string(buf.data());
    };

    std::ostringstream sql;
    sql << "UPDATE GAMES SET STATUS='downloaded',LOCAL_PATH='"
        << esc(localPath) << "',LAST_PLAYED=NULL WHERE ID=" << id;

    const std::string q = sql.str();
    bool ok = (mysql_real_query(conn, q.c_str(),
                                static_cast<unsigned long>(q.size())) == 0);
    if (!ok)
        std::cerr << "[MySQL] Update failed: " << mysql_error(conn) << "\n";
    mysql_close(conn);
    return ok;
}

bool markGameFailed(const OracleDBConfig& cfg, int id) {
    if (id <= 0) return false;
    if (!mysqlPortOpen(cfg.host, cfg.port)) return false;
    MYSQL* conn = mysqlConnect(cfg);
    if (!conn) return false;
    std::ostringstream sql;
    sql << "UPDATE GAMES SET STATUS='failed' WHERE ID=" << id;
    const std::string q = sql.str();
    bool ok = (mysql_real_query(conn, q.c_str(),
                                static_cast<unsigned long>(q.size())) == 0);
    if (!ok)
        std::cerr << "[MySQL] markGameFailed: " << mysql_error(conn) << "\n";
    mysql_close(conn);
    return ok;
}

bool updateGameDownloadUrl(const OracleDBConfig& cfg, int id, const std::string& newUrl) {
    if (id <= 0 || newUrl.empty()) return false;
    if (!mysqlPortOpen(cfg.host, cfg.port)) return false;
    MYSQL* conn = mysqlConnect(cfg);
    if (!conn) return false;

    auto esc = [&](const std::string& s) -> std::string {
        std::vector<char> buf(s.size() * 2 + 1);
        mysql_real_escape_string(conn, buf.data(), s.c_str(),
                                 static_cast<unsigned long>(s.size()));
        return std::string(buf.data());
    };

    std::ostringstream sql;
    sql << "UPDATE GAMES SET DOWNLOAD_URL='" << esc(newUrl) << "' WHERE ID=" << id;
    const std::string q = sql.str();
    bool ok = (mysql_real_query(conn, q.c_str(),
                                static_cast<unsigned long>(q.size())) == 0);
    if (!ok)
        std::cerr << "[MySQL] updateGameDownloadUrl: " << mysql_error(conn) << "\n";
    else
        std::cout << "[DB] Updated DOWNLOAD_URL for game id=" << id << "\n";
    mysql_close(conn);
    return ok;
}

bool queryGames(const OracleDBConfig& cfg, std::vector<OracleGameRow>& rows) {
    rows.clear();
    if (!mysqlPortOpen(cfg.host, cfg.port)) {
        std::cerr << "[DB] Cannot reach " << cfg.host << ":" << cfg.port
                  << " — Oracle games will not appear in library.\n";
        return false;
    }

    MYSQL* conn = mysqlConnect(cfg);
    if (!conn) return false;

    static const char* query =
        "SELECT ID,NAME,IFNULL(DOWNLOAD_URL,''),IFNULL(COVER_URL,''),"
        "IFNULL(SYSTEM_NAME,''),IFNULL(STATUS,'remote'),IFNULL(LOCAL_PATH,'')"
        " FROM GAMES WHERE STATUS!='failed'";

    if (mysql_real_query(conn, query, static_cast<unsigned long>(strlen(query))) != 0) {
        std::cerr << "[DB] Query failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return false;
    }

    MYSQL_RES* res = mysql_use_result(conn);
    if (!res) { mysql_close(conn); return false; }

    MYSQL_ROW mrow;
    while ((mrow = mysql_fetch_row(res))) {
        OracleGameRow r;
        try { r.id = std::stoi(mrow[0] ? mrow[0] : "0"); } catch (...) { continue; }
        r.name        = mrow[1] ? mrow[1] : "";
        r.downloadUrl = mrow[2] ? mrow[2] : "";
        r.coverUrl    = mrow[3] ? mrow[3] : "";
        r.systemName  = mrow[4] ? mrow[4] : "";
        r.status      = mrow[5] ? mrow[5] : "remote";
        r.localPath   = mrow[6] ? mrow[6] : "";
        rows.push_back(std::move(r));
    }

    mysql_free_result(res);
    mysql_close(conn);
    std::cout << "[DB] queryGames: fetched " << rows.size() << " row(s).\n";
    return true;
}

