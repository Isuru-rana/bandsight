#!/usr/bin/env python3
"""Builds every Bandsight brand asset from geometry in this file.

    uv run --with fonttools --with pillow docs/branding/generate.py <Outfit[wght].ttf>

Needs inkscape (SVG -> PNG) and ImageMagick `magick` (ICO). The wordmark is
Outfit Bold (SIL OFL, github.com/google/fonts/tree/main/ofl/outfit) converted
to outlines, so the SVGs carry no font dependency.

Outputs, relative to the repo root:
  docs/branding/*.svg, *.png, favicon.ico     marks, wordmarks, social preview
  packaging/icons/hicolor/<size>/apps/        launcher / tray icon
  cli/logo.h                                  the mark in braille for bandsight-cli
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "branding")
HICOLOR = os.path.join(ROOT, "packaging", "icons", "hicolor")

NAVY, WHITE, CYAN, TILE = "#0B2245", "#FFFFFF", "#00CAFE", "#0B0F17"

# The mark lives in a 560 x 560 box centred on (C, C).
C = 280

# Eye outline: one quarter as a cubic (tip -> top), mirrored for the rest.
# Points are offsets from the centre, traced from the original artwork.
EYE_OUTER = [(-246.5, 0), (-226.16, -45.68), (-110, -160), (0, -160)]
EYE_INNER = [(-210, 0), (-187.63, -33.16), (-100, -136), (0, -136)]


def scale_quarter(q, sx, sy):
    return [(x * sx, y * sy) for x, y in q]


def lens(q):
    """Closed almond from one quarter-curve, sharp at both tips."""
    f = lambda v: f"{v:.2f}".rstrip("0").rstrip(".")
    pt = lambda x, y: f"{f(C + x)} {f(C + y)}"
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = q
    return (
        f"M{pt(x0, y0)}"
        f"C{pt(x1, y1)} {pt(x2, y2)} {pt(x3, y3)}"
        f"C{pt(-x2, y2)} {pt(-x1, y1)} {pt(-x0, y0)}"
        f"C{pt(-x1, -y1)} {pt(-x2, -y2)} {pt(x3, -y3)}"
        f"C{pt(x2, -y2)} {pt(x1, -y1)} {pt(x0, y0)}Z"
    )


def arcs(r, half_angle_deg):
    """Upper and lower signal arcs of radius r, each spanning 2 * half_angle."""
    import math

    t = math.radians(half_angle_deg)
    dx, dy = r * math.sin(t), r * math.cos(t)
    up = f"M{C - dx:.2f} {C - dy:.2f}A{r} {r} 0 0 1 {C + dx:.2f} {C - dy:.2f}"
    dn = f"M{C - dx:.2f} {C + dy:.2f}A{r} {r} 0 0 0 {C + dx:.2f} {C + dy:.2f}"
    return f"{up} {dn}"


def mark(fg, small=False):
    """The eye mark as SVG elements in the 560 box.

    Full: dot r31, then a gap, a 24-wide arc, a gap, a 24-wide arc, a gap and
    the 24-thick eye outline. All three gaps are 19.
    Small (22-32 px): one arc fewer and heavier strokes, so it survives the
    downscale instead of turning into a grey blur.
    Micro (16 px): no room for arcs at all; a heavy eye with a cyan pupil.
    """
    if small == "micro":
        inner = scale_quarter(EYE_INNER, 175 / 210, 100 / 136)
        return (
            f'<path d="{lens(EYE_OUTER)} {lens(inner)}" fill="{fg}" fill-rule="evenodd"/>'
            f'<circle cx="{C}" cy="{C}" r="62" fill="{CYAN}"/>'
        )
    if small:
        inner = scale_quarter(EYE_INNER, 190 / 210, 120 / 136)
        return (
            f'<path d="{lens(EYE_OUTER)} {lens(inner)}" fill="{fg}" fill-rule="evenodd"/>'
            f'<path d="{arcs(82, 52)}" fill="none" stroke="{CYAN}" stroke-width="36"/>'
            f'<circle cx="{C}" cy="{C}" r="44" fill="{fg}"/>'
        )
    return (
        f'<path d="{lens(EYE_OUTER)} {lens(EYE_INNER)}" fill="{fg}" fill-rule="evenodd"/>'
        f'<path d="{arcs(62, 55)}" fill="none" stroke="{fg}" stroke-width="24"/>'
        f'<path d="{arcs(105, 54)}" fill="none" stroke="{CYAN}" stroke-width="24"/>'
        f'<circle cx="{C}" cy="{C}" r="31" fill="{fg}"/>'
    )


def svg(w, h, body, view=None):
    view = view or f"0 0 {w} {h}"
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view}" '
        f'width="{w}" height="{h}">{body}</svg>\n'
    )


def tile(small=False, pad=0.955):
    """App icon: the white mark on a dark rounded square, so it reads on both
    light and dark panels and menus."""
    s = pad
    off = C * (1 - s)
    return (
        f'<rect width="560" height="560" rx="124" fill="{TILE}"/>'
        f'<g transform="translate({off:.2f} {off:.2f}) scale({s})">{mark(WHITE, small)}</g>'
    )


def badge(fg=WHITE, back=TILE, edge=48):
    """The mark on an eye-shaped backing that follows the outline with a thick
    margin, sharp at the tips like the eye itself. Transparent outside."""
    s = 0.84
    off = C * (1 - s)
    return (
        f'<g transform="translate({off:.2f} {off:.2f}) scale({s})">'
        f'<path d="{lens(EYE_OUTER)}" fill="{back}" stroke="{back}" '
        f'stroke-width="{edge * 2}" stroke-linejoin="miter" stroke-miterlimit="10"/>'
        f'{mark(fg)}</g>'
    )


# ---- wordmark -------------------------------------------------------------

def outfit_paths(font_file, text, size):
    """Glyph outlines for `text` at `size` px with the baseline at y=0.

    Returns (list of (char, path, x, width)), total advance."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    f = TTFont(font_file)
    if "fvar" in f:
        f = instantiateVariableFont(f, {"wght": 700})
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    s = size / f["head"].unitsPerEm
    x, out = 0.0, []
    for ch in text:
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (s, 0, 0, -s, x, 0)))
        out.append((ch, pen.getCommands(), x, gs[g].width * s))
        x += gs[g].width * s
    return out, x


def split_i_dot(path):
    """Separates the tittle (the topmost closed subpath) from the 'i' stem."""
    parts = ["M" + p for p in path.split("M") if p]

    def top(p):
        import re

        ys = [float(v) for v in re.findall(r"-?\d+\.?\d*", p)[1::2]]
        return min(ys)

    parts.sort(key=top)
    return parts[0], " ".join(parts[1:])


def wordmark(font_file, fg):
    """Mark + 'Bandsight', the i's tittle in cyan.

    Proportions follow the original artwork: letter cap height is 0.49 of the
    eye's height, caps centred on the eye, a gap of 0.15 eye heights."""
    H = 200                       # eye height in the output
    k = H / 320                   # the eye is 320 tall in the 560 box
    cap = 0.49 * H / 0.676        # Outfit cap height = 0.676 em
    glyphs, advance = outfit_paths(font_file, "Bandsight", cap)
    tx = 493 * k + 0.15 * H       # eye width + gap
    base = H / 2 + 0.676 * cap / 2
    body = [f'<g transform="scale({k:.5f}) translate(-33.5 -120)">{mark(fg)}</g>']
    for ch, d, x, _ in glyphs:
        t = f'transform="translate({tx:.2f} {base:.2f})"'
        if ch == "i":
            dot, stem = split_i_dot(d)
            body.append(f'<path {t} d="{stem}" fill="{fg}"/><path {t} d="{dot}" fill="{CYAN}"/>')
        else:
            body.append(f'<path {t} d="{d}" fill="{fg}"/>')
    return svg(round(tx + advance), H, "".join(body))


def icon_for(size):
    if size <= 16:
        return "bandsight-app-icon-micro.svg"
    return "bandsight-app-icon-small.svg" if size <= 32 else "bandsight-app-icon.svg"


# ---- rendering ------------------------------------------------------------

def write(path, text):
    with open(path, "w") as f:
        f.write(text)


def png(src_svg, dst, w, h=None):
    h = h or w
    subprocess.run(
        ["inkscape", src_svg, "-w", str(w), "-h", str(h), "-o", dst],
        check=True, capture_output=True,
    )


def cli_logo(path, width=48):
    """The mark as braille cells (2 x 4 dots each) for bandsight-cli, with a
    per-cell flag for the cyan arcs. Written as a C++ header."""
    from PIL import Image

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "m.svg")
        out = os.path.join(tmp, "m.png")
        write(src, svg(560, 560, mark(WHITE)))
        # Just the eye's bounding box, so the logo has no empty margin.
        subprocess.run(["inkscape", src, "--export-area=30:117:530:443",
                        "-w", str(width), "-o", out], check=True, capture_output=True)
        im = Image.open(out).convert("RGBA")
    w, h = im.size
    dots = [(0, 0, 0x01), (0, 1, 0x02), (0, 2, 0x04), (1, 0, 0x08),
            (1, 1, 0x10), (1, 2, 0x20), (0, 3, 0x40), (1, 3, 0x80)]
    cells, cyan = [], []
    for cy in range(0, h, 4):
        line, mask = "", ""
        for cx in range(0, w, 2):
            v = on = blue = 0
            for dx, dy, bit in dots:
                x, y = cx + dx, cy + dy
                if x < w and y < h:
                    r, g, b, a = im.getpixel((x, y))
                    if a > 110:
                        v |= bit
                        on += 1
                        blue += r < 120 and b > 180
            line += chr(0x2800 + v) if v else " "
            mask += "c" if on and blue * 2 > on else "."
        cells.append(line)
        cyan.append(mask)
    while cells and not cells[-1].strip():
        cells.pop()
        cyan.pop()
    q = lambda t: '"' + t + '"'
    write(path, (
        "// SPDX-License-Identifier: GPL-3.0-or-later\n"
        "// Generated by docs/branding/generate.py from the brand mark. Do not edit.\n"
        "#pragma once\n\n"
        "namespace bandsight::cli::logo {\n\n"
        f"inline constexpr int kRows = {len(cells)};\n"
        f"inline constexpr int kCols = {len(cells[0])};\n\n"
        "// One row of braille cells each, UTF-8.\n"
        "inline constexpr const char* kCells[kRows] = {\n"
        + "".join(f"    {q(c)},\n" for c in cells) + "};\n\n"
        "// One character per cell: 'c' = drawn in the accent cyan.\n"
        "inline constexpr const char* kCyan[kRows] = {\n"
        + "".join(f"    {q(m)},\n" for m in cyan) + "};\n\n"
        "}  // namespace bandsight::cli::logo\n"
    ))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    font = sys.argv[1]
    j = lambda *p: os.path.join(OUT, *p)

    # Marks (transparent). "-dark" = for use ON dark backgrounds.
    write(j("bandsight-icon.svg"), svg(560, 560, mark(NAVY)))
    write(j("bandsight-icon-dark.svg"), svg(560, 560, mark(WHITE)))
    write(j("bandsight-icon-small.svg"), svg(560, 560, mark(NAVY, True)))
    write(j("bandsight-icon-small-dark.svg"), svg(560, 560, mark(WHITE, True)))
    write(j("bandsight-app-icon.svg"), svg(560, 560, tile()))
    write(j("bandsight-icon-badge.svg"), svg(560, 560, badge()))
    write(j("bandsight-app-icon-small.svg"), svg(560, 560, tile(True, 0.955)))
    write(j("bandsight-app-icon-micro.svg"), svg(560, 560, tile("micro", 0.955)))
    png(j("bandsight-icon.svg"), j("bandsight-icon.png"), 1024)
    png(j("bandsight-icon-dark.svg"), j("bandsight-icon-dark.png"), 1024)
    png(j("bandsight-app-icon.svg"), j("bandsight-app-icon.png"), 1024)
    png(j("bandsight-icon-badge.svg"), j("bandsight-icon-badge.png"), 1024)

    # Wordmarks.
    write(j("bandsight-logo.svg"), wordmark(font, NAVY))
    write(j("bandsight-logo-dark.svg"), wordmark(font, WHITE))
    for name in ("bandsight-logo", "bandsight-logo-dark"):
        src = j(name + ".svg")
        with open(src) as f:
            w = int(f.read().split('width="')[1].split('"')[0])
        png(src, j(name + ".png"), w * 3, 200 * 3)
    shutil.copyfile(j("bandsight-logo.png"), j("bandsight-logo-transparent.png"))

    # GitHub social preview, 1280 x 640 (Settings -> Social preview).
    with open(j("bandsight-logo-dark.svg")) as f:
        inner = f.read()
    lw = int(inner.split('width="')[1].split('"')[0])
    inner_body = inner.split(">", 1)[1].rsplit("</svg>", 1)[0]
    sc = 880 / lw
    social = svg(
        1280, 640,
        f'<rect width="1280" height="640" fill="{TILE}"/>'
        f'<g transform="translate({(1280 - lw * sc) / 2:.2f} {(640 - 200 * sc) / 2:.2f}) scale({sc:.4f})">{inner_body}</g>',
    )
    with tempfile.TemporaryDirectory() as tmp:
        sp = os.path.join(tmp, "social.svg")
        write(sp, social)
        png(sp, j("bandsight-social.png"), 1280, 640)

        # favicon.ico (16/32/48) and apple-touch-icon (180, no transparency).
        icos = []
        for size in (16, 32, 48):
            src = j(icon_for(size))
            p = os.path.join(tmp, f"f{size}.png")
            png(src, p, size)
            icos.append(p)
        subprocess.run(["magick", *icos, j("favicon.ico")], check=True)
        at = os.path.join(tmp, "at.svg")
        write(at, svg(180, 180, f'<rect width="180" height="180" fill="{TILE}"/>'
                      f'<g transform="translate(8.5 8.5) scale({163 / 560:.5f})">{mark(WHITE)}</g>'))
        png(at, j("apple-touch-icon.png"), 180)

    # hicolor theme icons for the launcher, window and tray.
    for size in (16, 22, 24, 32, 48, 64, 128, 256, 512):
        d = os.path.join(HICOLOR, f"{size}x{size}", "apps")
        os.makedirs(d, exist_ok=True)
        src = j(icon_for(size))
        png(src, os.path.join(d, "bandsight.png"), size)
    d = os.path.join(HICOLOR, "scalable", "apps")
    os.makedirs(d, exist_ok=True)
    write(os.path.join(d, "bandsight.svg"), svg(560, 560, tile()))

    cli_logo(os.path.join(ROOT, "cli", "logo.h"))


if __name__ == "__main__":
    main()
