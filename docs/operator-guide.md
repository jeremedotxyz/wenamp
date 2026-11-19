# Operator Guide

Quick reference for building, running, and configuring Wenamp.

---

## 1. Quickstart

Requirements: a C++17 compiler and CMake ≥ 3.16. SDL2 is optional (it provides the window).

```bash
# Clone the repository
git clone https://github.com/jeremedotxyz/wenamp.git
cd wenamp

# Build
cmake -S . -B build
cmake --build build -j

# Make something to listen to, then open the player
./build/wenamp-cli gen-demo demo.wav 30
./build/wenamp demo.wav --eq --playlist
```

No SDL2? CMake prints `SDL2 not found - skipping the 'wenamp' window front-end` and still builds `wenamp-cli` and the tests. Install SDL2 (`libsdl2-dev` on Debian/Ubuntu, `brew install sdl2` on macOS) and re-run `cmake -S . -B build` to enable the window.

### Build options

| Option | Default | Effect |
| :--- | :--- | :--- |
| `-DWENAMP_BUILD_GUI=OFF` | `ON` | Don't look for SDL2 / don't build `wenamp` |
| `-DWENAMP_BUILD_TESTS=OFF` | `ON` | Skip `wenamp_tests` |
| `-DCMAKE_PREFIX_PATH=<dir>` | — | Point CMake at a non-system SDL2 |
| `-DCMAKE_BUILD_TYPE=Debug` | `Release` | Debug build |

Run the tests:

```bash
ctest --test-dir build --output-on-failure      # unit suite + CLI selftest
./build/wenamp_tests playlist                   # only tests whose name contains "playlist"
```

---

## 2. Window Front-end — `wenamp`

```
wenamp [options] [files or .m3u playlists...]
```

| Option | Meaning |
| :--- | :--- |
| `--theme classic\|ember\|paper` | Skin colours (default `classic`) |
| `--scale N` | Integer window scale (default `2`) |
| `--eq` | Start with the equalizer window open |
| `--playlist` | Start with the playlist window open (opens automatically when more than one file is given) |
| `--frames N` | Headless smoke mode: run N frames, then exit |
| `--dump out.ppm` | With `--frames`: write the final frame as a PPM |
| `--version`, `--help` | |

The first file starts playing immediately. Drop more files on the window to append them. For keys and mouse controls see [classic-layout.md](classic-layout.md#keyboard--mouse).

**Headless use (CI, containers):**

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/wenamp demo.wav --frames 60 --dump frame.ppm
```

If no audio device can be opened the player prints a warning and the UI runs silently rather than exiting.

---

## 3. Command Line — `wenamp-cli`

| Command | Does |
| :--- | :--- |
| `info <file>` | Duration, source rate / channels, average bitrate, ID3 title / artist / album |
| `analyze <file> [--seconds N]` | Decodes and prints the 19-bar spectrum over time as ASCII |
| `render-ui <out.ppm> [options] [files...]` | Renders the real UI to an image — see below |
| `gen-tone <out.wav> [hz] [seconds]` | Stereo sine wave (default 440 Hz, 5 s) |
| `gen-demo <out.wav> [seconds]` | A-minor-ninth chord stack with a kick and hi-hat (default 20 s) |
| `play <files...> [--seconds N]` | Plays through the audio device (prints an error if none exists) |
| `bench` | EQ, spectrum and UI-render throughput |
| `selftest` | Seven end-to-end checks; exit code 1 on failure |
| `version` | Prints `wenamp 0.1.0` |

### `render-ui`

Drives the UI offline in 1/60 s steps — exactly the loop the window runs — then writes the frame.

| Option | Meaning |
| :--- | :--- |
| `--theme classic\|ember\|paper` | Skin colours |
| `--eq` / `--playlist` | Include those windows in the frame |
| `--preset <name>` | Apply an EQ preset (`Rock`, `Pop`, `Jazz`, …; unknown names exit with code 2) |
| `--at <seconds>` | Play this long before capturing (so the time, bars and marquee are mid-song) |
| `--scale <k>` | Integer upscale (default 1) |
| `--demo` | Generate and play a demo loop when no files are given |

```bash
./build/wenamp-cli render-ui shot.ppm --demo --at 2.2 --eq --playlist --preset Rock --scale 3
python3 -c "from PIL import Image; Image.open('shot.ppm').save('shot.png')"   # PPM → PNG
```

### Exit codes

| Code | Meaning |
| :--- | :--- |
| `0` | Success |
| `1` | A file could not be decoded / written, no audio device (`play`), or `selftest` failed |
| `2` | Usage error (missing arguments, unknown command or preset) |

---

## 4. Supported Input

| Item | Support |
| :--- | :--- |
| Audio formats | **WAV, FLAC, MP3** (miniaudio's built-in decoders) |
| Playlists | `.m3u`, `.m3u8` (read and write; `#EXTINF` honoured; relative paths resolved against the list's folder) |
| Tags | ID3v2.3 / v2.4 and ID3v1 (title, artist, album) |
| Sample rates / channels | Anything the decoders produce; resampled to 44.1 kHz stereo |

---

## 5. Equalizer Presets

Gains in dB for bands `60 170 310 600 1K 3K 6K 12K 14K 16K`, plus the preamp.

| Preset | 60 | 170 | 310 | 600 | 1K | 3K | 6K | 12K | 14K | 16K | Preamp |
| :--- | -: | -: | -: | -: | -: | -: | -: | -: | -: | -: | -: |
| Flat | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Rock | 4 | 3 | 1 | −2 | −3 | −1 | 2 | 4 | 5 | 5 | −3 |
| Pop | −1 | 2 | 4 | 5 | 3 | 0 | −1 | −1 | 1 | 1 | −2 |
| Jazz | 3 | 2 | 1 | 2 | −2 | −2 | 0 | 1 | 2 | 3 | −2 |
| Classical | 3 | 2 | 1 | 0 | 0 | 0 | −1 | −2 | −2 | −3 | −1 |
| Bass Boost | 7 | 6 | 4 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | −5 |
| Treble Boost | 0 | 0 | 0 | 0 | 0 | 1 | 3 | 5 | 6 | 7 | −4 |
| Vocal | −3 | −2 | 0 | 3 | 5 | 4 | 2 | 0 | −1 | −2 | −2 |

Presets that boost carry a negative preamp so the combined gain stays clear of clipping. The output is hard-clamped to ±1.0 as a last resort.

---

## 6. Troubleshooting

| Symptom | Cause / fix |
| :--- | :--- |
| `SDL2 not found - skipping the 'wenamp' window front-end` | Install SDL2 development files, or pass `-DCMAKE_PREFIX_PATH` |
| `warning: no audio output device available (UI will run silently)` | No sound card / server reachable. The UI still works. In containers, expect this |
| Log spam such as `ALSA lib ... cannot find card '0'` or `jack server is not running` | miniaudio probing audio backends on a machine with no hardware. Harmless |
| Status line shows `ERROR: cannot decode <path>` | Unsupported or corrupt file. Only WAV / FLAC / MP3 are built in |
| Title shows `?` for some characters | The 5×7 built-in font covers printable ASCII only |
| Bass bars look blocky | 1024-point FFT resolution — see [architecture.md](architecture.md#spectrum-analyzer-wenampspectrumanalyzer) |
