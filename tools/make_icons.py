"""Draws the game's UI icons: data/icon/icon_<n>.png, 32 x 32 pixels.

Run it with:  python tools/make_icons.py     (needs Python 3 and Pillow: pip install pillow)
Then build the game, which packs data/ into data.pak. The game only reads the finished PNGs and never runs this script.

How a picture is made:
  1. Each picture is one function that draws basic shapes (polygons, circles, lines) through the small canvas class C.
     Coordinates are on a 32 x 32 grid, whatever size the file ends up.
  2. C draws at S times that grid, so the edges come out smooth when the picture is shrunk.
  3. finish() grows the picture's solid area a little and puts that shape behind the art in a dark color. That is the
     outline, which keeps the picture readable on any background.
  4. The result is shrunk to its final size and written over the matching PNG.

What is in this file:
  - ICONS maps an icon's number to the function that draws it, in color with an outline.
  - GEAR_ICONS does the same for the eight empty gear slots, which are plain white with no outline because the game
    tints them faint.
  - The number is the id in src/game_tables.cpp, and data/icons.txt says which file each key uses.

To add an icon: write a function, add it to ICONS under the next free number, add its key to ICON_KEYS in
src/game_tables.cpp and a row to data/icons.txt, then run this script.

Running this overwrites every icon listed here, including any replaced by hand. To keep a hand-made icon, take its
number out of ICONS or GEAR_ICONS. A preview of them all is written to out/art_preview/icons.png.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, 'data', 'icon')
PREVIEW = os.path.join(REPO, 'out', 'art_preview')
S = 8

STEEL, STEEL_L, STEEL_D = (176, 186, 200), (226, 232, 240), (110, 120, 138)
WOOD, WOOD_L, WOOD_D = (150, 98, 52), (190, 134, 78), (98, 62, 32)
GOLD, GOLD_L, GOLD_D = (240, 190, 60), (255, 228, 130), (178, 122, 28)
RED, RED_L, RED_D = (214, 56, 60), (250, 120, 110), (140, 28, 40)
GREEN, GREEN_L, GREEN_D = (92, 176, 72), (150, 216, 110), (52, 118, 52)
BLUE, BLUE_L, BLUE_D = (66, 130, 220), (132, 186, 250), (40, 80, 160)
PURPLE, PURPLE_L, PURPLE_D = (150, 88, 220), (200, 150, 255), (90, 50, 150)
PAPER, PAPER_D = (238, 224, 188), (190, 168, 124)
STONE, STONE_L, STONE_D = (150, 148, 142), (192, 190, 182), (100, 98, 94)
SKIN, SKIN_D = (232, 186, 150), (190, 138, 104)
DARK = (46, 40, 50)
WHITE = (250, 250, 250)
ORANGE, YELLOW = (244, 130, 40), (255, 214, 80)
LEATHER, LEATHER_L, LEATHER_D = (138, 88, 50), (176, 122, 76), (92, 56, 30)


class C:
    def __init__(self):
        self.im = Image.new('RGBA', (32 * S, 32 * S), (0, 0, 0, 0))
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

    def line(self, col, w, *pts):
        p = [(x * S, y * S) for x, y in pts]
        self.d.line(p, fill=col, width=int(w * S), joint='curve')
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
    a = c.im.getchannel('A')
    grown = a.filter(ImageFilter.MaxFilter(11))
    base = Image.new('RGBA', c.im.size, (22, 12, 16, 0))
    base.putalpha(grown)
    base.alpha_composite(c.im)
    return base.resize((32, 32), Image.LANCZOS)


def equipment(c):  # breastplate
    c.poly(STEEL, (6, 6), (11, 4), (16, 7), (21, 4), (26, 6), (28, 12), (24, 15), (24, 25), (16, 29), (8, 25), (8, 15), (4, 12))
    c.poly(STEEL_L, (6, 6), (11, 4), (16, 7), (16, 29), (8, 25), (8, 15), (4, 12))
    c.line(STEEL_D, 1.2, (16, 8), (16, 28))
    c.line(STEEL_D, 1.2, (9, 17), (23, 17))
    c.line(GOLD, 1.6, (11, 5), (16, 8), (21, 5))


def inventory(c):  # backpack
    c.arc(LEATHER_D, 11, 2, 21, 12, 180, 360, 2)
    c.rect(LEATHER, 6, 7, 26, 29, 4)
    c.rect(LEATHER_L, 6, 7, 26, 15, 4)
    c.rect(LEATHER_D, 9, 18, 23, 26, 2)
    c.rect(LEATHER_L, 10, 19, 22, 25, 1)
    c.rect(GOLD, 14.5, 13, 17.5, 20, 1)


def behavior(c):  # brain
    pink, light, dark = (236, 130, 150), (250, 176, 186), (176, 72, 100)
    for x, y, r in ((10, 12, 6), (16, 9, 6.5), (22, 12, 6), (8, 18, 5), (24, 18, 5), (13, 20, 6), (20, 20, 6)):
        c.circ(pink, x, y, r)
    c.circ(light, 12, 9, 3)
    c.circ(light, 21, 9, 2.5)
    c.line(dark, 1.3, (16, 5), (16, 24))
    c.line(dark, 1.2, (8, 14), (12, 16), (10, 20))
    c.line(dark, 1.2, (24, 14), (20, 16), (22, 20))
    c.rect(dark, 14, 24, 18, 29, 1)


def game_log(c):  # scroll
    c.rect(PAPER, 8, 5, 24, 27)
    c.rect(PAPER_D, 5, 3, 27, 8, 2.5)
    c.rect(PAPER_D, 5, 24, 27, 29, 2.5)
    c.rect(WOOD, 4, 4.5, 6.5, 6.5)
    c.rect(WOOD, 25.5, 4.5, 28, 6.5)
    c.rect(WOOD, 4, 25.5, 6.5, 27.5)
    c.rect(WOOD, 25.5, 25.5, 28, 27.5)
    for y, x in ((11, 21), (15, 19), (19, 21)):
        c.line(WOOD_D, 1.5, (11, y), (x, y))


def person(c, x, y, s, body, head=SKIN):
    c.pie(body, x - 6 * s, y + 3 * s, x + 6 * s, y + 17 * s, 180, 360)
    c.circ(head, x, y, 4 * s)


def squad(c):  # three people
    person(c, 7.5, 12, 0.85, BLUE_D)
    person(c, 24.5, 12, 0.85, BLUE_D)
    person(c, 16, 11, 1.15, BLUE)


def unit_panel(c):  # one person, bust
    c.pie(RED, 6, 17, 26, 41, 180, 360)
    c.circ(SKIN, 16, 11, 7)
    c.pie(WOOD_D, 9, 3.5, 23, 15, 180, 360)


def strength(c):  # dumbbell
    c.rect(STEEL_L, 9, 14, 23, 18)
    c.rect(DARK, 3, 8, 7, 24, 1)
    c.rect(STEEL_D, 7, 5, 11, 27, 1)
    c.rect(DARK, 25, 8, 29, 24, 1)
    c.rect(STEEL_D, 21, 5, 25, 27, 1)


def dexterity(c):  # open hand
    for x, top, h in ((9, 8, 10), (13.5, 4, 14), (18, 3.5, 14), (22.5, 7, 11)):
        c.rect(SKIN, x - 1.8, top, x + 1.8, top + h, 1.8)
    c.rect(SKIN, 7, 14, 25, 26, 4)
    c.line(SKIN, 3.6, (25, 20), (29, 13))
    c.rect(SKIN_D, 9, 26, 23, 30, 1)


def vitality(c):  # heart
    c.circ(RED, 10.5, 12, 6.5)
    c.circ(RED, 21.5, 12, 6.5)
    c.poly(RED, (4.4, 14.5), (27.6, 14.5), (16, 29))
    c.circ(RED_L, 9.5, 10.5, 2.5)


def agility(c):  # winged boot
    c.poly(LEATHER, (12, 7), (20, 7), (20, 19), (28, 22), (28, 28), (12, 28))
    c.rect(LEATHER_D, 12, 25.5, 28, 28)
    c.rect(GOLD, 11.5, 6, 20.5, 9)
    c.poly(WHITE, (12, 10), (2, 5), (4, 10), (1, 11), (4, 15), (2, 17), (12, 19))
    c.line((190, 200, 220), 1, (5, 9), (11, 12))
    c.line((190, 200, 220), 1, (5, 14), (11, 16))


def intelligence(c):  # open book
    c.poly(BLUE_D, (2, 8), (16, 10), (30, 8), (30, 27), (16, 29), (2, 27))
    c.poly(PAPER, (4, 6), (16, 9), (16, 26), (4, 24))
    c.poly(WHITE, (28, 6), (16, 9), (16, 26), (28, 24))
    for i in (0, 1, 2):
        c.line(PAPER_D, 1, (6.5, 11 + i * 4), (13.5, 12.8 + i * 4))
        c.line(PAPER_D, 1, (25.5, 11 + i * 4), (18.5, 12.8 + i * 4))


def wisdom(c):  # owl
    brown, light = (128, 92, 62), (214, 184, 140)
    c.poly(brown, (6, 3), (11, 8), (21, 8), (26, 3), (26, 20), (16, 30), (6, 20))
    c.ell(brown, 5, 7, 27, 29)
    c.ell(light, 10, 16, 22, 29)
    c.circ(WHITE, 11, 13, 4.8)
    c.circ(WHITE, 21, 13, 4.8)
    c.circ(DARK, 11.5, 13, 2.2)
    c.circ(DARK, 20.5, 13, 2.2)
    c.poly(GOLD, (14, 16), (18, 16), (16, 20.5))


def charisma(c):  # speech bubble with a star
    c.rect(GOLD, 3, 4, 29, 22, 6)
    c.poly(GOLD, (9, 20), (17, 20), (8, 29))
    star(c, WHITE, 16, 13, 7)


def melee(c):  # crossed swords
    for flip in (False, True):
        fx = (lambda x: 32 - x) if flip else (lambda x: x)
        col = STEEL_L if flip else STEEL
        c.poly(col, (fx(26), 3), (fx(29), 3), (fx(29), 6), (fx(11), 24), (fx(8), 21))
        c.line(GOLD_D, 2.2, (fx(6), 18), (fx(14), 26))
        c.line(WOOD_D, 2.6, (fx(9), 23), (fx(5), 27))
        c.circ(GOLD, fx(4.5), 27.5, 1.8)


def projectiles(c):  # bow and arrow
    c.arc(WOOD, 2, 3, 26, 29, 270, 90, 2.6)
    c.line(WHITE, 0.8, (14, 3.5), (14, 28.5))
    c.line(WOOD_L, 1.5, (4, 16), (24, 16))
    c.poly(STEEL_L, (24, 12.5), (31, 16), (24, 19.5))
    c.poly(RED, (4, 16), (1, 12.5), (6.5, 12.5), (9, 16), (6.5, 19.5), (1, 19.5))


def arcane(c):  # crystal orb with sparks
    c.rect(WOOD_D, 9, 25, 23, 29, 1)
    c.poly(WOOD, (11, 22), (21, 22), (23, 26), (9, 26))
    c.circ(PURPLE, 16, 14, 11)
    c.circ(PURPLE_L, 14, 12, 7)
    star(c, WHITE, 16, 14, 7, 0.28, 4)
    star(c, YELLOW, 25, 5, 4, 0.28, 4)
    star(c, YELLOW, 6, 7, 3, 0.28, 4)


def block(c):  # shield
    c.poly(STEEL, (16, 2), (28, 6), (28, 16), (24, 24), (16, 30), (8, 24), (4, 16), (4, 6))
    c.poly(BLUE, (16, 5), (25, 8), (25, 16), (22, 22), (16, 26.5), (10, 22), (7, 16), (7, 8))
    c.poly(BLUE_L, (16, 5), (16, 26.5), (10, 22), (7, 16), (7, 8))
    c.line(GOLD, 2, (16, 8), (16, 23))
    c.line(GOLD, 2, (10, 14), (22, 14))


def mining(c):  # pickaxe
    c.line(WOOD, 3.2, (8, 28), (21, 10))
    c.poly(STEEL, (3, 11), (11, 4), (20, 3), (27, 6), (30, 15), (27, 22), (24, 11), (17, 7), (10, 7))
    c.poly(STEEL_L, (11, 4), (20, 3), (27, 6), (24, 8), (17, 6), (10, 7))
    c.rect(STEEL_D, 18, 5, 23, 11, 1)


def herbalism(c):  # herb sprig
    c.line(GREEN_D, 1.8, (14, 30), (16, 18), (17, 5))
    c.poly(GREEN, (16, 21), (5, 22), (2, 14), (10, 13))
    c.poly(GREEN_L, (16, 21), (10, 13), (2, 14))
    c.poly(GREEN, (16, 17), (27, 19), (30, 10), (22, 9))
    c.poly(GREEN_L, (16, 17), (22, 9), (30, 10))
    c.poly(GREEN, (17, 10), (10, 8), (10, 2), (16, 2))
    c.poly(GREEN_L, (17, 10), (16, 2), (22, 2), (22, 7))


def woodcutting(c):  # axe in a log
    c.ell(WOOD, 3, 18, 29, 30)
    c.ell(WOOD_L, 21, 19, 29, 29)
    c.ring(WOOD_D, 25, 24, 2, 1)
    c.line(WOOD_D, 3, (9, 20), (19, 4))
    c.poly(STEEL, (12, 15), (3, 7), (2, 15), (8, 21))
    c.poly(STEEL_L, (6, 10), (3, 7), (2, 15), (5, 18))
    c.line(WOOD_L, 2, (15, 11), (20, 3))


def skinning(c):  # hide and knife
    pelt = (186, 136, 88)
    c.poly(pelt, (5, 3), (11, 7), (21, 7), (27, 3), (24, 12), (25, 22), (28, 29), (20, 26), (12, 26), (4, 29), (7, 22), (8, 12))
    c.poly((150, 104, 64), (11, 9), (21, 9), (22, 22), (10, 22))
    c.line(WOOD_D, 3, (5, 27), (11, 21))
    c.poly(STEEL_L, (10, 22), (25, 6), (27, 11), (14, 25))


def farming(c):  # wheat
    for dx, lean in ((-7, -4), (0, 0), (7, 4)):
        x0, x1 = 16 + dx * 0.4, 16 + dx + lean * 0.5
        c.line(GOLD_D, 1.4, (x0, 30), (x1, 7))
        for i in range(4):
            y = 7 + i * 4
            t = i / 4.0
            x = x1 + (x0 - x1) * t * 0.5
            c.ell(GOLD, x - 3.6, y - 1.6, x - 0.2, y + 2)
            c.ell(GOLD_L, x + 0.2, y - 1.6, x + 3.6, y + 2)
        c.ell(GOLD_L, x1 - 1.6, 2.5, x1 + 1.6, 8)


def cooking(c):  # pot with steam
    for x in (10, 16, 22):
        c.line(WHITE, 1.5, (x, 11), (x + 1.8, 8), (x - 0.8, 5), (x + 0.8, 2.5))
    c.rect(DARK, 3, 15, 8, 18, 1)
    c.rect(DARK, 24, 15, 29, 18, 1)
    c.rect((88, 86, 100), 6, 13, 26, 28, 4)
    c.rect((132, 130, 146), 6, 13, 12, 26, 3)
    c.rect(STEEL_L, 5, 12, 27, 15, 1)


def flame(c, x, y, r):
    c.poly(ORANGE, (x, y - r * 1.5), (x + r * 0.9, y - r * 0.3), (x + r, y + r * 0.4), (x + r * 0.5, y + r), (x - r * 0.5, y + r), (x - r, y + r * 0.4), (x - r * 0.9, y - r * 0.4), (x - r * 0.4, y - r * 0.5))
    c.poly(YELLOW, (x, y - r * 0.5), (x + r * 0.5, y + r * 0.3), (x + r * 0.3, y + r), (x - r * 0.3, y + r), (x - r * 0.5, y + r * 0.3))


def smelting(c):  # ingot over flame
    flame(c, 16, 11, 8.5)
    c.poly((206, 96, 40), (3, 29), (8, 18), (24, 18), (29, 29))
    c.poly((250, 160, 70), (8, 18), (24, 18), (26, 22), (6, 22))
    c.poly((255, 214, 130), (10, 19), (20, 19), (19, 21), (9, 21))
    c.rect((150, 62, 30), 3, 27, 29, 30, 1)


def forging(c):  # anvil and hammer
    c.poly(STEEL_D, (3, 15), (12, 15), (12, 14), (29, 14), (29, 19), (22, 21), (21, 26), (26, 30), (8, 30), (13, 26), (12, 21), (8, 20))
    c.poly(STEEL, (3, 15), (12, 14), (29, 14), (29, 17), (9, 17))
    c.line(WOOD, 2.4, (24, 11), (13, 3))
    c.rect(STEEL_L, 7, 3, 16, 9, 1)
    star(c, YELLOW, 27, 6, 3.5, 0.3, 4)


def carpentry(c):  # handsaw
    c.poly(STEEL_L, (2, 22), (24, 5), (28, 12), (9, 28))
    for i in range(6):
        x = 9 + i * 3.2
        y = 28 - i * 2.7
        c.poly(STEEL_D, (x - 2.6, y - 0.8), (x + 0.6, y - 3.5), (x + 1.8, y + 0.8))
    c.poly(WOOD, (21, 3), (30, 2), (31, 13), (26, 15))
    c.poly((0, 0, 0, 0), (25, 6), (28, 5.5), (28.5, 10), (26.5, 10.5))


def stoneworking(c):  # stacked blocks and a chisel
    c.rect(STONE, 3, 21, 15, 30, 1)
    c.rect(STONE_D, 16, 21, 29, 30, 1)
    c.rect(STONE_L, 9, 12, 23, 20.5, 1)
    c.line(STEEL_L, 2.6, (18, 12), (25, 4))
    c.line(WOOD, 3.4, (24, 5), (28, 1.5))
    c.line(STONE_D, 1, (5, 24), (12, 24))
    star(c, YELLOW, 13, 7, 3.2, 0.3, 4)


def lockpicking(c):  # padlock and pick
    c.arc(STEEL, 7, 3, 21, 19, 180, 360, 3)
    c.rect(STEEL, 7, 10, 10, 15)
    c.rect(STEEL, 18, 10, 21, 15)
    c.rect(GOLD, 4, 13, 24, 29, 2)
    c.rect(GOLD_L, 4, 13, 24, 17, 2)
    c.circ(DARK, 14, 20, 2.5)
    c.rect(DARK, 12.8, 20, 15.2, 25)
    c.line(STEEL_L, 1.8, (14, 21), (24, 15), (30, 11))
    c.line(STEEL_L, 1.8, (30, 11), (30, 8))


def thievery(c):  # coin pouch and mask
    mask, rim = (84, 74, 104), (132, 120, 156)
    c.poly(mask, (2, 6), (9, 4), (16, 7), (23, 4), (30, 6), (29, 14), (23, 18), (17, 15), (15, 15), (9, 18), (3, 14))
    c.poly(rim, (2, 6), (9, 4), (16, 7), (23, 4), (30, 6), (30, 8), (23, 6), (16, 9), (9, 6), (2, 8))
    c.ell(WHITE, 6, 8, 13, 13)
    c.ell(WHITE, 19, 8, 26, 13)
    c.circ(DARK, 10, 10.5, 1.5)
    c.circ(DARK, 22, 10.5, 1.5)
    c.circ(GOLD_D, 16, 24, 7)
    c.circ(GOLD, 15.5, 23.5, 6)
    c.circ(GOLD_L, 14, 22, 2.5)
    c.line(GOLD_D, 1.3, (16, 20.5), (16, 26.5))


def medical(c):  # first aid kit
    c.arc(STEEL_D, 11, 2, 21, 12, 180, 360, 2)
    c.rect(WHITE, 3, 8, 29, 29, 3)
    c.rect((206, 206, 216), 3, 25, 29, 29, 2)
    c.rect(RED, 13, 11, 19, 26)
    c.rect(RED, 8, 15.5, 24, 21.5)


def identity(c):  # name card with a portrait
    c.rect(PAPER, 2, 6, 30, 26, 2)
    c.rect(BLUE, 2, 6, 30, 10, 2)
    c.rect(BLUE_D, 5, 12, 14, 23, 1)
    c.circ(SKIN, 9.5, 15.5, 2.5)
    c.pie(RED, 6, 18, 13, 28, 180, 360)
    for y, x in ((14, 26), (18, 24), (22, 26)):
        c.line(PAPER_D, 1.4, (17, y), (x, y))


def stats_tab(c):  # bar chart
    c.rect(GREEN, 5, 18, 11, 28, 1)
    c.rect(YELLOW, 13, 11, 19, 28, 1)
    c.rect(RED, 21, 4, 27, 28, 1)
    c.line(WHITE, 1.6, (3, 3), (3, 29), (29, 29))


def gear_tab(c):  # sword and shield
    c.poly(STEEL, (22, 2), (26, 2), (26, 6), (11, 21), (7, 17))
    c.poly(STEEL_L, (22, 2), (26, 2), (9, 19), (7, 17))
    c.line(GOLD_D, 2.2, (6, 14), (13, 22))
    c.line(WOOD_D, 2.6, (8, 20), (4, 24))
    c.poly(BLUE, (20, 12), (29, 15), (29, 22), (24.5, 29), (20, 22))
    c.poly(BLUE, (20, 12), (11, 15), (11, 22), (15.5, 29), (20, 31), (24.5, 29), (20, 22))
    c.poly(BLUE_L, (20, 12), (11, 15), (11, 22), (15.5, 29), (20, 31))
    c.line(GOLD, 1.6, (20, 14), (20, 29))


def items_tab(c):  # treasure chest
    c.rect(WOOD, 3, 15, 29, 29, 1)
    c.pie(WOOD_L, 3, 5, 29, 25, 180, 360)
    c.rect(GOLD_D, 3, 14, 29, 16.5)
    c.rect(GOLD_D, 6, 7.5, 9, 29)
    c.rect(GOLD_D, 23, 7.5, 26, 29)
    c.rect(GOLD, 13, 13, 19, 21, 1)
    c.circ(DARK, 16, 16.5, 1.3)


def graphics(c):  # monitor with a landscape
    c.rect(DARK, 2, 4, 30, 24, 2)
    c.rect(BLUE_L, 4, 6, 28, 22)
    c.poly(GREEN, (4, 22), (4, 17), (11, 10), (17, 17), (21, 13), (28, 20), (28, 22))
    c.circ(YELLOW, 22, 9.5, 2.3)
    c.rect(STEEL_D, 13, 24, 19, 28)
    c.rect(STEEL, 9, 27, 23, 30, 1)


def interface(c):  # window with title bar and slider
    c.rect(STEEL_L, 3, 5, 29, 27, 2)
    c.rect(BLUE, 3, 5, 29, 11, 2)
    c.rect(RED, 23.5, 6.5, 27, 9.5)
    c.line(STEEL_D, 1.5, (7, 16), (25, 16))
    c.circ(BLUE_D, 18, 16, 2.4)
    c.line(STEEL_D, 1.5, (7, 22), (25, 22))
    c.circ(BLUE_D, 11, 22, 2.4)


def controls(c):  # computer mouse
    c.rect(STEEL_L, 7, 3, 25, 29, 9)
    c.line(STEEL_D, 1.3, (7.5, 13), (24.5, 13))
    c.line(STEEL_D, 1.3, (16, 3.5), (16, 13))
    c.rect(BLUE, 14, 6, 18, 11, 1.5)


def audio_tab(c):  # speaker with sound waves
    c.poly(STEEL_L, (3, 12), (9, 12), (17, 5), (17, 27), (9, 20), (3, 20))
    c.poly(STEEL_D, (3, 16), (17, 16), (17, 27), (9, 20), (3, 20))
    c.arc(BLUE_L, 12, 10, 24, 22, 305, 55, 2.2)
    c.arc(BLUE_L, 9, 5, 29, 27, 305, 55, 2.2)


def language(c):  # globe
    c.circ(BLUE, 16, 16, 13)
    c.poly(GREEN, (9, 6), (17, 5), (19, 10), (14, 14), (15, 19), (11, 21), (6, 15), (5, 10))
    c.poly(GREEN, (20, 15), (27, 14), (28, 19), (24, 26), (20, 23))
    c.ring(WHITE, 16, 16, 13, 1.2)


# ---------- Empty gear slots: plain white with no outline, because the game draws them faint (theme::EMPTY_SLOT)
def gear_points(*pts):
    return [(x * S, y * S) for x, y in pts]


def gear_head(d):
    d.pieslice([6 * S, 5 * S, 26 * S, 27 * S], 180, 360, fill='white')
    d.rectangle([6 * S, 16 * S, 26 * S, 19 * S], fill='white')
    d.rectangle([14 * S, 19 * S, 18 * S, 27 * S], fill='white')
    d.rectangle([6 * S, 19 * S, 10 * S, 26 * S], fill='white')
    d.rectangle([22 * S, 19 * S, 26 * S, 26 * S], fill='white')


def gear_torso(d):
    d.polygon(gear_points((10, 4), (13, 7), (19, 7), (22, 4), (29, 8), (26, 15), (23, 13), (23, 28), (9, 28), (9, 13), (6, 15), (3, 8)), fill='white')


def gear_main_hand(d):
    d.polygon(gear_points((27, 3), (29, 5), (13, 22), (10, 19)), fill='white')
    d.polygon(gear_points((6, 17), (8, 15), (17, 24), (15, 26)), fill='white')
    d.polygon(gear_points((10, 22), (12, 24), (6, 30), (3, 27)), fill='white')


def gear_off_hand(d):
    d.polygon(gear_points((16, 3), (27, 7), (27, 16), (24, 23), (16, 29), (8, 23), (5, 16), (5, 7)), fill='white')


def gear_legs(d):
    d.polygon(gear_points((8, 3), (24, 3), (26, 29), (19, 29), (16, 12), (13, 29), (6, 29)), fill='white')


def gear_feet(d):
    d.polygon(gear_points((9, 4), (18, 4), (18, 18), (28, 22), (28, 28), (9, 28)), fill='white')


def gear_ring(d):
    d.ellipse([7 * S, 10 * S, 25 * S, 28 * S], outline='white', width=4 * S)
    d.polygon(gear_points((16, 2), (21, 7), (16, 13), (11, 7)), fill='white')


def gear_amulet(d):
    d.line([(7 * S, 3 * S), (16 * S, 16 * S), (25 * S, 3 * S)], fill='white', width=3 * S, joint='curve')
    d.ellipse([9 * S, 14 * S, 23 * S, 28 * S], fill='white')


GEAR_ICONS = {44: gear_head, 45: gear_torso, 46: gear_main_hand, 47: gear_off_hand, 48: gear_legs, 49: gear_feet,
              50: gear_ring, 51: gear_amulet}


ICONS = {
    1: equipment, 2: inventory, 3: behavior, 4: game_log, 5: squad,
    6: strength, 7: dexterity, 8: vitality, 10: agility, 11: intelligence, 12: wisdom, 13: charisma,
    17: melee, 18: projectiles, 19: arcane, 20: block,
    21: mining, 22: herbalism, 23: woodcutting, 24: skinning, 25: farming,
    26: cooking, 27: smelting, 28: forging, 29: carpentry, 30: stoneworking,
    31: lockpicking, 32: thievery, 33: medical,
    34: identity, 35: stats_tab, 36: gear_tab, 37: items_tab,
    38: graphics, 39: interface, 40: controls, 41: audio_tab, 42: unit_panel, 43: language,
}

def main():
    ids = sorted(ICONS)
    cols = 10
    SHOW = 72
    rows = (len(ids) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (SHOW + 44), rows * (SHOW + 22)), (41, 18, 22))
    dr = ImageDraw.Draw(sheet)
    for i, n in enumerate(ids):
        c = C()
        ICONS[n](c)
        out = finish(c)
        out.save(os.path.join(OUT, f'icon_{n}.png'))
        x, y = (i % cols) * (SHOW + 44) + 4, (i // cols) * (SHOW + 22) + 2
        big = out.resize((SHOW, SHOW), Image.BICUBIC)
        sheet.paste(big, (x, y), big)
        sheet.paste(out, (x + SHOW + 4, y + 20), out)
        dr.text((x, y + SHOW + 2), f'{n} {ICONS[n].__name__}', fill=(255, 255, 255))
    os.makedirs(PREVIEW, exist_ok=True)
    sheet.save(os.path.join(PREVIEW, 'icons.png'))
    for n, draw in GEAR_ICONS.items():
        im = Image.new('RGBA', (32 * S, 32 * S), (255, 255, 255, 0))
        draw(ImageDraw.Draw(im))
        im.resize((32, 32), Image.LANCZOS).save(os.path.join(OUT, f'icon_{n}.png'))
    print(len(ids) + len(GEAR_ICONS), 'icons written')


if __name__ == '__main__':
    main()
