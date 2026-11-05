#include "ConsoleLaunchers.hpp"
#include "NintendoSwitch.hpp"
#include "ShellMessage.hpp"
#include "top.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace fs = std::filesystem;

namespace {

bool runningUnderWsl() {
  std::ifstream osrelease("/proc/sys/kernel/osrelease");
  std::string line;
  std::getline(osrelease, line);
  std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return line.find("microsoft") != std::string::npos;
}

bool executableOnPath(const std::string &name) {
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv)
    return false;
  std::stringstream paths(pathEnv);
  std::string dir;
  while (std::getline(paths, dir, ':')) {
    if (dir.empty())
      continue;
    if (access((dir + "/" + name).c_str(), X_OK) == 0)
      return true;
  }
  return false;
}

// Link a key file from the repo's Misc BIOS/ into Ryubing's system dir.
// Uses a symlink so updating the repo copy propagates; falls back to a copy
// on filesystems that don't support symlinks.  Never overwrites an existing
// real (non-symlink) file the user installed themselves.
void linkKeyFile(const fs::path &systemDir, const char *fileName) {
  const fs::path source = fs::path("Misc BIOS") / fileName;
  if (!fs::exists(source))
    return;

  std::error_code ec;
  fs::create_directories(systemDir, ec);
  const fs::path dest = systemDir / fileName;

  // Already correctly linked?
  if (fs::is_symlink(dest, ec) &&
      fs::read_symlink(dest, ec) == fs::absolute(source))
    return;
  // A real file the user placed — leave it alone.
  if (fs::exists(dest, ec) && !fs::is_symlink(dest, ec))
    return;

  fs::remove(dest, ec); // remove stale symlink, if any
  if (fs::exists(dest, ec))
    return; // couldn't remove; don't clobber

  const fs::path absSource = fs::absolute(source, ec);
  if (ec)
    return;
  fs::create_symlink(absSource, dest, ec);
  if (ec) {
    // Symlink unsupported (e.g. some /mnt drives): copy instead.
    fs::copy_file(absSource, dest, fs::copy_options::none, ec);
  }
  if (!ec)
    std::cerr << "[Switch] Linked " << fileName << " -> " << systemDir << "\n";
}

// Point Ryubing at the console keys stored in the repo's Misc BIOS/ folder.
// Ryubing (a Ryujinx fork) reads prod.keys / title.keys from the system/
// subdir of its profile directory.  On Linux that is ~/.config/Ryujinx by
// default (Ryubing keeps the Ryujinx paths for compatibility); a portable
// build uses a "Ryujinx" folder beside the executable.  We seed the default
// Linux profile so the AppImage works out of the box.
void ensureSwitchKeys() {
  const char *home = std::getenv("HOME");
  if (!home || !*home)
    return;

  const fs::path repoKeys = fs::path("Misc BIOS") / "prod.keys";
  if (!fs::exists(repoKeys)) {
    postShellMessage("Switch: Misc BIOS/prod.keys not found — "
                     "Ryubing needs your console's prod.keys.");
    return;
  }

  // Default Linux profile dirs Ryubing/Ryujinx read, in priority order.
  const fs::path base = fs::path(home) / ".config";
  for (const char *profile : {"Ryubing", "Ryujinx"}) {
    const fs::path systemDir = base / profile / "system";
    linkKeyFile(systemDir, "prod.keys");
    linkKeyFile(systemDir, "title.keys"); // optional; ignored if absent
  }
}

// ─── Firmware install
// ───────────────────────────────────────────────────────── Ryubing does not
// read firmware from Misc BIOS/ — that folder is only where OmniCon stores the
// dumped files.  The emulator needs each .nca *installed* into
// <profile>/bis/system/Contents/registered/ using this layout:
//     registered/<hash>/<hash>.nca              (plain content .nca)
//     registered/<hash>.cnmt/<hash>.cnmt.nca    (content-meta .nca)
// Loose "Firmware X.Y.Z" dumps hold the files flat, named by their hash, so
// installing is just placing each file in its own hash-named folder.

// Parse "Firmware 21.1.0" -> {21,1,0} for version comparison.
std::array<int, 3> firmwareVersion(const std::string &dirName, bool &ok) {
  std::array<int, 3> v{0, 0, 0};
  ok = false;
  const std::string prefix = "Firmware ";
  if (dirName.rfind(prefix, 0) != 0)
    return v;
  if (std::sscanf(dirName.c_str() + prefix.size(), "%d.%d.%d", &v[0], &v[1],
                  &v[2]) == 3)
    ok = true;
  return v;
}

// Newest "Misc BIOS/Firmware X.Y.Z" directory, or empty path if none.
fs::path newestFirmwareDir() {
  fs::path best;
  std::array<int, 3> bestVer{0, 0, 0};
  bool haveBest = false;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator("Misc BIOS", ec)) {
    if (!entry.is_directory())
      continue;
    bool ok = false;
    const auto ver = firmwareVersion(entry.path().filename().string(), ok);
    if (!ok)
      continue;
    if (!haveBest || ver > bestVer) {
      bestVer = ver;
      best = entry.path();
      haveBest = true;
    }
  }
  return best;
}

// Install every .nca from the firmware dump into one profile's registered/.
// Returns the number of files newly installed (already-present ones skipped).
int installFirmwareInto(const fs::path &registered,
                        const fs::path &firmwareDir) {
  int installed = 0;
  std::error_code ec;
  fs::create_directories(registered, ec);
  for (const auto &entry : fs::directory_iterator(firmwareDir, ec)) {
    if (!entry.is_regular_file())
      continue;
    const std::string name = entry.path().filename().string();

    std::string hash; // folder name under registered/
    if (name.size() > 9 && name.compare(name.size() - 9, 9, ".cnmt.nca") == 0)
      hash = name.substr(0, name.size() - 9) + ".cnmt";
    else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".nca") == 0)
      hash = name.substr(0, name.size() - 4);
    else
      continue; // not a firmware content file

    const fs::path destDir = registered / hash;
    const fs::path destFile = destDir / name;
    if (fs::exists(destFile, ec))
      continue; // already installed

    fs::create_directories(destDir, ec);
    // Hard-link (same volume) or copy; never moves the repo original.
    fs::create_hard_link(entry.path(), destFile, ec);
    if (ec) {
      ec.clear();
      fs::copy_file(entry.path(), destFile, fs::copy_options::none, ec);
    }
    if (!ec)
      ++installed;
  }
  return installed;
}

// Install the newest Misc BIOS firmware dump into Ryubing's profiles so the
// emulator stops reporting missing firmware.  Idempotent: files already in
// place are skipped, so repeat launches are cheap.
void ensureSwitchFirmware() {
  const fs::path firmwareDir = newestFirmwareDir();
  if (firmwareDir.empty()) {
    postShellMessage("Switch: no 'Misc BIOS/Firmware X.Y.Z' folder found — "
                     "Ryubing needs installed firmware.");
    return;
  }

  const char *home = std::getenv("HOME");
  if (!home || !*home)
    return;
  const fs::path base = fs::path(home) / ".config";

  int total = 0;
  for (const char *profile : {"Ryubing", "Ryujinx"}) {
    const fs::path registered =
        base / profile / "bis" / "system" / "Contents" / "registered";
    total += installFirmwareInto(registered, firmwareDir);
  }
  if (total > 0)
    std::cerr << "[Switch] Installed " << total << " firmware file(s) from "
              << firmwareDir.filename() << " into Ryubing.\n";
}

} // namespace

bool NintendoSwitchConsoleLauncher::launch(
    const std::string &contentPath) const {
  const char *configured = std::getenv("OMNICON_SWITCH_LAUNCHER");
  std::string launcher = configured && *configured ? configured : "";
  if (launcher.empty()) {
    // WSL2 exposes only software Vulkan, which renders 3D games far too
    // slowly; there, prefer the Windows-native Ryubing via interop.
    launcher = runningUnderWsl() && executableOnPath("Ryubing-win")
                   ? "Ryubing-win"
                   : "Ryubing";
  }

  // Point Ryubing at the console keys and firmware in Misc BIOS/ before
  // launching.  (No-op for the Windows-native build, which reads its own
  // profile — that one already has prod.keys and firmware installed.)
  if (launcher != "Ryubing-win") {
    ensureSwitchKeys();
    ensureSwitchFirmware();
  }

  return launchExternalApplication(
      launcher,
      {"--fullscreen", std::filesystem::absolute(contentPath).string()});
}
