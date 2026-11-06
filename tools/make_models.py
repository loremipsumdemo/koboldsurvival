"""Makes simple low-poly prop models: the data/model/model_<n>.glb files that data/models.txt names for each prop.

Run it with:  python tools/make_models.py     (needs Python 3 and Pillow: pip install pillow)
Then build the game, which packs data/ into data.pak. The game only reads the finished .glb files and never runs this script.

Each prop in PROPS is one basic shape, its width, height and depth, and a color. To change a prop, edit its line and run this.
Props are drawn unlit, so each face gets a lighter or darker shade of the color from a small texture. The game scales a model's
largest side to the prop's size in src/props.h, so only the proportions here matter.

Running this overwrites the model of every prop in PROPS, including any replaced by hand.
"""
import io
import json
import math
import os
import struct
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(REPO, 'data')
SHADES = 8

# Each shape is an outline of (radius, height) points spun around the up axis, with a number of sides and a starting angle.
ROOF = math.sqrt(0.5)   # the radius that makes a four-sided shape exactly 1 wide
SHAPES = {
    'box': ([(0, 0), (ROOF, 0), (ROOF, 1), (0, 1)], 4, 45),
    'pyramid': ([(0, 0), (ROOF, 0), (0, 1)], 4, 45),
    'cylinder': ([(0, 0), (0.5, 0), (0.5, 1), (0, 1)], 8, 0),
    'cone': ([(0, 0), (0.5, 0), (0, 1)], 8, 0),
    'ball': ([(0, 0), (0.35, 0.15), (0.5, 0.5), (0.35, 0.85), (0, 1)], 8, 0),
}

PROPS = {
    'Wagon': ('box', 1.5, 0.8, 0.8, (150, 105, 65)),
    'Campfire': ('cone', 0.8, 0.7, 0.8, (230, 110, 40)),
    'Bedroll': ('box', 1.0, 0.15, 0.5, (95, 120, 75)),
    'Bed': ('box', 1.4, 0.45, 0.8, (70, 95, 150)),
    'Barrel': ('cylinder', 0.55, 0.7, 0.55, (150, 105, 65)),
    'Ore': ('ball', 1.0, 0.6, 0.8, (185, 110, 65)),
    'Tree': ('cone', 1.8, 3.0, 1.8, (60, 125, 60)),
    'Boulder': ('ball', 1.2, 0.8, 1.0, (130, 128, 122)),
    'Mushroom': ('cone', 0.5, 0.6, 0.5, (185, 135, 85)),
    'Bones': ('ball', 1.2, 0.25, 0.6, (230, 220, 190)),
    'Wall': ('box', 2.0, 1.3, 0.35, (150, 148, 140)),
    'Stall': ('pyramid', 2.0, 1.6, 1.1, (190, 70, 60)),
    'LockedChest': ('box', 0.9, 0.6, 0.55, (130, 85, 50)),
    'PrisonerCage': ('box', 1.2, 1.8, 1.2, (90, 95, 105)),
    'Chair': ('box', 0.45, 0.9, 0.45, (150, 105, 65)),
    'Bench': ('box', 1.5, 0.45, 0.36, (150, 105, 65)),
    'Stool': ('cylinder', 0.38, 0.5, 0.38, (170, 125, 80)),
    'Table': ('box', 1.5, 0.78, 0.82, (150, 105, 65)),
    'EndTable': ('box', 0.46, 0.7, 0.46, (110, 72, 45)),
    'RoundTable': ('cylinder', 1.2, 0.75, 1.2, (150, 105, 65)),
    'MetalTable': ('box', 1.5, 0.78, 0.78, (170, 178, 190)),
    'StandingTorch': ('cone', 0.4, 1.6, 0.4, (235, 150, 50)),
    'SupportBeam': ('box', 0.3, 2.5, 0.3, (110, 72, 45)),
    'TrainingDummy': ('cylinder', 0.5, 1.6, 0.5, (190, 160, 105)),
    'RawFoodStorage': ('box', 1.0, 1.0, 0.6, (175, 70, 70)),
    'FoodStorage': ('box', 0.85, 1.0, 0.4, (200, 165, 90)),
    'WeaponRack': ('box', 1.4, 1.0, 0.3, (120, 125, 135)),
    'ArmorStand': ('cylinder', 0.6, 1.7, 0.6, (175, 182, 195)),
    'WaterTank': ('cylinder', 1.2, 1.4, 1.2, (70, 130, 175)),
    'WaterPump': ('cylinder', 0.4, 1.4, 0.4, (80, 85, 95)),
    'Forge': ('pyramid', 1.6, 1.6, 1.0, (85, 80, 80)),
    'Altar': ('pyramid', 1.2, 1.2, 1.0, (150, 140, 170)),
    'Debris': ('ball', 1.0, 0.4, 1.0, (110, 100, 90)),
    'Shop': ('pyramid', 1.5, 1.6, 1.1, (165, 120, 75)),
    'Farm': ('box', 2.0, 0.2, 1.5, (105, 80, 50)),
    'Turbine': ('cone', 1.0, 2.0, 1.0, (205, 195, 165)),
    'Stove': ('box', 0.75, 1.2, 0.55, (70, 74, 84)),
}


def shape_faces(shape, width, height, depth):
    """The faces of a shape at its size, each a list of (x, y, z) points."""
    outline, sides, start = SHAPES[shape]
    faces = []
    for i in range(len(outline) - 1):
        (r0, y0), (r1, y1) = outline[i], outline[i + 1]
        for s in range(sides):
            ring = []
            for r, y, step in ((r0, y0, s), (r0, y0, s + 1), (r1, y1, s + 1), (r1, y1, s)):
                angle = math.radians(start) + 2 * math.pi * step / sides
                ring.append((r * math.cos(angle) * width, y * height, r * math.sin(angle) * depth))
            # At the axis two corners are the same point, so the face is a triangle
            face = [p for k, p in enumerate(ring) if p != ring[k - 1]]
            if len(face) >= 3:
                faces.append(face)
    return faces


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def face_normal(face, height):
    """The face's outward normal. The face's points are turned around if they ran the wrong way."""
    n = cross(sub(face[1], face[0]), sub(face[2], face[0]))
    length = math.sqrt(dot(n, n)) or 1.0
    n = (n[0] / length, n[1] / length, n[2] / length)
    middle = [sum(p[k] for p in face) / len(face) for k in range(3)]
    if dot(n, sub(middle, (0, height / 2, 0))) < 0:
        face.reverse()
        n = (-n[0], -n[1], -n[2])
    return n


def shade_index(n):
    light = 0.5 * (n[1] + 1) * 0.65 + 0.35 * max(0.0, n[0] * 0.6 + n[2] * 0.8)
    return min(SHADES - 1, int(light * SHADES))


def shade_texture(color):
    """A strip of the color from dark to light, as a PNG."""
    image = Image.new('RGB', (SHADES, 1))
    for i in range(SHADES):
        k = 0.55 + 0.6 * i / (SHADES - 1)
        image.putpixel((i, 0), tuple(min(255, int(c * k)) for c in color))
    out = io.BytesIO()
    image.save(out, 'PNG')
    return out.getvalue()


def write_glb(path, faces, height, color):
    pos, nrm, uv, idx = [], [], [], []
    for face in faces:
        n = face_normal(face, height)
        u = (shade_index(n) + 0.5) / SHADES
        base = len(pos) // 3
        for p in face:
            pos += p
            nrm += n
            uv += (u, 0.5)
        for k in range(1, len(face) - 1):
            idx += (base, base + k, base + k + 1)
    count = len(pos) // 3
    xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
    blobs = [struct.pack(f'<{len(pos)}f', *pos), struct.pack(f'<{len(nrm)}f', *nrm), struct.pack(f'<{len(uv)}f', *uv),
             struct.pack(f'<{len(idx)}H', *idx), shade_texture(color)]
    views, binary = [], b''
    for blob in blobs:
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(blob)})
        binary += blob + b'\0' * (-len(blob) % 4)
    doc = {
        'asset': {'version': '2.0', 'generator': 'tools/make_models.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}, 'indices': 3, 'material': 0}]}],
        'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}],
        'textures': [{'source': 0}],
        'images': [{'bufferView': 4, 'mimeType': 'image/png'}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3',
             'min': [min(xs), min(ys), min(zs)], 'max': [max(xs), max(ys), max(zs)]},
            {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': count, 'type': 'VEC2'},
            {'bufferView': 3, 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'},
        ],
        'bufferViews': views,
        'buffers': [{'byteLength': len(binary)}],
    }
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    with open(path, 'wb') as out:
        out.write(struct.pack('<III', 0x46546C67, 2, 28 + len(text) + len(binary)))
        out.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
        out.write(struct.pack('<II', len(binary), 0x004E4942) + binary)
    return len(idx) // 3


def main():
    """Writes the file data/models.txt names for each prop in PROPS. A prop missing from either one stops the script."""
    files = {}
    with open(os.path.join(DATA, 'models.txt'), encoding='utf-8') as f:
        for line in f:
            parts = line.split('\t')
            if not line.startswith('#') and len(parts) > 1 and not parts[0].startswith('Unit'):
                files[parts[0]] = parts[1]
    if set(files) != set(PROPS):
        raise SystemExit('PROPS and data/models.txt differ: ' + ', '.join(sorted(set(files) ^ set(PROPS))))
    for key, file in files.items():
        shape, width, height, depth, color = PROPS[key]
        tris = write_glb(os.path.join(DATA, 'model', file), shape_faces(shape, width, height, depth), height, color)
        print(f'{key:16s} {file:14s} {shape:9s} {tris} triangles')


if __name__ == '__main__':
    main()
