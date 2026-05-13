CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -pedantic
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
TARGET := omnicon_repl
SOURCES := main.cpp NES.cpp SNES.cpp Atari2600.cpp agnes.c

.PHONY: all run clean reset save



$(TARGET): $(SOURCES) NES.hpp SNES.hpp Atari2600.hpp top.hpp agnes.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SOURCES) -o $(TARGET) $(SDL_LIBS)

run: $(TARGET)
	@. ~/.oci/omnicon_env && ./$(TARGET)

clean:
	rm -f $(TARGET)
reset:
	make clean
	clear
	make
	make run
save:
	git add .
	git commit -m "save"
	git push