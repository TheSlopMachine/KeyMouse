# KeyMouse - MinGW-w64 build.
#
#   make build                              -> build/KeyMouse.exe
#   make build DEBUG=1                      unoptimised, with symbols
#   make build CROSS=x86_64-w64-mingw32-    cross-compile from Linux/macOS
#   make clean

CROSS   ?=
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres

TARGET  := KeyMouse
BUILD   := build
SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
RESOBJ  := $(BUILD)/app_res.o
EXE     := $(BUILD)/$(TARGET).exe

CFLAGS  := -std=c11 -Wall -Wextra -MMD -MP -Isrc
LDFLAGS := -mwindows -static
LDLIBS  := -luser32 -lshell32 -ladvapi32 -lwinmm

ifeq ($(DEBUG),1)
  CFLAGS += -O0 -g -DDEBUG
else
  CFLAGS  += -O2 -DNDEBUG
  LDFLAGS += -s
endif

# Directory creation / removal that works in cmd.exe and in POSIX shells.
ifeq ($(OS),Windows_NT)
  ifeq ($(findstring sh,$(notdir $(SHELL))),)
    RMDIR = if exist $(BUILD) rmdir /s /q $(BUILD)
  else
    RMDIR = rm -rf $(BUILD)
  endif
else
  RMDIR = rm -rf $(BUILD)
endif

# The output dir shares its name with the 'build' target, so create it at parse
# time (a plain 'mkdir' works in both cmd.exe and sh).
$(if $(wildcard $(BUILD)),,$(shell mkdir $(BUILD)))

.PHONY: all build clean
all: build
build: $(EXE)

$(EXE): $(OBJS) $(RESOBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(RESOBJ): res/app.rc res/app.manifest res/app.ico res/idle.ico res/active.ico src/resource.h
	$(WINDRES) -Isrc -O coff -i res/app.rc -o $@

clean:
	$(RMDIR)

-include $(OBJS:.o=.d)
