# SlickAmogus Custom Build

A customized build of [N64FlashcartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu) for the SummerCart64 flashcart, adding background music, custom menu sound effects, randomized background images, and animated MPEG1 video background support.

---

## Features added

| Feature | Description |
|---|---|
| Background Music | Plays `.mp3` files from `/menu/music/` shuffled; auto-pauses when the music player is open |
| Menu Sound Effects | Loads `.wav64` files from `/menu/effects/`; falls back to built-in ROM sounds |
| Background Images | Rotates `.png` files from `/menu/backgrounds/` at a configurable interval |
| Animated Background | A `.m1v` video in `/menu/backgrounds/` loops as the background (takes priority over PNGs) |

---

## SD Card Folder Structure

```
SD card root/
├── sc64menu.n64
└── menu/
    ├── music/            ← .mp3 files (background music)
    ├── effects/          ← .wav64 files (menu sound effects)
    │   ├── cursor.wav64
    │   ├── enter.wav64
    │   ├── back.wav64
    │   ├── error.wav64
    │   └── settings.wav64
    └── backgrounds/      ← .png files and/or a .m1v video
```

All folders are optional.

---

## Sound Effects — File Names

| File | When it plays |
|---|---|
| `cursor.wav64` | Cursor movement |
| `enter.wav64` | Selecting an item |
| `back.wav64` | Cancelling / going back |
| `error.wav64` | Error condition |
| `settings.wav64` | Opening a settings option |

---

## Converting WAV → WAV64

**Windows (Docker wrapper):**

```bat
tools\convert_wav.bat my_sound.wav sd_card\menu\effects
```

**Manual:**

```bash
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "C:/path/to/wav:/input" \
  -v "C:/path/to/output:/output" \
  n64menu-dev \
  bash -c "export N64_INST=/opt/libdragon && \
           /opt/libdragon/bin/audioconv64 --wav-compress 1 -o /output /input/mysound.wav"
```

Recommended input: 44100 Hz, 16-bit PCM WAV.

---

## Animated Background (MPEG1 .m1v)

Encode with ffmpeg:

```bash
ffmpeg -i input.mp4 -vf scale=640:480 -c:v mpeg1video -q:v 6 -an background.m1v
```

Place the `.m1v` file in `/menu/backgrounds/` on the SD card. It loops continuously. PNG files in the same folder are ignored when a video is present.

---

## Settings (in-menu)

| Setting | Default | Description |
|---|---|---|
| Background Music | On | Enable/disable MP3 BGM |
| Sound Effects | On | Enable/disable menu SFX |
| BG Image Rotation | 1 min | How often the background PNG changes (Off / 30s / 1min / 2min / 5min) |

---

## Building

### Prerequisites

- Docker Desktop running
- Git submodules initialized: `git submodule update --init --recursive`
- Docker image built once: `docker build -t n64menu-dev .devcontainer/`

### Full build

```bash
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "C:/Claude/N64/N64FlashcartMenu:/workspace" \
  -w /workspace n64menu-dev \
  bash -c "export N64_INST=/opt/libdragon && \
           cd libdragon && make clobber -j && make libdragon tools -j && make install tools-install -j && \
           cd /workspace && make all"
```

### Incremental build

```bash
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "C:/Claude/N64/N64FlashcartMenu:/workspace" \
  -w /workspace n64menu-dev \
  bash -c "export N64_INST=/opt/libdragon && \
           cd libdragon && make install tools-install -j && \
           cd /workspace && make all"
```

Output: `output/sc64menu.n64` — copy to SD card root.
