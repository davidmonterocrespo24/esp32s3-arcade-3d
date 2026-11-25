#!/usr/bin/env python3
"""
obj_to_header.py — Convert a .obj file to a C header with flat vertex+UV arrays
plus smoothed vertex normals (for Gouraud lighting and specular highlights).
Produces: car2_mesh.h (or stdout)

Usage:
    python assets/obj_to_header.py assets/Car2.obj > car2_mesh.h

The OBJ face entries (v/vt/vn or v/vt) are expanded into unique
(position + UV) combinations so the renderer can use a flat index array
with no runtime parsing.

Normals are computed here (not taken from the OBJ) with a crease angle:
faces sharing a position are averaged only if they bend less than
CREASE_DEG, so panel edges stay sharp while curved panels shade smoothly.
They are stored as int8 (x127) — 3 bytes per vertex in flash.
"""

import math
import os
import sys

CREASE_DEG = 50.0


def parse_obj(path):
    positions = []   # list of (x, y, z)
    uvs = []         # list of (u, v)
    faces = []       # list of [(vi, ti), (vi, ti), (vi, ti)]  — triangulated

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

                def parse_token(tok):
                    idx = tok.split('/')
                    vi = int(idx[0]) - 1
                    ti = int(idx[1]) - 1 if len(idx) > 1 and idx[1] != '' else 0
                    return (vi, ti)
                verts = [parse_token(p) for p in parts]
                for i in range(1, len(verts) - 1):          # fan triangulation
                    faces.append([verts[0], verts[i], verts[i + 1]])

    return positions, uvs, faces


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 1.0, 0.0)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def build_unique_verts(positions, uvs, faces):
    """Expand faces into unique (pos+UV) vertices, index triples and normals."""
    # Area-weighted face normals (cross product length = 2 * area)
    face_n = []
    for face in faces:
        a, b, c = (positions[vi] for (vi, _) in face)
        face_n.append(cross(sub(b, a), sub(c, a)))
    unit_n = [norm(n) for n in face_n]

    faces_at = {}
    for fi, face in enumerate(faces):
        for (vi, _) in face:
            faces_at.setdefault(vi, []).append(fi)

    cos_crease = math.cos(math.radians(CREASE_DEG))
    unique = {}
    verts = []
    acc = []        # accumulated normal per unique vertex
    indices = []

    for fi, face in enumerate(faces):
        for (vi, ti) in face:
            key = (vi, ti)
            if key not in unique:
                unique[key] = len(verts)
                px, py, pz = positions[vi]
                u, v = uvs[ti] if uvs else (0.0, 0.0)
                verts.append((px, py, pz, u, v))
                acc.append([0.0, 0.0, 0.0])
            idx = unique[key]
            indices.append(idx)
            # corner normal: neighbours within the crease angle of this face
            n = [0.0, 0.0, 0.0]
            for g in faces_at[vi]:
                if dot(unit_n[fi], unit_n[g]) >= cos_crease:
                    n[0] += face_n[g][0]
                    n[1] += face_n[g][1]
                    n[2] += face_n[g][2]
            n = norm(n)
            acc[idx][0] += n[0]
            acc[idx][1] += n[1]
            acc[idx][2] += n[2]

    normals = [norm(a) for a in acc]
    return verts, indices, normals


def write_header(verts, indices, normals, obj_path, out):
    vert_count = len(verts)
    tri_count = len(indices) // 3

    out.write('#pragma once\n')
    out.write(f'// Auto-generated from {os.path.basename(obj_path)} by assets/obj_to_header.py\n')
    out.write(f'// {vert_count} vertices, {tri_count} triangles, normals crease {CREASE_DEG:.0f} deg\n\n')
    out.write('#include <stdint.h>\n\n')
    out.write('struct Car2Vertex { float x, y, z, u, v; };\n\n')
    out.write(f'static const Car2Vertex car2_verts[{vert_count}] = {{\n')
    for i, (x, y, z, u, v) in enumerate(verts):
        comma = ',' if i < vert_count - 1 else ''
        out.write(f'  {{{x:.5f}f, {y:.5f}f, {z:.5f}f, {u:.5f}f, {v:.5f}f}}{comma}\n')
    out.write('};\n\n')

    out.write(f'// Smoothed vertex normals, int8 * 127\n')
    out.write(f'static const int8_t car2_normals[{vert_count * 3}] = {{\n')
    per_line = 8
    flat = []
    for (nx, ny, nz) in normals:
        flat += [int(round(nx * 127)), int(round(ny * 127)), int(round(nz * 127))]
    for i in range(0, len(flat), per_line * 3):
        chunk = flat[i:i + per_line * 3]
        comma = ',' if i + per_line * 3 < len(flat) else ''
        out.write('  ' + ', '.join(str(x) for x in chunk) + comma + '\n')
    out.write('};\n\n')

    out.write(f'static const uint16_t car2_indices[{len(indices)}] = {{\n')
    per_line = 12
    for i in range(0, len(indices), per_line):
        chunk = indices[i:i + per_line]
        comma = ',' if i + per_line < len(indices) else ''
        out.write('  ' + ', '.join(str(x) for x in chunk) + comma + '\n')
    out.write('};\n\n')
    out.write(f'static const int car2_vert_count = {vert_count};\n')
    out.write(f'static const int car2_tri_count  = {tri_count};\n')


def main():
    if len(sys.argv) < 2:
        print(f'Usage: python {sys.argv[0]} <file.obj>', file=sys.stderr)
        sys.exit(1)

    obj_path = sys.argv[1]
    positions, uvs, faces = parse_obj(obj_path)
    verts, indices, normals = build_unique_verts(positions, uvs, faces)

    print(f'Parsed: {len(positions)} positions, {len(uvs)} UVs, '
          f'{len(faces)} tris -> {len(verts)} unique verts', file=sys.stderr)

    write_header(verts, indices, normals, obj_path, sys.stdout)


if __name__ == '__main__':
    main()
