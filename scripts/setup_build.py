import base64, struct, zlib

# icon.png — custom 48x48 pixel-art 3DS console icon
ICON_B64 = (
    "iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAABTklEQVR4nO3WoQ7CMBAG"
    "4OoZEoIgSBSZmEIiUQTJM+B5AbAoFAISXgCB4RmQkzg8aMIDjMKW0pS23C6lvSUjP4Rs"
    "yfrtem3K2DmjFf7tdJI8cTwKEgGoCMjnBEFBUdT2EAzosWsqIQdySESCrvOuJTXoDcp0"
    "n7pC5gpZQKb9BgMSt9EVMo1tuY4EmRpIBlkqYbqLBEH2IXk8tk+VmEwYUOk+lQSmK75B"
    "r9/FVgS/ypyAipE4xdpSvkHajgkMAqKhIG+pzhG21eoFD21QqMlSJq46IJ9LDArytgmF"
    "B93GAx5CoHIVylfgn0BJOuMRoPupwRMSVK5C3zuV6Vmr4UEOm0zl/KT0Nxcex6DiLSdT"
    "8f/z6gCT4x5SK7Q+ygGa3Dd1PqpSIfcgyLJXKhTBzod/BGmfTguESA1yDqJ1/AgSKiCW"
    "LXkIgchVSA8ikgJEKk8OaYoZtvvNhgAAAABJRU5ErkJggg=="
)
open("icon.png", "wb").write(base64.b64decode(ICON_B64))
print("icon.png OK")

# Makefile — tabs via chr(9) to avoid editor/git corruption
T = chr(9)
lines = [
    "APP_TITLE       := 3DS File Server",
    "APP_DESCRIPTION := WiFi File Manager and something",
    "APP_AUTHOR      := DarkFox Co.",
    "",
    "TARGET  := 3ds-fileserver",
    "BUILD   := build",
    "SOURCES := source",
    "",
    "DEVKITPRO ?= /opt/devkitpro",
    "DEVKITARM ?= $(DEVKITPRO)/devkitARM",
    "export PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)",
    "",
    "PREFIX  := arm-none-eabi-",
    "CC      := $(PREFIX)gcc",
    "AS      := $(PREFIX)as",
    "AR      := $(PREFIX)ar",
    "OBJCOPY := $(PREFIX)objcopy",
    "CTRULIB ?= $(DEVKITPRO)/libctru",
    "",
    "ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft",
    "CFLAGS  := -g -Wall -O2 -mword-relocations -fomit-frame-pointer -ffunction-sections \\",
    T+T+"$(ARCH) -D__3DS__ -I$(SOURCES) -I$(CTRULIB)/include",
    "LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib",
    "LIBS    := -lctru -lm",
    "",
    "CFILES  := $(wildcard $(SOURCES)/*.c)",
    "OFILES  := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))",
    "",
    ".PHONY: all clean",
    "",
    "all: $(TARGET).3dsx",
    "",
    "$(BUILD):",
    T+"mkdir -p $(BUILD)",
    "",
    "$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)",
    T+"$(CC) $(CFLAGS) -c -o $@ $<",
    "",
    "$(TARGET).elf: $(OFILES)",
    T+"$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)",
    "",
    "$(TARGET).smdh: icon.png",
    T+'smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" icon.png $@',
    "",
    "$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh",
    T+"3dsxtool $< $@ --smdh=$(TARGET).smdh",
    "",
    "clean:",
    T+"rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf icon.png",
]
open("Makefile", "w").write("\n".join(lines) + "\n")
assert chr(9) in open("Makefile").read(), "ERROR: no tabs in Makefile!"
print("Makefile OK")
