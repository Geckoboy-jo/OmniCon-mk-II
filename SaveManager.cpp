#include "SaveManager.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

SaveManager &SaveManager::instance() {
  static SaveManager mgr;
  return mgr;
}

bool SaveManager::init(const std::string &dbPath) {
  dbPath_ = dbPath;
  try {
    fs::path p = dbPath_;
    if (p.has_parent_path())
      fs::create_directories(p.parent_path());
  } catch (...) {
    return false;
  }
  loadFromDisk();
  return true;
}

SaveManager::~SaveManager() { saveToDisk(); }

bool SaveManager::get(const std::string &key,
                      std::vector<unsigned char> &out) const {
  auto it = tree_.find(key);
  if (it == tree_.end())
    return false;
  out = it->second;
  return true;
}

bool SaveManager::put(const std::string &key,
                      const std::vector<unsigned char> &data) {
  tree_[key] = data;
  dirty_ = true;
  // For simplicity, flush immediately to avoid data loss on crash.
  saveToDisk();
  dirty_ = false;
  return true;
}

void SaveManager::flush() {
  if (dirty_)
    saveToDisk();
}

// Simple on-disk format: [MAGIC(4)] [version(1)] [count(u64)] then repeated
// entries: [keylen(u32)] [key bytes] [vallen(u64)] [val bytes]
void SaveManager::loadFromDisk() {
  tree_.clear();
  std::ifstream in(dbPath_, std::ios::binary);
  if (!in)
    return;
  char magic[4];
  in.read(magic, 4);
  if (!in || magic[0] != 'S' || magic[1] != 'A' || magic[2] != 'V' ||
      magic[3] != 'M')
    return;
  uint8_t ver = 0;
  in.read(reinterpret_cast<char *>(&ver), 1);
  uint64_t count = 0;
  in.read(reinterpret_cast<char *>(&count), sizeof(count));
  for (uint64_t i = 0; i < count && in; ++i) {
    uint32_t klen = 0;
    in.read(reinterpret_cast<char *>(&klen), sizeof(klen));
    if (!in)
      break;
    std::string key(klen, '\0');
    in.read(&key[0], klen);
    uint64_t vlen = 0;
    in.read(reinterpret_cast<char *>(&vlen), sizeof(vlen));
    if (!in)
      break;
    std::vector<unsigned char> val;
    val.resize(vlen);
    if (vlen > 0)
      in.read(reinterpret_cast<char *>(&val[0]), vlen);
    tree_[key] = std::move(val);
  }
}

void SaveManager::saveToDisk() {
  std::ofstream out(dbPath_ + ".tmp", std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "[SaveManager] Unable to write save DB: " << dbPath_ << "\n";
    return;
  }
  out.write("SAVM", 4);
  uint8_t ver = 1;
  out.write(reinterpret_cast<const char *>(&ver), 1);
  uint64_t count = tree_.size();
  out.write(reinterpret_cast<const char *>(&count), sizeof(count));
  for (const auto &[k, v] : tree_) {
    uint32_t klen = static_cast<uint32_t>(k.size());
    out.write(reinterpret_cast<const char *>(&klen), sizeof(klen));
    out.write(k.data(), klen);
    uint64_t vlen = static_cast<uint64_t>(v.size());
    out.write(reinterpret_cast<const char *>(&vlen), sizeof(vlen));
    if (vlen)
      out.write(reinterpret_cast<const char *>(v.data()), vlen);
  }
  out.close();
  try {
    fs::rename(dbPath_ + ".tmp", dbPath_);
  } catch (...) {
    std::cerr << "[SaveManager] Failed to move temp DB into place\n";
  }
}
