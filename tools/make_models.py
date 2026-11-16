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
  - A prop's fire (and any other particle origin, such as smoke) is a spot in data/props.txt (the fire: flag), measured on its model. When a
    model is made or replaced, update that origin as well, and record beside the row how it was found.
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
    # The Altar's: tapering square posts (top width over bottom width: .77 and .65), an eight-sided pedestal that widens upward, and the
    # bowl's two shells, which have no top so that it is open
    # The airship's balloon: a capsule standing on end (it is turned onto its side where it is used), eight-sided
    'blimp': ([(0, 0), (0.26, 0.015), (0.4, 0.06), (0.48, 0.14), (0.5, 0.24), (0.5, 0.76), (0.48, 0.86), (0.4, 0.94), (0.26, 0.985), (0, 1)], 8, 22.5),
    'taper_base': ([(0, 0), (SQUARE, 0), (SQUARE * 0.77, 1), (0, 1)], 4, 45),
    'taper_post': ([(0, 0), (SQUARE, 0), (SQUARE * 0.65, 1), (0, 1)], 4, 45),
    'pedestal': ([(0, 0), (0.5, 0), (0.675, 1), (0, 1)], 8, 22.5),
    'bowl_outer': ([(0, 0), (0.151, 0), (0.5, 1)], 8, 22.5),
    'bowl_inner': ([(0, 0), (0.297, 0), (0.5, 1)], 8, 22.5),
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
            cyl(x, radius, z + 0.01, radius * 0.6, 0.06, radius * 0.6, IRON, 90, 90)]


def barrel_parts(x, y, z, width, height, color=WOOD):
    return [cyl(x, y, z, width, height, width, color),
            cyl(x, y + height * 0.15, z, width * 1.08, height * 0.07, width * 1.08, IRON),
            cyl(x, y + height * 0.78, z, width * 1.08, height * 0.07, width * 1.08, IRON)]


def flame(x, y, z, width, height):
    return [cone(x, y, z, width, height, width, FLAME), cone(x, y, z, width * 1.2, height * 0.5, width * 1.2, FLAME_CORE)]


def wagon():
    parts = [box(-0.1, 0.3, 0, 1.1, 0.08, 0.66, WOOD), rod((0.42, 0.32, 0), (0.75, 0.12, 0), 0.05, DARK_WOOD)]
    for z in (-0.3, 0.3):
        parts.append(box(-0.1, 0.38, z, 1.1, 0.22, 0.05, PALE_WOOD))
    for x in (-0.65, 0.45):
        parts.append(box(x, 0.38, 0, 0.05, 0.23, 0.68, PALE_WOOD))   # a hair taller than the side rails, so their tops don't share a plane
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
    return legs(0.65, 0.36, 0.19, 0.08, DARK_WOOD) + [
        box(0, 0.12, 0, 1.3, 0.08, 0.78, DARK_WOOD), box(0, 0.2, 0, 1.26, 0.09, 0.72, LINEN), box(0.17, 0.26, 0, 0.9, 0.05, 0.76, BLUE),
        ball(-0.45, 0.28, 0, 0.24, 0.06, 0.5, LINEN), box(-0.67, 0, 0, 0.06, 0.6, 0.82, DARK_WOOD), box(0.67, 0, 0, 0.06, 0.38, 0.82, DARK_WOOD)]


def barrel():
    return barrel_parts(0, 0, 0, 0.55, 0.7) + [cyl(0, 0.7, 0, 0.47, 0.012, 0.47, DARK_WOOD)]


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


def bone(a, b, shaft, color):
    """One chunky bone from a to b: a shaft with a pair of knobs at each end, side by side as on a real one."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    px, pz = -dz / (math.hypot(dx, dz) or 1.0), dx / (math.hypot(dx, dz) or 1.0)   # sideways, across the ground
    parts = [rod(a, b, shaft, color, 'cylinder')]
    for end, sign in ((a, 1), (b, -1)):
        along = shaft * 0.5 * sign
        for side in (-1, 1):
            k = 0.07 * side
            parts.append(ball(end[0] + dx / length * along + px * k, end[1] - 0.045 + (0.012 if side > 0 else 0), end[2] + dz / length * along + pz * k, 0.125, 0.11, 0.125, color))
    return parts


def bones():
    """A heap of chunky, knobbed bones piled up to about a third of a tile."""
    pale, tan = BONE, (212, 192, 158)
    heap = [((-0.55, 0.08, 0.10), (0.50, 0.10, -0.05), pale), ((-0.50, 0.07, -0.25), (0.45, 0.09, 0.30), tan),
            ((-0.30, 0.10, 0.35), (0.30, 0.12, -0.30), pale), ((-0.45, 0.13, 0.05), (0.20, 0.30, -0.20), tan),
            ((0.40, 0.13, 0.30), (-0.15, 0.28, -0.10), pale), ((-0.35, 0.21, -0.20), (0.45, 0.23, 0.15), tan),
            ((0.00, 0.21, 0.40), (0.05, 0.25, -0.40), pale), ((-0.40, 0.29, 0.15), (0.35, 0.35, -0.05), pale),
            ((0.30, 0.31, -0.30), (-0.25, 0.33, 0.28), tan), ((0.10, 0.37, 0.15), (-0.30, 0.39, -0.10), pale)]
    parts = []
    for i, (a, b, color) in enumerate(heap):
        parts += bone(a, b, 0.075 + 0.004 * (i % 3), color)
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
    """A strongbox: a reddish wood body with a domed lid, all banded in gold, gold corner posts with pyramid feet, and a padlock hanging
    from a hasp. Its front, where the lock is, faces +z."""
    WOOD_RED = (150, 72, 48)
    GOLD = (232, 170, 60)
    GOLD_DARK = (190, 130, 40)
    GOLD_LIGHT = (250, 196, 80)
    parts = [box(0, 0, 0, 0.96, 0.05, 0.66, GOLD_DARK),                                  # the gold foot round the base
             box(0, 0.04, 0, 0.88, 0.31, 0.56, WOOD_RED)]                                # body, y .04 to .35
    for y in (0.14, 0.24):                                                                # plank seams, front and back
        parts += [box(0, y, 0.285, 0.88, 0.012, 0.02, (98, 48, 34)), box(0, y, -0.285, 0.88, 0.012, 0.02, (98, 48, 34))]
    parts.append(box(0, 0.3, 0, 0.95, 0.07, 0.62, GOLD))                                  # the gold band round the body's top, y .30 to .37
    # The lid is an eight-sided cylinder lying along x, half sunk in the body, a little longer than it (0.46 against 0.44) so its ends are
    # not flush with the body's. Gold rings round it sit a hair wider than the lid, and stop short of its ends.
    parts.append(cyl(0.46, 0.36, 0, 0.56, 0.92, 0.56, WOOD_RED, 0, 90))
    for x in (-0.3, 0.3):
        parts.append(cyl(x + 0.035, 0.36, 0, 0.62, 0.07, 0.62, GOLD, 0, 90))
    for x in (-0.4, 0.4):
        parts.append(cyl(x + 0.03, 0.36, 0, 0.6, 0.06, 0.6, GOLD_LIGHT, 0, 90))
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.43, 0.04, sz * 0.27, 0.08, 0.3, 0.08, GOLD))              # corner posts
            parts.append(pyramid(sx * 0.43, -0.01, sz * 0.27, 0.12, 0.07, 0.12, GOLD_LIGHT, 0, 180))   # pyramid feet, point down... a hair under
    parts += [box(0, 0.2, 0.285, 0.14, 0.2, 0.03, GOLD_DARK),                            # hasp plate on the front
              box(0, 0.36, 0.25, 0.12, 0.13, 0.06, GOLD),                                  # the hasp's tab over the lid
              box(0, 0.09, 0.3, 0.15, 0.17, 0.06, GOLD_LIGHT),                             # padlock body
              box(-0.04, 0.26, 0.3, 0.025, 0.07, 0.03, GOLD), box(0.04, 0.26, 0.3, 0.025, 0.07, 0.03, GOLD), box(0, 0.32, 0.3, 0.105, 0.025, 0.03, GOLD),   # its shackle
              box(0, 0.13, 0.36, 0.028, 0.065, 0.02, (60, 36, 20))]                        # keyhole
    return parts


def prisoner_cage():
    s = 0.6
    parts = legs(s, s, 1.8, 0.08, IRON) + [box(0, 0, 0, s * 2 + 0.12, 0.1, s * 2 + 0.12, DARK_WOOD), cyl(0, 0.1, 0, 0.9, 0.01, 0.9, STRAW)]
    for sz in (-1, 1):
        parts.append(box(0, 1.72, sz * s, s * 2, 0.06, 0.06, IRON))
        parts.append(box(sz * s, 1.72, 0, 0.06, 0.07, s * 2, IRON))   # a hair taller, so the rails' tops don't share a plane at the corners
    for t in (-0.36, -0.12, 0.12, 0.36):
        for sz in (-1, 1):
            parts.append(box(t, 0.1, sz * s, 0.03, 1.62, 0.03, IRON))
            parts.append(box(sz * s, 0.1, t, 0.03, 1.62, 0.03, IRON))
    return parts + [box(0.12, 0.9, s + 0.025, 0.1, 0.1, 0.03, BRASS)]


def taming_cage():
    """A wide wooden pen with a pitched roof, a straw floor and a feeding trough at the front, for an animal being tamed."""
    s = 0.7
    parts = legs(s, s, 1.5, 0.1, DARK_WOOD) + [box(0, 0, 0, s * 2 + 0.14, 0.1, s * 2 + 0.14, DARK_WOOD),
                                                 cyl(0, 0.1, 0, 1.2, 0.01, 1.2, STRAW)]
    # Rails round the sides, and slats between them
    for y in (0.45, 1.1, 1.45):
        for sz in (-1, 1):
            parts.append(box(0, y, sz * s, s * 2, 0.07, 0.06, WOOD))
            parts.append(box(sz * s, y, 0, 0.06, 0.08, s * 2, WOOD))
    for t in (-0.45, -0.15, 0.15, 0.45):
        for sz in (-1, 1):
            parts.append(box(t, 0.1, sz * s, 0.04, 1.4, 0.04, WOOD))
            parts.append(box(sz * s, 0.1, t, 0.04, 1.4, 0.04, WOOD))
    # A low pitched roof
    parts.append(box(0, 1.52, 0, s * 2 + 0.2, 0.06, s * 2 + 0.2, DARK_WOOD))
    parts.append(pyramid(0, 1.58, 0, s * 2 + 0.1, 0.22, s * 2 + 0.1, BARK))
    # The trough, outside the front rail
    parts += [box(0, 0.1, s + 0.16, 0.7, 0.18, 0.2, DARK_WOOD), box(0, 0.24, s + 0.16, 0.6, 0.05, 0.14, MEAT)]
    return parts


def chair():
    parts = legs(0.18, 0.18, 0.43, 0.04, DARK_WOOD) + [box(0, 0.43, 0, 0.44, 0.04, 0.44, WOOD), box(0, 0.47, 0, 0.36, 0.025, 0.36, RED)]
    for s in (-1, 1):
        parts.append(box(s * 0.18, 0.47, -0.18, 0.04, 0.42, 0.04, DARK_WOOD))
    return parts + [box(0, 0.62, -0.18, 0.36, 0.05, 0.03, WOOD), box(0, 0.78, -0.18, 0.44, 0.12, 0.06, WOOD)]


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
        box(0, 0.72, 0, 1.4, 0.06, 1.4, WOOD), box(0, 0.78, 0, 0.9, 0.012, 0.7, CANVAS, 8), box(0.1, 0.792, -0.05, 0.3, 0.012, 0.02, RED, 30),
        box(-0.25, 0.792, 0.05, 0.02, 0.012, 0.3, BLUE, 20), box(0.5, 0.78, 0.4, 0.14, 0.012, 0.2, LINEN, 20), box(0.52, 0.792, 0.42, 0.14, 0.012, 0.2, LINEN, -10),
        box(-0.5, 0.78, -0.4, 0.07, 0.07, 0.07, LINEN, 25), box(-0.38, 0.78, -0.32, 0.07, 0.07, 0.07, RED, 50)]


def end_table():
    return legs(0.19, 0.19, 0.42, 0.045, DARK_WOOD) + [
        box(0, 0.42, 0, 0.42, 0.17, 0.42, DARK_WOOD), box(0, 0.45, 0.215, 0.32, 0.11, 0.02, WOOD), box(0, 0.49, 0.23, 0.04, 0.03, 0.03, BRASS),
        box(0, 0.59, 0, 0.47, 0.035, 0.47, WOOD), cyl(0.05, 0.625, -0.04, 0.05, 0.05, 0.05, LINEN), cone(0.05, 0.675, -0.04, 0.03, 0.025, 0.03, FLAME)]


def round_table():
    return [box(0, 0, 0, 0.75, 0.05, 0.12, DARK_WOOD), box(0, 0, 0, 0.12, 0.06, 0.75, DARK_WOOD), cyl(0, 0.05, 0, 0.14, 0.65, 0.14, DARK_WOOD),
            cyl(0, 0.7, 0, 1.2, 0.05, 1.2, WOOD), cyl(0.2, 0.75, 0.15, 0.2, 0.03, 0.2, IRON), cyl(-0.22, 0.75, -0.1, 0.08, 0.1, 0.08, PALE_WOOD)]


def research_table():
    """A wooden drafting table: a tilted board with a blueprint, loose paper and a pencil on it, and a stool tucked under."""
    paper, blue, ink = (236, 230, 212), (74, 112, 176), (220, 232, 250)
    # Parts tilt about their own origins, so a sheet laid on the board has to be raised by how far the board's slope carries it at its
    # x: lay() puts a sheet's base `base` above the board's top, measured straight out from the board, so a sheet never ends up in
    # the board's own plane (or under it)
    nx, ny = math.sin(math.radians(14)), math.cos(math.radians(14))
    plane = ny * 0.75 + 0.04   # the board's top, along its normal, for a board whose origin is at x = 0

    def lay(x, z, w, d, color, base, thick):
        return box(x, (plane + base - nx * x) / ny, z, w, thick, d, color, 0, -14)
    parts = legs(0.6, 0.3, 0.7, 0.07, DARK_WOOD) + [
        box(0, 0.32, 0, 1.2, 0.05, 0.56, DARK_WOOD), box(0, 0.7, 0, 1.4, 0.05, 0.74, WOOD),
        # The board, tilted up at the back
        box(0, 0.75, -0.06, 1.26, 0.04, 0.6, PALE_WOOD, 0, -14),
        lay(-0.1, -0.05, 0.78, 0.46, blue, -0.004, 0.014),   # its top 0.01 over the board's
        lay(-0.32, -0.1, 0.24, 0.02, ink, 0.006, 0.016), lay(-0.1, 0.02, 0.4, 0.02, ink, 0.006, 0.016),   # the lines, 0.012 over the blueprint
        lay(0.08, -0.14, 0.02, 0.26, ink, 0.006, 0.016), lay(-0.24, 0.1, 0.02, 0.14, ink, 0.006, 0.016),
        # Loose paper, a pencil and a ruler
        box(0.46, 0.79, 0.02, 0.26, 0.012, 0.34, paper, 12, -14), box(0.5, 0.8, 0.08, 0.22, 0.01, 0.3, paper, -8, -14),
        box(0.3, 0.79, 0.3, 0.3, 0.02, 0.025, (222, 178, 60), 20, 0), box(0.44, 0.79, 0.26, 0.04, 0.03, 0.025, (60, 50, 50), 20, 0),
        box(-0.2, 0.78, 0.31, 0.5, 0.012, 0.05, BRASS), cyl(0.56, 0.75, -0.26, 0.09, 0.1, 0.09, IRON)]
    return parts


def workbench(top_color):
    """A stout bench with a shelf under it, shared by the three workbenches."""
    return legs(0.62, 0.3, 0.68, 0.09, DARK_WOOD) + [
        box(0, 0.68, 0, 1.45, 0.08, 0.74, top_color), box(0, 0.22, 0, 1.3, 0.04, 0.6, WOOD),
        box(0, 0.5, -0.365, 1.41, 0.4, 0.03, DARK_WOOD)]


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
        cyl(0.48, 0.76, -0.1, 0.2, 0.42, 0.2, (120, 84, 56)), box(0.48, 0.76, 0.0, 0.03, 0.4, 0.02, DARK_WOOD),
        cyl(-0.45, 0.76, 0.12, 0.26, 0.06, 0.26, DARK_STONE), cone(-0.45, 0.82, 0.12, 0.2, 0.08, 0.2, STEEL)]
    for i in range(5):
        parts.append(box(-0.05, 0.765, -0.16 + 0.07 * i, 0.62, 0.02, 0.02, PALE_WOOD, 4 * (i - 2)))
        parts.append(box(-0.33, 0.765, -0.16 + 0.07 * i, 0.08, 0.03, 0.035, STEEL, 4 * (i - 2)))
        parts.append(box(0.2, 0.775, -0.16 + 0.07 * i, 0.1, 0.03, 0.035, (228, 228, 236), 4 * (i - 2)))
    for dx in (-0.05, 0.0, 0.05):
        parts.append(box(0.48 + dx, 1.18, -0.1, 0.02, 0.16, 0.02, PALE_WOOD))
        parts.append(box(0.48 + dx, 1.3, -0.1, 0.05, 0.08, 0.03, RED))
    return parts


def standing_torch():
    parts = [cyl(0, 0, 0, 0.06, 1.1, 0.06, DARK_WOOD), cyl(0, 1.05, 0, 0.2, 0.09, 0.2, IRON)]
    for i in range(3):
        t = 2 * math.pi * i / 3
        parts.append(rod((0.28 * math.cos(t), 0, 0.28 * math.sin(t)), (0, 0.3, 0), 0.035, DARK_WOOD))
    return parts + [cyl(0, 1.14, 0, 0.14, 0.05, 0.14, (58, 44, 40))]   # the game draws its fire


def wall_torch():
    """A torch in an iron bracket that hangs on the side of a wall: a strap against the wall (the back of the model, z 0), an arm out from it
    and a cup holding a wooden torch whose head is for the game's fire. It is 0.5 tall and reaches 0.31 out, so it is drawn at its own size.
    Nothing shares a plane: the arm starts inside the strap, the torch inside the cup and the head wider than the torch."""
    return [box(0, 0, 0.02, 0.12, 0.5, 0.04, IRON),                                    # the strap on the wall
            box(0, 0.14, 0.14, 0.06, 0.05, 0.22, IRON),                                # the arm
            cyl(0, 0.19, 0.25, 0.12, 0.05, 0.12, IRON),                                # the cup
            cyl(0, 0.22, 0.25, 0.05, 0.22, 0.05, DARK_WOOD),                           # the torch
            cyl(0, 0.40, 0.25, 0.09, 0.06, 0.09, (58, 44, 40))]                        # its head, where the game draws the fire


# Wall decorations hang on the side of a wall: the back of each model is its z 0, and the game puts that against the wall's face. Each is
# drawn at its own size (props.txt's size is its largest side) and says how far out it reaches in its wall_mount flag. As with the Wall
# Torch, no two faces share a plane: every front part stands a little out of the one behind it.
def wall_painting():
    """A painting in a wooden frame: a landscape of sky, hills and a sun, on a canvas a little proud of the frame."""
    return [box(0, 0, 0.025, 0.7, 0.5, 0.05, DARK_WOOD),                                # the frame
            box(0, 0.05, 0.045, 0.6, 0.2, 0.02, (120, 170, 220)),                       # the sky's lower half...
            box(0, 0.25, 0.045, 0.6, 0.2, 0.02, (150, 196, 236)),                       # ...and its upper
            pyramid(-0.12, 0.05, 0.056, 0.34, 0.2, 0.012, (86, 140, 80)),               # a green hill
            pyramid(0.14, 0.05, 0.057, 0.3, 0.14, 0.012, (70, 112, 70)),                # one behind it
            box(0.2, 0.33, 0.058, 0.07, 0.07, 0.012, (250, 220, 90))]                   # the sun


def wall_banner():
    """A banner: a red cloth with a gold band and a gold emblem, hung from a wooden rod by two cords."""
    return [box(0, 0.85, 0.03, 0.5, 0.04, 0.04, DARK_WOOD),                             # the rod
            box(0, 0.1, 0.02, 0.4, 0.76, 0.015, RED),                                   # the cloth
            box(0, 0.7, 0.032, 0.4, 0.05, 0.012, BRASS),                                # the band across its top
            box(0, 0.38, 0.032, 0.12, 0.12, 0.012, BRASS),                              # the emblem
            box(-0.18, 0.89, 0.03, 0.03, 0.04, 0.03, BRASS), box(0.18, 0.89, 0.03, 0.03, 0.04, 0.03, BRASS)]   # the rod's ends


def wall_antlers():
    """A trophy: a bleached skull on a wooden plaque with a branching antler on each side."""
    parts = [box(0, 0, 0.02, 0.34, 0.36, 0.04, DARK_WOOD),                              # the plaque
             box(0, 0.08, 0.075, 0.14, 0.2, 0.07, BONE),                                # the skull
             box(-0.04, 0.17, 0.115, 0.035, 0.035, 0.01, (40, 30, 28)),                 # its eyes
             box(0.04, 0.17, 0.115, 0.035, 0.035, 0.01, (40, 30, 28))]
    for s in (-1, 1):
        parts += [rod((s * 0.05, 0.26, 0.07), (s * 0.2, 0.6, 0.07), 0.035, BONE),      # the beam
                  rod((s * 0.12, 0.42, 0.07), (s * 0.06, 0.63, 0.07), 0.03, BONE),     # its tines
                  rod((s * 0.17, 0.52, 0.07), (s * 0.32, 0.58, 0.07), 0.03, BONE)]
    return parts


def wall_swords():
    """Two swords crossed over a brass boss on a wooden board, the second a little in front of the first."""
    return [box(0, 0, 0.015, 0.5, 0.4, 0.03, DARK_WOOD),                                # the board
            rod((-0.2, 0.02, 0.05), (-0.14, 0.1, 0.05), 0.045, BRASS),                  # the first sword's hilt...
            rod((-0.14, 0.1, 0.05), (0.2, 0.38, 0.05), 0.032, STEEL),                   # ...and its blade
            rod((0.2, 0.02, 0.075), (0.14, 0.1, 0.075), 0.045, BRASS),                  # the second's
            rod((0.14, 0.1, 0.075), (-0.2, 0.38, 0.075), 0.032, STEEL),
            cyl(0, 0.205, 0.1, 0.1, 0.03, 0.1, BRASS, 90, 90)]                          # the boss where they cross


def guard_post():
    """A guard's post: a stone footing under a tall timber post with a crossbar, a red pennant and a lantern hung from it. No two parts
    share a plane: the crossbar and the pennant's edge are clear of the post's faces and of each other."""
    return [box(0, 0, 0, 0.9, 0.08, 0.9, DARK_STONE),                                  # footing
            box(0, 0.08, 0, 0.14, 1.5, 0.14, DARK_WOOD),                               # post
            pyramid(0, 1.58, 0, 0.24, 0.14, 0.24, WOOD),                               # its cap, wider than the post
            box(0, 1.2, 0, 0.5, 0.07, 0.1, WOOD),                                      # crossbar
            box(0.17, 0.97, 0, 0.26, 0.23, 0.025, RED),                                # pennant under the crossbar's right end
            box(-0.2, 1.14, 0, 0.022, 0.06, 0.022, IRON),                              # the lantern's hook
            box(-0.2, 0.96, 0, 0.1, 0.18, 0.1, BRASS),                                 # lantern
            box(-0.2, 1.0, 0.055, 0.06, 0.1, 0.02, GLOW)]                             # its light


def support_beam():
    return [box(0, 0, 0, 0.48, 0.2, 0.48, DARK_STONE), box(0, 0.2, 0, 0.2, 2.1, 0.2, WOOD), box(0, 2.3, 0, 1.2, 0.2, 0.24, WOOD),
            rod((0.08, 1.65, 0), (0.48, 2.3, 0), 0.09, DARK_WOOD), rod((-0.08, 1.65, 0), (-0.48, 2.3, 0), 0.09, DARK_WOOD),
            box(0, 0.6, 0, 0.22, 0.05, 0.22, IRON), box(0, 1.5, 0, 0.22, 0.05, 0.22, IRON)]


def training_dummy():
    return [box(0, 0, 0, 0.55, 0.08, 0.14, DARK_WOOD), box(0, 0, 0, 0.14, 0.09, 0.55, DARK_WOOD), cyl(0, 0.08, 0, 0.08, 1.2, 0.08, WOOD),
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
    # The shelves stand 0.01 back from the posts' faces and end inside them, the back panel runs out past the posts' backs, and
    # the top shelf and panel stop under the posts' tops, so nothing is flush
    parts = [box(-0.4, 0, 0, 0.05, 1.0, 0.38, WOOD), box(0.4, 0, 0, 0.05, 1.0, 0.38, WOOD), box(0, 0, -0.185, 0.82, 0.98, 0.03, DARK_WOOD)]
    for y in (0.02, 0.35, 0.67, 0.96):
        parts.append(box(0, y, 0, 0.8, 0.03, 0.36, WOOD))
    return parts + [ball(-0.17, 0.05, 0, 0.3, 0.28, 0.26, BURLAP), cyl(0.18, 0.05, 0, 0.18, 0.25, 0.18, (172, 118, 76)),
                    cyl(-0.18, 0.38, 0, 0.26, 0.12, 0.26, (240, 205, 95)), ball(0.17, 0.38, 0.02, 0.24, 0.11, 0.14, STRAW),
                    cyl(-0.2, 0.7, 0, 0.14, 0.2, 0.14, (92, 134, 170)), ball(0.1, 0.7, 0.05, 0.1, 0.1, 0.1, RED), ball(0.24, 0.7, -0.03, 0.1, 0.1, 0.1, RED)]


def weapon_rack():
    parts = [box(-0.66, 0, 0, 0.08, 1.0, 0.08, DARK_WOOD), box(0.66, 0, 0, 0.08, 1.0, 0.08, DARK_WOOD), box(-0.66, 0, 0, 0.1, 0.08, 0.5, DARK_WOOD),
             box(0.66, 0, 0, 0.1, 0.08, 0.5, DARK_WOOD), box(0, 0.92, 0, 1.44, 0.06, 0.1, WOOD), box(0, 0.12, 0.08, 1.3, 0.04, 0.06, WOOD)]
    parts += [box(-0.44, 0.16, 0.05, 0.045, 0.62, 0.012, STEEL), box(-0.44, 0.78, 0.05, 0.15, 0.025, 0.03, BRASS), box(-0.44, 0.8, 0.05, 0.03, 0.12, 0.03, DARK_WOOD)]
    parts += [box(-0.1, 0.16, 0.05, 0.03, 0.76, 0.03, WOOD), box(-0.03, 0.72, 0.05, 0.2, 0.16, 0.01, STEEL)]
    parts += [box(0.42, 0.16, 0.05, 0.025, 0.72, 0.025, PALE_WOOD), pyramid(0.42, 0.88, 0.05, 0.07, 0.1, 0.02, STEEL)]
    return parts


def armor_stand():
    return [box(0, 0, 0, 0.56, 0.08, 0.14, DARK_WOOD), box(0, 0, 0, 0.14, 0.09, 0.56, DARK_WOOD), cyl(0, 0.08, 0, 0.06, 1.35, 0.06, WOOD),
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
            box(0, 1.21, -0.04, 0.06, 0.19, 0.06, IRON), rod((0, 1.4, 0.14), (0, 1.25, -0.5), 0.04, WOOD),
            box(0, 0, 0.48, 0.56, 0.22, 0.42, WOOD), box(0, 0.22, 0.48, 0.48, 0.012, 0.34, WATER)]


def forge():
    return [box(-0.2, 0, 0, 1.15, 0.9, 0.86, DARK_STONE), box(-0.2, 0.9, 0, 1.2, 0.07, 0.92, STONE), box(-0.2, 0.97, -0.1, 0.42, 0.63, 0.38, DARK_STONE),
            box(-0.2, 0.15, 0.43, 0.6, 0.6, 0.03, (48, 44, 44)), box(-0.2, 0.2, 0.455, 0.46, 0.2, 0.03, GLOW),
            cyl(0.6, 0, 0.2, 0.28, 0.3, 0.28, BARK), box(0.6, 0.3, 0.2, 0.2, 0.05, 0.12, IRON), box(0.6, 0.35, 0.2, 0.09, 0.07, 0.08, IRON), box(0.6, 0.42, 0.2, 0.3, 0.05, 0.1, IRON)]


ALTAR_LIGHT = (168, 146, 144)
ALTAR_MID = (128, 108, 110)
ALTAR_DARK = (98, 82, 86)
ALTAR_DEEP = (80, 66, 72)
ALTAR_GOLD = (236, 160, 72)
ALTAR_GOLD_DARK = (184, 114, 46)
ALTAR_CLOTH = (190, 190, 190)   # the banner's cloth in the model the game tints: the faction's color is laid over its shading


def spin(x, z, yaw):
    """A point turned about the up axis the way a part's yaw turns it, so a part can be moved round the altar a quarter at a time."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (x * c + z * s, -x * s + z * c)


def altar_banner_cloth(red):
    """The four banners' cloth (a swallowtail on each face of the core), in the one color, so the game can give it the faction's."""
    parts = []
    for k in range(4):
        yaw = 90 * k
        for x, y, z, w, h, d, kind in ((0, 0.23, 0.3675, 0.22, 0.2, 0.025, 'box'),
                                       (-0.055, 0.24, 0.3675, 0.11, 0.08, 0.025, 'tail'), (0.055, 0.24, 0.3675, 0.11, 0.08, 0.025, 'tail')):
            px, pz = spin(x, z, yaw)
            if kind == 'box':
                parts.append(box(px, y, pz, w, h, d, red, yaw))
            else:
                parts.append(pyramid(px, y, pz, w, h, d, red, yaw, 180))   # tilted over: its point hangs down
    return parts


def altar_frame():
    """The altar without the cloth of its banners: a stepped stone base, four posts with gold caps, the core and the bowl on its pedestal."""
    parts = [box(0, 0, 0, 1.2, 0.07, 1.2, ALTAR_LIGHT), box(0, 0.06, 0, 0.92, 0.08, 0.92, ALTAR_MID), box(0, 0.13, 0, 0.7, 0.33, 0.7, ALTAR_DARK),
             box(0, 0.45, 0, 0.8, 0.05, 0.8, ALTAR_LIGHT), part('pedestal', 0, 0.49, 0, 0.34, 0.23, 0.34, ALTAR_DEEP)]
    # Four posts at the corners, each on a stepped foot and capped in gold
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = sx * 0.44, sz * 0.44
            parts += [part('taper_base', x, 0.06, z, 0.3, 0.11, 0.3, ALTAR_LIGHT), part('taper_post', x, 0.16, z, 0.2, 0.46, 0.2, ALTAR_DARK),
                      box(x, 0.61, z, 0.2, 0.04, 0.2, ALTAR_GOLD), box(x, 0.64, z, 0.15, 0.04, 0.15, ALTAR_GOLD_DARK), pyramid(x, 0.67, z, 0.12, 0.09, 0.12, ALTAR_GOLD)]
    # Four gold struts up the pedestal to the bowl
    for k in range(4):
        c, s = math.cos(math.radians(90 * k)), math.sin(math.radians(90 * k))
        parts.append(rod((c * 0.19, 0.54, s * 0.19), (c * 0.255, 0.78, s * 0.255), 0.032, ALTAR_GOLD))
    # The bowl: an outer shell, a shallower inner one a little inside it, and a gold rim of eight beams round the top (every other one a hair
    # taller, so the corners where they cross do not share a plane)
    parts += [part('bowl_outer', 0, 0.72, 0, 0.86, 0.30, 0.86, ALTAR_DEEP), part('bowl_inner', 0, 0.86, 0, 0.74, 0.155, 0.74, (150, 128, 140))]
    apothem = 0.385
    edge = 2 * 0.43 * math.sin(math.radians(22.5)) + 0.04
    for i in range(8):
        a = math.radians(45 * i)
        lift = 0.012 * (i % 2)
        parts.append(box(apothem * math.cos(a), 0.98 + lift, apothem * math.sin(a), edge, 0.06, 0.09, ALTAR_GOLD, -(45 * i + 90)))
    # Each banner's gold board, on the core's four faces (the cloth is laid over it)
    for k in range(4):
        yaw = 90 * k
        px, pz = spin(0, 0.3525, yaw)
        parts.append(box(px, 0.16, pz, 0.3, 0.31, 0.025, ALTAR_GOLD_DARK, yaw))
    return parts


def altar_banner():
    """Just the banners' cloth, for the game to draw in the owner faction's color over the altar."""
    return altar_banner_cloth(ALTAR_CLOTH)


def altar():
    """The whole altar, banners in red, as the mockup has it. The game draws altar_frame.glb and altar_banner.glb (in the faction's color)
    for an altar in the world; this one is what the building menu and previews show. The game draws its fire at the bowl's floor."""
    return altar_frame() + altar_banner_cloth(RED)


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
            parts.append(box(sx * 0.97, 0, z, 0.04, 0.33, 0.04, DARK_WOOD))
        parts.append(box(sx * 0.97, 0.24, 0, 0.02, 0.03, 1.48, WOOD))
    for sz in (-1, 1):
        for x in (-0.49, 0.49):
            parts.append(box(x, 0, sz * 0.74, 0.04, 0.33, 0.04, DARK_WOOD))
        parts.append(box(0, 0.25, sz * 0.74, 1.94, 0.03, 0.02, WOOD))
    return parts + [box(0.7, 0.08, 0.12, 0.04, 0.9, 0.04, WOOD), box(0.7, 0.72, 0.12, 0.62, 0.03, 0.02, WOOD), cyl(0.7, 0.45, 0.12, 0.26, 0.32, 0.18, RED),
                    ball(0.7, 0.78, 0.12, 0.17, 0.18, 0.17, BURLAP), cone(0.7, 0.92, 0.12, 0.3, 0.14, 0.3, STRAW)]


def turbine():
    top = 1.45
    parts = [box(0, 0, 0, 0.8, 0.1, 0.8, DARK_STONE), box(0, top, 0, 0.3, 0.18, 0.36, WOOD), rod((0, top + 0.09, -0.2), (0, top + 0.09, 0.26), 0.05, IRON, 'cylinder'),
             cyl(0, top + 0.09, 0.24, 0.12, 0.09, 0.12, DARK_WOOD, 90, 90)]
    for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        parts.append(rod((sx * 0.32, 0.1, sz * 0.32), (sx * 0.1, top, sz * 0.1), 0.05, DARK_WOOD))
    for y, k in ((0.5, 0.25), (1.0, 0.17)):
        for s in (-1, 1):
            parts.append(box(0, y, s * k, k * 2 + 0.05, 0.03, 0.03, WOOD))
            parts.append(box(s * k, y, 0, 0.03, 0.04, k * 2 + 0.05, WOOD))
    for i in range(4):
        t = 2 * math.pi * i / 4 + 0.4
        c, s = math.cos(t), math.sin(t)
        cy = top + 0.09
        zr = 0.28 + 0.012 * (i % 2)   # the blades' arms start at one point: every other one stands a hair further out
        parts.append(rod((0, cy, zr), (0.45 * c, cy + 0.45 * s, zr), 0.03, DARK_WOOD))
        parts.append(rod((0.13 * c - 0.05 * s, cy + 0.13 * s + 0.05 * c, 0.27), (0.45 * c - 0.05 * s, cy + 0.45 * s + 0.05 * c, 0.27), 0.1, CANVAS))
    return parts


def stove():
    return legs(0.28, 0.2, 0.17, 0.05, IRON) + [
        box(0, 0.17, 0, 0.75, 0.52, 0.55, (64, 68, 78)), box(0, 0.69, 0, 0.8, 0.05, 0.6, IRON), box(-0.17, 0.27, 0.28, 0.3, 0.3, 0.02, IRON),
        box(-0.17, 0.32, 0.296, 0.22, 0.18, 0.012, GLOW), box(0.2, 0.27, 0.28, 0.26, 0.3, 0.02, IRON), box(0.2, 0.52, 0.295, 0.18, 0.02, 0.015, BRASS),
        cyl(-0.17, 0.74, 0.05, 0.24, 0.14, 0.24, IRON), cyl(-0.17, 0.88, 0.05, 0.2, 0.012, 0.2, MEAT),
        cyl(0.2, 0.74, -0.14, 0.12, 0.4, 0.12, IRON), cone(0.2, 1.14, -0.14, 0.2, 0.06, 0.2, IRON)]


def airship():
    """A copper-brown boat hung under a cream balloon on four rods, with a ship's wheel in the bow and red tail fins. The game floats it above
    the ground; y = 0 here is the bottom of its keel, and its occupants stand at 0.7 (AIRSHIP_DECK), so the deck is at 0.66. Forward is +x. The
    mooring rope ties on at about (-0.5, 0.66, 0.52), on the side wall."""
    CREAM = (250, 224, 192)
    HULL = (160, 88, 48)
    HULL_DARK = (118, 64, 38)
    HULL_LIGHT = (205, 120, 60)
    ORANGE = (232, 148, 70)
    PALE = (236, 196, 130)
    BAND = (168, 92, 50)
    FIN_RED = (206, 70, 58)
    parts = [box(0, 0, 0, 1.7, 0.16, 0.5, HULL_DARK), box(0, 0.14, 0, 2.2, 0.2, 0.8, HULL), box(0, 0.32, 0, 2.5, 0.28, 1.0, HULL),
             box(0, 0.58, 0, 2.3, 0.08, 0.84, (170, 100, 56))]                           # the deck, its top at 0.66
    for z in (-0.44, 0.44):
        parts.append(box(0, 0.6, z, 2.5, 0.22, 0.1, HULL_LIGHT))                            # the side walls to 0.82
        parts.append(box(0, 0.8, z * 1.02, 2.52, 0.06, 0.14, ORANGE))                       # and their rims
    for x in (-1.2, 1.2):
        parts.append(box(x, 0.6, 0, 0.1, 0.22, 0.9, HULL_LIGHT))                            # the end walls
        parts.append(box(x * 1.008, 0.8, 0, 0.14, 0.06, 0.92, ORANGE))
    # Blocks on the rims where the rods stand: the corners, and under the balloon's two bands
    for x in (-1.2, -0.75, 0.75, 1.2):
        for z in (-0.45, 0.45):
            parts.append(box(x, 0.84, z, 0.16, 0.14, 0.16, (240, 156, 72)))
    # The balloon, laid along x (nose forward), 3.7 long and 1.4 high; two brown bands round it
    top = 2.2
    parts.append(part('blimp', 1.85, top, 0, 1.4, 3.7, 1.55, CREAM, 0, 90))
    for x in (-0.75, 0.75):
        parts.append(cyl(x + 0.14, top, 0, 1.4 * 1.07, 0.28, 1.55 * 1.07, BAND, 0, 90))
        for z in (-0.45, 0.45):
            parts.append(box(x, 1.55, z, 0.16, 0.14, 0.16, (240, 156, 72)))                 # the block each rod ends in, under the band
            parts.append(rod((x, 0.9, z * 0.93), (x, 1.6, z), 0.045, PALE))                 # the rod up from the rim
    # Tail fins at the stern: a upright one and a flat one, each a red panel in a brown frame, on a brown collar
    parts.append(cyl(-1.62, top, 0, 0.5, 0.18, 0.5, BAND, 0, 90))
    parts.append(box(-2.12, top - 0.5, 0, 0.55, 1.0, 0.05, FIN_RED))
    parts.append(box(-2.12, top - 0.015, 0, 0.55, 0.03, 1.0, FIN_RED))
    for z, y in ((0, top - 0.52), (0, top + 0.47)):
        parts.append(box(-2.12, y, z, 0.6, 0.06, 0.07, BAND))                              # the upright fin's top and bottom bars
    parts.append(box(-2.4, top - 0.52, 0, 0.06, 1.06, 0.07, BAND))
    parts.append(box(-2.1, top - 0.03, -0.5, 0.6, 0.06, 0.07, BAND))
    parts.append(box(-2.1, top - 0.03, 0.5, 0.6, 0.06, 0.07, BAND))
    parts.append(box(-2.4, top - 0.03, 0, 0.06, 0.06, 1.08, BAND))
    # The wheel in the bow, on a post: eight spokes, a ring of eight rods, and handles past it
    wx, wy, wr = 0.88, 1.1, 0.22
    parts.append(box(wx - 0.1, 0.64, -0.1, 0.2, 0.34, 0.2, HULL_DARK))
    parts.append(cyl(wx + 0.05, wy, 0, 0.12, 0.1, 0.12, HULL_LIGHT, 0, 90))
    for i in range(8):
        a, b = math.radians(45 * i), math.radians(45 * (i + 1))
        parts.append(rod((wx, wy, 0), (wx, wy + wr * math.cos(a), wr * math.sin(a)), 0.03, HULL_LIGHT))
        parts.append(rod((wx, wy + wr * math.cos(a), wr * math.sin(a)), (wx, wy + wr * math.cos(b), wr * math.sin(b)), 0.04, HULL_LIGHT))
        parts.append(rod((wx, wy + wr * math.cos(a), wr * math.sin(a)), (wx, wy + (wr + 0.08) * math.cos(a), (wr + 0.08) * math.sin(a)), 0.035, ORANGE))
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


def wall_specks(color):
    """Four specks too small to see, which keep a wall's bounds the whole tile now that it has no footing (the game centres a model on its
    bounds, so every shape has to be centred on its tile the same way)."""
    return [box(sx * 0.499, 0, sz * 0.499, 0.002, 0.002, 0.002, color) for sx in (-1, 1) for sz in (-1, 1)]


def deck_planks(y, h, main, light, lo_x=-0.5, hi_x=0.5, lo_z=-0.5, hi_z=0.5):
    """The top pattern a foundation tile and an upper floor tile share: four planks laid round a small square in the middle, the way a
    parquet pinwheel is, with a 0.01 seam between them. The game turns a floor tile to fit what is beside it, so the pattern has to look
    the same turned by any quarter turn, which a pair of long bands does not; planks run to the tile's edge with no seam there, so
    neighbouring tiles carry on into each other. lo/hi clip it to a floor that stops short of an edge. Planks stand at the same y, with
    no plane of one in the plane of another."""
    g = 0.005
    rects = [(-0.5, 0.1 - g, -0.5, -0.1 - g, main), (0.1 + g, 0.5, -0.5, 0.1 - g, main),
             (-0.1 + g, 0.5, 0.1 + g, 0.5, main), (-0.5, -0.1 - g, -0.1 + g, 0.5, main),
             (-0.1 + g, 0.1 - g, -0.1 + g, 0.1 - g, light)]
    parts = []
    for x0, x1, z0, z1, color in rects:
        x0, x1, z0, z1 = max(x0, lo_x), min(x1, hi_x), max(z0, lo_z), min(z1, hi_z)
        if x1 - x0 > 0.01 and z1 - z0 > 0.01:
            parts.append(box((x0 + x1) / 2, y, (z0 + z1) / 2, x1 - x0, h, z1 - z0, color))
    return parts


def foundation(pal=WOOD_PAL):
    """One tile of a finished foundation, a low deck. The game lays one for each tile the foundation covers. Its top is the same pattern as an
    upper floor's (see deck_planks), so neighbouring tiles read as one deck."""
    main, dark, light, foot = pal
    return [box(0, 0, 0, 0.99, 0.14, 0.99, dark)] + deck_planks(0.14, 0.06, main, light)


def wall_tile(arms, pal=WOOD_PAL):
    """One tile of wall: a post in the middle and an arm toward each wall beside it. It has no footing (a dark strip stuck out past
    the edge of an upper floor), and stands straight on what is under it; four specks keep the bounds the whole tile."""
    main, dark, light, foot = pal
    # No two faces share a plane, so none flicker: the post runs up into the cap, an arm runs from inside the post to the tile's edge
    # (meeting the next wall's arm there, not overlapping it) and up to 0.01 under the floor above, so no gap is left between them, and
    # each band stops 0.01 short of its arm's end
    cap = WALL_HEIGHT - 0.1
    parts = wall_specks(dark) + [box(0, 0, 0, 0.38, cap + 0.05, 0.38, dark)]
    for dx, dz in arms:
        cx, cz = dx * 0.335, dz * 0.335
        w, d = (0.33, 0.3) if dx else (0.3, 0.33)
        parts.append(box(cx, 0, cz, w, WALL_HEIGHT - 0.01, d, main))
        bw, bd = (w - 0.02, d + 0.02) if dx else (w + 0.02, d - 0.02)
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
    parts = wall_specks(dark) + [box(0, 0, 0, 0.5, 0.27, 0.5, dark),
                                 box(0, 0.27, 0, 0.44, cap - 0.27, 0.44, dark), box(0, cap, 0, 0.52, 0.12, 0.52, light)]
    courses = 8
    top = WALL_HEIGHT - 0.01   # the coping's top, 0.01 under the floor above
    ch = (top - 0.05) / courses
    for dx, dz in arms:
        # The mortar core runs up into the coping (a hair under its top), or the two tops share a plane and flicker
        parts.append(arm_box(dx, dz, 0.33, 0.30, 0, top - 0.04, dark))   # the mortar core
        for i in range(courses):
            # Alternate courses stand a hair further out, so no two block faces share a plane
            proud = 0.34 if i % 2 else 0.335
            parts.append(arm_box(dx, dz, 0.326, proud, i * ch + 0.008, ch - 0.016, main if i % 2 else light))
        parts.append(arm_box(dx, dz, 0.33, 0.36, top - 0.05, 0.05, light))   # coping along the top
    return parts


def metal_wall_tile(arms, pal=IRON_PAL):
    """A tile of metal wall: a square column with a flared foot and head, and arms of thin plate between a rail at the
    bottom and top, with an upright rib and rivets along each rail."""
    main, dark, light, foot = pal
    cap = WALL_HEIGHT - 0.1
    top = WALL_HEIGHT - 0.01   # the top rail's top, 0.01 under the floor above
    parts = wall_specks(dark) + [box(0, 0, 0, 0.42, 0.19, 0.42, dark),
                                 box(0, 0.19, 0, 0.3, cap - 0.19, 0.3, dark), box(0, cap, 0, 0.42, 0.1, 0.42, light)]
    for dx, dz in arms:
        parts.append(arm_box(dx, dz, 0.33, 0.14, 0, top - 0.03, main))             # the plate, running up behind the top rail
        parts.append(arm_box(dx, dz, 0.326, 0.22, 0, 0.17, dark))                  # bottom rail
        parts.append(arm_box(dx, dz, 0.326, 0.22, top - 0.12, 0.12, dark))         # top rail
        parts.append(arm_box(dx, dz, 0.05, 0.19, 0.17, cap - 0.33, light, 0.36))  # upright rib
        for along in (0.24, 0.44):
            for y in (0.07, top - 0.08):
                parts.append(arm_box(dx, dz, 0.035, 0.26, y, 0.04, light, along))  # rivets, standing out a little from the rails
    return parts


def crystal_wall_tile(arms, pal=CRYSTAL_PAL):
    """A tile of crystal wall: a tall crystal stood on its corner with a point on top, and arms of crystal slab with a
    darker band at the foot, small points along the top and shards leaning at the foot."""
    main, dark, light, foot = pal
    point = 0.34
    parts = wall_specks(dark) + [box(0, 0, 0, 0.34, WALL_HEIGHT - point, 0.34, dark, 45),
                                 pyramid(0, WALL_HEIGHT - point, 0, 0.34, point, 0.34, light, 45)]
    top = WALL_HEIGHT - 0.3
    for dx, dz in arms:
        parts.append(arm_box(dx, dz, 0.33, 0.18, 0, top, main))
        parts.append(arm_box(dx, dz, 0.326, 0.2, 0, 0.35, dark))
        for along in (0.24, 0.4):
            parts.append(pyramid(dx * along, top, dz * along, 0.12, 0.2, 0.12, light, 45))
            sx, sz = (0, 0.13) if dx else (0.13, 0)
            for sgn in (-1, 1):
                parts.append(pyramid(dx * along + sgn * sx, 0, dz * along + sgn * sz, 0.08, 0.32, 0.08, light, 45))
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
    # No footing (it stuck out past an upper floor's edge). The jambs stop a hair under the lintel's top, which runs 0.01 past their sides
    # and a little deeper, so nothing is flush; the cross rails reach 0.02 past the planks and end inside the jambs
    parts = []
    for x in (-0.44, 0.44):
        parts.append(box(x, 0, 0, 0.12, WALL_HEIGHT - 0.01, 0.36, dark))
    parts.append(box(0, WALL_HEIGHT - 0.22, 0, 1.02, 0.22, 0.38, dark))
    for x in (-0.28, -0.09, 0.09, 0.28):
        parts.append(box(x, 0, 0, 0.18, WALL_HEIGHT - 0.25, 0.08, main if x < 0 or x > 0.1 else light))
    parts += [box(0, 0.6, 0.05, 0.78, 0.08, 0.04, dark), box(0, 1.6, 0.05, 0.78, 0.08, 0.04, dark),
              box(0.26, 1.05, 0.08, 0.08, 0.08, 0.06, IRON)]
    return parts


def stone_door(pal=STONE_PAL):
    """A heavy slab door between thick stone jambs under a lintel, bound with two iron straps."""
    main, dark, light, foot = pal
    parts = []
    for x in (-0.42, 0.42):
        parts.append(box(x, 0, 0, 0.16, WALL_HEIGHT, 0.38, dark))
    parts.append(box(0, WALL_HEIGHT - 0.32, 0, 0.7, 0.31, 0.36, light))    # the lintel, ending inside the jambs, a hair under their top
    parts.append(box(0, 0, 0, 0.66, WALL_HEIGHT - 0.38, 0.12, main))       # the slab
    for y in (0.55, 1.55):
        parts.append(box(0, y, 0, 0.7, 0.08, 0.15, IRON))
    parts.append(box(0.22, 1.05, 0.07, 0.07, 0.12, 0.05, IRON))           # a pull
    return parts


def metal_door(pal=IRON_PAL):
    """A riveted plate door in a slim metal frame, with level ribs and a long bar handle."""
    main, dark, light, foot = pal
    parts = []
    for x in (-0.45, 0.45):
        parts.append(box(x, 0, 0, 0.1, WALL_HEIGHT, 0.34, dark))
    parts.append(box(0, WALL_HEIGHT - 0.16, 0, 0.8, 0.16, 0.32, dark))
    parts.append(box(0, 0, 0, 0.78, WALL_HEIGHT - 0.22, 0.07, main))
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
    parts = []
    for x in (-0.4, 0.4):
        parts.append(box(x, 0, 0, 0.16, WALL_HEIGHT - point, 0.16, dark, 45))
        parts.append(pyramid(x, WALL_HEIGHT - point, 0, 0.16, point, 0.16, light, 45))
    parts.append(box(0, WALL_HEIGHT - 0.6, 0, 0.62, 0.14, 0.18, light))   # the head between the pillars
    parts.append(box(0, 0, 0, 0.6, WALL_HEIGHT - 0.66, 0.1, main))
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
    spiked_at = set()   # where a spiked log already stands: two ranks meeting at a corner share its log
    for sx in (-1, 1):
        for sz in (-1, 1):
            parts.append(box(sx * 0.999, 0, sz * 0.999, 0.002, 0.002, 0.002, dark))
    parts.append(box(0.999, 3.9 - 0.002, 0.999, 0.002, 0.002, 0.002, dark))
    n, step = 5, 0.4
    for i in range(n):
        for j in range(n):
            # Alternating by (i + j), so every outer face (north, east, south, west) shows the same pattern: the old pick depended on the row
            # alone, which left the east and west faces in dark stripes and the north and south faces one flat colour at corners
            parts.append(cyl((i - 2) * step, 0, (j - 2) * step, 0.38, DECK_BODY, 0.38, main if (i + j) % 2 else light))
    parts.append(box(0, DECK_BODY, 0, 2.0, 0.08, 2.0, light))
    for d in spiked:
        for k in range(n):
            u = (k - 2) * step
            x, z = (d[0] * 0.8 + (u if d[1] else 0), d[1] * 0.8 + (u if d[0] else 0))
            if (round(x, 3), round(z, 3)) in spiked_at:
                continue
            spiked_at.add((round(x, 3), round(z, 3)))
            h = 3.45 + 0.12 * ((k * 5 + (d[0] + 2 * d[1])) % 4)
            # 0.4 across, a little wider than the 0.38 body log under it, so their sides never share a surface
            parts.append(cyl(x, DECK_BODY, z, 0.4, h - 0.5 - DECK_BODY, 0.4, main if k % 2 else light))
            parts.append(cone(x, h - 0.5, z, 0.4, 0.5, 0.4, dark))
        if d[0]:
            parts.append(box(d[0] * 0.99, 3.0, 0, 0.05, 0.12, 2.0, ROPE))
        else:
            parts.append(box(0, 3.0, d[1] * 0.99, 2.0, 0.13, 0.05, ROPE))   # a hair taller than the ropes along z, so their tops don't share a plane at the corners
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
                parts.append(box(x + px, 0, pz, 0.24, DECK_BODY + 0.02, 0.24, dark))   # a little proud of the tower's sides, and up into the deck (past the tower's top and the arch's)
        for y in (0.04, 1.2, 2.2):
            parts.append(box(x, y, 0, 0.86, 0.16, 1.66, dark))
        for zf in (-0.84, 0.84):
            parts.append(rod((x - 0.42, 0.2, zf), (x + 0.42, 2.1, zf), 0.08, light))
            parts.append(rod((x + 0.42, 0.2, zf + 0.012 * (1 if zf > 0 else -1)), (x - 0.42, 2.1, zf + 0.012 * (1 if zf > 0 else -1)), 0.08, light))   # the crossing brace 0.012 further out
    parts.append(box(0, 2.14, 0, 2.6, 0.30, 1.5, dark))   # its ends buried in the towers, its top under the deck
    parts.append(box(0, DECK_BODY - 0.02, 0, 4.02, 0.10, 2.02, light))   # sunk into the towers' tops and a little wider than them
    if open_:
        for sx in (-1, 1):
            parts.append(box(sx * 1.08, 0.05, 0.55, 0.14, 2.05, 0.95, main))
    else:
        for sx in (-1, 1):
            parts.append(box(sx * 0.6, 0.05, 0, 1.10, 2.05, 0.14, main))
            parts.append(rod((sx * 0.6 - 0.5, 0.2, -0.09), (sx * 0.6 + 0.5, 1.95, -0.09), 0.1, dark))
            parts.append(rod((sx * 0.6 + 0.5, 0.2, -0.105), (sx * 0.6 - 0.5, 1.95, -0.105), 0.1, dark))   # the crossing brace 0.015 further out
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
        parts.append(box(0, 0, z, 0.94 if i % 2 else 0.92, rise * (i + 1) / steps, 3.0 / steps + 0.002, main if i % 2 else light))   # neighbours' sides differ by 0.01
    for sx in (-1, 1):
        parts.append(box(sx * 0.48, 0, 0, 0.08, 0.2, 3.04, dark))
        parts.append(rod((sx * 0.48, 0.45, 1.5), (sx * 0.48, rise + 0.45, -1.5), 0.08, dark))
        for z in (1.4, 0.2, -1.0):
            h = (1.5 - z) / 3.0 * rise
            parts.append(box(sx * 0.48, h, z, 0.06, 0.5, 0.06, dark))
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
            parts.append(box(0, y, sx * 0.82, 1.6, 0.15, 0.12, main))
    for sx in (-1, 1):
        parts.append(rod((sx * 0.82, 0.3, 0.82), (sx * 0.82, 3.2, -0.82), 0.1, light))
        parts.append(rod((sx * 0.82, 0.3, -0.82), (sx * 0.82, 3.2, 0.82), 0.08, light))   # the crossing brace is slimmer, so its sides stand clear of the first's
    for x in (-0.8, -0.27, 0.27, 0.8):
        for z in (-0.92, 0.92):
            parts.append(box(x, 3.4, z, 0.1, 0.6, 0.08, main))
            parts.append(box(z, 3.4, x, 0.08, 0.6, 0.1, main))
    for z in (-0.95, 0.95):
        parts.append(box(0, 3.95, z, 1.9, 0.08, 0.1, dark))
        parts.append(box(z, 3.95, 0, 0.1, 0.09, 1.9, dark))
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
            parts.append(box(0, 0.25 + k * 0.33, 1.07, 0.6, 0.06, 0.1, light))
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
    """One tile of floor over the one below, built as a foundation is: a base slab with planks on it, but with its base
    in the main colour. The planks are laid exactly as the foundation's are (see deck_planks, a pinwheel that looks the same however the
    game turns the tile, running to every edge the floor reaches), so floors and foundations carry the same pattern from tile to tile.
    Each side with an arm (a floor beside it, or no wall under it) runs to the tile's edge; a side without
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
    parts.append(box(cx, 0, cz, w - 0.01, 0.09, d - 0.01, main))   # the slab stops 0.005 inside the planks' edges
    # The planks are laid about the tile's middle, not the slab's, so the pattern lines up from tile to tile
    return parts + deck_planks(0.09, 0.05, main, light, lo_x, hi_x, lo_z, hi_z)


def upper_floor(pal=WOOD_PAL):
    return floor_tile([], pal)


def stair_step(pal=WOOD_PAL):
    """One step of stairs, a whole tile and a unit tall. The game stretches it up to each step's height."""
    return [box(0, 0, 0, 1.0, 1.0, 1.0, pal[0])]


BUILDERS = {
    'Wagon': wagon, 'Campfire': campfire, 'Bedroll': bedroll, 'Bed': bed, 'Barrel': barrel, 'Ore': ore, 'Tree': tree,
    'Boulder': boulder, 'Mushroom': mushroom, 'Boneheap': bones, 'Stall': stall, 'LockedChest': locked_chest,
    'PrisonerCage': prisoner_cage, 'Chair': chair, 'Bench': bench, 'Stool': stool, 'Table': table, 'GameTable': game_table, 'EndTable': end_table,
    'RoundTable': round_table, 'ResearchTable': research_table, 'StandingTorch': standing_torch, 'WallTorch': wall_torch, 'Painting': wall_painting, 'WallBanner': wall_banner, 'AntlerMount': wall_antlers, 'CrossedSwords': wall_swords, 'SupportBeam': support_beam, 'GuardPost': guard_post,
    'TrainingDummy': training_dummy, 'RawFoodStorage': raw_food_storage, 'FoodStorage': food_storage, 'WeaponRack': weapon_rack,
    'ArmorStand': armor_stand, 'WaterTank': water_tank, 'WaterPump': water_pump, 'Forge': forge, 'Altar': altar, 'Debris': debris,
    'Farm': farm, 'Turbine': turbine, 'Stove': stove, 'WeaponBench': weapon_workbench, 'ArmorBench': armor_workbench, 'AmmoBench': ammunition_workbench, 'Airship': airship, 'Foundation': foundation,
    'WoodWall': wood_wall, 'WoodDoor': wood_door, 'PerimeterWall': perimeter_wall, 'PerimeterGate': perimeter_gate, 'Slope': slope, 'Watchtower': watchtower, 'Ladder': ladder, 'UpperFloor': upper_floor,
    'DragonCrystal': dragon_crystal, 'WoodStockpile': wood_stockpile, 'StoneStockpile': stone_stockpile,
    'IronStockpile': iron_stockpile, 'OreStockpile': ore_stockpile,
    'Wheelbarrow': wheelbarrow, 'TamingCage': taming_cage,
}
ALTAR_EXTRAS = {'altar_frame.glb': altar_frame, 'altar_banner.glb': altar_banner}
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
    # Models the game draws beside a prop's own, named for the prop they go with
    for file, builder in ALTAR_EXTRAS.items():
        if not only or 'Altar' in only:
            parts = builder()
            print(f'{"Altar":16s} {file:18s} {len(parts):3d} parts  {write_glb(os.path.join(DATA, "model", file), parts):4d} triangles')
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
