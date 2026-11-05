#pragma once
#include "ConsoleLaunchers.hpp"
#include "ExtraSystems.hpp"

// Launches a game through the MAME executable, either as an arcade romset or
// as a console machine with the content attached as media.
class MameConsoleLauncher final : public ConsoleLauncher {
public:
    explicit MameConsoleLauncher(const ExtraSystemInfo& info) : info_(info) {}
    bool launch(const std::string& contentPath) const override;

private:
    const ExtraSystemInfo& info_;
};
