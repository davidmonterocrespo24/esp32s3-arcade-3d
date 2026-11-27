#!/usr/bin/env python3
"""
Extract the UV region used by each GLB wall panel from the shared 'houses' atlas.
Crops that region and saves as 64x64 RGB565 header.
"""
import struct, json, os, io, sys
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

def get_accessor_data(gltf, bin_data, accessor_idx):
    acc = gltf['accessors'][accessor_idx]
    bv  = gltf['bufferViews'][acc['bufferView']]
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', 0)

    type_map = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
    comp_map = {5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
    n_comp   = type_map[acc['type']]
    fmt, comp_size = comp_map[acc['componentType']]
    elem_size = n_comp * comp_size

    data_out = []
    for i in range(acc['count']):
        if stride and stride != elem_size:
            base = offset + i * stride
        else:
            base = offset + i * elem_size
        vals = struct.unpack_from(f'<{n_comp}{fmt}', bin_data, base)
        data_out.append(vals)
    return data_out

def extract_uv_crop(glb_path, target_size=64):
    gltf, bin_data = read_glb(glb_path)

    # Extract the atlas image
    img_info = gltf['images'][0]
    bv = gltf['bufferViews'][img_info['bufferView']]
    img_bytes = bin_data[bv['byteOffset']:bv['byteOffset']+bv['byteLength']]
    atlas = Image.open(io.BytesIO(img_bytes)).convert('RGB')
    atlas_w, atlas_h = atlas.size

    # Find TEXCOORD_0 accessor
    mesh = gltf['meshes'][0]['primitives'][0]
    attrs = mesh['attributes']
    if 'TEXCOORD_0' not in attrs:
        print(f"  No TEXCOORD_0")
        return None

    uvs = get_accessor_data(gltf, bin_data, attrs['TEXCOORD_0'])
    uvs_np = np.array(uvs, dtype=np.float32)

    u_min, v_min = uvs_np.min(axis=0)
    u_max, v_max = uvs_np.max(axis=0)

    # Clamp to [0,1]
    u_min = max(0.0, min(1.0, u_min))
    v_min = max(0.0, min(1.0, v_min))
    u_max = max(0.0, min(1.0, u_max))
    v_max = max(0.0, min(1.0, v_max))

    # Pixel crop box
    px0 = int(u_min * atlas_w)
    py0 = int(v_min * atlas_h)
    px1 = int(u_max * atlas_w)
    py1 = int(v_max * atlas_h)

    print(f"  UV range: u=[{u_min:.3f},{u_max:.3f}] v=[{v_min:.3f},{v_max:.3f}]")
    print(f"  Pixel crop: ({px0},{py0})-({px1},{py1}) = {px1-px0}x{py1-py0}px")

    if px1 <= px0 or py1 <= py0:
        print("  Degenerate crop, skipping")
        return None

    cropped = atlas.crop((px0, py0, px1, py1))
    resized = cropped.resize((target_size, target_size), Image.Resampling.LANCZOS)
    return resized

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def save_header(img, base_name, target_size=64):
    pixels = img.load()
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
        f.write(f"// Size: {len(rgb565_data)*2} bytes ({len(rgb565_data)*2/1024:.1f}KB)\n\n")
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
    print(f"  -> Saved: {header_file}")

    # Save preview PNG
    preview = f"{base_name}_{target_size}x{target_size}.png"
    img.save(preview)
    print(f"  -> Preview: {preview}")

def main():
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
        "concrete_foot_wall",
    ]

    for name in targets:
        glb = f"{name}.glb"
        if not os.path.exists(glb):
            print(f"[{glb}] NOT FOUND, skipping")
            continue
        print(f"[{name}]")
        img = extract_uv_crop(glb, target_size=64)
        if img:
            save_header(img, name)
        print()

if __name__ == '__main__':
    main()
