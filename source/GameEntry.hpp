#pragma once

#include <string>
#include "SystemType.hpp"

struct GameEntry {
    std::string romPath;      // local ROM path (empty if not yet downloaded)
    std::string downloadUrl;  // source URL from DB (empty for purely local ROMs)
    std::string displayName;
    std::string coverPath;    // empty if not yet resolved
    SystemType  system = SystemType::Unknown;
    int         dbId   = 0;   // Oracle DB row id (0 if not in DB)
    int         achievementPercent = -1;  // 0-100 achievements earned (Steam/Xbox); -1 = unknown or none
};
