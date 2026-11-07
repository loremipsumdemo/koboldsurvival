"""Makes simple low-poly prop models: the data/model/model_<n>.glb files that data/props.txt names for each prop.

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
    return parts + flame(0, 0.03, 0, 0.3, 0.65)


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


def wall():
    parts = [box(0, 0, 0, 2.0, 0.15, 0.42, DARK_STONE), box(0, 0.15, 0, 1.9, 0.9, 0.34, STONE), box(0, 1.05, 0, 2.0, 0.08, 0.4, DARK_STONE)]
    for x in (-0.75, -0.25, 0.25, 0.75):
        parts.append(box(x, 1.13, 0, 0.3, 0.17, 0.36, STONE))
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
    return [box(0, 0, 0, 0.9, 0.36, 0.56, WOOD), cyl(0.44, 0.36, 0, 0.54, 0.88, 0.3, DARK_WOOD, 0, 90),
            box(-0.3, 0, 0, 0.06, 0.37, 0.58, IRON), box(0.3, 0, 0, 0.06, 0.37, 0.58, IRON),
            box(0, 0.26, 0.285, 0.1, 0.12, 0.03, BRASS), box(0, 0.32, 0.29, 0.05, 0.05, 0.03, IRON)]


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


def end_table():
    return legs(0.19, 0.19, 0.42, 0.045, DARK_WOOD) + [
        box(0, 0.42, 0, 0.42, 0.17, 0.42, DARK_WOOD), box(0, 0.45, 0.205, 0.32, 0.11, 0.02, WOOD), box(0, 0.49, 0.22, 0.04, 0.03, 0.03, BRASS),
        box(0, 0.59, 0, 0.47, 0.035, 0.47, WOOD), cyl(0.05, 0.625, -0.04, 0.05, 0.05, 0.05, LINEN), cone(0.05, 0.675, -0.04, 0.03, 0.025, 0.03, FLAME)]


def round_table():
    return [box(0, 0, 0, 0.75, 0.05, 0.12, DARK_WOOD), box(0, 0, 0, 0.12, 0.05, 0.75, DARK_WOOD), cyl(0, 0.05, 0, 0.14, 0.65, 0.14, DARK_WOOD),
            cyl(0, 0.7, 0, 1.2, 0.05, 1.2, WOOD), cyl(0.2, 0.75, 0.15, 0.2, 0.03, 0.2, IRON), cyl(-0.22, 0.75, -0.1, 0.08, 0.1, 0.08, PALE_WOOD)]


def metal_table():
    return legs(0.68, 0.33, 0.72, 0.06, IRON) + [
        box(0, 0.72, 0, 1.5, 0.05, 0.78, STEEL), box(0, 0.25, 0, 1.36, 0.025, 0.66, IRON), box(0.3, 0.275, 0, 0.28, 0.07, 0.13, STEEL),
        box(-0.3, 0.77, 0.1, 0.32, 0.025, 0.03, WOOD), box(-0.14, 0.77, 0.1, 0.05, 0.05, 0.1, IRON)]


def standing_torch():
    parts = [cyl(0, 0, 0, 0.06, 1.1, 0.06, DARK_WOOD), cyl(0, 1.05, 0, 0.2, 0.09, 0.2, IRON)]
    for i in range(3):
        t = 2 * math.pi * i / 3
        parts.append(rod((0.28 * math.cos(t), 0, 0.28 * math.sin(t)), (0, 0.3, 0), 0.035, DARK_WOOD))
    return parts + flame(0, 1.14, 0, 0.17, 0.46)


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
            cone(-0.2, 0.25, 0.46, 0.16, 0.3, 0.02, FLAME), cone(-0.32, 0.25, 0.46, 0.1, 0.2, 0.02, FLAME), cone(-0.08, 0.25, 0.46, 0.1, 0.2, 0.02, FLAME),
            cyl(0.6, 0, 0.2, 0.28, 0.3, 0.28, BARK), box(0.6, 0.3, 0.2, 0.2, 0.05, 0.12, IRON), box(0.6, 0.35, 0.2, 0.09, 0.07, 0.08, IRON), box(0.6, 0.42, 0.2, 0.3, 0.05, 0.1, IRON)]


def altar():
    parts = [box(0, 0, 0, 1.2, 0.14, 0.95, DARK_STONE), box(0, 0.14, 0, 0.95, 0.12, 0.75, STONE), box(0, 0.26, 0, 0.66, 0.36, 0.5, (150, 140, 170)),
             box(0, 0.62, 0, 0.8, 0.07, 0.6, STONE), cyl(0, 0.69, 0, 0.1, 0.1, 0.1, IRON), cyl(0, 0.79, 0, 0.42, 0.09, 0.42, IRON)]
    parts += [rod((-0.09, 0.3, 0.255), (0, 0.52, 0.255), 0.022, (150, 225, 255)), rod((0.09, 0.3, 0.255), (0, 0.52, 0.255), 0.022, (150, 225, 255))]
    parts += [rod((-0.5, 0.06, 0.32), (-0.2, 0.06, 0.32), 0.1, BARK, 'cylinder'), rod((-0.48, 0.06, 0.42), (-0.2, 0.06, 0.42), 0.1, BARK, 'cylinder')]
    return parts + flame(0, 0.88, 0, 0.28, 0.32)


def debris():
    return [cyl(0, 0, 0, 1.0, 0.03, 1.0, SOIL), ball(-0.12, 0, 0.03, 0.55, 0.3, 0.42, STONE), ball(0.2, 0, -0.12, 0.38, 0.26, 0.34, DARK_STONE),
            box(0.26, 0.04, 0.24, 0.24, 0.13, 0.17, STONE, 22), box(-0.3, 0.02, -0.26, 0.24, 0.12, 0.16, DARK_STONE, -28),
            rod((-0.42, 0.04, 0.2), (0.06, 0.44, -0.08), 0.06, DARK_WOOD), rod((0.42, 0.04, 0.08), (-0.02, 0.33, 0.22), 0.05, WOOD)]


def shop():
    return [box(0, 0, 0, 1.4, 0.1, 1.1, DARK_STONE), box(0, 0.1, 0, 1.3, 0.95, 1.0, PALE_WOOD), pyramid(0, 1.05, 0, 1.5, 0.55, 1.2, RED),
            box(-0.25, 0.42, 0.502, 0.56, 0.42, 0.01, (40, 34, 30)), box(-0.25, 0.4, 0.58, 0.68, 0.05, 0.17, WOOD), box(-0.25, 0.86, 0.58, 0.72, 0.04, 0.18, BLUE),
            box(0.42, 0.1, 0.505, 0.32, 0.8, 0.015, WOOD), box(0.53, 0.48, 0.52, 0.04, 0.04, 0.02, BRASS)]


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


BUILDERS = {
    'Wagon': wagon, 'Campfire': campfire, 'Bedroll': bedroll, 'Bed': bed, 'Barrel': barrel, 'Ore': ore, 'Tree': tree,
    'Boulder': boulder, 'Mushroom': mushroom, 'Bones': bones, 'Wall': wall, 'Stall': stall, 'LockedChest': locked_chest,
    'PrisonerCage': prisoner_cage, 'Chair': chair, 'Bench': bench, 'Stool': stool, 'Table': table, 'EndTable': end_table,
    'RoundTable': round_table, 'MetalTable': metal_table, 'StandingTorch': standing_torch, 'SupportBeam': support_beam,
    'TrainingDummy': training_dummy, 'RawFoodStorage': raw_food_storage, 'FoodStorage': food_storage, 'WeaponRack': weapon_rack,
    'ArmorStand': armor_stand, 'WaterTank': water_tank, 'WaterPump': water_pump, 'Forge': forge, 'Altar': altar, 'Debris': debris,
    'Shop': shop, 'Farm': farm, 'Turbine': turbine, 'Stove': stove, 'Airship': airship,
}


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
    if set(files) != set(BUILDERS):
        raise SystemExit('BUILDERS and data/props.txt differ: ' + ', '.join(sorted(set(files) ^ set(BUILDERS))))
    only = set(sys.argv[1:])
    for key, file in files.items():
        if only and key not in only:
            continue
        parts = BUILDERS[key]()
        tris = write_glb(os.path.join(DATA, 'model', file), parts)
        print(f'{key:16s} {file:14s} {len(parts):3d} parts  {tris:4d} triangles')


if __name__ == '__main__':
    main()
