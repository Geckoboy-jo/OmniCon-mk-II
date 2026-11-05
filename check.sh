#!/bin/bash
cd /mnt/c/Users/midge/Documents/GitHub/OmniCon-mk-II
g++ -std=c++17 -Wall -Wextra -Imgba -I. -Isource $(sdl2-config --cflags) $(mysql_config --cflags) -fsyntax-only \
  source/MinecraftConsoleLauncher.cpp \
  source/EpicGamesConsoleLauncher.cpp \
  source/GameShell.cpp \
  source/GameLibrary.cpp \
  source/ExtraSystems.cpp \
  source/main.cpp 2> build_check.log
echo "exit=$?"
echo "--- errors ---"
grep "error:" build_check.log || echo "(none)"
echo "--- warnings (excluding pre-existing) ---"
grep "warning:" build_check.log | grep -vE "gb\.h|Wunused-result" || echo "(none)"
