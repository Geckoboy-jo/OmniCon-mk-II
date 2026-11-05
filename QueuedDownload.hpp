#pragma once

#include <string>

struct QueuedDownload {
  int id = 0; // Oracle DB row id (0 if not from DB)
  std::string name;
  std::string url;
  std::string system;
  long long sizeBytes = 0;
  std::string queuedAt; // ISO-8601 timestamp
};
