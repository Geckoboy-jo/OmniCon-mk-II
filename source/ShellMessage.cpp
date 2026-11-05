#include "ShellMessage.hpp"

#include <chrono>
#include <iostream>
#include <mutex>

namespace {
std::mutex g_mutex;
std::string g_message;
std::chrono::steady_clock::time_point g_postedAt;
constexpr std::chrono::seconds kLifetime{8};
} // namespace

void postShellMessage(const std::string &message) {
  std::cerr << "[Shell] " << message << "\n";
  std::lock_guard<std::mutex> lock(g_mutex);
  g_message = message;
  g_postedAt = std::chrono::steady_clock::now();
}

bool currentShellMessage(std::string &out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_message.empty() ||
      std::chrono::steady_clock::now() - g_postedAt > kLifetime)
    return false;
  out = g_message;
  return true;
}
