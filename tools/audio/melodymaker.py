#!/usr/bin/env python3
"""
Melody Maker
============

A focused tool for generating, auditioning and exporting single-voice melodies.

This is a melody instrument only: there are no pads, drones, chords or
percussion in the audio output.  Everything in the program exists to shape one
melodic line - its notes, its rhythm, its tone and its space.

How it writes a melody
----------------------
A melody is one short idea, restated - not a sequence of acceptable pitches.
So the engine is motif-first:

1. It writes many candidate motifs and scores each for memorability: a
   distinct contour, varied intervals, a mix of small steps and larger leaps,
   a singable span, rhythmic shape, and few enough ideas to follow.  The best
   candidate is kept.  That search is the difference between a melody and a
   constrained random walk.
2. The motif is stored abstractly - a rhythm plus scale-steps measured from
   its own first note - so its contour and intervals travel with it and it
   stays recognisable starting anywhere.
3. A form lays it out as statement and answer.  The antecedent ends open, a
   question; the consequent resolves to the tonic, its answer.
4. Restatements come from named variation operators, each of which preserves
   at least one of {rhythm, contour, intervals}.  Every repeat therefore
   elaborates the idea - echoing its rhythm, its shape or its ending - rather
   than wandering into unrelated material.
5. Contrasting sections are derived from the motif and made to quote its
   closing interval, so a B section still argues for the original idea.

Features
--------
Pitch      18 scales, any root, adjustable register, singable-span and
           max-leap limits, leap-recovery, chord-tone gravity from a silent
           harmonic guide (structural notes only, so intervals survive).
Form       Seven forms - period, sentence, AABA, ABAC, bar form, chain, call
           and response - with open/closed cadences and 13 variation
           operators (repeat, sequence, new ending, ornament, diminution,
           augmentation, fragment, extend, displace, inversion, retrograde,
           widen intervals, new rhythm).
Rhythm     Grid subdivision, swing, syncopation, rhythmic space, note length,
           gate/legato, accents, phrase-level dynamic arc, and humanised
           timing + velocity.
Voice      Five waveforms, additive partials with rolloff, full ADSR, vibrato,
           unison detune, portamento, plus instrument presets.
Space      Tone (low-pass), delay with feedback, reverb, stereo width, drive.
Output     Live playback with looping, a piano roll coloured by form unit, a
           readout of the chosen motif and why it won, WAV / MP3 / MIDI
           export, JSON preset save & load.

Requirements
------------
    pip install numpy soundfile sounddevice pydub imageio-ffmpeg
(tkinter ships with Python; on Debian/Ubuntu: apt install python3-tk)

sounddevice is only needed for live playback, pydub + imageio-ffmpeg only for
MP3 export.  Everything else works without them.
"""

import os
import json
import math
import struct
import random
import warnings
import threading
import traceback

# ---------------------------------------------------------------------------
# ffmpeg bootstrap - must happen before pydub is imported
# ---------------------------------------------------------------------------
FFMPEG_PATH = None
try:
    import imageio_ffmpeg

    FFMPEG_PATH = imageio_ffmpeg.get_ffmpeg_exe()
    if FFMPEG_PATH and os.path.exists(FFMPEG_PATH):
        _ffdir = os.path.dirname(FFMPEG_PATH)
        os.environ["PATH"] = _ffdir + os.pathsep + os.environ.get("PATH", "")
        import tempfile
        import shutil

        _alias_dir = os.path.join(tempfile.gettempdir(), "ffmpeg_alias")
        os.makedirs(_alias_dir, exist_ok=True)
        _alias_name = "ffmpeg.exe" if os.name == "nt" else "ffmpeg"
        _alias_path = os.path.join(_alias_dir, _alias_name)
        if not os.path.exists(_alias_path):
            try:
                shutil.copy2(FFMPEG_PATH, _alias_path)
            except Exception:
                pass
        if os.path.exists(_alias_path):
            os.environ["PATH"] = _alias_dir + os.pathsep + os.environ["PATH"]
except Exception:
    FFMPEG_PATH = None

AudioSegment = None
with warnings.catch_warnings():
    warnings.simplefilter("ignore")
    try:
        from pydub import AudioSegment

        if FFMPEG_PATH:
            AudioSegment.converter = FFMPEG_PATH
    except Exception:
        AudioSegment = None

import numpy as np

try:
    import soundfile as sf

    HAS_SF = True
except Exception:
    sf = None
    HAS_SF = False

try:
    import sounddevice as sd

    HAS_AUDIO = True
except Exception:
    sd = None
    HAS_AUDIO = False

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import tkinter.font as tkfont


SAMPLE_RATE = 44100
APP_NAME = "Melody Maker"
APP_VERSION = "2.0"


# ===========================================================================
#  MUSIC THEORY
# ===========================================================================

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

SCALES = {
    "Major (Ionian)":         [0, 2, 4, 5, 7, 9, 11],
    "Natural Minor":          [0, 2, 3, 5, 7, 8, 10],
    "Harmonic Minor":         [0, 2, 3, 5, 7, 8, 11],
    "Melodic Minor":          [0, 2, 3, 5, 7, 9, 11],
    "Dorian":                 [0, 2, 3, 5, 7, 9, 10],
    "Phrygian":               [0, 1, 3, 5, 7, 8, 10],
    "Lydian":                 [0, 2, 4, 6, 7, 9, 11],
    "Mixolydian":             [0, 2, 4, 5, 7, 9, 10],
    "Locrian":                [0, 1, 3, 5, 6, 8, 10],
    "Phrygian Dominant":      [0, 1, 4, 5, 7, 8, 10],
    "Hungarian Minor":        [0, 2, 3, 6, 7, 8, 11],
    "Major Pentatonic":       [0, 2, 4, 7, 9],
    "Minor Pentatonic":       [0, 3, 5, 7, 10],
    "Blues":                  [0, 3, 5, 6, 7, 10],
    "Hirajoshi":              [0, 2, 3, 7, 8],
    "Kumoi":                  [0, 2, 3, 7, 9],
    "Whole Tone":             [0, 2, 4, 6, 8, 10],
    "Chromatic":              list(range(12)),
}

# Silent harmonic guide: scale-degree indices, one chord per bar.  Nothing is
# rendered from these - they only bias which notes the melody favours.
PROGRESSIONS = {
    "Free (no guide)": None,
    "I - V - vi - IV":        [0, 4, 5, 3],
    "I - vi - IV - V":        [0, 5, 3, 4],
    "I - IV - V - I":         [0, 3, 4, 0],
    "vi - IV - I - V":        [5, 3, 0, 4],
    "i - VI - III - VII":     [0, 5, 2, 6],
    "i - iv - VI - V":        [0, 3, 5, 4],
    "i - VII - VI - VII":     [0, 6, 5, 6],
    "ii - V - I":             [1, 4, 0],
    "I - iii - IV - iv":      [0, 2, 3, 3],
    "Pedal (tonic only)":     [0],
    "Descending (I-VII-VI-V)": [0, 6, 5, 4],
}

CONTOURS = {
    "Arch":            lambda u: math.sin(math.pi * u),
    "Valley":          lambda u: 1.0 - math.sin(math.pi * u),
    "Rising":          lambda u: u,
    "Falling":         lambda u: 1.0 - u,
    "Wave":            lambda u: 0.5 + 0.5 * math.sin(2 * math.pi * u - math.pi / 2),
    "Double Arch":     lambda u: math.sin(math.pi * ((2.0 * u) % 1.0)),
    "Terraced":        lambda u: min(1.0, math.floor(u * 4) / 3.0),
    "Level":           lambda u: 0.5,
}

WAVEFORMS = ["Sine", "Triangle", "Square", "Saw", "Pulse"]

# Voice presets write directly into the Voice tab parameters.
VOICE_PRESETS = {
    "Pluck": dict(wave="Saw", rolloff=1.35, attack=0.004, decay=0.28,
                  sustain=0.10, release=0.30, vib_rate=5.0, vib_depth=0.0,
                  unison=2, detune=6.0, glide=0.0, pulse_width=0.5),
    "Music Box": dict(wave="Sine", rolloff=0.70, attack=0.002, decay=0.55,
                      sustain=0.05, release=0.70, vib_rate=5.0, vib_depth=0.0,
                      unison=1, detune=0.0, glide=0.0, pulse_width=0.5),
    "Electric Piano": dict(wave="Triangle", rolloff=1.00, attack=0.004,
                           decay=0.70, sustain=0.28, release=0.55,
                           vib_rate=4.5, vib_depth=0.01, unison=2, detune=4.0,
                           glide=0.0, pulse_width=0.5),
    "Soft Flute": dict(wave="Sine", rolloff=1.80, attack=0.090, decay=0.22,
                       sustain=0.78, release=0.30, vib_rate=5.5, vib_depth=0.11,
                       unison=1, detune=0.0, glide=0.02, pulse_width=0.5),
    "Analog Lead": dict(wave="Saw", rolloff=0.95, attack=0.012, decay=0.18,
                        sustain=0.65, release=0.22, vib_rate=5.8, vib_depth=0.06,
                        unison=3, detune=12.0, glide=0.04, pulse_width=0.5),
    "Square Chip": dict(wave="Square", rolloff=1.10, attack=0.001, decay=0.10,
                        sustain=0.55, release=0.06, vib_rate=7.0, vib_depth=0.04,
                        unison=1, detune=0.0, glide=0.0, pulse_width=0.5),
    "Pulse Chip": dict(wave="Pulse", rolloff=1.10, attack=0.001, decay=0.12,
                       sustain=0.50, release=0.06, vib_rate=7.0, vib_depth=0.05,
                       unison=1, detune=0.0, glide=0.0, pulse_width=0.25),
    "Marimba": dict(wave="Sine", rolloff=0.55, attack=0.002, decay=0.32,
                    sustain=0.04, release=0.28, vib_rate=5.0, vib_depth=0.0,
                    unison=1, detune=0.0, glide=0.0, pulse_width=0.5),
    "Glass Pad": dict(wave="Triangle", rolloff=1.40, attack=0.220, decay=0.60,
                      sustain=0.70, release=1.10, vib_rate=3.2, vib_depth=0.05,
                      unison=3, detune=9.0, glide=0.06, pulse_width=0.5),
    "Bass": dict(wave="Square", rolloff=1.60, attack=0.006, decay=0.30,
                 sustain=0.45, release=0.18, vib_rate=4.0, vib_depth=0.0,
                 unison=2, detune=3.0, glide=0.05, pulse_width=0.5),
}

GM_PROGRAMS = {
    "Acoustic Grand Piano": 0, "Electric Piano": 4, "Music Box": 10,
    "Marimba": 12, "Celesta": 8, "Acoustic Guitar": 24,
    "Electric Guitar (clean)": 27, "Acoustic Bass": 32, "Violin": 40,
    "Cello": 42, "String Ensemble": 48, "Trumpet": 56, "Alto Sax": 65,
    "Oboe": 68, "Flute": 73, "Pan Flute": 75, "Square Lead": 80,
    "Saw Lead": 81, "Warm Pad": 89, "Kalimba": 108,
}


def midi_to_freq(midi):
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def note_to_midi(name):
    return 12 * (int(name[-1]) + 1) + NOTE_NAMES.index(name[:-1])


def midi_to_name(midi):
    return f"{NOTE_NAMES[int(midi) % 12]}{int(midi) // 12 - 1}"


def build_pitch_pool(root_midi, scale, lo_midi, hi_midi):
    """Every scale pitch between lo and hi inclusive, ascending."""
    pcs = {s % 12 for s in scale}
    root_pc = root_midi % 12
    pool = [m for m in range(int(lo_midi), int(hi_midi) + 1)
            if (m - root_pc) % 12 in pcs]
    return pool or [int(root_midi)]


def chord_pitch_classes(root_midi, scale, degree, seventh=False):
    """Pitch classes of the triad/seventh built on a scale degree."""
    n = len(scale)
    steps = (0, 2, 4, 6) if seventh else (0, 2, 4)
    out = set()
    for st in steps:
        out.add((root_midi + scale[(degree + st) % n]) % 12)
    return out


# ===========================================================================
#  MELODY GENERATION
# ===========================================================================
#
#  The engine is motif-first, not note-first, because a melody is not a
#  sequence of acceptable pitches - it is one short idea, restated.
#
#  1. A motif is held as an abstract idea: a rhythm, plus a sequence of
#     scale-step offsets measured from its own first note.  Storing pitch
#     relatively means contour and intervals travel with the motif, so it
#     stays recognisable when it starts somewhere else.
#  2. Many candidate motifs are written and scored for memorability -
#     distinct contour, varied intervals, a mix of small steps and larger
#     leaps, a singable span, rhythmic shape, and few enough ideas to follow.
#     The best candidate wins.  This search is what separates the output from
#     anything a random walk would produce.
#  3. A form lays the motif out as statement and answer: the antecedent ends
#     open (a question), the consequent resolves to the tonic (its answer).
#  4. Variations come from named operators, each of which preserves at least
#     one of {rhythm, contour, intervals}.  So a restatement elaborates the
#     idea - echoing its rhythm, its shape or its ending - instead of
#     wandering off into something unrelated.
#  5. Contrasting sections (B, C) are derived from the motif and made to
#     quote its closing interval, so new material still argues for the
#     original idea rather than abandoning it.


class Note:
    """One melodic event, positioned in beats."""
    __slots__ = ("beat", "dur", "midi", "vel", "label")

    def __init__(self, beat, dur, midi, vel, label="A"):
        self.beat = float(beat)
        self.dur = float(dur)
        self.midi = int(midi)
        self.vel = float(vel)
        self.label = str(label)

    def __repr__(self):
        return (f"Note({self.beat:.3f}, {self.dur:.3f}, "
                f"{midi_to_name(self.midi)}, {self.vel:.2f}, {self.label})")


def _sign(x):
    return 0 if x == 0 else (1 if x > 0 else -1)


class Motif:
    """A musical idea: a rhythm plus scale-steps relative to its first note."""
    __slots__ = ("rhythm", "steps")

    def __init__(self, rhythm, steps):
        rhythm = [(int(s), max(1, int(d))) for s, d in rhythm]
        steps = [int(x) for x in steps]
        k = min(len(rhythm), len(steps))
        self.rhythm = rhythm[:k]
        self.steps = steps[:k]

    def copy(self):
        return Motif(list(self.rhythm), list(self.steps))

    @property
    def n(self):
        return len(self.steps)

    def intervals(self):
        return [b - a for a, b in zip(self.steps, self.steps[1:])]

    def contour(self):
        return tuple(_sign(i) for i in self.intervals())

    def durations(self):
        return [d for _, d in self.rhythm]

    def onsets(self):
        return [s for s, _ in self.rhythm]

    def span(self):
        return (max(self.steps) - min(self.steps)) if self.steps else 0

    def end_slot(self):
        return max((s + d) for s, d in self.rhythm) if self.rhythm else 0

    def describe(self, sub=2):
        """Short human-readable signature of the idea."""
        arrows = {1: "/", -1: "\\", 0: "-"}
        shape = "".join(arrows[s] for s in self.contour())
        iv = " ".join(f"{i:+d}" for i in self.intervals())
        rh = " ".join(f"{d / sub:g}" for d in self.durations())
        return shape, iv, rh


# ---------------------------------------------------------------------------
#  Writing candidate motifs
# ---------------------------------------------------------------------------

def _gen_rhythm(rng, slots, sub, n_notes, syncopation):
    """Choose onsets, favouring beats unless syncopation is asked for."""
    slots = max(1, int(slots))
    n_notes = max(1, min(int(n_notes), slots))
    chosen = {0}
    cands = list(range(1, slots))
    if cands and n_notes > 1:
        w = np.array([1.0 if (s % sub == 0) else (0.18 + 0.80 * syncopation)
                      for s in cands], dtype=float)
        w /= w.sum()
        k = min(n_notes - 1, len(cands))
        picked = rng.choice(len(cands), size=k, replace=False, p=w)
        for i in np.atleast_1d(picked):
            chosen.add(cands[int(i)])
    onsets = sorted(chosen)
    out = []
    for i, s in enumerate(onsets):
        nxt = onsets[i + 1] if i + 1 < len(onsets) else slots
        out.append((s, max(1, nxt - s)))
    return out


def _shape_durations(rhythm, rng, note_length, rest_density):
    """Clip note lengths to open up rhythmic space."""
    out = []
    last = len(rhythm) - 1
    for i, (s, gap) in enumerate(rhythm):
        d = gap
        if note_length < 0.5:
            d = max(1, int(round(gap * (0.35 + 1.30 * note_length))))
        if i < last and gap > 1 and rng.random() < rest_density * 0.7:
            d = max(1, min(d, gap - 1))
        out.append((s, d))
    return out


def _gen_steps(rng, n, max_leap_steps, step_bias, span_steps, contour_fn,
               leap_recovery):
    """Walk a shape in scale-step space, recovering from leaps by step."""
    n = max(2, int(n))
    lo = -(span_steps // 3)
    hi = lo + span_steps
    steps = [0]
    pending_leap = 0
    for k in range(1, n):
        u = k / (n - 1)
        target = lo + float(contour_fn(u)) * (hi - lo)
        cur = steps[-1]

        if leap_recovery and pending_leap:
            direction = -1 if pending_leap > 0 else 1
            size = 1 if rng.random() < 0.78 else 2
        else:
            if target > cur:
                direction = 1
            elif target < cur:
                direction = -1
            else:
                direction = 1 if rng.random() < 0.5 else -1
            pull = min(1.0, abs(target - cur) / max(1.0, span_steps * 0.4))
            if rng.random() > pull and rng.random() < 0.4:
                direction *= -1
            if rng.random() < step_bias:
                size = 1 if rng.random() < 0.72 else 2
            else:
                size = int(rng.integers(3, max(4, max_leap_steps + 1)))

        nxt = int(np.clip(cur + direction * size, lo, hi))
        if nxt == cur:
            nxt = cur + (1 if cur < hi else -1)
            nxt = int(np.clip(nxt, lo, hi))
        steps.append(nxt)
        move = nxt - cur
        pending_leap = move if abs(move) >= 3 else 0
    return steps


def _longest_run(seq):
    best = run = 1
    for a, b in zip(seq, seq[1:]):
        run = run + 1 if a == b else 1
        best = max(best, run)
    return best if seq else 0


def longest_silence(m, unit_slots):
    """Biggest run of empty slots in a unit, including before and after."""
    if not m.rhythm:
        return unit_slots
    worst = 0
    prev_end = 0
    for sl, d in m.rhythm:
        worst = max(worst, sl - prev_end)
        prev_end = max(prev_end, sl + d)
    return max(worst, unit_slots - prev_end)


def fill_unit(m, p, ctx, rng):
    """Close long silences so a unit does not leave bars standing empty.

    Small holes are closed by sustaining the note in front of them.  A long
    hole at the end is answered by restating the idea in the space - which is
    what diminution and fragmentation are for in the first place, rather than
    compressing the motif and leaving the rest of the bar silent.
    """
    unit_slots = ctx["unit_slots"]
    sub = ctx["sub"]
    max_gap = max(1, int(round(float(p["max_gap"]) * sub)))
    if m.n == 0 or unit_slots <= 0:
        return m

    rhythm = list(m.rhythm)
    steps = list(m.steps)

    # A long rest before the first note: start the idea earlier.
    if rhythm[0][0] > max_gap:
        shift = rhythm[0][0] - max_gap
        rhythm = [(sl - shift, d) for sl, d in rhythm]

    # Internal holes: sustain the preceding note until only max_gap is left.
    for i in range(len(rhythm) - 1):
        sl, d = rhythm[i]
        nxt = rhythm[i + 1][0]
        if nxt - (sl + d) > max_gap:
            rhythm[i] = (sl, max(d, nxt - sl - max_gap))

    # A hole at the end: restate the idea into it.
    origin = m.rhythm[0][0]
    end = max(sl + d for sl, d in rhythm)
    guard = 0
    while unit_slots - end > max_gap and guard < 8:
        guard += 1
        shift = int(rng.choice([0, 0, -2, -1, 1, 2]))
        added = 0
        for (sl, d), st in zip(m.rhythm, m.steps):
            new_sl = end + (sl - origin)
            if new_sl >= unit_slots:
                break
            rhythm.append((new_sl, max(1, min(d, unit_slots - new_sl))))
            steps.append(st + shift)
            added += 1
        if not added:
            break
        end = max(sl + d for sl, d in rhythm)

    return Motif(rhythm, steps)


def motif_fitness(m, p, pool, anchor, sub, bpb, unit_slots=None):
    """Score a motif for memorability.  Higher is better.

    Returns (total, breakdown) so the UI can show why a motif was chosen.
    """
    if m.n < 2:
        return -1e6, {}

    npool = len(pool)
    idxs = [int(np.clip(anchor + s, 0, npool - 1)) for s in m.steps]
    pitches = [pool[i] for i in idxs]
    semis = [b - a for a, b in zip(pitches, pitches[1:])]
    absd = [abs(x) for x in semis]
    n = m.n

    # --- contour: a distinct shape, not a flat line and not a scribble ----
    signs = [_sign(x) for x in semis]
    moving = [s for s in signs if s != 0]
    changes = sum(1 for a, b in zip(moving, moving[1:]) if a != b)
    ideal = min(3, max(1, int(round((n - 1) * 0.4))))
    flat = signs.count(0) / max(1, len(signs))
    if not moving:
        contour = -1.5
    else:
        contour = 1.0 - abs(changes - ideal) / max(1.0, n - 1.0)
        contour -= 0.9 * flat * (0.4 + float(p["note_repeat"]))
        contour -= 0.25 * max(0, _longest_run(signs) - 3)

    # --- intervals: varied, mixing small steps with larger leaps ----------
    distinct = len(set(absd)) / max(1, len(absd))
    has_step = any(1 <= d <= 2 for d in absd)
    has_leap = any(d >= 3 for d in absd)
    mix = 1.0 if (has_step and has_leap) else (0.45 if has_step or has_leap else 0.0)
    intervals = 0.55 * distinct + 0.45 * mix
    intervals -= 0.30 * max(0, _longest_run(absd) - 2)

    # --- singability: a span a voice can carry, leaps resolved by step ----
    span = max(pitches) - min(pitches)
    cap = float(p["singable_span"])
    max_leap = float(p["max_leap"])
    over = max(0.0, span - cap)
    oversize = sum(1 for d in absd if d > max_leap)
    rec_total = rec_ok = 0
    for i, d in enumerate(absd):
        if d >= 3 and i + 1 < len(semis):
            rec_total += 1
            if _sign(semis[i + 1]) == -_sign(semis[i]) and absd[i + 1] <= 2:
                rec_ok += 1
    recovery = (rec_ok / rec_total) if rec_total else 1.0
    singable = 1.0 - 0.10 * over - 0.55 * oversize
    singable += 0.30 * recovery if bool(p["leap_recovery"]) else 0.0

    # --- rhythm: two or three note lengths, a long note to land on --------
    durs = m.durations()
    nd = len(set(durs))
    shortest, longest = min(durs), max(durs)
    rhythm = 0.5 if 2 <= nd <= 3 else (0.12 if nd == 1 else 0.25)
    if longest >= 2 * shortest:
        rhythm += 0.28
    if durs[-1] >= max(durs[:-1]) if len(durs) > 1 else False:
        rhythm += 0.22
    off = sum(1 for s in m.onsets() if s % sub != 0) / max(1, len(durs))
    rhythm += 0.22 * (1.0 - abs(off - float(p["syncopation"])))

    # --- simplicity: easy to follow, easy to remember ---------------------
    simple = 1.0 - 0.18 * max(0, nd - 3) - 0.14 * max(0, n - 7) \
        - 0.30 * max(0, 3 - n)

    # --- cadence: approach the final note by step -------------------------
    cadence = 0.25 if absd and absd[-1] <= 2 else 0.0

    # --- coverage: no bar left standing empty -----------------------------
    silence = 0
    coverage = 0.0
    if unit_slots:
        silence = longest_silence(m, unit_slots)
        allowed = max(1.0, float(p["max_gap"]) * sub)
        coverage = -1.10 * max(0.0, silence - allowed) / allowed

    w_contour = 0.6 + 1.6 * float(p["contour_strength"])
    w_interval = 0.5 + 1.6 * float(p["interval_variety"])
    total = (w_contour * contour
             + w_interval * intervals
             + 1.30 * singable
             + 0.95 * rhythm
             + 0.70 * simple
             + cadence
             + coverage)

    return total, dict(contour=contour, intervals=intervals, singable=singable,
                       rhythm=rhythm, simple=simple, span=span,
                       changes=changes, recovery=recovery, notes=n,
                       coverage=coverage, silence=silence)


def pick_motif(p, rng, ctx, candidates=None):
    """Write many motifs and keep the most memorable one."""
    sub, bpb = ctx["sub"], ctx["bpb"]
    slots = ctx["unit_slots"]
    pool, anchor = ctx["pool"], ctx["anchor"]
    scale_len = len(ctx["scale"])

    n_cand = int(candidates if candidates is not None else p["candidates"])
    n_cand = max(1, n_cand)
    contour_fn = CONTOURS.get(p["contour"], CONTOURS["Arch"])
    span_steps = max(2, int(round(float(p["singable_span"]) * scale_len / 12.0)))
    max_leap_steps = max(1, int(round(float(p["max_leap"]) * scale_len / 12.0)))
    want_notes = int(round(float(p["motif_notes"])))

    best = None
    best_score = -1e18
    best_break = {}
    for _ in range(n_cand):
        n_notes = want_notes
        if rng.random() < 0.35:
            n_notes = int(np.clip(want_notes + rng.integers(-1, 2), 2, 12))
        rhythm = _gen_rhythm(rng, slots, sub, n_notes, float(p["syncopation"]))
        rhythm = _shape_durations(rhythm, rng, float(p["note_length"]),
                                  float(p["rest_density"]))
        steps = _gen_steps(rng, len(rhythm), max_leap_steps,
                           float(p["stepwise"]), span_steps, contour_fn,
                           bool(p["leap_recovery"]))
        m = Motif(rhythm, steps)
        score, br = motif_fitness(m, p, pool, anchor, sub, bpb, slots)
        if score > best_score:
            best, best_score, best_break = m, score, br
    return best, best_score, best_break


# ---------------------------------------------------------------------------
#  Variation operators
# ---------------------------------------------------------------------------
#  Every operator names what it keeps intact.  "rhythm", "contour" and
#  "intervals" are exact claims about the whole motif; "elaboration" means the
#  idea is developed (fragmented, extended, decorated) rather than restated
#  note for note.

def _v_repeat(m, rng, p, ctx):
    return m.copy(), {"rhythm", "contour", "intervals"}


def _v_sequence(m, rng, p, ctx):
    k = int(rng.choice([-3, -2, -1, 1, 2, 3]))
    return Motif(m.rhythm, [s + k for s in m.steps]), \
        {"rhythm", "contour", "intervals"}


def _v_recadence(m, rng, p, ctx):
    steps = list(m.steps)
    if len(steps) >= 2:
        steps[-1] = steps[-2] + int(rng.choice([-2, -1, 1, 2]))
    return Motif(m.rhythm, steps), {"rhythm"}


def _v_ornament(m, rng, p, ctx):
    """Decorate one note with a passing tone - the skeleton is untouched."""
    r, s = list(m.rhythm), list(m.steps)
    cands = [i for i, (_sl, d) in enumerate(r) if d >= 2 and i + 1 < len(r)]
    if not cands:
        return m.copy(), {"rhythm", "contour", "intervals"}
    i = int(rng.choice(cands))
    sl, d = r[i]
    half = max(1, d // 2)
    step_dir = _sign(s[i + 1] - s[i]) or 1
    r[i] = (sl, half)
    r.insert(i + 1, (sl + half, max(1, d - half)))
    s.insert(i + 1, s[i] + step_dir)
    return Motif(r, s), {"elaboration"}


def _v_diminution(m, rng, p, ctx):
    r = [(sl // 2, max(1, d // 2)) for sl, d in m.rhythm]
    return Motif(r, list(m.steps)), {"contour", "intervals"}


def _v_augmentation(m, rng, p, ctx):
    r = [(sl * 2, d * 2) for sl, d in m.rhythm]
    return Motif(r, list(m.steps)), {"contour", "intervals"}


def _v_truncate(m, rng, p, ctx):
    if m.n <= 2:
        return m.copy(), {"rhythm", "contour", "intervals"}
    k = int(np.clip(m.n - rng.integers(1, max(2, m.n // 2 + 1)), 2, m.n))
    return Motif(m.rhythm[:k], m.steps[:k]), {"elaboration"}


def _v_extend(m, rng, p, ctx):
    """Echo the motif's ending - its last gesture, sequenced once more."""
    if m.n < 2:
        return m.copy(), {"rhythm", "contour", "intervals"}
    r, s = list(m.rhythm), list(m.steps)
    last_sl, last_d = r[-1]
    iv = s[-1] - s[-2]
    r.append((last_sl + last_d, last_d))
    s.append(s[-1] + iv)
    return Motif(r, s), {"elaboration"}


def _v_invert(m, rng, p, ctx):
    a = m.steps[0]
    return Motif(m.rhythm, [a - (s - a) for s in m.steps]), {"rhythm"}


def _v_retrograde(m, rng, p, ctx):
    return Motif(m.rhythm, list(reversed(m.steps))), {"rhythm"}


def _v_expand(m, rng, p, ctx):
    f = float(rng.choice([1.5, 2.0]))
    a = m.steps[0]
    return Motif(m.rhythm, [a + int(round((s - a) * f)) for s in m.steps]), \
        {"rhythm", "contour"}


def _v_rhythm_shift(m, rng, p, ctx):
    room = ctx["unit_slots"] - m.end_slot()
    shift = 1 if room >= 1 else 0
    r = [(sl + shift, d) for sl, d in m.rhythm]
    return Motif(r, list(m.steps)), {"contour", "intervals"}


def _v_new_rhythm(m, rng, p, ctx):
    """Same pitches, different rhythm - a genuinely different motif."""
    r = _gen_rhythm(rng, ctx["unit_slots"], ctx["sub"], m.n,
                    float(p["syncopation"]))
    r = _shape_durations(r, rng, float(p["note_length"]),
                         float(p["rest_density"]))
    steps = list(m.steps)[:len(r)]
    return Motif(r[:len(steps)], steps), {"contour", "intervals"}


VAR_LIGHT = [_v_repeat, _v_sequence, _v_recadence, _v_ornament]
VAR_MEDIUM = [_v_diminution, _v_augmentation, _v_truncate, _v_extend,
              _v_rhythm_shift]
VAR_STRONG = [_v_invert, _v_retrograde, _v_expand, _v_new_rhythm]

VAR_NAMES = {
    _v_repeat: "repeat", _v_sequence: "sequence", _v_recadence: "new ending",
    _v_ornament: "ornament", _v_diminution: "diminution",
    _v_augmentation: "augmentation", _v_truncate: "fragment",
    _v_extend: "extend", _v_rhythm_shift: "displace",
    _v_invert: "inversion", _v_retrograde: "retrograde",
    _v_expand: "widen intervals", _v_new_rhythm: "new rhythm",
}


def vary(m, rng, p, ctx, strength, rounds=1):
    """Apply variation operators, keeping at least one property intact."""
    out = m
    used = []
    for _ in range(max(1, int(rounds))):
        w = np.array([max(0.06, 1.0 - strength),
                      0.22 + 0.55 * strength,
                      0.04 + 0.80 * strength], dtype=float)
        w /= w.sum()
        pool = [VAR_LIGHT, VAR_MEDIUM, VAR_STRONG][int(rng.choice(3, p=w))]
        op = pool[int(rng.integers(0, len(pool)))]
        out, _kept = op(out, rng, p, ctx)
        used.append(VAR_NAMES.get(op, "vary"))
        if out.n < 2:
            out = m.copy()
            break
    return out, used


def make_contrast(m, rng, p, ctx, strength=1.0):
    """New-feeling material, derived from the motif and quoting its ending."""
    if rng.random() < 0.5:
        out, _ = _v_invert(m, rng, p, ctx)
        out, _ = _v_expand(out, rng, p, ctx)
    else:
        out, _ = _v_new_rhythm(m, rng, p, ctx)
        out, _ = _v_sequence(out, rng, p, ctx)
    if rng.random() < 0.5 * strength:
        out, _ = _v_rhythm_shift(out, rng, p, ctx)
    # Relate it back: close with the motif's own final interval.
    if out.n >= 2 and m.n >= 2:
        steps = list(out.steps)
        steps[-1] = steps[-2] + (m.steps[-1] - m.steps[-2])
        out = Motif(out.rhythm, steps)
    return out


def make_development(m, rng, p, ctx):
    """Fragment the motif and sequence the fragment - classic development."""
    frag, _ = _v_truncate(m, rng, p, ctx)
    seq, _ = _v_sequence(frag, rng, p, ctx)
    off = frag.end_slot()
    rhythm = list(frag.rhythm) + [(sl + off, d) for sl, d in seq.rhythm]
    steps = list(frag.steps) + list(seq.steps)
    return Motif(rhythm, steps)


# ---------------------------------------------------------------------------
#  Form
# ---------------------------------------------------------------------------
#  Each entry is a list of (label, cadence) units.  "half" ends open - the
#  question.  "close" resolves to the tonic - the answer.

FORMS = {
    "Period (a a' a a'')": [("A", None), ("A'", "half"),
                            ("A", None), ("A''", "close")],
    "Sentence (a a' dev cad)": [("A", None), ("A'", None),
                                ("D", "half"), ("A''", "close")],
    "AABA": [("A", None), ("A", "half"), ("B", None), ("A''", "close")],
    "ABAC": [("A", None), ("B", "half"), ("A'", None), ("C", "close")],
    "AAB (bar form)": [("A", None), ("A'", "half"), ("B", "close")],
    "Chain (a a' a'' a''')": [("A", None), ("A'", None),
                              ("A''", "half"), ("A'''", "close")],
    "Call & response (a b a b')": [("A", None), ("B", "half"),
                                   ("A", None), ("B'", "close")],
}


def _instantiate(label, motif, rng, p, ctx, strength, cache):
    """Build the motif variant a form slot calls for (cached per cycle)."""
    if label in cache:
        return cache[label]

    base = label.rstrip("'")
    ticks = len(label) - len(base)

    if base == "A":
        if ticks == 0:
            out, used = (motif.copy(), ["statement"]) if strength < 0.999 \
                else vary(motif, rng, p, ctx, strength * 0.4, 1)
        else:
            out, used = vary(motif, rng, p, ctx, strength, min(2, ticks))
    elif base == "D":
        out, used = make_development(motif, rng, p, ctx), ["development"]
    else:
        out = make_contrast(motif, rng, p, ctx, strength)
        used = ["contrast"]
        if ticks:
            out, extra = vary(out, rng, p, ctx, strength * 0.7, 1)
            used += extra

    # Operators such as diminution and fragmentation shorten the material;
    # fill the unit rather than leave bars of silence behind them.
    before = longest_silence(out, ctx["unit_slots"])
    out = fill_unit(out, p, ctx, rng)
    if longest_silence(out, ctx["unit_slots"]) < before - 1:
        used = used + ["filled"]

    cache[label] = (out, used)
    return cache[label]


def _fit_register(idxs, lo, hi):
    """Bring a unit inside the playing register by moving it bodily.

    Transposing keeps every interval intact; clipping note by note would
    flatten the contour into a smear against the ceiling, which is exactly
    the scattered look a melody must not have.
    """
    if not idxs:
        return idxs
    if max(idxs) - min(idxs) > hi - lo:
        return [int(np.clip(i, lo, hi)) for i in idxs]
    shift = 0
    if min(idxs) < lo:
        shift = lo - min(idxs)
    elif max(idxs) > hi:
        shift = hi - max(idxs)
    return [i + shift for i in idxs]


def _place_unit(m, label, cadence, start_bar, p, ctx, out,
                pitch_cache=None, key=None):
    """Lay one form unit down as real notes.

    Pitches resolved for a given (label, cadence) are cached and replayed, so
    a restated unit really is a restatement - repetition is what makes a
    melody stick, and re-fitting it to each bar's harmony would erode it.
    """
    pool, sub, bpb = ctx["pool"], ctx["sub"], ctx["bpb"]
    anchor, scale, root_midi = ctx["anchor"], ctx["scale"], ctx["root_midi"]
    prog, rng = ctx["prog"], ctx["rng"]
    unit_slots, total_beats = ctx["unit_slots"], ctx["total_beats"]
    npool = len(pool)
    root_pc = root_midi % 12

    cached = pitch_cache.get(key) if (pitch_cache is not None and key) else None
    if cached is not None:
        trio, idxs = [tuple(t) for t in cached[0]], list(cached[1])
    else:
        trio = [(sl, min(d, unit_slots - sl), st)
                for (sl, d), st in zip(m.rhythm, m.steps) if sl < unit_slots]
        if not trio:
            return
        reg_lo, reg_hi = ctx["reg_lo"], ctx["reg_hi"]
        idxs = [int(anchor + st) for _sl, _d, st in trio]
        idxs = _fit_register(idxs, reg_lo, reg_hi)

        # Fit the guide harmony by nudging only structural notes, so the
        # motif's own intervals survive.
        chord_bias = float(p["chord_bias"])
        if prog and chord_bias > 0:
            last_i = len(trio) - 1
            for j, (sl, _d, _st) in enumerate(trio):
                downbeat = (sl % (bpb * sub) == 0)
                if not (downbeat or j == 0 or j == last_i):
                    continue
                if rng.random() > chord_bias:
                    continue
                bar = start_bar + int((sl / sub) // bpb)
                degree = prog[bar % len(prog)] % len(scale)
                pcs = chord_pitch_classes(root_midi, scale, degree,
                                          bool(p["sevenths"]))
                if pool[idxs[j]] % 12 in pcs:
                    continue
                for off in (1, -1, 2, -2):
                    c = idxs[j] + off
                    if reg_lo <= c <= reg_hi and pool[c] % 12 in pcs:
                        idxs[j] = c
                        break

        # Cadence: the question stays open, the answer resolves.  A closing
        # cadence always lands on the tonic - that resolution is the point -
        # while an open one only moves if a suitable note is nearby.
        if cadence and bool(p["resolve"]):
            if cadence == "close":
                targets, cap = {root_pc}, npool
            else:
                targets = {(root_midi + scale[4 % len(scale)]) % 12,
                           (root_midi + scale[1 % len(scale)]) % 12}
                cap = 4
            last = idxs[-1]
            best, best_d = last, npool + 1
            for c in range(reg_lo, reg_hi + 1):
                if pool[c] % 12 in targets and abs(c - last) < best_d:
                    best, best_d = c, abs(c - last)
            if best_d <= cap:
                idxs[-1] = best
            elif cadence != "close" and pool[idxs[-1]] % 12 == root_pc:
                # an open ending must not sound closed
                for off in (-1, 1, -2, 2):
                    c = idxs[-1] + off
                    if reg_lo <= c <= reg_hi and pool[c] % 12 != root_pc:
                        idxs[-1] = c
                        break
            sl, d, st = trio[-1]
            trio[-1] = (sl, max(d, unit_slots - sl), st)

        if pitch_cache is not None and key:
            pitch_cache[key] = ([tuple(t) for t in trio], list(idxs))

    pitches = [pool[i] for i in idxs]
    pmin, pmax = min(pitches), max(pitches)
    prange = max(1, pmax - pmin)
    n = len(trio)

    arc = float(p["dyn_arc"])
    accent = float(p["accent"])
    gate = float(p["gate"])
    swing = float(p["swing"])
    hum_t = float(p["humanize_time"])
    hum_v = float(p["humanize_vel"])
    base_beat = start_bar * bpb

    for j, ((sl, d, _st), pitch) in enumerate(zip(trio, pitches)):
        beat = base_beat + sl / sub
        if swing > 0 and sub >= 2 and (sl % 2 == 1):
            beat += (0.5 / sub) * swing * 0.66

        strong = (sl % sub == 0)
        downbeat = (sl % (bpb * sub) == 0)
        u = j / max(1, n - 1)
        # A phrase envelope that begins with weight, swells, then relaxes -
        # a plain sine would mute the downbeat it starts on.
        shape = 0.55 + 0.45 * math.sin(math.pi * u) - 0.25 * u
        height = (pitch - pmin) / prange

        vel = 0.55
        vel += accent * (0.22 if downbeat else (0.11 if strong else 0.0))
        vel += arc * (0.14 * shape + 0.06 * height)
        if cadence == "close" and j == n - 1:
            vel -= 0.08 * arc

        if hum_t > 0 and j > 0:
            beat += float(rng.normal(0, 0.030 * hum_t))
        if hum_v > 0:
            vel += float(rng.normal(0, 0.12 * hum_v))

        beat = max(0.0, beat)
        if beat >= total_beats:
            continue
        vel = float(np.clip(vel, 0.10, 1.0))
        span = min(d / sub, total_beats - beat)
        out.append(Note(beat, max(0.05, span * gate), pitch, vel, label))


def close_gaps(notes, max_gap, total_beats):
    """Sustain notes so no audible silence runs longer than max_gap.

    Articulation and note-length settings compound with the rhythm's own
    spacing, so the limit is enforced on the finished line - where the real
    distance between one note stopping and the next starting is known.
    """
    if not notes:
        return notes
    for i, nt in enumerate(notes):
        nxt = notes[i + 1].beat if i + 1 < len(notes) else total_beats
        room = nxt - nt.beat
        if room <= 0.0:
            continue
        if nt.beat + nt.dur < nxt - max_gap:
            nt.dur = max(nt.dur, room - max_gap)
        # Never run past the next onset: the line is one voice, and an
        # overlap on a repeated pitch would cut the second note short on
        # export.  The floor only applies when there is room for it.
        nt.dur = min(max(0.02, nt.dur), room) if room > 0.02 else room
    return notes


def generate_melody(p, rng=None, info=None):
    """Write a melody: find a motif, then state, vary and answer it.

    Pass a dict as `info` to receive the chosen motif and its scoring.
    """
    rng = rng or np.random.default_rng(int(p["seed"]))

    scale = SCALES.get(p["scale"], SCALES["Major (Ionian)"])
    root_midi = note_to_midi(p["root_note"])
    lo = root_midi + int(round(p["range_lo"]))
    hi = root_midi + int(round(p["range_hi"]))
    if hi - lo < 12:
        hi = lo + 12
    pool = build_pitch_pool(root_midi, scale, lo, hi)

    bpb = int(p["beats_per_bar"])
    sub = int(p["subdivision"])
    bars = max(1, int(round(p["bars"])))
    unit_bars = max(1, int(round(p["unit_bars"])))
    total_beats = bars * bpb

    # Anchor the motif on a stable degree near the middle of the register,
    # so step 0 of the idea is the tonic.
    mid = (lo + hi) / 2.0
    stable = [i for i, m in enumerate(pool) if m % 12 == root_midi % 12]
    if stable:
        anchor = min(stable, key=lambda i: abs(pool[i] - mid))
    else:
        anchor = len(pool) // 2

    # The playing register.  Variation operators may transpose and widen the
    # idea, so every unit is fitted back inside this window - a melody lives
    # in a voice-sized band, not wherever the arithmetic lands.
    span_steps = max(2, int(round(float(p["singable_span"]) * len(scale) / 12.0)))
    width = span_steps + 2          # the motif, plus room to sequence it
    reg_lo = max(0, anchor - width // 3 - 1)
    reg_hi = min(len(pool) - 1, reg_lo + width)
    if reg_hi - reg_lo < width:     # ran out of room at the top
        reg_lo = max(0, reg_hi - width)
    if reg_hi <= reg_lo:
        reg_lo, reg_hi = 0, len(pool) - 1

    ctx = dict(pool=pool, anchor=anchor, scale=scale, root_midi=root_midi,
               prog=PROGRESSIONS.get(p["progression"]), rng=rng, sub=sub,
               bpb=bpb, unit_slots=max(1, unit_bars * bpb * sub),
               total_beats=total_beats, reg_lo=reg_lo, reg_hi=reg_hi,
               span_steps=span_steps)

    motif, score, breakdown = pick_motif(p, rng, ctx)

    form = FORMS.get(p["form"], FORMS["Period (a a' a a'')"])
    units = max(1, int(math.ceil(bars / unit_bars)))
    base_strength = float(p["variation"])

    notes = []
    plan = []
    sections = []
    cache = {}
    pitch_cache = {}
    for i in range(units):
        start_bar = i * unit_bars
        if start_bar >= bars:
            break
        slot = i % len(form)
        cycle = i // len(form)
        if slot == 0 and cycle > 0:
            cache = {}            # a new cycle may re-colour the material
            pitch_cache = {}
        label, cadence = form[slot]
        strength = min(1.0, base_strength + 0.12 * cycle)
        variant, used = _instantiate(label, motif, rng, p, ctx, strength, cache)
        shown = label + ("" if cycle == 0 else f"({cycle + 1})")
        _place_unit(variant, shown, cadence, start_bar, p, ctx, notes,
                    pitch_cache, (label, cadence))
        plan.append((shown, cadence, used))
        # The unit's true span on the timeline, independent of where its
        # notes happen to start and stop.
        sections.append((shown, start_bar * bpb,
                         min((start_bar + unit_bars) * bpb, total_beats)))

    notes.sort(key=lambda nt: nt.beat)
    close_gaps(notes, float(p["max_gap"]), total_beats)

    if info is not None:
        shape, iv, rh = motif.describe(sub)
        info.update(motif=motif, score=score, breakdown=breakdown,
                    shape=shape, intervals=iv, rhythm=rh, plan=plan,
                    sections=sections, form=p["form"],
                    anchor_pitch=pool[anchor], register=(pool[reg_lo],
                                                         pool[reg_hi]),
                    candidates=int(p["candidates"]))
    return notes


def sections_from_notes(notes):
    """Group consecutive notes by form label, for display."""
    out = []
    for nt in notes:
        if out and out[-1][0] == nt.label:
            out[-1][2] = max(out[-1][2], nt.beat + nt.dur)
        else:
            out.append([nt.label, nt.beat, nt.beat + nt.dur])
    return [(lbl, a, b) for lbl, a, b in out]


# ===========================================================================
#  SYNTHESIS
# ===========================================================================

def _partials(wave, rolloff, pulse_width, n_max):
    """Return [(harmonic_number, amplitude), ...] for a waveform."""
    out = []
    if wave == "Sine":
        out = [(1, 1.0)]
    elif wave == "Triangle":
        for k in range(1, n_max + 1, 2):
            sign = 1.0 if ((k - 1) // 2) % 2 == 0 else -1.0
            out.append((k, sign / (k * k)))
    elif wave == "Square":
        for k in range(1, n_max + 1, 2):
            out.append((k, 1.0 / k))
    elif wave == "Saw":
        for k in range(1, n_max + 1):
            out.append((k, 1.0 / k))
    elif wave == "Pulse":
        w = float(np.clip(pulse_width, 0.05, 0.95))
        for k in range(1, n_max + 1):
            a = (2.0 / (k * math.pi)) * math.sin(math.pi * k * w)
            if abs(a) > 1e-4:
                out.append((k, a))
    else:
        out = [(1, 1.0)]

    # Extra spectral tilt.  rolloff 0 = raw, higher = darker.
    shaped = []
    for k, a in out:
        shaped.append((k, a / (k ** max(0.0, rolloff - 1.0)) if k > 1 else a))
    norm = sum(abs(a) for _, a in shaped) or 1.0
    return [(k, a / norm) for k, a in shaped]


def _adsr(n, sr, attack, decay, sustain, release):
    env = np.zeros(n, dtype=np.float32)
    a = max(1, int(attack * sr))
    d = max(1, int(decay * sr))
    r = max(1, int(release * sr))
    a = min(a, n)
    d = min(d, max(0, n - a))
    body = n - a - d
    if body < 0:
        body = 0
    if a > 0:
        env[:a] = np.linspace(0.0, 1.0, a, dtype=np.float32)
    if d > 0:
        env[a:a + d] = np.linspace(1.0, sustain, d, dtype=np.float32)
    if body > 0:
        env[a + d:] = sustain
    r = min(r, n)
    if r > 0:
        env[n - r:] *= np.linspace(1.0, 0.0, r, dtype=np.float32)
    return env


def synth_note(midi, dur_s, v, sr=SAMPLE_RATE, prev_midi=None):
    """Render one note to a mono float32 array."""
    tail = float(v["release"])
    total = dur_s + tail
    n = int(total * sr)
    if n <= 1:
        return np.zeros(0, dtype=np.float32)

    t = np.arange(n, dtype=np.float32) / sr
    freq = midi_to_freq(midi)

    # Portamento from the previous pitch.
    glide = float(v["glide"])
    if prev_midi is not None and glide > 0.002 and prev_midi != midi:
        g = min(glide, total * 0.5)
        gn = max(1, int(g * sr))
        ramp = np.ones(n, dtype=np.float32)
        ramp[:gn] = np.linspace(0.0, 1.0, gn, dtype=np.float32)
        start_f = midi_to_freq(prev_midi)
        freq_t = start_f * (freq / start_f) ** ramp
    else:
        freq_t = np.full(n, freq, dtype=np.float32)

    # Vibrato (applied to instantaneous frequency).
    vd = float(v["vib_depth"])
    if vd > 0.001:
        onset = np.clip(t / max(0.08, dur_s * 0.45), 0.0, 1.0)
        lfo = np.sin(2 * np.pi * float(v["vib_rate"]) * t) * vd * onset
        freq_t = freq_t * (2.0 ** (lfo / 12.0))

    phase = 2 * np.pi * np.cumsum(freq_t) / sr

    nyq = sr * 0.45
    n_max = max(1, min(40, int(nyq / max(40.0, freq))))
    parts = _partials(v["wave"], float(v["rolloff"]),
                      float(v["pulse_width"]), n_max)

    unison = max(1, int(round(v["unison"])))
    detune_c = float(v["detune"])
    wave = np.zeros(n, dtype=np.float32)
    for ui in range(unison):
        if unison == 1:
            cents = 0.0
        else:
            cents = detune_c * ((ui / (unison - 1)) * 2.0 - 1.0)
        mult = 2.0 ** (cents / 1200.0)
        for k, amp in parts:
            if k * freq * mult > nyq:
                break
            wave += amp * np.sin(phase * k * mult).astype(np.float32)
    wave /= unison

    env = _adsr(n, sr, float(v["attack"]), float(v["decay"]),
                float(v["sustain"]), float(v["release"]))
    return (wave * env).astype(np.float32)


# ===========================================================================
#  EFFECTS
# ===========================================================================

def _fft_lowpass(x, sr, cutoff, slope=2.0):
    if cutoff >= sr * 0.49:
        return x
    n = len(x)
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    with np.errstate(divide="ignore", invalid="ignore"):
        gain = 1.0 / np.sqrt(1.0 + (freqs / max(20.0, cutoff)) ** (2 * slope))
    return np.fft.irfft(spec * gain, n=n).astype(np.float32)


def _fft_convolve(x, ir):
    n = len(x) + len(ir) - 1
    size = 1 << (n - 1).bit_length()
    out = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir, size), size)
    return out[:len(x)].astype(np.float32)


def _reverb_ir(sr, seconds, rng):
    n = int(max(0.05, seconds) * sr)
    noise = rng.normal(0, 1, n).astype(np.float32)
    decay = np.exp(-np.linspace(0, 6.0, n)).astype(np.float32)
    ir = noise * decay
    # Soften the very start so it reads as space rather than a click.
    head = min(n, int(0.008 * sr))
    if head > 1:
        ir[:head] *= np.linspace(0, 1, head, dtype=np.float32)
    ir = _fft_lowpass(ir, sr, 6500.0, slope=1.5)
    peak = np.max(np.abs(ir)) or 1.0
    return (ir / peak).astype(np.float32)


def _delay(x, sr, time_s, feedback, mix):
    if mix <= 0.001 or time_s <= 0.001:
        return x
    d = max(1, int(time_s * sr))
    out = x.copy()
    gain = feedback
    shift = d
    taps = 0
    while gain > 0.02 and shift < len(x) and taps < 24:
        out[shift:] += x[:len(x) - shift] * gain * mix
        gain *= feedback
        shift += d
        taps += 1
    return out.astype(np.float32)


def _loudness_normalize(x, target_rms=0.105, ceiling=0.89, sr=SAMPLE_RATE):
    """Level to a consistent loudness, then limit - never peak-normalise.

    Peak normalisation pushes every render, sparse or dense, to full scale,
    so a four-note phrase comes out as loud as a run of semiquavers.  This
    measures the loudness of the parts that are actually sounding and leaves
    headroom above it.
    """
    mono = x.mean(axis=1) if x.ndim == 2 else x
    win = max(1, int(0.05 * sr))
    usable = (len(mono) // win) * win
    level = 0.0
    if usable >= win:
        frames = mono[:usable].reshape(-1, win)
        energies = np.sqrt((frames.astype(np.float64) ** 2).mean(axis=1))
        if energies.size:
            floor = max(1e-6, float(energies.max()) * 0.08)
            voiced = energies[energies > floor]
            if voiced.size:
                level = float(np.percentile(voiced, 70))
    if level <= 1e-6:
        level = float(np.sqrt(np.mean(mono.astype(np.float64) ** 2)))
    if level <= 1e-6:
        return x

    y = (x * (target_rms / level)).astype(np.float32)

    # Soft knee limiter so transients round off instead of clipping.
    knee = ceiling * 0.75
    mag = np.abs(y)
    hot = mag > knee
    if np.any(hot):
        excess = (mag[hot] - knee) / max(1e-6, ceiling - knee)
        y[hot] = np.sign(y[hot]) * (knee + (ceiling - knee) * np.tanh(excess))

    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak > 0.97:
        y = y * (0.97 / peak)
    return y.astype(np.float32)


def _drive(x, amount):
    if amount <= 0.001:
        return x
    k = 1.0 + amount * 12.0
    return (np.tanh(x * k) / np.tanh(k)).astype(np.float32)


# ===========================================================================
#  RENDER
# ===========================================================================

def render_melody(notes, p, sr=SAMPLE_RATE, progress=None):
    """Render Notes to a stereo float32 array."""
    bpm = float(p["bpm"])
    beat = 60.0 / bpm
    bars = int(round(p["bars"]))
    bpb = int(p["beats_per_bar"])
    total_beats = bars * bpb

    delay_s = float(p["delay_time"]) * (beat if p["delay_sync"] else 1.0)
    tail = float(p["release"]) + float(p["reverb_size"]) + delay_s * 5.0 + 0.4
    n = int((total_beats * beat + tail) * sr) + 1
    buf = np.zeros(n, dtype=np.float32)

    voice = {k: p[k] for k in ("wave", "rolloff", "attack", "decay", "sustain",
                               "release", "vib_rate", "vib_depth", "unison",
                               "detune", "glide", "pulse_width")}

    prev_midi = None
    count = len(notes)
    for i, nt in enumerate(notes):
        sig = synth_note(nt.midi, nt.dur * beat, voice, sr, prev_midi)
        prev_midi = nt.midi
        if sig.size == 0:
            continue
        start = int(nt.beat * beat * sr)
        if start >= n:
            continue
        end = min(start + sig.size, n)
        buf[start:end] += sig[:end - start] * nt.vel
        if progress and count and i % 16 == 0:
            progress(i / count)

    # ---- Tone / drive ---------------------------------------------------
    tone = float(p["tone"])
    cutoff = 240.0 * (2.0 ** (tone * 7.2))      # ~240 Hz .. ~19 kHz
    buf = _fft_lowpass(buf, sr, cutoff, slope=2.0)
    buf = _drive(buf, float(p["drive"]))

    peak = float(np.max(np.abs(buf))) or 1.0
    buf = buf / peak

    # ---- Delay ----------------------------------------------------------
    buf = _delay(buf, sr, delay_s, float(p["delay_feedback"]),
                 float(p["delay_mix"]))

    # ---- Reverb ---------------------------------------------------------
    rv = float(p["reverb_mix"])
    if rv > 0.001:
        rng = np.random.default_rng(int(p["seed"]) + 7919)
        ir = _reverb_ir(sr, float(p["reverb_size"]), rng)
        wet = _fft_convolve(buf, ir)
        wp = float(np.max(np.abs(wet))) or 1.0
        wet = wet / wp
        buf = buf * (1.0 - rv * 0.55) + wet * rv

    # ---- Stereo ---------------------------------------------------------
    width = float(p["width"])
    if width <= 0.001:
        left = right = buf
    else:
        d = max(1, int(0.013 * width * sr))
        delayed = np.concatenate([np.zeros(d, dtype=np.float32), buf[:-d]])
        left = buf * (1.0 - 0.3 * width) + delayed * (0.3 * width)
        right = buf * (1.0 - 0.3 * width) + \
            np.concatenate([np.zeros(d // 2, dtype=np.float32), buf[:-(d // 2)]]) * (0.3 * width)
        # Mild mid/side widening.
        mid = (left + right) * 0.5
        side = (left - right) * 0.5 * (1.0 + width)
        left, right = mid + side, mid - side

    stereo = np.stack([left, right], axis=1).astype(np.float32)

    # ---- Trim dead air from the tail ------------------------------------
    loud = np.max(np.abs(stereo), axis=1)
    voiced = np.nonzero(loud > 1e-4)[0]
    if voiced.size:
        end = int(min(len(stereo), voiced[-1] + int(0.3 * sr)))
        start_floor = int(total_beats * beat * sr * 0.5)
        stereo = stereo[:max(end, min(start_floor, len(stereo)))]

    # ---- Fades and final gain ------------------------------------------
    fi = min(int(0.02 * sr), len(stereo) // 8)
    fo = min(int(0.35 * sr), len(stereo) // 4)
    if fi > 0:
        stereo[:fi] *= np.linspace(0, 1, fi, dtype=np.float32)[:, None]
    if fo > 0:
        stereo[-fo:] *= np.linspace(1, 0, fo, dtype=np.float32)[:, None]

    # Consistent loudness with headroom, rather than everything slammed to
    # full scale.  output_gain trims around that standard level.
    gain = float(np.clip(p["output_gain"], 0.05, 1.5))
    stereo = _loudness_normalize(stereo, target_rms=0.105 * gain, sr=sr)
    return np.clip(stereo, -1.0, 1.0).astype(np.float32)


# ===========================================================================
#  EXPORT
# ===========================================================================

def export_wav(audio, path, sr=SAMPLE_RATE):
    """Write 16-bit PCM.  Accepts mono (1-D or N x 1) or N x channels."""
    if HAS_SF:
        sf.write(path, audio, sr, subtype="PCM_16")
        return
    # Minimal WAV writer so export works without soundfile.
    data = np.asarray(audio, dtype=np.float32)
    if data.ndim == 1:
        data = data[:, None]
    channels = int(data.shape[1])
    pcm = (np.clip(data, -1.0, 1.0) * 32767.0).astype("<i2").tobytes()
    block = channels * 2
    with open(path, "wb") as fh:
        fh.write(b"RIFF")
        fh.write(struct.pack("<I", 36 + len(pcm)))
        fh.write(b"WAVEfmt ")
        fh.write(struct.pack("<IHHIIHH", 16, 1, channels, sr,
                             sr * block, block, 16))
        fh.write(b"data")
        fh.write(struct.pack("<I", len(pcm)))
        fh.write(pcm)


def export_mp3(stereo, path, sr=SAMPLE_RATE, bitrate="192k"):
    if AudioSegment is None:
        raise RuntimeError("pydub is not installed - MP3 export unavailable.")
    import tempfile

    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    try:
        export_wav(stereo, tmp.name, sr)
        AudioSegment.from_wav(tmp.name).export(path, format="mp3", bitrate=bitrate)
    finally:
        try:
            os.unlink(tmp.name)
        except Exception:
            pass


def _vlq(value):
    value = int(max(0, value))
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def export_midi(notes, path, bpm, beats_per_bar=4, program=0, ticks=480):
    """Write a type-0 MIDI file - no external dependency."""
    events = []
    for nt in notes:
        on = int(round(nt.beat * ticks))
        off = int(round((nt.beat + nt.dur) * ticks))
        off = max(off, on + 1)
        vel = int(np.clip(round(nt.vel * 127), 1, 127))
        events.append((on, 1, 0x90, nt.midi, vel))
        events.append((off, 0, 0x80, nt.midi, 0))
    events.sort(key=lambda e: (e[0], e[1]))

    track = bytearray()
    track += _vlq(0) + bytes([0xFF, 0x51, 0x03]) + \
        int(round(60_000_000 / bpm)).to_bytes(3, "big")
    track += _vlq(0) + bytes([0xFF, 0x58, 0x04, beats_per_bar, 2, 24, 8])
    track += _vlq(0) + bytes([0xC0, int(program) & 0x7F])

    last = 0
    for tick, _order, status, pitch, vel in events:
        track += _vlq(tick - last) + bytes([status, int(pitch) & 0x7F, vel])
        last = tick
    track += _vlq(0) + bytes([0xFF, 0x2F, 0x00])

    with open(path, "wb") as fh:
        fh.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, ticks))
        fh.write(b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


# ===========================================================================
#  PLAYBACK
# ===========================================================================

class Player:
    def __init__(self):
        self.data = None
        self.pos = 0
        self.stream = None
        self.playing = False
        self.loop = False
        self.lock = threading.Lock()

    def load(self, data):
        self.stop()
        with self.lock:
            self.data = data
            self.pos = 0

    def _callback(self, outdata, frames, time_info, status):
        with self.lock:
            if self.data is None:
                outdata.fill(0)
                return
            written = 0
            while written < frames:
                remaining = len(self.data) - self.pos
                if remaining <= 0:
                    if self.loop:
                        self.pos = 0
                        continue
                    outdata[written:] = 0
                    self.playing = False
                    raise sd.CallbackStop()
                take = min(frames - written, remaining)
                outdata[written:written + take] = self.data[self.pos:self.pos + take]
                self.pos += take
                written += take

    def play(self):
        if not HAS_AUDIO or self.data is None:
            return
        if self.pos >= len(self.data):
            self.pos = 0
        if self.stream is None or not self.stream.active:
            self.stream = sd.OutputStream(samplerate=SAMPLE_RATE, channels=2,
                                          callback=self._callback,
                                          dtype="float32", blocksize=1024)
            self.stream.start()
        self.playing = True

    def pause(self):
        self.playing = False
        if self.stream is not None:
            try:
                self.stream.stop()
                self.stream.close()
            except Exception:
                pass
            self.stream = None

    def stop(self):
        self.pause()
        with self.lock:
            self.pos = 0

    def seek(self, seconds):
        with self.lock:
            if self.data is None:
                return
            self.pos = int(np.clip(seconds * SAMPLE_RATE, 0, max(0, len(self.data) - 1)))

    def current_time(self):
        return self.pos / SAMPLE_RATE

    def duration(self):
        return len(self.data) / SAMPLE_RATE if self.data is not None else 0.0


# ===========================================================================
#  UI THEME
# ===========================================================================

class Theme:
    bg = "#0f1116"
    panel = "#161922"
    panel2 = "#1d2130"
    panel3 = "#252a3a"
    border = "#2b3142"
    text = "#e8eaf0"
    muted = "#8d94a8"
    dim = "#636a7d"
    accent = "#6c8dff"
    accent_dk = "#4f6fe0"
    accent_text = "#0b0e16"
    good = "#49d49a"
    warn = "#e9b949"
    bad = "#ef6b6b"
    grid = "#222738"
    grid_bar = "#333a50"
    note = "#6c8dff"
    note_hi = "#9db3ff"
    playhead = "#ff8a5b"


def pick_font(root, candidates, size, weight="normal"):
    available = set(tkfont.families(root))
    for name in candidates:
        if name in available:
            return (name, size, weight)
    return ("TkDefaultFont", size, weight)


def apply_theme(root):
    T = Theme
    ui = pick_font(root, ["Segoe UI", "Inter", "SF Pro Text", "Helvetica Neue",
                          "Ubuntu", "DejaVu Sans"], 10)
    ui_sm = (ui[0], 9)
    ui_bold = (ui[0], 10, "bold")
    ui_title = (ui[0], 16, "bold")
    mono = pick_font(root, ["Consolas", "SF Mono", "DejaVu Sans Mono",
                            "Courier New"], 9)

    st = ttk.Style(root)
    try:
        st.theme_use("clam")
    except Exception:
        pass

    root.configure(bg=T.bg)
    st.configure(".", background=T.panel, foreground=T.text, font=ui,
                 borderwidth=0, focuscolor=T.panel)

    st.configure("TFrame", background=T.panel)
    st.configure("App.TFrame", background=T.bg)
    st.configure("Card.TFrame", background=T.panel)
    st.configure("Sub.TFrame", background=T.panel2)
    st.configure("Header.TFrame", background=T.panel2)

    st.configure("TLabel", background=T.panel, foreground=T.text, font=ui)
    st.configure("App.TLabel", background=T.bg, foreground=T.text, font=ui)
    st.configure("Title.TLabel", background=T.panel2, foreground=T.text, font=ui_title)
    st.configure("Tagline.TLabel", background=T.panel2, foreground=T.muted, font=ui_sm)
    st.configure("Section.TLabel", background=T.panel, foreground=T.muted,
                 font=(ui[0], 8, "bold"))
    st.configure("Param.TLabel", background=T.panel, foreground=T.text, font=ui)
    st.configure("Value.TLabel", background=T.panel, foreground=T.accent, font=mono)
    st.configure("Hint.TLabel", background=T.panel, foreground=T.dim, font=ui_sm)
    st.configure("Status.TLabel", background=T.panel2, foreground=T.muted, font=ui_sm)
    st.configure("Clock.TLabel", background=T.panel, foreground=T.muted, font=mono)
    st.configure("Motif.TLabel", background=T.panel, foreground=T.text, font=mono)

    st.configure("TButton", background=T.panel3, foreground=T.text,
                 borderwidth=0, padding=(12, 7), font=ui)
    st.map("TButton",
           background=[("pressed", T.border), ("active", T.border),
                       ("disabled", T.panel2)],
           foreground=[("disabled", T.dim)])

    st.configure("Accent.TButton", background=T.accent, foreground=T.accent_text,
                 borderwidth=0, padding=(14, 8), font=ui_bold)
    st.map("Accent.TButton",
           background=[("pressed", T.accent_dk), ("active", T.note_hi),
                       ("disabled", T.panel2)],
           foreground=[("disabled", T.dim)])

    st.configure("Transport.TButton", background=T.panel3, foreground=T.text,
                 borderwidth=0, padding=(10, 7), font=ui)
    st.map("Transport.TButton",
           background=[("pressed", T.border), ("active", T.border)])

    st.configure("Toggle.TButton", background=T.panel3, foreground=T.muted,
                 borderwidth=0, padding=(10, 7), font=ui)
    st.map("Toggle.TButton", background=[("active", T.border)])
    st.configure("ToggleOn.TButton", background=T.accent_dk, foreground=T.text,
                 borderwidth=0, padding=(10, 7), font=ui)
    st.map("ToggleOn.TButton", background=[("active", T.accent)])

    st.configure("TNotebook", background=T.bg, borderwidth=0, tabmargins=(0, 0, 0, 0))
    st.configure("TNotebook.Tab", background=T.bg, foreground=T.muted,
                 padding=(13, 9), borderwidth=0, font=ui_sm)
    st.map("TNotebook.Tab",
           background=[("selected", T.panel)],
           foreground=[("selected", T.text), ("active", T.text)])

    st.configure("TScale", background=T.panel, troughcolor=T.panel3,
                 bordercolor=T.panel, lightcolor=T.accent, darkcolor=T.accent)
    st.configure("Horizontal.TScale", background=T.accent, troughcolor=T.panel3,
                 bordercolor=T.panel3, lightcolor=T.accent, darkcolor=T.accent)
    st.configure("Seek.Horizontal.TScale", background=T.playhead,
                 troughcolor=T.panel3, bordercolor=T.panel3,
                 lightcolor=T.playhead, darkcolor=T.playhead)

    st.configure("TCombobox", fieldbackground=T.panel2, background=T.panel3,
                 foreground=T.text, arrowcolor=T.muted, bordercolor=T.border,
                 lightcolor=T.border, darkcolor=T.border, padding=(8, 6))
    st.map("TCombobox",
           fieldbackground=[("readonly", T.panel2)],
           foreground=[("readonly", T.text)],
           arrowcolor=[("active", T.accent)])

    st.configure("TCheckbutton", background=T.panel, foreground=T.text,
                 indicatorcolor=T.panel3, font=ui)
    st.map("TCheckbutton",
           background=[("active", T.panel)],
           indicatorcolor=[("selected", T.accent), ("active", T.border)],
           foreground=[("active", T.text)])

    st.configure("TSeparator", background=T.border)
    st.configure("Vertical.TScrollbar", background=T.panel3, troughcolor=T.panel,
                 bordercolor=T.panel, arrowcolor=T.muted, width=11)
    st.map("Vertical.TScrollbar", background=[("active", T.border)])
    st.configure("Horizontal.TScrollbar", background=T.panel3, troughcolor=T.panel,
                 bordercolor=T.panel, arrowcolor=T.muted)
    st.map("Horizontal.TScrollbar", background=[("active", T.border)])

    st.configure("TProgressbar", background=T.accent, troughcolor=T.panel3,
                 bordercolor=T.panel3, lightcolor=T.accent, darkcolor=T.accent)

    root.option_add("*TCombobox*Listbox.background", T.panel2)
    root.option_add("*TCombobox*Listbox.foreground", T.text)
    root.option_add("*TCombobox*Listbox.selectBackground", T.accent)
    root.option_add("*TCombobox*Listbox.selectForeground", T.accent_text)
    root.option_add("*TCombobox*Listbox.font", ui)

    return dict(ui=ui, ui_sm=ui_sm, ui_bold=ui_bold, mono=mono, title=ui_title)


# ===========================================================================
#  WIDGETS
# ===========================================================================

class ScrollArea(ttk.Frame):
    """A vertically scrolling container with a dark canvas backing."""

    def __init__(self, parent, **kw):
        super().__init__(parent, style="Card.TFrame", **kw)
        self.canvas = tk.Canvas(self, bg=Theme.panel, highlightthickness=0,
                                borderwidth=0)
        self.vsb = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=self.vsb.set)
        self.vsb.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.inner = ttk.Frame(self.canvas, style="Card.TFrame")
        self._win = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", self._on_inner)
        self.canvas.bind("<Configure>", self._on_canvas)
        self.canvas.bind("<Enter>", self._bind_wheel)
        self.canvas.bind("<Leave>", self._unbind_wheel)

    def _on_inner(self, _e):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas(self, e):
        self.canvas.itemconfig(self._win, width=e.width)

    def _bind_wheel(self, _e):
        self.canvas.bind_all("<MouseWheel>", self._wheel)
        self.canvas.bind_all("<Button-4>", self._wheel)
        self.canvas.bind_all("<Button-5>", self._wheel)

    def _unbind_wheel(self, _e):
        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.canvas.unbind_all(seq)

    def _wheel(self, e):
        if getattr(e, "num", None) == 4:
            self.canvas.yview_scroll(-1, "units")
        elif getattr(e, "num", None) == 5:
            self.canvas.yview_scroll(1, "units")
        elif e.delta:
            self.canvas.yview_scroll(int(-e.delta / 120), "units")


class PianoRoll(ttk.Frame):
    """Piano-roll display with a key strip, grid, notes and a playhead.

    Notes are coloured by their form label, so the shape of the motif and its
    restatements is visible at a glance.
    """

    KEY_W = 38
    PAD_TOP = 20
    PAD_BOT = 8

    # base / peak colour per section letter
    SECTION_RGB = {
        "A": ((0x3f, 0x5a, 0xc4), (0x9d, 0xb3, 0xff)),
        "B": ((0x1f, 0x7a, 0x62), (0x5f, 0xe0, 0xb4)),
        "C": ((0x8a, 0x5f, 0x1c), (0xf0, 0xc0, 0x60)),
        "D": ((0x6a, 0x3c, 0x8f), (0xc5, 0x9c, 0xf5)),
    }

    def __init__(self, parent, on_seek=None):
        super().__init__(parent, style="Card.TFrame")
        self.on_seek = on_seek
        self.notes = []
        self.sections = []        # true form-unit spans, gap-free
        self.gap_sections = []    # where the notes actually sound
        self.show_gaps = False
        self.total_beats = 16.0
        self.beats_per_bar = 4
        self.zoom = 1.0
        self.lo = 48
        self.hi = 84
        self._ph_time = 0.0
        self._sec_per_beat = 0.5

        self.keys = tk.Canvas(self, width=self.KEY_W, bg=Theme.panel2,
                              highlightthickness=0, borderwidth=0)
        self.keys.pack(side="left", fill="y")

        right = ttk.Frame(self, style="Card.TFrame")
        right.pack(side="left", fill="both", expand=True)

        self.canvas = tk.Canvas(right, bg=Theme.panel, highlightthickness=0,
                                borderwidth=0)
        self.hsb = ttk.Scrollbar(right, orient="horizontal",
                                 command=self.canvas.xview)
        self.canvas.configure(xscrollcommand=self.hsb.set)
        self.hsb.pack(side="bottom", fill="x")
        self.canvas.pack(side="top", fill="both", expand=True)

        self.canvas.bind("<Configure>", lambda e: self.redraw())
        self.keys.bind("<Configure>", lambda e: self.redraw())
        self.canvas.bind("<Button-1>", self._click)
        self.canvas.bind("<B1-Motion>", self._click)

    # -- data ------------------------------------------------------------
    def set_notes(self, notes, total_beats, beats_per_bar, sec_per_beat,
                  sections=None, gap_sections=None):
        self.notes = notes or []
        self.gap_sections = gap_sections or sections or []
        self.sections = sections or self.gap_sections
        self.total_beats = max(1.0, float(total_beats))
        self.beats_per_bar = max(1, int(beats_per_bar))
        self._sec_per_beat = sec_per_beat
        if self.notes:
            lo = min(n.midi for n in self.notes)
            hi = max(n.midi for n in self.notes)
        else:
            lo, hi = 60, 72
        span = hi - lo
        if span < 23:
            pad = (24 - span) // 2
            lo, hi = lo - pad, hi + pad
        self.lo, self.hi = lo - 1, hi + 1
        self.redraw()

    def set_zoom(self, z):
        self.zoom = max(1.0, float(z))
        self.redraw()

    def set_playhead(self, seconds):
        self._ph_time = seconds
        if not self.canvas.winfo_ismapped():
            return
        x = self._x_for_beat(seconds / max(1e-6, self._sec_per_beat))
        h = max(1, self.canvas.winfo_height())
        self.canvas.coords("playhead", x, 0, x, h)
        # Keep the playhead in view while scrolled in.
        try:
            w = self.canvas.winfo_width()
            total = self._content_w()
            if total > w:
                lo, hi = self.canvas.xview()
                frac = x / total
                if frac < lo or frac > hi - 0.02:
                    self.canvas.xview_moveto(max(0.0, frac - 0.3))
        except Exception:
            pass

    # -- geometry ---------------------------------------------------------
    def _content_w(self):
        return max(self.canvas.winfo_width(), 10) * self.zoom

    def _x_for_beat(self, beat):
        return (beat / self.total_beats) * self._content_w()

    def _row_h(self, h):
        rows = max(1, self.hi - self.lo + 1)
        return (h - self.PAD_TOP - self.PAD_BOT) / rows

    def _y_for_midi(self, midi, h):
        rh = self._row_h(h)
        return self.PAD_TOP + (self.hi - midi) * rh, rh

    # -- drawing ----------------------------------------------------------
    def redraw(self):
        c = self.canvas
        c.delete("all")
        self.keys.delete("all")
        h = max(c.winfo_height(), 10)
        cw = self._content_w()
        c.configure(scrollregion=(0, 0, cw, h))

        rh = self._row_h(h)

        # Key strip + horizontal lanes.
        for midi in range(self.lo, self.hi + 1):
            y, _ = self._y_for_midi(midi, h)
            black = (midi % 12) in (1, 3, 6, 8, 10)
            lane = Theme.panel if not black else "#131620"
            c.create_rectangle(0, y, cw, y + rh, fill=lane, outline="")
            self.keys.create_rectangle(
                0, y, self.KEY_W, y + rh,
                fill=("#10131b" if black else "#262b3a"),
                outline=Theme.panel, width=1)
            if midi % 12 == 0 and rh >= 7:
                self.keys.create_text(self.KEY_W - 4, y + rh / 2,
                                      text=midi_to_name(midi), anchor="e",
                                      fill=Theme.dim,
                                      font=("TkDefaultFont", 7))
                c.create_line(0, y, cw, y, fill=Theme.grid_bar, width=1)

        # Beat / bar grid.
        bpb = self.beats_per_bar
        step = 1.0
        if cw / max(1.0, self.total_beats) < 6:
            step = float(bpb)
        b = 0.0
        while b <= self.total_beats + 1e-6:
            x = self._x_for_beat(b)
            is_bar = (abs(b % bpb) < 1e-6)
            c.create_line(x, 0, x, h,
                          fill=(Theme.grid_bar if is_bar else Theme.grid),
                          width=(1 if not is_bar else 1))
            if is_bar and cw / max(1.0, self.total_beats / bpb) > 34:
                c.create_text(x + 4, 7, text=str(int(b // bpb) + 1), anchor="w",
                              fill=Theme.dim, font=("TkDefaultFont", 7))
            b += step

        # Notes.
        for n in self.notes:
            x0 = self._x_for_beat(n.beat)
            x1 = self._x_for_beat(n.beat + n.dur)
            y, _ = self._y_for_midi(n.midi, h)
            if x1 - x0 < 2:
                x1 = x0 + 2
            top = y + max(0.5, rh * 0.14)
            bot = y + rh - max(0.5, rh * 0.14)
            shade = self._note_color(n.vel, n.label)
            c.create_rectangle(x0, top, x1, bot, fill=shade, outline="",
                               tags="note")
            if rh > 6:
                c.create_line(x0, top, x1, top,
                              fill=self._note_color(1.0, n.label), width=1)

        # Form strip: which unit of the form each stretch of bars belongs to.
        # By default the blocks butt together, because the form is continuous
        # even where a unit's last note has already died away.
        for label, a, b in self._strip_spans():
            x0 = self._x_for_beat(a)
            x1 = self._x_for_beat(min(b, self.total_beats))
            col = self._note_color(0.75, label)
            c.create_rectangle(x0, 1, max(x0 + 2, x1), self.PAD_TOP - 7,
                               fill=col, outline="")
            if x1 - x0 > 22:
                c.create_text((x0 + x1) / 2, (self.PAD_TOP - 6) / 2,
                              text=label, fill="#0c0f16",
                              font=("TkDefaultFont", 7, "bold"))

        if not self.notes:
            c.create_text(cw / 2, h / 2,
                          text="No melody yet  -  press Generate",
                          fill=Theme.dim, font=("TkDefaultFont", 10))

        c.create_line(0, 0, 0, h, fill=Theme.playhead, width=2, tags="playhead")
        self.set_playhead(self._ph_time)

    def set_show_gaps(self, flag):
        """Show the strip as the notes actually sound, gaps and all."""
        self.show_gaps = bool(flag)
        self.redraw()

    def _strip_spans(self):
        """Spans for the form strip, contiguous unless gaps are asked for."""
        if self.show_gaps:
            return list(self.gap_sections)
        secs = list(self.sections)
        if not secs:
            return []
        out = []
        for i, (label, a, _b) in enumerate(secs):
            start = 0.0 if i == 0 else out[-1][2]
            end = secs[i + 1][1] if i + 1 < len(secs) else self.total_beats
            out.append((label, start, max(start, end)))
        return out

    @classmethod
    def _note_color(cls, vel, label="A"):
        v = float(np.clip(vel, 0.0, 1.0))
        letter = (label or "A")[0].upper()
        base, top = cls.SECTION_RGB.get(letter, cls.SECTION_RGB["A"])
        rgb = [int(base[i] + (top[i] - base[i]) * v) for i in range(3)]
        return "#%02x%02x%02x" % tuple(rgb)

    def _click(self, e):
        if not self.on_seek:
            return
        x = self.canvas.canvasx(e.x)
        beat = (x / max(1.0, self._content_w())) * self.total_beats
        self.on_seek(max(0.0, beat * self._sec_per_beat))


# ===========================================================================
#  PARAMETER DEFINITIONS
# ===========================================================================
# (key, kind, label, lo, hi, default, decimals, hint)
#   kind: "num" | "int" | "choice" | "bool"

P_MELODY = [
    ("root_note", "choice", "Root note", None, None, "C4", 0, "Tonal centre"),
    ("scale", "choice", "Scale", None, None, "Major (Ionian)", 0, None),
    ("range_lo", "int", "Register low", -24, 12, 0, 0, "Semitones from root"),
    ("range_hi", "int", "Register high", 0, 36, 19, 0, "Semitones from root"),
    ("motif_notes", "int", "Motif length (notes)", 2, 12, 7, 0,
     "The smallest complete idea - short is memorable"),
    ("candidates", "int", "Motif search", 1, 300, 64, 0,
     "Motifs written and scored before the best is kept"),
    ("contour", "choice", "Motif contour", None, None, "Arch", 0,
     "The shape the idea is searched for"),
    ("contour_strength", "num", "Contour weight", 0.0, 1.0, 0.60, 2,
     "How much a distinct shape counts when scoring"),
    ("interval_variety", "num", "Interval variety", 0.0, 1.0, 0.65, 2,
     "Rewards mixed intervals over the same one repeated"),
    ("stepwise", "num", "Step / leap balance", 0.0, 1.0, 0.62, 2,
     "High = mostly steps, low = leapy"),
    ("max_leap", "int", "Max leap (semitones)", 2, 14, 7, 0, None),
    ("singable_span", "int", "Singable span", 5, 24, 12, 0,
     "Semitones the motif may cover - keep it voice-sized"),
    ("leap_recovery", "bool", "Answer leaps with a step", None, None, True, 0,
     None),
    ("note_repeat", "num", "Avoid repeated notes", 0.0, 1.0, 0.50, 2, None),
    ("progression", "choice", "Harmonic guide", None, None, "I - V - vi - IV", 0,
     "Silent - only nudges structural notes"),
    ("chord_bias", "num", "Chord-tone pull", 0.0, 1.0, 0.50, 2, None),
    ("sevenths", "bool", "Use sevenths in guide", None, None, False, 0, None),
    ("resolve", "bool", "Enforce cadences", None, None, True, 0,
     "Antecedents stay open, consequents resolve"),
]

P_RHYTHM = [
    ("bpm", "int", "Tempo (BPM)", 40, 220, 104, 0, None),
    ("bars", "int", "Length (bars)", 1, 64, 16, 0, None),
    ("beats_per_bar", "int", "Beats per bar", 2, 7, 4, 0, None),
    ("subdivision", "int", "Grid (per beat)", 1, 6, 2, 0,
     "2 = eighths, 3 = triplets, 4 = sixteenths"),
    ("form", "choice", "Form", None, None, "Period (a a' a a'')", 0,
     "How the motif is stated, varied and answered"),
    ("unit_bars", "int", "Unit length (bars)", 1, 4, 2, 0,
     "Bars per form unit"),
    ("variation", "num", "Variation strength", 0.0, 1.0, 0.40, 2,
     "How far restatements develop the idea"),
    ("note_length", "num", "Note length", 0.0, 1.0, 0.45, 2, None),
    ("gate", "num", "Gate / legato", 0.2, 1.0, 0.88, 2, None),
    ("rest_density", "num", "Rhythmic space", 0.0, 1.0, 0.22, 2, None),
    ("max_gap", "num", "Longest rest (beats)", 0.5, 8.0, 2.0, 1,
     "Caps the silence between notes - raise it to let phrases breathe"),
    ("syncopation", "num", "Syncopation", 0.0, 1.0, 0.25, 2, None),
    ("swing", "num", "Swing", 0.0, 1.0, 0.0, 2, None),
    ("accent", "num", "Accent strength", 0.0, 1.0, 0.45, 2, None),
    ("dyn_arc", "num", "Dynamic arc", 0.0, 1.0, 0.55, 2,
     "Shapes loud and quiet notes across each phrase"),
    ("humanize_time", "num", "Humanise timing", 0.0, 1.0, 0.18, 2,
     "Shifts notes off the grid"),
    ("humanize_vel", "num", "Humanise velocity", 0.0, 1.0, 0.25, 2, None),
]

P_VOICE = [
    ("wave", "choice", "Waveform", None, None, "Saw", 0, None),
    ("pulse_width", "num", "Pulse width", 0.05, 0.95, 0.5, 2,
     "Pulse waveform only"),
    ("rolloff", "num", "Harmonic rolloff", 0.0, 2.5, 1.35, 2,
     "Higher = darker, fewer upper partials"),
    ("attack", "num", "Attack (s)", 0.001, 0.6, 0.004, 3, None),
    ("decay", "num", "Decay (s)", 0.01, 2.0, 0.28, 3, None),
    ("sustain", "num", "Sustain level", 0.0, 1.0, 0.10, 2, None),
    ("release", "num", "Release (s)", 0.01, 2.5, 0.30, 3, None),
    ("vib_rate", "num", "Vibrato rate (Hz)", 0.5, 10.0, 5.0, 2, None),
    ("vib_depth", "num", "Vibrato depth", 0.0, 1.0, 0.0, 2, "In semitones"),
    ("unison", "int", "Unison voices", 1, 4, 2, 0, None),
    ("detune", "num", "Detune (cents)", 0.0, 30.0, 6.0, 1, None),
    ("glide", "num", "Portamento (s)", 0.0, 0.3, 0.0, 3, None),
]

P_FX = [
    ("tone", "num", "Tone", 0.0, 1.0, 0.72, 2, "Low-pass brightness"),
    ("drive", "num", "Drive", 0.0, 1.0, 0.08, 2, None),
    ("delay_mix", "num", "Delay mix", 0.0, 1.0, 0.22, 2, None),
    ("delay_time", "num", "Delay time", 0.05, 2.0, 0.75, 2,
     "In beats when synced, else seconds"),
    ("delay_sync", "bool", "Sync delay to tempo", None, None, True, 0, None),
    ("delay_feedback", "num", "Delay feedback", 0.0, 0.85, 0.35, 2, None),
    ("reverb_mix", "num", "Reverb mix", 0.0, 1.0, 0.25, 2, None),
    ("reverb_size", "num", "Reverb size (s)", 0.2, 4.0, 1.6, 2, None),
    ("width", "num", "Stereo width", 0.0, 1.0, 0.45, 2, None),
    ("output_gain", "num", "Output level", 0.1, 1.5, 1.0, 2,
     "Trim around the standard loudness - 1.0 leaves headroom"),
]

ALL_PARAMS = P_MELODY + P_RHYTHM + P_VOICE + P_FX

CHOICE_VALUES = {
    "root_note": [f"{n}{o}" for o in (2, 3, 4, 5) for n in NOTE_NAMES],
    "scale": list(SCALES.keys()),
    "contour": list(CONTOURS.keys()),
    "progression": list(PROGRESSIONS.keys()),
    "form": list(FORMS.keys()),
    "wave": WAVEFORMS,
}

# Parameters excluded from Randomise (structure and search budget, not
# musical character).
NO_RANDOM = {"bars", "beats_per_bar", "output_gain", "delay_sync",
             "candidates", "singable_span", "leap_recovery", "resolve",
             "unit_bars"}

# Randomise draws from these ranges rather than the full slider travel.  The
# extremes stay reachable by hand, but a dice roll should land on something
# playable - most of the slider's range exists for deliberate use, not for
# chance to wander into.
RANDOM_RANGE = {
    # -- melody ---------------------------------------------------------
    "root_note": [f"{n}{o}" for o in (3, 4) for n in NOTE_NAMES],
    "scale": [s for s in SCALES
              if s not in ("Chromatic", "Whole Tone", "Locrian")],
    "range_lo": (-5, 2),
    "range_hi": (14, 21),
    "motif_notes": (4, 8),
    "contour": [c for c in CONTOURS if c != "Level"],
    "contour_strength": (0.40, 0.90),
    "interval_variety": (0.40, 0.90),
    "stepwise": (0.45, 0.85),
    "max_leap": (4, 9),
    "note_repeat": (0.30, 0.70),
    "progression": [k for k in PROGRESSIONS if k != "Free (no guide)"],
    "chord_bias": (0.30, 0.80),
    "sevenths": [False, False, True],
    # -- rhythm ---------------------------------------------------------
    "bpm": (72, 148),
    "subdivision": (2, 4),
    "variation": (0.20, 0.60),
    "note_length": (0.25, 0.70),
    "gate": (0.70, 1.00),
    "rest_density": (0.05, 0.40),
    "max_gap": (1.0, 2.5),
    "syncopation": (0.00, 0.50),
    "swing": (0.00, 0.60),
    "accent": (0.30, 0.70),
    "dyn_arc": (0.35, 0.80),
    "humanize_time": (0.05, 0.40),
    "humanize_vel": (0.10, 0.45),
    # -- voice ----------------------------------------------------------
    "pulse_width": (0.20, 0.60),
    "rolloff": (0.70, 1.90),
    "attack": (0.002, 0.120),
    "decay": (0.10, 0.80),
    "sustain": (0.05, 0.70),
    "release": (0.10, 0.80),
    "vib_rate": (4.0, 6.5),
    "vib_depth": (0.00, 0.12),
    "unison": (1, 3),
    "detune": (0.0, 12.0),
    "glide": (0.00, 0.06),
    # -- space ----------------------------------------------------------
    "tone": (0.50, 0.90),
    "drive": (0.00, 0.30),
    "delay_mix": (0.00, 0.40),
    "delay_time": (0.25, 1.00),
    "delay_feedback": (0.10, 0.50),
    "reverb_mix": (0.08, 0.45),
    "reverb_size": (0.80, 2.60),
    "width": (0.20, 0.80),
}

STYLE_PRESETS = {
    "Default": {},
    "Folk Song": dict(scale="Major (Ionian)", progression="I - V - vi - IV",
                      form="Period (a a\' a a\'\')", unit_bars=2,
                      motif_notes=7, contour="Arch", contour_strength=0.70,
                      interval_variety=0.55, stepwise=0.80, max_leap=5,
                      singable_span=12, subdivision=2, bpm=96,
                      note_length=0.50, rest_density=0.20, syncopation=0.10,
                      variation=0.30, dyn_arc=0.60, wave="Triangle",
                      rolloff=1.1, attack=0.006, decay=0.4, sustain=0.2,
                      release=0.4, tone=0.70, reverb_mix=0.22, delay_mix=0.12),
    "Chiptune Lead": dict(scale="Major Pentatonic",
                          progression="I - V - vi - IV", form="AABA",
                          unit_bars=2, motif_notes=7, contour="Wave",
                          contour_strength=0.55, interval_variety=0.80,
                          stepwise=0.50, max_leap=9, singable_span=16,
                          subdivision=4, bpm=150, note_length=0.20, gate=0.75,
                          rest_density=0.15, syncopation=0.45, variation=0.45,
                          dyn_arc=0.45, wave="Pulse", pulse_width=0.25,
                          rolloff=1.0, attack=0.001, decay=0.1, sustain=0.5,
                          release=0.06, unison=1, detune=0.0, tone=0.85,
                          drive=0.15, delay_mix=0.3, delay_feedback=0.3,
                          reverb_mix=0.12, width=0.3),
    "Music Box": dict(scale="Major (Ionian)", progression="I - IV - V - I",
                      form="Period (a a\' a a\'\')", unit_bars=2,
                      motif_notes=5, contour="Arch", contour_strength=0.75,
                      interval_variety=0.50, stepwise=0.72, max_leap=7,
                      singable_span=12, subdivision=2, bpm=84,
                      note_length=0.35, rest_density=0.25, variation=0.25,
                      dyn_arc=0.65, wave="Sine", rolloff=0.7, attack=0.002,
                      decay=0.55, sustain=0.05, release=0.7, unison=1,
                      detune=0.0, tone=0.62, reverb_mix=0.42, reverb_size=2.4,
                      delay_mix=0.15, width=0.55),
    "Night Drive": dict(scale="Natural Minor",
                        progression="i - VI - III - VII",
                        form="Call & response (a b a b\')", unit_bars=2,
                        motif_notes=6, contour="Falling",
                        contour_strength=0.55, interval_variety=0.60,
                        stepwise=0.58, max_leap=8, singable_span=14,
                        subdivision=2, bpm=112, note_length=0.55, gate=0.95,
                        rest_density=0.18, syncopation=0.30, variation=0.40,
                        dyn_arc=0.60, wave="Saw", rolloff=1.2, attack=0.02,
                        decay=0.3, sustain=0.6, release=0.35, unison=3,
                        detune=14.0, glide=0.05, vib_depth=0.05, tone=0.6,
                        drive=0.2, delay_mix=0.35, delay_feedback=0.45,
                        reverb_mix=0.3, width=0.75),
    "Ambient Flute": dict(scale="Kumoi", progression="Pedal (tonic only)",
                          form="Chain (a a\' a\'\' a\'\'\')", unit_bars=2,
                          motif_notes=4, contour="Wave",
                          contour_strength=0.50, interval_variety=0.55,
                          stepwise=0.82, max_leap=7, singable_span=12,
                          subdivision=2, bpm=68, note_length=0.80, gate=1.0,
                          rest_density=0.45, max_gap=4.0, syncopation=0.10,
                          variation=0.50,
                          dyn_arc=0.70, wave="Sine", rolloff=1.8, attack=0.09,
                          decay=0.25, sustain=0.78, release=0.8, vib_rate=5.2,
                          vib_depth=0.12, glide=0.03, tone=0.55,
                          reverb_mix=0.55, reverb_size=3.2, delay_mix=0.3,
                          delay_feedback=0.45, width=0.85),
    "Bebop Run": dict(scale="Mixolydian", progression="ii - V - I",
                      form="Sentence (a a\' dev cad)", unit_bars=2,
                      motif_notes=8, contour="Double Arch",
                      contour_strength=0.45, interval_variety=0.85,
                      stepwise=0.80, max_leap=10, singable_span=18,
                      subdivision=4, bpm=168, note_length=0.15, gate=0.8,
                      rest_density=0.18, syncopation=0.50, swing=0.62,
                      variation=0.65, dyn_arc=0.50, humanize_time=0.35,
                      humanize_vel=0.40, wave="Triangle", rolloff=1.0,
                      attack=0.005, decay=0.2, sustain=0.45, release=0.2,
                      tone=0.72, reverb_mix=0.18, delay_mix=0.08, width=0.35),
    "Bass Line": dict(scale="Minor Pentatonic", progression="i - iv - VI - V",
                      range_lo=-24, range_hi=-3, form="AABA", unit_bars=1,
                      motif_notes=5, contour="Level", contour_strength=0.30,
                      interval_variety=0.70, stepwise=0.55, max_leap=12,
                      singable_span=14, subdivision=4, bpm=118,
                      note_length=0.25, gate=0.70, rest_density=0.25,
                      syncopation=0.55, variation=0.25, dyn_arc=0.35,
                      wave="Square", rolloff=1.6, attack=0.006, decay=0.3,
                      sustain=0.45, release=0.18, unison=2, detune=3.0,
                      glide=0.05, tone=0.42, drive=0.25, delay_mix=0.0,
                      reverb_mix=0.08, width=0.15),
    "Glass Arpeggio": dict(scale="Lydian", progression="I - iii - IV - iv",
                           form="ABAC", unit_bars=2, motif_notes=6,
                           contour="Rising", contour_strength=0.65,
                           interval_variety=0.75, stepwise=0.30, max_leap=12,
                           singable_span=19, subdivision=4, bpm=128,
                           note_length=0.20, gate=0.60, rest_density=0.08,
                           syncopation=0.15, variation=0.35, dyn_arc=0.55,
                           wave="Triangle", rolloff=1.3, attack=0.004,
                           decay=0.35, sustain=0.12, release=0.5, unison=2,
                           detune=8.0, tone=0.8, delay_mix=0.4,
                           delay_feedback=0.5, reverb_mix=0.4,
                           reverb_size=2.6, width=0.8),
    "Hook (short & sticky)": dict(scale="Dorian",
                                  progression="i - iv - VI - V",
                                  form="AAB (bar form)", unit_bars=2,
                                  motif_notes=5, candidates=120,
                                  contour="Arch", contour_strength=0.85,
                                  interval_variety=0.75, stepwise=0.65,
                                  max_leap=7, singable_span=10,
                                  subdivision=2, bpm=100, note_length=0.45,
                                  rest_density=0.30, syncopation=0.35,
                                  variation=0.20, dyn_arc=0.70,
                                  humanize_time=0.22, wave="Triangle",
                                  rolloff=1.1, attack=0.005, decay=0.35,
                                  sustain=0.25, release=0.4, tone=0.72,
                                  reverb_mix=0.28, delay_mix=0.18, width=0.5),
}


# ===========================================================================
#  APPLICATION
# ===========================================================================

class MelodyMakerApp:
    def __init__(self, root):
        self.root = root
        self.fonts = apply_theme(root)
        root.title(f"{APP_NAME} {APP_VERSION}")
        root.geometry("1240x790")
        root.minsize(1040, 660)

        self.player = Player()
        self.audio = None
        self.notes = []
        self.info = {}
        self.busy = False
        self._seeking = False
        self._dirty = True

        self.params = {}       # key -> tk variable
        self.meta = {}         # key -> spec dict
        self.seek_var = tk.DoubleVar(value=0.0)
        self.zoom_var = tk.DoubleVar(value=1.0)
        # Off by default, and it stays off across generations: the form strip
        # reads as one continuous bar unless the gaps are asked for.
        self.show_gaps_var = tk.BooleanVar(value=False)
        self.style_var = tk.StringVar(value="Default")
        self.voice_var = tk.StringVar(value="Pluck")
        self.midi_prog_var = tk.StringVar(value="Flute")
        self.seed_var = tk.IntVar(value=1)

        self._build_header()
        self._build_body()
        self._build_statusbar()

        self._refresh_env_status()
        self.root.after(80, self._tick)
        self.root.bind("<space>", lambda e: self.toggle_play())
        self.root.bind("<Control-g>", lambda e: self.generate())
        self.root.bind("<Control-r>", lambda e: self.randomize())
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.generate()

    # -- layout ----------------------------------------------------------
    def _build_header(self):
        bar = ttk.Frame(self.root, style="Header.TFrame", padding=(18, 12))
        bar.pack(side="top", fill="x")

        left = ttk.Frame(bar, style="Header.TFrame")
        left.pack(side="left")
        ttk.Label(left, text=APP_NAME, style="Title.TLabel").pack(anchor="w")
        ttk.Label(left, text="Motif-driven single-voice melodies",
                  style="Tagline.TLabel").pack(anchor="w")

        right = ttk.Frame(bar, style="Header.TFrame")
        right.pack(side="right")
        self.chip_row = ttk.Frame(right, style="Header.TFrame")
        self.chip_row.pack(side="top", anchor="e")

        ctl = ttk.Frame(right, style="Header.TFrame")
        ctl.pack(side="top", anchor="e", pady=(8, 0))

        ttk.Label(ctl, text="Style", style="Status.TLabel").pack(side="left",
                                                                 padx=(0, 6))
        cb = ttk.Combobox(ctl, textvariable=self.style_var, state="readonly",
                          values=list(STYLE_PRESETS.keys()), width=16)
        cb.pack(side="left", padx=(0, 10))
        cb.bind("<<ComboboxSelected>>", lambda e: self.apply_style_preset())

        ttk.Button(ctl, text="Randomise", command=self.randomize).pack(
            side="left", padx=(0, 6))
        ttk.Button(ctl, text="Reset", command=self.reset_defaults).pack(side="left")

        ttk.Separator(self.root, orient="horizontal").pack(side="top", fill="x")

    def _build_body(self):
        body = ttk.Frame(self.root, style="App.TFrame", padding=(14, 12))
        body.pack(side="top", fill="both", expand=True)

        # ---- left: roll + transport -------------------------------------
        left = ttk.Frame(body, style="App.TFrame")
        left.pack(side="left", fill="both", expand=True, padx=(0, 12))

        roll_card = ttk.Frame(left, style="Card.TFrame", padding=10)
        roll_card.pack(side="top", fill="both", expand=True)

        head = ttk.Frame(roll_card, style="Card.TFrame")
        head.pack(side="top", fill="x", pady=(0, 8))
        ttk.Label(head, text="PIANO ROLL", style="Section.TLabel").pack(side="left")
        self.roll_info = ttk.Label(head, text="", style="Hint.TLabel")
        self.roll_info.pack(side="left", padx=(12, 0))

        zoom_box = ttk.Frame(head, style="Card.TFrame")
        zoom_box.pack(side="right")
        ttk.Checkbutton(zoom_box, text="Section gaps",
                        variable=self.show_gaps_var,
                        command=self._toggle_gaps).pack(side="left",
                                                        padx=(0, 14))
        ttk.Label(zoom_box, text="Zoom", style="Hint.TLabel").pack(side="left",
                                                                   padx=(0, 6))
        zs = ttk.Scale(zoom_box, from_=1.0, to=10.0, orient="horizontal",
                       variable=self.zoom_var, length=120,
                       command=lambda v: self.roll.set_zoom(float(v)))
        zs.pack(side="left")

        self.roll = PianoRoll(roll_card, on_seek=self._seek_to)
        self.roll.pack(side="top", fill="both", expand=True)

        # ---- transport ---------------------------------------------------
        tr = ttk.Frame(left, style="Card.TFrame", padding=12)
        tr.pack(side="top", fill="x", pady=(12, 0))

        row1 = ttk.Frame(tr, style="Card.TFrame")
        row1.pack(fill="x")

        self.gen_btn = ttk.Button(row1, text="Generate", style="Accent.TButton",
                                  command=self.generate, width=13)
        self.gen_btn.pack(side="left", padx=(0, 10))

        self.play_btn = ttk.Button(row1, text="Play", style="Transport.TButton",
                                   command=self.toggle_play, width=9)
        self.play_btn.pack(side="left", padx=(0, 4))
        ttk.Button(row1, text="Stop", style="Transport.TButton", width=7,
                   command=self.stop_playback).pack(side="left", padx=(0, 4))
        ttk.Button(row1, text="-5s", style="Transport.TButton", width=5,
                   command=lambda: self.nudge(-5)).pack(side="left", padx=(0, 4))
        ttk.Button(row1, text="+5s", style="Transport.TButton", width=5,
                   command=lambda: self.nudge(5)).pack(side="left", padx=(0, 8))

        self.loop_btn = ttk.Button(row1, text="Loop", style="Toggle.TButton",
                                   width=7, command=self.toggle_loop)
        self.loop_btn.pack(side="left", padx=(0, 8))

        self.clock = ttk.Label(row1, text="0:00.0 / 0:00.0", style="Clock.TLabel")
        self.clock.pack(side="right")

        row2 = ttk.Frame(tr, style="Card.TFrame")
        row2.pack(fill="x", pady=(10, 0))
        self.seek = ttk.Scale(row2, from_=0, to=100, orient="horizontal",
                              style="Seek.Horizontal.TScale",
                              variable=self.seek_var, command=self._on_seek_drag)
        self.seek.pack(fill="x")
        self.seek.bind("<ButtonPress-1>", lambda e: setattr(self, "_seeking", True))
        self.seek.bind("<ButtonRelease-1>", self._on_seek_release)

        row3 = ttk.Frame(tr, style="Card.TFrame")
        row3.pack(fill="x", pady=(10, 0))
        ttk.Label(row3, text="Seed", style="Param.TLabel").pack(side="left")
        self.seed_entry = ttk.Entry(row3, width=8)
        self.seed_entry.insert(0, "1")
        self.seed_entry.pack(side="left", padx=(8, 6))
        ttk.Button(row3, text="New seed", width=10,
                   command=self.new_seed).pack(side="left", padx=(0, 4))
        ttk.Button(row3, text="-", width=3,
                   command=lambda: self.bump_seed(-1)).pack(side="left", padx=(0, 2))
        ttk.Button(row3, text="+", width=3,
                   command=lambda: self.bump_seed(1)).pack(side="left")

        self.export_wav_btn = ttk.Button(row3, text="Export WAV",
                                         command=self.do_export_wav)
        self.export_wav_btn.pack(side="right", padx=(6, 0))
        self.export_mp3_btn = ttk.Button(row3, text="Export MP3",
                                         command=self.do_export_mp3)
        self.export_mp3_btn.pack(side="right", padx=(6, 0))
        self.export_midi_btn = ttk.Button(row3, text="Export MIDI",
                                          command=self.do_export_midi)
        self.export_midi_btn.pack(side="right")

        # ---- right: parameter tabs ---------------------------------------
        right = ttk.Frame(body, style="App.TFrame", width=400)
        right.pack(side="right", fill="y")
        right.pack_propagate(False)

        nb = ttk.Notebook(right)
        nb.pack(fill="both", expand=True)

        self._make_tab(nb, "Melody", P_MELODY)
        self._make_tab(nb, "Rhythm", P_RHYTHM)
        self._make_tab(nb, "Voice", P_VOICE, voice_presets=True)
        self._make_tab(nb, "Space", P_FX)
        self._make_output_tab(nb)

    def _make_tab(self, nb, title, specs, voice_presets=False):
        page = ttk.Frame(nb, style="Card.TFrame")
        nb.add(page, text=title)
        area = ScrollArea(page)
        area.pack(fill="both", expand=True)
        body = ttk.Frame(area.inner, style="Card.TFrame", padding=(16, 14))
        body.pack(fill="both", expand=True)

        if voice_presets:
            ttk.Label(body, text="INSTRUMENT PRESET",
                      style="Section.TLabel").pack(anchor="w")
            cb = ttk.Combobox(body, textvariable=self.voice_var, state="readonly",
                              values=list(VOICE_PRESETS.keys()))
            cb.pack(fill="x", pady=(4, 14))
            cb.bind("<<ComboboxSelected>>", lambda e: self.apply_voice_preset())

        for spec in specs:
            self._add_param(body, spec)

    def _make_output_tab(self, nb):
        page = ttk.Frame(nb, style="Card.TFrame")
        nb.add(page, text="Motif")
        area = ScrollArea(page)
        area.pack(fill="both", expand=True)
        body = ttk.Frame(area.inner, style="Card.TFrame", padding=(16, 14))
        body.pack(fill="both", expand=True)

        ttk.Label(body, text="THE MOTIF", style="Section.TLabel").pack(anchor="w")
        ttk.Label(body, text="The idea everything else is built from.",
                  style="Hint.TLabel").pack(anchor="w", pady=(2, 6))
        self.motif_lbl = ttk.Label(body, text="-", style="Motif.TLabel",
                                   justify="left")
        self.motif_lbl.pack(anchor="w")

        ttk.Separator(body, orient="horizontal").pack(fill="x", pady=(16, 14))

        ttk.Label(body, text="MIDI EXPORT", style="Section.TLabel").pack(anchor="w")
        ttk.Label(body, text="Program used when writing a .mid file.",
                  style="Hint.TLabel").pack(anchor="w", pady=(2, 4))
        ttk.Combobox(body, textvariable=self.midi_prog_var, state="readonly",
                     values=list(GM_PROGRAMS.keys())).pack(fill="x", pady=(0, 16))

        ttk.Separator(body, orient="horizontal").pack(fill="x", pady=(0, 14))

        ttk.Label(body, text="PRESETS", style="Section.TLabel").pack(anchor="w")
        ttk.Label(body, text="Save or load every parameter as JSON.",
                  style="Hint.TLabel").pack(anchor="w", pady=(2, 8))
        btns = ttk.Frame(body, style="Card.TFrame")
        btns.pack(fill="x")
        ttk.Button(btns, text="Save preset…", command=self.save_preset).pack(
            side="left", fill="x", expand=True, padx=(0, 5))
        ttk.Button(btns, text="Load preset…", command=self.load_preset).pack(
            side="left", fill="x", expand=True)

        ttk.Separator(body, orient="horizontal").pack(fill="x", pady=(18, 14))

        ttk.Label(body, text="SHORTCUTS", style="Section.TLabel").pack(anchor="w")
        for text in ("Space - play / pause",
                     "Ctrl+G - generate",
                     "Ctrl+R - randomise",
                     "Click the roll to scrub"):
            ttk.Label(body, text=text, style="Hint.TLabel").pack(anchor="w", pady=1)

        ttk.Separator(body, orient="horizontal").pack(fill="x", pady=(18, 14))
        ttk.Label(body, text="ANALYSIS", style="Section.TLabel").pack(anchor="w")
        self.analysis = ttk.Label(body, text="-", style="Hint.TLabel",
                                  justify="left")
        self.analysis.pack(anchor="w", pady=(4, 0))

    def _add_param(self, parent, spec):
        key, kind, label, lo, hi, default, dec, hint = spec
        self.meta[key] = dict(kind=kind, lo=lo, hi=hi, default=default,
                              decimals=dec)

        if kind == "bool":
            var = tk.BooleanVar(value=bool(default))
            self.params[key] = var
            cb = ttk.Checkbutton(parent, text=label, variable=var,
                                 command=self._mark_dirty)
            cb.pack(anchor="w", pady=(12, 0))
            if hint:
                ttk.Label(parent, text=hint, style="Hint.TLabel").pack(
                    anchor="w", pady=(1, 0))
            return

        if kind == "choice":
            var = tk.StringVar(value=default)
            self.params[key] = var
            ttk.Label(parent, text=label, style="Param.TLabel").pack(
                anchor="w", pady=(14, 3))
            cb = ttk.Combobox(parent, textvariable=var, state="readonly",
                              values=CHOICE_VALUES[key])
            cb.pack(fill="x")
            cb.bind("<<ComboboxSelected>>", lambda e: self._mark_dirty())
            if hint:
                ttk.Label(parent, text=hint, style="Hint.TLabel").pack(
                    anchor="w", pady=(2, 0))
            return

        var = tk.DoubleVar(value=float(default))
        self.params[key] = var
        head = ttk.Frame(parent, style="Card.TFrame")
        head.pack(fill="x", pady=(14, 2))
        ttk.Label(head, text=label, style="Param.TLabel").pack(side="left")
        val_lbl = ttk.Label(head, text="", style="Value.TLabel")
        val_lbl.pack(side="right")
        self.meta[key]["label_widget"] = val_lbl

        sc = ttk.Scale(parent, from_=lo, to=hi, variable=var,
                       orient="horizontal", style="Horizontal.TScale")
        sc.pack(fill="x")
        if hint:
            ttk.Label(parent, text=hint, style="Hint.TLabel").pack(
                anchor="w", pady=(2, 0))

        var.trace_add("write", lambda *_a, k=key: self._on_num_change(k))
        self._update_value_label(key)

    def _build_statusbar(self):
        bar = ttk.Frame(self.root, style="Header.TFrame", padding=(16, 7))
        bar.pack(side="bottom", fill="x")
        self.status = ttk.Label(bar, text="Ready", style="Status.TLabel")
        self.status.pack(side="left")
        self.prog = ttk.Progressbar(bar, mode="determinate", length=160,
                                    maximum=100)
        self.prog.pack(side="right")

    # -- environment chips ------------------------------------------------
    def _chip(self, text, color):
        lbl = tk.Label(self.chip_row, text=text, bg=Theme.panel3, fg=color,
                       font=self.fonts["ui_sm"], padx=9, pady=3)
        lbl.pack(side="left", padx=(6, 0))

    def _refresh_env_status(self):
        for w in self.chip_row.winfo_children():
            w.destroy()
        self._chip("Playback " + ("ready" if HAS_AUDIO else "off"),
                   Theme.good if HAS_AUDIO else Theme.bad)
        mp3_ok = AudioSegment is not None and FFMPEG_PATH
        self._chip("MP3 " + ("ready" if mp3_ok else "off"),
                   Theme.good if mp3_ok else Theme.warn)
        self._chip("WAV ready", Theme.good)
        self._chip("MIDI ready", Theme.good)
        if not HAS_AUDIO:
            self.set_status("sounddevice not installed - preview disabled, "
                            "export still works.")

    # -- parameter plumbing ------------------------------------------------
    def _on_num_change(self, key):
        self._update_value_label(key)
        self._mark_dirty()

    def _update_value_label(self, key):
        m = self.meta.get(key)
        if not m or "label_widget" not in m:
            return
        val = self.params[key].get()
        if m["kind"] == "int":
            txt = str(int(round(val)))
        else:
            txt = f"{val:.{m['decimals']}f}"
        m["label_widget"].config(text=txt)

    def _mark_dirty(self, *_a):
        self._dirty = True
        if hasattr(self, "gen_btn"):
            self.gen_btn.config(text="Generate *")

    def values(self):
        out = {}
        for key, var in self.params.items():
            m = self.meta[key]
            v = var.get()
            if m["kind"] == "int":
                v = int(round(float(v)))
            elif m["kind"] == "num":
                v = float(v)
            out[key] = v
        try:
            out["seed"] = int(self.seed_entry.get() or 0)
        except Exception:
            out["seed"] = 1
        if out["range_hi"] <= out["range_lo"] + 6:
            out["range_hi"] = out["range_lo"] + 12
        return out

    def set_values(self, data, mark=True):
        for key, val in data.items():
            if key not in self.params:
                continue
            m = self.meta[key]
            try:
                if m["kind"] == "bool":
                    self.params[key].set(bool(val))
                elif m["kind"] == "choice":
                    if val in CHOICE_VALUES[key]:
                        self.params[key].set(val)
                else:
                    self.params[key].set(float(val))
            except Exception:
                pass
        for key in self.params:
            self._update_value_label(key)
        if mark:
            self._mark_dirty()

    # -- presets -----------------------------------------------------------
    def apply_style_preset(self):
        name = self.style_var.get()
        self.reset_defaults(keep_style=True)
        data = STYLE_PRESETS.get(name) or {}
        self.set_values(data)
        self.set_status(f"Style preset: {name}")
        self.generate()

    def apply_voice_preset(self):
        name = self.voice_var.get()
        data = VOICE_PRESETS.get(name) or {}
        self.set_values(data)
        self.set_status(f"Voice preset: {name}")
        self.generate()

    def reset_defaults(self, keep_style=False):
        for key, m in self.meta.items():
            if m["kind"] == "bool":
                self.params[key].set(bool(m["default"]))
            elif m["kind"] == "choice":
                self.params[key].set(m["default"])
            else:
                self.params[key].set(float(m["default"]))
            self._update_value_label(key)
        if not keep_style:
            self.style_var.set("Default")
            self.set_status("Reset to defaults")
            self._mark_dirty()

    def randomize(self):
        """Roll new settings, drawing from playable ranges rather than the
        full travel of every slider."""
        rng = random.Random()
        for key, m in self.meta.items():
            if key in NO_RANDOM:
                continue
            spec = RANDOM_RANGE.get(key)
            if isinstance(spec, list):
                self.params[key].set(rng.choice(spec))
            elif isinstance(spec, tuple):
                lo, hi = spec
                if m["kind"] == "int":
                    self.params[key].set(rng.randint(int(lo), int(hi)))
                else:
                    self.params[key].set(round(rng.uniform(lo, hi),
                                               m["decimals"]))
            elif m["kind"] == "bool":
                self.params[key].set(rng.random() < 0.5)
            elif m["kind"] == "choice":
                self.params[key].set(rng.choice(CHOICE_VALUES[key]))
            elif m["kind"] == "int":
                self.params[key].set(rng.randint(int(m["lo"]), int(m["hi"])))
            else:
                self.params[key].set(round(rng.uniform(m["lo"], m["hi"]),
                                           m["decimals"]))
            self._update_value_label(key)

        # Keep the register sane whatever the dice said.
        lo = self.params["range_lo"].get()
        hi = self.params["range_hi"].get()
        if hi - lo < 12:
            self.params["range_hi"].set(lo + 12)
        elif hi - lo > 26:
            self.params["range_hi"].set(lo + 26)
        self._update_value_label("range_hi")

        self.new_seed(regen=False)
        self.set_status("Randomised")
        self.generate()

    def save_preset(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json", initialfile="melody_preset.json",
            filetypes=[("JSON preset", "*.json")])
        if not path:
            return
        data = self.values()
        data["_app"] = APP_NAME
        data["_version"] = APP_VERSION
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=2)
            self.set_status(f"Saved preset: {os.path.basename(path)}")
        except Exception as exc:
            messagebox.showerror("Save failed", str(exc))

    def load_preset(self):
        path = filedialog.askopenfilename(filetypes=[("JSON preset", "*.json")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))
            return
        self.set_values(data)
        if "seed" in data:
            self.seed_entry.delete(0, "end")
            self.seed_entry.insert(0, str(int(data["seed"])))
        self.set_status(f"Loaded preset: {os.path.basename(path)}")
        self.generate()

    # -- seed --------------------------------------------------------------
    def new_seed(self, regen=True):
        self.seed_entry.delete(0, "end")
        self.seed_entry.insert(0, str(random.randint(1, 999999)))
        self._mark_dirty()
        if regen:
            self.generate()

    def bump_seed(self, delta):
        try:
            cur = int(self.seed_entry.get() or 0)
        except Exception:
            cur = 0
        self.seed_entry.delete(0, "end")
        self.seed_entry.insert(0, str(max(0, cur + delta)))
        self.generate()

    # -- generation --------------------------------------------------------
    def generate(self, *_a):
        if self.busy:
            return
        self.busy = True
        self.gen_btn.config(state="disabled", text="Working…")
        self.set_status("Generating melody…")
        self.prog.config(value=4)
        self.player.stop()
        self._set_play_state(False)

        p = self.values()

        def worker():
            try:
                rng = np.random.default_rng(int(p["seed"]))
                info = {}
                notes = generate_melody(p, rng, info)

                def tick(frac):
                    self.root.after(0, lambda f=frac: self.prog.config(
                        value=10 + f * 85))

                audio = render_melody(notes, p, SAMPLE_RATE, progress=tick)
                self.root.after(0, lambda: self._on_done(notes, audio, p, info))
            except Exception as exc:
                traceback.print_exc()
                msg = str(exc)
                self.root.after(0, lambda: self._on_fail(msg))

        threading.Thread(target=worker, daemon=True).start()

    def _on_done(self, notes, audio, p, info=None):
        self.notes = notes
        self.audio = audio
        self.info = info or {}
        self.player.load(audio)

        spb = 60.0 / float(p["bpm"])
        total_beats = p["bars"] * p["beats_per_bar"]
        self.roll.show_gaps = bool(self.show_gaps_var.get())
        self.roll.set_notes(notes, total_beats, p["beats_per_bar"], spb,
                            sections=self.info.get("sections"),
                            gap_sections=sections_from_notes(notes))

        dur = self.player.duration()
        self.seek.config(to=max(0.1, dur))
        self.seek_var.set(0.0)
        self.roll_info.config(
            text=f"{len(notes)} notes  ·  {p['bars']} bars  ·  "
                 f"{p['bpm']} BPM  ·  {p['beats_per_bar']}/4  ·  {p['form']}")
        self._update_analysis(notes, p)

        self.prog.config(value=0)
        self._dirty = False
        self.gen_btn.config(state="normal", text="Generate")
        self.busy = False
        self.set_status(f"Ready — {dur:.1f}s, {len(notes)} notes. "
                        "Press Space to play.")

    def _on_fail(self, msg):
        self.prog.config(value=0)
        self.gen_btn.config(state="normal", text="Generate")
        self.busy = False
        self.set_status("Generation failed")
        messagebox.showerror("Generation failed", msg)

    def _update_analysis(self, notes, p):
        if not notes:
            self.motif_lbl.config(text="-")
            self.analysis.config(text="-")
            return

        info = getattr(self, "info", {}) or {}
        motif = info.get("motif")
        if motif is not None:
            br = info.get("breakdown", {})
            plan = " → ".join(lbl for lbl, _cad, _used in info.get("plan", []))
            moves = []
            for lbl, _cad, used in info.get("plan", [])[:len(FORMS) + 3]:
                if used and used[0] not in ("statement",):
                    moves.append(f"{lbl}: {', '.join(used)}")
            self.motif_lbl.config(
                text=(f"Shape    {info.get('shape', '')}\n"
                      f"Steps    {info.get('intervals', '')} semitone-steps\n"
                      f"Rhythm   {info.get('rhythm', '')} beats\n"
                      f"Notes    {motif.n}   ·   span {br.get('span', 0)} st"
                      f"   ·   turns {br.get('changes', 0)}\n"
                      f"Chosen from {info.get('candidates', 0)} candidates "
                      f"(score {info.get('score', 0):.2f})\n"
                      f"\nForm     {plan}\n"
                      + ("\n".join(moves) if moves else "")))

        pitches = [n.midi for n in notes]
        lo, hi = min(pitches), max(pitches)
        steps = [abs(b - a) for a, b in zip(pitches, pitches[1:])]
        avg_step = sum(steps) / len(steps) if steps else 0.0
        leaps = sum(1 for s in steps if s > 2)
        repeats = sum(1 for s in steps if s == 0)
        dur = sum(n.dur for n in notes) / max(1, len(notes))
        labels = len({n.label for n in notes})
        self.analysis.config(
            text=(f"Notes: {len(notes)} in {labels} units\n"
                  f"Range: {midi_to_name(lo)} – {midi_to_name(hi)} "
                  f"({hi - lo} semitones)\n"
                  f"Mean interval: {avg_step:.2f} semitones\n"
                  f"Leaps (>2 st): {leaps} of {len(steps)}\n"
                  f"Repeated notes: {repeats} of {len(steps)}\n"
                  f"Mean note length: {dur:.2f} beats"))

    # -- transport ---------------------------------------------------------
    def toggle_play(self, *_a):
        if self.audio is None:
            return
        if not HAS_AUDIO:
            messagebox.showinfo(
                "Playback unavailable",
                "sounddevice is not installed, so live preview is off.\n\n"
                "pip install sounddevice\n\nExport still works.")
            return
        if self.player.playing:
            self.player.pause()
            self._set_play_state(False)
        else:
            self.player.play()
            self._set_play_state(True)

    def stop_playback(self):
        self.player.stop()
        self._set_play_state(False)
        self.seek_var.set(0.0)
        self.roll.set_playhead(0.0)

    def _toggle_gaps(self):
        self.roll.set_show_gaps(self.show_gaps_var.get())

    def toggle_loop(self):
        self.player.loop = not self.player.loop
        self.loop_btn.config(style="ToggleOn.TButton" if self.player.loop
                             else "Toggle.TButton")

    def nudge(self, seconds):
        if self.audio is None:
            return
        self._seek_to(np.clip(self.player.current_time() + seconds,
                              0, self.player.duration()))

    def _seek_to(self, seconds):
        if self.audio is None:
            return
        was = self.player.playing
        self.player.seek(seconds)
        self.seek_var.set(seconds)
        self.roll.set_playhead(seconds)
        if was:
            self.player.pause()
            self.player.play()
            self._set_play_state(True)

    def _set_play_state(self, playing):
        self.play_btn.config(text="Pause" if playing else "Play")

    def _on_seek_drag(self, val):
        if self._seeking:
            self.roll.set_playhead(float(val))

    def _on_seek_release(self, _e):
        self._seeking = False
        self._seek_to(self.seek_var.get())

    def _tick(self):
        if self.audio is not None and not self._seeking:
            cur = self.player.current_time()
            total = self.player.duration()
            self.seek_var.set(cur)
            self.roll.set_playhead(cur)
            self.clock.config(text=f"{self._fmt(cur)} / {self._fmt(total)}")
            if self.player.playing and not self.player.loop and cur >= total - 0.05:
                self._set_play_state(False)
        self.root.after(60, self._tick)

    @staticmethod
    def _fmt(sec):
        m, s = divmod(max(0.0, sec), 60)
        return f"{int(m)}:{s:04.1f}"

    def set_status(self, text):
        self.status.config(text=text)

    # -- export ------------------------------------------------------------
    def _check_audio(self):
        if self.audio is None:
            messagebox.showinfo("Nothing to export", "Generate a melody first.")
            return False
        return True

    def do_export_wav(self):
        if not self._check_audio():
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".wav", initialfile="melody.wav",
            filetypes=[("WAV audio", "*.wav")])
        if path:
            self._export_async(lambda: export_wav(self.audio, path), path)

    def do_export_mp3(self):
        if not self._check_audio():
            return
        if AudioSegment is None or not FFMPEG_PATH:
            messagebox.showerror(
                "MP3 unavailable",
                "MP3 export needs pydub and ffmpeg.\n\n"
                "pip install pydub imageio-ffmpeg")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".mp3", initialfile="melody.mp3",
            filetypes=[("MP3 audio", "*.mp3")])
        if path:
            self._export_async(lambda: export_mp3(self.audio, path), path)

    def do_export_midi(self):
        if not self.notes:
            messagebox.showinfo("Nothing to export", "Generate a melody first.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".mid", initialfile="melody.mid",
            filetypes=[("MIDI file", "*.mid")])
        if not path:
            return
        p = self.values()
        prog = GM_PROGRAMS.get(self.midi_prog_var.get(), 0)
        self._export_async(
            lambda: export_midi(self.notes, path, p["bpm"],
                                p["beats_per_bar"], prog), path)

    def _export_async(self, fn, path):
        self.set_status(f"Writing {os.path.basename(path)}…")

        def worker():
            try:
                fn()
                self.root.after(0, lambda: self.set_status(
                    f"Saved {os.path.basename(path)}"))
            except Exception as exc:
                msg = str(exc)
                traceback.print_exc()
                self.root.after(0, lambda: self.set_status("Export failed"))
                self.root.after(0, lambda: messagebox.showerror("Export failed", msg))

        threading.Thread(target=worker, daemon=True).start()

    def _on_close(self):
        try:
            self.player.stop()
        except Exception:
            pass
        self.root.destroy()


def main():
    if not HAS_AUDIO:
        print("Note: sounddevice missing - live playback disabled "
              "(pip install sounddevice)")
    if AudioSegment is None:
        print("Note: pydub missing - MP3 export disabled (pip install pydub)")
    root = tk.Tk()
    MelodyMakerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
