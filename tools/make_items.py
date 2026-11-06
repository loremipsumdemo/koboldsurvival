"""Draws the game's item sprites: data/item/item_<id>.png, 64 x 64 pixels.

Run it with:  python tools/make_items.py     (needs Python 3 and Pillow: pip install pillow)
Then build the game, which packs data/ into data.pak. The game only reads the finished PNGs and never runs this script.

How a picture is made:
  1. Each picture is one function that draws basic shapes (polygons, circles, lines) through the small canvas class C.
     Coordinates are on a 32 x 32 grid, whatever size the file ends up.
  2. C draws at S times that grid, so the edges come out smooth when the picture is shrunk.
  3. finish() grows the picture's solid area a little and puts that shape behind the art in a dark color. That is the
     outline, which keeps the picture readable on any background.
  4. The result is shrunk to its final size and written over the matching PNG.

How a drawing is chosen: the script reads data/itemdb.txt and looks at each item's name, in this order.
  - GEAR holds armor, weapons, accessories and ammunition. The function gets the item's material from itemdb.txt as its
    colors (MATS), so changing a material there changes the sprite the next time this runs.
  - PLAIN holds materials, food and potions, which have one fixed look.
  - "Raw <animal> Meat" and "Cooked <animal> Meat" use the animal's cut in MEAT, in raw or cooked colors.
  - "<animal> Bones" uses the animal's drawing in BONES.

To add an item: add it to data/itemdb.txt, write a function, and add its name to GEAR or PLAIN (a new animal goes in MEAT
and BONES), then run this script. An item with no drawing stops the script with an error that names it.

Running this overwrites the sprite of every item in itemdb.txt, including any replaced by hand. A preview of them all,
with their ids, is written to out/art_preview/items.png. make_emotes.py borrows the helpers and colors in this file.
"""
import math
import os
import re
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(REPO, 'data')
OUT = os.path.join(ROOT, 'item')
PREVIEW = os.path.join(REPO, 'out', 'art_preview')
S = 8

MATS = {
    'Leather': ((138, 88, 50), (178, 124, 78), (92, 56, 30)),
    'Iron': ((138, 144, 156), (190, 196, 208), (84, 90, 104)),
    'Bronze': ((192, 126, 60), (236, 176, 104), (124, 76, 32)),
    'Steel': ((172, 188, 208), (234, 242, 250), (102, 118, 140)),
    'Silver': ((212, 216, 230), (255, 255, 255), (146, 152, 174)),
    'Bone': ((226, 214, 184), (248, 240, 218), (162, 146, 114)),
    'Wood': ((150, 98, 52), (192, 136, 80), (98, 62, 32)),
    '': ((150, 98, 52), (192, 136, 80), (98, 62, 32)),
}
WOOD = MATS['Wood']
LEATHER = MATS['Leather']
STEEL = MATS['Steel']
IRON = MATS['Iron']
BONE = MATS['Bone']
GOLD = ((240, 190, 60), (255, 230, 134), (178, 122, 28))
RED = ((214, 56, 60), (250, 120, 110), (140, 28, 40))
GREEN = ((92, 176, 72), (150, 216, 110), (52, 118, 52))
BLUE = ((66, 130, 220), (132, 186, 250), (40, 80, 160))
PURPLE = ((150, 88, 220), (200, 150, 255), (90, 50, 150))
CLOTH = ((96, 110, 70), (132, 148, 96), (64, 74, 46))
DARK = (46, 40, 50)
WHITE = (250, 250, 250)
ORANGE, YELLOW = (244, 130, 40), (255, 214, 80)
GLASS = (206, 232, 244)
CLEAR = (0, 0, 0, 0)


class C:
    def __init__(self):
        self.im = Image.new('RGBA', (32 * S, 32 * S), CLEAR)
        self.d = ImageDraw.Draw(self.im)

    def poly(self, col, *pts):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=col)

    def rect(self, col, x0, y0, x1, y1, r=0):
        box = [x0 * S, y0 * S, x1 * S, y1 * S]
        if r:
            self.d.rounded_rectangle(box, r * S, fill=col)
        else:
            self.d.rectangle(box, fill=col)

    def ell(self, col, x0, y0, x1, y1):
        self.d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=col)

    def circ(self, col, x, y, r):
        self.ell(col, x - r, y - r, x + r, y + r)

    def ring(self, col, x, y, r, w):
        self.d.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], outline=col, width=int(w * S))

    def oval_ring(self, col, x0, y0, x1, y1, w):
        self.d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], outline=col, width=int(w * S))

    def line(self, col, w, *pts):
        self.d.line([(x * S, y * S) for x, y in pts], fill=col, width=int(w * S), joint='curve')
        for x, y in pts:
            self.circ(col, x, y, w / 2)

    def arc(self, col, x0, y0, x1, y1, a, b, w):
        self.d.arc([x0 * S, y0 * S, x1 * S, y1 * S], a, b, fill=col, width=int(w * S))

    def pie(self, col, x0, y0, x1, y1, a, b):
        self.d.pieslice([x0 * S, y0 * S, x1 * S, y1 * S], a, b, fill=col)


def star(c, col, x, y, r, inner=0.42, n=5, rot=-90):
    pts = []
    for i in range(n * 2):
        rr = r if i % 2 == 0 else r * inner
        a = math.radians(rot + i * 180 / n)
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    c.poly(col, *pts)


def finish(c):
    grown = c.im.getchannel('A').filter(ImageFilter.MaxFilter(9))
    base = Image.new('RGBA', c.im.size, (22, 12, 16, 0))
    base.putalpha(grown)
    base.alpha_composite(c.im)
    return base.resize((64, 64), Image.LANCZOS)


def dots(c, col, x0, y0, x1, y1, step=2.4, r=0.55):
    y = y0
    row = 0
    while y <= y1:
        x = x0 + (step / 2 if row % 2 else 0)
        while x <= x1:
            c.circ(col, x, y, r)
            x += step
        y += step * 0.8
        row += 1


# ---------- Head
def cap(c, m):
    b, l, d = m
    c.pie(b, 5, 8, 27, 34, 180, 360)
    c.pie(l, 7, 10, 19, 30, 180, 300)
    c.rect(d, 4, 20, 28, 24, 1.5)
    c.line(d, 1, (16, 8), (16, 20))


def hood(c, m):
    b, l, d = m
    c.poly(b, (16, 2), (25, 8), (27, 18), (29, 29), (3, 29), (5, 18), (7, 8))
    c.poly(l, (16, 2), (7, 8), (5, 18), (3, 29), (10, 29), (10, 12))
    c.ell(DARK, 10, 10, 22, 25)
    c.poly(d, (3, 29), (10, 23), (16, 26), (22, 23), (29, 29))


def coif(c, m):
    b, l, d = m
    c.ell(b, 6, 2, 26, 24)
    c.poly(b, (6, 14), (26, 14), (29, 29), (3, 29))
    dots(c, d, 5, 4, 28, 28)
    c.ell(DARK, 11, 10, 21, 22)
    c.circ(l, 11, 6, 1.6)


def helm(c, m):
    b, l, d = m
    c.pie(b, 5, 3, 27, 29, 180, 360)
    c.rect(b, 5, 15, 27, 24)
    c.pie(l, 7, 5, 18, 25, 180, 280)
    c.rect(d, 4, 14, 28, 17)
    c.rect(DARK, 8, 18, 14, 24)
    c.rect(DARK, 18, 18, 24, 24)
    c.rect(b, 14, 16, 18, 27)


def visor(c, m):
    b, l, d = m
    c.pie(b, 5, 3, 27, 29, 180, 360)
    c.rect(b, 5, 15, 27, 28, 2)
    c.pie(l, 7, 5, 16, 22, 180, 280)
    c.poly(d, (4, 13), (28, 13), (28, 19), (16, 23), (4, 19))
    c.rect(DARK, 7, 15, 25, 17)
    for x in (9, 12, 15, 18, 21):
        c.rect(DARK, x, 20, x + 1, 25 - abs(x - 15) * 0.25)
    c.circ(GOLD[0], 4.5, 16, 1.4)
    c.circ(GOLD[0], 27.5, 16, 1.4)


def bascinet(c, m):
    b, l, d = m
    c.poly(b, (16, 1), (24, 10), (27, 18), (27, 29), (5, 29), (5, 18), (8, 10))
    c.poly(l, (16, 1), (8, 10), (5, 18), (5, 29), (10, 29), (10, 14))
    c.ell(DARK, 10, 14, 22, 26)
    c.rect(d, 3, 27, 29, 30, 1)
    dots(c, b, 11, 27.5, 28, 29.5, 2.2, 0.4)


def sallet(c, m):
    b, l, d = m
    c.poly(b, (17, 6), (24, 10), (31, 25), (24, 23), (14, 23))
    c.ell(b, 4, 4, 25, 24)
    c.pie(l, 6, 5, 20, 22, 180, 290)
    c.poly(l, (24, 12), (31, 25), (28, 24), (22, 13))
    c.rect(DARK, 3, 12.5, 17, 15.5)
    c.poly(d, (6, 18), (17, 18), (17, 25), (14, 29), (8, 29))
    c.line(d, 0.9, (5, 11), (21, 11))


def great_helm(c, m):
    b, l, d = m
    c.rect(b, 6, 3, 26, 29, 2)
    c.rect(l, 6, 3, 12, 29, 2)
    c.rect(d, 6, 2, 26, 5, 1)
    c.rect(DARK, 7, 12, 25, 15)
    c.rect(GOLD[0], 14.5, 5, 17.5, 29)
    for y in (19, 22, 25):
        for x in (9, 11.5, 20.5, 23):
            c.circ(DARK, x, y, 0.6)


def circlet(c, m):
    b, l, d = m
    c.oval_ring(d, 3, 9, 29, 25, 3)
    c.arc(l, 3, 9, 29, 25, 0, 180, 3)
    c.poly(b, (11, 22), (16, 15), (21, 22))
    c.ell(BLUE[0], 13.5, 18.5, 18.5, 24)
    c.ell(BLUE[1], 14.5, 19.5, 16.5, 21.5)


def crown(c, m):
    b, l, d = m
    c.poly(GOLD[0], (3, 26), (3, 9), (9, 17), (16, 5), (23, 17), (29, 9), (29, 26))
    c.poly(GOLD[1], (3, 26), (3, 9), (9, 17), (9, 26))
    c.rect(GOLD[2], 3, 23, 29, 27)
    for x, col in ((8, RED[0]), (16, BLUE[0]), (24, GREEN[0])):
        c.circ(col, x, 25, 1.4)
    for x, y in ((3, 8), (16, 4), (29, 8)):
        c.circ(WHITE, x, y, 1.7)


# ---------- Torso
def shirt_shape(c, col, sleeve=True):
    if sleeve:
        c.poly(col, (10, 4), (22, 4), (31, 10), (27, 17), (24, 14), (24, 29), (8, 29), (8, 14), (5, 17), (1, 10))
    else:
        c.poly(col, (9, 4), (23, 4), (25, 12), (24, 29), (8, 29), (7, 12))


def tunic(c, m):
    b, l, d = CLOTH
    shirt_shape(c, b)
    c.poly(l, (10, 4), (16, 4), (16, 29), (8, 29), (8, 14), (5, 17), (1, 10))
    c.poly(DARK, (13, 4), (19, 4), (16, 9))
    c.rect(m[0], 8, 21, 24, 24)
    c.rect(GOLD[0], 14.5, 21, 17.5, 24)


def jerkin(c, m):
    b, l, d = m
    shirt_shape(c, b, False)
    c.poly(l, (9, 4), (14, 4), (14, 29), (8, 29), (7, 12))
    c.poly(DARK, (13, 4), (19, 4), (17, 14), (15, 14))
    c.line(d, 1, (16, 13), (16, 29))
    for y in (16, 20, 24):
        c.line(WHITE, 0.8, (14, y), (18, y + 2))
        c.line(WHITE, 0.8, (18, y), (14, y + 2))


def lamellar(c, m):
    b, l, d = m
    shirt_shape(c, LEATHER[2], False)
    for row, y in enumerate(range(6, 28, 4)):
        for i in range(5):
            x = 7.5 + i * 3.5
            c.rect(b if (i + row) % 2 else l, x, y, x + 3, y + 3.6, 0.6)
    c.poly(DARK, (13, 3), (19, 3), (16, 7))


def brigandine(c, m):
    b, l, d = m
    shirt_shape(c, (130, 40, 50), False)
    c.poly((170, 60, 66), (9, 4), (14, 4), (14, 29), (8, 29), (7, 12))
    for y in range(8, 29, 4):
        for x in (10, 13.5, 18.5, 22):
            c.circ(l, x, y, 0.9)
    c.rect(d, 15.3, 5, 16.7, 29)
    c.poly(DARK, (13, 4), (19, 4), (16, 8))


def hauberk(c, m):
    b, l, d = m
    shirt_shape(c, b)
    dots(c, d, 3, 6, 30, 28)
    c.poly(CLEAR, (0, 16), (5, 18), (8, 15), (8, 30), (0, 30))
    c.poly(CLEAR, (32, 16), (27, 18), (24, 15), (24, 30), (32, 30))
    c.poly(DARK, (13, 4), (19, 4), (16, 8))
    c.rect(LEATHER[0], 8, 18, 24, 20.5)
    c.rect(GOLD[0], 14.5, 17.8, 17.5, 20.8)


def plate_base(c, m):
    b, l, d = m
    c.poly(b, (7, 5), (12, 3), (16, 6), (20, 3), (25, 5), (26, 13), (24, 25), (16, 30), (8, 25), (6, 13))
    c.poly(l, (7, 5), (12, 3), (16, 6), (16, 30), (8, 25), (6, 13))
    c.line(d, 0.9, (16, 7), (16, 29))


def breastplate(c, m):
    plate_base(c, m)
    c.line(m[2], 0.9, (8, 16), (24, 16))
    c.arc(m[2], 8, 7, 16, 17, 0, 180, 0.8)
    c.arc(m[2], 16, 7, 24, 17, 0, 180, 0.8)


def cuirass(c, m):
    plate_base(c, m)
    c.rect(LEATHER[0], 4, 8, 7, 11)
    c.rect(LEATHER[0], 25, 8, 28, 11)
    c.rect(LEATHER[0], 4, 18, 7.5, 21)
    c.rect(LEATHER[0], 24.5, 18, 28, 21)
    for y in (20, 23, 26):
        c.line(m[2], 0.8, (9, y), (23, y))
    c.circ(GOLD[0], 16, 12, 1.7)


def pauldrons(c, m):
    b, l, d = m
    c.pie(b, 0.5, 2, 12, 18, 180, 360)
    c.pie(b, 20, 2, 31.5, 18, 180, 360)
    c.pie(l, 1.5, 3, 8, 15, 180, 330)
    c.pie(l, 21, 3, 27.5, 15, 180, 330)
    c.line(d, 0.8, (1, 10), (11.5, 10))
    c.line(d, 0.8, (20.5, 10), (31, 10))


def chestplate(c, m):
    plate_base(c, m)
    pauldrons(c, m)
    for y in (22, 25):
        c.line(m[2], 0.8, (10, y), (22, y))


def full_plate(c, m):
    plate_base(c, m)
    pauldrons(c, m)
    c.rect(m[2], 11, 1, 21, 6, 1)
    c.rect(DARK, 13, 2.5, 19, 5)
    for y in (17, 20, 23, 26):
        c.line(m[2], 0.8, (9.5, y), (22.5, y))
    star(c, RED[0], 16, 12, 3)


def ceremonial(c, m):
    plate_base(c, m)
    pauldrons(c, m)
    c.line(GOLD[0], 1.3, (7, 6), (12, 4), (16, 7), (20, 4), (25, 6))
    c.line(GOLD[0], 1.2, (8, 24), (16, 29), (24, 24))
    c.line(GOLD[0], 1, (16, 8), (16, 28))
    c.line(GOLD[0], 1, (10, 16), (22, 16))
    c.ell(BLUE[0], 12.5, 11, 19.5, 19)
    c.ell(BLUE[1], 13.5, 12, 16.5, 15)
    c.line(GOLD[0], 1.4, (2, 10), (11, 10))
    c.line(GOLD[0], 1.4, (21, 10), (30, 10))


# ---------- Legs
def legs_shape(c, col, bottom=30, spread=2):
    c.poly(col, (7, 3), (25, 3), (27 + spread * 0.5, bottom), (18, bottom), (16, 13), (14, bottom), (5 - spread * 0.5, bottom))


def breeches(c, m):
    b, l, d = m
    legs_shape(c, b, 19, 0)
    c.poly(l, (7, 3), (13, 3), (11, 19), (5, 19))
    c.rect(d, 6.5, 3, 25.5, 6.5)
    c.rect(GOLD[0], 14.5, 3, 17.5, 6.5)
    c.line(d, 1, (5.5, 17), (13.5, 17))
    c.line(d, 1, (18.5, 17), (26.5, 17))


def trousers(c, m):
    b, l, d = m
    legs_shape(c, b)
    c.poly(l, (7, 3), (13, 3), (11, 30), (4, 30))
    c.rect(d, 6.5, 3, 25.5, 6.5)
    c.line(d, 1, (16, 7), (16, 13))


def leggings(c, m):
    b, l, d = m
    legs_shape(c, LEATHER[2])
    for y in (6, 11, 16, 21, 26):
        c.rect(b, 5, y, 14.3, y + 3.4, 1)
        c.rect(l, 17.7, y, 27, y + 3.4, 1)
    c.rect(LEATHER[0], 6.5, 3, 25.5, 6)


def chausses(c, m):
    b, l, d = m
    legs_shape(c, b)
    c.poly(l, (7, 3), (11, 3), (9, 30), (4, 30))
    for y in (9, 14, 19, 24):
        c.line(d, 0.8, (5, y), (13.5, y))
        c.line(d, 0.8, (18.5, y), (27, y))
    c.rect(LEATHER[0], 6.5, 3, 25.5, 6.5)


def tassets(c, m):
    b, l, d = m
    c.rect(LEATHER[0], 5, 3, 27, 7, 1)
    c.poly(b, (5, 7), (15, 7), (14.5, 25), (10, 29), (5.5, 25))
    c.poly(b, (17, 7), (27, 7), (26.5, 25), (22, 29), (17.5, 25))
    c.poly(l, (5, 7), (9, 7), (9, 28), (5.5, 25))
    c.poly(l, (17, 7), (21, 7), (21, 28), (17.5, 25))
    for y in (13, 19):
        c.line(d, 0.9, (5.3, y), (14.7, y))
        c.line(d, 0.9, (17.3, y), (26.7, y))
    c.circ(GOLD[0], 16, 5, 1.5)


def cuisses(c, m):
    b, l, d = m
    legs_shape(c, LEATHER[0], 30, 0)
    c.poly(b, (6, 5), (15, 5), (14, 18), (6, 18))
    c.poly(b, (17, 5), (26, 5), (26, 18), (18, 18))
    c.poly(l, (6, 5), (10, 5), (10, 18), (6, 18))
    c.poly(l, (17, 5), (21, 5), (21, 18), (18, 18))
    c.ell(d, 6.5, 16.5, 13.5, 22)
    c.ell(d, 18.5, 16.5, 25.5, 22)
    c.circ(l, 10, 19, 1.3)
    c.circ(l, 22, 19, 1.3)


def greave(c, m, x, trim=None):
    b, l, d = m
    c.poly(b, (x, 3), (x + 10, 3), (x + 9.5, 18), (x + 8, 28), (x + 2, 28), (x + 0.5, 18))
    c.poly(l, (x, 3), (x + 4, 3), (x + 4.5, 28), (x + 2, 28), (x + 0.5, 18))
    c.line(d, 0.8, (x + 5, 4), (x + 5, 27))
    if trim:
        c.line(trim, 1.4, (x + 0.3, 3.5), (x + 9.7, 3.5))
        c.line(trim, 1.2, (x + 2, 27.5), (x + 8, 27.5))
        c.circ(trim, x + 5, 11, 1.6)


def greaves(c, m):
    greave(c, m, 4)
    greave(c, m, 18)
    for x in (6, 20):
        c.rect(LEATHER[0], x - 2, 9, x + 8, 11)
        c.rect(LEATHER[0], x - 2, 20, x + 6, 22)


def plate_leggings(c, m):
    b, l, d = m
    legs_shape(c, b)
    c.poly(l, (7, 3), (12, 3), (10.5, 30), (4, 30))
    c.rect(d, 6.5, 3, 25.5, 6.5)
    c.ell(l, 6, 13, 13.5, 19)
    c.ell(l, 18.5, 13, 26, 19)
    for y in (10, 22, 26):
        c.line(d, 0.8, (4.8, y), (13.8, y))
        c.line(d, 0.8, (18.2, y), (27.2, y))


def mail_leggings(c, m):
    b, l, d = m
    legs_shape(c, b)
    dots(c, d, 4, 8, 28, 29, 2.2, 0.5)
    c.poly(CLEAR, (14.3, 14), (17.7, 14), (18, 31), (14, 31))
    c.rect(LEATHER[0], 6.5, 3, 25.5, 7)
    c.rect(GOLD[0], 14.5, 3, 17.5, 7)


def gilded_greaves(c, m):
    greave(c, m, 4, GOLD[0])
    greave(c, m, 18, GOLD[0])


# ---------- Feet
def boot(c, m, top=5, toe=29):
    b, l, d = m
    c.poly(b, (9, top), (19, top), (19, 18), (toe - 3, 21), (toe, 25), (toe, 28), (9, 28))
    c.poly(l, (9, top), (13, top), (13, 28), (9, 28))
    c.rect(d, 8.5, 26, toe + 0.3, 29.5, 0.8)


def sandals(c, m):
    b, l, d = m
    for x in (3, 18):
        c.rect(d, x, 3, x + 11, 30, 5)
        c.rect(b, x + 1, 4, x + 10, 29, 4.5)
        c.rect(l, x - 0.5, 9, x + 11.5, 12)
        c.rect(l, x - 0.5, 19, x + 11.5, 22)
        c.line(l, 1.6, (x + 5.5, 4), (x + 5.5, 10))


def shoes(c, m):
    boot(c, m, 15)
    c.rect(m[2], 8.5, 14, 19.5, 16.5)
    c.line(WHITE, 0.9, (19, 18), (22, 20))


def boots(c, m):
    boot(c, m, 6)
    c.rect(m[2], 8.3, 5, 19.7, 8.5, 0.6)
    for y in (12, 16):
        c.rect(m[2], 9, y, 19, y + 1.2)


def riding_boots(c, m):
    boot(c, m, 2, 28)
    c.poly(m[2], (9, 2), (19, 2), (21, 6), (19, 8), (9, 8))
    c.line(STEEL[0], 1.3, (9, 22), (3, 21))
    star(c, GOLD[0], 3, 21, 3, 0.4, 6)


def hobnail(c, m):
    boot(c, LEATHER, 7)
    c.rect(m[0], 8.5, 26, 29.3, 29.5, 0.8)
    for x in range(10, 29, 3):
        c.circ(m[1], x, 29.6, 1)
    c.rect(m[0], 8.3, 6, 19.7, 9)


def wrappings(c, m):
    b, l, d = m
    c.poly(b, (10, 3), (20, 3), (20, 17), (28, 21), (30, 28), (10, 28))
    for i, y in enumerate(range(4, 28, 3)):
        x1 = 20 if y < 17 else 20 + (y - 17) * 0.95
        c.line(d if i % 2 else l, 1.2, (10, y + 1.5), (x1, y))
    c.line(l, 1.3, (20, 5), (25, 10))


def marching(c, m):
    boot(c, LEATHER, 5)
    c.poly(m[0], (19, 18), (26, 21), (29, 25), (29, 27), (19, 27))
    c.poly(m[1], (19, 18), (23, 19.5), (23, 27), (19, 27))
    c.rect(m[0], 8.3, 4, 19.7, 7.5)
    for y in (11, 15):
        c.rect(DARK, 8.8, y, 19.2, y + 1.6)
        c.rect(GOLD[0], 12.5, y - 0.3, 15, y + 1.9)


def sabatons(c, m):
    b, l, d = m
    c.poly(b, (9, 12), (18, 12), (19, 17), (28, 22), (30, 28), (9, 28))
    c.poly(l, (9, 12), (13, 12), (13, 28), (9, 28))
    for x, y in ((19, 17), (22, 19), (25, 21.5)):
        c.line(d, 0.9, (x, y), (x, 28))
    c.rect(d, 8.5, 11, 18.5, 14)
    c.rect(LEATHER[2], 8.5, 26.5, 30, 29.3)


def sollerets(c, m):
    b, l, d = m
    c.poly(b, (7, 10), (16, 10), (17, 17), (31, 27), (31, 28), (7, 28))
    c.poly(l, (7, 10), (11, 10), (11, 28), (7, 28))
    for x, y in ((17, 17), (20, 19.5), (23, 22), (26, 24)):
        c.line(d, 0.8, (x, y), (x, 28))
    c.rect(d, 6.5, 9, 16.5, 12)
    c.rect(LEATHER[2], 6.5, 26.5, 31, 29)


def warboots(c, m):
    b, l, d = m
    c.poly(b, (9, 3), (20, 3), (20, 16), (28, 20), (30, 24), (30, 28), (9, 28))
    c.poly(l, (9, 3), (13, 3), (13, 28), (9, 28))
    c.rect(d, 8.3, 2.5, 20.7, 6)
    c.rect(d, 8.5, 26.5, 30.5, 30, 0.8)
    for y in (6, 13):
        c.poly(l, (9, y), (9, y + 4), (3, y + 2))
    c.poly(l, (20, 13), (27, 17), (24, 8))
    c.line(RED[0], 1.4, (9, 18), (20, 18))


# ---------- Accessories
def band(c, m):
    b, l, d = m
    c.ring(d, 16, 16, 12, 5)
    c.arc(b, 4, 4, 28, 28, 0, 360, 3.5)
    c.arc(l, 4, 4, 28, 28, 180, 300, 2)


def charm(c, m):
    b, l, d = m
    c.arc(LEATHER[1], 5, -3, 27, 19, 20, 160, 1.5)
    c.poly(b, (12, 13), (20, 13), (18, 24), (16, 30), (14, 24))
    c.poly(l, (12, 13), (15, 13), (15, 27), (14, 24))
    c.circ(LEATHER[2], 16, 15, 1.3)
    c.circ(RED[0], 8, 13, 2)
    c.circ(RED[0], 24, 13, 2)


def bracelet(c, m):
    b, l, d = m
    c.oval_ring(d, 2, 6, 30, 28, 6)
    c.arc(b, 2, 6, 30, 28, 0, 180, 5)
    c.arc(l, 2, 6, 30, 28, 30, 110, 3)
    for a in (25, 65, 115, 155):
        r = math.radians(a)
        c.circ(d, 16 + 11.5 * math.cos(r), 17 + 8.5 * math.sin(r), 1)


def brooch(c, m):
    b, l, d = m
    c.line(STEEL[1], 1.3, (3, 25), (29, 7))
    c.circ(d, 16, 16, 10.5)
    c.circ(b, 16, 16, 9)
    c.ring(l, 16, 16, 8, 1)
    star(c, l, 16, 16, 7.5, 0.5, 8)
    c.circ(RED[2], 16, 16, 4.5)
    c.circ(RED[0], 16, 16, 3.7)
    c.circ(RED[1], 14.8, 14.8, 1.2)


def ring(c, m):
    b, l, d = m
    c.ring(d, 16, 19, 10, 3.4)
    c.arc(l, 6, 9, 26, 29, 110, 250, 2)
    c.poly(b, (10, 9), (22, 9), (19, 12), (13, 12))
    c.poly(BLUE[0], (11, 8), (14, 3), (18, 3), (21, 8), (16, 13))
    c.poly(BLUE[1], (14, 3), (16, 3), (15, 8), (11, 8))


def chain(c, col, y=14):
    c.arc(col, 5, -6, 27, y + 4, 15, 165, 1.3)


def amulet(c, m):
    b, l, d = m
    chain(c, l)
    c.circ(d, 16, 21, 8.5)
    c.circ(b, 16, 21, 7)
    c.circ(GREEN[2], 16, 21, 4.2)
    c.circ(GREEN[0], 16, 21, 3.4)
    c.circ(GREEN[1], 14.8, 19.8, 1.2)


def pendant(c, m):
    b, l, d = m
    chain(c, l)
    c.circ(l, 16, 13.5, 1.7)
    c.poly(PURPLE[0], (16, 14), (22, 22), (19.5, 28), (12.5, 28), (10, 22))
    c.poly(PURPLE[1], (16, 14), (16, 28), (12.5, 28), (10, 22))
    c.line(b, 1, (10, 22), (12.5, 28), (19.5, 28), (22, 22), (16, 14), (10, 22))


def talisman(c, m):
    b, l, d = m
    c.arc(LEATHER[1], 8, -4, 24, 10, 15, 165, 1.3)
    c.circ(d, 16, 18, 11.5)
    c.circ(b, 16, 18, 10)
    c.circ(l, 13.5, 15, 4)
    c.line(RED[2], 1.6, (16, 11), (16, 25))
    c.line(RED[2], 1.6, (16, 15), (11, 11.5))
    c.line(RED[2], 1.6, (16, 15), (21, 11.5))
    c.line(RED[2], 1.6, (16, 21), (11, 25))
    c.line(RED[2], 1.6, (16, 21), (21, 25))


def torc(c, m):
    b, l, d = m
    c.arc(d, 3, 3, 29, 29, 120, 60, 4.5)
    c.arc(b, 3, 3, 29, 29, 120, 60, 3.2)
    c.arc(l, 3, 3, 29, 29, 170, 290, 1.5)
    c.circ(GOLD[0], 9.5, 27, 3)
    c.circ(GOLD[0], 22.5, 27, 3)
    c.circ(GOLD[1], 8.7, 26.2, 1.1)
    c.circ(GOLD[1], 21.7, 26.2, 1.1)


def cloak(c, m):
    r = ((130, 40, 60), (174, 64, 80), (84, 24, 42))
    c.poly(r[0], (10, 4), (22, 4), (29, 30), (3, 30))
    c.poly(r[1], (10, 4), (15, 4), (11, 30), (3, 30))
    c.poly(r[2], (19, 7), (22, 30), (18, 30), (17, 8))
    c.ell(DARK, 11, 2, 21, 8)
    c.circ(GOLD[0], 16, 8.5, 2.2)
    c.line(m[1], 1.6, (11, 7), (21, 7))


# ---------- Weapons
def blade(c, m, tip, base, width, hilt=True):
    b, l, d = m
    dx, dy = base[0] - tip[0], base[1] - tip[1]
    n = math.hypot(dx, dy)
    ux, uy = dx / n, dy / n
    px, py = -uy * width / 2, ux * width / 2
    near = (tip[0] + ux * width * 1.2, tip[1] + uy * width * 1.2)
    c.poly(b, tip, (near[0] + px, near[1] + py), (base[0] + px, base[1] + py), (base[0] - px, base[1] - py), (near[0] - px, near[1] - py))
    c.poly(l, tip, (near[0] + px, near[1] + py), (base[0] + px, base[1] + py), base)
    if hilt:
        g = width * 1.3 + 1.5
        c.line(GOLD[2], 2.2, (base[0] + -uy * g, base[1] + ux * g), (base[0] - -uy * g, base[1] - ux * g))
    return ux, uy


def sword(c, m, tip, base, width, grip, pommel=True, guard=None):
    ux, uy = blade(c, m, tip, base, width)
    end = (base[0] + ux * grip, base[1] + uy * grip)
    c.line(LEATHER[2], 2.6, base, end)
    if pommel:
        c.circ(GOLD[0], end[0], end[1], 1.8)


def short_sword(c, m):
    sword(c, m, (27, 4), (10, 21), 4.5, 6)


def long_sword(c, m):
    sword(c, m, (30, 2), (9, 23), 3.8, 7)
    c.line(m[2], 0.6, (26, 6), (11, 21))


def axe(c, m):
    c.line(WOOD[0], 3, (7, 29), (20, 5))
    c.pie(m[0], 8, 1, 30, 21, 300, 110)
    c.pie(m[1], 8, 1, 30, 21, 300, 15)
    c.rect(m[2], 16, 8, 21, 12)
    c.circ(WOOD[1], 20.5, 4.5, 1.5)


def mace(c, m):
    c.line(WOOD[0], 3, (6, 28), (19, 13))
    c.line(LEATHER[2], 3.4, (6, 28), (9.5, 24))
    c.circ(m[0], 21, 10, 7)
    for a in range(0, 360, 60):
        r = math.radians(a)
        x, y = 21 + 7.5 * math.cos(r), 10 + 7.5 * math.sin(r)
        c.poly(m[2], (x + 3 * math.cos(r), y + 3 * math.sin(r)), (x - 2 * math.sin(r), y + 2 * math.cos(r)), (x + 2 * math.sin(r), y - 2 * math.cos(r)))
    c.circ(m[1], 19, 8, 3)


def flail(c, m):
    c.line(WOOD[0], 3, (4, 29), (13, 16))
    c.line(LEATHER[2], 3.4, (4, 29), (7, 25))
    c.line(IRON[2], 1.2, (13, 16), (15, 12), (19, 11), (20, 15))
    for x, y in ((14.5, 13.5), (17, 11), (19.5, 12.5)):
        c.circ(IRON[1], x, y, 1)
    c.circ(m[0], 22, 20, 6.5)
    for a in range(0, 360, 45):
        r = math.radians(a)
        c.line(m[2], 1.3, (22 + 6 * math.cos(r), 20 + 6 * math.sin(r)), (22 + 9 * math.cos(r), 20 + 9 * math.sin(r)))
    c.circ(m[1], 20, 18, 2.4)


def rapier(c, m):
    blade(c, m, (31, 1), (10, 22), 1.8, False)
    c.arc(GOLD[0], 3, 15, 17, 29, 200, 60, 1.6)
    c.circ(GOLD[2], 10, 22, 3.2)
    c.line(LEATHER[2], 2.4, (10, 22), (4.5, 27.5))
    c.circ(GOLD[0], 4, 28, 1.8)


def dagger(c, m):
    sword(c, m, (24, 6), (12, 18), 5, 7)


def knife(c, m):
    c.poly(m[0], (25, 3), (27, 8), (15, 21), (11, 17))
    c.poly(m[1], (25, 3), (15, 21), (11, 17))
    c.line(WOOD[0], 3.6, (12, 20), (6, 26))
    c.circ(IRON[1], 9, 23, 0.8)


def stiletto(c, m):
    blade(c, m, (29, 3), (12, 20), 2, True)
    c.line(LEATHER[2], 2.4, (12, 20), (6, 26))
    c.circ(RED[0], 5.5, 26.5, 2)


def buckler(c, m):
    c.circ(m[2], 16, 16, 13.5)
    c.circ(m[0], 16, 16, 12)
    for x in (9, 14, 19, 24):
        c.line(m[2], 0.8, (x, 5.5), (x, 26.5))
    c.ring(IRON[0], 16, 16, 12.8, 1.8)
    c.circ(IRON[2], 16, 16, 5)
    c.circ(IRON[1], 15, 15, 3.5)


def kite(c, m):
    b, l, d = m
    c.poly(d, (16, 2), (27, 5), (27, 13), (16, 31), (5, 13), (5, 5))
    c.poly(RED[0], (16, 4.5), (25, 6.5), (25, 12.8), (16, 27.5), (7, 12.8), (7, 6.5))
    c.poly(RED[1], (16, 4.5), (16, 27.5), (7, 12.8), (7, 6.5))
    c.line(b, 2, (16, 5), (16, 27))
    c.line(b, 2, (7.5, 12), (24.5, 12))


def tower(c, m):
    b, l, d = m
    c.rect(d, 6, 1, 26, 31, 2)
    c.rect(b, 7.5, 2.5, 24.5, 29.5, 1)
    c.rect(l, 7.5, 2.5, 13, 29.5, 1)
    c.rect(BLUE[0], 12, 5, 20, 27)
    c.rect(GOLD[0], 15, 7, 17, 25)
    c.rect(GOLD[0], 13, 12, 19, 14)
    for x, y in ((9, 5), (23, 5), (9, 27), (23, 27), (9, 16), (23, 16)):
        c.circ(d, x, y, 0.9)


def greatsword(c, m):
    ux, uy = blade(c, m, (31, 1), (10, 22), 5.5)
    c.line(m[2], 0.8, (26, 6), (11, 21))
    c.line(LEATHER[2], 2.8, (10, 22), (3, 29))
    c.circ(GOLD[0], 2.5, 29.5, 2)


def warhammer(c, m):
    c.line(WOOD[0], 3, (5, 30), (21, 9))
    c.line(LEATHER[2], 3.4, (5, 30), (10, 24))
    c.poly(m[0], (11, 4), (18, -1 + 2), (31, 11), (25, 18))
    c.poly(m[1], (11, 4), (18, 1), (24, 7), (17, 10))
    c.poly(m[2], (11, 4), (25, 18), (22, 20), (9, 7))
    star(c, GOLD[0], 20, 9, 2.4, 0.4, 4)


def battleaxe(c, m):
    b, l, d = m
    c.line(WOOD[2], 3.4, (16, 3), (16, 31))
    c.line(WOOD[0], 2.2, (16, 3), (16, 31))
    c.line(LEATHER[2], 3.4, (16, 24), (16, 30))
    for s in (1, -1):
        fx = lambda x: 16 + s * x
        c.poly(d, (fx(1), 7), (fx(12), 1), (fx(15.5), 10), (fx(12), 20), (fx(1), 14))
        c.poly(b, (fx(1), 8), (fx(11), 3), (fx(14), 10.5), (fx(11), 18), (fx(1), 13))
        c.poly(l, (fx(11), 3), (fx(14), 10.5), (fx(11), 18), (fx(9), 10.5))
    c.rect(d, 13.5, 6, 18.5, 15, 1)
    c.poly(l, (13.5, 6), (18.5, 6), (16, 0.5))


def short_bow(c, m):
    c.arc(WOOD[2], -2, 5, 25, 27, 275, 85, 4)
    c.arc(WOOD[0], -2, 5, 25, 27, 280, 80, 2.8)
    c.line(WHITE, 0.9, (11.5, 5.5), (11.5, 26.5))
    c.rect(LEATHER[1], 20.5, 12.5, 25.5, 19.5, 0.6)
    c.circ(WOOD[1], 12, 5.5, 1.5)
    c.circ(WOOD[1], 12, 26.5, 1.5)


def long_bow(c, m):
    c.arc(WOOD[2], -12, 0, 27, 32, 277, 83, 4.2)
    c.arc(WOOD[1], -12, 0, 27, 32, 281, 79, 2.6)
    c.line(WHITE, 0.9, (7.5, 0.8), (7.5, 31.2))
    c.rect(LEATHER[2], 21.5, 12, 27.5, 20, 0.6)
    c.line(LEATHER[1], 1, (21.5, 14.5), (27.5, 14.5))
    c.line(LEATHER[1], 1, (21.5, 17.5), (27.5, 17.5))


def crossbow(c, m):
    c.line(WOOD[0], 4, (16, 4), (16, 26))
    c.rect(WOOD[2], 13, 22, 19, 31, 1.5)
    c.arc(IRON[0], 3, 5, 29, 19, 180, 360, 2.8)
    c.arc(IRON[1], 3, 5, 29, 19, 190, 260, 1.2)
    c.line(WHITE, 0.8, (3.5, 12), (16, 19), (28.5, 12))
    c.line(STEEL[1], 1.2, (16, 2), (16, 18))
    c.poly(STEEL[0], (13.5, 4.5), (16, 0.5), (18.5, 4.5))
    c.rect(DARK, 15, 19.5, 17, 23)


def arrows(c, head, fletch, tail_y=30, shaft=WOOD[1], fire=False, n=3):
    pos = [(8, 26, 7, 8), (16, 29, 16, 4), (24, 26, 25, 8)][:n]
    for x0, y0, x1, y1 in pos:
        c.line(shaft, 1.5, (x0, y0), (x1, y1))
        c.poly(fletch, (x0 - 2, y0), (x0 + 2, y0), (x0 + 2, y0 - 4), (x0, y0 - 2.4), (x0 - 2, y0 - 4))
        if not fire:
            c.poly(head, (x1 - 2.4, y1 + 1), (x1 + 2.4, y1 + 1), (x1, y1 - 4))
    c.rect(LEATHER[1], 5, 15, 27, 18, 1)


def wood_arrows(c, m):
    arrows(c, STEEL[0], RED[0])


def bone_arrows(c, m):
    arrows(c, BONE[1], WHITE, shaft=BONE[0])


def fire_arrows(c, m):
    arrows(c, ORANGE, YELLOW, fire=True)
    for x, y in ((7, 7), (16, 4.5), (25, 7)):
        c.poly(ORANGE, (x, y - 5), (x + 3, y), (x + 2, y + 3), (x - 2, y + 3), (x - 3, y))
        c.poly(YELLOW, (x, y - 2), (x + 1.5, y + 1), (x + 1, y + 3), (x - 1, y + 3), (x - 1.5, y + 1))


def bolts(c, m):
    for x, y0, y1 in ((9, 25, 9), (16, 28, 6), (23, 25, 9)):
        c.line(DARK, 2, (x, y0), (x, y1))
        c.poly(IRON[1], (x - 2.6, y1 + 1), (x + 2.6, y1 + 1), (x, y1 - 4))
        c.poly(RED[0], (x - 2.6, y0 + 1), (x + 2.6, y0 + 1), (x + 1, y0 - 3), (x - 1, y0 - 3))
    c.rect(LEATHER[1], 6, 15, 26, 18, 1)


# ---------- Materials and treasure
def ore(c):
    s = ((112, 104, 100), (150, 142, 136), (76, 70, 68))
    c.poly(s[0], (3, 25), (5, 12), (13, 5), (24, 7), (29, 16), (28, 26), (18, 29), (7, 29))
    c.poly(s[1], (5, 12), (13, 5), (16, 14), (9, 20))
    c.poly(s[2], (16, 14), (29, 16), (28, 26), (18, 29))
    for x, y, r in ((12, 12, 2.2), (20, 11, 2.6), (10, 23, 2.4), (20, 21, 3), (25, 17, 1.8)):
        c.circ((206, 118, 76), x, y, r)
        c.circ((240, 170, 120), x - r * 0.3, y - r * 0.3, r * 0.4)


def log(c, m, dark=False):
    b, l, d = m
    c.poly(b, (3, 18), (22, 5), (29, 15), (10, 28))
    c.poly(l, (3, 18), (22, 5), (25, 9), (6, 22))
    for t in (0.3, 0.6):
        c.line(d, 0.8, (3 + 19 * t, 20 - 13 * t), (9 + 19 * t, 27 - 14 * t))
    c.ell(WOOD[1], 2, 16, 13, 30)
    c.ell((222, 178, 120), 3.5, 18, 11.5, 28)
    c.oval_ring(d, 5, 20, 10, 26, 0.8)
    c.circ(d, 7.5, 23, 0.7)


def oak_log(c):
    log(c, ((112, 76, 44), (146, 104, 64), (72, 46, 26)))
    c.ell(GREEN[0], 18, 1, 26, 7)
    c.line(GREEN[2], 0.8, (19, 6), (25, 2))


def wood_log(c):
    log(c, WOOD)


def healing_herb(c):
    c.line(GREEN[2], 1.6, (16, 30), (16, 8))
    for y, s in ((22, 1), (17, -1), (12, 1)):
        c.poly(GREEN[0], (16, y), (16 + s * 11, y - 1), (16 + s * 8, y - 7))
        c.poly(GREEN[1], (16, y), (16 + s * 8, y - 7), (16 + s * 3, y - 6))
    star(c, (255, 130, 160), 16, 6, 5, 0.5, 6)
    c.circ(YELLOW, 16, 6, 1.7)
    c.line(LEATHER[1], 2, (12, 27), (20, 27))


def gold_coin(c):
    c.circ(GOLD[2], 16, 16, 13)
    c.circ(GOLD[0], 15.5, 15.5, 12)
    c.circ(GOLD[1], 13, 12, 5)
    c.ring(GOLD[2], 15.5, 15.5, 9, 1)
    star(c, GOLD[2], 15.5, 15.5, 6)
    star(c, WHITE, 25, 6, 3.4, 0.25, 4)


def wolf_pelt(c):
    g, l, d = (138, 138, 146), (182, 182, 190), (88, 88, 98)
    c.poly(g, (16, 2), (20, 6), (27, 6), (31, 11), (25, 14), (24, 21), (29, 28), (22, 26), (18, 31), (14, 31), (10, 26), (3, 28), (8, 21), (7, 14), (1, 11), (5, 6), (12, 6))
    c.poly(l, (12, 8), (20, 8), (21, 24), (11, 24))
    c.poly(g, (12, 3), (14, 6), (11, 7))
    c.poly(g, (20, 3), (21, 7), (18, 6))
    c.circ(DARK, 13, 5.5, 0.7)
    c.circ(DARK, 19, 5.5, 0.7)
    c.line(d, 0.9, (16, 9), (16, 24))


def relic(c):
    s = ((154, 128, 74), (200, 176, 110), (98, 80, 44))
    c.poly(s[0], (10, 2), (22, 2), (24, 6), (22, 12), (27, 26), (27, 30), (5, 30), (5, 26), (10, 12), (8, 6))
    c.poly(s[1], (10, 2), (15, 2), (14, 30), (5, 30), (5, 26), (10, 12), (8, 6))
    c.circ((90, 220, 210), 16, 8, 3)
    c.circ(WHITE, 15, 7, 1)
    for x, y in ((12, 18), (19, 18), (12, 24), (19, 24), (16, 21)):
        star(c, s[2], x, y, 2.2, 0.3, 4)
    star(c, (120, 240, 230), 26, 7, 3.2, 0.25, 4)


def potion(c, col, shape='round'):
    b, l, d = col
    c.rect(WOOD[1], 13, 1.5, 19, 5.5, 1)
    c.rect(GLASS, 12.5, 5, 19.5, 12)
    if shape == 'round':
        c.circ(GLASS, 16, 20, 10.5)
        c.circ(b, 16, 20.5, 9)
        c.poly(GLASS, (5, 11), (27, 11), (27, 15.5), (5, 15.5))
        c.circ(l, 12, 19, 2.6)
        c.circ(d, 20, 26, 2)
    elif shape == 'flask':
        c.poly(GLASS, (12.5, 11), (19.5, 11), (28, 28), (26, 30), (6, 30), (4, 28))
        c.poly(b, (10.5, 17), (21.5, 17), (26.5, 28), (25, 29), (7, 29), (5.5, 28))
        c.circ(l, 12, 23, 2)
        c.circ(l, 19, 26, 1.2)
    else:
        c.rect(GLASS, 7, 11, 25, 30, 3)
        c.rect(b, 8.5, 15, 23.5, 28.5, 2)
        c.rect(l, 10, 16, 13, 26, 1)
        c.line(GOLD[0], 1.4, (12.5, 8), (19.5, 8))


def health_potion(c):
    potion(c, RED)


def mana_potion(c):
    potion(c, BLUE, 'flask')


def elixir(c):
    potion(c, ((240, 130, 40), (255, 200, 110), (180, 84, 20)), 'square')
    star(c, YELLOW, 16, 22, 4)


def antidote(c):
    potion(c, GREEN, 'flask')
    c.rect(WHITE, 11, 22, 21, 27, 0.5)
    c.line(RED[0], 1, (14, 24.5), (18, 24.5))
    c.line(RED[0], 1, (16, 23), (16, 26))


# ---------- Food
CRUST, CRUST_L, CRUST_D = (206, 142, 70), (236, 186, 110), (150, 92, 40)


def bread(c):
    c.ell(CRUST_D, 2, 9, 30, 28)
    c.ell(CRUST, 2, 8, 30, 26)
    c.ell(CRUST_L, 5, 9.5, 20, 18)
    for x in (10, 16, 22):
        c.line(CRUST_D, 1.3, (x - 2, 12), (x + 2, 19))


def apple(c):
    c.circ(RED[0], 11.5, 18, 8.8)
    c.circ(RED[0], 20.5, 18, 8.8)
    c.ell(RED[2], 12, 22, 20, 28)
    c.circ(RED[1], 10, 15, 3)
    c.line(WOOD[2], 1.5, (16, 10), (17.5, 4))
    c.poly(GREEN[0], (18, 7), (25, 2), (27, 7), (21, 10))


def berries(c):
    c.poly(GREEN[0], (4, 6), (15, 4), (16, 11), (8, 13))
    c.poly(GREEN[1], (28, 6), (17, 4), (16, 11), (24, 13))
    for x, y, r in ((10, 17, 4.5), (20, 16, 5), (15, 24, 5), (24, 24, 4.3), (7, 25, 3.8)):
        c.circ(PURPLE[2], x, y, r)
        c.circ((120, 60, 180), x - 0.3, y - 0.3, r * 0.85)
        c.circ(PURPLE[1], x - r * 0.3, y - r * 0.3, r * 0.3)


def biscuit(c):
    c.rect(CRUST_D, 4, 6, 28, 28, 3)
    c.rect((216, 180, 124), 4, 5, 28, 26, 3)
    c.rect((236, 208, 156), 6, 7, 16, 14, 2)
    for y in (10, 15.5, 21):
        for x in (9, 16, 23):
            c.circ(CRUST_D, x, y, 1.1)


def cheese(c):
    y, l, d = (244, 200, 70), (255, 228, 130), (196, 146, 40)
    c.poly(d, (3, 18), (29, 8), (29, 26), (3, 28))
    c.poly(y, (3, 18), (29, 8), (29, 24), (3, 25))
    c.poly(l, (3, 18), (29, 8), (23, 5))
    for x, yy, r in ((9, 21, 1.8), (16, 17.5, 2.4), (24, 15, 1.7), (22, 21, 1.5)):
        c.circ(d, x, yy, r)


def fish_shape(c, b, l, d, eye=True):
    c.ell(b, 3, 8, 25, 24)
    c.poly(b, (22, 16), (31, 7), (29, 16), (31, 25))
    c.poly(l, (6, 11), (20, 9.5), (22, 14), (5, 15))
    c.poly(d, (11, 9), (18, 3), (20, 9))
    c.poly(d, (12, 23), (17, 28), (19, 22))
    c.arc(d, 3, 10, 15, 22, 310, 50, 1)
    if eye:
        c.circ(WHITE, 8, 13.5, 1.8)
        c.circ(DARK, 7.6, 13.5, 0.9)


def smoked_fish(c):
    fish_shape(c, (168, 110, 54), (214, 158, 88), (112, 68, 32))
    for x in (13, 17, 21):
        c.line((112, 68, 32), 0.9, (x, 12), (x - 2, 21))


def honey_cake(c):
    c.rect(CRUST_D, 4, 13, 28, 28, 2)
    c.rect(CRUST, 4, 12, 28, 26, 2)
    c.rect((252, 232, 190), 4, 18, 28, 20)
    c.rect(GOLD[0], 3, 8, 29, 14, 3)
    for x, y in ((7, 18), (15, 20), (24, 17)):
        c.rect(GOLD[0], x - 1.5, 12, x + 1.5, y, 1.5)
    c.ell(GOLD[1], 7, 9, 17, 12)
    star(c, WHITE, 24, 6, 2.8, 0.25, 4)


def pie(c):
    c.poly((170, 170, 180), (2, 19), (30, 19), (26, 28), (6, 28))
    c.pie(CRUST, 3, 5, 29, 31, 180, 360)
    c.pie(CRUST_L, 5, 7, 20, 25, 180, 280)
    c.rect(CRUST_D, 2, 17, 30, 20.5, 1.5)
    for x in (10, 16, 22):
        c.line(CRUST_D, 1.3, (x, 8.5), (x, 14))
    for x in (11, 16, 21):
        c.line(WHITE, 1, (x, 4), (x + 1, 1.5))


def stew(c):
    for x in (11, 21):
        c.line(WHITE, 1.3, (x, 9), (x + 1.5, 6), (x - 0.8, 3.5))
    c.ell((122, 74, 40), 3, 10, 29, 20)
    c.ell((164, 92, 44), 5, 11, 27, 19)
    c.circ(ORANGE, 11, 14.5, 2)
    c.circ(GREEN[0], 17, 13.5, 1.6)
    c.circ((200, 150, 100), 22, 15, 2)
    c.poly(WOOD[0], (3, 15), (29, 15), (25, 26), (21, 29), (11, 29), (7, 26))
    c.poly(WOOD[1], (3, 15), (11, 15), (13, 29), (11, 29), (7, 26))


def roast_leg(c):
    drumstick(c, (140, 80, 40), (186, 124, 66), (96, 52, 26), big=True)
    for p in ((10, 11), (15, 8), (9, 17)):
        c.circ((100, 56, 28), p[0], p[1], 0.9)


def ration(c):
    c.rect((172, 150, 110), 3, 6, 29, 28, 2)
    c.rect((206, 186, 144), 3, 6, 14, 28, 2)
    c.line(LEATHER[2], 2, (16, 6), (16, 28))
    c.line(LEATHER[2], 2, (3, 17), (29, 17))
    c.circ(LEATHER[0], 16, 17, 2.5)
    c.poly((168, 74, 62), (20, 8), (27, 8), (27, 14), (20, 14))
    c.circ(CRUST, 8, 11, 2.4)


def mushroom(c, cap_col, light, spots, stem=(236, 226, 200)):
    c.poly(stem, (12, 16), (20, 16), (22, 29), (10, 29))
    c.poly((198, 186, 158), (17, 16), (20, 16), (22, 29), (17, 29))
    c.pie(cap_col, 2, 3, 30, 31, 180, 360)
    c.rect((206, 190, 158), 3, 15.5, 29, 18.5, 1.5)
    c.pie(light, 5, 5, 18, 27, 190, 280)
    for x, y, r in spots:
        c.circ(WHITE, x, y, r)


def plain_mushroom(c):
    mushroom(c, (178, 124, 76), (214, 166, 112), ((22, 10, 1.5), (11, 12, 1)))


def poison_mushroom(c):
    mushroom(c, (150, 46, 170), (196, 96, 214), ((10, 11, 2), (17, 7, 1.8), (23, 12, 2)), (206, 226, 170))
    c.circ(GREEN[1], 6, 25, 1.5)
    c.circ(GREEN[1], 27, 24, 1.2)


def water(c):
    c.poly(WOOD[0], (5, 8), (27, 8), (24, 29), (8, 29))
    c.poly(WOOD[1], (5, 8), (11, 8), (12, 29), (8, 29))
    c.ell(BLUE[0], 5, 5, 27, 12)
    c.ell(BLUE[1], 8, 6, 19, 10)
    for y in (13, 24):
        c.line(IRON[2], 1.5, (6 + (y - 13) * 0.14, y), (26 - (y - 13) * 0.14, y))
    c.arc(IRON[1], 5, -3, 27, 17, 180, 360, 1.5)


def ingot(c):
    c.poly(IRON[2], (2, 26), (7, 13), (28, 13), (30, 26))
    c.poly(IRON[0], (7, 13), (25, 13), (30, 25), (2, 25))
    c.poly(IRON[1], (9, 5), (24, 5), (28, 13), (7, 13))
    c.poly(WHITE, (11, 7), (18, 7), (17, 11), (10, 11))
    c.poly((84, 90, 104), (3, 25), (29, 25), (28, 28), (4, 28))


# ---------- Meat
RAW = ((206, 66, 76), (240, 124, 124), (146, 36, 54))
COOKED = ((150, 88, 44), (196, 136, 76), (98, 56, 28))
FAT = (250, 214, 200)
COOKED_FAT = (232, 190, 124)


def tint(col, t):
    if t is None:
        return col
    return tuple(tuple(int(v * 0.65 + w * 0.35) for v, w in zip(a, t)) for a in col)


def steak(c, m, fat, big=False):
    b, l, d = m
    k = 0 if big else 2
    c.poly(d, (4 + k, 12), (11, 5 + k), (22, 4 + k), (29 - k, 11), (28 - k, 22), (20, 29 - k), (9, 28 - k), (4 + k, 22))
    c.poly(b, (5.5 + k, 12.5), (11.5, 6.5 + k), (21.5, 5.5 + k), (27.5 - k, 11.5), (26.5 - k, 21.5), (19.5, 27.5 - k), (9.5, 26.5 - k), (5.5 + k, 21.5))
    c.ell(l, 9, 9 + k, 19, 16 + k)
    c.arc(fat, 4 + k, 4 + k, 29 - k, 29 - k, 20, 110, 2)
    return k


def bear_meat(c, m, fat):
    steak(c, m, fat, True)
    c.line(fat, 1, (10, 18), (15, 21), (21, 17))
    c.line(fat, 1, (15, 10), (20, 13))


def t_bone(c, m, fat):
    steak(c, m, fat)
    c.line(fat, 2.2, (16, 7), (16, 26))
    c.line(fat, 2, (10, 14), (22, 14))


def drumstick(c, b, l, d, big=False, bone=(246, 236, 212)):
    c.line(bone, 3.4, (20, 21), (27, 28))
    c.circ(bone, 28.5, 26.5, 2.2)
    c.circ(bone, 25.5, 29.5, 2.2)
    r = 10.5 if big else 8.5
    c.circ(d, 13, 13, r + 1)
    c.circ(b, 12.5, 12.5, r)
    c.poly(b, (10, 21), (21, 11), (23, 22), (20, 24))
    c.circ(l, 9.5, 9.5, r * 0.42)


def chicken_meat(c, m, fat):
    drumstick(c, m[0], m[1], m[2])


def hock(c, m, fat):
    b, l, d = m
    c.poly(d, (5, 8), (22, 3), (28, 11), (24, 26), (11, 29), (4, 20))
    c.poly(b, (6, 9), (21, 5), (26, 12), (22, 24), (11, 27), (6, 19))
    c.ell(l, 9, 8, 18, 15)
    c.circ(fat, 19, 23, 4.5)
    c.circ(WHITE, 19, 23, 2.3)
    c.arc(fat, 5, 3, 28, 29, 180, 280, 1.6)


def rib_chop(c, m, fat):
    b, l, d = m
    for i, y in enumerate((6, 13, 20)):
        c.line(fat if fat != COOKED_FAT else WHITE, 2, (24, y + 1), (30, y - 1))
        c.rect(d, 3, y, 25, y + 6.5, 2.5)
        c.rect(b, 3.5, y + 0.5, 24, y + 5.5, 2.2)
        c.rect(l, 6, y + 1.3, 15, y + 2.8, 0.7)


def loin(c, m, fat):
    b, l, d = m
    c.ell(d, 2, 9, 30, 25)
    c.ell(b, 3, 9.5, 29, 24)
    c.ell(l, 6, 11, 19, 16)
    for x in (10, 16, 22):
        c.line(LEATHER[2], 1.4, (x, 9), (x, 25))


def haunch(c, m, fat, small=True):
    b, l, d = m
    c.poly(d, (7, 8), (17, 5), (24, 11), (24, 20), (15, 25), (7, 19))
    c.poly(b, (8, 9), (16.5, 6.5), (22.5, 11.5), (22.5, 19), (15, 23.5), (8, 18))
    c.circ(l, 12, 11, 3)
    c.line(fat if fat != COOKED_FAT else WHITE, 2.4, (19, 21), (26, 28))
    c.circ(fat if fat != COOKED_FAT else WHITE, 27, 28.5, 1.8)


def fish_meat(c, m, fat):
    fish_shape(c, m[0], m[1], m[2], eye=False)
    for x in (9, 13, 17, 21):
        c.line(fat, 0.9, (x, 11), (x - 2, 21))


def claw(c, m, fat):
    b, l, d = m
    c.line(d, 6, (4, 28), (13, 19))
    c.line(b, 4.4, (4, 28), (13, 19))
    c.circ(fat, 3.5, 28.5, 2.4)
    c.ell(d, 8, 9, 23, 24)
    c.ell(b, 9, 10, 22, 23)
    c.poly(d, (13, 12), (19, 2), (31, 5), (24, 8), (21, 14))
    c.poly(b, (14, 12), (19.5, 3.5), (28, 5.5), (23, 7.5), (20.5, 13))
    c.poly(d, (21, 15), (26, 11.5), (31.5, 18), (24, 24), (19, 22))
    c.poly(b, (21.5, 16), (26, 13), (30, 18), (24, 22.5), (20, 21))
    c.ell(l, 11, 12, 16, 17)


def lizard_tail(c, m, fat):
    b, l, d = m
    c.line(d, 7, (6, 7), (13, 14), (19, 22), (29, 28))
    c.line(b, 5.2, (6, 7), (13, 14), (19, 22), (29, 28))
    c.ell(fat, 2, 3, 10, 11)
    c.circ(b, 6, 7, 2)
    for x, y in ((12, 13), (16, 18), (20, 23), (25, 26)):
        c.line(l, 1, (x - 1.5, y + 1.5), (x + 1.5, y - 1.5))


def snake_coil(c, m, fat):
    b, l, d = m
    c.ring(d, 16, 16, 13, 5.4)
    c.ring(b, 16, 16, 12.5, 4)
    c.ring(d, 16, 16, 7, 4.4)
    c.ring(b, 16, 16, 6.5, 3.2)
    c.circ(fat, 16, 16, 2.4)
    for a in range(0, 360, 40):
        r = math.radians(a)
        c.circ(l, 16 + 10.8 * math.cos(r), 16 + 10.8 * math.sin(r), 0.8)


def bacon(c, m, fat):
    b, l, d = m
    for x, sh in ((5, 0), (17, 2)):
        pts_top = [(x + i * 2, 4 + sh + (1.8 if i % 2 else -1.0)) for i in range(5)]
        pts = pts_top + [(x + 9, 28 + sh * 0), (x + 1, 28)]
        c.poly(d, *pts)
        c.poly(b, *[(px + 0.7, py + 0.7) for px, py in pts[:5]], (x + 8, 27), (x + 1.7, 27))
        c.line(fat, 1.4, (x + 2.2, 7 + sh), (x + 2.5, 26))
        c.line(fat, 1.4, (x + 6.2, 7 + sh), (x + 6.4, 26))


def lamb_chop(c, m, fat):
    b, l, d = m
    bone = WHITE if fat == COOKED_FAT else (250, 240, 224)
    c.line(bone, 3, (19, 17), (29, 5))
    c.circ(d, 13, 19, 10)
    c.circ(b, 12.5, 18.5, 8.8)
    c.arc(fat, 3, 9, 23, 29, 70, 250, 2.4)
    c.circ(l, 13, 17, 3.4)
    c.circ(bone, 15, 21, 1.8)


def duck_breast(c, m, fat):
    b, l, d = m
    c.pie(d, 3, 4, 29, 34, 180, 360)
    c.pie(b, 4.5, 5.5, 27.5, 32, 180, 360)
    c.rect(fat, 3, 18, 29, 21.5, 1.5)
    for x in (9, 14, 19, 24):
        c.line(d, 1, (x - 2, 8.5), (x + 2, 16))


def turtle_meat(c, m, fat):
    s = ((88, 128, 70), (130, 168, 96), (54, 88, 46))
    c.ell(s[2], 2, 5, 30, 28)
    c.ell(s[0], 3, 5.5, 29, 26)
    c.ell(m[2], 7, 9, 25, 23)
    c.ell(m[0], 8, 9.5, 24, 22)
    c.ell(m[1], 10, 11, 18, 15)
    c.arc(s[1], 3, 5.5, 29, 26, 190, 290, 1.5)


def bat_wing(c, m, fat):
    b, l, d = m
    c.poly(b, (3, 5), (29, 3), (30, 13), (26, 22), (21, 16), (17, 27), (12, 18), (6, 24))
    c.poly(l, (3, 5), (29, 3), (28, 8), (6, 11))
    bone = fat if fat != COOKED_FAT else WHITE
    c.line(bone, 1.3, (3, 5), (29, 3))
    c.line(bone, 1, (8, 5), (6, 23))
    c.line(bone, 1, (15, 4.5), (17, 26))
    c.line(bone, 1, (22, 4), (26, 21))


def frog_legs(c, m, fat):
    b, l, d = m
    for s in (1, -1):
        fx = lambda x: 16 + s * x
        c.line(d, 5.4, (fx(1), 7), (fx(9), 13), (fx(4), 21))
        c.line(b, 4, (fx(1), 7), (fx(9), 13), (fx(4), 21))
        c.line(d, 2.6, (fx(4), 21), (fx(11), 26))
        c.line(l, 1.4, (fx(4), 21), (fx(11), 26))
        c.poly(d, (fx(10), 25), (fx(15), 24), (fx(14), 30))
    c.circ(fat, 16, 6, 2.6)


def big_haunch(c, m, fat, mark):
    b, l, d = m
    bone = WHITE
    c.line(bone, 4, (22, 22), (29, 29))
    c.circ(bone, 30, 27, 2.2)
    c.circ(bone, 27, 30, 2.2)
    c.circ(d, 13, 13, 12.5)
    c.circ(b, 12.5, 12.5, 11.2)
    c.poly(b, (10, 23), (23, 10), (26, 24), (23, 26))
    c.circ(l, 9, 9, 4.6)
    star(c, mark, 16, 16, 4.5, 0.3, 4)


def gorlicorn_meat(c, m, fat):
    big_haunch(c, tint(m, (150, 80, 220)), fat, (230, 190, 255))


def griffin_meat(c, m, fat):
    big_haunch(c, tint(m, (240, 180, 60)), fat, (255, 240, 170))


def manticore_meat(c, m, fat):
    b, l, d = tint(m, (190, 30, 30))
    big_haunch(c, (b, l, d), fat, (255, 120, 90))


MEAT = {
    'Bear': (bear_meat, (120, 50, 50)), 'Cow': (t_bone, None), 'Chicken': (chicken_meat, (250, 190, 170)),
    'Boar': (hock, (140, 60, 60)), 'Wolf': (rib_chop, (170, 70, 90)), 'Deer': (loin, (120, 40, 50)),
    'Rabbit': (haunch, (250, 170, 160)), 'Fish': (fish_meat, (250, 150, 110)), 'Crab': (claw, (240, 110, 70)),
    'Lizard': (lizard_tail, (140, 160, 90)), 'Snake': (snake_coil, (150, 120, 90)), 'Pig': (bacon, (250, 170, 170)),
    'Sheep': (lamb_chop, (220, 110, 110)), 'Duck': (duck_breast, (180, 70, 70)), 'Turtle': (turtle_meat, (210, 120, 100)),
    'Bat': (bat_wing, (90, 50, 80)), 'Frog': (frog_legs, (200, 190, 130)),
    'Gorlicorn': (gorlicorn_meat, None), 'Griffin': (griffin_meat, None), 'Manticore': (manticore_meat, None),
}


def meat_item(c, animal, raw):
    fn, t = MEAT[animal]
    base = RAW if raw else COOKED
    if t is not None and raw:
        base = tint(base, t)
    fn(c, base, FAT if raw else COOKED_FAT)
    if not raw and fn not in (gorlicorn_meat, griffin_meat, manticore_meat):
        star(c, (255, 226, 150), 27, 5, 2.4, 0.25, 4)


# ---------- Bones
B0, B1, B2 = BONE


def bone_seg(c, a, b, w=2.8, knob=2.2):
    c.line(B2, w + 1, a, b)
    for p in (a, b):
        dx, dy = (b[0] - a[0]), (b[1] - a[1])
        n = math.hypot(dx, dy) or 1
        px, py = -dy / n * knob * 0.75, dx / n * knob * 0.75
        c.circ(B2, p[0] + px, p[1] + py, knob + 0.5)
        c.circ(B2, p[0] - px, p[1] - py, knob + 0.5)
    c.line(B0, w, a, b)
    for p in (a, b):
        dx, dy = (b[0] - a[0]), (b[1] - a[1])
        n = math.hypot(dx, dy) or 1
        px, py = -dy / n * knob * 0.75, dx / n * knob * 0.75
        c.circ(B0, p[0] + px, p[1] + py, knob)
        c.circ(B1, p[0] - px, p[1] - py, knob)


def skull(c, x=16, y=14, r=9, jaw=True):
    c.circ(B2, x, y, r + 0.7)
    c.circ(B0, x, y, r)
    c.circ(B1, x - r * 0.3, y - r * 0.35, r * 0.45)
    if jaw:
        c.rect(B2, x - r * 0.62, y + r * 0.5, x + r * 0.62, y + r * 1.25, 1)
        c.rect(B0, x - r * 0.52, y + r * 0.5, x + r * 0.52, y + r * 1.13, 1)
    c.circ(DARK, x - r * 0.38, y + r * 0.05, r * 0.26)
    c.circ(DARK, x + r * 0.38, y + r * 0.05, r * 0.26)
    c.poly(DARK, (x, y + r * 0.3), (x - r * 0.13, y + r * 0.58), (x + r * 0.13, y + r * 0.58))


def bear_bones(c):
    bone_seg(c, (4, 28), (28, 21))
    skull(c, 16, 12, 10)
    c.circ(B0, 8, 4.5, 3)
    c.circ(B0, 24, 4.5, 3)
    for x in (11, 21):
        c.poly(WHITE, (x - 1.5, 20), (x + 1.5, 20), (x, 25))


def cow_bones(c):
    c.poly(B1, (9, 12), (2, 8), (3, 1), (6, 6), (12, 9))
    c.poly(B1, (23, 12), (30, 8), (29, 1), (26, 6), (20, 9))
    c.poly(B2, (7, 7), (16, 4), (25, 7), (22, 22), (19, 30), (13, 30), (10, 22))
    c.poly(B0, (8, 8), (16, 5), (24, 8), (21, 22), (18.5, 29), (13.5, 29), (11, 22))
    c.circ(DARK, 12, 14, 2.3)
    c.circ(DARK, 20, 14, 2.3)
    c.circ(DARK, 14.5, 26, 0.9)
    c.circ(DARK, 17.5, 26, 0.9)


def wishbone(c, col=None):
    c.line(B2, 4, (16, 27), (7, 7))
    c.line(B2, 4, (16, 27), (25, 7))
    c.line(B0, 2.8, (16, 27), (7, 7))
    c.line(B1, 2.8, (16, 27), (25, 7))
    c.circ(B0, 16, 27, 2.8)


def chicken_bones(c):
    wishbone(c)
    c.poly((240, 180, 70), (10, 28), (3, 29), (6, 25))


def boar_bones(c):
    skull(c, 16, 13, 10)
    for s in (1, -1):
        fx = lambda x: 16 + s * x
        c.poly(B2, (fx(4.5), 23), (fx(15.5), 8), (fx(9), 17.5))
        c.poly(WHITE, (fx(5), 21.5), (fx(14.5), 9.5), (fx(9), 17))
    c.rect(DARK, 13, 19, 19, 20.5)


def wolf_bones(c):
    bone_seg(c, (6, 6), (26, 26))
    bone_seg(c, (26, 6), (6, 26))
    c.poly(WHITE, (13, 13), (19, 13), (16, 25))
    c.poly(B2, (15, 13), (19, 13), (16, 25))


def deer_bones(c):
    for s in (1, -1):
        fx = lambda x: 16 + s * x
        c.line(B2, 3.4, (fx(1), 30), (fx(5), 18), (fx(11), 3))
        c.line(B2, 3, (fx(5), 18), (fx(13), 14))
        c.line(B2, 3, (fx(8), 10), (fx(5), 3))
        c.line(B0, 2.3, (fx(1), 30), (fx(5), 18), (fx(11), 3))
        c.line(B0, 2, (fx(5), 18), (fx(13), 14))
        c.line(B0, 2, (fx(8), 10), (fx(5), 3))


def rabbit_bones(c):
    c.line(B2, 5, (11, 13), (8, 2))
    c.line(B2, 5, (21, 13), (24, 2))
    c.line(B0, 3.6, (11, 13), (8, 2))
    c.line(B0, 3.6, (21, 13), (24, 2))
    skull(c, 16, 20, 7.5)


def fish_bones(c):
    c.line(B2, 2.8, (6, 16), (28, 16))
    c.line(B0, 1.8, (6, 16), (28, 16))
    for x in (11, 15, 19, 23):
        h = 7 - (x - 11) * 0.3
        c.line(B2, 1.8, (x, 16 - h), (x + 1.5, 16))
        c.line(B2, 1.8, (x, 16 + h), (x + 1.5, 16))
        c.line(B0, 1, (x, 16 - h), (x + 1.5, 16))
        c.line(B0, 1, (x, 16 + h), (x + 1.5, 16))
    c.poly(B2, (26, 16), (31, 9), (31, 23))
    c.poly(B0, (27, 16), (30.3, 11), (30.3, 21))
    c.circ(B2, 6, 16, 5.5)
    c.circ(B0, 6, 16, 4.6)
    c.circ(DARK, 5, 15, 1.3)


def crab_bones(c):
    pale = ((232, 170, 140), (250, 214, 190), (170, 110, 90))
    claw(c, pale, B1)
    c.line(pale[2], 2, (26, 26), (21, 21))


def spine(c, pts, r=2.2):
    for x, y in pts:
        c.circ(B2, x, y, r + 0.6)
    for x, y in pts:
        c.circ(B0, x, y, r)
        c.circ(B1, x - 0.5, y - 0.5, r * 0.4)


def lizard_bones(c):
    pts = [(5 + i * 2.9, 16 + 8 * math.sin(i * 0.75)) for i in range(9)]
    spine(c, pts)
    c.circ(B2, 5, 15, 4.5)
    c.circ(B0, 5, 15, 3.7)
    c.circ(DARK, 4, 14, 1)
    for x, y in pts[2:7:2]:
        c.line(B0, 1, (x - 3, y + 4), (x, y))
        c.line(B0, 1, (x + 3, y - 4), (x, y))


def snake_bones(c):
    pts = []
    for i in range(30):
        a = i * 0.52
        r = 3 + i * 0.37
        pts.append((16 + r * math.cos(a), 16 + r * math.sin(a)))
    spine(c, pts[::1], 1.2)
    x, y = pts[-1]
    c.poly(B2, (x - 3, y - 3), (x + 4, y - 2), (x, y + 4))
    c.poly(WHITE, (x - 2, y - 2), (x + 3, y - 1.5), (x, y + 3))


def pig_bones(c):
    skull(c, 16, 12, 9)
    c.ell(B2, 11, 16, 21, 24)
    c.ell(B0, 12, 17, 20, 23)
    c.circ(DARK, 14.5, 20, 1)
    c.circ(DARK, 17.5, 20, 1)
    c.poly(B1, (8, 6), (5, 1), (12, 4))
    c.poly(B1, (24, 6), (27, 1), (20, 4))
    bone_seg(c, (5, 29), (27, 29), 1.8, 1.4)


def sheep_bones(c):
    skull(c, 16, 16, 8)
    for s in (1, -1):
        cx = 16 + s * 10
        c.ring(B2, cx, 13, 6.5, 3.4)
        c.ring(B1, cx, 13, 6.2, 2.4)


def duck_bones(c):
    c.poly((178, 122, 28), (15, 10), (31, 14), (31, 20), (15, 21))
    c.poly((240, 190, 60), (15, 11), (30, 15), (30, 18.5), (15, 18))
    c.line((178, 122, 28), 0.8, (17, 16), (30, 16.5))
    skull(c, 11, 14, 9, False)
    bone_seg(c, (5, 28), (24, 28), 2, 1.5)


def turtle_bones(c):
    c.ell(B2, 2, 6, 30, 28)
    c.ell(B0, 3, 6.5, 29, 27)
    for x, y in ((16, 10), (9, 13), (23, 13), (16, 17), (9, 21), (23, 21), (16, 24)):
        star(c, B2, x, y, 3.2, 0.82, 6)
        star(c, B1, x, y, 2.3, 0.82, 6)


def bat_bones(c):
    pts = [(16, 3), (9, 8), (2, 22), (9, 8), (12, 23), (9, 8), (6, 26)]
    for s in (1, -1):
        fx = lambda x: 16 + (x - 16) * s
        for a, b in (((16, 13), (8, 5)), ((8, 5), (2, 24)), ((8, 5), (8, 25)), ((8, 5), (13, 24))):
            c.line(B2, 2.3, (fx(a[0]), a[1]), (fx(b[0]), b[1]))
        for a, b in (((16, 13), (8, 5)), ((8, 5), (2, 24)), ((8, 5), (8, 25)), ((8, 5), (13, 24))):
            c.line(B0, 1.3, (fx(a[0]), a[1]), (fx(b[0]), b[1]))
    skull(c, 16, 15, 4.5, False)
    c.poly(B1, (12.5, 12), (12, 7), (15, 10.5))
    c.poly(B1, (19.5, 12), (20, 7), (17, 10.5))


def frog_bones(c):
    c.ell(B2, 8, 2, 24, 14)
    c.ell(B0, 9, 3, 23, 13)
    c.circ(DARK, 12.5, 8, 2)
    c.circ(DARK, 19.5, 8, 2)
    spine(c, [(16, 15), (16, 18), (16, 21)], 1.5)
    for s in (1, -1):
        fx = lambda x: 16 + s * x
        c.line(B2, 2.4, (fx(1), 22), (fx(10), 17), (fx(6), 25), (fx(14), 29))
        c.line(B0, 1.4, (fx(1), 22), (fx(10), 17), (fx(6), 25), (fx(14), 29))


def gorlicorn_bones(c):
    c.poly((206, 150, 255), (13, 10), (19, 10), (16, 0))
    c.line(PURPLE[0], 0.8, (14.5, 7), (17.5, 5))
    skull(c, 16, 18, 10.5)
    c.poly(B1, (7, 11), (3, 4), (11, 8))
    c.poly(B1, (25, 11), (29, 4), (21, 8))
    star(c, PURPLE[1], 28, 22, 3, 0.25, 4)


def griffin_bones(c):
    c.circ(B2, 13, 13, 10.5)
    c.circ(B0, 13, 13, 9.5)
    c.circ(B1, 10, 9, 4)
    c.poly(GOLD[2], (19, 7), (31, 13), (29, 24), (25, 19), (19, 19))
    c.poly(GOLD[0], (19, 8), (30, 13.5), (28.5, 21), (25, 17.5), (19, 17))
    c.circ(DARK, 15, 12, 2.6)
    bone_seg(c, (3, 28), (24, 28), 2, 1.5)
    c.poly(GOLD[1], (3, 22), (7, 18), (8, 24))


def manticore_bones(c):
    skull(c, 11, 13, 9)
    for x in (7, 11, 15):
        c.poly(WHITE, (x - 1.2, 20), (x + 1.2, 20), (x, 24))
    pts = [(21, 27), (25, 26), (28, 22), (29, 17), (28, 12), (25, 8)]
    spine(c, pts, 2)
    c.poly(RED[2], (26, 10), (22.5, 7), (22, 1))
    c.poly(RED[0], (25.5, 9), (23, 7), (22.5, 3))


def bones_generic(c):
    bone_seg(c, (5, 25), (27, 19))
    bone_seg(c, (6, 13), (26, 26))
    bone_seg(c, (8, 7), (24, 12), 2.2, 1.8)


BONES = {'Bear': bear_bones, 'Cow': cow_bones, 'Chicken': chicken_bones, 'Boar': boar_bones, 'Wolf': wolf_bones, 'Deer': deer_bones,
         'Rabbit': rabbit_bones, 'Fish': fish_bones, 'Crab': crab_bones, 'Lizard': lizard_bones, 'Snake': snake_bones, 'Pig': pig_bones,
         'Sheep': sheep_bones, 'Duck': duck_bones, 'Turtle': turtle_bones, 'Bat': bat_bones, 'Frog': frog_bones,
         'Gorlicorn': gorlicorn_bones, 'Griffin': griffin_bones, 'Manticore': manticore_bones}

GEAR = {
    'Cap': cap, 'Hood': hood, 'Coif': coif, 'Helm': helm, 'Visor': visor, 'Bascinet': bascinet, 'Sallet': sallet, 'Great Helm': great_helm,
    'Circlet': circlet, 'Crown': crown, 'Tunic': tunic, 'Jerkin': jerkin, 'Lamellar Vest': lamellar, 'Brigandine': brigandine,
    'Hauberk': hauberk, 'Breastplate': breastplate, 'Cuirass': cuirass, 'Chestplate': chestplate, 'Full Plate': full_plate,
    'Ceremonial Plate': ceremonial, 'Breeches': breeches, 'Trousers': trousers, 'Leggings': leggings, 'Chausses': chausses,
    'Tassets': tassets, 'Cuisses': cuisses, 'Greaves': greaves, 'Plate Leggings': plate_leggings, 'Mail Leggings': mail_leggings,
    'Gilded Greaves': gilded_greaves, 'Sandals': sandals, 'Shoes': shoes, 'Boots': boots, 'Riding Boots': riding_boots,
    'Hobnail Boots': hobnail, 'Wrappings': wrappings, 'Marching Boots': marching, 'Sabatons': sabatons, 'Sollerets': sollerets,
    'Warboots': warboots, 'Band': band, 'Charm': charm, 'Bracelet': bracelet, 'Brooch': brooch, 'Ring': ring, 'Amulet': amulet,
    'Pendant': pendant, 'Talisman': talisman, 'Torc': torc, 'Cloak': cloak, 'Short Sword': short_sword, 'Long Sword': long_sword,
    'Axe': axe, 'Mace': mace, 'Flail': flail, 'Rapier': rapier, 'Dagger': dagger, 'Knife': knife, 'Stiletto': stiletto,
    'Buckler': buckler, 'Kite Shield': kite, 'Tower Shield': tower, 'Greatsword': greatsword, 'Warhammer': warhammer,
    'Battleaxe': battleaxe, 'Short Bow': short_bow, 'Long Bow': long_bow, 'Crossbow': crossbow, 'Wood Arrows': wood_arrows,
    'Bone Arrows': bone_arrows, 'Fire Arrows': fire_arrows, 'Crossbow Bolts': bolts,
}

PLAIN = {
    'Iron Ore': ore, 'Oak Log': oak_log, 'Healing Herb': healing_herb, 'Gold Coin': gold_coin, 'Wolf Pelt': wolf_pelt,
    'Ancient Relic': relic, 'Health Potion': health_potion, 'Mana Potion': mana_potion, 'Elixir of Might': elixir, 'Bread': bread,
    'Antidote': antidote, 'Apple': apple, 'Wild Berries': berries, 'Hard Biscuit': biscuit, 'Cheese Wedge': cheese,
    'Smoked Fish': smoked_fish, 'Honey Cake': honey_cake, 'Mutton Pie': pie, 'Stew Bowl': stew, 'Roast Boar Leg': roast_leg,
    'Trail Ration': ration, 'Wood Log': wood_log, 'Water': water, 'Iron Ingot': ingot, 'Mushroom': plain_mushroom,
    'Poison Mushroom': poison_mushroom, 'Bones': bones_generic,
}


def read_items():
    text = open(os.path.join(ROOT, 'itemdb.txt'), encoding='utf-8').read()
    parts = re.split(r'^\[(\d+)\]\s*$', text, flags=re.M)
    out = []
    for i in range(1, len(parts), 2):
        d = {}
        for line in parts[i + 1].splitlines():
            if '=' in line and not line.startswith('#'):
                k, v = line.split('=', 1)
                d[k.strip()] = v.strip()
        out.append((int(parts[i]), d.get('name', ''), d.get('material', '')))
    return out


def draw_item(c, name, material):
    if name in GEAR:
        GEAR[name](c, MATS[material])
    elif name in PLAIN:
        PLAIN[name](c)
    elif name.startswith('Raw '):
        animal = 'Fish' if name == 'Raw Fish' else name[4:-5]
        meat_item(c, animal, True)
    elif name.startswith('Cooked '):
        animal = 'Fish' if name == 'Cooked Fish' else name[7:-5]
        meat_item(c, animal, False)
    elif name.endswith(' Bones'):
        BONES[name[:-6]](c)
    else:
        raise SystemExit('no drawing for ' + name)


def main():
    items = read_items()
    cols = 16
    CELL = 72
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * CELL, rows * (CELL + 12)), (41, 18, 22))
    dr = ImageDraw.Draw(sheet)
    for i, (n, name, material) in enumerate(items):
        c = C()
        draw_item(c, name, material)
        out = finish(c)
        out.save(os.path.join(OUT, f'item_{n}.png'))
        x, y = (i % cols) * CELL + 4, (i // cols) * (CELL + 12) + 2
        sheet.paste(out, (x, y), out)
        dr.text((x, y + 64), str(n), fill=(255, 255, 255))
    os.makedirs(PREVIEW, exist_ok=True)
    sheet.save(os.path.join(PREVIEW, 'items.png'))
    print(len(items), 'items written')


if __name__ == '__main__':
    main()
