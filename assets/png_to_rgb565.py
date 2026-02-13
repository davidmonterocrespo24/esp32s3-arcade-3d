#!/usr/bin/env python3
"""
png_to_rgb565.py — Convert a PNG image to a C header with RGB565 pixel data.
Resizes to 128x128 by default (nearest-neighbour).
Produces: car2_texture.h (or stdout)

Usage:
    python assets/png_to_rgb565.py assets/car2.png > car2_texture.h

Requires: Pillow  (pip install Pillow)
"""

import sys
import os

try:
    from PIL import Image
except ImportError:
    print('Error: Pillow not installed. Run: pip install Pillow', file=sys.stderr)
    sys.exit(1)

TARGET_W = 128
TARGET_H = 128

def to_rgb565(r, g, b):
    """Pack 8-bit R,G,B into a 16-bit RGB565 value (big-endian word)."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert(img_path, out, width=TARGET_W, height=TARGET_H):
    img = Image.open(img_path).convert('RGB')
    img = img.resize((width, height), Image.NEAREST)
    pixels = list(img.getdata())  # list of (r, g, b) tuples

    basename = os.path.basename(img_path)
    out.write('#pragma once\n')
    out.write(f'// {basename} converted to RGB565, {width}x{height}\n')
    out.write(f'#define CAR2_TEX_W {width}\n')
    out.write(f'#define CAR2_TEX_H {height}\n\n')
    out.write(f'static const uint16_t car2_texture[{width * height}] PROGMEM = {{\n')

    per_line = 16
    total    = width * height
    for i in range(0, total, per_line):
        chunk  = pixels[i:i + per_line]
        values = ', '.join(f'0x{to_rgb565(r,g,b):04X}' for (r,g,b) in chunk)
        comma  = ',' if i + per_line < total else ''
        out.write(f'{values}{comma}\n')

    out.write('};\n')

def main():
    if len(sys.argv) < 2:
        print(f'Usage: python {sys.argv[0]} <image.png> [width height]', file=sys.stderr)
        sys.exit(1)

    img_path = sys.argv[1]
    w = int(sys.argv[2]) if len(sys.argv) > 2 else TARGET_W
    h = int(sys.argv[3]) if len(sys.argv) > 3 else TARGET_H

    print(f'Converting {img_path} → {w}x{h} RGB565...', file=sys.stderr)
    convert(img_path, sys.stdout, w, h)
    print('Done.', file=sys.stderr)

if __name__ == '__main__':
    main()
