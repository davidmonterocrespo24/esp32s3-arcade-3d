#!/usr/bin/env python3
"""
Visualize UV regions of each GLB on the atlas, then extract clean crops.
"""
import struct, json, os, io
import numpy as np
from PIL import Image, ImageDraw, ImageFont

def read_glb(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic, version, length = struct.unpack_from('<III', data, 0)
    assert magic == 0x46546C67
    offset = 12
    chunks = {}
    while offset < len(data):
        chunk_len, chunk_type = struct.unpack_from('<II', data, offset)
        offset += 8
        chunks[chunk_type] = data[offset:offset+chunk_len]
        offset += chunk_len
    gltf = json.loads(chunks[0x4E4F534A].decode('utf-8'))
    bin_data = chunks.get(0x004E4942, b'')
    return gltf, bin_data

def get_uvs(gltf, bin_data):
    mesh = gltf['meshes'][0]['primitives'][0]
    attrs = mesh['attributes']
    if 'TEXCOORD_0' not in attrs:
        return None
    acc = gltf['accessors'][attrs['TEXCOORD_0']]
    bv  = gltf['bufferViews'][acc['bufferView']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', 0)
    elem_size = 8  # 2 floats
    uvs = []
    for i in range(acc['count']):
        base = offset + i * (stride if stride else elem_size)
        u, v = struct.unpack_from('<ff', bin_data, base)
        uvs.append((u, v))
    return np.array(uvs, dtype=np.float32)

def get_atlas(gltf, bin_data):
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
    rgb565_data = []
    for y in range(target_size):
        for x in range(target_size):
            r, g, b = pixels[x, y]
            rgb565_data.append(rgb888_to_rgb565(r, g, b))
    header_file = f"{base_name}_texture.h"
    guard = f"{base_name.upper()}_TEXTURE_H"
    with open(header_file, 'w') as f:
        f.write(f"// Building texture: {base_name}\n")
        f.write(f"// {target_size}x{target_size} RGB565\n")
        f.write(f"// Size: {len(rgb565_data)*2} bytes\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define {base_name.upper()}_TEX_W {target_size}\n")
        f.write(f"#define {base_name.upper()}_TEX_H {target_size}\n\n")
        f.write(f"const uint16_t PROGMEM {base_name}_texture[{len(rgb565_data)}] = {{\n")
        for i in range(0, len(rgb565_data), 8):
            line = rgb565_data[i:i+8]
            f.write("  " + ", ".join(f"0x{v:04X}" for v in line) + ",\n")
        f.write("};\n\n")
        f.write(f"#endif // {guard}\n")
    print(f"  -> {header_file}  ({target_size}x{target_size})")

# List of GLBs with the face-texture index we want
# We pick the face (triangle) that covers the main wall surface
# and extract its tight UV bounds
targets = [
    "stuco_wall",
    "stuco_wall_window_1",
    "stuco_wall_window_2",
    "stuco_wall_door",
    "stuco_wall_metal_door",
    "fancy_brick_wall_window_1",
    "fancy_brick_wall_window_2",
    "brick_wall",
    "brick_wall_window_1",
    "brick_wall_window_2",
]

# Load atlas once
gltf0, bin0 = read_glb("stuco_wall.glb")
atlas = get_atlas(gltf0, bin0)
aw, ah = atlas.size

# Build annotated overview
overview = atlas.copy().convert('RGBA')
draw = ImageDraw.Draw(overview)

colors_cycle = [
    (255,0,0,180),(0,200,0,180),(0,100,255,180),(255,165,0,180),
    (200,0,200,180),(0,200,200,180),(255,255,0,180),(255,80,80,180),
    (80,255,80,180),(80,80,255,180),
]

print("UV regions per model:")
print(f"{'Model':<35} {'u_min':>6} {'u_max':>6} {'v_min':>6} {'v_max':>6}  {'px_w':>5} {'px_h':>5}")
print("-"*75)

for i, name in enumerate(targets):
    glb = f"{name}.glb"
    if not os.path.exists(glb):
        continue
    gltf, bin_data = read_glb(glb)
    uvs = get_uvs(gltf, bin_data)
    if uvs is None:
        continue

    u_min, v_min = uvs.min(axis=0)
    u_max, v_max = uvs.max(axis=0)

    px0 = int(u_min * aw)
    py0 = int(v_min * ah)
    px1 = int(u_max * aw)
    py1 = int(v_max * ah)

    print(f"{name:<35} {u_min:>6.3f} {u_max:>6.3f} {v_min:>6.3f} {v_max:>6.3f}  {px1-px0:>5} {py1-py0:>5}")

    col = colors_cycle[i % len(colors_cycle)]
    draw.rectangle([px0, py0, px1, py1], outline=col[:3], width=3)
    draw.text((px0+2, py0+2), name.replace('_',' '), fill=col[:3])

    # Crop and save header
    cropped = atlas.crop((px0, py0, px1, py1))
    save_header(cropped, name)

overview.save("atlas_uv_map.png")
print("\nAtlas with UV regions: atlas_uv_map.png")
