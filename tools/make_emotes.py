"""Draws the game's emotes, the small bubbles over a unit's head: data/emote/emote_<n>.png, 64 x 64 pixels.

Run it with:  python tools/make_emotes.py     (needs Python 3 and Pillow: pip install pillow)
Then build the game, which packs data/ into data.pak. The game only reads the finished PNGs and never runs this script.

How an emote is made:
  1. bubble() draws a speech bubble with a tail, in the color of the emote's type: STATUS (white, what a unit is doing),
     WARN (yellow, needs a look), ALARM (red, trouble) or CUE (green, something you can do).
  2. The emote's own function draws its symbol on top, on the same 32 x 32 grid as the other art scripts. It uses the
     canvas class C and the colors from make_items.py.
  3. A dark outline is added and the picture is shrunk to 64 x 64.

64 x 64 is deliberate. The game draws emotes about 28 pixels wide with no mipmaps, so a much larger source looks jagged.

EMOTES lists each emote's number, function and bubble color. The number is the id in src/emotes.h, and data/emotes.txt
says which file each key uses. To add an emote: write a function, add it to EMOTES under the next free number, add it
to src/emotes.h, EMOTE_KEYS in src/game_tables.cpp and data/emotes.txt, then run this script.

Running this overwrites every emote listed here, including any replaced by hand. A preview of them all, over grass,
sand and dark ground at large and actual size, is written to out/art_preview/emotes.png.
"""
import os
from PIL import Image, ImageDraw, ImageFilter

import make_items as art
from make_items import C, star, DARK, GOLD, GREEN, IRON, LEATHER, RED, STEEL, WHITE, WOOD

art.S = 16   # emotes are drawn larger, to shrink further
OUT = os.path.join(art.ROOT, 'emote')
SIZE = 64

STATUS = ((252, 250, 244), (206, 200, 190))
WARN = ((255, 226, 130), (214, 160, 60))
ALARM = ((255, 170, 150), (206, 84, 70))
CUE = ((170, 232, 170), (84, 166, 96))


def bubble(c, tone):
    fill, shade = tone
    c.poly(shade, (11, 23), (21, 23), (16, 31))
    c.rect(shade, 1.5, 1.5, 30.5, 25.5, 6)
    c.poly(fill, (12, 22), (20, 22), (16, 29))
    c.rect(fill, 1.5, 1.5, 30.5, 24, 6)


def mining(c):
    c.line(WOOD[0], 2.6, (9, 22), (19, 8))
    c.poly(IRON[2], (5, 9), (11, 4.5), (19, 3.5), (25, 6), (27, 13), (25, 18), (23, 10), (17, 7), (10, 7.5))
    c.poly(IRON[0], (11, 5), (19, 4.5), (24, 6.5), (22, 8), (17, 6), (10, 6.8))


def woodcutting(c):
    c.line(WOOD[0], 2.8, (10, 22.5), (20, 4.5))
    c.poly(IRON[2], (16, 8), (7, 3.5), (4, 11), (6, 18), (13, 14))
    c.poly(IRON[1], (15, 9), (8, 5), (5.5, 11), (7, 16), (12.5, 13))
    c.ell(WOOD[1], 19, 15, 28, 22.5)
    c.oval_ring(WOOD[2], 21.5, 17, 25.5, 20.5, 0.8)


def cooking(c):
    for x in (11, 16, 21):
        c.line((150, 150, 160), 1.2, (x, 9), (x + 1.2, 6.5), (x - 0.6, 4.5))
    c.rect(DARK, 5, 12, 8, 14.5, 0.8)
    c.rect(DARK, 24, 12, 27, 14.5, 0.8)
    c.rect((88, 86, 100), 7, 11, 25, 22.5, 3)
    c.rect((132, 130, 146), 7, 11, 12, 21, 2.5)
    c.rect(STEEL[0], 6, 10, 26, 12.5, 1)


def encumbered(c):
    c.poly(LEATHER[2], (12, 8), (20, 8), (23, 4), (9, 4))
    c.ell(LEATHER[2], 6, 7, 26, 23)
    c.ell(LEATHER[0], 7, 8, 25, 22)
    c.ell(LEATHER[1], 9, 9.5, 17, 15)
    c.rect(GOLD[0], 11.5, 7, 20.5, 9, 0.8)
    c.poly(RED[0], (13.5, 11), (18.5, 11), (18.5, 15.5), (21.5, 15.5), (16, 21.5), (10.5, 15.5), (13.5, 15.5))


def zed(c, x, y, s, col):
    c.line(col, 1.7 * s, (x, y), (x + 5 * s, y), (x, y + 5 * s), (x + 5 * s, y + 5 * s))


def sleeping(c):
    blue = (64, 96, 190)
    zed(c, 5, 14, 1.3, blue)
    zed(c, 14.5, 9, 1.0, blue)
    zed(c, 22, 4.5, 0.75, blue)


def hungry(c):
    col = STEEL[2]
    c.line(col, 2, (10, 11), (10, 22))
    c.line(col, 1.3, (7, 4.5), (7, 9), (10, 11.5), (13, 9), (13, 4.5))
    c.line(col, 1.3, (10, 4.5), (10, 11))
    c.line(col, 2, (22, 4.5), (22, 22))
    c.poly(col, (22, 4), (25.5, 7), (25.5, 14), (22, 14))
    c.line(RED[0], 2.2, (4, 21), (28, 5))


def lockpicking(c):
    c.arc(STEEL[2], 6, 3, 18, 15, 180, 360, 2.4)
    c.rect(STEEL[2], 6, 8.5, 8.4, 12)
    c.rect(STEEL[2], 15.6, 8.5, 18, 12)
    c.rect(GOLD[0], 3.5, 10.5, 20.5, 23, 1.5)
    c.rect(GOLD[1], 3.5, 10.5, 20.5, 13.5, 1.5)
    c.circ(DARK, 12, 16, 1.9)
    c.rect(DARK, 11, 16, 13, 20)
    c.line(STEEL[2], 1.5, (12, 17), (21, 12), (28, 9), (28, 5.5))


def locked_in(c):
    c.rect(IRON[0], 4, 3.5, 28, 6)
    c.rect(IRON[0], 4, 20, 28, 22.5)
    for x in (6, 11, 21, 26):
        c.rect(IRON[2], x - 1, 5, x + 1, 21)
    c.arc(STEEL[2], 12.5, 7.5, 19.5, 15, 180, 360, 1.5)
    c.rect(GOLD[0], 11, 11, 21, 19, 1)
    c.circ(DARK, 16, 15, 1.4)


def herbalism(c):
    c.line(GREEN[2], 1.5, (16, 23), (16, 8))
    for y, s in ((19, 1), (15, -1), (11, 1)):
        c.poly(GREEN[0], (16, y), (16 + s * 10, y - 1), (16 + s * 8, y - 6.5))
        c.poly(GREEN[1], (16, y), (16 + s * 8, y - 6.5), (16 + s * 3, y - 5))
    c.poly(GREEN[1], (16, 9), (13, 5), (16, 3), (19, 5))


def unconscious(c):
    c.circ(SKIN_TONE, 16, 15, 8.5)
    for x in (12.5, 19.5):
        c.line(DARK, 1.3, (x - 1.7, 12), (x + 1.7, 15.5))
        c.line(DARK, 1.3, (x + 1.7, 12), (x - 1.7, 15.5))
    c.ell(DARK, 14, 18, 18, 21)
    star(c, GOLD[0], 6, 7, 3.4)
    star(c, GOLD[0], 16, 4.5, 2.8)
    star(c, GOLD[0], 26, 7, 3.4)


SKIN_TONE = (232, 186, 150)


def playing_dead(c):
    c.circ(SKIN_TONE, 16, 13, 10)
    c.line(DARK, 1.5, (9.5, 9), (13.5, 13))
    c.line(DARK, 1.5, (13.5, 9), (9.5, 13))
    c.circ(WHITE, 20.5, 11, 3)
    c.circ(DARK, 21.5, 11, 1.5)
    c.line(DARK, 1.2, (17.5, 8), (23.5, 7))
    c.line(DARK, 1.3, (11, 18), (20, 18))
    c.ell(RED[1], 15, 17.5, 20, 22)


def cue_put_down(c):
    c.poly(GREEN[2], (12, 4), (20, 4), (20, 11), (25, 11), (16, 20), (7, 11), (12, 11))
    c.poly(GREEN[0], (13, 5), (19, 5), (19, 12), (22.5, 12), (16, 18.5), (9.5, 12), (13, 12))
    c.rect(WOOD[2], 5, 20.5, 27, 23, 1)


def cue_talk(c):
    for x in (9, 16, 23):
        c.circ((48, 96, 58), x, 13, 3)


def no_upkeep(c):
    c.poly(WOOD[0], (5, 15), (20, 6), (26, 13), (11, 22))
    c.poly(WOOD[1], (5, 15), (20, 6), (22, 8.5), (7, 17.5))
    c.ell(WOOD[1], 4, 13.5, 12, 22.5)
    c.ell((222, 178, 120), 5.5, 15, 10.5, 21)
    c.circ(WOOD[2], 8, 18, 0.8)
    c.ring(RED[0], 16, 13, 10.5, 2)
    c.line(RED[0], 2, (8.5, 5.5), (23.5, 20.5))


def decaying(c):
    s = ((142, 138, 130), (186, 182, 172), (92, 88, 84))
    c.rect(s[0], 7, 4, 25, 20, 1)
    c.rect(s[1], 7, 4, 13, 20, 1)
    c.line(DARK, 1.3, (16, 4), (13.5, 9), (18, 12), (15, 16), (17, 20))
    c.line(DARK, 0.9, (18, 12), (23, 10))
    c.poly(s[2], (5, 20), (10, 19), (11, 23), (6, 23.5))
    c.poly(s[2], (22, 20.5), (27, 20), (26.5, 23.5), (22.5, 23.5))
    c.poly(s[0], (13, 21), (17, 20.5), (16.5, 23.5), (13.5, 23.5))


EMOTES = [
    (1, mining, STATUS), (2, woodcutting, STATUS), (3, cooking, STATUS), (4, encumbered, WARN), (5, sleeping, STATUS),
    (6, hungry, WARN), (7, lockpicking, STATUS), (8, locked_in, ALARM), (9, herbalism, STATUS), (10, unconscious, STATUS),
    (12, playing_dead, WARN), (13, cue_put_down, CUE), (14, cue_talk, CUE), (15, no_upkeep, WARN), (16, decaying, ALARM),
]

def main():
    BACKS = [(92, 140, 70), (206, 188, 140), (70, 60, 56)]
    sheet = Image.new('RGB', (len(EMOTES) * 76, 3 * 104), (0, 0, 0))
    dr = ImageDraw.Draw(sheet)
    for i, (n, fn, tone) in enumerate(EMOTES):
        c = C()
        bubble(c, tone)
        fn(c)
        grown = c.im.getchannel('A').filter(ImageFilter.MaxFilter(17))
        base = Image.new('RGBA', c.im.size, (22, 12, 16, 0))
        base.putalpha(grown)
        base.alpha_composite(c.im)
        out = base.resize((SIZE, SIZE), Image.LANCZOS)
        out.save(os.path.join(OUT, f'emote_{n}.png'))
        for row, back in enumerate(BACKS):
            x, y = i * 76, row * 104
            dr.rectangle([x, y, x + 76, y + 104], fill=back)
            big = out.resize((64, 64), Image.BILINEAR)
            small = out.resize((28, 28), Image.BILINEAR)
            sheet.paste(big, (x + 6, y + 4), big)
            sheet.paste(small, (x + 24, y + 70), small)
    os.makedirs(art.PREVIEW, exist_ok=True)
    sheet.save(os.path.join(art.PREVIEW, 'emotes.png'))
    print(len(EMOTES), 'emotes written')


if __name__ == '__main__':
    main()
