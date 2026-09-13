#!/usr/bin/env python3
"""Rasterizes the status cards' artwork into the 1bpp bitmaps in
include/CardArt.h, and writes a preview PNG of each finished 400x600 panel.

    python3 tools/gencard.py

include/CardArt.h and tools/card_preview.png are OUTPUTS. Edit this script and
re-run it; do not hand-edit the header.

Both inputs are downloaded, pinned by URL, rather than vendored: the icon is
6 KB of path data and the font is 135 KB, and neither belongs in a firmware
repo when the only artifact that ships is the packed bitmap.

  Icon: Material Icons "battery_charging_full", rounded style, Apache 2.0
        github.com/google/material-design-icons
  Type: Jost, weight 700, SIL OFL 1.1 - github.com/google/fonts
        Jost is the open Futura, which is what the enclosure's back is
        engraved in. Same typeface on the case and on the last screen the
        device ever draws.
"""
import os
import re
import sys
import urllib.request

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(HERE)

ICON_BASE = ("https://raw.githubusercontent.com/google/material-design-icons/"
             "master/src/%s/materialiconsround/24px.svg")
FONT_URL = ("https://github.com/google/fonts/raw/main/ofl/jost/Jost%5Bwght%5D.ttf")

PANEL_W, PANEL_H = 400, 600

# Supersample, then threshold at 50%. The panel has no greys to dither into at
# this size, so the edges want to be decided once, cleanly, here.
SS = 8

# Every card mirrors the Polaroid frame the photos use: glyph inside the square
# image area, words on the chin below it. test_card_respects_the_polaroid_frame
# asserts exactly this, against IMAGE_BOTTOM = 400.
ICON_Y = 90
TEXT_Y = 455
FONT_WEIGHT = 700

# Preview ink per card. The firmware picks the real ink in StatusCard.h; this
# only makes the checked-in PNG look like what the panel will actually show.
INK_RGB = {"red": (190, 50, 45), "blue": (40, 60, 140)}

# prefix, icon path, words, icon height, text height, tracking, preview ink
CARDS = [
    ("", "device/battery_charging_full", "CHARGE ME", 290, 46, 6, "red"),
    ("NO_PHOTOS_", "image/photo_library", "ADD PHOTOS", 270, 40, 4, "blue"),
]

# --- svg path ---------------------------------------------------------------

TOKEN = re.compile(r'([MmLlHhVvCcSsZz])|(-?\d*\.?\d+(?:[eE][-+]?\d+)?)')


def parse_path(d):
    """Flatten an SVG path into a list of closed subpaths of points."""
    toks = TOKEN.findall(d)
    i = 0
    subpaths, cur = [], []
    x = y = sx = sy = 0.0
    cmd = None
    prev_c2 = None

    def num():
        nonlocal i
        while i < len(toks) and toks[i][0]:
            i += 1
        v = float(toks[i][1])
        i += 1
        return v

    def cubic(x0, y0, x1, y1, x2, y2, x3, y3):
        n = 24
        for k in range(1, n + 1):
            t = k / n
            mt = 1 - t
            cur.append((
                mt*mt*mt*x0 + 3*mt*mt*t*x1 + 3*mt*t*t*x2 + t*t*t*x3,
                mt*mt*mt*y0 + 3*mt*mt*t*y1 + 3*mt*t*t*y2 + t*t*t*y3,
            ))

    while i < len(toks):
        if toks[i][0]:
            cmd = toks[i][0]
            i += 1
        if cmd in 'Zz':
            if cur:
                subpaths.append(cur)
                cur = []
            x, y = sx, sy
            prev_c2 = None
            continue
        if cmd in 'Mm':
            if cur:
                subpaths.append(cur)
                cur = []
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if cmd == 'm' else (nx, ny)
            sx, sy = x, y
            cur = [(x, y)]
            cmd = 'l' if cmd == 'm' else 'L'
            prev_c2 = None
        elif cmd in 'Ll':
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if cmd == 'l' else (nx, ny)
            cur.append((x, y))
            prev_c2 = None
        elif cmd in 'Hh':
            nx = num()
            x = x + nx if cmd == 'h' else nx
            cur.append((x, y))
            prev_c2 = None
        elif cmd in 'Vv':
            ny = num()
            y = y + ny if cmd == 'v' else ny
            cur.append((x, y))
            prev_c2 = None
        elif cmd in 'CcSs':
            if cmd in 'Cc':
                a, b, c, dd, e, f = (num() for _ in range(6))
                if cmd == 'c':
                    a, b, c, dd, e, f = x+a, y+b, x+c, y+dd, x+e, y+f
            else:
                c, dd, e, f = (num() for _ in range(4))
                if cmd == 's':
                    c, dd, e, f = x+c, y+dd, x+e, y+f
                a, b = (2*x - prev_c2[0], 2*y - prev_c2[1]) if prev_c2 else (x, y)
            cubic(x, y, a, b, c, dd, e, f)
            prev_c2 = (c, dd)
            x, y = e, f
        else:
            raise SystemExit("unsupported path command %r" % cmd)
    if cur:
        subpaths.append(cur)
    return subpaths


def render_icon(d, target_h):
    """Even-odd fill is XOR of the subpaths' interiors, which is what knocks
    the bolt out of the battery as a hole rather than drawing it over the top."""
    subs = parse_path(d)
    xs = [p[0] for s in subs for p in s]
    ys = [p[1] for s in subs for p in s]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    scale = (target_h * SS) / (y1 - y0)
    w = int(round((x1 - x0) * scale)) + 1
    h = int(round((y1 - y0) * scale)) + 1

    acc = Image.new('1', (w, h), 0)
    for s in subs:
        m = Image.new('1', (w, h), 0)
        ImageDraw.Draw(m).polygon(
            [((px - x0) * scale, (py - y0) * scale) for px, py in s], fill=1)
        acc = ImageChops.logical_xor(acc, m)
    return downsample(acc)


def downsample(img):
    w, h = img.size
    g = img.convert('L').resize((max(1, round(w / SS)), max(1, round(h / SS))),
                                Image.LANCZOS)
    return g.point(lambda v: 255 if v >= 128 else 0, mode='1')


def render_text(text, font_bytes, target_h, tracking, weight):
    import io
    size = target_h * SS
    f = ImageFont.truetype(io.BytesIO(font_bytes), size)
    if weight:
        f.set_variation_by_axes([weight])
    img = Image.new('L', (size * len(text) * 2, size * 2), 0)
    d = ImageDraw.Draw(img)
    x = 0.0
    for ch in text:
        d.text((x, 0), ch, font=f, fill=255)
        x += f.getlength(ch) + tracking * SS
    img = img.crop(img.getbbox())
    w, h = img.size
    g = img.resize((max(1, round(w / SS)), max(1, round(h / SS))), Image.LANCZOS)
    return g.point(lambda v: 255 if v >= 128 else 0, mode='1')


# --- packing ----------------------------------------------------------------

def pack(img):
    """1bpp, MSB first, one row at a time. Stride is whole bytes so a row lookup
    in firmware is a shift and a mask rather than a division."""
    w, h = img.size
    stride = (w + 7) // 8
    px = img.load()
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out), stride


def as_c_array(data, indent="    "):
    lines = []
    for i in range(0, len(data), 12):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + 12])
        lines.append(indent + chunk + ",")
    return "\n".join(lines)


def fetch(url):
    with urllib.request.urlopen(url, timeout=30) as r:
        return r.read()


def build_card(prefix, icon_path, line, icon_h, text_h, tracking, ink, font_bytes):
    """Rasterizes one card and returns its header block plus a preview image."""
    svg = fetch(ICON_BASE % icon_path).decode()
    path = re.findall(r'd="([^"]*)"', svg)[-1]

    icon = render_icon(path, icon_h)
    text = render_text(line, font_bytes, text_h, tracking, FONT_WEIGHT)

    icon_x = (PANEL_W - icon.size[0]) // 2
    text_x = (PANEL_W - text.size[0]) // 2
    print("  icon %dx%d at (%d,%d), text %dx%d at (%d,%d)"
          % (icon.size + (icon_x, ICON_Y) + text.size + (text_x, TEXT_Y)))

    if ICON_Y + icon.size[1] >= 400:
        raise SystemExit("%s: icon runs past the frame\'s image area at y=400" % line)
    if TEXT_Y + text.size[1] > PANEL_H:
        raise SystemExit("%s: text runs off the bottom of the panel" % line)
    if text.size[0] > PANEL_W:
        raise SystemExit("%s: text is wider than the panel" % line)

    icon_bits, icon_stride = pack(icon)
    text_bits, text_stride = pack(text)

    block = '''
// {icon_path} + "{line}"
inline constexpr std::uint16_t {p}ICON_W = {iw};
inline constexpr std::uint16_t {p}ICON_H = {ih};
inline constexpr std::uint16_t {p}ICON_STRIDE = {istr};
inline constexpr std::uint16_t {p}ICON_X = {ix};
inline constexpr std::uint16_t {p}ICON_Y = {iy};

inline constexpr std::array<std::uint8_t, {ilen}> {p}ICON_BITS{{{{
{idata}
}}}};

static_assert({p}ICON_BITS.size() == static_cast<std::size_t>({p}ICON_STRIDE) * {p}ICON_H,
              "icon bitmap size disagrees with its stride and height");
static_assert({p}ICON_STRIDE * 8 >= {p}ICON_W, "stride is too narrow for the icon");

inline constexpr std::uint16_t {p}TEXT_W = {tw};
inline constexpr std::uint16_t {p}TEXT_H = {th};
inline constexpr std::uint16_t {p}TEXT_STRIDE = {tstr};
inline constexpr std::uint16_t {p}TEXT_X = {tx};
inline constexpr std::uint16_t {p}TEXT_Y = {ty};

inline constexpr std::array<std::uint8_t, {tlen}> {p}TEXT_BITS{{{{
{tdata}
}}}};

static_assert({p}TEXT_BITS.size() == static_cast<std::size_t>({p}TEXT_STRIDE) * {p}TEXT_H,
              "text bitmap size disagrees with its stride and height");
static_assert({p}TEXT_STRIDE * 8 >= {p}TEXT_W, "stride is too narrow for the text");
'''.format(p=prefix, icon_path=icon_path, line=line,
           iw=icon.size[0], ih=icon.size[1], istr=icon_stride, ix=icon_x, iy=ICON_Y,
           ilen=len(icon_bits), idata=as_c_array(icon_bits),
           tw=text.size[0], th=text.size[1], tstr=text_stride, tx=text_x, ty=TEXT_Y,
           tlen=len(text_bits), tdata=as_c_array(text_bits))

    # Preview in the real palette: e-ink black is ~18% reflectance and the
    # paper is bone, so previewing against #000/#FFF flatters the result.
    prev = Image.new('RGB', (PANEL_W, PANEL_H), (220, 218, 210))
    prev.paste(Image.new('RGB', icon.size, INK_RGB[ink]), (icon_x, ICON_Y), icon)
    prev.paste(Image.new('RGB', text.size, (45, 43, 42)), (text_x, TEXT_Y), text)
    return block, prev


def main():
    print("fetching font...")
    font_bytes = fetch(FONT_URL)

    blocks, total = [], 0
    for prefix, icon_path, line, icon_h, text_h, tracking, ink in CARDS:
        print("%s:" % line)
        block, prev = build_card(prefix, icon_path, line, icon_h, text_h,
                                 tracking, ink, font_bytes)
        blocks.append(block)
        name = (line.lower().replace(' ', '_')) + "_preview.png"
        prev.save(os.path.join(HERE, name))
        print("  wrote tools/%s" % name)

    header = '''#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/*
 * GENERATED by tools/gencard.py. Edit the script, not this file.
 *
 * Icons: Material Icons, rounded, Apache 2.0.
 * Type:  Jost weight 700, SIL OFL 1.1. Jost is the open Futura, which is what
 *        the enclosure back is engraved in.
 *
 * 1bpp, MSB first, whole-byte stride, so a row lookup is a shift and a mask.
 * Bitmaps rather than a font: each card draws exactly one fixed string.
 */

namespace polaroid::card::art {{
{blocks}
}}  // namespace polaroid::card::art
'''.format(blocks="".join(blocks))

    out_h = os.path.join(FIRMWARE, "include", "CardArt.h")
    with open(out_h, "w") as fh:
        fh.write(header)
    print("wrote %s" % out_h)


if __name__ == "__main__":
    main()
