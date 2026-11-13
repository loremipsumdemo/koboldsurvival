"""Makes simple low-poly prop models: the data/model/<name>.glb files that data/props.txt names for each prop.

Run it with:  python tools/make_models.py     (needs Python 3 and Pillow: pip install pillow)
Then build the game, which packs data/ into data.pak. The game only reads the finished .glb files and never runs this script.

Each prop is one function that returns a list of parts, made with box(), cyl(), cone(), ball(), pyramid() and rod().
A part's x, y, z is the middle of its bottom, and y = 0 is the ground. To change a prop, edit its function and run this.

Things the game does with a model:
  - Props are drawn unlit, so each corner gets a lighter or darker shade of its color from a small texture, by which way its
    face looks and how near the ground it is.
  - The largest side of the model is scaled to the prop's size in src/props.h. Models are built at that size, in game units,
    where a unit is about 1.2 tall.
  - A sleeper lies at half a bed's height, so the top of a bed or bedroll should be about twice the height of its mattress.
  - A click has to land on one of the model's triangles, so the gaps in an open prop like the cage can't be clicked.

Running this overwrites the model of every prop, including any replaced by hand.

Z-FIGHTING: when you make or edit a model, try to remove z-fighting in the first pass. Two surfaces in the same plane flicker
against each other, so never leave overlapping parts with a face in common: a face meeting another part's face (a post flush with
the edge of the box it stands on, a lintel ending exactly on a tower's side, a stacked box starting exactly where the one below
ends, a part resting exactly on the deck) must stand 0.01 - 0.02 clear of it, either buried inside the other part or sticking out
past it. Check every pair of parts whose sides, tops or bottoms land on the same number.
"""
import io
import json
import math
import os
import struct
import sys
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(REPO, 'data')
SHADES = 8
FADE_HEIGHT = 0.3   # how far above the ground the darkening reaches
FADE_AMOUNT = 0.35

WOOD = (150, 105, 65)
DARK_WOOD = (98, 64, 40)
PALE_WOOD = (196, 158, 108)
BARK = (92, 68, 48)
STONE = (150, 148, 140)
DARK_STONE = (92, 90, 92)
IRON = (84, 88, 100)
STEEL = (176, 184, 196)
BRASS = (206, 162, 70)
BONE = (230, 220, 190)
LEAF = (66, 132, 62)
DARK_LEAF = (44, 100, 56)
SOIL = (92, 66, 44)
STRAW = (222, 190, 112)
WATER = (70, 140, 190)
CANVAS = (214, 202, 170)
LINEN = (232, 226, 208)
RED = (172, 58, 52)
BLUE = (70, 95, 150)
GREEN = (95, 120, 75)
BURLAP = (178, 148, 100)
MEAT = (176, 66, 66)
FLAME = (255, 140, 40)
FLAME_CORE = (255, 216, 96)
GLOW = (255, 170, 60)

# Each shape is an outline of (radius, height) points spun around the up axis, with a number of sides and a starting angle.
SQUARE = math.sqrt(0.5)   # the radius that makes a four-sided shape exactly 1 wide
SHAPES = {
    'box': ([(0, 0), (SQUARE, 0), (SQUARE, 1), (0, 1)], 4, 45),
    'pyramid': ([(0, 0), (SQUARE, 0), (0, 1)], 4, 45),
    'cylinder': ([(0, 0), (0.5, 0), (0.5, 1), (0, 1)], 8, 0),
    'cone': ([(0, 0), (0.5, 0), (0, 1)], 8, 0),
    'ball': ([(0, 0), (0.35, 0.15), (0.5, 0.5), (0.35, 0.85), (0, 1)], 8, 0),
}


def part(shape, x, y, z, w, h, d, color, yaw=0.0, tilt=0.0):
    return (shape, (x, y, z), (w, h, d), color, yaw, tilt)


def box(x, y, z, w, h, d, color, yaw=0.0, tilt=0.0):
    return part('box', x, y, z, w, h, d, color, yaw, tilt)


def cyl(x, y, z, w, h, d, color, yaw=0.0, tilt=0.0):
    return part('cylinder', x, y, z, w, h, d, color, yaw, tilt)


def cone(x, y, z, w, h, d, color, yaw=0.0, tilt=0.0):
    return part('cone', x, y, z, w, h, d, color, yaw, tilt)


def ball(x, y, z, w, h, d, color):
    return part('ball', x, y, z, w, h, d, color)


def pyramid(x, y, z, w, h, d, color, yaw=0.0, tilt=0.0):
    return part('pyramid', x, y, z, w, h, d, color, yaw, tilt)


def rod(a, b, width, color, shape='box'):
    """A shape running from point a to point b, for beams, logs and anything else that leans."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    tilt = math.degrees(math.acos(dy / length))
    yaw = math.degrees(math.atan2(dz, -dx))
    return part(shape, a[0], a[1], a[2], width, length, width, color, yaw, tilt)


def legs(x, z, height, width, color):
    return [box(sx * x, 0, sz * z, width, height, width, color) for sx in (-1, 1) for sz in (-1, 1)]


def wheel(x, z, radius):
    """A wheel with its axle along z, starting at z."""
    return [cyl(x, radius, z, radius * 2, 0.05, radius * 2, DARK_WOOD, 90, 90),
            cyl(x, radius, z + 0.01, radius * 0.6, 0.05, radius * 0.6, IRON, 90, 90)]


def barrel_parts(x, y, z, width, height, color=WOOD):
    return [cyl(x, y, z, width, height, width, color),
            cyl(x, y + height * 0.15, z, width * 1.04, height * 0.07, width * 1.04, IRON),
            cyl(x, y + height * 0.78, z, width * 1.04, height * 0.07, width * 1.04, IRON)]


def flame(x, y, z, width, height):
    return [cone(x, y, z, width, height, width, FLAME), cone(x, y, z, width * 1.2, height * 0.5, width * 1.2, FLAME_CORE)]


def wagon():
    parts = [box(-0.1, 0.3, 0, 1.1, 0.08, 0.66, WOOD), rod((0.42, 0.32, 0), (0.75, 0.12, 0), 0.05, DARK_WOOD)]
    for z in (-0.3, 0.3):
        parts.append(box(-0.1, 0.38, z, 1.1, 0.22, 0.05, PALE_WOOD))
    for x in (-0.65, 0.45):
        parts.append(box(x, 0.38, 0, 0.05, 0.22, 0.66, PALE_WOOD))
    for x in (-0.42, 0.22):
        parts.append(rod((x, 0.24, -0.42), (x, 0.24, 0.42), 0.04, IRON))
        parts += wheel(x, 0.36, 0.24) + wheel(x, -0.41, 0.24)
    parts += [ball(-0.3, 0.38, 0, 0.32, 0.3, 0.32, BURLAP)] + barrel_parts(0.12, 0.38, 0, 0.26, 0.32)
    return parts


def campfire():
    parts = []
    for i in range(7):
        t = 2 * math.pi * i / 7
        parts.append(ball(0.33 * math.cos(t), 0, 0.33 * math.sin(t), 0.14, 0.1, 0.14, DARK_STONE))
    for i in range(3):
        t = 2 * math.pi * i / 3
        parts.append(rod((0.28 * math.cos(t), 0.03, 0.28 * math.sin(t)), (-0.03 * math.cos(t), 0.26, -0.03 * math.sin(t)), 0.07, BARK, 'cylinder'))
    # No flame: the game draws the fire itself (the fire flag in data/props.txt), over a bed of embers
    return parts + [cyl(0, 0, 0, 0.42, 0.05, 0.42, (58, 44, 40)), cyl(0, 0.05, 0, 0.26, 0.03, 0.26, GLOW)]


def bedroll():
    return [box(0.05, 0, 0, 0.86, 0.05, 0.5, GREEN), box(0.15, 0.05, 0, 0.6, 0.03, 0.46, BURLAP),
            ball(-0.25, 0.05, 0, 0.26, 0.08, 0.36, LINEN), cyl(0.5, 0.08, -0.26, 0.16, 0.52, 0.16, GREEN, 90, 90)]


def bed():
    return legs(0.65, 0.36, 0.2, 0.08, DARK_WOOD) + [
        box(0, 0.12, 0, 1.3, 0.08, 0.78, DARK_WOOD), box(0, 0.2, 0, 1.26, 0.09, 0.72, LINEN), box(0.17, 0.26, 0, 0.92, 0.05, 0.76, BLUE),
        ball(-0.45, 0.28, 0, 0.24, 0.06, 0.5, LINEN), box(-0.67, 0, 0, 0.06, 0.6, 0.8, DARK_WOOD), box(0.67, 0, 0, 0.06, 0.38, 0.8, DARK_WOOD)]


def barrel():
    return barrel_parts(0, 0, 0, 0.55, 0.7) + [cyl(0, 0.7, 0, 0.47, 0.005, 0.47, DARK_WOOD)]


def ore():
    return [ball(0, 0, 0, 1.0, 0.6, 0.8, STONE), ball(0.15, 0.42, 0.15, 0.2, 0.16, 0.2, (190, 110, 60)),
            ball(-0.25, 0.3, 0.2, 0.2, 0.16, 0.18, (190, 110, 60)), ball(-0.05, 0.44, -0.2, 0.18, 0.14, 0.18, (190, 110, 60)),
            ball(0.33, 0.2, -0.13, 0.16, 0.14, 0.16, (190, 110, 60))]


def tree():
    return [cone(0, 0, 0, 0.55, 0.3, 0.55, BARK), cyl(0, 0, 0, 0.26, 1.2, 0.26, BARK), ball(0, 0.85, 0, 1.8, 1.2, 1.8, DARK_LEAF),
            ball(0.3, 1.5, 0.15, 1.3, 1.1, 1.3, LEAF), ball(-0.28, 1.35, -0.2, 1.2, 1.0, 1.2, LEAF), ball(0, 2.1, 0, 0.95, 0.9, 0.95, LEAF)]


def boulder():
    return [ball(0, 0, 0, 1.2, 0.8, 1.0, STONE), ball(0.38, 0, 0.32, 0.4, 0.28, 0.34, DARK_STONE), ball(-0.05, 0.62, -0.05, 0.6, 0.18, 0.5, GREEN)]


def mushroom_parts(x, z, height, width, color=(182, 128, 78)):
    return [cyl(x, 0, z, width * 0.3, height * 0.75, width * 0.3, BONE), ball(x, height * 0.55, z, width, height * 0.45, width, color)]


def mushroom():
    return mushroom_parts(-0.04, 0, 0.6, 0.42) + mushroom_parts(0.2, 0.15, 0.34, 0.2) + mushroom_parts(0.17, -0.16, 0.4, 0.22, (146, 68, 168))


def bones():
    parts = [ball(-0.48, 0, 0, 0.24, 0.2, 0.22, BONE), rod((-0.34, 0.03, 0), (0.28, 0.03, 0), 0.045, BONE, 'cylinder'),
             rod((0.3, 0.03, 0.05), (0.6, 0.02, 0.2), 0.04, BONE, 'cylinder'), rod((0.3, 0.03, -0.05), (0.6, 0.02, -0.2), 0.04, BONE, 'cylinder'),
             rod((-0.28, 0.03, 0.06), (-0.05, 0.02, 0.3), 0.035, BONE, 'cylinder'), rod((-0.28, 0.03, -0.06), (-0.05, 0.02, -0.3), 0.035, BONE, 'cylinder')]
    for i in range(4):
        x = -0.22 + i * 0.1
        for s in (-1, 1):
            parts.append(rod((x, 0.04, 0), (x, 0.17, s * 0.13), 0.022, BONE))
            parts.append(rod((x, 0.17, s * 0.13), (x, 0.01, s * 0.2), 0.022, BONE))
    return parts


def stall():
    parts = [box(0, 0, 0.3, 1.85, 0.75, 0.05, PALE_WOOD), box(0, 0.75, 0.1, 1.9, 0.06, 0.55, WOOD), pyramid(0, 1.3, 0, 2.0, 0.3, 1.15, RED),
             box(-0.55, 0.81, 0.1, 0.4, 0.14, 0.34, PALE_WOOD), ball(-0.55, 0.92, 0.1, 0.3, 0.1, 0.26, RED),
             ball(0.1, 0.81, 0.1, 0.28, 0.1, 0.16, STRAW), cyl(0.58, 0.81, 0.1, 0.28, 0.08, 0.28, (240, 205, 95))]
    for x in (-0.9, 0.9):
        for z in (-0.45, 0.45):
            parts.append(box(x, 0, z, 0.07, 1.35, 0.07, DARK_WOOD))
    return parts


def locked_chest():
    """A strongbox: planked body on a dark plinth, a domed lid, iron straps and corner plates, and a brass padlock on a hasp.
    Its front, where the lock is, faces +z."""
    parts = [box(0, 0, 0, 0.94, 0.06, 0.6, DARK_WOOD),                                   # plinth
             box(0, 0.06, 0, 0.9, 0.3, 0.56, WOOD)]                                       # body
    for y in (0.15, 0.24):                                                                # plank seams, front and back
        parts += [box(0, y, 0.282, 0.9, 0.012, 0.01, DARK_WOOD), box(0, y, -0.282, 0.9, 0.012, 0.01, DARK_WOOD)]
    # The lid is a half cylinder lying along x, resting on the body
    parts.append(cyl(0.45, 0.36, 0, 0.56, 0.9, 0.56, DARK_WOOD, 0, 90))
    for x in (-0.3, 0.3):
        parts.append(box(x, 0.06, 0, 0.07, 0.3, 0.585, IRON))                           # strap down the body
        parts.append(cyl(x + 0.035, 0.36, 0, 0.6, 0.07, 0.6, IRON, 0, 90))             # and over the lid
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.435, 0.06, sz * 0.27, 0.075, 0.3, 0.075, IRON))     # corner plates
    parts += [box(0, 0.2, 0.287, 0.15, 0.2, 0.03, IRON),                                 # hasp plate on the front
              box(0, 0.12, 0.315, 0.11, 0.1, 0.045, BRASS),                              # padlock body
              box(0, 0.22, 0.315, 0.05, 0.05, 0.03, BRASS),                              # its shackle
              box(0, 0.145, 0.34, 0.02, 0.035, 0.01, DARK_STONE)]                        # keyhole
    return parts


def prisoner_cage():
    s = 0.6
    parts = legs(s, s, 1.8, 0.08, IRON) + [box(0, 0, 0, s * 2 + 0.08, 0.1, s * 2 + 0.08, DARK_WOOD), cyl(0, 0.1, 0, 0.9, 0.01, 0.9, STRAW)]
    for sz in (-1, 1):
        parts.append(box(0, 1.72, sz * s, s * 2, 0.06, 0.06, IRON))
        parts.append(box(sz * s, 1.72, 0, 0.06, 0.06, s * 2, IRON))
    for t in (-0.36, -0.12, 0.12, 0.36):
        for sz in (-1, 1):
            parts.append(box(t, 0.1, sz * s, 0.03, 1.62, 0.03, IRON))
            parts.append(box(sz * s, 0.1, t, 0.03, 1.62, 0.03, IRON))
    return parts + [box(0.12, 0.9, s + 0.025, 0.1, 0.1, 0.03, BRASS)]


def taming_cage():
    """A wide wooden pen with a pitched roof, a straw floor and a feeding trough at the front, for an animal being tamed."""
    s = 0.7
    parts = legs(s, s, 1.5, 0.1, DARK_WOOD) + [box(0, 0, 0, s * 2 + 0.1, 0.1, s * 2 + 0.1, DARK_WOOD),
                                                 cyl(0, 0.1, 0, 1.2, 0.01, 1.2, STRAW)]
    # Rails round the sides, and slats between them
    for y in (0.45, 1.1, 1.45):
        for sz in (-1, 1):
            parts.append(box(0, y, sz * s, s * 2, 0.07, 0.06, WOOD))
            parts.append(box(sz * s, y, 0, 0.06, 0.07, s * 2, WOOD))
    for t in (-0.45, -0.15, 0.15, 0.45):
        for sz in (-1, 1):
            parts.append(box(t, 0.1, sz * s, 0.05, 1.4, 0.05, WOOD))
            parts.append(box(sz * s, 0.1, t, 0.05, 1.4, 0.05, WOOD))
    # A low pitched roof
    parts.append(box(0, 1.52, 0, s * 2 + 0.2, 0.06, s * 2 + 0.2, DARK_WOOD))
    parts.append(pyramid(0, 1.58, 0, s * 2 + 0.1, 0.22, s * 2 + 0.1, BARK))
    # The trough, outside the front rail
    parts += [box(0, 0.1, s + 0.16, 0.7, 0.18, 0.2, DARK_WOOD), box(0, 0.24, s + 0.16, 0.6, 0.05, 0.14, MEAT)]
    return parts


def chair():
    parts = legs(0.18, 0.18, 0.43, 0.04, DARK_WOOD) + [box(0, 0.43, 0, 0.44, 0.04, 0.44, WOOD), box(0, 0.47, 0, 0.36, 0.025, 0.36, RED)]
    for s in (-1, 1):
        parts.append(box(s * 0.18, 0.47, -0.18, 0.04, 0.43, 0.04, DARK_WOOD))
    return parts + [box(0, 0.62, -0.18, 0.36, 0.05, 0.03, WOOD), box(0, 0.78, -0.18, 0.4, 0.12, 0.035, WOOD)]


def bench():
    parts = [box(0, 0.4, 0, 1.5, 0.05, 0.36, WOOD), box(0, 0.16, 0, 1.2, 0.05, 0.04, DARK_WOOD)]
    for x in (-0.58, 0.58):
        parts.append(box(x, 0, 0, 0.07, 0.4, 0.3, DARK_WOOD))
    return parts


def stool():
    parts = [cyl(0, 0.44, 0, 0.38, 0.06, 0.38, WOOD)]
    for i in range(3):
        t = 2 * math.pi * i / 3 + 0.5
        parts.append(rod((0.235 * math.cos(t), 0, 0.235 * math.sin(t)), (0.09 * math.cos(t), 0.45, 0.09 * math.sin(t)), 0.035, DARK_WOOD))
    return parts


def table():
    return legs(0.65, 0.33, 0.72, 0.08, DARK_WOOD) + [
        box(0, 0.72, 0, 1.5, 0.06, 0.82, WOOD), box(0, 0.17, 0, 1.25, 0.05, 0.04, DARK_WOOD),
        cyl(-0.3, 0.78, 0.12, 0.08, 0.09, 0.08, PALE_WOOD), cyl(0.3, 0.78, -0.08, 0.2, 0.02, 0.2, IRON)]


def game_table():
    """A square table with a map, dice and cards on it, a seat on each side."""
    return legs(0.6, 0.6, 0.72, 0.08, DARK_WOOD) + [
        box(0, 0.72, 0, 1.4, 0.06, 1.4, WOOD), box(0, 0.78, 0, 0.9, 0.012, 0.7, CANVAS, 8), box(0.1, 0.792, -0.05, 0.3, 0.008, 0.02, RED, 30),
        box(-0.25, 0.792, 0.05, 0.02, 0.008, 0.3, BLUE, 20), box(0.5, 0.78, 0.4, 0.14, 0.012, 0.2, LINEN, 20), box(0.52, 0.792, 0.42, 0.14, 0.012, 0.2, LINEN, -10),
        box(-0.5, 0.78, -0.4, 0.07, 0.07, 0.07, LINEN, 25), box(-0.38, 0.78, -0.32, 0.07, 0.07, 0.07, RED, 50)]


def end_table():
    return legs(0.19, 0.19, 0.42, 0.045, DARK_WOOD) + [
        box(0, 0.42, 0, 0.42, 0.17, 0.42, DARK_WOOD), box(0, 0.45, 0.205, 0.32, 0.11, 0.02, WOOD), box(0, 0.49, 0.22, 0.04, 0.03, 0.03, BRASS),
        box(0, 0.59, 0, 0.47, 0.035, 0.47, WOOD), cyl(0.05, 0.625, -0.04, 0.05, 0.05, 0.05, LINEN), cone(0.05, 0.675, -0.04, 0.03, 0.025, 0.03, FLAME)]


def round_table():
    return [box(0, 0, 0, 0.75, 0.05, 0.12, DARK_WOOD), box(0, 0, 0, 0.12, 0.05, 0.75, DARK_WOOD), cyl(0, 0.05, 0, 0.14, 0.65, 0.14, DARK_WOOD),
            cyl(0, 0.7, 0, 1.2, 0.05, 1.2, WOOD), cyl(0.2, 0.75, 0.15, 0.2, 0.03, 0.2, IRON), cyl(-0.22, 0.75, -0.1, 0.08, 0.1, 0.08, PALE_WOOD)]


def research_table():
    """A wooden drafting table: a tilted board with a blueprint, loose paper and a pencil on it, and a stool tucked under."""
    paper, blue, ink = (236, 230, 212), (74, 112, 176), (220, 232, 250)
    parts = legs(0.6, 0.3, 0.7, 0.07, DARK_WOOD) + [
        box(0, 0.32, 0, 1.2, 0.05, 0.56, DARK_WOOD), box(0, 0.7, 0, 1.4, 0.05, 0.74, WOOD),
        # The board, tilted up at the back
        box(0, 0.75, -0.06, 1.26, 0.04, 0.6, PALE_WOOD, 0, -14),
        box(-0.1, 0.8, -0.05, 0.78, 0.012, 0.46, blue, 0, -14),
        box(-0.32, 0.815, -0.1, 0.24, 0.006, 0.02, ink, 0, -14), box(-0.1, 0.815, 0.02, 0.4, 0.006, 0.02, ink, 0, -14),
        box(0.08, 0.815, -0.14, 0.02, 0.006, 0.26, ink, 0, -14), box(-0.24, 0.815, 0.1, 0.02, 0.006, 0.14, ink, 0, -14),
        # Loose paper, a pencil and a ruler
        box(0.46, 0.79, 0.02, 0.26, 0.012, 0.34, paper, 12, -14), box(0.5, 0.8, 0.08, 0.22, 0.01, 0.3, paper, -8, -14),
        box(0.3, 0.79, 0.3, 0.3, 0.02, 0.025, (222, 178, 60), 20, 0), box(0.44, 0.79, 0.26, 0.04, 0.02, 0.025, (60, 50, 50), 20, 0),
        box(-0.2, 0.78, 0.31, 0.5, 0.012, 0.05, BRASS), cyl(0.56, 0.75, -0.26, 0.09, 0.1, 0.09, IRON)]
    return parts


def workbench(top_color):
    """A stout bench with a shelf under it, shared by the three workbenches."""
    return legs(0.62, 0.3, 0.68, 0.09, DARK_WOOD) + [
        box(0, 0.68, 0, 1.45, 0.08, 0.74, top_color), box(0, 0.22, 0, 1.3, 0.04, 0.6, WOOD),
        box(0, 0.5, -0.36, 1.45, 0.4, 0.03, DARK_WOOD)]


def weapon_workbench():
    """A bench with an anvil and hammer, a sword blade being made, and blades hung on its back board."""
    parts = workbench(WOOD) + [
        box(-0.38, 0.76, 0.02, 0.36, 0.08, 0.2, IRON), box(-0.38, 0.84, 0.02, 0.2, 0.08, 0.14, IRON), box(-0.38, 0.92, 0.02, 0.5, 0.07, 0.18, STEEL),
        box(0.2, 0.765, 0.1, 0.6, 0.02, 0.06, STEEL, 12), box(0.5, 0.765, 0.04, 0.12, 0.03, 0.03, DARK_WOOD, 12), box(0.42, 0.765, 0.055, 0.03, 0.03, 0.16, BRASS, 12),
        box(0.1, 0.77, -0.16, 0.26, 0.04, 0.04, DARK_WOOD), box(0.26, 0.77, -0.16, 0.08, 0.08, 0.1, IRON),
        box(0, 0.76, -0.33, 1.45, 0.5, 0.03, DARK_WOOD)]
    for x in (-0.45, -0.15, 0.15, 0.45):
        parts += [box(x, 0.86, -0.31, 0.05, 0.34, 0.015, STEEL), box(x, 0.82, -0.31, 0.14, 0.03, 0.02, BRASS)]
    return parts


def armor_workbench():
    """A bench with a breastplate on a stand, a helm, a roll of hide and a mallet."""
    parts = workbench(PALE_WOOD) + [
        cyl(-0.35, 0.76, 0.0, 0.08, 0.3, 0.08, DARK_WOOD), ball(-0.35, 0.98, 0.0, 0.4, 0.42, 0.26, STEEL), box(-0.35, 1.0, 0.13, 0.26, 0.26, 0.02, IRON),
        ball(0.2, 0.76, 0.08, 0.26, 0.24, 0.26, STEEL), box(0.2, 0.8, 0.2, 0.2, 0.05, 0.03, IRON),
        cyl(0.5, 0.82, -0.14, 0.14, 0.4, 0.14, (150, 110, 80), 0, 90),
        box(0.02, 0.77, -0.2, 0.22, 0.04, 0.04, DARK_WOOD), box(0.14, 0.77, -0.2, 0.1, 0.1, 0.12, WOOD)]
    return parts


def ammunition_workbench():
    """A bench with a sheaf of arrow shafts, a bowl of arrowheads, feathers, and a quiver standing on it."""
    parts = workbench(WOOD) + [
        cyl(0.48, 0.76, -0.1, 0.2, 0.42, 0.2, (120, 84, 56)), box(0.48, 0.76, 0.0, 0.03, 0.42, 0.02, DARK_WOOD),
        cyl(-0.45, 0.76, 0.12, 0.26, 0.06, 0.26, DARK_STONE), cone(-0.45, 0.82, 0.12, 0.2, 0.08, 0.2, STEEL)]
    for i in range(5):
        parts.append(box(-0.05, 0.765, -0.16 + 0.07 * i, 0.62, 0.02, 0.02, PALE_WOOD, 4 * (i - 2)))
        parts.append(box(-0.32, 0.765, -0.16 + 0.07 * i, 0.08, 0.03, 0.035, STEEL, 4 * (i - 2)))
        parts.append(box(0.2, 0.775, -0.16 + 0.07 * i, 0.1, 0.03, 0.035, (228, 228, 236), 4 * (i - 2)))
    for dx in (-0.05, 0.0, 0.05):
        parts.append(box(0.48 + dx, 1.18, -0.1, 0.02, 0.16, 0.02, PALE_WOOD))
        parts.append(box(0.48 + dx, 1.3, -0.1, 0.05, 0.08, 0.02, RED))
    return parts


def standing_torch():
    parts = [cyl(0, 0, 0, 0.06, 1.1, 0.06, DARK_WOOD), cyl(0, 1.05, 0, 0.2, 0.09, 0.2, IRON)]
    for i in range(3):
        t = 2 * math.pi * i / 3
        parts.append(rod((0.28 * math.cos(t), 0, 0.28 * math.sin(t)), (0, 0.3, 0), 0.035, DARK_WOOD))
    return parts + [cyl(0, 1.14, 0, 0.14, 0.05, 0.14, (58, 44, 40))]   # the game draws its fire


def support_beam():
    return [box(0, 0, 0, 0.48, 0.2, 0.48, DARK_STONE), box(0, 0.2, 0, 0.2, 2.1, 0.2, WOOD), box(0, 2.3, 0, 1.2, 0.2, 0.24, WOOD),
            rod((0.08, 1.65, 0), (0.48, 2.3, 0), 0.09, DARK_WOOD), rod((-0.08, 1.65, 0), (-0.48, 2.3, 0), 0.09, DARK_WOOD),
            box(0, 0.6, 0, 0.22, 0.05, 0.22, IRON), box(0, 1.5, 0, 0.22, 0.05, 0.22, IRON)]


def training_dummy():
    return [box(0, 0, 0, 0.55, 0.08, 0.14, DARK_WOOD), box(0, 0, 0, 0.14, 0.08, 0.55, DARK_WOOD), cyl(0, 0.08, 0, 0.08, 1.2, 0.08, WOOD),
            cyl(0, 0.65, 0, 0.42, 0.5, 0.42, BURLAP), ball(0, 1.2, 0, 0.28, 0.3, 0.28, BURLAP), box(0, 1.0, 0, 1.2, 0.06, 0.06, WOOD),
            cyl(0, 0.8, 0.2, 0.2, 0.02, 0.2, RED, 90, 90), cyl(-0.45, 0.85, 0.04, 0.36, 0.03, 0.36, WOOD, 90, 90)]


def hanging_meat(x, y, s):
    return [box(x, y - 0.14 * s, 0, 0.012, 0.14 * s, 0.012, IRON), ball(x, y - 0.42 * s, 0, 0.17 * s, 0.3 * s, 0.15 * s, MEAT)]


def raw_food_storage():
    parts = legs(0.46, 0.27, 1.0, 0.06, DARK_WOOD) + [box(0, 0.95, 0, 1.0, 0.05, 0.05, DARK_WOOD), box(0, 0, 0, 0.92, 0.25, 0.56, WOOD),
                                                       ball(-0.22, 0.25, 0, 0.3, 0.1, 0.22, MEAT), ball(0.2, 0.25, 0.03, 0.34, 0.1, 0.24, MEAT)]
    for x, s in ((-0.3, 1.0), (0, 0.85), (0.3, 1.0)):
        parts += hanging_meat(x, 0.95, s)
    return parts


def food_storage():
    parts = [box(-0.4, 0, 0, 0.05, 1.0, 0.38, WOOD), box(0.4, 0, 0, 0.05, 1.0, 0.38, WOOD), box(0, 0, -0.18, 0.8, 1.0, 0.02, DARK_WOOD)]
    for y in (0.02, 0.35, 0.67, 0.97):
        parts.append(box(0, y, 0, 0.8, 0.03, 0.38, WOOD))
    return parts + [ball(-0.17, 0.05, 0, 0.3, 0.28, 0.26, BURLAP), cyl(0.18, 0.05, 0, 0.18, 0.25, 0.18, (172, 118, 76)),
                    cyl(-0.18, 0.38, 0, 0.26, 0.12, 0.26, (240, 205, 95)), ball(0.17, 0.38, 0.02, 0.24, 0.11, 0.14, STRAW),
                    cyl(-0.2, 0.7, 0, 0.14, 0.2, 0.14, (92, 134, 170)), ball(0.1, 0.7, 0.05, 0.1, 0.1, 0.1, RED), ball(0.24, 0.7, -0.03, 0.1, 0.1, 0.1, RED)]


def weapon_rack():
    parts = [box(-0.66, 0, 0, 0.08, 1.0, 0.08, DARK_WOOD), box(0.66, 0, 0, 0.08, 1.0, 0.08, DARK_WOOD), box(-0.66, 0, 0, 0.1, 0.08, 0.5, DARK_WOOD),
             box(0.66, 0, 0, 0.1, 0.08, 0.5, DARK_WOOD), box(0, 0.92, 0, 1.4, 0.06, 0.08, WOOD), box(0, 0.12, 0.08, 1.3, 0.04, 0.06, WOOD)]
    parts += [box(-0.44, 0.16, 0.05, 0.045, 0.62, 0.012, STEEL), box(-0.44, 0.78, 0.05, 0.15, 0.025, 0.03, BRASS), box(-0.44, 0.8, 0.05, 0.03, 0.12, 0.03, DARK_WOOD)]
    parts += [box(-0.1, 0.16, 0.05, 0.03, 0.76, 0.03, WOOD), box(-0.03, 0.72, 0.05, 0.2, 0.16, 0.016, STEEL)]
    parts += [box(0.42, 0.16, 0.05, 0.025, 0.72, 0.025, PALE_WOOD), pyramid(0.42, 0.88, 0.05, 0.07, 0.1, 0.02, STEEL)]
    return parts


def armor_stand():
    return [box(0, 0, 0, 0.56, 0.08, 0.14, DARK_WOOD), box(0, 0, 0, 0.14, 0.08, 0.56, DARK_WOOD), cyl(0, 0.08, 0, 0.06, 1.35, 0.06, WOOD),
            box(0, 1.22, 0, 0.62, 0.05, 0.05, WOOD), cyl(0, 0.7, 0, 0.44, 0.6, 0.34, STEEL), cone(0, 0.45, 0, 0.42, 0.26, 0.3, IRON, 0, 0),
            ball(-0.3, 1.2, 0, 0.24, 0.17, 0.26, STEEL), ball(0.3, 1.2, 0, 0.24, 0.17, 0.26, STEEL), ball(0, 1.4, 0, 0.28, 0.3, 0.3, STEEL),
            box(0, 1.5, 0.14, 0.2, 0.025, 0.02, IRON)]


def water_tank():
    parts = []
    for i in range(4):
        t = 2 * math.pi * i / 4 + 0.78
        parts.append(box(0.4 * math.cos(t), 0, 0.4 * math.sin(t), 0.2, 0.12, 0.2, DARK_STONE))
    parts += [cyl(0, 0.12, 0, 1.2, 1.26, 1.2, WOOD), cyl(0, 1.38, 0, 1.1, 0.02, 1.1, WATER)]
    for y in (0.3, 0.72, 1.14):
        parts.append(cyl(0, y, 0, 1.24, 0.06, 1.24, IRON))
    return parts + [rod((0, 0.32, 0.58), (0, 0.32, 0.72), 0.05, BRASS, 'cylinder')]


def water_pump():
    return [box(0, 0, 0, 0.6, 0.2, 0.6, DARK_STONE), cyl(0, 0.2, 0, 0.16, 0.95, 0.16, IRON), cyl(0, 1.15, 0, 0.2, 0.06, 0.2, STEEL),
            rod((0, 0.85, 0.05), (0, 0.8, 0.38), 0.07, IRON, 'cylinder'), rod((0, 0.8, 0.37), (0, 0.14, 0.37), 0.03, WATER, 'cylinder'),
            box(0, 1.21, -0.04, 0.05, 0.19, 0.05, IRON), rod((0, 1.4, 0.14), (0, 1.25, -0.5), 0.04, WOOD),
            box(0, 0, 0.48, 0.56, 0.22, 0.42, WOOD), box(0, 0.22, 0.48, 0.48, 0.005, 0.34, WATER)]


def forge():
    return [box(-0.2, 0, 0, 1.15, 0.9, 0.86, DARK_STONE), box(-0.2, 0.9, 0, 1.2, 0.07, 0.92, STONE), box(-0.2, 0.97, -0.1, 0.42, 0.63, 0.38, DARK_STONE),
            box(-0.2, 0.15, 0.43, 0.6, 0.6, 0.03, (48, 44, 44)), box(-0.2, 0.2, 0.45, 0.46, 0.2, 0.02, GLOW),
            cyl(0.6, 0, 0.2, 0.28, 0.3, 0.28, BARK), box(0.6, 0.3, 0.2, 0.2, 0.05, 0.12, IRON), box(0.6, 0.35, 0.2, 0.09, 0.07, 0.08, IRON), box(0.6, 0.42, 0.2, 0.3, 0.05, 0.1, IRON)]


def altar():
    parts = [box(0, 0, 0, 1.2, 0.14, 0.95, DARK_STONE), box(0, 0.14, 0, 0.95, 0.12, 0.75, STONE), box(0, 0.26, 0, 0.66, 0.36, 0.5, (150, 140, 170)),
             box(0, 0.62, 0, 0.8, 0.07, 0.6, STONE), cyl(0, 0.69, 0, 0.1, 0.1, 0.1, IRON), cyl(0, 0.79, 0, 0.42, 0.09, 0.42, IRON)]
    parts += [rod((-0.09, 0.3, 0.255), (0, 0.52, 0.255), 0.022, (150, 225, 255)), rod((0.09, 0.3, 0.255), (0, 0.52, 0.255), 0.022, (150, 225, 255))]
    parts += [rod((-0.5, 0.06, 0.32), (-0.2, 0.06, 0.32), 0.1, BARK, 'cylinder'), rod((-0.48, 0.06, 0.42), (-0.2, 0.06, 0.42), 0.1, BARK, 'cylinder')]
    return parts + [cyl(0, 0.88, 0, 0.3, 0.03, 0.3, (58, 44, 40))]   # the game draws its fire


def debris():
    return [cyl(0, 0, 0, 1.0, 0.03, 1.0, SOIL), ball(-0.12, 0, 0.03, 0.55, 0.3, 0.42, STONE), ball(0.2, 0, -0.12, 0.38, 0.26, 0.34, DARK_STONE),
            box(0.26, 0.04, 0.24, 0.24, 0.13, 0.17, STONE, 22), box(-0.3, 0.02, -0.26, 0.24, 0.12, 0.16, DARK_STONE, -28),
            rod((-0.42, 0.04, 0.2), (0.06, 0.44, -0.08), 0.06, DARK_WOOD), rod((0.42, 0.04, 0.08), (-0.02, 0.33, 0.22), 0.05, WOOD)]


def farm():
    parts = [box(0, 0, 0, 2.0, 0.08, 1.5, SOIL)]
    rows = [-0.6, -0.36, -0.12, 0.12, 0.36, 0.6]
    for z in rows:
        parts.append(box(0, 0.08, z, 1.76, 0.045, 0.13, (72, 50, 34)))
    for x in (-0.65, -0.35, -0.05, 0.25, 0.55):
        for z in rows[:2]:
            parts.append(cone(x, 0.12, z, 0.1, 0.42, 0.1, STRAW))
        for z in rows[2:]:
            parts.append(pyramid(x, 0.12, z, 0.16, 0.14, 0.16, LEAF))
    for sx in (-1, 1):
        for z in (-0.74, 0, 0.74):
            parts.append(box(sx * 0.98, 0, z, 0.04, 0.33, 0.04, DARK_WOOD))
        parts.append(box(sx * 0.98, 0.24, 0, 0.02, 0.03, 1.5, WOOD))
    for sz in (-1, 1):
        for x in (-0.49, 0.49):
            parts.append(box(x, 0, sz * 0.74, 0.04, 0.33, 0.04, DARK_WOOD))
        parts.append(box(0, 0.24, sz * 0.74, 2.0, 0.03, 0.02, WOOD))
    return parts + [box(0.7, 0.08, 0.12, 0.04, 0.9, 0.04, WOOD), box(0.7, 0.72, 0.12, 0.62, 0.03, 0.03, WOOD), cyl(0.7, 0.45, 0.12, 0.26, 0.32, 0.18, RED),
                    ball(0.7, 0.78, 0.12, 0.17, 0.18, 0.17, BURLAP), cone(0.7, 0.92, 0.12, 0.3, 0.14, 0.3, STRAW)]


def turbine():
    top = 1.45
    parts = [box(0, 0, 0, 0.8, 0.1, 0.8, DARK_STONE), box(0, top, 0, 0.3, 0.18, 0.36, WOOD), rod((0, top + 0.09, -0.2), (0, top + 0.09, 0.26), 0.05, IRON, 'cylinder'),
             cyl(0, top + 0.09, 0.24, 0.12, 0.05, 0.12, DARK_WOOD, 90, 90)]
    for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        parts.append(rod((sx * 0.32, 0.1, sz * 0.32), (sx * 0.1, top, sz * 0.1), 0.05, DARK_WOOD))
    for y, k in ((0.5, 0.25), (1.0, 0.17)):
        for s in (-1, 1):
            parts.append(box(0, y, s * k, k * 2 + 0.05, 0.03, 0.03, WOOD))
            parts.append(box(s * k, y, 0, 0.03, 0.03, k * 2 + 0.05, WOOD))
    for i in range(4):
        t = 2 * math.pi * i / 4 + 0.4
        c, s = math.cos(t), math.sin(t)
        cy = top + 0.09
        parts.append(rod((0, cy, 0.28), (0.45 * c, cy + 0.45 * s, 0.28), 0.03, DARK_WOOD))
        parts.append(rod((0.13 * c - 0.05 * s, cy + 0.13 * s + 0.05 * c, 0.27), (0.45 * c - 0.05 * s, cy + 0.45 * s + 0.05 * c, 0.27), 0.1, CANVAS))
    return parts


def stove():
    return legs(0.28, 0.2, 0.17, 0.05, IRON) + [
        box(0, 0.17, 0, 0.75, 0.52, 0.55, (64, 68, 78)), box(0, 0.69, 0, 0.8, 0.05, 0.6, IRON), box(-0.17, 0.27, 0.28, 0.3, 0.3, 0.02, IRON),
        box(-0.17, 0.32, 0.292, 0.22, 0.18, 0.006, GLOW), box(0.2, 0.27, 0.28, 0.26, 0.3, 0.02, IRON), box(0.2, 0.52, 0.295, 0.18, 0.02, 0.015, BRASS),
        cyl(-0.17, 0.74, 0.05, 0.24, 0.14, 0.24, IRON), cyl(-0.17, 0.88, 0.05, 0.2, 0.004, 0.2, MEAT),
        cyl(0.2, 0.74, -0.14, 0.12, 0.4, 0.12, IRON), cone(0.2, 1.14, -0.14, 0.2, 0.06, 0.2, IRON)]


def airship():
    """A wooden boat hung under a canvas balloon. The game floats it above the ground; y = 0 here is the bottom of its keel."""
    parts = [box(0, 0, 0, 2.0, 0.18, 0.55, DARK_WOOD), box(0, 0.18, 0, 2.5, 0.42, 1.0, WOOD), box(0, 0.6, 0, 2.5, 0.05, 1.0, PALE_WOOD),
             pyramid(1.45, 0.18, 0, 0.5, 0.42, 1.0, WOOD, 0, 0), box(-1.3, 0.6, 0, 0.3, 0.35, 1.0, DARK_WOOD)]
    for z in (-0.5, 0.5):
        parts.append(box(0, 0.65, z, 2.5, 0.14, 0.05, DARK_WOOD))
    parts.append(ball(0, 1.45, 0, 3.6, 1.35, 1.7, CANVAS))
    parts.append(box(0, 1.42, 0, 3.0, 0.06, 1.3, BURLAP))
    for x in (-0.9, 0.9):
        for z in (-0.45, 0.45):
            parts.append(rod((x, 0.65, z), (x * 1.05, 1.5, z * 1.35), 0.03, BURLAP))
    parts += [rod((-1.45, 0.45, 0), (-1.75, 0.45, 0), 0.06, IRON, 'cylinder'),
              box(-1.8, 0.2, 0, 0.04, 0.5, 0.12, DARK_WOOD), box(-1.8, 0.4, 0, 0.04, 0.12, 0.55, DARK_WOOD)]
    return parts



def wheelbarrow():
    """A tray on one wheel at the front (+x, the way it is pushed), with two handles back toward its pusher."""
    parts = [box(0.1, 0.3, 0, 0.75, 0.24, 0.56, WOOD), box(0.1, 0.5, 0, 0.66, 0.05, 0.46, DARK_WOOD),
             box(0.1, 0.54, -0.27, 0.78, 0.05, 0.04, PALE_WOOD), box(0.1, 0.54, 0.27, 0.78, 0.05, 0.04, PALE_WOOD)]
    parts += wheel(0.58, -0.03, 0.2)
    for z in (-0.2, 0.2):
        parts.append(rod((0.62, 0.2, z * 0.4), (0.25, 0.32, z), 0.04, IRON))
        parts.append(rod((0.4, 0.32, z), (-0.62, 0.5, z * 1.2), 0.05, DARK_WOOD))
        parts.append(box(-0.62, 0.47, z * 1.2, 0.1, 0.06, 0.07, BARK))
        parts.append(box(-0.15, 0, z, 0.05, 0.33, 0.05, DARK_WOOD))
    return parts


WALL_HEIGHT = 2.4   # src/building.h FLOOR_HEIGHT: the upper floor sits on the walls


# A building piece's colours by material: main, dark, light, footing
WOOD_PAL = (WOOD, DARK_WOOD, PALE_WOOD, DARK_STONE)
STONE_PAL = ((158, 152, 140), (112, 108, 100), (192, 186, 172), (82, 80, 78))
IRON_PAL = ((132, 138, 150), (84, 88, 100), (178, 184, 196), (70, 72, 80))   # shown as Metal; made of Iron Ingots
CRYSTAL_PAL = ((150, 92, 212), (98, 54, 152), (206, 166, 252), (72, 52, 92))
# Each material's models sit beside the wood one: wall_wood.glb has wall_metal.glb, wall_crystal_straight.glb and so on.
# The game also lays each material's texture over them (tools/make_building_textures.py).
MATERIALS = {'_stone': STONE_PAL, '_metal': IRON_PAL, '_crystal': CRYSTAL_PAL}


def foundation(pal=WOOD_PAL):
    """One tile of a finished foundation, a low deck. The game lays one for each tile the foundation covers."""
    main, dark, light, foot = pal
    return [box(0, 0, 0, 1.0, 0.14, 1.0, dark), box(0, 0.14, -0.25, 0.97, 0.06, 0.46, main), box(0, 0.14, 0.25, 0.97, 0.06, 0.46, light)]


def wall_tile(arms, pal=WOOD_PAL):
    """One tile of wall: a post in the middle and an arm toward each wall beside it. The footing spans the whole tile, so
    that every shape is centred on its tile the same way (the game centres a model on its bounds)."""
    main, dark, light, foot = pal
    # No two faces share a plane, so none flicker: the post stops under the cap, an arm runs from inside the post to the
    # tile's edge (meeting the next wall's arm there, not overlapping it) and stops just under the cap's height, and each
    # band is a hair shorter than its arm
    cap = WALL_HEIGHT - 0.1
    parts = [box(0, 0, 0, 1.0, 0.05, 1.0, foot), box(0, 0.05, 0, 0.38, cap - 0.06, 0.38, dark)]
    for dx, dz in arms:
        cx, cz = dx * 0.335, dz * 0.335
        w, d = (0.33, 0.3) if dx else (0.3, 0.33)
        parts.append(box(cx, 0.05, cz, w, cap - 0.07, d, main))
        bw, bd = (w - 0.004, d + 0.02) if dx else (w + 0.02, d - 0.004)
        for y in (0.6, 1.3, 2.0):
            parts.append(box(cx, y, cz, bw, 0.05, bd, dark))
    parts.append(box(0, cap, 0, 0.42, 0.1, 0.42, light))
    return parts


def arm_box(dx, dz, along, across, y, h, color, centre=0.335):
    """A box on a wall's arm toward (dx, dz): along runs toward the tile's edge, across through the wall."""
    w, d = (along, across) if dx else (across, along)
    return box(dx * centre, y, dz * centre, w, h, d, color)


def stone_wall_tile(arms, pal=STONE_PAL):
    """A tile of stone wall: a thick pier on a plinth under a flat capstone, and arms of cut blocks laid in courses on a
    darker core, the joints between the courses showing as sunk mortar."""
    main, dark, light, foot = pal
    cap = WALL_HEIGHT - 0.12
    parts = [box(0, 0, 0, 1.0, 0.05, 1.0, foot), box(0, 0.05, 0, 0.5, 0.22, 0.5, dark),
             box(0, 0.27, 0, 0.44, cap - 0.27, 0.44, dark), box(0, cap, 0, 0.52, 0.12, 0.52, light)]
    courses = 7
    ch = (cap - 0.06 - 0.05) / courses
    for dx, dz in arms:
        # The mortar core stops a hair under the coping's top (cap - 0.01), or the two tops share a plane and flicker
        parts.append(arm_box(dx, dz, 0.33, 0.30, 0.05, cap - 0.07, dark))   # the mortar core
        for i in range(courses):
            # Alternate courses stand a hair further out, so no two block faces share a plane
            proud = 0.34 if i % 2 else 0.335
            parts.append(arm_box(dx, dz, 0.326, proud, 0.05 + i * ch + 0.008, ch - 0.016, main if i % 2 else light))
        parts.append(arm_box(dx, dz, 0.33, 0.36, cap - 0.06, 0.05, light))   # coping along the top
    return parts


def metal_wall_tile(arms, pal=IRON_PAL):
    """A tile of metal wall: a square column with a flared foot and head, and arms of thin plate between a rail at the
    bottom and top, with an upright rib and rivets along each rail."""
    main, dark, light, foot = pal
    cap = WALL_HEIGHT - 0.1
    parts = [box(0, 0, 0, 1.0, 0.05, 1.0, foot), box(0, 0.05, 0, 0.42, 0.14, 0.42, dark),
             box(0, 0.19, 0, 0.3, cap - 0.19, 0.3, dark), box(0, cap, 0, 0.42, 0.1, 0.42, light)]
    for dx, dz in arms:
        parts.append(arm_box(dx, dz, 0.33, 0.14, 0.05, cap - 0.04, main))          # the plate
        parts.append(arm_box(dx, dz, 0.326, 0.22, 0.05, 0.12, dark))              # bottom rail
        parts.append(arm_box(dx, dz, 0.326, 0.22, cap - 0.16, 0.12, dark))        # top rail
        parts.append(arm_box(dx, dz, 0.05, 0.19, 0.17, cap - 0.33, light, 0.36))  # upright rib
        for along in (0.24, 0.44):
            for y in (0.09, cap - 0.12):
                parts.append(arm_box(dx, dz, 0.035, 0.235, y, 0.04, light, along))  # rivets
    return parts


def crystal_wall_tile(arms, pal=CRYSTAL_PAL):
    """A tile of crystal wall: a tall crystal stood on its corner with a point on top, and arms of crystal slab with a
    darker band at the foot, small points along the top and shards leaning at the foot."""
    main, dark, light, foot = pal
    point = 0.34
    parts = [box(0, 0, 0, 1.0, 0.05, 1.0, foot), box(0, 0.05, 0, 0.34, WALL_HEIGHT - point - 0.05, 0.34, dark, 45),
             pyramid(0, WALL_HEIGHT - point, 0, 0.34, point, 0.34, light, 45)]
    top = WALL_HEIGHT - 0.3
    for dx, dz in arms:
        parts.append(arm_box(dx, dz, 0.33, 0.18, 0.05, top - 0.05, main))
        parts.append(arm_box(dx, dz, 0.326, 0.2, 0.05, 0.3, dark))
        for along in (0.24, 0.4):
            parts.append(pyramid(dx * along, top, dz * along, 0.12, 0.2, 0.12, light, 45))
            sx, sz = (0, 0.13) if dx else (0.13, 0)
            for sgn in (-1, 1):
                parts.append(pyramid(dx * along + sgn * sx, 0.05, dz * along + sgn * sz, 0.08, 0.32, 0.08, light, 45))
    return parts


def wood_wall(pal=WOOD_PAL):
    return wall_tile([], pal)


# Each material's wall is a design of its own, not the wood one in other colours
WALL_STYLES = {'': (wall_tile, WOOD_PAL), '_stone': (stone_wall_tile, STONE_PAL), '_metal': (metal_wall_tile, IRON_PAL),
               '_crystal': (crystal_wall_tile, CRYSTAL_PAL)}

# The wall's other shapes, written next to its model: <model>_end.glb and so on. Each is drawn for its arms east (+x),
# south (+z), west and north; the game turns it to fit.
WALL_SHAPES = {
    '_end': [(1, 0)],
    '_straight': [(1, 0), (-1, 0)],
    '_corner': [(1, 0), (0, 1)],
    '_tee': [(1, 0), (0, 1), (-1, 0)],
    '_cross': [(1, 0), (0, 1), (-1, 0), (0, -1)],
}
SHAPED = {'WoodWall', 'UpperFloor'}
MATERIAL_KEYS = {'Foundation', 'WoodWall', 'WoodDoor', 'UpperFloor', 'Ladder', 'PerimeterWall', 'PerimeterGate', 'Slope'}


def wood_door(pal=WOOD_PAL):
    """A plank door in a frame, across the x axis. The game turns it to lie along the foundation's edge."""
    main, dark, light, foot = pal
    parts = [box(0, 0, 0, 1.0, 0.05, 0.4, foot)]
    for x in (-0.44, 0.44):
        parts.append(box(x, 0.05, 0, 0.12, WALL_HEIGHT - 0.05, 0.36, dark))
    parts.append(box(0, WALL_HEIGHT - 0.22, 0, 1.0, 0.22, 0.36, dark))
    for x in (-0.28, -0.09, 0.09, 0.28):
        parts.append(box(x, 0.05, 0, 0.18, WALL_HEIGHT - 0.3, 0.08, main if x < 0 or x > 0.1 else light))
    parts += [box(0, 0.6, 0.05, 0.74, 0.08, 0.04, dark), box(0, 1.6, 0.05, 0.74, 0.08, 0.04, dark),
              box(0.26, 1.05, 0.08, 0.08, 0.08, 0.06, IRON)]
    return parts


def stone_door(pal=STONE_PAL):
    """A heavy slab door between thick stone jambs under a lintel, bound with two iron straps."""
    main, dark, light, foot = pal
    parts = [box(0, 0, 0, 1.0, 0.05, 0.4, foot)]
    for x in (-0.42, 0.42):
        parts.append(box(x, 0.05, 0, 0.16, WALL_HEIGHT - 0.05, 0.38, dark))
    parts.append(box(0, WALL_HEIGHT - 0.32, 0, 0.67, 0.32, 0.36, light))   # the lintel, between the jambs
    parts.append(box(0, 0.05, 0, 0.66, WALL_HEIGHT - 0.38, 0.12, main))    # the slab
    for y in (0.55, 1.55):
        parts.append(box(0, y, 0, 0.664, 0.08, 0.15, IRON))
    parts.append(box(0.22, 1.05, 0.07, 0.07, 0.12, 0.05, IRON))           # a pull
    return parts


def metal_door(pal=IRON_PAL):
    """A riveted plate door in a slim metal frame, with level ribs and a long bar handle."""
    main, dark, light, foot = pal
    parts = [box(0, 0, 0, 1.0, 0.05, 0.4, foot)]
    for x in (-0.45, 0.45):
        parts.append(box(x, 0.05, 0, 0.1, WALL_HEIGHT - 0.05, 0.34, dark))
    parts.append(box(0, WALL_HEIGHT - 0.16, 0, 0.8, 0.16, 0.32, dark))
    parts.append(box(0, 0.05, 0, 0.78, WALL_HEIGHT - 0.22, 0.07, main))
    for y in (0.35, 0.95, 1.55, 2.0):
        parts.append(box(0, y, 0, 0.72, 0.06, 0.1, light))
        for x in (-0.33, 0.33):
            parts.append(box(x, y + 0.01, 0, 0.04, 0.04, 0.12, light))
    parts.append(box(0.28, 0.85, 0.07, 0.04, 0.4, 0.04, light))           # the handle
    return parts


def crystal_door(pal=CRYSTAL_PAL):
    """A door of crystal panes set with diamonds, between two pointed crystal pillars."""
    main, dark, light, foot = pal
    point = 0.3
    parts = [box(0, 0, 0, 1.0, 0.05, 0.4, foot)]
    for x in (-0.4, 0.4):
        parts.append(box(x, 0.05, 0, 0.16, WALL_HEIGHT - point - 0.05, 0.16, dark, 45))
        parts.append(pyramid(x, WALL_HEIGHT - point, 0, 0.16, point, 0.16, light, 45))
    parts.append(box(0, WALL_HEIGHT - 0.6, 0, 0.62, 0.14, 0.18, light))   # the head between the pillars
    parts.append(box(0, 0.05, 0, 0.6, WALL_HEIGHT - 0.66, 0.1, main))
    for y in (0.35, 0.85, 1.35):
        parts.append(box(0, y, 0, 0.2, 0.2, 0.13, light, 0, 45))           # diamonds, a little proud of the pane
    return parts


# Each material's door is a design of its own
DOOR_STYLES = {'': wood_door, '_stone': stone_door, '_metal': metal_door, '_crystal': crystal_door}


ROPE = (176, 150, 96)
DECK_TOP = 2.54      # src/building.h FLOOR_TOP: where a unit on the first upper floor stands, and so where a wall's walkway is
DECK_BODY = DECK_TOP - 0.08
OPEN_FACE = ((0, -1), (1, 0), (0, 1), (-1, 0))   # north, east, south, west: the bits of a mask 1, 2, 4, 8


def perimeter_wall(pal=WOOD_PAL, arms=()):
    """A tile of Perimeter Wall, a palisade: a mass of lashed logs under a plank deck at walking height, filling its 2 by 2 footprint. It
    faces north: every open side but the south one (the way in, behind it) is a rank of tall sharpened logs, and the south one a low rail.
    arms are the sides with another Perimeter piece beside them (no spikes there); the game turns the model to its facing. Four specks too
    small to see keep every shape's bounds the same."""
    main, dark, light, foot = pal
    exposed = [d for d in OPEN_FACE if d not in arms]
    back = (0, 1) if (0, 1) in exposed else None
    spiked = [d for d in exposed if d != back]
    parts = []
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.999, 0, sz * 0.999, 0.002, 0.002, 0.002, dark))
    parts.append(box(0.999, 3.9 - 0.002, 0.999, 0.002, 0.002, 0.002, dark))
    n, step = 5, 0.4
    for i in range(n):
        for j in range(n):
            parts.append(cyl((i - 2) * step, 0, (j - 2) * step, 0.38, DECK_BODY, 0.38, main if (i * 3 + j * 5) % 3 else light))
    parts.append(box(0, DECK_BODY, 0, 2.0, 0.08, 2.0, light))
    for d in spiked:
        for k in range(n):
            u = (k - 2) * step
            x, z = (d[0] * 0.8 + (u if d[1] else 0), d[1] * 0.8 + (u if d[0] else 0))
            h = 3.45 + 0.12 * ((k * 5 + (d[0] + 2 * d[1])) % 4)
            parts.append(cyl(x, DECK_BODY, z, 0.38, h - 0.5 - DECK_BODY, 0.38, main if k % 2 else light))
            parts.append(cone(x, h - 0.5, z, 0.38, 0.5, 0.38, dark))
        if d[0]:
            parts.append(box(d[0] * 0.99, 3.0, 0, 0.05, 0.12, 2.0, ROPE))
        else:
            parts.append(box(0, 3.0, d[1] * 0.99, 2.0, 0.12, 0.05, ROPE))
    if back:
        for k in (-2, 0, 2):
            parts.append(cyl(k * step, DECK_TOP, 0.85, 0.2, 0.55, 0.2, main))
        parts.append(box(0, DECK_TOP + 0.45, 0.85, 1.9, 0.09, 0.1, dark))
    return parts


def perimeter_gate(pal=WOOD_PAL, open_=False):
    """A Perimeter Gate, a timber gatehouse with no footing: frame towers either side of the arch, a deck over it at walking height, a rank
    of spikes along the outer (north) edge, a low rail along the inner one, and a roof on four posts. Its leaves shut, or swung back."""
    main, dark, light, foot = pal
    parts = []
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 1.999, 0, sz * 0.999, 0.002, 0.002, 0.002, dark))
    parts.append(box(1.999, 4.8 - 0.002, 0.999, 0.002, 0.002, 0.002, dark))
    for sx in (-1, 1):
        x = sx * 1.6
        parts.append(box(x, 0, 0, 0.8, DECK_BODY, 1.6, main))
        for px in (-0.3, 0.3):
            for pz in (-0.7, 0.7):
                parts.append(box(x + px, 0, pz, 0.24, DECK_BODY - 0.02, 0.24, dark))   # a little proud of the tower's sides, a little short of its top
        for y in (0.04, 1.2, 2.2):
            parts.append(box(x, y, 0, 0.86, 0.16, 1.66, dark))
        for zf in (-0.84, 0.84):
            parts.append(rod((x - 0.42, 0.2, zf), (x + 0.42, 2.1, zf), 0.08, light))
            parts.append(rod((x + 0.42, 0.2, zf), (x - 0.42, 2.1, zf), 0.08, light))
    parts.append(box(0, 2.14, 0, 2.6, 0.30, 1.5, dark))   # its ends buried in the towers, its top under the deck
    parts.append(box(0, DECK_BODY - 0.02, 0, 4.02, 0.10, 2.02, light))   # sunk into the towers' tops and a little wider than them
    if open_:
        for sx in (-1, 1):
            parts.append(box(sx * 1.08, 0.05, 0.55, 0.14, 2.05, 0.95, main))
    else:
        for sx in (-1, 1):
            parts.append(box(sx * 0.6, 0.05, 0, 1.10, 2.05, 0.14, main))
            parts.append(rod((sx * 0.6 - 0.5, 0.2, -0.09), (sx * 0.6 + 0.5, 1.95, -0.09), 0.1, dark))
            parts.append(rod((sx * 0.6 + 0.5, 0.2, -0.09), (sx * 0.6 - 0.5, 1.95, -0.09), 0.1, dark))
    for k in range(10):
        x = -1.8 + k * 0.4
        h = 3.45 + 0.12 * ((k * 5) % 4)
        parts.append(cyl(x, DECK_TOP - 0.02, -0.82, 0.38, h - 0.5 - DECK_TOP + 0.02, 0.38, main if k % 2 else light))
        parts.append(cone(x, h - 0.5, -0.82, 0.38, 0.5, 0.38, dark))
    parts.append(box(0, 3.0, -1.0, 4.0, 0.12, 0.05, ROPE))
    for x in (-1.8, -0.9, 0, 0.9, 1.8):
        parts.append(cyl(x, DECK_TOP - 0.02, 0.9, 0.2, 0.57, 0.2, main))
    parts.append(box(0, DECK_TOP + 0.45, 0.9, 4.0, 0.09, 0.1, dark))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 1.75, DECK_TOP - 0.02, sz * 0.5, 0.18, 1.77, 0.18, dark))
    parts.append(box(0, 4.38, 0, 4.4, 0.14, 2.2, dark))
    parts.append(box(0, 4.51, 0, 3.4, 0.14, 1.7, main))   # each roof tier sunk a little into the one below
    parts.append(box(0, 4.64, 0, 2.2, 0.16, 1.1, light))
    return parts


def perimeter_gate_open(pal=WOOD_PAL):
    return perimeter_gate(pal, True)


def slope(pal=WOOD_PAL):
    """A Slope: a ramp 1 wide and 3 long, rising 2.54 to the north (the way it faces), of twelve blocks that read as a slope, with a rail
    each side."""
    main, dark, light, foot = pal
    rise, steps = 2.54, 12
    parts = []
    for i in range(steps):
        z = 1.5 - (i + 0.5) * 3.0 / steps
        parts.append(box(0, 0, z, 0.92, rise * (i + 1) / steps, 3.0 / steps + 0.002, main if i % 2 else light))
    for sx in (-1, 1):
        parts.append(box(sx * 0.48, 0, 0, 0.08, 0.2, 3.0, dark))
        parts.append(rod((sx * 0.48, 0.45, 1.5), (sx * 0.48, rise + 0.45, -1.5), 0.08, dark))
        for z in (1.4, 0.2, -1.0):
            h = (1.5 - z) / 3.0 * rise
            parts.append(box(sx * 0.48, h, z, 0.07, 0.5, 0.07, dark))
    return parts


def watchtower(pal=WOOD_PAL, manned=False):
    """A Watchtower: four braced posts under a railed platform with a roof over it, on a 2 by 2 footprint. With nobody up it, a ladder
    climbs its south face; manned, the ladder is gone."""
    main, dark, light, foot = pal
    parts = []
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.999, 0, sz * 0.999, 0.002, 0.002, 0.002, dark))
            parts.append(box(sx * 0.82, 0, sz * 0.82, 0.3, 3.3, 0.3, dark))
    parts.append(box(0, 3.3, 0, 2.0, 0.1, 2.0, main))
    for y in (0.5, 1.7):
        for sx in (-1, 1):
            parts.append(box(sx * 0.82, y, 0, 0.12, 0.14, 1.6, main))
            parts.append(box(0, y, sx * 0.82, 1.6, 0.14, 0.12, main))
    for sx in (-1, 1):
        parts.append(rod((sx * 0.82, 0.3, 0.82), (sx * 0.82, 3.2, -0.82), 0.1, light))
        parts.append(rod((sx * 0.82, 0.3, -0.82), (sx * 0.82, 3.2, 0.82), 0.1, light))
    for x in (-0.8, -0.27, 0.27, 0.8):
        for z in (-0.92, 0.92):
            parts.append(box(x, 3.4, z, 0.1, 0.6, 0.08, main))
            parts.append(box(z, 3.4, x, 0.08, 0.6, 0.1, main))
    for z in (-0.95, 0.95):
        parts.append(box(0, 3.95, z, 1.9, 0.08, 0.1, dark))
        parts.append(box(z, 3.95, 0, 0.1, 0.08, 1.9, dark))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.9, 3.4, sz * 0.9, 0.14, 1.3, 0.14, dark))
    parts.append(box(0, 4.7, 0, 2.5, 0.12, 2.5, dark))
    parts.append(pyramid(0, 4.82, 0, 2.3, 0.18, 2.3, main))
    parts.append(box(0, 4.998, 0, 0.1, 0.002, 0.1, light))
    if not manned:
        for sx in (-1, 1):
            parts.append(box(sx * 0.3, 0, 1.07, 0.08, 3.4, 0.08, light))
        for k in range(10):
            parts.append(box(0, 0.25 + k * 0.33, 1.07, 0.6, 0.06, 0.08, light))
    return parts


def dragon_crystal():
    """A geode: a ring of dark rock split open round a crown of purple crystal spires, one tall in the middle and the
    rest leaning out, each a six-sided shaft under a pale point. Rare: a new level has one."""
    deep, mid, pale, rock, rock2 = (98, 54, 152), (150, 92, 212), (214, 180, 255), (72, 66, 78), (96, 90, 100)
    parts = [cyl(0, 0, 0, 1.0, 0.1, 0.92, rock)]
    # The broken rock round the rim
    for i in range(7):
        t = 2 * math.pi * i / 7 + 0.3
        r = 0.4
        parts.append(ball(r * math.cos(t), 0.04, r * math.sin(t) * 0.92, 0.3, 0.2 + 0.1 * (i % 3), 0.28, rock2 if i % 2 else rock))

    def spire(x, z, lean_x, lean_z, height, width, color):
        base = (x, 0.1, z)
        top = (x + lean_x * height, 0.1 + height * 0.74, z + lean_z * height)
        tip = (x + lean_x * height * 1.3, 0.1 + height, z + lean_z * height * 1.3)
        parts.append(rod(base, top, width, color, 'cylinder'))
        parts.append(rod(top, tip, width, pale, 'cone'))

    spire(0, 0, 0.03, -0.02, 1.2, 0.26, mid)
    for i, (h, w) in enumerate(((0.8, 0.2), (0.62, 0.16), (0.9, 0.2), (0.55, 0.15), (0.72, 0.18), (0.48, 0.14))):
        t = 2 * math.pi * i / 6 + 0.55
        cx, cz = 0.2 * math.cos(t), 0.2 * math.sin(t)
        spire(cx, cz, 0.42 * math.cos(t), 0.42 * math.sin(t), h, w, deep if i % 2 else mid)
    # Small shards at the foot
    for i in range(5):
        t = 2 * math.pi * i / 5 + 1.1
        parts.append(pyramid(0.34 * math.cos(t), 0.1, 0.3 * math.sin(t), 0.1, 0.2, 0.1, pale, 40 * i, 0))
    return parts


def wood_stockpile():
    """Logs stacked on a low pallet."""
    parts = [box(0, 0, 0, 1.3, 0.1, 1.0, DARK_WOOD)]
    for row, (y, n) in enumerate(((0.1, 4), (0.33, 3), (0.56, 2))):
        for i in range(n):
            x = (i - (n - 1) / 2) * 0.27
            parts.append(cyl(x, y, 0, 0.24, 0.24, 0.9, WOOD, 0, 1.5707963))
    return parts


def stone_stockpile():
    """Stone blocks on a low pallet."""
    parts = [box(0, 0, 0, 1.3, 0.1, 1.0, DARK_WOOD)]
    # The top two blocks differ in height and footprint, so their tops and sides never share a plane
    for x, z, y, h, c in ((-0.32, -0.22, 0.1, 0.3, STONE), (0.32, -0.22, 0.1, 0.3, DARK_STONE), (-0.32, 0.22, 0.1, 0.3, DARK_STONE),
                          (0.32, 0.22, 0.1, 0.3, STONE), (-0.16, -0.02, 0.4, 0.3, STONE), (0.3, 0.05, 0.4, 0.26, DARK_STONE)):
        parts.append(box(x, y, z, 0.5, h, 0.4 if h == 0.3 else 0.36, c))
    return parts


def iron_stockpile():
    """Iron ingots stacked on a low pallet."""
    parts = [box(0, 0, 0, 1.3, 0.1, 1.0, DARK_WOOD)]
    for y, n in ((0.1, 3), (0.26, 2)):
        for i in range(n):
            x = (i - (n - 1) / 2) * 0.4
            for z in (-0.22, 0.22):
                parts.append(box(x, y, z, 0.36, 0.16, 0.38, STEEL if (i + (y > 0.2)) % 2 else IRON))
    return parts


def ore_stockpile():
    """Raw ore lumps heaped on a low pallet."""
    parts = [box(0, 0, 0, 1.3, 0.1, 1.0, DARK_WOOD)]
    for x, z, y, w, h, c in ((-0.3, -0.2, 0.1, 0.5, 0.3, STONE), (0.3, -0.2, 0.1, 0.5, 0.3, DARK_STONE), (-0.3, 0.22, 0.1, 0.5, 0.3, DARK_STONE),
                             (0.3, 0.22, 0.1, 0.5, 0.3, STONE), (0.0, 0.0, 0.4, 0.5, 0.26, STONE)):
        parts.append(ball(x, y, z, w, h, w * 0.85, c))
    for x, y, z in ((-0.3, 0.32, -0.2), (0.32, 0.3, 0.22), (0.05, 0.6, 0.0), (0.3, 0.3, -0.2)):
        parts.append(ball(x, y, z, 0.16, 0.12, 0.16, (190, 110, 60)))
    return parts


def ladder(pal=WOOD_PAL):
    """Two rails and rungs, from the ground up past the upper floor."""
    main, dark, light, foot = pal
    parts = []
    for x in (-0.26, 0.26):
        parts.append(box(x, 0, 0, 0.08, WALL_HEIGHT + 0.3, 0.08, dark))
    y = 0.3
    while y < WALL_HEIGHT + 0.2:
        parts.append(box(0, y, 0, 0.5, 0.05, 0.06, main))
        y += 0.32
    return parts


def floor_trim(pal):
    """How far a floor's edge reaches from the middle of its tile where it stops at the wall below it (the cap of that material's wall)."""
    return 0.27 if pal is STONE_PAL or pal is CRYSTAL_PAL else 0.22


def floor_tile(arms, pal=WOOD_PAL):
    """One tile of floor over the one below, built as a foundation is: a base slab with two plank bands on it, but with its base
    in the main colour. Each side with an arm (a floor beside it, or no wall under it) runs to the tile's edge; a side without
    one stops at the wall's outer face, so an edge or a corner doesn't stick out past the wall. Four specks too small to see keep
    every shape's bounds the whole tile."""
    main, dark, light, foot = pal
    trim = floor_trim(pal)
    lo_x = -0.5 if (-1, 0) in arms else -trim
    hi_x = 0.5 if (1, 0) in arms else trim
    lo_z = -0.5 if (0, -1) in arms else -trim
    hi_z = 0.5 if (0, 1) in arms else trim
    cx, cz, w, d = (lo_x + hi_x) / 2, (lo_z + hi_z) / 2, hi_x - lo_x, hi_z - lo_z
    parts = []
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.499, 0, sz * 0.499, 0.002, 0.002, 0.002, main))
    parts.append(box(cx, 0, cz, w, 0.09, d, main))
    half = d / 2
    parts.append(box(cx, 0.09, cz - half / 2, w - 0.03, 0.05, half - 0.03, light))
    parts.append(box(cx, 0.09, cz + half / 2, w - 0.03, 0.05, half - 0.03, main))
    return parts


def upper_floor(pal=WOOD_PAL):
    return floor_tile([], pal)


def stair_step(pal=WOOD_PAL):
    """One step of stairs, a whole tile and a unit tall. The game stretches it up to each step's height."""
    return [box(0, 0, 0, 1.0, 1.0, 1.0, pal[0])]


BUILDERS = {
    'Wagon': wagon, 'Campfire': campfire, 'Bedroll': bedroll, 'Bed': bed, 'Barrel': barrel, 'Ore': ore, 'Tree': tree,
    'Boulder': boulder, 'Mushroom': mushroom, 'Bones': bones, 'Stall': stall, 'LockedChest': locked_chest,
    'PrisonerCage': prisoner_cage, 'Chair': chair, 'Bench': bench, 'Stool': stool, 'Table': table, 'GameTable': game_table, 'EndTable': end_table,
    'RoundTable': round_table, 'ResearchTable': research_table, 'StandingTorch': standing_torch, 'SupportBeam': support_beam,
    'TrainingDummy': training_dummy, 'RawFoodStorage': raw_food_storage, 'FoodStorage': food_storage, 'WeaponRack': weapon_rack,
    'ArmorStand': armor_stand, 'WaterTank': water_tank, 'WaterPump': water_pump, 'Forge': forge, 'Altar': altar, 'Debris': debris,
    'Farm': farm, 'Turbine': turbine, 'Stove': stove, 'WeaponBench': weapon_workbench, 'ArmorBench': armor_workbench, 'AmmoBench': ammunition_workbench, 'Airship': airship, 'Foundation': foundation,
    'WoodWall': wood_wall, 'WoodDoor': wood_door, 'PerimeterWall': perimeter_wall, 'PerimeterGate': perimeter_gate, 'Slope': slope, 'Watchtower': watchtower, 'Ladder': ladder, 'UpperFloor': upper_floor,
    'DragonCrystal': dragon_crystal, 'WoodStockpile': wood_stockpile, 'StoneStockpile': stone_stockpile,
    'IronStockpile': iron_stockpile, 'OreStockpile': ore_stockpile,
    'Wheelbarrow': wheelbarrow, 'TamingCage': taming_cage,
}
# Props the game draws itself, with no model file
PROCEDURAL = {'Stairs'}


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def shape_faces(shape, size):
    """The faces of a shape at its size, standing on its own origin. A flat end is one face, not a fan of triangles."""
    outline, sides, start = SHAPES[shape]
    w, h, d = size
    def corner(r, y, step):
        angle = math.radians(start) + 2 * math.pi * step / sides
        return (r * math.cos(angle) * w, y * h, r * math.sin(angle) * d)
    faces = []
    for i in range(len(outline) - 1):
        (r0, y0), (r1, y1) = outline[i], outline[i + 1]
        if y0 == y1:
            faces.append([corner(max(r0, r1), y0, s) for s in range(sides)])
            continue
        for s in range(sides):
            ring = [corner(r0, y0, s), corner(r0, y0, s + 1), corner(r1, y1, s + 1), corner(r1, y1, s)]
            face = [p for k, p in enumerate(ring) if p != ring[k - 1]]   # at the axis two corners are the same point
            if len(face) >= 3:
                faces.append(face)
    return faces


def outward(face, middle):
    """The face's normal, pointing away from the shape's middle. The face's points are turned around if they ran the wrong way."""
    n = cross(sub(face[1], face[0]), sub(face[2], face[0]))
    length = math.sqrt(dot(n, n)) or 1.0
    n = (n[0] / length, n[1] / length, n[2] / length)
    center = [sum(p[k] for p in face) / len(face) for k in range(3)]
    if dot(n, sub(center, middle)) < 0:
        face.reverse()
        n = (-n[0], -n[1], -n[2])
    return n


def turn(p, yaw, tilt):
    """Leans a point over by tilt, then turns it around up by yaw (both in degrees)."""
    ct, st = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))
    x, y = p[0] * ct - p[1] * st, p[0] * st + p[1] * ct
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (x * cy + p[2] * sy, y, -x * sy + p[2] * cy)


def shade_u(n, y):
    """Where on the texture's dark to light strip a corner falls, by which way its face looks and how near the ground it is."""
    light = 0.5 * (n[1] + 1) * 0.65 + 0.35 * max(0.0, n[0] * 0.6 + n[2] * 0.8)
    light -= FADE_AMOUNT * max(0.0, 1.0 - y / FADE_HEIGHT)
    return (max(0.0, min(1.0, light)) * (SHADES - 1) + 0.5) / SHADES


def shade_texture(colors):
    """One row per color, each running from dark to light, as a PNG."""
    image = Image.new('RGB', (SHADES, len(colors)))
    for row, color in enumerate(colors):
        for i in range(SHADES):
            k = 0.5 + 0.65 * i / (SHADES - 1)
            image.putpixel((i, row), tuple(min(255, int(c * k)) for c in color))
    out = io.BytesIO()
    image.save(out, 'PNG')
    return out.getvalue()


def write_glb(path, parts):
    colors = sorted({p[3] for p in parts})
    pos, nrm, uv, idx = [], [], [], []
    for shape, place, size, color, yaw, tilt in parts:
        v = (colors.index(color) + 0.5) / len(colors)
        for face in shape_faces(shape, size):
            outward(face, (0, size[1] / 2, 0))
            face = [turn(p, yaw, tilt) for p in face]
            face = [(p[0] + place[0], p[1] + place[1], p[2] + place[2]) for p in face]
            n = cross(sub(face[1], face[0]), sub(face[2], face[0]))
            length = math.sqrt(dot(n, n)) or 1.0
            n = (n[0] / length, n[1] / length, n[2] / length)
            base = len(pos) // 3
            for p in face:
                pos += p
                nrm += n
                uv += (shade_u(n, p[1]), v)
            for k in range(1, len(face) - 1):
                idx += (base, base + k, base + k + 1)
    count = len(pos) // 3
    xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
    blobs = [struct.pack(f'<{len(pos)}f', *pos), struct.pack(f'<{len(nrm)}f', *nrm), struct.pack(f'<{len(uv)}f', *uv),
             struct.pack(f'<{len(idx)}H', *idx), shade_texture(colors)]
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
    """Writes the file data/props.txt names for each prop. A prop missing from either one stops the script.
    With prop keys after the script's name (python tools/make_models.py Airship), only those are written."""
    files = {}
    model_column = None
    with open(os.path.join(DATA, 'props.txt'), encoding='utf-8') as f:
        for line in f:
            fields = line.rstrip('\r\n').split('\t')
            if line.startswith('#') or len(fields) < 2:
                continue
            if model_column is None:
                model_column = fields.index('model')
            else:
                files[fields[0]] = fields[model_column]
    for key in PROCEDURAL:
        files.pop(key, None)
    if set(files) != set(BUILDERS):
        raise SystemExit('BUILDERS and data/props.txt differ: ' + ', '.join(sorted(set(files) ^ set(BUILDERS))))
    only = set(sys.argv[1:])
    for key, file in files.items():
        if only and key not in only:
            continue
        parts = BUILDERS[key]()
        tris = write_glb(os.path.join(DATA, 'model', file), parts)
        print(f'{key:16s} {file:14s} {len(parts):3d} parts  {tris:4d} triangles')
        stem = file[:-4] if file.endswith('.glb') else file
        extra = {}
        shape_builders = {'WoodWall': wall_tile, 'UpperFloor': lambda a, p: floor_tile(a, p)}
        if key in SHAPED:
            for suffix, arms in WALL_SHAPES.items():
                extra[suffix] = (lambda f, a: lambda: f(a, WOOD_PAL))(shape_builders[key], arms)
        if key in MATERIAL_KEYS:
            for mat, pal in MATERIALS.items():
                if key == 'WoodWall':
                    style, spal = WALL_STYLES[mat]
                    extra[mat] = (lambda f, pl: lambda: f([], pl))(style, spal)
                    for suffix, arms in WALL_SHAPES.items():
                        extra[mat + suffix] = (lambda f, a, pl: lambda: f(a, pl))(style, arms, spal)
                elif key == 'UpperFloor':
                    f = shape_builders[key]
                    extra[mat] = (lambda f, pl: lambda: f([], pl))(f, pal)
                    for suffix, arms in WALL_SHAPES.items():
                        extra[mat + suffix] = (lambda f, a, pl: lambda: f(a, pl))(f, arms, pal)
                elif key == 'WoodDoor':
                    extra[mat] = (lambda f, pl: lambda: f(pl))(DOOR_STYLES[mat], pal)
                else:
                    extra[mat] = (lambda f, pl: lambda: f(pl))(BUILDERS[key], pal)
        # A material's file takes the wood one's name with its own material: wall_wood_end.glb, wall_stone_end.glb
        base = stem[:-5] if stem.endswith('_wood') else stem
        for suffix, build in extra.items():
            shaped = build()
            name = (base if suffix.split('_')[1] in ('stone', 'metal', 'crystal') else stem) + suffix + '.glb'
            tris = write_glb(os.path.join(DATA, 'model', name), shaped)
            print(f'{key + suffix:22s} {name:26s} {len(shaped):3d} parts  {tris:4d} triangles')
    if not only or 'PerimeterWall' in only:
        # Every Perimeter Wall tile, facing north, for each set of sides that have another piece beside them (bits north 1, east 2, south 4,
        # west 8): perimeter_wall_wood_f5.glb, perimeter_wall_metal_f12.glb and so on
        for mat, pal in [('_wood', WOOD_PAL)] + list(MATERIALS.items()):
            for mask in range(16):
                arms = [a for bit, a in enumerate(OPEN_FACE) if (mask >> bit) & 1]
                parts = perimeter_wall(pal, arms)
                name = f'perimeter_wall{mat}_f{mask}.glb'
                write_glb(os.path.join(DATA, 'model', name), parts)
            print(f'Perimeter Wall{mat}: 16 facing models')
    if not only or 'Watchtower' in only:
        parts = watchtower(WOOD_PAL, True)
        tris = write_glb(os.path.join(DATA, 'model', 'watchtower_wood_manned.glb'), parts)
        print(f'{"Watchtower manned":22s} watchtower_wood_manned.glb {len(parts):3d} parts  {tris:4d} triangles')
    if not only or 'PerimeterGate' in only:
        # The Perimeter Gate open, beside its closed model in each material
        for mat, pal in [('_wood', WOOD_PAL)] + list(MATERIALS.items()):
            parts = perimeter_gate_open(pal)
            name = 'perimeter_gate' + mat + '_open.glb'
            tris = write_glb(os.path.join(DATA, 'model', name), parts)
            print(f'{"Gate open" + mat:22s} {name:26s} {len(parts):3d} parts  {tris:4d} triangles')
    if not only or 'Stairs' in only:
        # Stairs are drawn by the game, a step at a time, each this model stretched to its height: stair_step.glb and
        # one beside it in each material
        for mat, pal in [('_wood', WOOD_PAL)] + list(MATERIALS.items()):
            parts = stair_step(pal)
            name = 'stair_step' + mat + '.glb'
            tris = write_glb(os.path.join(DATA, 'model', name), parts)
            print(f'{"Stairs step" + mat:22s} {name:26s} {len(parts):3d} parts  {tris:4d} triangles')


if __name__ == '__main__':
    main()
