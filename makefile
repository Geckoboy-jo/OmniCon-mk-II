CC := g++
CFLAGS := -std=c++17 -Wall -Wextra -pedantic -Imgba -Imupen64plus
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null || echo "-I/usr/include/mysql")
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null || echo "-lmysqlclient")
SAMEBOY_LIB := third_party/SameBoy/build/lib/libsameboy.a
TARGET := omnicon_repl
SOURCES := main.cpp NES.cpp SNES.cpp GameBoy.cpp GameBoyColor.cpp Atari2600.cpp Atari5200.cpp Atari800.cpp GBA.cpp NDS.cpp 3DS.cpp agnes.c GameLibrary.cpp GameShell.cpp Odyssey.cpp SegaGenesis.cpp N64.cpp PS1.cpp PS2.cpp GameCube.cpp SaveManager.cpp OracleDB.cpp DownloadManager.cpp

.PHONY: all run clean reset save



$(TARGET): $(SOURCES) NES.hpp SNES.hpp GameBoy.hpp GameBoyColor.hpp Atari2600.hpp Atari5200.hpp Atari800.hpp GBA.hpp NDS.hpp 3DS.hpp SegaGenesis.hpp N64.hpp PS1.hpp PS2.hpp GameCube.hpp GameLibrary.hpp GameShell.hpp top.hpp agnes.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(MYSQL_CFLAGS) $(SOURCES) -o $(TARGET) $(SAMEBOY_LIB) $(SDL_LIBS) -lSDL2_ttf -lSDL2_image -ldl -lmgba $(MYSQL_LIBS) -pthread

oracle_test: OracleDB.cpp oracle_test.cpp OracleDB.hpp
	$(CC) $(CFLAGS) $(MYSQL_CFLAGS) OracleDB.cpp oracle_test.cpp -o oracle_test $(MYSQL_LIBS)

run: $(TARGET)
	./$(TARGET)

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
	git push --force