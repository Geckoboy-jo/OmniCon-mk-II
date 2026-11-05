#!/bin/bash
# SDL2 Setup Script for OmniCon NES Emulator

echo "Installing SDL2 development libraries..."

# Detect package manager
if command -v apt-get &> /dev/null; then
    echo "Using apt-get..."
    sudo apt-get update
    sudo apt-get install -y libsdl2-dev libsdl2-2.0-0
elif command -v yum &> /dev/null; then
    echo "Using yum..."
    sudo yum install -y SDL2-devel SDL2
elif command -v pacman &> /dev/null; then
    echo "Using pacman..."
    sudo pacman -S sdl2
elif command -v brew &> /dev/null; then
    echo "Using brew..."
    brew install sdl2
else
    echo "ERROR: Could not detect package manager"
    echo "Please install SDL2 development libraries manually:"
    echo "  Ubuntu/Debian: sudo apt-get install libsdl2-dev"
    echo "  Fedora/RHEL:   sudo yum install SDL2-devel"
    echo "  Arch:          sudo pacman -S sdl2"
    echo "  macOS:         brew install sdl2"
    exit 1
fi

echo "SDL2 installation complete!"
echo "Now run: make clean && make"
