#pragma once

#include <string>

struct OracleGameRow {
    int         id = 0;
    std::string name;
    std::string downloadUrl;
    std::string coverUrl;
    std::string systemName;
    std::string status;    // 'remote' | 'downloaded' | 'queued' | 'failed'
    std::string localPath;
};
