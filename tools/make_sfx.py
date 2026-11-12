"""Synthesizes the game's sound effects: the .mp3 files in SFX/. Every sound is made from sine waves and noise right here.

Run it with:  python tools/make_sfx.py     (needs Python 3, NumPy and ffmpeg on the PATH: pip install numpy)
Then build the game, which copies SFX/ next to the executable. The game only plays the finished files and never runs this
script.

How a sound is made:
  1. The sound's own function builds it from the small instruments below: osc() and noise() make the raw sound, filt() and
     the envelopes shape it, and mix() places the pieces in time. thud(), wood(), metal(), rustle(), voice() and the others
     are ready-made materials. Noise is seeded from the sound's key, so running this twice gives the same sounds.
  2. finish() cuts the silent tail and scales the sound so its loudest sample is at the level given in SOUNDS.
  3. ffmpeg encodes it to a mono MP3, under the file name data/sfx.txt gives that key.
  4. The MP3 is decoded again and measured. The script stops with an error if any sound clips, is silent, or is too long.

SOUNDS lists each sound's key, function and peak level in dB below full scale. Levels set the balance: Hit is the loudest,
finished work and events come next, repeating work sounds are quieter, and the interface sounds heard all the time are
quietest (UiHover, then UiClick). Working sounds repeat every 1.6 seconds (the Forge's every 3), so each must be shorter.

To make only some of the sounds, name their keys: python tools/make_sfx.py DanceStep MotivationChime

To add a sound: write a function, add it to SOUNDS, add the key to the Sfx enum in src/sounds.h, SFX_KEYS in
src/game_tables.cpp and data/sfx.txt (with a new file name), then run this script. To change one, edit its function or
level and run this script.

Running this overwrites every sound listed here, including any replaced by hand. It prints each sound's length and levels.
A preview with every sound in a row is written to out/art_preview/sfx_preview.wav, and the printed "at" column says where
in it each sound starts, so the sounds can be listened to without starting the game.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import wave
import zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, 'data', 'sfx.txt')
OUT = os.path.join(ROOT, 'SFX')
PREVIEW = os.path.join(ROOT, 'out', 'art_preview', 'sfx_preview.wav')
RATE = 44100
BITRATE = '128k'

MAX_PEAK = -1.0        # dB: anything above this after MP3 decoding risks clipping
MIN_LOUDNESS = -50.0   # dB: anything below this is as good as silent
MAX_ONSET = 0.04       # seconds of nothing a sound may start with (the MP3 encoder pads a little)
MAX_LENGTH = 2.5       # seconds, unless LIMITS says otherwise


random = np.random.default_rng()


# ---- raw sound ----

def count(dur):
    return max(1, int(round(dur * RATE)))


def glide(start, end, n):
    return start * (end / start) ** np.linspace(0.0, 1.0, n)


def osc(freq, dur, end=None, shape='sine', wobble=0.0, wobble_rate=5.0):
    """A wave at freq Hz, gliding to end. wobble bends the pitch by that fraction, wobble_rate times a second."""
    n = count(dur)
    f = glide(freq, end or freq, n)
    if wobble:
        f = f * (1.0 + wobble * np.sin(2.0 * np.pi * wobble_rate * np.arange(n) / RATE))
    phase = np.cumsum(f) / RATE
    if shape == 'saw':
        return 2.0 * (phase % 1.0) - 1.0
    if shape == 'square':
        return np.sign(np.sin(2.0 * np.pi * phase))
    if shape == 'triangle':
        return 2.0 * np.abs(2.0 * (phase % 1.0) - 1.0) - 1.0
    return np.sin(2.0 * np.pi * phase)


def noise(dur):
    return random.uniform(-1.0, 1.0, count(dur))


def filt(x, freq, q=0.7, mode='lp'):
    """A resonant filter: lp, bp or hp. freq is Hz, or (start, end) to sweep across the sound."""
    n = len(x)
    f = glide(freq[0], freq[1], n) if isinstance(freq, tuple) else np.full(n, float(freq))
    g = np.tan(np.pi * np.minimum(f, RATE * 0.45) / RATE)
    k = 1.0 / q
    a1 = 1.0 / (1.0 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
    out = [0.0] * n
    ic1 = ic2 = 0.0
    for i, (s, b1, b2, b3) in enumerate(zip(x.tolist(), a1.tolist(), a2.tolist(), a3.tolist())):
        v3 = s - ic2
        v1 = b1 * ic1 + b2 * v3
        v2 = ic2 + b2 * ic1 + b3 * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        out[i] = v2 if mode == 'lp' else v1 * k if mode == 'bp' else s - k * v1 - v2
    return np.array(out)


# ---- envelopes ----

def fall(x, tau):
    """Dies away, losing about two thirds every tau seconds."""
    return x * np.exp(-np.arange(len(x)) / RATE / tau)


def swell(x, peak=0.3, power=1.0):
    """Rises from nothing and falls back, loudest at peak (0 to 1) of the way through."""
    u = np.linspace(0.0, 1.0, len(x))
    up = np.clip(u / max(peak, 1e-4), 0.0, 1.0)
    down = np.clip((1.0 - u) / max(1.0 - peak, 1e-4), 0.0, 1.0)
    return x * (np.sin(up * np.pi / 2.0) * np.sin(down * np.pi / 2.0)) ** power


def mix(*parts):
    """Lays pieces over each other. Each part is (seconds, samples) or (seconds, samples, gain)."""
    end = max(count(p[0]) + len(p[1]) for p in parts)
    out = np.zeros(end)
    for p in parts:
        start = count(p[0]) if p[0] > 0 else 0
        out[start:start + len(p[1])] += p[1] * (p[2] if len(p) > 2 else 1.0)
    return out


def scale(x):
    top = np.abs(x).max()
    return x / top if top > 0 else x


# ---- materials ----

def partials(freq, dur, modes):
    """A struck object: modes are (frequency ratio, gain, decay seconds)."""
    return sum(fall(osc(freq * r, dur), tau) * g for r, g, tau in modes)


def tick(freq=1200.0, dur=0.03, tau=0.006):
    """A very short click of a hard, small thing."""
    return partials(freq, dur, [(1.0, 1.0, tau), (2.3, 0.4, tau * 0.6)]) + fall(filt(noise(dur), freq * 2.0, 0.8, 'hp'), tau * 0.4) * 0.3


def note(freq, dur=0.25, tau=0.07):
    """A soft mallet note, like a small wooden bar."""
    return swell(partials(freq, dur, [(1.0, 1.0, tau), (3.9, 0.22, tau * 0.25), (9.2, 0.05, tau * 0.1)]), 0.012)


def bell(freq, dur=0.6, tau=0.22):
    """A clear ringing note."""
    return swell(partials(freq, dur, [(1.0, 1.0, tau), (2.0, 0.4, tau * 0.7), (3.01, 0.22, tau * 0.45), (5.4, 0.08, tau * 0.2)]), 0.005)


def thud(freq=95.0, dur=0.22, end=45.0, tau=0.06):
    """A low blow on something soft or heavy."""
    body = fall(osc(freq, dur, end), tau)
    return swell(body, 0.015) + fall(filt(noise(dur), 900.0), 0.012) * 0.45


def wood(freq=420.0, dur=0.14, tau=0.03):
    """A knock on wood."""
    body = partials(freq, dur, [(1.0, 1.0, tau), (2.41, 0.55, tau * 0.6), (3.93, 0.25, tau * 0.4)])
    return body + fall(filt(noise(dur), 2500.0, 0.8, 'bp'), 0.006) * 0.6


def metal(freq=1800.0, dur=0.3, tau=0.07):
    """A strike on metal, with the uneven overtones of a real object."""
    body = partials(freq, dur, [(1.0, 1.0, tau), (1.51, 0.5, tau * 0.8), (2.76, 0.6, tau * 0.6), (5.4, 0.3, tau * 0.35), (8.93, 0.15, tau * 0.2)])
    return body + fall(filt(noise(dur), 6000.0, 0.8, 'hp'), 0.003) * 0.5


def stone(dur=0.16, freq=1400.0, tau=0.03):
    """A hard, gritty crack of rock."""
    return fall(filt(noise(dur), freq, 1.2, 'bp'), tau) + fall(osc(freq * 0.22, dur, freq * 0.12), tau * 0.8) * 0.5


def rustle(dur=0.2, freq=3200.0, grains=30.0, q=0.8):
    """Cloth, leaves or leather moving: noise that flutters at random."""
    n = count(dur)
    flutter = np.interp(np.arange(n), np.linspace(0, n, max(2, int(dur * grains) + 2)), random.uniform(0.1, 1.0, max(2, int(dur * grains) + 2)))
    return swell(filt(noise(dur), freq, q, 'bp') * flutter, 0.35)


def swish(start, end, dur, q=1.6):
    """Air moving past: noise with a sweeping pitch."""
    return swell(filt(noise(dur), (start, end), q, 'bp'), 0.45)


def crackle(dur, per_second=30.0, freq=2600.0):
    """Sparse little pops, like burning wood or hot fat."""
    n = count(dur)
    x = np.zeros(n)
    hits = random.integers(0, n, max(1, int(dur * per_second)))
    x[hits] = random.uniform(-1.0, 1.0, len(hits))
    return filt(x, freq, 2.5, 'bp')


def bubble(freq, dur=0.05, rise=1.5):
    """One rising bubble of water."""
    return swell(fall(osc(freq, dur, freq * rise), dur * 0.45), 0.08)


def water(dur, per_second=45.0, low=350.0, high=1300.0):
    """Moving water: a crowd of bubbles over a wash of noise."""
    parts = [(0.0, filt(noise(dur), 1800.0, 0.6, 'bp'), 0.1)]
    for _ in range(max(1, int(dur * per_second))):
        parts.append((random.uniform(0.0, dur * 0.95), bubble(random.uniform(low, high), random.uniform(0.025, 0.07)), random.uniform(0.15, 0.5)))
    return mix(*parts)


def voice(start, end, dur, vowel=(650.0, 1100.0), peak=0.2, rough=0.0, wobble=0.012):
    """A throaty sound: a buzzing pitch shaped by two vowel resonances. rough adds gravel to it."""
    source = osc(start, dur, end, 'saw', wobble, 6.0)
    if rough:
        source = source * (1.0 - rough + rough * osc(start * 0.53, dur, end * 0.53, 'square', 0.02, 11.0))
    shaped = filt(source, vowel[0], 3.5, 'bp') + filt(source, vowel[1], 4.0, 'bp') * 0.55
    return swell(shaped, peak)


# ---- interface: dry, wooden and short, so they sit under the game ----

def ui_hover():
    return fall(osc(2300.0, 0.028), 0.006)


def ui_click():
    return mix((0.0, tick(1050.0, 0.05, 0.009)), (0.0, fall(osc(520.0, 0.05), 0.012), 0.5))


def ui_back():
    return mix((0.0, tick(820.0, 0.05, 0.012)), (0.035, tick(560.0, 0.06, 0.016), 0.9))


def ui_tab():
    return mix((0.0, filt(swell(noise(0.05), 0.5), 3800.0, 1.0, 'bp'), 0.4), (0.03, tick(1450.0, 0.04, 0.008)))


def ui_toggle():
    return mix((0.0, tick(1700.0, 0.025, 0.004)), (0.038, tick(1250.0, 0.035, 0.006), 0.8))


def ui_slider():
    return mix((0.0, tick(900.0, 0.04, 0.007)), (0.0, fall(osc(300.0, 0.05), 0.012), 0.6))


def ui_menu_open():
    return mix((0.0, swish(1200.0, 4200.0, 0.09, 1.0), 0.5), (0.065, tick(1350.0, 0.04, 0.009)))


def ui_error():
    buzz = filt(osc(150.0, 0.09, 140.0, 'square'), 900.0)
    return mix((0.0, swell(buzz, 0.1)), (0.11, swell(filt(osc(125.0, 0.11, 112.0, 'square'), 800.0), 0.08)))


def ui_saved():
    return mix((0.0, note(659.3, 0.2)), (0.1, note(987.8, 0.28, 0.09)))


def ui_loaded():
    return mix((0.0, note(587.3, 0.18)), (0.08, note(740.0, 0.18), 0.9), (0.16, note(1174.7, 0.3, 0.09)))


def ui_pause():
    return mix((0.0, note(784.0, 0.18, 0.05)), (0.09, note(523.3, 0.24, 0.07)))


def ui_resume():
    return mix((0.0, note(523.3, 0.16, 0.05)), (0.08, note(784.0, 0.22, 0.07)))


def ui_select():
    return fall(osc(980.0, 0.07, 1500.0), 0.018)


def ui_order():
    return mix((0.0, note(740.0, 0.06, 0.015)), (0.055, note(1109.0, 0.09, 0.02)))


def talk_open():
    # A questioning little "mm-hm?"
    return mix((0.0, voice(190.0, 170.0, 0.11, (420.0, 1250.0)), 1.0), (0.13, voice(170.0, 235.0, 0.17, (520.0, 1500.0), 0.3)))


# ---- items ----

def item_move():
    return mix((0.0, rustle(0.07, 2400.0), 0.5), (0.04, wood(300.0, 0.08, 0.015)))


def equip():
    return mix((0.0, rustle(0.15, 2600.0), 0.55), (0.1, metal(2400.0, 0.14, 0.03), 0.5), (0.13, metal(3100.0, 0.14, 0.03), 0.35))


def unequip():
    return mix((0.0, metal(2700.0, 0.12, 0.025), 0.45), (0.03, rustle(0.16, 1900.0), 0.6))


def gear_changed():
    return mix((0.0, rustle(0.3, 2500.0), 0.55), (0.07, metal(2200.0, 0.14, 0.03), 0.4), (0.17, metal(2900.0, 0.14, 0.03), 0.35),
               (0.25, metal(2500.0, 0.14, 0.03), 0.3))


def drop():
    return mix((0.0, thud(135.0, 0.16, 70.0, 0.04)), (0.0, filt(fall(noise(0.14), 0.03), 1100.0), 0.45), (0.07, wood(240.0, 0.08, 0.018), 0.25))


def pickup():
    return mix((0.0, rustle(0.1, 2200.0), 0.5), (0.065, fall(osc(480.0, 0.07, 900.0), 0.025)))


def bite(gain=1.0):
    return filt(fall(noise(0.075), 0.022), 1700.0, 0.9, 'bp') * gain


def eat():
    return mix((0.0, bite()), (0.012, bite(0.6)), (0.15, thud(170.0, 0.08, 110.0, 0.02), 0.25), (0.17, bite(0.7)), (0.32, bite(0.55)),
               (0.32, thud(160.0, 0.08, 105.0, 0.02), 0.2))


def gulp(freq=240.0, gain=1.0):
    return swell(osc(freq, 0.11, freq * 2.1), 0.2) * gain


def drink():
    return mix((0.0, gulp(230.0)), (0.16, gulp(250.0, 0.9)), (0.33, gulp(270.0, 0.8)), (0.12, water(0.05, 80.0), 0.2), (0.28, water(0.05, 80.0), 0.2))


def poison():
    # A retch that sags in pitch, then a sick fizz
    return mix((0.0, voice(180.0, 80.0, 0.5, (480.0, 1000.0), 0.25, 0.5, 0.04)), (0.12, filt(swell(noise(0.45), 0.2), (1800.0, 4500.0), 2.0, 'bp'), 0.2))


# ---- work: heard over and over while a unit works, so plain and not tiring ----

def work_mining():
    # Iron on rock: a bright strike, the crack, and a few grains falling
    return mix((0.0, metal(2100.0, 0.16, 0.022), 0.55), (0.0, stone(0.16)), (0.09, stone(0.04, 3200.0, 0.008), 0.18), (0.14, stone(0.04, 2800.0, 0.008), 0.12))


def work_woodcutting():
    # An axe biting: a hollow knock with a sharp edge on it
    return mix((0.0, wood(310.0, 0.2, 0.045)), (0.0, filt(fall(noise(0.05), 0.008), 3400.0, 0.9, 'bp'), 0.55), (0.0, thud(120.0, 0.12, 80.0, 0.03), 0.45))


def work_herbalism():
    return mix((0.0, rustle(0.28, 3800.0, 40.0), 0.8), (0.17, tick(2600.0, 0.03, 0.005), 0.2), (0.22, thud(320.0, 0.05, 220.0, 0.012), 0.2))


def sizzle(dur):
    return swell(filt(noise(dur), 6000.0, 0.7, 'hp'), 0.2) * 0.35 + crackle(dur, 36.0, 3600.0) * 0.8


def work_cooking():
    return mix((0.0, sizzle(0.95)), (0.2, wood(560.0, 0.08, 0.016), 0.12))


def pump_stroke():
    # A squeaking lever, a clunk at the bottom of the stroke, then water coming up the pipe
    squeak = swell(osc(1050.0, 0.17, 1350.0, 'triangle', 0.02, 28.0), 0.5)
    return mix((0.0, filt(squeak, 1300.0, 3.0, 'bp'), 0.3), (0.2, wood(210.0, 0.12, 0.03), 0.8), (0.21, metal(980.0, 0.1, 0.02), 0.12))


def work_pump():
    return mix((0.0, pump_stroke()), (0.3, swell(water(0.5, 50.0), 0.2), 0.8))


def work_training():
    # A stick on a straw dummy
    return mix((0.0, thud(170.0, 0.14, 90.0, 0.035)), (0.0, filt(fall(noise(0.13), 0.04), 2400.0, 0.7, 'bp'), 0.7), (0.0, wood(620.0, 0.06, 0.012), 0.2))


def work_lockpick():
    return mix((0.0, metal(4200.0, 0.05, 0.007), 0.8), (0.1, metal(5100.0, 0.05, 0.006), 0.55), (0.17, metal(3800.0, 0.05, 0.007), 0.65),
               (0.31, metal(4700.0, 0.05, 0.006), 0.5), (0.05, swish(5000.0, 7000.0, 0.28, 2.0), 0.06))


def forge_burning():
    dur = 2.3
    roar = filt(noise(dur), 260.0, 0.8)
    breath = 1.0 + 0.25 * np.sin(2.0 * np.pi * 1.7 * np.arange(count(dur)) / RATE)
    return mix((0.0, swell(scale(roar) * breath, 0.35)), (0.0, swell(crackle(dur, 16.0, 2200.0), 0.35), 0.4), (0.0, swell(filt(noise(dur), 1100.0, 0.8, 'bp'), 0.4), 0.05))


# ---- work finished: one notch more satisfying than the work ----

def done_ore():
    # A chunk breaking free, bouncing and settling
    return mix((0.0, stone(0.16, 900.0, 0.04)), (0.0, thud(150.0, 0.12, 80.0, 0.03), 0.6), (0.12, stone(0.08, 1800.0, 0.018), 0.55),
               (0.21, stone(0.06, 2300.0, 0.012), 0.35), (0.28, metal(2500.0, 0.12, 0.022), 0.15))


def done_tree():
    # The last chop's split, then a heavy log landing and rolling once
    split = filt(fall(noise(0.09), 0.02), 2800.0, 0.9, 'bp')
    return mix((0.0, split, 0.7), (0.0, wood(280.0, 0.14, 0.03), 0.6), (0.2, thud(95.0, 0.3, 48.0, 0.09)), (0.2, wood(170.0, 0.16, 0.04), 0.55), (0.37, wood(205.0, 0.12, 0.03), 0.3))


def done_mushroom():
    # A soft pop as it comes out of the ground
    return mix((0.0, rustle(0.08, 3500.0), 0.35), (0.05, fall(osc(350.0, 0.09, 900.0), 0.022)), (0.07, filt(fall(noise(0.03), 0.008), 1500.0), 0.25))


def done_cooking():
    return mix((0.0, sizzle(0.28), 0.45), (0.12, bell(1318.5, 0.45, 0.13), 0.5), (0.21, bell(1760.0, 0.45, 0.14), 0.4))


def done_water():
    # Water tipped into a pail
    return mix((0.0, swell(water(0.42, 70.0, 300.0, 1000.0), 0.3)), (0.0, wood(250.0, 0.1, 0.025), 0.3), (0.0, swell(filt(noise(0.42), (900.0, 2400.0), 1.0, 'bp'), 0.25), 0.25))


def done_training():
    return mix((0.0, thud(170.0, 0.13, 90.0, 0.035), 0.85), (0.0, filt(fall(noise(0.1), 0.03), 2400.0, 0.7, 'bp'), 0.5), (0.12, note(659.3, 0.15, 0.04), 0.4), (0.21, note(880.0, 0.22, 0.06), 0.45))


def done_forge():
    # A hammer ringing on the anvil, and the hot metal quenched
    return mix((0.0, metal(1560.0, 0.55, 0.14)), (0.0, thud(130.0, 0.1, 90.0, 0.02), 0.4), (0.28, swell(filt(noise(0.55), (6500.0, 3000.0), 0.8, 'bp'), 0.08), 0.3))


# ---- locks and trade ----

def unlocked():
    # The pins giving way, and the latch springing back
    return mix((0.0, metal(3600.0, 0.05, 0.007), 0.5), (0.07, metal(3100.0, 0.05, 0.007), 0.6), (0.15, wood(520.0, 0.09, 0.016), 0.6), (0.15, metal(1250.0, 0.24, 0.05)))


def lockpick_failed():
    # The pick slipping and snapping
    return mix((0.0, swish(5200.0, 2600.0, 0.15, 3.0), 0.5), (0.13, metal(4300.0, 0.08, 0.009)), (0.16, stone(0.1, 2700.0, 0.012), 0.2), (0.2, wood(230.0, 0.12, 0.03), 0.35))


def coin(freq, gain=1.0):
    return metal(freq, 0.26, 0.05) * gain


def trade_done():
    return mix((0.0, coin(3050.0, 0.7)), (0.08, coin(3600.0, 0.75)), (0.17, coin(3320.0, 0.8)), (0.24, coin(4150.0, 0.9)))


def trade_failed():
    return mix((0.0, wood(250.0, 0.12, 0.028), 0.6), (0.06, note(392.0, 0.18, 0.05), 0.8), (0.18, note(311.1, 0.28, 0.08), 0.85))


def store():
    # Something set in, and a lid or door coming down
    return mix((0.0, rustle(0.06, 2300.0), 0.4), (0.04, wood(250.0, 0.14, 0.03)), (0.04, thud(120.0, 0.11, 75.0, 0.025), 0.4))


def take():
    # A lid or door knocking, and something lifted out
    return mix((0.0, wood(340.0, 0.1, 0.018), 0.65), (0.05, rustle(0.11, 2600.0), 0.6), (0.11, fall(osc(320.0, 0.06, 560.0), 0.02), 0.25))


# ---- units ----

def hit():
    # A hard blow landing: a sharp smack over a heavy body
    return mix((0.0, thud(145.0, 0.19, 65.0, 0.05)), (0.0, filt(fall(noise(0.09), 0.016), 2300.0, 0.7, 'bp'), 0.95), (0.0, wood(480.0, 0.06, 0.012), 0.25))


def miss():
    return swish(800.0, 2900.0, 0.19, 1.3)


def downed():
    # A grunt, then the body hitting the ground
    return mix((0.0, voice(190.0, 105.0, 0.22, (560.0, 1050.0), 0.2, 0.3), 0.5), (0.18, thud(85.0, 0.32, 40.0, 0.09)), (0.18, filt(fall(noise(0.16), 0.04), 900.0), 0.45), (0.28, thud(120.0, 0.13, 65.0, 0.03), 0.35))


def dies():
    # A last long breath falling away
    return mix((0.0, voice(150.0, 58.0, 0.85, (620.0, 980.0), 0.14, 0.4, 0.018)), (0.2, filt(swell(noise(0.62), 0.25), (1400.0, 450.0), 1.0, 'bp'), 0.12))


def recovers():
    # A breath drawn in, and a small warm rising chord
    return mix((0.0, filt(swell(noise(0.3), 0.75), (700.0, 2100.0), 1.0, 'bp'), 0.22), (0.14, bell(523.3, 0.45, 0.15), 0.5), (0.24, bell(659.3, 0.45, 0.15), 0.42), (0.34, bell(784.0, 0.5, 0.18), 0.42))


def turn_hostile():
    # A rising snarl
    return voice(120.0, 215.0, 0.5, (720.0, 1500.0), 0.55, 0.65, 0.03)


def locked_in():
    # A cage door slammed and its bolt thrown
    return mix((0.0, metal(520.0, 0.42, 0.11)), (0.0, thud(110.0, 0.2, 70.0, 0.05), 0.7), (0.2, metal(1900.0, 0.08, 0.012), 0.55), (0.26, metal(1350.0, 0.09, 0.018), 0.65))


def occupy():
    # Wood creaking under someone's weight
    creak = swell(filt(osc(180.0, 0.18, 240.0, 'saw', 0.02, 32.0), 1100.0, 3.0, 'bp'), 0.5)
    return mix((0.0, thud(150.0, 0.12, 95.0, 0.03), 0.6), (0.0, rustle(0.08, 1800.0), 0.3), (0.06, creak, 0.5))


def level_up():
    return mix((0.0, bell(523.3, 0.4, 0.1), 0.6), (0.09, bell(659.3, 0.4, 0.1), 0.65), (0.18, bell(784.0, 0.4, 0.11), 0.7), (0.27, bell(1046.5, 0.65, 0.2), 0.85),
               (0.27, bell(1568.0, 0.55, 0.15), 0.25))


def carry_up():
    # Effort, and cloth shifting on a shoulder
    return mix((0.0, voice(135.0, 180.0, 0.17, (580.0, 1150.0), 0.35, 0.3), 0.6), (0.12, rustle(0.18, 1700.0), 0.65), (0.26, thud(160.0, 0.08, 100.0, 0.02), 0.3))


def put_down():
    # A breath out, and a body laid on the ground
    return mix((0.0, rustle(0.13, 1500.0), 0.5), (0.0, voice(160.0, 118.0, 0.16, (560.0, 1050.0), 0.2, 0.2), 0.35), (0.1, thud(105.0, 0.2, 55.0, 0.055)))


def dance_step():
    # A light foot landing mid-dance: a short soft thud and a trace of dust
    return mix((0.0, thud(110.0, 0.07, 60.0, 0.02), 0.8), (0.0, rustle(0.04, 1400.0), 0.15))


def motivation_chime():
    # Two bright bells lifting, then a longer one: a faction's spirits rising over its threshold
    return mix((0.0, bell(659.3, 0.5, 0.18), 0.6), (0.11, bell(987.8, 0.7, 0.24), 0.75), (0.11, bell(1975.5, 0.5, 0.12), 0.2),
               (0.24, bell(1318.5, 0.9, 0.3), 0.7))


# Key: (function, peak level in dB).
SOUNDS = {
    'UiHover': (ui_hover, -26),
    'UiClick': (ui_click, -15),
    'UiBack': (ui_back, -14),
    'UiTab': (ui_tab, -16),
    'UiToggle': (ui_toggle, -14),
    'UiSlider': (ui_slider, -15),
    'UiMenuOpen': (ui_menu_open, -15),
    'UiSelect': (ui_select, -17),
    'UiOrder': (ui_order, -13),
    'UiError': (ui_error, -18),
    'UiSaved': (ui_saved, -13),
    'UiLoaded': (ui_loaded, -12),
    'UiPause': (ui_pause, -13),
    'UiResume': (ui_resume, -13),
    'TalkOpen': (talk_open, -12),

    'ItemMove': (item_move, -9),
    'Equip': (equip, -7),
    'Unequip': (unequip, -8),
    'GearChanged': (gear_changed, -6),
    'Drop': (drop, -9),
    'Pickup': (pickup, -9),
    'Eat': (eat, -6),
    'Drink': (drink, -13),
    'Poison': (poison, -4),
    'Store': (store, -8),
    'Take': (take, -7),

    'WorkMining': (work_mining, -5),
    'WorkWoodcutting': (work_woodcutting, -7),
    'WorkHerbalism': (work_herbalism, -10),
    'WorkCooking': (work_cooking, -12),
    'WorkPump': (work_pump, -9),
    'WorkTraining': (work_training, -8),
    'WorkLockpick': (work_lockpick, -9),
    'ForgeBurning': (forge_burning, -11),

    'DoneOre': (done_ore, -5),
    'DoneTree': (done_tree, -6),
    'DoneMushroom': (done_mushroom, -8),
    'DoneCooking': (done_cooking, -7),
    'DoneWater': (done_water, -6),
    'DoneTraining': (done_training, -6),
    'DoneForge': (done_forge, -5),

    'Unlocked': (unlocked, -6),
    'LockpickFailed': (lockpick_failed, -5),
    'TradeDone': (trade_done, -6),
    'TradeFailed': (trade_failed, -11),

    'Hit': (hit, -2),
    'Miss': (miss, -8),
    'Downed': (downed, -6),
    'Dies': (dies, -3),
    'Recovers': (recovers, -8),
    'TurnHostile': (turn_hostile, -4),
    'LockedIn': (locked_in, -6),
    'Occupy': (occupy, -11),
    'LevelUp': (level_up, -6),
    'CarryUp': (carry_up, -7),
    'PutDown': (put_down, -10),
    'DanceStep': (dance_step, -24),
    'MotivationChime': (motivation_chime, -8),
}

# Key: (longest it may be in seconds, highest peak it may have in dB), for the sounds with a job that demands it.
LIMITS = {
    'UiHover': (0.06, -24.0),
    'UiClick': (0.12, -12.0),
    'UiSelect': (0.12, -12.0),
    'UiTab': (0.12, -12.0),
    'UiToggle': (0.12, -12.0),
    'WorkMining': (1.5, MAX_PEAK),
    'WorkWoodcutting': (1.5, MAX_PEAK),
    'WorkHerbalism': (1.5, MAX_PEAK),
    'WorkCooking': (1.5, MAX_PEAK),
    'WorkPump': (1.5, MAX_PEAK),
    'WorkTraining': (1.5, MAX_PEAK),
    'WorkLockpick': (1.5, MAX_PEAK),
    'ForgeBurning': (2.9, MAX_PEAK),
    'DanceStep': (0.25, MAX_PEAK),
}


def finish(x, level):
    """Centers the sound, cuts its silent tail, softens its ends so they don't click, and sets its peak."""
    x = x - x.mean()
    top = np.abs(x).max()
    loud = np.nonzero(np.abs(x) > top * 0.002)[0]
    x = x[:loud[-1] + 1]
    n = len(x)
    ease_in = min(count(0.0008), n // 4)
    ease_out = min(count(0.006), n // 4)
    x[:ease_in] *= np.linspace(0.0, 1.0, ease_in)
    x[n - ease_out:] *= np.linspace(1.0, 0.0, ease_out)
    return x / np.abs(x).max() * 10.0 ** (level / 20.0)


def to_pcm16(x):
    return (np.clip(x, -1.0, 1.0) * 32767.0).astype('<i2').tobytes()


def write_wav(path, x):
    with wave.open(path, 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(to_pcm16(x))


def encode(x, path):
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, 'sound.wav')
        write_wav(source, x)
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', source, '-map_metadata', '-1', '-c:a', 'libmp3lame',
                        '-b:a', BITRATE, '-ac', '1', '-ar', str(RATE), path], check=True)


def decode(path):
    done = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '1', '-ar', str(RATE), '-'],
                          check=True, capture_output=True)
    return np.frombuffer(done.stdout, '<f4')


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-9))


def measure(x):
    """Length in seconds, peak in dB, loudness in dB (the loudest 50 ms) and seconds before the sound begins."""
    window = min(count(0.05), len(x))
    power = np.convolve(x * x, np.ones(window) / window, 'valid')
    top = np.abs(x).max()
    start = int(np.argmax(np.abs(x) > top * 0.01))
    return len(x) / RATE, db(top), db(np.sqrt(power.max())), start / RATE


def problems(key, length, peak, loudness, onset):
    longest, highest = LIMITS.get(key, (MAX_LENGTH, MAX_PEAK))
    found = []
    if length > longest:
        found.append(f'{length:.2f} s is longer than {longest} s')
    if peak > highest:
        found.append(f'peak {peak:.1f} dB is above {highest} dB')
    if loudness < MIN_LOUDNESS:
        found.append(f'loudness {loudness:.1f} dB is as good as silent')
    if onset > MAX_ONSET:
        found.append(f'starts {onset * 1000:.0f} ms late')
    return found


def read_table():
    """The file name data/sfx.txt gives each key."""
    files = {}
    with open(TABLE, encoding='utf-8') as f:
        for line in f:
            if line.strip() and not line.startswith('#'):
                cols = line.split('\t')
                files[cols[0]] = cols[1]
    return files


def main():
    global random
    if not shutil.which('ffmpeg'):
        sys.exit('make_sfx needs ffmpeg on the PATH')
    files = read_table()
    if set(files) != set(SOUNDS):
        sys.exit(f'data/sfx.txt and SOUNDS disagree: only in the table {sorted(set(files) - set(SOUNDS))}, '
                 f'only in SOUNDS {sorted(set(SOUNDS) - set(files))}')
    os.makedirs(OUT, exist_ok=True)
    gap = np.zeros(count(0.5))
    preview = []
    at = 0.0
    failed = []
    print(f'{"key":<17}{"file":<13}{"at":>7}{"length":>8}{"peak":>7}{"loud":>7}')
    only = set(sys.argv[1:])   # keys named on the command line: make just those, leaving the other files as they are
    if only - set(SOUNDS):
        sys.exit(f'not sounds: {sorted(only - set(SOUNDS))}')
    for key, (make, level) in SOUNDS.items():
        if only and key not in only:
            continue
        random = np.random.default_rng(zlib.crc32(key.encode()))
        path = os.path.join(OUT, files[key])
        encode(finish(make(), level), path)
        heard = decode(path)
        length, peak, loudness, onset = measure(heard)
        print(f'{key:<17}{files[key]:<13}{at:>6.1f}s{length:>7.2f}s{peak:>7.1f}{loudness:>7.1f}')
        failed += [f'{key}: {p}' for p in problems(key, length, peak, loudness, onset)]
        preview += [heard.astype(float), gap]
        at += length + len(gap) / RATE
    if only:
        print(f'Wrote {len(only)} sounds to {OUT}')
    else:
        os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
        write_wav(PREVIEW, np.concatenate(preview))
        print(f'Wrote {len(SOUNDS)} sounds to {OUT}, and a preview to {PREVIEW}')
    if failed:
        sys.exit('Problems:\n  ' + '\n  '.join(failed))


if __name__ == '__main__':
    main()
