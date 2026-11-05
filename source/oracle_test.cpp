#include "OracleDB.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ─── minimal URL helpers ───────────────────────────────────────────────────
static std::string urlOrigin(const std::string &url) {
  auto p = url.find("://");
  if (p == std::string::npos)
    return "";
  auto s = url.find('/', p + 3);
  return s == std::string::npos ? url : url.substr(0, s);
}
static std::string urlDirectory(const std::string &url) {
  auto q = url.find_first_of("?#");
  std::string c = q == std::string::npos ? url : url.substr(0, q);
  auto s = c.rfind('/');
  auto p = c.find("://");
  if (p != std::string::npos && s <= p + 2)
    return c + "/";
  return s == std::string::npos ? c + "/" : c.substr(0, s + 1);
}
static std::string resolveUrl(const std::string &base,
                              const std::string &href) {
  if (href.empty())
    return "";
  if (href.rfind("http", 0) == 0)
    return href;
  if (href.rfind("//", 0) == 0) {
    auto p = base.find("://");
    return (p != std::string::npos ? base.substr(0, p) : "https") + ":" + href;
  }
  if (href[0] == '/')
    return urlOrigin(base) + href;
  return urlDirectory(base) + href;
}
static std::string toLower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  return s;
}
static std::string fileExt(const std::string &url) {
  auto q = url.find_first_of("?#");
  std::string c = q == std::string::npos ? url : url.substr(0, q);
  if (c.empty() || c.back() == '/')
    return "";
  auto slash = c.rfind('/');
  std::string name = slash == std::string::npos ? c : c.substr(slash + 1);
  auto dot = name.rfind('.');
  return (dot == std::string::npos || dot + 1 >= name.size())
             ? ""
             : toLower(name.substr(dot + 1));
}
static std::string fileName(const std::string &url) {
  auto q = url.find_first_of("?#");
  std::string c = q == std::string::npos ? url : url.substr(0, q);
  auto s = c.rfind('/');
  return (s == std::string::npos || s + 1 >= c.size()) ? c : c.substr(s + 1);
}
static bool isRomLink(const std::string &url) {
  static const std::set<std::string> allowed = {
      "zip", "7z",   "rar", "tar", "gz",  "bz2", "xz",  "iso",
      "bin", "cue",  "rom", "nes", "sfc", "smc", "n64", "z64",
      "v64", "gba",  "gb",  "gbc", "nds", "chd", "wad", "cia",
      "rvz", "wbfs", "a26", "a52", "atr", "md",  "3ds"};
  static const std::set<std::string> blocked = {
      "jpg", "jpeg", "png", "gif",  "bmp", "webp", "svg", "ico", "html",
      "htm", "php",  "asp", "aspx", "jsp", "xml",  "js",  "css"};
  auto ext = fileExt(url);
  if (ext.empty() || blocked.count(ext))
    return false;
  return allowed.count(ext) > 0;
}

// ─── fetch URL via curl ────────────────────────────────────────────────────
static bool fetchUrl(const std::string &url, std::string &out) {
  // quote the url for the shell
  std::string q = "'";
  for (char c : url) {
    if (c == '\'')
      q += "'\"'\"'";
    else
      q += c;
  }
  q += "'";
  std::string cmd = "curl --location --fail --silent --show-error " + q;
  FILE *f = popen(cmd.c_str(), "r");
  if (!f)
    return false;
  char buf[8192];
  std::ostringstream ss;
  while (fgets(buf, sizeof(buf), f))
    ss << buf;
  int rc = pclose(f);
  out = ss.str();
  return rc == 0;
}

// ─── parse hrefs from HTML ─────────────────────────────────────────────────
static std::string urlDecode(const std::string &s) {
  std::string r;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) &&
        isxdigit((unsigned char)s[i + 2])) {
      r += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else if (s[i] == '+') {
      r += ' ';
    } else {
      r += s[i];
    }
  }
  return r;
}

// Infer system name from keywords in the archive.org page URL / identifier.
static std::string detectSystem(const std::string &pageUrl) {
  struct {
    const char *kw;
    const char *sys;
  } hints[] = {{"nintendo-entertainment-system", "NES"},
               {"no-intro-nes", "NES"},
               {"nesdb", "NES"},
               {"-nes-", "NES"},
               {"_nes_", "NES"},
               {"super-nintendo", "SNES"},
               {"snes", "SNES"},
               {"game-boy-color", "GBC"},
               {"gameboy-color", "GBC"},
               {"gbc", "GBC"},
               {"game-boy-advance", "GBA"},
               {"gameboy-advance", "GBA"},
               {"gba", "GBA"},
               {"game-boy", "GB"},
               {"gameboy", "GB"},
               {"-gb-", "GB"},
               {"_gb_", "GB"},
               {"nintendo-ds", "NDS"},
               {"nds", "NDS"},
               {"nintendo-64", "N64"},
               {"n64", "N64"},
               {"atari-2600", "Atari2600"},
               {"atari2600", "Atari2600"},
               {"atari-5200", "Atari5200"},
               {"atari5200", "Atari5200"},
               {"atari-800", "Atari800"},
               {"atari800", "Atari800"},
               {"sega-genesis", "SegaGenesis"},
               {"megadrive", "SegaGenesis"},
               {"genesis", "SegaGenesis"},
               {"3ds", "3DS"},
               {"nintendo-3ds", "3DS"},
               {nullptr, nullptr}};
  std::string low = pageUrl;
  std::transform(low.begin(), low.end(), low.begin(), ::tolower);
  for (auto *h = hints; h->kw; ++h)
    if (low.find(h->kw) != std::string::npos)
      return h->sys;
  return "Unknown";
}

static std::vector<std::string> extractLinks(const std::string &pageUrl,
                                             const std::string &html) {
  std::vector<std::string> links;
  std::set<std::string> seen;

  // If this is an archive.org download or details page, build the canonical
  // download base (with trailing slash) so relative file hrefs resolve to
  // download/IDENTIFIER/FILENAME instead of download/FILENAME.
  std::string archiveBase;
  {
    std::regex arcRe(R"(https?://archive\.org/(?:download|details)/([^/?#]+))");
    std::smatch arcM;
    if (std::regex_search(pageUrl, arcM, arcRe))
      archiveBase = "https://archive.org/download/" + arcM[1].str() + "/";
  }

  std::regex re(R"(href\s*=\s*["']([^"']+)["'])", std::regex::icase);
  for (auto it = std::sregex_iterator(html.begin(), html.end(), re);
       it != std::sregex_iterator(); ++it) {
    std::string href = (*it)[1].str();
    if (href.empty() || href[0] == '#')
      continue;
    if (href.rfind("mailto:", 0) == 0 || href.rfind("javascript:", 0) == 0)
      continue;

    std::string resolved = resolveUrl(pageUrl, href);
    if (resolved.rfind("http", 0) != 0)
      continue;
    if (!isRomLink(resolved))
      continue;

    // Fix malformed archive.org URL: download/FILENAME (missing identifier).
    // This happens when the page URL has no trailing slash and relative hrefs
    // are bare filenames; resolveUrl strips the identifier as a path segment.
    if (!archiveBase.empty()) {
      std::regex malRe(
          R"(https?://archive\.org/download/[^/?#]+\.[a-zA-Z0-9]+$)");
      if (std::regex_match(resolved, malRe))
        resolved = archiveBase + fileName(resolved);
    }

    if (seen.insert(resolved).second)
      links.push_back(resolved);
  }
  return links;
}

int main(int argc, char **argv) {
  // Parse flags
  bool dryRun = false;
  std::string url;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dry-run" || arg == "-n") {
      dryRun = true;
    } else if (url.empty()) {
      url = arg;
    }
  }

  // ── Oracle DB config (skip when dry-running) ──────────────────────────
  OracleDBConfig cfg;
  if (!dryRun) {
    // Hardcoded OracleDBConfig for testing
    cfg.host = "147.224.62.87"; // updated to public DB IP
    cfg.port = 13306;
    cfg.user = "omnicon";
    cfg.password = "Geckoboy07!1word";
    cfg.service = "omnicon";

    if (cfg.host.empty()) {
      std::cerr << "Aborting: no Oracle host configured.\n"
                << "Set ORACLE_APP_HOST env var or enter when prompted.\n"
                << "Use --dry-run to test URL parsing without a DB.\n";
      return 1;
    }

    if (!createGamesTable(cfg)) {
      std::cerr
          << "Failed to ensure games table exists.\n"
          << "Check Oracle host/credentials and that sqlplus is installed.\n"
          << "Use --dry-run to test URL parsing without a DB.\n";
      return 1;
    }
    std::cout << "Games table ready.\n";
  } else {
    std::cout << "[dry-run] Skipping Oracle DB setup.\n";
  }

  // ── URL to load ───────────────────────────────────────────────────────
  if (url.empty()) {
    std::cout << "Enter a URL to load ROM links from: ";
    std::getline(std::cin, url);
  }
  if (url.empty()) {
    std::cout << "No URL provided, exiting.\n";
    return 0;
  }

  std::cout << "Fetching " << url << " ...\n";
  std::string html;
  if (!fetchUrl(url, html)) {
    std::cerr << "Failed to fetch URL (is curl installed?).\n";
    return 1;
  }
  std::cout << "Fetched " << html.size() << " bytes.\n";

  auto links = extractLinks(url, html);
  if (links.empty()) {
    std::cout << "No downloadable ROM links found on that page.\n";
    return 0;
  }
  std::cout << "Found " << links.size() << " ROM link(s).\n";

  if (dryRun) {
    std::cout << "\n[dry-run] Links that would be inserted:\n";
    for (const auto &link : links)
      std::cout << "  " << fileName(link) << "\n    -> " << link << "\n";
    return 0;
  }

  std::string systemName = detectSystem(url);
  std::cout << "Detected system: " << systemName << "\n";

  std::cout << "Inserting into DB...\n";
  int ok = 0, fail = 0;
  for (const auto &link : links) {
    // Decode the percent-encoded filename and strip the archive extension
    // so the NAME field is human-readable, e.g. "10-Yard Fight (USA, Europe)".
    std::string rawFile =
        fileName(link); // "10-Yard%20Fight%20%28USA%2C%20Europe%29.zip"
    std::string decoded =
        urlDecode(rawFile); // "10-Yard Fight (USA, Europe).zip"
    auto dot = decoded.rfind('.');
    std::string name =
        (dot != std::string::npos) ? decoded.substr(0, dot) : decoded;
    if (insertGameEntry(cfg, name, link, "", systemName, 0)) {
      std::cout << "  [OK] " << name << "\n";
      ++ok;
    } else {
      std::cerr << "  [FAIL] " << name << "\n";
      ++fail;
    }
  }
  std::cout << "Done: " << ok << " inserted, " << fail << " failed.\n";
  return fail > 0 ? 1 : 0;
}
