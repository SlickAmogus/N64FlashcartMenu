# N64 Flashcart Menu — SlickAmogus Custom Build

A customized fork of [N64FlashcartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu) for the SummerCart64, adding **background music**, **custom menu sound effects**, **animated video backgrounds**, **PNG slideshow backgrounds**, and a few new in-menu config options. Everything upstream still works — these features layer on top.

> See [CUSTOM.md](./CUSTOM.md) for the full setup guide (asset folders, conversion tools, file naming, build commands).

---

## What this fork adds

| Feature | Description |
|---|---|
| **Background music** | Plays `.mp3` files from `/menu/music/` shuffled; auto-pauses when the in-menu music player is opened. |
| **Custom sound effects** | Loads `.wav64` files from `/menu/effects/` for cursor / enter / back / error / settings clicks. Falls back to ROM sounds when missing. |
| **Background slideshow** | Rotates `.png` files from `/menu/backgrounds/` at a configurable interval (Off / 30s / 1 min / 2 min / 5 min). Up to 400 PNGs, shuffled. PNGs up to 1280×1024 accepted. |
| **Animated MPEG1 background** | A single `.m1v` video in `/menu/backgrounds/` loops as the background. Takes priority over PNGs; toggleable in-menu. |
| **Screensaver** | After a configurable idle timeout, the browser dims to a bouncing element (DVD-style) to protect CRTs. Any button press wakes it; the wake press is swallowed. |
| **Marquee scroll** | Long file/ROM names that would normally be truncated now smoothly scroll when selected. No config needed. |
| **Configurable text color** | Main text color is set in **Settings → Text Color** (White / Yellow / Cyan / Green / Red / Orange / Pink / Amber). Takes effect immediately. |

---

## SD card layout

```
SD card root/
├── sc64menu.n64
└── menu/
    ├── music/            ← .mp3 files (BGM, shuffled)
    ├── effects/          ← cursor.wav64, enter.wav64, back.wav64, error.wav64, settings.wav64
    ├── backgrounds/      ← .png files OR a single .m1v video
    └── screensaver/      ← bouncer.png (optional — falls back to animated text)
```

All folders are optional — anything missing just disables that feature.

> **Screensaver image:** place any PNG (up to 320×240) at `/menu/screensaver/bouncer.png`. It will bounce around the screen. Without it, a colored text label bounces and cycles color on each edge hit.

---

## In-menu settings

| Setting | Default | Notes |
|---|---|---|
| Background Music | On | Toggle MP3 BGM on/off. |
| Sound Effects | On | Toggle menu SFX on/off. |
| BG Image Rotation | 1 min | Off / 30s / 1 min / 2 min / 5 min. |
| Animated Backgrounds | On | When On, a `.m1v` in `/menu/backgrounds/` plays instead of the PNG slideshow. |
| Screensaver | On | Toggle the screensaver on/off. |
| Screensaver Timeout | 5 min | 1 / 5 / 10 / 30 min, or Off. |
| Text Color | White | White / Yellow / Cyan / Green / Red / Orange / Pink / Amber. Applies immediately. |

---

## Asset preparation tools (Windows)

Found in `tools/`. All require ffmpeg / Docker on `PATH`.

| Script | Purpose |
|---|---|
| `tools\convert_bgm.bat <file> [out_dir]` | Re-encodes any audio file to 128 kbps MP3 with loudness normalization. Input can be `.mp3`, `.flac`, `.wav`, `.ogg`, etc. Safe to run on existing MP3s. |
| `tools\convert_wav.bat <file.wav> <out_dir>` | Wraps `audioconv64` in Docker to produce a `.wav64` SFX file. |
| `tools\convert_bg.bat [input_folder] [crop\|fit\|stretch]` | Bulk-converts PNGs in a folder to 640×480 backgrounds via ffmpeg. Output goes to `<input_folder>\converted\`. Default mode is `crop` (scale-to-cover then center-crop). |
| `tools\rename_bg.bat [folder]` | Renames PNGs in a folder to `bg1.png`, `bg2.png`, … sequentially. Useful before copying to the SD card. |
| `tools\build.bat` | Interactive build wrapper — choose Incremental (fast) or Full (rebuilds everything from scratch). |

### Animated background (MPEG1)

Encode any video to a 640×480 MPEG1 elementary stream with ffmpeg:

```bash
ffmpeg -i input.mp4 -vf scale=640:480 -c:v mpeg1video -q:v 6 -an background.m1v
```

Place the result in `/menu/backgrounds/`. It will loop continuously.

---

## Building

The full Docker-based build steps are in [CUSTOM.md](./CUSTOM.md#building). Quick version:

```bash
# One-time setup
git submodule update --init --recursive
docker build -t n64menu-dev .devcontainer/

# Build (incremental)
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "C:/Claude/N64/N64FlashcartMenu:/workspace" \
  -w /workspace n64menu-dev \
  bash -c "export N64_INST=/opt/libdragon && \
           cd libdragon && make install tools-install -j && \
           cd /workspace && make all"
```

Output: `output/sc64menu.n64` — copy to the root of your SD card.

---

## Flashcart support

This fork inherits upstream support; the new features are tested on **SummerCart64**. Upstream's full support matrix:

### Supported
* SummerCart64
* 64Drive

### Work in Progress
* EverDrive-64 (X and V series)
* ED64P (clones)

---

## Upstream menu features (still present)

* Loads all known N64 games (including byte-swapped ROMs).
* Full 64DD emulation and disk loading (SC64 only).
* Emulator support: NES, SNES, GB, GBC, SMS, GG, CHF.
* N64 ROM box art display.
* PNG background image support (extended here into a slideshow).
* Comprehensive ROM save / info database.
* RTC support.
* MP3 music player view.
* Menu sound effects.
* N64 ROM fast reboot on reset.
* ROM history and favorites.

Beta: ROM Datel code editor · Zip browsing/extraction · Controller Pak backup/restore · Game art switching.

---

## License

This project is released under the [GNU AFFERO GENERAL PUBLIC LICENSE](LICENSE.md).

Original `N64FlashcartMenu` authors:
* [Mateusz Faderewski / Polprzewodnikowy](https://github.com/Polprzewodnikowy)
* [Robin Jones / NetworkFusion](https://github.com/networkfusion)

## Open source software used

### Libraries
* [libdragon](https://github.com/DragonMinded/libdragon/tree/preview) — UNLICENSE
* [libspng](https://github.com/randy408/libspng) — BSD 2-Clause
* [mini.c](https://github.com/univrsal/mini.c) — BSD 2-Clause
* [minimp3](https://github.com/lieff/minimp3) — CC0 1.0
* [miniz](https://github.com/richgel999/miniz) — MIT

### Sounds (default ROM-baked SFX)
* [Cursor sound](https://pixabay.com/en/sound-effects/click-buttons-ui-menu-sounds-effects-button-7-203601/) by Skyscraper_seven
* [Actions (Enter, Back) sound](https://pixabay.com/en/sound-effects/menu-button-user-interface-pack-190041/) by Liecio
* [Error sound](https://pixabay.com/en/sound-effects/error-call-to-attention-129258/) by Universfield

### Emulators
* [neon64v2](https://github.com/hcs64/neon64v2) — ISC
* [sodium64](https://github.com/Hydr8gon/sodium64) — GPL-3.0
* [gb64](https://github.com/lambertjamesd/gb64) — MIT
* [smsPlus64](https://github.com/fhoedemakers/smsplus64) — GPL-3.0
* [Press-F-Ultra](https://github.com/celerizer/Press-F-Ultra) — MIT

### Fonts
* [Firple](https://github.com/negset/Firple) — SIL Open Font License 1.1
