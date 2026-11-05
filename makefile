CC := g++
CFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic -Imgba -I. -Isource
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || echo "-I/usr/include/SDL2")
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null || echo "-lSDL2")
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null || echo "-I/usr/include/mysql")
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null || echo "-lmysqlclient")
SAMEBOY_LIB := third_party/SameBoy/build/lib/libsameboy.a
TARGET := omnicon_repl
SOURCES := $(filter-out source/oracle_test.cpp,$(wildcard source/*.cpp)) \
           $(wildcard source/*.c)

.PHONY: all run clean reset save bootroms cdecrypt deps



$(TARGET): $(SOURCES) | bootroms
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(MYSQL_CFLAGS) $(SOURCES) -o $(TARGET) $(SAMEBOY_LIB) $(SDL_LIBS) -lSDL2_ttf -lSDL2_image -ldl -lmgba $(MYSQL_LIBS) -lX11 -lXi -pthread

deps:
	bash scripts/install_deps.sh --build

bootroms: deps
	$(MAKE) -C third_party/SameBoy bootroms

cdecrypt:
	$(MAKE) -C third_party/cdecrypt

oracle_test: \source/OracleDB.cpp \source/oracle_test.cpp \source/OracleDB.hpp \source/OracleDBConfig.hpp \source/OracleGameRow.hpp
	$(CC) $(CFLAGS) $(MYSQL_CFLAGS) \source/OracleDB.cpp \source/oracle_test.cpp \-o oracle_test $(MYSQL_LIBS)

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