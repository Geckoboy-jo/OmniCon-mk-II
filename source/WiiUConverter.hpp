#pragma once

#include <functional>
#include <string>
#include "WiiUConverterResult.hpp"

// Converts Wii U NUS archives (title.tmd/title.tik/*.app inside a .7z or .zip)
// into titles installed in Cemu's mlc folder. Conversion is lazy: it runs the
// first time a title is launched (or via the `convert-wiiu` command) and is
// recorded in saves/wiiu_converted.tsv so later runs skip it.
namespace WiiUConverter {

using Progress = std::function<void(const std::string&)>;

bool isArchive(const std::string& path);

// OMNICON_CEMU_MLC if set, otherwise Cemu's default mlc01 under XDG_DATA_HOME.
std::string mlcPath();

// Installs the title from the archive unless it was already converted.
Result ensureConverted(const std::string& archivePath, const Progress& progress = nullptr);

// First .rpx inside <titleDir>/code, or "" if there is none.
std::string findRpx(const std::string& titleDir);

// Installed base game directory for any base/update/DLC title ID, or "".
std::string installedBaseDir(const std::string& titleId);

// Converts every archive in a directory. Returns the number converted or
// already present; failures (if given) receives the number that failed.
int convertAll(const std::string& directory, const Progress& progress = nullptr,
               int* failures = nullptr);

}  // namespace WiiUConverter
