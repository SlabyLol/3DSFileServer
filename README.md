# 3DSFileServer


A homebrew application for the Nintendo 3DS that runs an HTTP file server over Wi-Fi.

## Features

| Feature | Description |
|---|---|
| 📂 Browse | Navigate files & folders on the SD card |
| ⬆️ Upload | Upload files from your browser |
| ⬇️ Download | Download files to your PC |
| 🗑️ Delete | Delete files/folders (console confirmation required) |
| ✏️ Rename | Rename files/folders (console confirmation required) |
| 📁 New folder | Create a directory (console confirmation required) |
| 📄 New file | Create an empty file (console confirmation required) |

## Console Confirmation

Every **write action** (upload, delete, rename, create) shows a prompt on the **bottom screen** of the 3DS:

```
  3DS File Server

  Client: 192.168.1.42

  Action:
  Upload: photo.jpg (245183 bytes) to /pictures

  [A] Yes    [B] No
```

- **A** → action is executed  
- **B** → action is cancelled, nothing changes

## Installation

1. Download `3ds-fileserver.3dsx` from [Releases](../../releases)
2. Copy it to the `/3ds/` folder on your SD card
3. Launch via the **Homebrew Launcher**

## Usage

1. Start the app
2. Open in your browser: `http://[IP shown on screen]:3760`
3. Done!

## Building locally

```bash
# Requires devkitPro with 3DS support
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

make
```

## GitHub Actions

The build runs automatically on every push. The `.3dsx` is available as a workflow artifact.

To create a release, push a tag:
```bash
git tag v1.0.0
git push origin v1.0.0
```

## Project structure

```
3ds-fileserver/
├── .github/
│   └── workflows/
│       └── build.yml       ← GitHub Actions CI
├── source/
│   └── main.c              ← Main program
├── romfs/                  ← Resources (optional)
├── Makefile
└── README.md
```

## Notes

- The upload staging buffer lives in the 3DS RAM — very large files (>8 MB) may cause issues since the 3DS has ~96 MB RAM total. Normal files work fine.
- Port is **3760**.
