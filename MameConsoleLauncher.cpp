#include "MameConsoleLauncher.hpp"
#include "ShellMessage.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string mameExecutable() {
  const char *configured = std::getenv("OMNICON_MAME");
  if (configured && *configured)
    return configured;
  const fs::path bundled = fs::path("third_party") / "mame" / "mame";
  if (fs::exists(bundled))
    return fs::absolute(bundled).string();
  if (fs::exists("/usr/games/mame"))
    return "/usr/games/mame";
  return "mame";
}

bool anyFileExists(const std::vector<fs::path> &dirs, const std::string &name) {
  for (const auto &dir : dirs)
    if (fs::exists(dir / name))
      return true;
  return false;
}

// Reports missing files using the same ';' (all) / '|' (any of) syntax as the
// RetroArch launchers.
void warnMissing(const std::string &system, const char *required,
                 const std::vector<fs::path> &dirs) {
  std::stringstream all(required ? required : "");
  std::string entry;
  while (std::getline(all, entry, ';')) {
    if (entry.empty())
      continue;
    bool found = false;
    std::stringstream any(entry);
    std::string name;
    while (std::getline(any, name, '|'))
      found = found || anyFileExists(dirs, name);
    if (!found)
      postShellMessage(system + ": missing " + entry +
                       " (put it in Misc BIOS/mame/). The game may not start.");
  }
}

} // namespace

bool MameConsoleLauncher::launch(const std::string &contentPath) const {
  const fs::path content = fs::absolute(contentPath);
  if (!fs::exists(content)) {
    postShellMessage(std::string(info_.name) +
                     ": file not found: " + content.string());
    return false;
  }

  const std::string exe = mameExecutable();
  const bool haveExe = exe.find('/') == std::string::npos || fs::exists(exe);
  if (!haveExe) {
    postShellMessage(std::string(info_.name) +
                     ": MAME not found. Install it or set OMNICON_MAME.");
    return false;
  }

  const fs::path biosDir = fs::absolute(fs::path("Misc BIOS") / "mame");
  std::vector<fs::path> searchDirs = {biosDir, content.parent_path(),
                                      fs::absolute("roms")};

  std::string rompath = content.parent_path().string() + ";" +
                        biosDir.string() + ";" + fs::absolute("roms").string();
  std::vector<std::string> args;

  if (info_.backend == ExtraBackend::MameArcade) {
    warnMissing(info_.name, info_.bios, searchDirs);
    args = {content.stem().string(), "-rompath", rompath, "-skip_gameinfo"};
  } else {
    const char *override_ =
        info_.envVar && *info_.envVar ? std::getenv(info_.envVar) : nullptr;
    const std::string machine =
        override_ && *override_ ? override_ : info_.machine;
    if (!anyFileExists(searchDirs, machine + ".zip"))
      postShellMessage(
          std::string(info_.name) + ": MAME machine ROMs " + machine +
          ".zip not found in Misc BIOS/mame/. The game may not start.");
    warnMissing(info_.name, info_.bios, searchDirs);
    args = {machine,
            "-rompath",
            rompath,
            "-skip_gameinfo",
            std::string("-") + info_.media,
            content.string()};
  }

  if (!launchExternalApplication(exe, args)) {
    postShellMessage(std::string(info_.name) + ": failed to start MAME (" +
                     exe + ").");
    return false;
  }
  return true;
}
