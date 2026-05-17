APP_TITLE       := 3DS File Server
APP_DESCRIPTION := WiFi File Manager
APP_AUTHOR      := DarkFox

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

CTRULIB ?= $(DEVKITPRO)/libctru

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -O2 -mword-relocations \
           -fomit-frame-pointer -ffunction-sections \
           $(ARCH) -D__3DS__ \
           -I$(SOURCES) -I$(CTRULIB)/include

LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib
LIBS    := -lctru -lm

#---------------------------------------------------------------------------------
# Sources
#---------------------------------------------------------------------------------
CFILES  := $(wildcard $(SOURCES)/*.c)
OFILES  := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean

all: $(TARGET).3dsx

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# smdhtool requires: title, description, author, icon.png, output.smdh
# Generate a minimal 48x48 grey PNG icon on the fly with ImageMagick
$(TARGET).smdh: $(TARGET).elf
	convert -size 48x48 xc:#333344 icon.png 2>/dev/null || \
	  python3 -c "
import struct, zlib
def png(w,h,data):
    def chunk(t,d): s=struct.pack('>I',len(d))+t+d; return s+struct.pack('>I',zlib.crc32(s[4:])&0xffffffff)
    raw=b''.join(b'\x00'+bytes([0x33,0x33,0x44]*w) for _ in range(h))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
open('icon.png','wb').write(png(48,48,None))
"
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh

clean:
	rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf icon.png
