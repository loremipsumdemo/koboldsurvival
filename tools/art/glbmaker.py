#!/usr/bin/env python3
"""
Block Model Workbench: simple GLB shapes with wear cuts, stretching,
face extension and procedural textures.

Install once:
    pip install trimesh numpy pillow manifold3d scipy

Run:
    python cube_glb_gui.py

Shape      Block (beveled cube), Sphere, Cylinder, Cone.
Stretch    Scale along X, Y, Z. New shape, or the current model.
Extend     Mark faces in the viewer, then push or pull them along their normals.
Cuts       V-notch, Round bite, Groove. Random or at marked spots. Undo steps back.
Texture    14 procedural patterns. Colours and pattern are randomized on start
           and on each new shape (toggle in the sidebar). Pick colours from the
           colour wheel or type hex values. Replace make_texture() with your
           real textures later.
Viewer     Left-drag orbit, right-drag pan, W/A/S/D move, mouse wheel zoom.
           The sidebar scrolls with the mouse wheel.
"""
import colorsys
import itertools
import math
import os
import queue
import random
import sys
import threading

import numpy as np
import trimesh
from PIL import Image

SHAPES = ("Block", "Cube", "Sphere", "Ellipsoid", "Cylinder", "Cone", "Frustum", "Capsule",
          "Dome", "Torus", "Tube", "Square pyramid", "Tetrahedron", "Octahedron",
          "Icosahedron", "Triangular prism", "Hexagonal prism", "Octagonal prism",
          "Wedge", "Rock")
CUT_TYPES = ("V-notch", "Round bite", "Groove")
PATTERNS = ("Wood", "Planks", "Bricks", "Tiles", "Stone", "Marble", "Rust", "Camo",
            "Concrete", "Speckle", "Dots", "Diagonal", "Checker", "Stripes")
TEX_PX = 256


# ---------------------------------------------------------------- shapes

def make_beveled_cube(size, bevel_frac):
    """Cube with a flat bevel on every edge (convex hull of truncated-corner points)."""
    h = size / 2
    k = h - size * bevel_frac
    pts = []
    for corner in itertools.product((-1, 1), repeat=3):
        for axis in range(3):
            p = np.array(corner, dtype=float) * k
            p[axis] = corner[axis] * h
            pts.append(p)
    return trimesh.convex.convex_hull(np.array(pts))


def ensure_outward(mesh):
    """Make winding consistent and face normals point outward (positive volume)."""
    fixed = trimesh.Trimesh(vertices=mesh.vertices, faces=mesh.faces.copy(), process=False)
    trimesh.repair.fix_normals(fixed)
    if fixed.volume < 0:
        fixed.invert()
    return fixed


def scale_mesh(mesh, factors):
    """Stretch vertices along X, Y, Z. Topology is unchanged."""
    return trimesh.Trimesh(vertices=mesh.vertices * np.array(factors),
                           faces=mesh.faces, process=False)


UPRIGHT = trimesh.transformations.rotation_matrix(-math.pi / 2, [1, 0, 0])  # trimesh Z-up to Y-up
ANCHOR_PRESETS = {
    "Bottom centre": (0.0, -1.0, 0.0),
    "Centre": (0.0, 0.0, 0.0),
    "Top centre": (0.0, 1.0, 0.0),
    "Bottom front-left": (-1.0, -1.0, 1.0),
    "Bottom back-right": (1.0, -1.0, -1.0),
}


def anchor_point(mesh, fx, fy, fz):
    """
    Anchor in model space. Each fraction is -1 (min) .. 0 (middle) .. +1 (max).
    All zeros = middle of the bounding box. Otherwise the anchor is the shape's own
    extreme vertex in that direction (e.g. bottom centre = the lowest vertex, using its
    own X and Z). Vertices tied for the extreme (like a flat base) are averaged, so
    the anchor always sits on the shape.
    """
    lo, hi = mesh.bounds
    f = np.array([fx, fy, fz], dtype=float)
    center = (lo + hi) / 2
    if np.allclose(f, 0):
        return center
    half = np.where((hi - lo) / 2 > 0, (hi - lo) / 2, 1.0)
    norm = (mesh.vertices - center) / half  # every axis in -1..1
    score = norm @ f
    tol = 0.02 * np.abs(f).sum()
    picked = mesh.vertices[score >= score.max() - tol]
    return picked.mean(axis=0)


def _ring(n, radius, y, phase=0.0):
    a = phase + 2 * np.pi * np.arange(n) / n
    return np.column_stack([radius * np.cos(a), np.full(n, y), radius * np.sin(a)])


def _hull(points):
    return trimesh.convex.convex_hull(np.asarray(points, dtype=float))


def _upright(mesh):
    mesh.apply_transform(UPRIGHT)
    return mesh


def _prism(n):
    return lambda s, b, rng: _hull(np.vstack([_ring(n, s / 2, -s / 2), _ring(n, s / 2, s / 2)]))


def _dome(s):
    """Upper hemisphere built from exact horizontal rings, so the base is perfectly flat."""
    r = s / 2
    rings = [_ring(32, r * np.cos(p), r * np.sin(p)) for p in np.linspace(0, np.pi / 2, 6)[:-1]]
    return _hull(np.vstack(rings + [[[0.0, r, 0.0]]]))


def _tube(s):
    outer = _upright(trimesh.creation.cylinder(radius=s / 2, height=s, sections=32))
    inner = _upright(trimesh.creation.cylinder(radius=s * 0.3, height=s * 1.2, sections=32))
    result = trimesh.boolean.difference([outer, inner], engine="manifold")
    return result if result is not None and len(result.faces) else outer


def _rock(s, rng):
    m = trimesh.creation.icosphere(subdivisions=3, radius=s / 2)
    d = m.vertices / np.linalg.norm(m.vertices, axis=1, keepdims=True)
    x, y, z = d[:, 0], d[:, 1], d[:, 2]
    p = [rng.uniform(0, 2 * math.pi) for _ in range(4)]
    disp = (1 + 0.18 * np.sin(3 * x + p[0]) * np.cos(2 * y + p[1])
            + 0.10 * np.sin(5 * z + 2 * x + p[2]) + 0.05 * np.sin(9 * y + p[3]))
    return trimesh.Trimesh(vertices=d * (s / 2) * disp[:, None], faces=m.faces, process=False)


SHAPE_BUILDERS = {
    "Block": lambda s, b, rng: make_beveled_cube(s, b),
    "Cube": lambda s, b, rng: trimesh.creation.box(extents=(s, s, s)),
    "Sphere": lambda s, b, rng: trimesh.creation.icosphere(subdivisions=3, radius=s / 2),
    "Ellipsoid": lambda s, b, rng: scale_mesh(
        trimesh.creation.icosphere(subdivisions=3, radius=s / 2), (0.8, 1.3, 0.8)),
    "Cylinder": lambda s, b, rng: _upright(trimesh.creation.cylinder(radius=s / 2, height=s, sections=32)),
    "Cone": lambda s, b, rng: _upright(trimesh.creation.cone(radius=s / 2, height=s, sections=32)),
    "Frustum": lambda s, b, rng: _hull(np.vstack([_ring(32, s / 2, -s / 2), _ring(32, s / 4, s / 2)])),
    "Capsule": lambda s, b, rng: _upright(
        trimesh.creation.capsule(height=s / 2, radius=s / 4, count=[16, 16])),
    "Dome": lambda s, b, rng: _dome(s),
    "Torus": lambda s, b, rng: _upright(
        trimesh.creation.torus(major_radius=s * 0.35, minor_radius=s * 0.15)),
    "Tube": lambda s, b, rng: _tube(s),
    "Square pyramid": lambda s, b, rng: _hull(
        [[-s / 2, -s / 2, -s / 2], [s / 2, -s / 2, -s / 2], [s / 2, -s / 2, s / 2],
         [-s / 2, -s / 2, s / 2], [0, s / 2, 0]]),
    "Tetrahedron": lambda s, b, rng: _hull(
        np.array([[1, 1, 1], [1, -1, -1], [-1, 1, -1], [-1, -1, 1]], dtype=float) * s / 2),
    "Octahedron": lambda s, b, rng: _hull(np.vstack([np.eye(3), -np.eye(3)]) * s / 2),
    "Icosahedron": lambda s, b, rng: trimesh.creation.icosphere(subdivisions=0, radius=s / 2),
    "Triangular prism": _prism(3),
    "Hexagonal prism": _prism(6),
    "Octagonal prism": _prism(8),
    "Wedge": lambda s, b, rng: _hull(
        [[-s / 2, -s / 2, s / 2], [s / 2, -s / 2, s / 2], [-s / 2, -s / 2, -s / 2],
         [s / 2, -s / 2, -s / 2], [-s / 2, s / 2, -s / 2], [s / 2, s / 2, -s / 2]]),
    "Rock": lambda s, b, rng: _rock(s, rng),
}


def make_shape(kind, size, bevel_frac, factors, seed=0):
    """Build a standard shape, stretch it, and centre its bounding box on the origin."""
    m = SHAPE_BUILDERS[kind](size, bevel_frac, random.Random(seed))
    lo, hi = m.bounds
    m = trimesh.Trimesh(vertices=m.vertices - (lo + hi) / 2, faces=m.faces, process=False)
    return ensure_outward(scale_mesh(m, factors))


def random_merge(rng, count=None):
    """
    Union of 2-5 random shapes. Each is rotated and offset so it overlaps the others;
    the boolean union cuts the seam where they meet and removes the hidden faces.
    Returns (mesh, list of shape names).
    """
    n = count or rng.randint(2, 5)
    parts, names = [], []
    for i in range(n):
        kind = rng.choice(SHAPES)
        size = rng.uniform(0.6, 1.4)
        factors = [round(rng.uniform(0.6, 1.6) * 20) / 20 for _ in range(3)]
        m = make_shape(kind, size, rng.uniform(0.05, 0.25), factors, rng.randint(0, 9999))
        m.apply_transform(trimesh.transformations.rotation_matrix(
            rng.uniform(0, 2 * math.pi), [0, 1, 0]))
        if i:
            d = np.array([rng.gauss(0, 1) for _ in range(3)])
            m.apply_translation(d / np.linalg.norm(d) * rng.uniform(0.2, 0.7))
        parts.append(m)
        names.append(kind)
    merged = trimesh.boolean.union(parts, engine="manifold")
    if merged is None or len(merged.faces) == 0:
        raise RuntimeError("The shapes did not merge. Try again.")
    return ensure_outward(merged), names


def generate_model(rng, anchor_fracs):
    """One random export-ready model: a single shape or a merge, 0-3 random cuts, a random texture."""
    if rng.random() < 0.3:
        mesh, _ = random_merge(rng)
        shape = "Merged"
    else:
        shape = rng.choice(SHAPES)
        factors = [round(rng.uniform(0.6, 1.6) * 20) / 20 for _ in range(3)]
        mesh = make_shape(shape, rng.uniform(0.6, 2.2), rng.uniform(0.05, 0.25),
                          factors, rng.randint(0, 9999))
    for _ in range(rng.randint(0, 3)):
        try:
            center, normal = random_spot(mesh, rng)
            mesh = cut(mesh, rng.choice(CUT_TYPES), center, normal,
                       float(mesh.extents.max()), rng.uniform(0.08, 0.2), rng)
        except Exception:
            pass  # a cut that fails is skipped; the model is still exported
    pattern = rng.choice(PATTERNS)
    a, b = random_palette(rng)
    tex = make_texture(pattern, a, b, rng.randint(2, 10), rng.randint(0, 9999))
    return finalize_for_export(mesh, tex, shape, anchor_point(mesh, *anchor_fracs))


def extend_faces(mesh, face_ids, amount):
    """Push (amount > 0) or pull (amount < 0) faces along their normals."""
    ids = np.array(sorted(face_ids), dtype=int)
    verts = mesh.vertices.copy()
    disp = np.zeros_like(verts)
    count = np.zeros(len(verts))
    vert_ids = mesh.faces[ids].ravel()
    np.add.at(disp, vert_ids, np.repeat(mesh.face_normals[ids], 3, axis=0))
    np.add.at(count, vert_ids, 1)
    moving = count > 0
    verts[moving] += disp[moving] / count[moving, None] * amount
    return ensure_outward(trimesh.Trimesh(vertices=verts, faces=mesh.faces, process=False))


# ---------------------------------------------------------------- cuts

def _frame(x_axis, z_axis):
    """4x4 transform that maps local x to x_axis and local z to z_axis."""
    m = np.eye(4)
    m[:3, 0] = x_axis
    m[:3, 1] = np.cross(z_axis, x_axis)
    m[:3, 2] = z_axis
    return m


def make_cutter(kind, center, normal, radius, depth, rng):
    if kind == "Round bite":
        cutter = trimesh.creation.icosphere(subdivisions=2, radius=radius)
        cutter.apply_translation(center + normal * radius * 0.3)
        return cutter

    v = np.array([rng.gauss(0, 1) for _ in range(3)])
    tangent = np.cross(normal, v)
    tangent /= np.linalg.norm(tangent)

    if kind == "Groove":
        cutter = trimesh.creation.cylinder(radius=radius, height=radius * 4, sections=3)
        cutter.apply_transform(_frame(normal, tangent))
    else:  # V-notch
        cutter = trimesh.creation.cylinder(radius=radius, height=2 * depth, sections=3)
        cutter.apply_transform(_frame(tangent, normal))
    cutter.apply_translation(center)
    return cutter


def cut(mesh, kind, center, normal, size, chip_frac, rng):
    """Apply one cut at a surface point. Raises if the cut removes everything."""
    radius = size * chip_frac * rng.uniform(0.75, 1.25)
    depth = radius * rng.uniform(0.6, 0.9)
    cutter = make_cutter(kind, center, normal, radius, depth, rng)
    result = trimesh.boolean.difference([mesh, cutter], engine="manifold")
    if result is None or len(result.faces) == 0:
        raise RuntimeError("That cut would remove the whole model. Try a smaller cut size.")
    return ensure_outward(result)


def random_spot(mesh, rng):
    i = rng.randrange(len(mesh.faces))
    return mesh.triangles_center[i], mesh.face_normals[i]


# ---------------------------------------------------------------- UVs and export

def unshare(mesh):
    """Copy with separate vertices per face. Face i of the copy is face i of the original."""
    return trimesh.Trimesh(
        vertices=mesh.vertices[mesh.faces].reshape(-1, 3),
        faces=np.arange(len(mesh.faces) * 3).reshape(-1, 3),
        process=False,
    )


def make_uv(flat, shape):
    """
    UVs for the unshared mesh (one UV per corner).
    Block: planar projection per face (each face picks its own axis).
    Round shapes: cylindrical around the upright Y axis, unwrapped per face so the seam stays clean.
    """
    v = flat.vertices
    if shape in ("Block", "Merged"):
        mn, mx = v.min(axis=0), v.max(axis=0)
        span = np.where(mx - mn > 0, mx - mn, 1.0)
        norm = (v - mn) / span
        axis = np.argmax(np.abs(flat.vertex_normals), axis=1)
        uv = np.zeros((len(v), 2))
        for a in range(3):
            m = axis == a
            uv[m] = norm[m][:, [i for i in range(3) if i != a]]
        return uv

    ymin, ymax = v[:, 1].min(), v[:, 1].max()
    uv = np.column_stack([
        np.arctan2(v[:, 2], v[:, 0]) / (2 * np.pi) % 1.0,
        (v[:, 1] - ymin) / max(ymax - ymin, 1e-9),
    ])
    tri = uv[flat.faces]                       # (F, 3, 2); flat.faces is arange, so this is per corner
    u = tri[:, :, 0]
    wrap = (u.max(axis=1) - u.min(axis=1)) > 0.5
    u[wrap] = np.where(u[wrap] < 0.5, u[wrap] + 1.0, u[wrap])
    return tri.reshape(-1, 2)


def make_material(image):
    """Swap baseColorTexture for your real texture later."""
    return trimesh.visual.material.PBRMaterial(baseColorTexture=image)


def finalize_for_export(mesh, tex, shape, anchor=None):
    """Export copy. UVs are computed first, then the anchor point is moved to the origin."""
    flat = unshare(mesh)
    uv = make_uv(flat, shape)
    anchor = np.zeros(3) if anchor is None else np.asarray(anchor, dtype=float)
    moved = trimesh.Trimesh(vertices=flat.vertices - anchor, faces=flat.faces, process=False)
    moved.visual = trimesh.visual.TextureVisuals(
        uv=uv, material=make_material(Image.fromarray(tex, "RGB")))
    return moved


# ---------------------------------------------------------------- colour and texture

def hex_to_rgb(text):
    text = text.strip().lstrip("#")
    if len(text) != 6:
        raise ValueError(f"Colour must look like #c08040, got '{text}'")
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def hsv_hex(h, s, v):
    r, g, b = colorsys.hsv_to_rgb(h % 1.0, s, v)
    return "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))


def hsv_array(h, s, v):
    """Vectorised HSV to RGB in 0..1, for drawing the colour wheel."""
    h6 = h * 6.0
    i = np.floor(h6).astype(int) % 6
    f = h6 - np.floor(h6)
    p = v * (1 - s)
    q = v * (1 - f * s)
    t = v * (1 - (1 - f) * s)
    one = np.ones_like(h)
    r = np.select([i == k for k in range(6)], [v * one, q, p * one, p * one, t, v * one])
    g = np.select([i == k for k in range(6)], [t, v * one, v * one, q, p * one, p * one])
    b = np.select([i == k for k in range(6)], [p * one, p * one, t, v * one, v * one, q])
    return np.stack([r, g, b], axis=-1)


def random_palette(r):
    """Two harmonious colours: lighter A, darker B (swapped sometimes)."""
    hue = r.random()
    scheme = r.choice(("analogous", "complementary", "monochrome"))
    hue_b = {"analogous": hue + r.uniform(0.03, 0.12),
             "complementary": hue + 0.5,
             "monochrome": hue}[scheme]
    sat = r.uniform(0.25, 0.75)
    a = hsv_hex(hue, sat, r.uniform(0.5, 0.9))
    b = hsv_hex(hue_b, min(1.0, sat + r.uniform(-0.1, 0.2)), r.uniform(0.12, 0.45))
    return (b, a) if r.random() < 0.3 else (a, b)


def make_texture(pattern, color_a, color_b, tiles, seed, px=TEX_PX):
    """
    Procedural texture as a (px, px, 3) uint8 array.
    Pixels blend colour A (mix 0) into colour B (mix 1), with a light
    multiplier for per-plank / per-brick / per-tile variation and fine grain.
    """
    rng = np.random.default_rng(seed)
    a = np.array(hex_to_rgb(color_a), dtype=float)
    b = np.array(hex_to_rgb(color_b), dtype=float)
    ys, xs = np.mgrid[0:px, 0:px]
    u = xs / px
    cell = max(4, px // tiles)

    def noise(cells):
        """Smooth random field in [0, 1]: small random grid, bicubic upscale."""
        small = rng.random((cells, cells))
        img = Image.fromarray((small * 255).astype(np.uint8)).resize((px, px), Image.BICUBIC)
        return np.asarray(img, dtype=float) / 255.0

    light = np.ones((px, px))
    if pattern == "Wood":
        warp = (noise(4) - 0.5) * 0.25
        rings = 0.5 + 0.5 * np.sin(2 * np.pi * tiles * 3 * (u + warp))
        mix = rings ** 6 * 0.9 + 0.1 * noise(96)
        light = 0.92 + 0.12 * noise(40)

    elif pattern == "Planks":
        band_h = cell
        length = 2 * band_h
        band = ys // band_h
        offset = (band % 2) * (length // 2)
        seam = (ys % band_h < 3) | (((xs + offset) % length) < 3)
        light = rng.uniform(0.85, 1.12, size=px // band_h + 2)[band]
        mix = np.where(seam, 1.0, 0.2 * noise(64))

    elif pattern == "Bricks":
        row_h = cell
        brick_w = 2 * row_h
        row = ys // row_h
        offset = (row % 2) * (brick_w // 2)
        col = (xs + offset) // brick_w
        mortar = (ys % row_h < 3) | (((xs + offset) % brick_w) < 3)
        light = rng.uniform(0.8, 1.15, size=(px // row_h + 2, px // brick_w + 3))[row, col]
        mix = np.where(mortar, 1.0, 0.15 * noise(48))

    elif pattern == "Tiles":
        grout = (xs % cell < 3) | (ys % cell < 3)
        light = rng.uniform(0.85, 1.1, size=(px // cell + 2, px // cell + 2))[ys // cell, xs // cell]
        mix = np.where(grout, 1.0, 0.12 * noise(48))

    elif pattern == "Stone":
        mix = 0.6 * noise(5) + 0.4 * noise(20)
        light = 0.85 + 0.3 * noise(40)

    elif pattern == "Marble":
        turb = 0.6 * noise(4) + 0.3 * noise(12) + 0.1 * noise(40)
        mix = (0.5 + 0.5 * np.sin(2 * np.pi * tiles * (u + 0.9 * turb))) ** 4
        light = 0.95 + 0.1 * noise(64)

    elif pattern == "Rust":
        blot = 0.6 * noise(10) + 0.4 * noise(4)
        mix = np.clip((blot - 0.45) * 3.0, 0, 1)
        light = 0.9 + 0.2 * noise(32)

    elif pattern == "Camo":
        mix = np.floor((0.7 * noise(4) + 0.3 * noise(12)) * 3) / 2

    elif pattern == "Concrete":
        mix = 0.5 * noise(48) + 0.5 * noise(160)
        light = 0.9 + 0.2 * noise(128)

    elif pattern == "Speckle":
        mix = (noise(80) > 0.82).astype(float)

    elif pattern == "Dots":
        dx = (xs % cell) - cell / 2
        dy = (ys % cell) - cell / 2
        mix = ((dx * dx + dy * dy) < (cell * 0.3) ** 2).astype(float)

    elif pattern == "Diagonal":
        mix = (((xs + ys) * tiles // px) % 2).astype(float)

    elif pattern == "Checker":
        mix = (((xs * tiles) // px + (ys * tiles) // px) % 2).astype(float)

    else:  # Stripes
        mix = (((xs * tiles) // px) % 2).astype(float)

    mix = np.clip(mix + 0.06 * (noise(64) - 0.5), 0.0, 1.0)[..., None]
    col = (a * (1 - mix) + b * mix) * light[..., None]
    return np.clip(col, 0, 255).astype(np.uint8)


def sample_face_colors(flat, uv, tex):
    """Colour of each face from the texture, used by the viewer."""
    px = tex.shape[0]
    cu = uv[flat.faces].mean(axis=1)
    col = np.floor(cu[:, 0] * px).astype(int) % px
    row = np.floor(cu[:, 1] * px).astype(int) % px
    return tex[row, col].astype(float)


# ---------------------------------------------------------------- viewer

class Viewer:
    """Software 3D viewer on a Tk canvas: orbit, pan, WASD, wheel zoom, face picking."""

    def __init__(self, parent, width=600, height=560):
        import tkinter as tk
        self.tk = tk
        self.w, self.h = width, height
        self.canvas = tk.Canvas(parent, width=width, height=height,
                                bg="#23262b", highlightthickness=0)
        self.yaw, self.pitch, self.dist = 0.6, 0.4, 4.0
        self.target = np.zeros(3)
        self.flat = None
        self.colors = None
        self.keys = set()
        self.drag = None
        self.press_xy = (0, 0)
        self.pick_mode = False
        self.selected = set()
        self.anchor = None
        self.show_grid = True
        self.show_anchor = True
        self._proj = None

        c = self.canvas
        c.bind("<ButtonPress-1>", lambda e: self._press(e, "orbit"))
        c.bind("<B1-Motion>", self._drag)
        c.bind("<ButtonRelease-1>", self._release)
        c.bind("<ButtonPress-3>", lambda e: self._press(e, "pan"))
        c.bind("<B3-Motion>", self._drag)
        c.bind("<MouseWheel>", lambda e: self._zoom(0.9 if e.delta > 0 else 1.1))
        c.bind("<Button-4>", lambda e: self._zoom(0.9))
        c.bind("<Button-5>", lambda e: self._zoom(1.1))

    def bind_keys(self, root):
        root.bind("<KeyPress>", self._key_down, add="+")
        root.bind("<KeyRelease>", self._key_up, add="+")
        self.canvas.after(16, self._tick)

    def set_model(self, base, tex, shape):
        self.flat = unshare(base)
        self.colors = sample_face_colors(self.flat, make_uv(self.flat, shape), tex)
        self.redraw()

    def set_loaded(self, mesh):
        """Show a model loaded from a .glb file, using its own texture and UVs."""
        self.flat = mesh
        vis = mesh.visual
        mat = getattr(vis, "material", None)
        tex = getattr(mat, "baseColorTexture", None) or getattr(mat, "image", None)
        uv = getattr(vis, "uv", None)
        if uv is not None and tex is not None:
            self.colors = sample_face_colors(mesh, np.asarray(uv), np.asarray(tex.convert("RGB")))
        else:
            self.colors = np.full((len(mesh.faces), 3), 170.0)
        self.anchor = np.zeros(3)  # exported models have their anchor at the origin
        self.selected = set()
        self.target = mesh.bounds.mean(axis=0)
        self.dist = float(np.clip(2.8 * mesh.extents.max(), 0.5, 30.0))
        self.redraw()

    def set_anchor(self, point):
        self.anchor = None if point is None else np.asarray(point, dtype=float)
        self.redraw()

    def _basis(self):
        eye = self.target + self.dist * np.array([
            math.cos(self.pitch) * math.sin(self.yaw),
            math.sin(self.pitch),
            math.cos(self.pitch) * math.cos(self.yaw)])
        fwd = self.target - eye
        fwd /= np.linalg.norm(fwd)
        right = np.cross(fwd, [0.0, 1.0, 0.0])
        right /= np.linalg.norm(right)
        up = np.cross(right, fwd)
        return eye, fwd, right, up

    def redraw(self):
        c = self.canvas
        c.delete("all")
        if self.flat is None:
            self._proj = None
            return
        eye, fwd, right, up = self._basis()
        rel = self.flat.vertices - eye
        x, y, z = rel @ right, rel @ up, rel @ fwd

        f = min(self.w, self.h)
        zs = np.where(z > 0.05, z, 1.0)
        sx = self.w / 2 + f * x / zs
        sy = self.h / 2 - f * y / zs

        faces = self.flat.faces
        visible = (z[faces] > 0.05).all(axis=1)
        depth = z[faces].mean(axis=1)
        shade = 0.35 + 0.65 * np.abs(self.flat.face_normals @ (-fwd))
        self._proj = (sx, sy, depth, visible, faces)

        if self.show_grid and self.anchor is not None:  # ground grid, behind the model
            ax0, ay0, az0 = self.anchor
            half = max(float(np.max(np.ptp(self.flat.vertices, axis=0))), 0.5) * 1.5
            ts = np.linspace(-half, half, 11)
            for t in ts:
                for p0, p1 in (([ax0 + t, ay0, az0 - half], [ax0 + t, ay0, az0 + half]),
                               ([ax0 - half, ay0, az0 + t], [ax0 + half, ay0, az0 + t])):
                    q = np.array([p0, p1]) - eye
                    gx, gy, gz = q @ right, q @ up, q @ fwd
                    if (gz <= 0.05).any():
                        continue
                    c.create_line(self.w / 2 + f * gx[0] / gz[0], self.h / 2 - f * gy[0] / gz[0],
                                  self.w / 2 + f * gx[1] / gz[1], self.h / 2 - f * gy[1] / gz[1],
                                  fill="#4a5058")

        for i in np.argsort(-depth):
            if not visible[i]:
                continue
            rgb = np.clip(self.colors[i] * shade[i], 0, 255).astype(int)
            color = "#%02x%02x%02x" % tuple(rgb)
            c.create_polygon(*self._pts(sx, sy, faces[i]), fill=color, outline=color)

        for i in self.selected:
            if i < len(faces) and visible[i]:
                c.create_polygon(*self._pts(sx, sy, faces[i]),
                                 fill="", outline="#ff4040", width=3)

        if self.anchor is not None and self.show_anchor:  # full X / Y / Z axis lines through the anchor
            L = 0.6 * float(np.max(np.ptp(self.flat.vertices, axis=0)))
            dirs = np.eye(3)
            ends = np.vstack([self.anchor + L * dirs, self.anchor - L * dirs, [self.anchor]])
            r = ends - eye
            ax_, ay_, az_ = r @ right, r @ up, r @ fwd
            zz = np.where(az_ > 0.05, az_, 1.0)
            qx = self.w / 2 + f * ax_ / zz
            qy = self.h / 2 - f * ay_ / zz
            # rows 0-2 = +X,+Y,+Z ; rows 3-5 = -X,-Y,-Z ; row 6 = anchor
            for k, (label, col) in enumerate(zip("XYZ", ("#3fc15a", "#f2d21b", "#b165d6"))):
                c.create_line(qx[k], qy[k], qx[k + 3], qy[k + 3], fill=col, width=3)
                c.create_text(qx[k], qy[k] - 12, text=label, fill=col, font=("Segoe UI", 10, "bold"))
                c.create_text(qx[k + 3], qy[k + 3] + 12, text=label, fill=col, font=("Segoe UI", 10, "bold"))
            c.create_oval(qx[6] - 8, qy[6] - 8, qx[6] + 8, qy[6] + 8, outline="#ff8c1a", width=3)
            c.create_text(qx[6] + 14, qy[6] - 14, text="anchor", fill="#ff8c1a",
                          anchor="w", font=("Segoe UI", 10, "bold"))

    @staticmethod
    def _pts(sx, sy, face):
        return np.stack([sx[face], sy[face]], axis=1).ravel().tolist()

    def _pick(self, px, py):
        if self._proj is None:
            return None
        sx, sy, depth, visible, faces = self._proj
        a, b, c = faces[:, 0], faces[:, 1], faces[:, 2]
        ax, ay, bx, by, cx, cy = sx[a], sy[a], sx[b], sy[b], sx[c], sy[c]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        with np.errstate(divide="ignore", invalid="ignore"):
            w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
            w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
        w2 = 1 - w0 - w1
        inside = visible & (np.abs(den) > 1e-9) & (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        idx = np.nonzero(inside)[0]
        if idx.size == 0:
            return None
        return int(idx[np.argmin(depth[idx])])

    def _press(self, e, mode):
        self.drag = (mode, e.x, e.y)
        self.press_xy = (e.x, e.y)

    def _drag(self, e):
        if not self.drag:
            return
        mode, x0, y0 = self.drag
        dx, dy = e.x - x0, e.y - y0
        self.drag = (mode, e.x, e.y)
        if mode == "orbit":
            self.yaw -= dx * 0.01
            self.pitch = float(np.clip(self.pitch + dy * 0.01, -1.5, 1.5))
        else:
            _, _, right, up = self._basis()
            self.target += (-dx * right + dy * up) * self.dist * 0.0015
        self.redraw()

    def _release(self, e):
        if not self.drag:
            return
        mode = self.drag[0]
        self.drag = None
        sx0, sy0 = self.press_xy
        is_click = abs(e.x - sx0) < 4 and abs(e.y - sy0) < 4
        if mode == "orbit" and self.pick_mode and is_click:
            hit = self._pick(e.x, e.y)
            if hit is not None:
                self.selected ^= {hit}
                self.redraw()

    def _zoom(self, factor):
        self.dist = float(np.clip(self.dist * factor, 0.5, 30.0))
        self.redraw()

    def _key_down(self, e):
        if isinstance(e.widget, self.tk.Entry):
            return
        k = e.keysym.lower()
        if len(k) == 1 and k in "wasd":
            self.keys.add(k)

    def _key_up(self, e):
        self.keys.discard(e.keysym.lower())

    def _tick(self):
        if self.keys and self.flat is not None:
            _, fwd, _, _ = self._basis()
            fh = np.array([fwd[0], 0.0, fwd[2]])
            fh /= np.linalg.norm(fh)
            rh = np.cross(fh, [0.0, 1.0, 0.0])
            move = np.zeros(3)
            if "w" in self.keys: move += fh
            if "s" in self.keys: move -= fh
            if "d" in self.keys: move += rh
            if "a" in self.keys: move -= rh
            n = np.linalg.norm(move)
            if n > 0:
                self.target += move / n * self.dist * 0.02
                self.redraw()
        self.canvas.after(16, self._tick)


# ---------------------------------------------------------------- colour wheel

class ColorWheel:
    """Hue and saturation wheel with a brightness slider. Calls on_change(hex) live."""

    SIZE = 220

    def __init__(self, root, initial, on_change, title):
        import tkinter as tk
        from tkinter import ttk
        self.tk = tk
        self.on_change = on_change
        rgb = [c / 255 for c in hex_to_rgb(initial)]
        self.h, self.s, self.v = colorsys.rgb_to_hsv(*rgb)

        self.top = tk.Toplevel(root)
        self.top.title(title)
        self.top.resizable(False, False)
        S = self.SIZE
        self.canvas = tk.Canvas(self.top, width=S, height=S, highlightthickness=0)
        self.canvas.grid(row=0, column=0, columnspan=2, padx=12, pady=(12, 6))
        self.canvas.bind("<ButtonPress-1>", self._pick)
        self.canvas.bind("<B1-Motion>", self._pick)

        self.swatch = tk.Label(self.top, width=10, height=2, relief="solid")
        self.swatch.grid(row=1, column=0, padx=12, sticky="w")
        self.hex_var = tk.StringVar()
        entry = ttk.Entry(self.top, textvariable=self.hex_var, width=10)
        entry.grid(row=1, column=1, padx=(0, 12), sticky="w")
        entry.bind("<Return>", self._from_entry)

        self.val = tk.Scale(self.top, from_=0.0, to=1.0, resolution=0.01, orient="horizontal",
                            label="Brightness", length=S, command=self._on_v)
        self.val.set(self.v)
        self.val.grid(row=2, column=0, columnspan=2, padx=12)
        ttk.Button(self.top, text="Done", command=self.top.destroy).grid(
            row=3, column=0, columnspan=2, pady=10)

        self._redraw_wheel()
        self._update()

    def _redraw_wheel(self):
        from PIL import ImageTk
        S = self.SIZE
        R = S / 2 - 2
        ys, xs = np.mgrid[0:S, 0:S]
        dx = (xs + 0.5 - S / 2) / R
        dy = (S / 2 - (ys + 0.5)) / R
        r = np.hypot(dx, dy)
        hue = (np.arctan2(dy, dx) / (2 * np.pi)) % 1.0
        rgb = hsv_array(hue, np.clip(r, 0, 1), self.v)
        arr = (rgb * 255).astype(np.uint8)
        arr[r > 1] = (220, 218, 213)
        self._img = ImageTk.PhotoImage(Image.fromarray(arr, "RGB"))
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self._img)
        self._draw_marker()

    def _draw_marker(self):
        self.canvas.delete("marker")
        S, R = self.SIZE, self.SIZE / 2 - 2
        ang = 2 * math.pi * self.h
        x = S / 2 + self.s * R * math.cos(ang)
        y = S / 2 - self.s * R * math.sin(ang)
        self.canvas.create_oval(x - 7, y - 7, x + 7, y + 7, outline="black", width=2, tags="marker")
        self.canvas.create_oval(x - 4, y - 4, x + 4, y + 4, outline="white", width=2, tags="marker")

    def _pick(self, e):
        S, R = self.SIZE, self.SIZE / 2 - 2
        dx = (e.x - S / 2) / R
        dy = (S / 2 - e.y) / R
        self.s = min(1.0, math.hypot(dx, dy))
        self.h = (math.atan2(dy, dx) / (2 * math.pi)) % 1.0
        self._update()

    def _on_v(self, value):
        self.v = float(value)
        self._redraw_wheel()
        self._update()

    def _from_entry(self, _event=None):
        try:
            rgb = [c / 255 for c in hex_to_rgb(self.hex_var.get())]
        except ValueError:
            return
        self.h, self.s, self.v = colorsys.rgb_to_hsv(*rgb)
        self.val.set(self.v)
        self._redraw_wheel()
        self._update()

    def _update(self):
        hx = hsv_hex(self.h, self.s, self.v)
        self.swatch.config(bg=hx)
        self.hex_var.set(hx)
        self._draw_marker()
        self.on_change(hx)


# ---------------------------------------------------------------- GUI

def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title("Block Model Workbench")
    root.minsize(980, 640)
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass

    shape_v = tk.StringVar(value=SHAPES[0])
    size_v = tk.DoubleVar(value=1.0)
    bevel_v = tk.DoubleVar(value=0.12)
    sx_v = tk.DoubleVar(value=1.0)
    sy_v = tk.DoubleVar(value=1.0)
    sz_v = tk.DoubleVar(value=1.0)
    extend_v = tk.DoubleVar(value=0.1)
    seed_v = tk.IntVar(value=1)
    kind_v = tk.StringVar(value=CUT_TYPES[0])
    chip_v = tk.DoubleVar(value=0.15)
    count_v = tk.IntVar(value=3)
    mark_v = tk.BooleanVar(value=False)
    pattern_v = tk.StringVar(value="Wood")
    tiles_v = tk.IntVar(value=4)
    tex_seed_v = tk.IntVar(value=7)
    color_a_v = tk.StringVar(value="#8a5a34")
    color_b_v = tk.StringVar(value="#3e2715")
    rand_new_v = tk.BooleanVar(value=True)
    status_v = tk.StringVar(value="Ready.")
    anchor_preset_v = tk.StringVar(value="Bottom centre")
    grid_v = tk.BooleanVar(value=True)
    anchor_v = tk.BooleanVar(value=True)
    ax_v = tk.DoubleVar(value=0.0)
    ay_v = tk.DoubleVar(value=-1.0)
    az_v = tk.DoubleVar(value=0.0)

    state = {"base": None, "shape": SHAPES[0], "rng": random.Random(1),
             "history": [], "wheel": None, "browsing": False,
             "glb_files": [], "glb_index": 0}

    # ---- layout: scrollable sidebar | viewer
    bg = style.lookup("TFrame", "background") or "#dcdad5"
    outer = ttk.Frame(root)
    outer.grid(row=0, column=0, sticky="ns")
    side = tk.Canvas(outer, width=350, highlightthickness=0, bg=bg)
    bar = ttk.Scrollbar(outer, orient="vertical", command=side.yview)
    side.configure(yscrollcommand=bar.set)
    bar.pack(side="right", fill="y")
    side.pack(side="left", fill="both", expand=True)
    left = ttk.Frame(side, padding=12)
    win = side.create_window((0, 0), window=left, anchor="nw")
    left.bind("<Configure>", lambda e: side.configure(scrollregion=side.bbox("all")))
    side.bind("<Configure>", lambda e: side.itemconfigure(win, width=e.width))

    def sidebar_wheel(e):
        w = e.widget
        if isinstance(w, str):
            try:
                w = root.nametowidget(w)
            except KeyError:
                return
        while w is not None:  # only scroll when the pointer is over the sidebar
            if w is side:
                num = getattr(e, "num", None)
                if num == 4:
                    step = -1
                elif num == 5:
                    step = 1
                else:
                    step = -1 if e.delta > 0 else 1
                side.yview_scroll(step, "units")
                return
            w = w.master

    root.bind_all("<MouseWheel>", sidebar_wheel, add="+")
    root.bind_all("<Button-4>", sidebar_wheel, add="+")
    root.bind_all("<Button-5>", sidebar_wheel, add="+")

    viewer = Viewer(root)
    viewer.canvas.grid(row=0, column=1, sticky="nsew", padx=(0, 12), pady=12)
    viewer.bind_keys(root)

    def sync_view(*a):
        viewer.show_grid = grid_v.get()
        viewer.show_anchor = anchor_v.get()
        viewer.redraw()

    grid_v.trace_add("write", sync_view)
    anchor_v.trace_add("write", sync_view)
    ttk.Label(root, foreground="#555",
              text="Left-drag orbit  ·  Right-drag pan  ·  W A S D move  ·  Mouse wheel zoom"
              ).grid(row=1, column=1, pady=(0, 6))
    ttk.Label(root, textvariable=status_v, relief="sunken", anchor="w", padding=(8, 3)
              ).grid(row=2, column=0, columnspan=2, sticky="ew")
    root.columnconfigure(1, weight=1)
    root.rowconfigure(0, weight=1)

    # ---- widget helpers
    def section(title):
        f = ttk.LabelFrame(left, text=title, padding=10)
        f.pack(fill="x", pady=(0, 8))
        return f

    def row_frame(parent, label):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=2)
        ttk.Label(row, text=label, width=20).pack(side="left")
        return row

    def slider(parent, label, var, lo, hi, res, fmt):
        row = row_frame(parent, label)
        readout = ttk.Label(row, width=6, anchor="e")
        readout.pack(side="right")
        tk.Scale(row, variable=var, from_=lo, to=hi, resolution=res, orient="horizontal",
                 showvalue=False, length=140, highlightthickness=0, bd=0).pack(side="left", padx=4)
        var.trace_add("write", lambda *a: readout.config(text=fmt.format(var.get())))
        readout.config(text=fmt.format(var.get()))

    def button(parent, text, cmd):
        ttk.Button(parent, text=text, command=cmd).pack(fill="x", pady=2)

    # ---- 1. shape
    sec = section("View")
    ttk.Checkbutton(sec, text="Show ground grid", variable=grid_v).pack(anchor="w")
    ttk.Checkbutton(sec, text="Show anchor", variable=anchor_v).pack(anchor="w")

    sec = section("1. Shape")
    row = row_frame(sec, "Shape")
    ttk.Combobox(row, textvariable=shape_v, values=SHAPES, state="readonly", width=18).pack(side="left")
    slider(sec, "Size", size_v, 0.5, 3.0, 0.1, "{:.1f}")
    slider(sec, "Bevel (block only)", bevel_v, 0.02, 0.30, 0.01, "{:.2f}")
    slider(sec, "Random seed", seed_v, 0, 9999, 1, "{}")
    button(sec, "Create new shape", lambda: new_shape())
    button(sec, "Create random shape", lambda: random_shape())
    button(sec, "Merge 2-5 random shapes", lambda: merge_random_shapes())

    sec = section("Anchor point (pivot)")
    row = row_frame(sec, "Preset")
    combo_a = ttk.Combobox(row, textvariable=anchor_preset_v, values=list(ANCHOR_PRESETS),
                           state="readonly", width=18)
    combo_a.pack(side="left")
    combo_a.bind("<<ComboboxSelected>>", lambda e: apply_preset())
    slider(sec, "Anchor X", ax_v, -1.0, 1.0, 0.05, "{:+.2f}")
    slider(sec, "Anchor Y", ay_v, -1.0, 1.0, 0.05, "{:+.2f}")
    slider(sec, "Anchor Z", az_v, -1.0, 1.0, 0.05, "{:+.2f}")
    ttk.Label(sec, foreground="#555", wraplength=290, justify="left",
              text="The red, green and blue axes mark the anchor. -1 is the min edge, "
                   "0 the middle, +1 the max edge. The anchor sits on the shape's own extreme "
                   "vertices. Export places this point at the origin."
              ).pack(fill="x", pady=(4, 0))

    # ---- 2. stretch and extend
    sec = section("2. Stretch and extend")
    slider(sec, "Stretch X", sx_v, 0.3, 2.5, 0.05, "{:.2f}")
    slider(sec, "Stretch Y", sy_v, 0.3, 2.5, 0.05, "{:.2f}")
    slider(sec, "Stretch Z", sz_v, 0.3, 2.5, 0.05, "{:.2f}")
    button(sec, "Apply stretch to current model", lambda: stretch_current())
    slider(sec, "Extend amount", extend_v, -0.4, 0.8, 0.01, "{:+.2f}")
    button(sec, "Extend marked faces", lambda: extend_marked())

    # ---- 3. cuts
    sec = section("3. Wear cuts")
    row = row_frame(sec, "Cut type")
    combo = ttk.Combobox(row, textvariable=kind_v, values=CUT_TYPES, state="readonly", width=14)
    combo.pack(side="left")
    descriptions = {
        "V-notch": "A small triangular chip bitten into a face.",
        "Round bite": "A rounded dent, good for corners and edges.",
        "Groove": "A long shallow scratch running across a face.",
    }
    desc = ttk.Label(sec, foreground="#555", wraplength=290, justify="left",
                     text=descriptions[kind_v.get()])
    desc.pack(fill="x", pady=(2, 4))
    combo.bind("<<ComboboxSelected>>", lambda e: desc.config(text=descriptions[kind_v.get()]))
    slider(sec, "Cut size", chip_v, 0.05, 0.40, 0.01, "{:.2f}")
    slider(sec, "Random cuts per press", count_v, 1, 12, 1, "{}")
    ttk.Checkbutton(sec, text="Mark spots: click faces in the viewer", variable=mark_v,
                    command=lambda: setattr(viewer, "pick_mode", mark_v.get())
                    ).pack(anchor="w", pady=(4, 4))
    btns = ttk.Frame(sec)
    btns.pack(fill="x")
    btns.columnconfigure((0, 1), weight=1)
    ttk.Button(btns, text="Cut random spots", command=lambda: cut_random()
               ).grid(row=0, column=0, sticky="ew", padx=2, pady=2)
    ttk.Button(btns, text="Cut marked spots", command=lambda: cut_marked()
               ).grid(row=0, column=1, sticky="ew", padx=2, pady=2)
    ttk.Button(btns, text="Clear marks", command=lambda: clear_marks()
               ).grid(row=1, column=0, sticky="ew", padx=2, pady=2)
    ttk.Button(btns, text="Undo", command=lambda: undo()
               ).grid(row=1, column=1, sticky="ew", padx=2, pady=2)

    # ---- 4. texture
    sec = section("4. Texture")
    row = row_frame(sec, "Pattern")
    ttk.Combobox(row, textvariable=pattern_v, values=PATTERNS, state="readonly", width=14).pack(side="left")
    slider(sec, "Repeats", tiles_v, 1, 16, 1, "{}")
    slider(sec, "Texture seed", tex_seed_v, 0, 9999, 1, "{}")

    def colour_row(label, var):
        row = row_frame(sec, label)
        ttk.Entry(row, textvariable=var, width=10).pack(side="left")
        swatch = tk.Button(row, width=3, relief="solid", bd=1, cursor="hand2",
                           command=lambda: open_wheel(var, label))
        swatch.pack(side="left", padx=6)

        def paint(*a):
            try:
                swatch.config(bg=var.get())
            except tk.TclError:
                pass
        var.trace_add("write", paint)
        paint()

    colour_row("Colour A", color_a_v)
    colour_row("Colour B", color_b_v)
    ttk.Label(sec, foreground="#555", text="Click a swatch to open the colour wheel.",
              wraplength=290).pack(fill="x", pady=(2, 4))
    ttk.Checkbutton(sec, text="Randomize texture on new shape", variable=rand_new_v
                    ).pack(anchor="w", pady=(2, 4))
    button(sec, "Randomize texture (pattern, colours, seed)", lambda: randomize_texture())
    button(sec, "Randomize colours only", lambda: randomize_colours())

    # ---- 5. export
    sec = section("5. Export")
    button(sec, "Export .glb", lambda: export())
    button(sec, "Batch export 99 random models", lambda: batch_export())

    # ---- 6. browse a folder of .glb files one at a time
    sec = section("6. Browse .glb folder")
    button(sec, "Open folder of .glb files...", lambda: open_glb_folder())
    nav = ttk.Frame(sec)
    nav.pack(fill="x", pady=2)
    nav.columnconfigure((0, 1), weight=1)
    ttk.Button(nav, text="< Previous", command=lambda: step_glb(-1)).grid(row=0, column=0, sticky="ew", padx=2)
    ttk.Button(nav, text="Next >", command=lambda: step_glb(1)).grid(row=0, column=1, sticky="ew", padx=2)
    glb_label_v = tk.StringVar(value="No folder open. Left and right arrow keys also step through.")
    ttk.Label(sec, textvariable=glb_label_v, wraplength=290, justify="left").pack(fill="x", pady=(4, 0))

    def arrow(delta):
        def handler(e):
            if isinstance(e.widget, (tk.Entry, ttk.Entry)):  # don't steal typing
                return
            step_glb(delta)
        return handler

    root.bind("<Left>", arrow(-1), add="+")
    root.bind("<Right>", arrow(1), add="+")

    # ---- logic
    def anchor_fracs():
        return (ax_v.get(), ay_v.get(), az_v.get())

    def refresh_anchor():
        fr = anchor_fracs()
        name = next((k for k, v in ANCHOR_PRESETS.items() if np.allclose(v, fr)), "Custom")
        anchor_preset_v.set(name)  # manual slider edits show as "Custom"
        if state["base"] is not None and not state["browsing"]:
            viewer.set_anchor(anchor_point(state["base"], *fr))

    def apply_preset():
        ax, ay, az = ANCHOR_PRESETS[anchor_preset_v.get()]
        ax_v.set(ax)
        ay_v.set(ay)
        az_v.set(az)

    for v in (ax_v, ay_v, az_v):
        v.trace_add("write", lambda *a: refresh_anchor())

    def stretch_factors():
        return (sx_v.get(), sy_v.get(), sz_v.get())

    def current_texture():
        return make_texture(pattern_v.get(), color_a_v.get(), color_b_v.get(),
                            tiles_v.get(), tex_seed_v.get())

    def refresh_view():
        if state["base"] is None or state["browsing"]:
            return
        try:
            tex = current_texture()
        except ValueError as exc:  # half-typed colour: keep the last good view
            status_v.set(str(exc))
            return
        viewer.set_model(state["base"], tex, state["shape"])
        viewer.set_anchor(anchor_point(state["base"], *anchor_fracs()))

    for v in (color_a_v, color_b_v, tiles_v, pattern_v, tex_seed_v):
        v.trace_add("write", lambda *a: refresh_view())

    def set_base(mesh, msg):
        state["base"] = mesh
        state["browsing"] = False
        viewer.selected.clear()
        status_v.set(msg)
        refresh_view()

    def push_history():
        if state["base"] is not None:
            state["history"].append(state["base"])
            del state["history"][:-40]

    def open_wheel(var, title):
        old = state["wheel"]
        if old is not None:
            try:
                old.top.destroy()
            except tk.TclError:
                pass
        try:
            initial = var.get()
            hex_to_rgb(initial)
        except ValueError:
            initial = "#808080"
        state["wheel"] = ColorWheel(root, initial, lambda hx: var.set(hx), f"Pick {title}")

    def randomize_colours():
        color_a_v.set(random_palette(random.Random())[0])
        color_b_v.set(random_palette(random.Random())[1])
        status_v.set("Colours randomized.")

    def randomize_texture():
        r = random.Random()
        pattern_v.set(r.choice(PATTERNS))
        a, b = random_palette(r)
        color_a_v.set(a)
        color_b_v.set(b)
        tiles_v.set(r.randint(2, 10))
        tex_seed_v.set(r.randint(0, 9999))
        status_v.set(f"Texture randomized: {pattern_v.get()}, seed {tex_seed_v.get()}.")

    def new_shape():
        state["rng"] = random.Random(seed_v.get())
        state["history"].clear()
        state["shape"] = shape_v.get()
        if rand_new_v.get():
            randomize_texture()
        mesh = make_shape(state["shape"], size_v.get(), bevel_v.get(), stretch_factors(),
                          seed_v.get())
        set_base(mesh, f"New {state['shape'].lower()} created.")

    def random_shape():
        r = random.Random()
        shape_v.set(r.choice(SHAPES))
        size_v.set(round(r.uniform(0.6, 2.2), 1))
        bevel_v.set(round(r.uniform(0.05, 0.25), 2))
        sx_v.set(round(r.uniform(0.6, 1.6) * 20) / 20)
        sy_v.set(round(r.uniform(0.6, 1.6) * 20) / 20)
        sz_v.set(round(r.uniform(0.6, 1.6) * 20) / 20)
        seed_v.set(r.randint(0, 9999))
        new_shape()  # anchor is kept: bottom centre by default, or whatever was set by hand
        status_v.set(f"Random {shape_v.get().lower()} created (seed {seed_v.get()}).")

    def merge_random_shapes():
        try:
            mesh, names = random_merge(random.Random())
        except Exception as exc:
            messagebox.showerror("Merge failed", str(exc))
            return
        state["shape"] = "Merged"
        state["history"].clear()
        if rand_new_v.get():
            randomize_texture()
        set_base(mesh, "Merged: " + " + ".join(names))

    def ensure_shape():
        if state["base"] is None:
            new_shape()

    def stretch_current():
        ensure_shape()
        push_history()
        set_base(scale_mesh(state["base"], stretch_factors()), "Stretch applied to current model.")

    def extend_marked():
        if not viewer.selected:
            status_v.set("No faces marked. Turn on 'Mark spots' and click faces in the viewer.")
            return
        ensure_shape()
        push_history()
        n = len(viewer.selected)
        mesh = extend_faces(state["base"], viewer.selected, extend_v.get())
        set_base(mesh, f"Extended {n} face(s) by {extend_v.get():+.2f}.")

    def run_cuts(spot_fn, label):
        ensure_shape()
        mesh = state["base"]
        spots = spot_fn(mesh)
        if not spots:
            status_v.set("Nothing to cut. Mark spots in the viewer first.")
            return
        push_history()
        try:
            for center, normal in spots:
                mesh = cut(mesh, kind_v.get(), center, normal,
                           size_v.get(), chip_v.get(), state["rng"])
        except Exception as exc:
            state["history"].pop()
            messagebox.showerror("Cut failed", str(exc))
            return
        set_base(mesh, f"{len(spots)} {kind_v.get()} cut(s) {label}. Faces: {len(mesh.faces)}")

    def cut_random():
        n = count_v.get()
        run_cuts(lambda m: [random_spot(m, state["rng"]) for _ in range(n)], "at random spots")

    def cut_marked():
        def marked(m):
            return [(m.triangles_center[i], m.face_normals[i])
                    for i in sorted(viewer.selected) if i < len(m.faces)]
        run_cuts(marked, "at marked spots")

    def clear_marks():
        viewer.selected.clear()
        status_v.set("Marks cleared.")
        viewer.redraw()

    def undo():
        if not state["history"]:
            status_v.set("Nothing to undo.")
            return
        state["base"] = state["history"].pop()
        state["browsing"] = False
        viewer.selected.clear()
        status_v.set("Undid last change.")
        refresh_view()

    def show_glb(index):
        files = state["glb_files"]
        path = files[index]
        try:
            mesh = trimesh.load(path, force="mesh")
        except Exception as exc:
            messagebox.showerror("Could not load", f"{os.path.basename(path)}\n{exc}")
            return
        state["glb_index"] = index
        state["browsing"] = True
        viewer.set_loaded(mesh)
        glb_label_v.set(f"{index + 1} / {len(files)}  ·  {os.path.basename(path)}")
        status_v.set(f"Viewing {os.path.basename(path)}")

    def open_glb_folder():
        folder = filedialog.askdirectory(title="Choose a folder of .glb files")
        if not folder:
            return
        files = sorted(os.path.join(folder, f) for f in os.listdir(folder)
                       if f.lower().endswith(".glb"))
        if not files:
            messagebox.showinfo("No models", "That folder has no .glb files.")
            return
        state["glb_files"] = files
        show_glb(0)

    def step_glb(delta):
        files = state["glb_files"]
        if not files:
            status_v.set("Open a folder of .glb files first.")
            return
        show_glb((state["glb_index"] + delta) % len(files))  # wraps at either end

    def batch_export():
        folder = filedialog.askdirectory(title="Choose a folder for the 99 models")
        if not folder:
            return
        total = 99
        fr = anchor_fracs()
        base_seed = random.randint(0, 10 ** 9)
        cancel = threading.Event()
        events = queue.Queue()

        dlg = tk.Toplevel(root)
        dlg.title("Batch export")
        dlg.resizable(False, False)
        dlg.transient(root)
        msg_v = tk.StringVar(value="Starting...")
        ttk.Label(dlg, textvariable=msg_v).pack(padx=16, pady=(14, 6), anchor="w")
        bar = ttk.Progressbar(dlg, maximum=total, length=380)
        bar.pack(padx=16, pady=6)

        def stop():
            cancel.set()
            msg_v.set("Cancelling after the current model...")

        ttk.Button(dlg, text="Cancel", command=stop).pack(pady=(4, 14))
        dlg.protocol("WM_DELETE_WINDOW", stop)

        # Worker thread: no Tk calls here. Models are never shown in the viewer.
        def worker():
            failed, done = [], 0
            for i in range(1, total + 1):
                if cancel.is_set():
                    break
                try:
                    model = generate_model(random.Random(base_seed + i), fr)
                    model.export(os.path.join(folder, f"model_{i}.glb"))
                    done += 1
                except Exception as exc:
                    failed.append(f"model_{i}.glb: {exc}")
                events.put(("progress", i))
            events.put(("done", (done, failed)))

        threading.Thread(target=worker, daemon=True).start()

        def poll():
            while True:
                try:
                    kind, value = events.get_nowait()
                except queue.Empty:
                    break
                if kind == "progress":
                    bar["value"] = value
                    msg_v.set(f"Exporting model {value} of {total}...")
                else:
                    dlg.destroy()
                    done, failed = value
                    text = f"Exported {done} of {total} models to:\n{folder}"
                    if failed:
                        text += "\n\nFailed:\n" + "\n".join(failed[:10])
                    if cancel.is_set():
                        text = "Cancelled.\n\n" + text
                    messagebox.showinfo("Batch export", text)
                    return
            root.after(100, poll)

        root.after(100, poll)

    def export():
        ensure_shape()
        path = filedialog.asksaveasfilename(defaultextension=".glb",
                                            filetypes=[("GLB model", "*.glb")])
        if not path:
            return
        try:
            anchor = anchor_point(state["base"], *anchor_fracs())
            finalize_for_export(state["base"], current_texture(), state["shape"], anchor).export(path)
            status_v.set(f"Saved {path}")
        except Exception as exc:
            messagebox.showerror("Export failed", str(exc))

    randomize_texture()  # colours and pattern are random from the start
    new_shape()
    root.mainloop()


# ---------------------------------------------------------------- entry

if __name__ == "__main__":
    if "--test" in sys.argv:
        # Headless check: python cube_glb_gui.py --test /path/out.glb
        out_path = sys.argv[sys.argv.index("--test") + 1]
        rng = random.Random(1)
        for shape in SHAPES:
            block = make_shape(shape, 1.0, 0.12, (1.0, 1.2, 0.8))
            for kind in CUT_TYPES:
                center, normal = random_spot(block, rng)
                block = cut(block, kind, center, normal, 1.0, 0.15, rng)
            outward = (np.einsum("ij,ij->i", block.face_normals,
                                 block.triangles_center - block.centroid) > 0).mean()
            print(f"{shape}: faces={len(block.faces)} watertight={block.is_watertight} "
                  f"volume={block.volume:.3f} (must be > 0) outward~{outward:.0%} "
                  f"extents(x,y,z)={np.round(block.extents, 2).tolist()}")
        tex = make_texture("Wood", "#8a5a34", "#3e2715", 4, 7)
        finalize_for_export(block, tex, "Cone").export(out_path)
        print(f"ok: {out_path}")
    else:
        run_gui()
