"""Makes the seamless gray textures the game lays over its building pieces, one for each material:

  data/texture/wood_grain.png       Wood: foundations, walls, doors and floors of wood, and every ladder and stairs
  data/texture/stone_blocks.png     Stone
  data/texture/metal_plates.png     Metal (made of Iron Ingots)
  data/texture/crystal_facets.png   Dragon Crystal

Run it with:  python tools/make_building_textures.py     (needs Python 3 and Pillow: pip install pillow)
With names after the script's (python tools/make_building_textures.py stone), only those are written.
Then build the game, which packs data/ into data.pak.

Each picture is gray around a middle of 128: the game multiplies a piece's own colors by it (128 leaves a color as it is,
darker darkens it, lighter lightens it), so the piece's model keeps its colors and shading. Each tiles both ways and
covers GRAIN_TILE world units in the game (src/renderer.cpp). Laid on a wall, the picture's v runs up the wall, so the
wood grain stands upright and the stone's courses and the metal's brushing lie level; on a floor, v runs along z.
"""
import math
import os
import random
import sys
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(REPO, 'data', 'texture')
SIDE = 256


def periodic_noise(rng, cells):
    """Smooth noise over [0, 1) that wraps at both edges: a lattice of random values, blended with a smooth curve."""
    grid = [[rng.random() for _ in range(cells)] for _ in range(cells)]

    def at(u, v):
        x, y = u * cells, v * cells
        x0, y0 = int(math.floor(x)) % cells, int(math.floor(y)) % cells
        x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
        fx, fy = x - math.floor(x), y - math.floor(y)
        sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        top = grid[y0][x0] * (1 - sx) + grid[y0][x1] * sx
        bottom = grid[y1][x0] * (1 - sx) + grid[y1][x1] * sx
        return top * (1 - sy) + bottom * sy
    return at


def wrap(d):
    """A difference across the picture, the short way round as it wraps."""
    return (d + 0.5) % 1.0 - 0.5


def gray(k, lift=0):
    g = max(0, min(255, int(round(128 * k + lift))))
    return (g, g, g, 255)


def wood(u, v, tools):
    """Grain lines running up the picture, bent by noise and swirled round a few knots."""
    wobble, wobble2, streaks, blotch = tools
    knots = [(0.27, 0.31, 0.035), (0.71, 0.78, 0.028), (0.62, 0.18, 0.02)]
    rings = 11   # grain lines across the picture; a whole number, so the picture tiles across
    x = u + 0.06 * (wobble(u, v) - 0.5) + 0.025 * (wobble2(u, v) - 0.5)
    knot_dark = 0.0
    for kx, ky, kr in knots:
        dx, dy = wrap(u - kx), wrap(v - ky)
        d = math.hypot(dx, dy * 0.45)
        if d < kr * 4:
            x += 0.22 * kr * kr / max(d * d, kr * kr * 0.25) * dx / max(d, 1e-4) * (1 - d / (kr * 4))
            knot_dark = max(knot_dark, max(0.0, 1.0 - d / kr))
    phase = (x * rings) % 1.0
    line = math.exp(-((phase - 0.15) ** 2) / 0.004)   # the dark late wood: a thin hard line
    soft = 0.5 + 0.5 * math.cos(2 * math.pi * phase)
    pores = streaks(u * 3 % 1.0, v) - 0.5
    k = 1.0 - 0.20 * line - 0.07 * soft + 0.10 * pores + 0.10 * (blotch(u, v) - 0.5) - 0.22 * knot_dark ** 1.5
    return gray(k, 6)


def stone(u, v, tools):
    """Cut blocks laid in courses, each course set half a block along from the one under it, with sunk mortar between."""
    grain, blotch, chips = tools
    courses, per_course = 4, 2
    row = int(v * courses)
    fv = v * courses - row
    shifted = (u + (0.5 / per_course if row % 2 else 0.0)) % 1.0
    col = int(shifted * per_course)
    fu = shifted * per_course - col
    # How far into the block from its nearest edge, in picture units
    edge = min(fu / per_course, (1 - fu) / per_course, fv / courses, (1 - fv) / courses)
    mortar = 0.012
    if edge < mortar:
        return gray(0.62 + 0.1 * (grain(u, v) - 0.5), 8)
    # Each block a little lighter or darker than its neighbours, rounded off at its edges, speckled and pitted
    rng = random.Random(row * 97 + col * 13 + 5)
    tone = 0.9 + 0.2 * rng.random()
    bevel = min(1.0, (edge - mortar) / 0.02)
    k = tone * (0.86 + 0.14 * bevel) + 0.14 * (grain(u, v) - 0.5) + 0.08 * (blotch(u, v) - 0.5)
    if chips(u, v) > 0.78:
        k -= 0.12
    return gray(k, 8)


def metal(u, v, tools):
    """Riveted plates with seams between them, brushed level, each a shade apart, with a faint sheen across."""
    brush, blotch = tools
    plates = 2
    pu, pv = u * plates % 1.0, v * plates % 1.0
    iu, iv = int(u * plates), int(v * plates)
    seam = min(pu, 1 - pu, pv, 1 - pv) / plates
    if seam < 0.006:
        return gray(0.58)
    k = 0.96 + 0.06 * ((iu * 3 + iv * 5) % 4 - 1.5) / 1.5
    k += 0.10 * (brush(u * 0.5 % 1.0, v * 8 % 1.0) - 0.5)       # streaks running level
    k += 0.06 * (blotch(u, v) - 0.5)
    k += 0.05 * math.sin(2 * math.pi * (pu + pv))                # a sheen across each plate
    if seam < 0.014:
        k += 0.10 if (pu < 0.5) == (pv < 0.5) else -0.08         # the plate's raised lip
    # Rivets: a row along each plate's edges
    inset = 0.06
    for ru in (inset, 0.5, 1 - inset):
        for rv in (inset, 0.5, 1 - inset):
            if ru == 0.5 and rv == 0.5:
                continue
            d = math.hypot(pu - ru, pv - rv) / plates
            if d < 0.011:
                k = 1.25 - 40.0 * d   # a bright dome
            elif d < 0.016:
                k -= 0.18             # its shadow
    return gray(k, 10)


def crystal(u, v, tools):
    """Flat facets meeting at sharp bright ridges, each facet its own shade, with a glint here and there."""
    points, shimmer = tools
    best = second = 9.0
    best_i = 0
    for i, (px, py) in enumerate(points):
        d = math.hypot(wrap(u - px), wrap(v - py) * 0.7)   # facets a little taller than wide
        if d < best:
            second, best, best_i = best, d, i
        elif d < second:
            second = d
    ridge = second - best
    facet = 0.82 + 0.36 * random.Random(best_i * 31 + 3).random()
    # Each facet shades from its middle out, as if it tilts
    k = facet - 0.9 * best + 0.06 * (shimmer(u, v) - 0.5)
    if ridge < 0.006:
        k = 1.35
    elif ridge < 0.014:
        k += 0.12
    return gray(k, 6)


def make(name, filename, seed, pixel, tools_for):
    rng = random.Random(seed)
    tools = tools_for(rng)
    image = Image.new('RGBA', (SIDE, SIDE))
    px = image.load()
    for j in range(SIDE):
        for i in range(SIDE):
            px[i, j] = pixel(i / SIDE, j / SIDE, tools)
    os.makedirs(TEXTURES, exist_ok=True)
    out = os.path.join(TEXTURES, filename)
    image.save(out)
    print(f'{name:8s} {out}')


TEXTURE_LIST = {
    'wood': ('wood_grain.png', 7, wood,
             lambda r: (periodic_noise(r, 4), periodic_noise(r, 9), periodic_noise(r, 32), periodic_noise(r, 3))),
    'stone': ('stone_blocks.png', 11, stone,
              lambda r: (periodic_noise(r, 24), periodic_noise(r, 4), periodic_noise(r, 40))),
    'metal': ('metal_plates.png', 13, metal, lambda r: (periodic_noise(r, 32), periodic_noise(r, 3))),
    'crystal': ('crystal_facets.png', 17, crystal,
                lambda r: ([(r.random(), r.random()) for _ in range(14)], periodic_noise(r, 6))),
}


def main():
    only = set(sys.argv[1:])
    unknown = only - set(TEXTURE_LIST)
    if unknown:
        raise SystemExit('No such texture: ' + ', '.join(sorted(unknown)) + ' (there are ' + ', '.join(TEXTURE_LIST) + ')')
    for name, (filename, seed, pixel, tools_for) in TEXTURE_LIST.items():
        if not only or name in only:
            make(name, filename, seed, pixel, tools_for)


if __name__ == '__main__':
    main()
