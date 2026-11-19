# Classic Layout & Visual Specification

> Wenamp reproduces the *arrangement* of the classic 275-pixel-wide desktop player: a title bar, a recessed LCD readout, a spectrum well, a seek bar, a row of transport buttons, and docked equalizer and playlist windows. Every pixel is drawn procedurally by this project. **No original skin art, bitmaps, fonts or code are used**, and the project is not affiliated with or endorsed by the makers of any existing player.

---

## The Docked Stack

All three windows share one 275 px column and are drawn into a single surface:

```
 x:  0                                                   275
 y=0  ┌─────────────────────────────────────────────────────┐
      │ ≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡ WENAMP ≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡≡ [x] │ 14 px title bar
      │ ▶ 00:02    ┌─ title marquee (scrolls) ──────────────┐│
      │            ├─ 1411 kbps 44 kHz   MONO STEREO ───────┤│
      │ ▂▃▅▇▆▅▃▂▁  └────────────────────────────────────────┘│
      │ ▁▂▃▅▇▆▅▃▂  [volume ▬▬▬▬▬▬] [bal ▬▬▬]    [EQ] [PL]    │
      │ [════════ seek ═════════════════════════════════════]│
      │ [⏮][▶][⏸][⏹][⏭]  [⏏]  [SHUFFLE] [REPEAT]            │
 116  ├─────────────────────────────────────────────────────┤
      │ EQUALIZER                                        [x] │
      │ [ON]      ┌── response curve ──┐       [ Preset ]    │
      │  ┃  +12   ┃ ┃ ┃ ┃ ┃ ┃ ┃ ┃ ┃ ┃                         │
      │  ┃   0    ┃ ┃ ┃ ┃ ┃ ┃ ┃ ┃ ┃ ┃                         │
      │  ┃  -12   60 170 310 600 1K 3K 6K 12K 14K 16K        │
 232  ├─────────────────────────────────────────────────────┤
      │ PLAYLIST                                         [x] │
      │ 1. Artist - Title (3:41)                          ▒ │
      │ 2. ...                                            ░ │
      │ [REM] [CLR]                         12 TRACKS 41:07  │
 386  └─────────────────────────────────────────────────────┘
```

Heights: main **116**, equalizer **116**, playlist **154** (386 total). Hiding the equalizer slides the playlist up under the main window.

Rectangles below are `x, y, width, height` in unscaled UI pixels, relative to the top-left of the window they belong to. They are the same constants the renderer and the hit-tester both use (`src/ui.cpp`), so the picture and the clickable area cannot drift apart.

---

## Main Window — 275 × 116

| Control | Rect | Behaviour |
| :--- | :--- | :--- |
| Title bar | `0, 0, 275, 14` | Drag handle for the OS window (SDL hit-test) |
| Close | `263, 3, 9, 9` | Requests quit |
| Time display | `36, 24, 70, 18` (well starts 12 px left, for the state glyph) | Seven-segment `MM:SS`. Click toggles elapsed ↔ remaining. Blinks while paused |
| Visualizer | `24, 43, 76, 16` | 19 bars × 4 px pitch (3 px bar + 1 px gap), peak caps |
| Title marquee | `111, 24, 154, 9` | Shows `N. Artist - Title (m:ss)`; scrolls at 28 px/s when too wide |
| Info strip | `109, 40, 158, 14` | `kbps`, `kHz`, `MONO` / `STEREO` (lit for the playing file's *source* format) |
| Volume | `107, 57, 68, 13` | 10 px thumb; green→red fill. Mouse wheel ±5 % |
| Balance | `177, 57, 38, 13` | 10 px thumb; snaps to centre within ±12 % |
| EQ toggle | `219, 58, 23, 12` | Shows / hides the equalizer window |
| PL toggle | `242, 58, 23, 12` | Shows / hides the playlist window |
| Seek bar | `16, 72, 248, 10` | 29 px thumb; shows pending time while dragging, seeks on release. Wheel ±5 s |
| Previous | `16, 88, 23, 18` | |
| Play | `39, 88, 23, 18` | Idle: start. Paused: resume. Playing: restart the track |
| Pause | `62, 88, 23, 18` | Toggle |
| Stop | `85, 88, 23, 18` | Stops and rewinds |
| Next | `108, 88, 23, 18` | |
| Eject | `136, 89, 22, 16` | Hints how to add files (open dialogs are platform code; wenamp uses drag-and-drop and the command line) |
| Shuffle | `164, 89, 46, 15` | Lit when on |
| Repeat | `212, 89, 52, 15` | Cycles Off → All → One (`REPEAT 1`) |
| Status line | `6, 107` | Transient messages: volume %, balance, EQ preset, errors (~1–3 s) |

---

## Equalizer Window — 275 × 116

| Control | Rect | Behaviour |
| :--- | :--- | :--- |
| ON | `14, 18, 26, 12` | Enables / bypasses the whole EQ |
| Preset | `205, 18, 56, 12` | Click cycles through the 8 presets; label shows the current name |
| Response graph | `84, 18, 113, 30` | Live combined magnitude response, 30 Hz – 20 kHz (log), ±12 dB |
| Preamp slider | `x = 21`, `y = 52`, 14 × 44 | ±12 dB |
| Band sliders | `x = 78 + 18·i`, `y = 52`, 14 × 44 each | ±12 dB; 6 px thumb; detent at 0 |
| Band labels | below each slider, alternating two rows | `60 170 310 600 1K 3K 6K 12K 14K 16K` |

---

## Playlist Window — 275 × 154

| Control | Rect | Behaviour |
| :--- | :--- | :--- |
| List | `5, 17, 258, 108` | 12 rows of 9 px. Click selects; double-click plays. Current track in the accent colour |
| Scrollbar | `266, 17, 5, 108` | Drag, or use the mouse wheel (3 rows per notch) |
| REM | `6, 132, 30, 13` | Removes the selected entry (stops playback if it was playing) |
| CLR | `38, 132, 30, 13` | Clears the list and unloads the track |
| Total | right-aligned at the bottom | `N TRACKS  h:mm:ss` over entries whose duration is known |

---

## Themes

Every colour in the skin lives in one struct (`Theme`). Switch at runtime with `T`, or start with `--theme`.

| Token | `classic` | `ember` | `paper` |
| :--- | :--- | :--- | :--- |
| Window body | `#3a3f4b` | `#2b1e19` | `#e8e4da` |
| Title gradient | `#5b6378 → #282c38` | `#aa3c16 → #4a1808` | `#aa3c16 → #8a2e10` |
| LCD lit | `#39ff6a` | `#ff8a4a` | `#1e241c` |
| LCD dim | `#123b1f` | `#451c0c` | `#d2cdbe` |
| Accent | `#ffb42e` | `#9fb892` | `#aa3c16` |
| Bars low → high | `#1fd64b → #f03e2b` | `#5f7156 → #aa3c16` | `#5f7156 → #aa3c16` |
| Playlist text | `#39ff6a` | `#ff9e66` | `#242822` |

(`ember` and `paper` borrow the ember `#aa3c16` / moss `#5f7156` pair used across the jereme.xyz sites.)

---

## Keyboard & Mouse

| Input | Action |
| :--- | :--- |
| `Z` / `X` / `C` / `V` / `B` | Previous / Play / Pause / Stop / Next |
| `Space` | Toggle pause |
| `←` / `→` | Seek −5 s / +5 s |
| `↑` / `↓` | Volume +5 % / −5 % |
| `S` / `R` | Shuffle / Repeat (Off → All → One) |
| `E` / `P` | Toggle equalizer / playlist window |
| `T` | Next theme |
| `Enter` | Play the selected playlist row |
| `Del` | Remove the selected playlist row |
| `Q` / `Esc` | Quit |
| Drop a file | Adds it (and plays it if nothing is playing). `.m3u` / `.m3u8` lists are expanded |
| Mouse wheel | Over volume / visualizer / title: volume. Over seek bar: seek. Over the playlist: scroll |

---

## Glyphs & Digits

- **Text** is a 5 × 7 column-major bitmap font for printable ASCII (`0x20`–`0x7E`) with a 6 px advance. Anything else renders as `?`, so non-ASCII titles degrade visibly rather than crashing.
- **Time digits** are 9 × 13 seven-segment cells: lit segments use `lcdOn`, unlit ones `lcdDim`, which gives the classic ghosted look for free.
- **Transport icons** are built from filled rectangles and triangle scanlines — no glyph font involved.
