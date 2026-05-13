CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -pedantic
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
SAMEBOY_LIB := third_party/SameBoy/build/lib/libsameboy.a
TARGET := omnicon_repl
SOURCES := main.cpp NES.cpp SNES.cpp GameBoy.cpp GameBoyColor.cpp Atari2600.cpp Atari5200.cpp GBA.cpp NDS.cpp agnes.c

.PHONY: all run clean reset save



$(TARGET): $(SOURCES) NES.hpp SNES.hpp GameBoy.hpp GameBoyColor.hpp Atari2600.hpp Atari5200.hpp GBA.hpp NDS.hpp top.hpp agnes.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SOURCES) -o $(TARGET) $(SAMEBOY_LIB) $(SDL_LIBS) -ldl -lmgba

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