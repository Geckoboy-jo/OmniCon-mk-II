#ifndef NINTENDO_SWITCH_HPP
#define NINTENDO_SWITCH_HPP
#include "ConsoleLaunchers.hpp"

class NintendoSwitchConsoleLauncher final : public ConsoleLauncher {
public:
  bool launch(const std::string &contentPath) const override;
};

#endif // NINTENDO_SWITCH_HPP