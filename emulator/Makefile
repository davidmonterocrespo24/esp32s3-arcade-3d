# Default Raylib path. Change this if your Raylib installation is different.
RAYLIB_PATH ?= C:/raylib

# Compiler settings
CC = g++
CFLAGS = -I. -I.. -I$(RAYLIB_PATH)/include -O2 -std=c++17 -D_WIN32 -Wno-narrowing
LDFLAGS = -L$(RAYLIB_PATH)/lib -lraylib -lopengl32 -lgdi32 -lwinmm -static-libgcc -static-libstdc++

# Source files
SRCS = main.cpp \
       Arduino.cpp \
       TFT_eSPI.cpp \
       car_game_wrapper.cpp \
       ../colors.cpp \
       ../utils.cpp \
       ../track.cpp \
       ../rendering.cpp \
       ../physics.cpp

# Object files (place them in emulator folder to avoid cluttering parent)
OBJS = $(notdir $(SRCS:.cpp=.o))

# VPATH is used to look for source files in parent directory
VPATH = ..

all: car_game_emu.exe

car_game_emu.exe: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Rule to compile cpp files
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	del *.o car_game_emu.exe
