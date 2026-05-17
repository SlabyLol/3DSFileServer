"""
setup_build.py — run before make.
Generates:
  - icon.png  : minimal 48x48 RGB PNG required by smdhtool
  - Makefile  : written with real tab characters (avoids git/editor corruption)
"""
import struct
import zlib

# ── icon.png ──────────────────────────────────────────────────────────────────

def png_chunk(tag, data):
    s = struct.pack('>I', len(data)) + tag + data
    return s + struct.pack('>I', zlib.crc32(s[4:]) & 0xffffffff)

# 48x48 solid dark-blue image, filter byte 0x00 per row
row = b'\x00' + bytes([0x33, 0x33, 0x44] * 48)
raw = row * 48

png = (
    b'\x89PNG\r\n\x1a\n'
    + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 48, 48, 8, 2, 0, 0, 0))
    + png_chunk(b'IDAT', zlib.compress(raw))
    + png_chunk(b'IEND', b'')
)

with open('icon.png', 'wb') as f:
    f.write(png)
print('icon.png written OK')

# ── Makefile ──────────────────────────────────────────────────────────────────

T = '\t'  # real tab

makefile = (
    'APP_TITLE       := 3DS File Server\n'
    'APP_DESCRIPTION := WiFi File Manager\n'
    'APP_AUTHOR      := 3DSFileServer\n'
    '\n'
    'TARGET  := 3ds-fileserver\n'
    'BUILD   := build\n'
    'SOURCES := source\n'
    '\n'
    'DEVKITPRO ?= /opt/devkitpro\n'
    'DEVKITARM ?= $(DEVKITPRO)/devkitARM\n'
    'export PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)\n'
    '\n'
    'PREFIX  := arm-none-eabi-\n'
    'CC      := $(PREFIX)gcc\n'
    'AS      := $(PREFIX)as\n'
    'AR      := $(PREFIX)ar\n'
    'OBJCOPY := $(PREFIX)objcopy\n'
    'CTRULIB ?= $(DEVKITPRO)/libctru\n'
    '\n'
    'ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft\n'
    'CFLAGS  := -g -Wall -O2 -mword-relocations -fomit-frame-pointer'
    ' -ffunction-sections \\\n'
    + T + T + '$(ARCH) -D__3DS__ -I$(SOURCES) -I$(CTRULIB)/include\n'
    'LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib\n'
    'LIBS    := -lctru -lm\n'
    '\n'
    'CFILES  := $(wildcard $(SOURCES)/*.c)\n'
    'OFILES  := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))\n'
    '\n'
    '.PHONY: all clean\n'
    '\n'
    'all: $(TARGET).3dsx\n'
    '\n'
    '$(BUILD):\n'
    + T + 'mkdir -p $(BUILD)\n'
    '\n'
    '$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)\n'
    + T + '$(CC) $(CFLAGS) -c -o $@ $<\n'
    '\n'
    '$(TARGET).elf: $(OFILES)\n'
    + T + '$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)\n'
    '\n'
    '$(TARGET).smdh: icon.png\n'
    + T + 'smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)"'
          ' "$(APP_AUTHOR)" icon.png $@\n'
    '\n'
    '$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh\n'
    + T + '3dsxtool $< $@ --smdh=$(TARGET).smdh\n'
    '\n'
    'clean:\n'
    + T + 'rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf icon.png\n'
)

with open('Makefile', 'w') as f:
    f.write(makefile)

# Verify tabs are present
assert '\t' in makefile, 'BUG: no tabs in generated Makefile!'
print('Makefile written OK')
