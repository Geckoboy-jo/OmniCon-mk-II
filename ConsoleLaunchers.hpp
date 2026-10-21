#pragma once

#include "top.hpp"
#include "RetroArchConsoleLauncher.hpp"
class ConsoleLauncher {
public:
    virtual ~ConsoleLauncher() = default;
    virtual bool launch(const std::string& contentPath) const = 0;

protected:
    static bool launchExternalApplication(const std::string& launcher,
                                          const std::vector<std::string>& arguments);
};