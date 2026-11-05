#pragma once

#include <string>

// Thread-safe one-line message shown in the shell's info bar for a few seconds.
void postShellMessage(const std::string &message);

// Returns true and fills `out` while a message is still fresh.
bool currentShellMessage(std::string &out);
