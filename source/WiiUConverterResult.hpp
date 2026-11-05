#pragma once

#include <string>

namespace WiiUConverter {

struct Result {
  bool ok = false;
  std::string titleId;  // 16 lowercase hex digits
  std::string titleDir; // installed directory inside the mlc folder
  std::string error;
};

} // namespace WiiUConverter
