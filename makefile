CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -pedantic
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
TARGET := omnicon_repl
SOURCES := main.cpp wrapper.cpp NES.cpp GameBoy.cpp SNES.cpp Atari800.cpp agnes.c
SAMEBOY_BUILD := third_party/SameBoy/build/lib/libsameboy.a
SAMEBOY_CORE_SOURCES := $(wildcard third_party/SameBoy/Core/*.c)
SAMEBOY_BOOTROMS := \
	third_party/SameBoy/build/bin/BootROMs/dmg_boot.bin \
	third_party/SameBoy/build/bin/BootROMs/mgb_boot.bin \
	third_party/SameBoy/build/bin/BootROMs/cgb0_boot.bin \
	third_party/SameBoy/build/bin/BootROMs/cgb_boot.bin \
	third_party/SameBoy/build/bin/BootROMs/agb_boot.bin \
	third_party/SameBoy/build/bin/BootROMs/sgb_boot.bin \
	third_party/SameBoy/build/bin/BootROMs/sgb2_boot.bin

.PHONY: all run clean reset save

all: $(TARGET)


$(SAMEBOY_BUILD): $(SAMEBOY_CORE_SOURCES)
	$(MAKE) -C third_party/SameBoy CONF=release build/lib/libsameboy.a

$(SAMEBOY_BOOTROMS):
	$(MAKE) -C third_party/SameBoy CONF=release bootroms


$(TARGET): $(SOURCES) NES.hpp top.hpp agnes.h $(SAMEBOY_BUILD) $(SAMEBOY_BOOTROMS)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SOURCES) -o $(TARGET) $(SAMEBOY_BUILD) $(SDL_LIBS) -ldl -lm

run: $(TARGET)
	@. ~/.oci/omnicon_env && ./$(TARGET)

clean:
	rm -f $(TARGET)
	$(MAKE) -C third_party/SameBoy clean
reset:
	make clean
	clear
	make
	make run
save:
	git add .
	git commit -m "save"
	git push