CC := g++
CFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic -Imgba -I.
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null || echo "-I/usr/include/mysql")
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null || echo "-lmysqlclient")
SAMEBOY_LIB := third_party/SameBoy/build/lib/libsameboy.a
TARGET := omnicon_repl
SOURCES := main.cpp NES.cpp SNES.cpp GameBoy.cpp GameBoyColor.cpp Atari2600.cpp Atari5200.cpp Atari800.cpp GBA.cpp NDS.cpp 3DS.cpp agnes.c GameLibrary.cpp GameShell.cpp Macro.cpp MacroStudio.cpp Odyssey.cpp SegaGenesis.cpp N64.cpp PS1.cpp PS2.cpp PS3.cpp GameCube.cpp SaveManager.cpp OracleDB.cpp DownloadManager.cpp ConsoleLaunchers.cpp SegaMasterSystemConsoleLauncher.cpp GameGearConsoleLauncher.cpp SegaSaturnConsoleLauncher.cpp WiiConsoleLauncher.cpp WiiUConsoleLauncher.cpp NintendoSwitchConsoleLauncher.cpp PSPConsoleLauncher.cpp PSVitaConsoleLauncher.cpp XboxConsoleLauncher.cpp Xbox360ConsoleLauncher.cpp XboxCloudConsoleLauncher.cpp Atari7800ConsoleLauncher.cpp AtariLynxConsoleLauncher.cpp AtariJaguarConsoleLauncher.cpp PCEngineConsoleLauncher.cpp ColecoVisionConsoleLauncher.cpp IntellivisionConsoleLauncher.cpp ExtraSystems.cpp MameConsoleLauncher.cpp ShellMessage.cpp WiiUConverter.cpp

.PHONY: all run clean reset save bootroms cdecrypt deps



$(TARGET): $(SOURCES) NES.hpp SNES.hpp GameBoy.hpp GameBoyColor.hpp Atari2600.hpp Atari5200.hpp Atari800.hpp GBA.hpp NDS.hpp 3DS.hpp SegaGenesis.hpp N64.hpp PS1.hpp PS2.hpp PS3.hpp GameCube.hpp GameLibrary.hpp GameShell.hpp Macro.hpp ConsoleLaunchers.hpp WiiUConverter.hpp top.hpp agnes.h \
	SystemType.hpp GameEntry.hpp WiiUConverterResult.hpp \
	n3ds_input_t.hpp n3ds_color_t.hpp atari2600_input_t.hpp atari2600_color_t.hpp \
	atari5200_input_t.hpp atari5200_color_t.hpp atari800_input_t.hpp atari800_color_t.hpp \
	gba_input_t.hpp n64_input_t.hpp nds_input_t.hpp nds_color_t.hpp \
	ps1_input_t.hpp ps1_color_t.hpp ps2_input_t.hpp ps2_color_t.hpp ps3_input_t.hpp ps3_color_t.hpp \
	gamecube_input_t.hpp gamecube_color_t.hpp genesis_input_t.hpp genesis_color_t.hpp \
	ExtraSystems.hpp MameConsoleLauncher.hpp ShellMessage.hpp ExtraConsoleLaunchers.hpp RetroArchConsoleLauncher.hpp | bootroms
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(MYSQL_CFLAGS) $(SOURCES) -o $(TARGET) $(SAMEBOY_LIB) $(SDL_LIBS) -lSDL2_ttf -lSDL2_image -ldl -lmgba $(MYSQL_LIBS) -lX11 -lXi -pthread

deps:
	bash scripts/install_deps.sh --build

bootroms: deps
	$(MAKE) -C third_party/SameBoy bootroms

cdecrypt:
	$(MAKE) -C third_party/cdecrypt

oracle_test: OracleDB.cpp oracle_test.cpp OracleDB.hpp OracleDBConfig.hpp OracleGameRow.hpp
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