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

























class AtariLynxConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class AtariJaguarConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class PCEngineConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class ColecoVisionConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class IntellivisionConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};
