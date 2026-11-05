#pragma once

#include <string>

struct OracleDBConfig {
  std::string host;
  int port = 13306;
  std::string service; // MySQL database name (default: omnicon)
  std::string user;
  std::string password;
};
