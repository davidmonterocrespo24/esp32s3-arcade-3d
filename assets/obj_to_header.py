#!/usr/bin/env python3
"""
obj_to_header.py — Convert a .obj file to a C header with flat vertex+UV arrays.
Produces: car2_mesh.h (or stdout)

Usage:
    python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h

The OBJ face entries (v/vt/vn or v/vt) are expanded into unique
(position + UV) combinations so the renderer can use a flat index array
with no runtime parsing.
"""

import sys
import os

def parse_obj(path):
    positions = []   # list of (x, y, z)
    uvs       = []   # list of (u, v)
    faces     = []   # list of [(vi, ti), (vi, ti), (vi, ti)]  — triangulated

    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('v '):
                parts = line.split()
                positions.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif line.startswith('vt '):
                parts = line.split()
                uvs.append((float(parts[1]), float(parts[2])))
            elif line.startswith('f '):
                parts = line.split()[1:]
                # Parse each vertex token: v, v/vt, v/vt/vn, v//vn
                def parse_token(tok):
                    idx = tok.split('/')
                    vi = int(idx[0]) - 1
                    ti = int(idx[1]) - 1 if len(idx) > 1 and idx[1] != '' else 0
                    return (vi, ti)
                verts = [parse_token(p) for p in parts]
                # Triangulate (fan)
                for i in range(1, len(verts) - 1):
                    faces.append([verts[0], verts[i], verts[i + 1]])

    return positions, uvs, faces

def build_unique_verts(positions, uvs, faces):
    """Expand faces into unique (pos+UV) vertices and index triples."""
    unique = {}   # (vi, ti) -> new index
    verts  = []   # list of (x,y,z,u,v)
    indices = []  # flat list of ints (3 per triangle)

    for face in faces:
        for (vi, ti) in face:
            key = (vi, ti)
            if key not in unique:
                unique[key] = len(verts)
                px, py, pz = positions[vi]
                u, v = uvs[ti] if uvs else (0.0, 0.0)
                verts.append((px, py, pz, u, v))
            indices.append(unique[key])

    return verts, indices

def write_header(verts, indices, obj_path, out):
    vert_count = len(verts)
    tri_count  = len(indices) // 3
    name       = os.path.splitext(os.path.basename(obj_path))[0]

    out.write('#pragma once\n')
    out.write(f'// Auto-generated from {os.path.basename(obj_path)}\n')
    out.write(f'// {vert_count} vertices, {tri_count} triangles\n\n')
    out.write('struct Car2Vertex { float x, y, z, u, v; };\n\n')
    out.write(f'static const Car2Vertex car2_verts[{vert_count}] = {{\n')

    for i, (x, y, z, u, v) in enumerate(verts):
        comma = ',' if i < vert_count - 1 else ''
        out.write(f'  {{{x:.5f}f, {y:.5f}f, {z:.5f}f, {u:.5f}f, {v:.5f}f}}{comma}\n')

    out.write('};\n\n')
    out.write(f'static const uint16_t car2_indices[{len(indices)}] = {{\n')

    per_line = 12
    for i in range(0, len(indices), per_line):
        chunk = indices[i:i + per_line]
        line  = ', '.join(str(x) for x in chunk)
        comma = ',' if i + per_line < len(indices) else ''
        out.write(f'  {line}{comma}\n')

    out.write('};\n\n')
    out.write(f'static const int car2_vert_count = {vert_count};\n')
    out.write(f'static const int car2_tri_count  = {tri_count};\n')

def main():
    if len(sys.argv) < 2:
        print(f'Usage: python {sys.argv[0]} <file.obj>', file=sys.stderr)
        sys.exit(1)

    obj_path = sys.argv[1]
    positions, uvs, faces = parse_obj(obj_path)
    verts, indices = build_unique_verts(positions, uvs, faces)

    print(f'Parsed: {len(positions)} positions, {len(uvs)} UVs, '
          f'{len(faces)} tris → {len(verts)} unique verts', file=sys.stderr)

    write_header(verts, indices, obj_path, sys.stdout)

if __name__ == '__main__':
    main()
