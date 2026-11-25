# PC builds of the game.
#   make            -> car_game_emu.exe  (Raylib window, Windows/MinGW)
#   make headless   -> car_game_headless (no window; writes PPM frames)
#
# Both compile the SAME game sources as the ESP32; only Arduino.cpp,
# host_io.cpp and the frontend (main*.cpp) are PC-specific.

# Default Raylib path. Change this if your Raylib installation is different.
RAYLIB_PATH ?= C:/raylib

CXX      ?= g++
CXXFLAGS  = -I. -I.. -O2 -std=c++17 -Wall -Wno-unused-function -Wno-unused-variable

GAME_SRCS = ../gfx.cpp \
            ../colors.cpp \
            ../utils.cpp \
            ../track.cpp \
            ../physics.cpp \
            ../rendering.cpp \
            ../render_road.cpp \
            ../render_building.cpp \
            ../render_player.cpp \
            ../render_traffic.cpp \
            ../render_hud.cpp

HOST_SRCS = Arduino.cpp host_io.cpp car_game_wrapper.cpp

all: car_game_emu.exe

car_game_emu.exe: main.cpp $(HOST_SRCS) $(GAME_SRCS) ../*.h ../car_game.ino
	$(CXX) $(CXXFLAGS) -I$(RAYLIB_PATH)/include main.cpp $(HOST_SRCS) $(GAME_SRCS) -o $@ \
	  -L$(RAYLIB_PATH)/lib -lraylib -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++

headless: car_game_headless

car_game_headless: main_headless.cpp $(HOST_SRCS) $(GAME_SRCS) ../*.h ../car_game.ino
	$(CXX) $(CXXFLAGS) main_headless.cpp $(HOST_SRCS) $(GAME_SRCS) -o $@

clean:
	-rm -f car_game_emu.exe car_game_headless car_game_headless.exe *.o

.PHONY: all headless clean
