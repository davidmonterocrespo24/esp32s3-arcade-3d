#!/usr/bin/env python3
"""
Extract and optimize building textures from GLB files for ESP32
Reduces to small sizes and converts to RGB565
"""

import os
import sys
import glob
from PIL import Image
import trimesh

def rgb888_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def extract_texture_from_glb(glb_file):
    """Extract texture from GLB file"""
    try:
        mesh = trimesh.load(glb_file, force='mesh')
        if hasattr(mesh.visual, 'material'):
            mat = mesh.visual.material
            if hasattr(mat, 'baseColorTexture') and mat.baseColorTexture is not None:
                return mat.baseColorTexture
    except:
        pass
    return None

def process_texture(input_glb, target_size=64):
    """Process GLB texture: extract, resize, convert to RGB565"""
    
    base_name = os.path.splitext(os.path.basename(input_glb))[0]
    
    # Extract texture
    texture = extract_texture_from_glb(input_glb)
    if texture is None:
        print(f"No texture in {base_name}")
        return None
    
    print(f"\nProcessing: {base_name}")
    print(f"  Original size: {texture.size}")
    
    # Resize to target size
    texture = texture.resize((target_size, target_size), Image.Resampling.LANCZOS)
    texture = texture.convert('RGB')
    
    print(f"  Resized to: {target_size}x{target_size}")
    
    # Save preview PNG
    preview_file = f"{base_name}_{target_size}x{target_size}.png"
    texture.save(preview_file)
    print(f"  Preview saved: {preview_file}")
    
    # Convert to RGB565 array
    pixels = texture.load()
    rgb565_data = []
    
    for y in range(target_size):
        for x in range(target_size):
            r, g, b = pixels[x, y]
            rgb565 = rgb888_to_rgb565(r, g, b)
            rgb565_data.append(rgb565)
    
    # Generate C header
    header_file = f"{base_name}_texture.h"
    guard = f"{base_name.upper()}_TEXTURE_H"
    
    with open(header_file, 'w') as f:
        f.write(f"// Building texture: {base_name}\n")
        f.write(f"// {target_size}x{target_size} RGB565\n")
        f.write(f"// Size: {len(rgb565_data) * 2} bytes ({len(rgb565_data) * 2 / 1024:.1f}KB)\n\n")
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define {base_name.upper()}_TEX_W {target_size}\n")
        f.write(f"#define {base_name.upper()}_TEX_H {target_size}\n\n")
        
        # Store in PROGMEM (Flash) to save RAM
        f.write(f"const uint16_t PROGMEM {base_name}_texture[{len(rgb565_data)}] = {{\n")
        
        for i in range(0, len(rgb565_data), 8):
            line = rgb565_data[i:i+8]
            f.write("  " + ", ".join(f"0x{val:04X}" for val in line) + ",\n")
        
        f.write("};\n\n")
        f.write(f"#endif // {guard}\n")
    
    print(f"  Header saved: {header_file}")
    print(f"  Size: {len(rgb565_data) * 2} bytes ({len(rgb565_data) * 2 / 1024:.1f}KB)")
    
    return header_file

def main():
    print("=" * 70)
    print("Building Texture Extractor for ESP32-S3")
    print("=" * 70)
    
    # Find all stuco_wall GLB files
    glb_files = glob.glob("stuco_wall*.glb")
    
    if not glb_files:
        print("No stuco_wall*.glb files found!")
        return
    
    print(f"\nFound {len(glb_files)} wall textures\n")
    
    # Process interesting ones
    interesting = [
        "stuco_wall.glb",
        "stuco_wall_window_1.glb",
        "stuco_wall_window_2.glb",
        "stuco_wall_door.glb",
        "stuco_wall_metal_door.glb",
        "fancy_brick_wall_window_1.glb",
        "fancy_brick_wall_window_2.glb",
    ]
    
    processed = []
    for glb in interesting:
        if os.path.exists(glb):
            result = process_texture(glb, target_size=64)
            if result:
                processed.append(result)
    
    print("\n" + "=" * 70)
    print(f"Processed {len(processed)} textures")
    print(f"Total size: {len(processed) * 8} KB in PROGMEM (Flash)")
    print("\nThese textures can be used in render_building.cpp")
    print("to add more realistic building facades!")
    print("=" * 70)

if __name__ == "__main__":
    main()
