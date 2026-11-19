# Architecture & Technical Design

Wenamp is a classic desktop audio player rebuilt in C++17: a three-window docked skin (player, equalizer, playlist), a 19-bar spectrum visualizer, and a 10-band equalizer, drawn entirely in software from rectangles, a 5×7 bitmap font and seven-segment digits. There are no image assets.

The central design rule is that **the engine never knows about a window**. Audio is *pulled* through one function, the UI is a pure function of state drawn into a pixel buffer, and input arrives as plain coordinates. The SDL window, the headless CLI and the test suite all drive exactly the same code.

```
                                      ┌─────────────────────────────────┐
                                      │   Operator (mouse / keys / drop) │
                                      └────────────────┬────────────────┘
                                                       │ coordinates, Key enums
                                                       ▼
 ┌───────────────────────────┐          ┌─────────────────────────────────┐
 │        Playlist           │◄────────►│               Ui                │
 │ order · shuffle · repeat  │  next /  │  hit-testing · drag state       │
 │ M3U load/save · ID3 titles│  prev    │  marquee · status · 3 windows   │
 └───────────────────────────┘          └───────┬─────────────────┬───────┘
                                                │ transport,      │ render()
                                                │ volume, EQ      │
                                                ▼                 ▼
 ┌───────────────────────────────────────────────────┐   ┌──────────────────────┐
 │                      Player                       │   │       Canvas         │
 │                                                   │   │  ARGB32 framebuffer  │
 │  file ─► miniaudio decoder ─► 44.1 kHz f32 stereo │   │  rect · bevel · text │
 │              │                                    │   │  PPM / scale / hash  │
 │              ▼                                    │   └──────────┬───────────┘
 │        Equalizer  (preamp + 10 biquads)           │              │
 │              │                                    │              ▼
 │              ▼                                    │   ┌──────────────────────┐
 │      volume · balance · clamp                     │   │  SDL2 texture blit   │
 │              │                                    │   │  (or PPM in the CLI) │
 │              ├──────────────► ring buffer (8192)  │   └──────────────────────┘
 │              ▼                       │            │
 │     device callback / offline pull   │            │
 └──────────────────────────────────────┼────────────┘
                                        │ latest 1024 mono samples
                                        ▼
                         ┌───────────────────────────┐
                         │     SpectrumAnalyzer      │
                         │ Hann · FFT · 19 log bars  │
                         │ attack / decay / peaks    │
                         └───────────────────────────┘
```

---

## 1. Dependency Policy

| Dependency | Role | How it is carried |
| :--- | :--- | :--- |
| **miniaudio** `0.11.22` | WAV / FLAC / MP3 decoding, resampling, device output | Vendored single header, `third_party/miniaudio.h` (public domain / MIT-0) |
| **SDL2** | Window, input, texture blit — *nothing else* | Optional. Found by CMake; if absent, `wenamp` is skipped and `wenamp-cli` still builds |
| C++17 standard library | Everything else | — |

Audio never goes through SDL. FFT, EQ, tag parsing, playlist logic, the font, and the whole skin are written in this repository. Pillow is needed only by `scripts/generate_social_preview.py` and never by the build.

---

## 2. The Pull Model

`Player::render(float* out, size_t frames)` is the single place audio is produced. Everything calls it:

| Caller | Cadence | Purpose |
| :--- | :--- | :--- |
| miniaudio device callback | Driven by the sound card | Real playback |
| `Offline` driver in `wenamp-cli` | 735 frames per 1/60 s step | `render-ui`, `analyze` — reproducible, no hardware |
| Test suite | Any size | Position, EQ, volume, balance, end-of-track |

Inside `render()`:

```
 lock ─► decoder.read(frames) ─► position += read ─► EQ.process(read)   (only while Playing)
 unlock
 zero-fill any shortfall ─► apply volume + balance ─► clamp to [-1, 1]
                          └─► write (L+R)/2 into the visualizer ring
 short read?  ─► state = Stopped, ended = true, position = length
```

Because the end-of-track flag is consumed by the UI's `tick()` (`consumeEnded()` returns true exactly once), auto-advance never happens on the audio thread.

### Threading

```
  audio thread                         UI thread
  ────────────                         ─────────
  render() ── mu_ (decoder, position, EQ) ──── open / seek / stop / play
     │                                         setEqGain / applyEqPreset
     ├─ atomics: state, ended, volume, balance  ◄── read & written freely
     └─ ring_ + atomic write cursor  ──────────►  copyLatest() (visualizer)
```

One mutex guards the decoder, position and equalizer. Volume, balance and state are atomics. The visualizer tap is a lock-free ring: the audio thread writes and publishes the cursor with release semantics; the UI thread reads with acquire semantics. A torn read can only blur one visualizer frame.

---

## 3. Signal Path & DSP

### Fixed engine format

Every file is decoded and resampled by miniaudio to **44.1 kHz, 2-channel, 32-bit float**. Mono files are duplicated to both channels. The original rate, channel count and average bitrate (file size ÷ duration) are probed first and shown in the info strip.

### FFT (`wenamp::FFT`)

An iterative radix-2 Cooley–Tukey transform with a precomputed bit-reversal table and twiddle factors. Sizes must be a power of two (the constructor throws otherwise). It is verified against a naive DFT and against known bin placement.

### Spectrum analyzer (`wenamp::SpectrumAnalyzer`)

1. Take the latest 1024 mono samples, apply a **Hann** window.
2. FFT, then for each of **19 bars** take the maximum magnitude over that bar's bin range.
3. Normalise so a full-scale sine reads 0 dB (a Hann-windowed sine peaks at `amplitude × N / 4`), convert to dB, and map `-70 dB … 0 dB` onto `0 … 1`.
4. Add a gentle `0.012 × bar` tilt so the high bars are not permanently starved on typical program material.
5. Smooth: **instant attack**, linear decay at 1.6 full-scales per second.
6. Peak caps hold at the highest bar value and fall with gravity (1.8 / s²).

Bars are log-spaced from **50 Hz to 16 kHz** (each bar is ×1.35 wider than the last):

| Bar | Hz | FFT bins | | Bar | Hz | FFT bins |
| :-: | :--- | :--- | :-: | :-: | :--- | :--- |
| 1 | 50 – 68 | 1 – 1 | | 11 | 1,041 – 1,410 | 24 – 32 |
| 2 | 68 – 92 | 1 – 2 | | 12 | 1,410 – 1,911 | 32 – 44 |
| 3 | 92 – 124 | 2 – 2 | | 13 | 1,911 – 2,588 | 44 – 60 |
| 4 | 124 – 168 | 2 – 3 | | 14 | 2,588 – 3,506 | 60 – 81 |
| 5 | 168 – 228 | 3 – 5 | | 15 | 3,506 – 4,750 | 81 – 110 |
| 6 | 228 – 309 | 5 – 7 | | 16 | 4,750 – 6,435 | 110 – 149 |
| 7 | 309 – 419 | 7 – 9 | | 17 | 6,435 – 8,718 | 149 – 202 |
| 8 | 419 – 567 | 9 – 13 | | 18 | 8,718 – 11,811 | 202 – 274 |
| 9 | 567 – 768 | 13 – 17 | | 19 | 11,811 – 16,000 | 274 – 371 |
| 10 | 768 – 1,041 | 17 – 24 | | | | |

> **Known limit.** A 1024-point FFT at 44.1 kHz has 43 Hz resolution, so the lowest bars share bins (bars 1 and 2 both read bin 1; bars 3 and 4 both read bin 2). Bass detail is coarser than the treble. A larger window would sharpen it at the cost of latency; 1024 was chosen to keep the display responsive at 60 fps.

### Equalizer (`wenamp::Equalizer`)

A preamp followed by **ten peaking biquads** (RBJ audio-EQ-cookbook form, Q = 1.2) in series, in transposed direct form II, evaluated in `double` per channel.

| Band | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| :--- | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| Centre | 60 | 170 | 310 | 600 | 1 k | 3 k | 6 k | 12 k | 14 k | 16 k |

```
  A  = 10^(gain_dB / 40)          w0 = 2π · f0 / fs         α = sin(w0) / (2Q)

  b0 = (1 + α·A) / a0             a0 = 1 + α / A
  b1 = (−2·cos w0) / a0           a1 = (−2·cos w0) / a0
  b2 = (1 − α·A) / a0             a2 = (1 − α / A) / a0
```

- Gains and preamp clamp to **±12 dB**.
- A band at exactly 0 dB is skipped, and a fully flat EQ with a 0 dB preamp is a **bit-exact bypass** (tested by comparing buffers with `==`).
- Filter memory is cleared on seek, stop and track change so no stale state rings into the next passage.
- `responseDb()` evaluates the true combined magnitude response from the biquad coefficients; the curve drawn in the EQ window is that, not a decoration.

Eight presets ship in `eqPresets()` — see the [operator guide](operator-guide.md#equalizer-presets).

---

## 4. Playlist (`wenamp::Playlist`)

| Concern | Behaviour |
| :--- | :--- |
| Titles | `Artist - Title` from ID3 tags, else the file stem |
| Auto-advance | Honours `RepeatMode::One` (replays the same track) |
| User-initiated Next | Ignores repeat-one, and wraps at the end |
| Shuffle | A permutation, with the playing track pinned first, so every track plays once per cycle. Seeded `mt19937`, so a given seed is reproducible |
| Shuffle + Repeat All | Reshuffles at the end of the cycle; the just-played track sits at position 0, so it cannot repeat across the boundary |
| Previous in shuffle | Walks back through the permutation (history) |
| M3U | Reads `#EXTINF` duration/title, resolves relative paths against the playlist's folder, tolerates plain path-per-line files and CRLF; writes `#EXTM3U` |
| Edits | `remove` and `move` keep `current()` pointing at the same entry |

### Tags (`wenamp::readTags`)

ID3v2.3 and v2.4 (`TIT2`, `TPE1`, `TALB`; Latin-1, UTF-16 with and without BOM, UTF-16BE, UTF-8) with synchsafe sizes and extended-header skipping, falling back to ID3v1. Output is UTF-8. Frame sizes are validated against the tag body and the tag is capped at 16 MiB, so a malformed file cannot cause an out-of-bounds read.

---

## 5. UI (`wenamp::Ui`)

### One surface, three docked windows

```
   y = 0   ┌─────────────────────────────┐
           │  Main        275 × 116      │  always present
 y = 116   ├─────────────────────────────┤
           │  Equalizer   275 × 116      │  when shown
 y = 232   ├─────────────────────────────┤
           │  Playlist    275 × 154      │  when shown
 y = 386   └─────────────────────────────┘     (slides up if EQ is hidden)
```

`windowHeight()` is the sum of the visible windows; the SDL front-end resizes the OS window when it changes. Exact control rectangles are in [classic-layout.md](classic-layout.md).

### Input → state → pixels

```
   mouseDown(x, y) ─► hitTest(x, y) ─► Hit{Region, index} ─► press()
                          │                                     │
                          │                  starts a Drag? ──► mouseMove ─► dragTo()
                          ▼                                     │
                    stacked windows:                            ▼
                    main / eq / playlist                  mouseUp ─► commit (seek applies here)

   tick(dt)  ─► marquee scroll · status timeout · analyzer update · auto-advance
   render(c) ─► drawMain · drawEq · drawPlaylist ─► blit into one Canvas
```

- **Hit-testing is data, not callbacks.** `hitTest` returns a `Region` enum plus an index (EQ band, playlist row), which makes the whole mouse surface table-testable without a window.
- **Drags are modal.** Seek, volume, balance, each EQ band, the preamp and the playlist scrollbar capture the drag until release. The seek bar shows the *pending* position while dragging and only seeks on release.
- **Detents.** Balance snaps to centre within ±12 %, EQ sliders within ±0.8 dB of zero.
- **Rendering is deterministic.** `Canvas::hash()` fingerprints a frame, so tests assert that identical state renders identically and that state changes (playing, theme, extra windows) change the pixels.

### Skin

The skin is procedural. `Theme` is a plain struct of 21 colours; three ship (`classic`, `ember`, `paper`). Chrome is 1 px bevels; the time display is seven-segment geometry; text uses a built-in 5×7 font. Because nothing is loaded from disk, there is no asset path to fail and every theme scales cleanly by integer factors.

---

## 6. Front-ends

| Binary | Needs | Does |
| :--- | :--- | :--- |
| `wenamp` | SDL2 | Borderless draggable window, file drop, keyboard shortcuts, integer scale factor |
| `wenamp-cli` | nothing | `info`, `analyze`, `render-ui`, `gen-tone`, `gen-demo`, `play`, `bench`, `selftest` |
| `wenamp_tests` | nothing | The unit suite |

The SDL front-end is deliberately thin: it translates events into `Ui` calls, calls `tick()` and `render()`, and uploads the finished buffer as a streaming texture with nearest-neighbour scaling. The title bar is made draggable through SDL's window hit-test using `Ui::isDragRegion`.

---

## 7. Testing Strategy

`wenamp_tests` uses a tiny in-repo harness (`tests/testing.hpp`) instead of a framework, in keeping with the dependency policy.

| Suite | Covers |
| :--- | :--- |
| `test_dsp.cpp` | FFT vs. naive DFT, bin placement, analyzer bar mapping / attack / decay, EQ gain accuracy, bypass exactness, preamp, presets |
| `test_playlist.cpp` | Navigation, repeat modes, shuffle coverage and determinism, history, remove/move, M3U round-trip and relative paths |
| `test_tags.cpp` | ID3v1, ID3v2 in Latin-1 / UTF-16, bad input, tag-to-title wiring |
| `test_player.cpp` | Open / reject, render, pause, end-of-track signal, seek clamping, volume, balance, resampling, EQ in path, visualizer tap |
| `test_ui.cpp` | Every control's hit region, transport, sliders, seek-on-release, EQ and playlist interaction, keys, auto-advance, deterministic rendering across all themes |

Tests synthesise their own audio and tag fixtures into a per-run scratch directory, so the suite needs no media files and no sound card (miniaudio's null backend is used when no device exists; the device test skips itself if even that is unavailable).

`wenamp-cli selftest` is a seven-check end-to-end smoke test that CI also runs through `ctest`.

---

## 8. Performance

Reference run of `wenamp-cli bench` on a 2-vCPU cloud container (Release build, GCC 13):

| Stage | Result |
| :--- | :--- |
| 10-band EQ, stereo | ~850× realtime |
| 1024-point spectrum | ~13,800 frames/s (~73 µs each) |
| Full software UI frame (275 × 386) | ~6,200 fps |

The visualizer costs about 0.4 % of a 60 fps frame budget. Figures vary by machine; the command exists so you can measure your own.
