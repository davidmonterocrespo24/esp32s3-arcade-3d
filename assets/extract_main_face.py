#!/usr/bin/env python3
"""
Extract the UV region of the LARGEST face (main wall surface) of each GLB.
The main wall is the biggest quad/triangle in the mesh.
"""
import struct, json, os, io
import numpy as np
from PIL import Image

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

def get_accessor_floats(gltf, bin_data, idx, n_comp):
    acc = gltf['accessors'][idx]
    bv  = gltf['bufferViews'][acc['bufferView']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', 0)
    elem_size = n_comp * 4
    result = []
    for i in range(acc['count']):
        base = offset + i * (stride if stride else elem_size)
        vals = struct.unpack_from(f'<{n_comp}f', bin_data, base)
        result.append(vals)
    return np.array(result, dtype=np.float32)

def get_accessor_indices(gltf, bin_data, idx):
    acc = gltf['accessors'][idx]
    bv  = gltf['bufferViews'][acc['bufferView']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', 0)
    ct = acc['componentType']
    if ct == 5123:   fmt, sz = 'H', 2
    elif ct == 5125: fmt, sz = 'I', 4
    else:            fmt, sz = 'B', 1
    result = []
    for i in range(acc['count']):
        base = offset + i * (stride if stride else sz)
        result.append(struct.unpack_from(f'<{fmt}', bin_data, base)[0])
    return np.array(result, dtype=np.int32)

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
    rgb565_data = [rgb888_to_rgb565(*pixels[x,y]) for y in range(target_size) for x in range(target_size)]
    header_file = f"{base_name}_texture.h"
    guard = f"{base_name.upper()}_TEXTURE_H"
    with open(header_file, 'w') as f:
        f.write(f"// Building texture: {base_name}\n// {target_size}x{target_size} RGB565\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n#include <Arduino.h>\n\n")
        f.write(f"#define {base_name.upper()}_TEX_W {target_size}\n")
        f.write(f"#define {base_name.upper()}_TEX_H {target_size}\n\n")
        f.write(f"const uint16_t PROGMEM {base_name}_texture[{len(rgb565_data)}] = {{\n")
        for i in range(0, len(rgb565_data), 8):
            f.write("  " + ", ".join(f"0x{v:04X}" for v in rgb565_data[i:i+8]) + ",\n")
        f.write("};\n\n")
        f.write(f"#endif // {guard}\n")
    print(f"  -> {header_file}")

def process(name):
    glb = f"{name}.glb"
    if not os.path.exists(glb):
        print(f"[{name}] NOT FOUND"); return

    gltf, bin_data = read_glb(glb)
    atlas = get_atlas(gltf, bin_data)
    aw, ah = atlas.size

    prim = gltf['meshes'][0]['primitives'][0]
    attrs = prim['attributes']

    positions = get_accessor_floats(gltf, bin_data, attrs['POSITION'], 3)
    uvs       = get_accessor_floats(gltf, bin_data, attrs['TEXCOORD_0'], 2)
    indices   = get_accessor_indices(gltf, bin_data, prim['indices'])

    # Group triangles
    tris = indices.reshape(-1, 3)

    # Find the triangle with the largest 3D area facing roughly +Z or -Z (front face)
    best_area = -1
    best_uv_bbox = None

    # Compute area of each triangle in 3D, keep top N by area, then get UV bounds
    areas = []
    for tri in tris:
        v0, v1, v2 = positions[tri[0]], positions[tri[1]], positions[tri[2]]
        cross = np.cross(v1 - v0, v2 - v0)
        area = np.linalg.norm(cross) * 0.5
        areas.append(area)
    areas = np.array(areas)

    # Take top 30% triangles by area — these form the main wall face
    threshold = np.percentile(areas, 70)
    main_tris = tris[areas >= threshold]

    # Get UV bounds of these triangles
    main_vert_ids = np.unique(main_tris.flatten())
    main_uvs = uvs[main_vert_ids]

    u_min, v_min = main_uvs.min(axis=0)
    u_max, v_max = main_uvs.max(axis=0)

    # Also print all unique UV clusters to understand structure
    # Find tight UV bbox of the largest single connected region
    # Strategy: find the single triangle with the biggest area, get its UV bbox
    biggest_tri_idx = np.argmax(areas)
    biggest_tri = tris[biggest_tri_idx]
    single_uvs = uvs[biggest_tri]
    su_min, sv_min = single_uvs.min(axis=0)
    su_max, sv_max = single_uvs.max(axis=0)

    print(f"[{name}]")
    print(f"  Biggest tri UV: u=[{su_min:.3f},{su_max:.3f}] v=[{sv_min:.3f},{sv_max:.3f}]")
    print(f"  Top-30%% tris UV: u=[{u_min:.3f},{u_max:.3f}] v=[{v_min:.3f},{v_max:.3f}]")

    # Use the top-30% region
    px0 = max(0, int(u_min * aw))
    py0 = max(0, int(v_min * ah))
    px1 = min(aw, int(u_max * aw))
    py1 = min(ah, int(v_max * ah))
    print(f"  Crop: ({px0},{py0})-({px1},{py1}) = {px1-px0}x{py1-py0}px")

    if px1 > px0 and py1 > py0:
        cropped = atlas.crop((px0, py0, px1, py1))
        save_header(cropped, name)
    print()

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

for t in targets:
    process(t)
