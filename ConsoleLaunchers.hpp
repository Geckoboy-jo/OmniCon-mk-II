#pragma once

#include <string>
#include <vector>

class ConsoleLauncher {
public:
    virtual ~ConsoleLauncher() = default;
    virtual bool launch(const std::string& contentPath) const = 0;

protected:
    static bool launchExternalApplication(const std::string& launcher,
                                          const std::vector<std::string>& arguments);
};

class RetroArchConsoleLauncher : public ConsoleLauncher {
protected:
    static bool launchRetroArch(const std::string& systemName,
                                const char* coreEnvironmentVariable,
                                const char* defaultCore,
                                const std::string& contentPath);
};

class SegaMasterSystemConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class GameGearConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class SegaSaturnConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class WiiConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class WiiUConsoleLauncher final : public ConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class NintendoSwitchConsoleLauncher final : public ConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class PSPConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class PSVitaConsoleLauncher final : public ConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class XboxConsoleLauncher final : public ConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class Xbox360ConsoleLauncher final : public ConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
};

class Atari7800ConsoleLauncher final : public RetroArchConsoleLauncher {
public:
    bool launch(const std::string& contentPath) const override;
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
