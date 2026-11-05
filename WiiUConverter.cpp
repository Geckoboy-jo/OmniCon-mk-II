#include "WiiUConverter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace WiiUConverter {
namespace {

const char *const kRecordFile = "saves/wiiu_converted.tsv";
const char *const kCdecryptDir = "third_party/cdecrypt";

std::string shellQuote(const std::string &value) {
  std::string quoted = "'";
  for (char c : value) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  return quoted + "'";
}

std::string toLower(std::string value) {
  for (char &c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

void report(const Progress &progress, const std::string &message) {
  if (progress)
    progress(message);
}

// Returns a 7-Zip-compatible command, or "" when none is installed.
std::string sevenZipCommand() {
  for (const char *candidate : {"7z", "7zz", "7zr"}) {
    const std::string probe = std::string(candidate) + " i >/dev/null 2>&1";
    if (std::system(probe.c_str()) == 0)
      return candidate;
  }
  return "";
}

// Builds the bundled cdecrypt on first use; returns its path or "" with error
// set.
std::string ensureCdecrypt(const Progress &progress, std::string &error) {
  const fs::path directory = fs::absolute(kCdecryptDir);
  const fs::path binary = directory / "cdecrypt";
  if (fs::exists(binary))
    return binary.string();
  if (!fs::exists(directory / "cdecrypt.c")) {
    error = "Bundled cdecrypt source is missing from " + directory.string();
    return "";
  }
  report(progress, "First run: building cdecrypt...");
  const std::string command =
      "make -C " + shellQuote(directory.string()) + " >/dev/null 2>&1";
  if (std::system(command.c_str()) != 0 || !fs::exists(binary)) {
    error = "Could not build cdecrypt (is a C compiler and make installed?)";
    return "";
  }
  return binary.string();
}

// Big-endian title ID at offset 0x18C of title.tmd.
std::string readTitleId(const fs::path &tmd) {
  std::ifstream file(tmd, std::ios::binary);
  unsigned char bytes[8] = {};
  if (!file.seekg(0x18C) ||
      !file.read(reinterpret_cast<char *>(bytes), sizeof(bytes)))
    return "";
  static const char *const kHex = "0123456789abcdef";
  std::string id;
  for (unsigned char b : bytes) {
    id += kHex[b >> 4];
    id += kHex[b & 0x0F];
  }
  return id;
}

fs::path findNusDirectory(const fs::path &root) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->is_regular_file(ec) &&
        toLower(it->path().filename().string()) == "title.tmd" &&
        fs::exists(it->path().parent_path() / "title.tik"))
      return it->path().parent_path();
  }
  return {};
}

fs::path titleDirectory(const std::string &titleId) {
  return fs::path(mlcPath()) / "usr" / "title" / titleId.substr(0, 8) /
         titleId.substr(8);
}

bool titleInstalled(const fs::path &directory) {
  std::error_code ec;
  return fs::is_directory(directory / "meta", ec) ||
         fs::is_directory(directory / "content", ec);
}

struct Record {
  std::uintmax_t size = 0;
  std::string titleId;
};

std::map<std::string, Record> loadRecords() {
  std::map<std::string, Record> records;
  std::ifstream file(kRecordFile);
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream fields(line);
    std::string path, size, titleId;
    if (std::getline(fields, path, '\t') && std::getline(fields, size, '\t') &&
        std::getline(fields, titleId, '\t')) {
      records[path] = {
          static_cast<std::uintmax_t>(std::strtoull(size.c_str(), nullptr, 10)),
          titleId};
    }
  }
  return records;
}

void saveRecord(const std::string &archive, const Record &record) {
  auto records = loadRecords();
  records[archive] = record;
  std::error_code ec;
  fs::create_directories(fs::path(kRecordFile).parent_path(), ec);
  std::ofstream file(kRecordFile, std::ios::trunc);
  for (const auto &[path, rec] : records)
    file << path << '\t' << rec.size << '\t' << rec.titleId << '\n';
}

} // namespace

bool isArchive(const std::string &path) {
  const std::string ext = toLower(fs::path(path).extension().string());
  return ext == ".7z" || ext == ".zip";
}

std::string mlcPath() {
  if (const char *configured = std::getenv("OMNICON_CEMU_MLC");
      configured && *configured)
    return configured;
  if (const char *data = std::getenv("XDG_DATA_HOME"); data && *data)
    return (fs::path(data) / "Cemu" / "mlc01").string();
  const char *home = std::getenv("HOME");
  return ((home && *home ? fs::path(home) : fs::path("/root")) / ".local" /
          "share" / "Cemu" / "mlc01")
      .string();
}

std::string findRpx(const std::string &titleDir) {
  std::error_code ec;
  for (const auto &entry :
       fs::directory_iterator(fs::path(titleDir) / "code", ec)) {
    if (entry.is_regular_file(ec) &&
        toLower(entry.path().extension().string()) == ".rpx")
      return entry.path().string();
  }
  return "";
}

std::string installedBaseDir(const std::string &titleId) {
  if (titleId.size() != 16)
    return "";
  const fs::path base = titleDirectory("00050000" + titleId.substr(8));
  return findRpx(base.string()).empty() ? "" : base.string();
}

Result ensureConverted(const std::string &archivePath,
                       const Progress &progress) {
  Result result;
  std::error_code ec;
  const std::string archive = fs::absolute(archivePath).string();
  if (!fs::is_regular_file(archive, ec)) {
    result.error = "Archive not found: " + archive;
    return result;
  }
  const std::uintmax_t archiveSize = fs::file_size(archive, ec);

  // Already converted on a previous run?
  const auto records = loadRecords();
  if (auto it = records.find(archive);
      it != records.end() && it->second.size == archiveSize &&
      it->second.titleId.size() == 16 &&
      titleInstalled(titleDirectory(it->second.titleId))) {
    result.ok = true;
    result.titleId = it->second.titleId;
    result.titleDir = titleDirectory(result.titleId).string();
    return result;
  }

  const std::string sevenZip = sevenZipCommand();
  if (sevenZip.empty()) {
    result.error =
        "7-Zip is required to unpack Wii U archives (install p7zip-full).";
    return result;
  }
  const std::string cdecrypt = ensureCdecrypt(progress, result.error);
  if (cdecrypt.empty())
    return result;

  std::string pattern =
      (fs::path(archive).parent_path() / "_omnicon_nus_XXXXXX").string();
  std::vector<char> writable(pattern.begin(), pattern.end());
  writable.push_back('\0');
  if (!mkdtemp(writable.data())) {
    result.error =
        "Could not create a temporary directory next to the archive.";
    return result;
  }
  const fs::path staging = writable.data();
  auto cleanup = [&]() {
    std::error_code removeError;
    fs::remove_all(staging, removeError);
  };

  report(progress, "Unpacking " + fs::path(archive).filename().string() +
                       " (first run only)...");
  const std::string extract = sevenZip + " x -y -aoa -o" +
                              shellQuote(staging.string()) + " " +
                              shellQuote(archive) + " >/dev/null 2>&1";
  if (std::system(extract.c_str()) != 0) {
    cleanup();
    result.error = "Could not unpack " + archive;
    return result;
  }

  const fs::path nusDirectory = findNusDirectory(staging);
  if (nusDirectory.empty()) {
    cleanup();
    result.error =
        "Archive does not contain Wii U NUS content (title.tmd/title.tik).";
    return result;
  }
  const std::string titleId = readTitleId(nusDirectory / "title.tmd");
  if (titleId.empty()) {
    cleanup();
    result.error = "Could not read the title ID from title.tmd.";
    return result;
  }

  const fs::path target = titleDirectory(titleId);
  report(progress,
         "Decrypting title " + titleId + " into " + target.string() + "...");
  fs::remove_all(target, ec);
  fs::create_directories(target.parent_path(), ec);
  const std::string decrypt = shellQuote(cdecrypt) + " " +
                              shellQuote(nusDirectory.string()) + " " +
                              shellQuote(target.string()) + " >/dev/null 2>&1";
  const int status = std::system(decrypt.c_str());
  cleanup();
  if (status != 0 || !titleInstalled(target)) {
    fs::remove_all(target, ec);
    result.error = "Decryption failed for title " + titleId + ".";
    return result;
  }

  saveRecord(archive, {archiveSize, titleId});
  report(progress, "Installed title " + titleId + ".");
  result.ok = true;
  result.titleId = titleId;
  result.titleDir = target.string();
  return result;
}

int convertAll(const std::string &directory, const Progress &progress,
               int *failures) {
  std::vector<fs::path> archives;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(directory, ec)) {
    if (entry.is_regular_file(ec) && isArchive(entry.path().string()))
      archives.push_back(entry.path());
  }
  std::sort(archives.begin(), archives.end());

  int converted = 0;
  int failed = 0;
  for (std::size_t i = 0; i < archives.size(); ++i) {
    report(progress, "[" + std::to_string(i + 1) + "/" +
                         std::to_string(archives.size()) + "] " +
                         archives[i].filename().string());
    const Result result = ensureConverted(archives[i].string(), progress);
    if (result.ok) {
      ++converted;
    } else {
      ++failed;
      report(progress, "  failed: " + result.error);
    }
  }
  if (failures)
    *failures = failed;
  return converted;
}

} // namespace WiiUConverter
