#!/usr/bin/env python3
"""
Inspect GLB files and extract all embedded textures correctly.
GLB format: 12-byte header + JSON chunk + BIN chunk
"""
import struct, json, os, sys
from PIL import Image
import io

def read_glb(path):
    with open(path, 'rb') as f:
        data = f.read()

    # GLB header: magic(4) + version(4) + length(4)
    magic, version, length = struct.unpack_from('<III', data, 0)
    assert magic == 0x46546C67, "Not a GLB file"

    offset = 12
    chunks = {}
    while offset < len(data):
        chunk_len, chunk_type = struct.unpack_from('<II', data, offset)
        offset += 8
        chunk_data = data[offset:offset+chunk_len]
        offset += chunk_len
        chunks[chunk_type] = chunk_data

    json_chunk = json.loads(chunks[0x4E4F534A].decode('utf-8'))  # JSON
    bin_chunk  = chunks.get(0x004E4942, b'')                      # BIN

    return json_chunk, bin_chunk

def extract_all_images(glb_path):
    gltf, bin_data = read_glb(glb_path)

    images_info = gltf.get('images', [])
    buffer_views = gltf.get('bufferViews', [])
    images_out = []

    for i, img in enumerate(images_info):
        name = img.get('name', f'image_{i}')
        mime = img.get('mimeType', 'image/png')
        bv_idx = img.get('bufferView')
        if bv_idx is None:
            print(f"  Image {i} '{name}': no bufferView, skipping")
            continue
        bv = buffer_views[bv_idx]
        start = bv['byteOffset']
        length = bv['byteLength']
        img_bytes = bin_data[start:start+length]
        pil_img = Image.open(io.BytesIO(img_bytes)).convert('RGBA')
        images_out.append((name, mime, pil_img))
        print(f"  Image {i} '{name}': {pil_img.size}, {mime}")

    return images_out

def main():
    glb_files = sorted([f for f in os.listdir('.') if f.endswith('.glb')])
    print(f"Found {len(glb_files)} GLB files\n")

    for glb in glb_files:
        print(f"[{glb}]")
        try:
            imgs = extract_all_images(glb)
            if not imgs:
                print("  (no images)")
        except Exception as e:
            print(f"  ERROR: {e}")
        print()

if __name__ == '__main__':
    main()
