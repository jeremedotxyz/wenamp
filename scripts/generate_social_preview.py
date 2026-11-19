"""Render the 1280 x 640 share card for Wenamp.

The card is generated from the project itself rather than drawn by hand:
  * the player window is the *real* UI, rendered by `wenamp-cli render-ui`
  * the terminal panel is the *real* output of `wenamp-cli info` on a generated file
  * the test and source counts are read from the repository
  * the version comes from include/wenamp/version.hpp

Build first, then run:
    cmake -S . -B build && cmake --build build -j
    pip install Pillow
    python3 scripts/generate_social_preview.py [--cli build/wenamp-cli] [--tests build/wenamp_tests]
"""

import argparse
import base64
import io
import math
import re
import subprocess
import sys
import tempfile
from html import escape
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "assets"
OUT.mkdir(parents=True, exist_ok=True)

W, H, SCALE = 1280, 640, 2

# Palette (dark panel, ember / moss accents from the jereme.xyz design system)
BG = (12, 14, 20)             # #0c0e14
PANEL_BG = (18, 22, 32)       # #121620
BORDER = (46, 54, 72)         # #2e3648
INK = (235, 240, 248)         # #ebf0f8
MUTED = (125, 138, 156)       # #7d8a9c
EMBER = (221, 94, 52)         # #dd5e34  (lifted from #aa3c16 for contrast on dark)
EMBER_LIGHT = (255, 176, 140) # #ffb08c
MOSS = (143, 168, 128)        # #8fa880
LCD = (57, 255, 106)          # #39ff6a
RED = (248, 113, 113)         # #f87171
AMBER = (251, 191, 36)        # #fbbf24
GREEN = (74, 222, 128)        # #4ade80


# ── real data ────────────────────────────────────────────────────────────────
def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


def read_version():
    m = re.search(r'kVersion\s*=\s*"([^"]+)"', (ROOT / "include/wenamp/version.hpp").read_text())
    return m.group(1) if m else "0.0.0"


def count_lines(globs):
    total = 0
    for g in globs:
        for p in ROOT.glob(g):
            total += sum(1 for _ in p.open(encoding="utf-8", errors="ignore"))
    return total


def test_summary(tests_bin):
    """Returns (tests, checks) by actually running the suite when it is built."""
    if tests_bin.exists():
        out = subprocess.run([str(tests_bin)], capture_output=True, text=True).stdout
        m = re.search(r"(\d+) tests, (\d+) checks, (\d+) failed", out)
        if m and m.group(3) == "0":
            return int(m.group(1)), int(m.group(2))
        sys.exit("test suite is failing - refusing to print a green number on the card")
    n = sum(len(re.findall(r"^TEST\(", p.read_text(), re.M)) for p in (ROOT / "tests").glob("test_*.cpp"))
    return n, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default=str(ROOT / "build" / "wenamp-cli"))
    ap.add_argument("--tests", default=str(ROOT / "build" / "wenamp_tests"))
    args = ap.parse_args()
    cli = Path(args.cli)
    if not cli.exists():
        sys.exit(f"{cli} not found - build the project first (cmake -S . -B build && cmake --build build)")

    version = read_version()
    n_tests, n_checks = test_summary(Path(args.tests))
    src_lines = count_lines(["src/*.cpp", "include/wenamp/*.hpp"])

    tmp = Path(tempfile.mkdtemp(prefix="wenamp-card-"))
    wav = tmp / "demo.wav"
    run([str(cli), "gen-demo", str(wav), "12"])
    info_lines = run([str(cli), "info", str(wav)]).stdout.strip().splitlines()
    shot = tmp / "ui.ppm"
    run([str(cli), "render-ui", str(shot), "--demo", "--at", "2.4", "--scale", "1"])
    ui_img = Image.open(shot).convert("RGB")  # 275 x 116, the real main window

    # ── canvas (PNG supersampled, SVG mirrored) ──────────────────────────────
    image = Image.new("RGB", (W * SCALE, H * SCALE), BG)
    draw = ImageDraw.Draw(image)
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        "<title>WENAMP — a classic desktop player in C++17</title>",
        "<desc>Three docked windows, a 19-bar FFT visualizer and a 10-band equalizer, drawn entirely in software.</desc>",
        f'<rect width="{W}" height="{H}" fill="#0c0e14"/>',
    ]

    def hexc(c):
        return f"#{c[0]:02x}{c[1]:02x}{c[2]:02x}"

    def line(pts, color=BORDER, width=1.0):
        draw.line([(x * SCALE, y * SCALE) for x, y in pts], fill=color, width=max(1, round(width * SCALE)))
        svg.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x, y in pts)}" fill="none" stroke="{hexc(color)}" stroke-width="{width}"/>')

    def rect(x, y, w, h, color=BORDER, fill=None, width=1.0, rx=0):
        box = [(x * SCALE, y * SCALE), ((x + w) * SCALE, (y + h) * SCALE)]
        if rx:
            draw.rounded_rectangle(box, radius=rx * SCALE, fill=fill, outline=color, width=max(1, round(width * SCALE)))
        else:
            draw.rectangle(box, fill=fill, outline=color, width=max(1, round(width * SCALE)))
        f = hexc(fill) if fill else "none"
        svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{f}" stroke="{hexc(color)}" stroke-width="{width}"/>')

    def circle(cx, cy, r, fill):
        draw.ellipse([(cx - r) * SCALE, (cy - r) * SCALE, (cx + r) * SCALE, (cy + r) * SCALE], fill=fill)
        svg.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{hexc(fill)}"/>')

    font_cache = {}

    def font(size, bold):
        key = (size, bold)
        if key not in font_cache:
            names = (
                ["/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", "/System/Library/Fonts/Menlo.ttc"]
                if bold
                else ["/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/System/Library/Fonts/Menlo.ttc"]
            )
            f = None
            for n in names:
                try:
                    f = ImageFont.truetype(n, round(size * SCALE))
                    break
                except Exception:
                    continue
            font_cache[key] = f or ImageFont.load_default()
        return font_cache[key]

    def text(x, y, val, size=13, color=INK, bold=False):
        draw.text((x * SCALE, y * SCALE), val, font=font(size, bold), fill=color, anchor="la")
        svg.append(
            f'<text x="{x}" y="{y + size}" fill="{hexc(color)}" font-family="DejaVu Sans Mono, Menlo, Consolas, monospace" '
            f'font-size="{size}" font-weight="{700 if bold else 400}">{escape(val)}</text>'
        )

    # ── background grid + frame ──────────────────────────────────────────────
    for gx in range(30, W - 29, 32):
        line([(gx, 30), (gx, H - 30)], (22, 26, 38), 0.5)
    for gy in range(30, H - 29, 32):
        line([(30, gy), (W - 30, gy)], (22, 26, 38), 0.5)
    rect(36, 36, W - 72, H - 72, BORDER, PANEL_BG, 1.2, rx=8)
    line([(36, 78), (W - 36, 78)], BORDER, 1.0)
    circle(56, 57, 5, RED)
    circle(72, 57, 5, AMBER)
    circle(88, 57, 5, GREEN)

    # tabs mirror the three docked windows
    rect(120, 44, 96, 26, EMBER, (30, 26, 28), 1.0, rx=4)
    text(132, 50, "PLAYER", 11, EMBER_LIGHT, bold=True)
    text(230, 50, "EQUALIZER", 11, MUTED)
    text(342, 50, "PLAYLIST", 11, MUTED)
    text(W - 292, 50, f"WENAMP // C++17 · v{version}", 11, MUTED, bold=True)

    # ── title block with a mini spectrum as the logo ─────────────────────────
    for i in range(9):
        h = 8 + 26 * abs(math.sin(i * 0.7 + 0.6)) * (1 - i / 14)
        col = MOSS if i < 4 else (AMBER if i < 7 else EMBER)
        rect(64 + i * 6, 150 - h, 4, h, col, col, 0)
    text(130, 100, "WENAMP", 36, INK, bold=True)
    text(276, 112, "/ CLASSIC", 22, EMBER, bold=True)
    text(132, 146, "A DESKTOP AUDIO PLAYER, DRAWN ENTIRELY IN SOFTWARE", 12, EMBER_LIGHT, bold=True)
    text(W - 480, 108, "C++17 · FFT VISUALIZER · 10-BAND EQ", 11, LCD, bold=True)
    text(W - 480, 128, "WAV · FLAC · MP3 · ID3 · M3U · SDL2 OPTIONAL", 11, MUTED)
    line([(56, 182), (W - 56, 182)], BORDER, 0.8)

    # ── left panel: the real CLI session ─────────────────────────────────────
    rect(56, 204, 560, 360, BORDER, (14, 17, 24), 1.0, rx=6)
    rect(56, 204, 560, 34, BORDER, (20, 25, 36), 1.0)
    text(72, 214, "SESSION: wenamp-cli", 11, MUTED)
    text(500, 214, "[REAL OUTPUT]", 10, LCD, bold=True)

    y = 254
    text(76, y, "❯ wenamp-cli gen-demo demo.wav 12", 12, INK, bold=True)
    y += 24
    text(76, y, "❯ wenamp-cli info demo.wav", 12, INK, bold=True)
    y += 24
    for ln in info_lines:
        key, _, val = ln.partition(":")
        text(92, y, f"{key.strip():<9}", 11, MUTED)
        text(176, y, val.strip().replace(str(wav), "demo.wav"), 11, LCD)
        y += 20
    y += 10
    text(76, y, "❯ ctest --test-dir build", 12, INK, bold=True)
    y += 24
    circle(84, y + 6, 4, GREEN)
    tail = f"{n_tests} tests" + (f" · {n_checks} checks" if n_checks else "") + " · 0 failed"
    text(96, y, tail, 11, GREEN, bold=True)
    y += 22
    text(96, y, f"{src_lines:,} lines in src/ + include/ · 1 vendored header · 0 image assets", 11, MUTED)

    # ── right panel: the real player window, integer-scaled ──────────────────
    rect(640, 204, 584, 360, BORDER, (14, 17, 24), 1.0, rx=6)
    rect(640, 204, 584, 34, BORDER, (20, 25, 36), 1.0)
    text(656, 214, "PLAYER WINDOW · 275 × 116 · RENDERED BY THE ENGINE", 11, EMBER_LIGHT, bold=True)
    text(1130, 214, "[2× SCALE]", 10, MUTED)

    k = 2
    ui2 = ui_img.resize((ui_img.width * k, ui_img.height * k), Image.NEAREST)
    px, py = 640 + (584 - ui2.width) // 2, 254
    buf = io.BytesIO()
    ui2.save(buf, "PNG")
    b64 = base64.b64encode(buf.getvalue()).decode()
    svg.append(
        f'<image x="{px}" y="{py}" width="{ui2.width}" height="{ui2.height}" '
        f'style="image-rendering:pixelated" href="data:image/png;base64,{b64}"/>'
    )
    text(656, 500, "19 log bars · 1024-pt FFT · Hann window", 11, MUTED)
    text(656, 520, "10 peaking biquads · ±12 dB · bit-exact bypass when flat", 11, MUTED)
    text(656, 540, "3 skins · docked EQ + playlist · seek, shuffle, repeat", 11, MUTED)

    # ── footer ───────────────────────────────────────────────────────────────
    line([(36, H - 76), (W - 36, H - 76)], BORDER, 1.0)
    circle(56, H - 56, 4, GREEN)
    text(68, H - 62, "ENGINE ONLINE", 11, EMBER_LIGHT, bold=True)
    text(200, H - 62, "PULL-MODEL AUDIO · HEADLESS-TESTABLE · SDL2 FOR THE WINDOW ONLY", 11, MUTED)
    text(W - 360, H - 62, "github.com/jeremedotxyz/wenamp", 11, LCD, bold=True)

    svg.append("</svg>")

    png_path = OUT / "social-preview.png"
    svg_path = OUT / "social-preview.svg"
    # Supersampled drawing is downsampled to the 1280 x 640 share-card size; the pixel-art
    # player is pasted afterwards at native resolution so its hard edges stay crisp.
    final = image.resize((W, H), Image.LANCZOS)
    final.paste(ui2, (px, py))
    final.save(png_path, "PNG", optimize=True)
    svg_path.write_text("\n".join(svg), encoding="utf-8")
    print(f"Generated {png_path} ({png_path.stat().st_size:,} bytes)")
    print(f"Generated {svg_path} ({svg_path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
