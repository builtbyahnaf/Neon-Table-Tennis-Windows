# =============================================================================
# Makefile — Neon Table Tennis
# =============================================================================
# Production build configuration for MinGW-w64 on Windows.
#
# USAGE:
#   mingw32-make                  # Builds release binary (neon_table_tennis.exe)
#   mingw32-make BUILD=debug      # Builds debug binary with symbols (-g3, -O0)
#   mingw32-make clean            # Removes built executable
#   mingw32-make run              # Builds and executes the game
# =============================================================================

CC      ?= gcc
BUILD   ?= release
TARGET  := neon_table_tennis.exe

# -----------------------------------------------------------------------------
# Dependency Search Paths (configurable via environment or make args)
# -----------------------------------------------------------------------------
RAYLIB_PATH ?= C:/raylib/raylib
MSYS_PATH   ?= C:/msys64/ucrt64

# Source files
SRC := src/main.c \
       src/core/core.c \
       src/engine/engine.c \
       src/physics/physics.c \
       src/animation/anim_engine.c \
       src/database/db_handler.c \
       src/audio/audio_mgr.c \
       src/ui/ui_system.c

# Preprocessor flags
CPPFLAGS += -Iinclude \
            -I$(RAYLIB_PATH)/src \
            -I$(MSYS_PATH)/include \
            -D_CRT_SECURE_NO_WARNINGS

# Compiler flags
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

ifeq ($(BUILD),release)
    CFLAGS += -O3 -DNDEBUG
else
    CFLAGS += -O0 -g3 -DDEBUG
endif

# Linker library paths and dependencies
LDFLAGS += -L$(RAYLIB_PATH)/src \
           -L$(MSYS_PATH)/lib

LDLIBS  += -lraylib -lsqlite3 -lopengl32 -lgdi32 -lwinmm -lm

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	cmd /c if exist $(TARGET) del /f /q $(TARGET)

.PHONY: all run clean
