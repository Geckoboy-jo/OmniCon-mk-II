#pragma once
#include "ConsoleLaunchers.hpp"

// Launches an Xbox Cloud Gaming title (Xbox One or Xbox Series X|S library
// entry) in the browser. contentPath is the Microsoft Store product id,
// optionally suffixed ":store" to open the Store page of a game that cannot be
// streamed, or kAddGamesId to open the Xbox Cloud Gaming home page and the
// Xbox Store (some games are not streamable).
inline constexpr const char* kAddGamesId = "ADDMORE";

class XboxCloudConsoleLauncher final : public ConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};
