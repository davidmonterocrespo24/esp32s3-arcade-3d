#!/usr/bin/env python3
"""
Manually crop specific regions from the houses atlas based on visual inspection.
Each crop is a clean, tileable wall texture for use in render_building.cpp
"""
import struct, json, io, os
import numpy as np
from PIL import Image

def read_glb(path):
    with open(path, 'rb') as f: data = f.read()
    magic, version, length = struct.unpack_from('<III', data, 0)
    assert magic == 0x46546C67
    offset = 12; chunks = {}
    while offset < len(data):
        chunk_len, chunk_type = struct.unpack_from('<II', data, offset)
        offset += 8; chunks[chunk_type] = data[offset:offset+chunk_len]; offset += chunk_len
    gltf = json.loads(chunks[0x4E4F534A].decode('utf-8'))
    return gltf, chunks.get(0x004E4942, b'')

def get_atlas():
    gltf, bin_data = read_glb("stuco_wall.glb")
    img_info = gltf['images'][0]
    bv = gltf['bufferViews'][img_info['bufferView']]
    img_bytes = bin_data[bv['byteOffset']:bv['byteOffset']+bv['byteLength']]
    return Image.open(io.BytesIO(img_bytes)).convert('RGB')

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def save_header(img, base_name, target_size=64):
    resized = img.resize((target_size, target_size), Image.Resampling.LANCZOS)
    resized.save(f"{base_name}_{target_size}x{target_size}.png")
    pixels = resized.load()
    rgb565_data = [rgb888_to_rgb565(*pixels[x,y]) for y in range(target_size) for x in range(target_size)]
    guard = f"{base_name.upper()}_TEXTURE_H"
    with open(f"{base_name}_texture.h", 'w') as f:
        f.write(f"// Building texture: {base_name}\n// {target_size}x{target_size} RGB565\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n#include <Arduino.h>\n\n")
        f.write(f"#define {base_name.upper()}_TEX_W {target_size}\n")
        f.write(f"#define {base_name.upper()}_TEX_H {target_size}\n\n")
        f.write(f"const uint16_t PROGMEM {base_name}_texture[{len(rgb565_data)}] = {{\n")
        for i in range(0, len(rgb565_data), 8):
            f.write("  " + ", ".join(f"0x{v:04X}" for v in rgb565_data[i:i+8]) + ",\n")
        f.write("};\n\n")
        f.write(f"#endif // {guard}\n")
    print(f"  Saved: {base_name}_texture.h  ({target_size}x{target_size})")

atlas = get_atlas()
aw, ah = atlas.size  # 1024x1024
print(f"Atlas: {aw}x{ah}")

# Manual UV crops identified from atlas visual inspection + triangle analysis
# Format: (name, u_min, u_max, v_min, v_max) in 0..1 coords
# These are the clean per-panel regions from the triangle data:
#
# stuco wall plain:          u=[0.268,0.388] v=[0.002,0.123]  — plain stucco
# stuco wall section:        u=[0.349,0.389] v=[0.026,0.108]  — single stucco panel
# stuco window panel:        u=[0.392,0.448] v=[0.126,0.242]  — window on stucco
# fancy brick plain:         u=[0.268,0.308] v=[0.026,0.108]  — fancy brick panel
# fancy brick window:        u=[0.392,0.448] v=[0.126,0.372]  — brick+window
# brick plain:               u=[0.393,0.514] v=[0.002,0.123]  — plain brick
# brick window:              u=[0.392,0.448] v=[0.126,0.242]  — window on brick  (same region as stuco window!)
# door (blue):               u=[0.000,0.126] v=[0.002,0.123]  — blue door
# door (metal/roller):       u=[0.000,0.126] v=[0.126,0.372]  — metal roller door
# concrete/tiles:            u=[0.519,0.640] v=[0.000,0.123]  — right side of atlas

crops = [
    # name                         u0      u1      v0      v1
    ("stuco_wall",                0.268,  0.388,  0.002,  0.123),
    ("stuco_wall_window_1",       0.392,  0.448,  0.126,  0.242),  # single window panel
    ("stuco_wall_window_2",       0.268,  0.389,  0.026,  0.123),  # plain stucco section
    ("stuco_wall_door",           0.000,  0.126,  0.002,  0.123),  # blue door
    ("stuco_wall_metal_door",     0.000,  0.126,  0.126,  0.372),  # metal roller door
    ("fancy_brick_wall_window_1", 0.392,  0.448,  0.126,  0.372),  # brick + window
    ("fancy_brick_wall_window_2", 0.268,  0.308,  0.026,  0.108),  # fancy brick plain
    ("brick_wall",                0.393,  0.514,  0.002,  0.123),  # plain brick
    ("brick_wall_window_1",       0.392,  0.514,  0.126,  0.242),  # brick window
    ("brick_wall_window_2",       0.126,  0.267,  0.002,  0.123),  # different brick section
]

for name, u0, u1, v0, v1 in crops:
    px0 = int(u0 * aw); py0 = int(v0 * ah)
    px1 = int(u1 * aw); py1 = int(v1 * ah)
    print(f"[{name}]  ({px0},{py0})-({px1},{py1}) = {px1-px0}x{py1-py0}px")
    cropped = atlas.crop((px0, py0, px1, py1))
    save_header(cropped, name)
    print()
