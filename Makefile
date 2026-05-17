APP_TITLE       := 3DS File Server
APP_DESCRIPTION := WiFi File Manager
APP_AUTHOR      := 3DSFileServer

TARGET  := 3ds-fileserver
BUILD   := build
SOURCES := source

#---------------------------------------------------------------------------------
# devkitARM cross-compiler
#---------------------------------------------------------------------------------
DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
export PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)

PREFIX  := arm-none-eabi-
CC      := $(PREFIX)gcc
CXX     := $(PREFIX)g++
AS      := $(PREFIX)as
AR      := $(PREFIX)ar
OBJCOPY := $(PREFIX)objcopy
STRIP   := $(PREFIX)strip

CTRULIB ?= $(DEVKITPRO)/libctru

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -g -Wall -O2 -mword-relocations \
             -fomit-frame-pointer -ffunction-sections \
             $(ARCH) -D__3DS__ \
             -I$(SOURCES) -I$(CTRULIB)/include

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := -g $(ARCH)
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) \
             -L$(CTRULIB)/lib -lctru -lm

#---------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------
CFILES   := $(wildcard $(SOURCES)/*.c)
OFILES   := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean

all: $(BUILD) $(TARGET).3dsx

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).smdh: $(TARGET).elf
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh

clean:
	rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf
