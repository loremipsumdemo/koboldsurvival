import os
import sys
import warnings
import random
import math

# ---------------------------------------------------------------------------
# ffmpeg setup — MUST happen before pydub is imported
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
        _alias_path = os.path.join(_alias_dir, "ffmpeg.exe")
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
import soundfile as sf
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading

try:
    import sounddevice as sd
    HAS_AUDIO = True
except Exception:
    sd = None
    HAS_AUDIO = False


SAMPLE_RATE = 44100

# ---------------------------------------------------------------------------
# Music theory data
# ---------------------------------------------------------------------------
# Scale degrees as semitone offsets from root, mapped to Roman numeral
# quality so we can construct triads/7ths properly.
SCALES = {
    "Major":            [0, 2, 4, 5, 7, 9, 11],
    "Minor":            [0, 2, 3, 5, 7, 8, 10],
    "Dorian":           [0, 2, 3, 5, 7, 9, 10],
    "Lydian":           [0, 2, 4, 6, 7, 9, 11],
    "Mixolydian":       [0, 2, 4, 5, 7, 9, 10],
    "Major Pentatonic": [0, 2, 4, 7, 9],
    "Minor Pentatonic": [0, 3, 5, 7, 10],
}

# Scale-degree chord qualities per scale mode. For each mode, list the
# quality of the triad built on each scale degree:
#   m = minor, M = major, d = diminished, a = augmented
MODE_QUALITIES = {
    "Major":            ["M", "m", "m", "M", "M", "m", "d"],
    "Minor":            ["m", "d", "M", "m", "m", "M", "M"],
    "Dorian":           ["m", "m", "M", "M", "m", "d", "M"],
    "Lydian":           ["M", "M", "m", "d", "M", "m", "m"],
    "Mixolydian":       ["M", "m", "d", "M", "m", "m", "M"],
    "Major Pentatonic": ["M", "m", "m", "M", "m"],
    "Minor Pentatonic": ["m", "M", "m", "M", "m"],
}

# Common diatonic progressions as (scale-degree index, beats-to-hold).
# Indices are 0-based degree numbers (0 = tonic). Chosen to be
# friendly for ambient — no functional dominants required.
PROGRESSIONS = {
    "I–vi–IV–V":  [0, 5, 3, 4],
    "I–IV–vi–V":  [0, 3, 5, 4],
    "I–V–vi–IV":  [0, 4, 5, 3],
    "vi–IV–I–V":  [5, 3, 0, 4],
    "I–iii–IV–IV": [0, 2, 3, 3],
    "i–VI–III–VII": [0, 5, 2, 6],
    "i–iv–VI–V":  [0, 3, 5, 4],
    "i–VII–VI–VII": [0, 6, 5, 6],
    "I–ii–IV–I":  [0, 1, 3, 0],
    "Imaj7 vamp": [0, 0, 3, 3],
}

# Section patterns — each song is laid out as a sequence of sections.
# Each section has a relative length (weight) and behavioural flags.
SECTION_LAYOUT = [
    ("Intro",  0.10, dict(perc=False, pad_octave=0,  seventh=False)),
    ("Verse",  0.25, dict(perc=True,  pad_octave=0,  seventh=False)),
    ("Chorus", 0.25, dict(perc=True,  pad_octave=12, seventh=True)),
    ("Bridge", 0.15, dict(perc=False, pad_octave=0,  seventh=True)),
    ("Verse",  0.15, dict(perc=True,  pad_octave=0,  seventh=False)),
    ("Outro",  0.10, dict(perc=False, pad_octave=0,  seventh=False)),
]

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def midi_to_freq(midi):
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def note_to_midi(note):
    return 12 * (int(note[-1]) + 1) + NOTE_NAMES.index(note[:-1])


# ---------------------------------------------------------------------------
# Chord construction
# ---------------------------------------------------------------------------
def build_chord(root_midi, scale_name, degree, seventh=False):
    """Return a list of MIDI note numbers for the chord on `degree`."""
    scale = SCALES[scale_name]
    n = len(scale)
    # Extend scale to cover third and fifth above any degree
    extended = []
    for octave in range(0, 3):
        for s in scale:
            extended.append(12 * octave + s)

    # Interval offsets (in scale steps) from scale degree
    if seventh:
        steps = [0, 2, 4, 6]
    else:
        steps = [0, 2, 4]

    notes = []
    for step in steps:
        idx = degree + step
        if idx >= len(extended):
            idx = idx % n
        semis = extended[idx]
        notes.append(root_midi + semis)
    return notes


# ---------------------------------------------------------------------------
# Synthesis primitives
# ---------------------------------------------------------------------------
def generate_pad_note(freq, duration, sample_rate=SAMPLE_RATE,
                      amp_scale=1.0, attack_ratio=0.4, release_ratio=0.5):
    n = int(duration * sample_rate)
    if n <= 0:
        return np.zeros(0)
    t = np.linspace(0, duration, n, endpoint=False)
    wave = np.zeros(n)
    # Slightly warmer harmonic set for sustained chords
    for mult, amp in [(1.0, 1.0), (2.0, 0.30), (3.0, 0.12), (4.0, 0.05)]:
        det = 1.0 + 0.003 * np.sin(2 * np.pi * 0.3 * t)
        wave += amp * np.sin(2 * np.pi * freq * mult * det * t)

    attack = min(2.0, duration * attack_ratio)
    release = min(3.0, duration * release_ratio)
    env = np.ones(n)
    a_samples = int(attack * sample_rate)
    r_samples = int(release * sample_rate)
    if a_samples > 0:
        env[:a_samples] = np.linspace(0, 1, a_samples)
    if r_samples > 0:
        env[-r_samples:] *= np.linspace(1, 0, r_samples)
    return wave * env * amp_scale


def generate_drone(freq, duration, sample_rate=SAMPLE_RATE):
    n = int(duration * sample_rate)
    if n <= 0:
        return np.zeros(0)
    t = np.linspace(0, duration, n, endpoint=False)
    lfo = 1.0 + 0.5 * np.sin(2 * np.pi * 0.05 * t)
    wave = (
        0.6 * np.sin(2 * np.pi * freq * t)
        + 0.3 * np.sin(2 * np.pi * freq * 2.01 * t)
        + 0.15 * np.sin(2 * np.pi * freq * 0.5 * t)
    ) * lfo
    fade = int(min(3.0, duration * 0.3) * sample_rate)
    env = np.ones(n)
    if fade < n // 2:
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
    return wave * env * 0.5


def render_percussion_segment(beats, bpm, sample_rate=SAMPLE_RATE):
    """Render percussion for a specific number of beats.

    Pattern per beat: strong accent on downbeat (beat 1 of every 4),
    soft tick on the others. Returns mono float array.
    """
    beat_dur = 60.0 / bpm
    n = int(beats * beat_dur * sample_rate)
    out = np.zeros(n)

    # Build a single tick sample
    tick_len = int(0.12 * sample_rate)
    tt = np.linspace(0, 0.12, tick_len, endpoint=False)
    soft_tick = np.sin(2 * np.pi * 220 * tt) * np.exp(-tt * 25)
    hard_tick = np.sin(2 * np.pi * 90 * tt) * np.exp(-tt * 20) * 0.7 \
                + np.random.default_rng(0).normal(0, 0.4, tick_len) * np.exp(-tt * 30) * 0.5

    for b in range(int(beats)):
        start = int(b * beat_dur * sample_rate)
        end = min(start + tick_len, n)
        seg = end - start
        if seg <= 0:
            break
        if b % 4 == 0:
            out[start:end] += hard_tick[:seg] * 0.22
        else:
            out[start:end] += soft_tick[:seg] * 0.10
    return out


# ---------------------------------------------------------------------------
# Main generator
# ---------------------------------------------------------------------------
def generate_ambient(
    duration=30.0,
    bpm=60,
    root_note="A3",
    scale_name="Minor",
    progression_name="i–VI–III–VII",
    pad_volume=0.6,
    drone_volume=0.5,
    perc_volume=0.3,
    brightness=0.5,
    density=0.5,
    seed=42,
):
    sample_rate = SAMPLE_RATE
    n = int(duration * sample_rate)
    rng = np.random.default_rng(int(seed))

    root_midi = note_to_midi(root_note)
    beat_dur = 60.0 / bpm

    # Validate progression for chosen scale
    prog = PROGRESSIONS.get(progression_name)
    if prog is None:
        prog = list(PROGRESSIONS.values())[0]
    # Filter degrees that exceed scale length
    scale_len = len(SCALES[scale_name])
    prog = [d if d < scale_len else d % scale_len for d in prog]

    # How many beats each chord is held — density affects harmonic rhythm.
    # density=0 -> chords held for 8 beats (slow), 1 -> 2 beats (fast).
    chord_beats = int(round(8 - 6 * float(np.clip(density, 0.0, 1.0))))
    chord_beats = max(2, chord_beats)

    # ---- Output buffers ----
    pad = np.zeros(n)
    perc = np.zeros(n)

    # ---- Drone (constant root) ----
    drone_root_freq = midi_to_freq(root_midi - 12)
    drone_sig = generate_drone(drone_root_freq, duration, sample_rate)
    if len(drone_sig) < n:
        drone_sig = np.pad(drone_sig, (0, n - len(drone_sig)))
    else:
        drone_sig = drone_sig[:n]

    # ---- Section layout ----
    total_weight = sum(s[1] for s in SECTION_LAYOUT)
    section_spans = []
    cursor = 0.0
    for name, weight, flags in SECTION_LAYOUT:
        length = duration * (weight / total_weight)
        section_spans.append((name, cursor, cursor + length, flags))
        cursor += length

    # Pick a different progression offset per section for variety
    # but keep verse/chorus anchored on the same progression.
    verse_prog = prog
    chorus_prog = prog
    bridge_prog = prog[::-1]   # reverse order for contrast

    # ---- Render each section ----
    for name, t_start, t_end, flags in section_spans:
        sec_start = int(t_start * sample_rate)
        sec_end = min(int(t_end * sample_rate), n)
        sec_len = sec_end - sec_start
        if sec_len <= 0:
            continue

        sec_beats = (t_end - t_start) / beat_dur
        full_chord_cycles = max(1, int(math.ceil(sec_beats / chord_beats)))

        # Choose progression for this section
        if name.startswith("Chorus"):
            this_prog = chorus_prog
        elif name == "Bridge":
            this_prog = bridge_prog
        else:
            this_prog = verse_prog

        octave_shift = flags["pad_octave"]
        add_seventh = flags["seventh"]
        use_perc = flags["perc"]

        # ---- Percussion ----
        if use_perc and perc_volume > 0.0:
            perc_seg = render_percussion_segment(sec_beats, bpm, sample_rate)
            seg_n = min(len(perc_seg), sec_len)
            perc[sec_start:sec_start + seg_n] += perc_seg[:seg_n]

        # ---- Pad chords ----
        # Bass note of the section (root, an octave below chord root)
        bass_freq = midi_to_freq(root_midi - 12)

        t_cursor_beats = 0.0
        chord_idx = 0
        while t_cursor_beats < sec_beats:
            degree = this_prog[chord_idx % len(this_prog)]
            chord_notes = build_chord(root_midi, scale_name, degree,
                                       seventh=add_seventh)

            # Apply octave shift for chorus brightness
            chord_notes = [m + octave_shift for m in chord_notes]

            # Extend into next octave for fuller voicing on chorus
            if name.startswith("Chorus"):
                chord_notes = chord_notes + [m + 12 for m in chord_notes[:2]]

            # Render each note of the chord
            hold_beats = min(chord_beats, sec_beats - t_cursor_beats)
            if hold_beats <= 0:
                break
            hold_sec = hold_beats * beat_dur

            chord_start = sec_start + int(t_cursor_beats * beat_dur * sample_rate)
            chord_end = min(chord_start + int(hold_sec * sample_rate), sec_end)
            chord_len = chord_end - chord_start
            if chord_len <= 0:
                break

            for m in chord_notes:
                freq = midi_to_freq(m)
                # Softer amplitude for higher notes
                amp = 0.6 if m <= root_midi + 12 else 0.4
                note_sig = generate_pad_note(freq, chord_len / sample_rate,
                                              sample_rate, amp_scale=amp)
                seg = min(len(note_sig), chord_len)
                pad[chord_start:chord_start + seg] += note_sig[:seg]

            t_cursor_beats += hold_beats
            chord_idx += 1

    # ---- Brightness filter (low-pass) ----
    brightness = float(np.clip(brightness, 0.0, 1.0))
    win = max(1, int((1.0 - brightness) * 300))
    if win > 1:
        kernel = np.ones(win) / win
        pad = np.convolve(pad, kernel, mode="same")

    # ---- Normalize pad ----
    peak = np.max(np.abs(pad)) or 1.0
    pad = pad / peak

    # ---- Normalize drone ----
    drone_peak = np.max(np.abs(drone_sig)) or 1.0
    drone_sig = drone_sig / drone_peak

    # ---- Normalize percussion ----
    perc_peak = np.max(np.abs(perc)) or 1.0
    perc = perc / perc_peak

    # ---- Mix ----
    mixed = (
        pad * pad_volume
        + drone_sig * drone_volume * 0.7
        + perc * perc_volume * 0.6
    )

    # Gentle fades at the ends to avoid clicks
    fade_in = min(int(2.0 * sample_rate), n // 4)
    fade_out = min(int(2.0 * sample_rate), n // 4)
    if fade_in > 0:
        mixed[:fade_in] *= np.linspace(0, 1, fade_in)
    if fade_out > 0:
        mixed[-fade_out:] *= np.linspace(1, 0, fade_out)

    peak = np.max(np.abs(mixed)) or 1.0
    mixed = mixed / peak * 0.9

    # Stereo width
    delay = int(0.015 * sample_rate)
    left = mixed.copy()
    right = np.concatenate([np.zeros(delay), mixed[:-delay]])
    stereo = np.stack([left, right], axis=1)
    return stereo.astype(np.float32)


def export_mp3(stereo, path):
    if AudioSegment is None:
        raise RuntimeError("pydub not available")
    import tempfile
    tmp_wav = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp_wav.close()
    try:
        sf.write(tmp_wav.name, stereo, SAMPLE_RATE, subtype="PCM_16")
        audio = AudioSegment.from_wav(tmp_wav.name)
        audio.export(path, format="mp3", bitrate="192k")
    finally:
        try:
            os.unlink(tmp_wav.name)
        except Exception:
            pass


class Player:
    def __init__(self):
        self.data = None
        self.pos = 0
        self.stream = None
        self.playing = False
        self.lock = threading.Lock()

    def load(self, data):
        self.stop()
        self.data = data
        self.pos = 0

    def _callback(self, outdata, frames, time_info, status):
        with self.lock:
            if self.data is None:
                outdata.fill(0)
                return
            remaining = len(self.data) - self.pos
            if remaining <= 0:
                outdata.fill(0)
                self.playing = False
                raise sd.CallbackStop()
            n = min(frames, remaining)
            outdata[:n] = self.data[self.pos:self.pos + n]
            if n < frames:
                outdata[n:] = 0
            self.pos += n

    def play(self):
        if not HAS_AUDIO or self.data is None:
            return
        if self.pos >= len(self.data):
            self.pos = 0
        if self.stream is None or not self.stream.active:
            self.stream = sd.OutputStream(
                samplerate=SAMPLE_RATE, channels=2,
                callback=self._callback, dtype="float32",
            )
            self.stream.start()
        self.playing = True

    def pause(self):
        self.playing = False
        if self.stream is not None:
            try:
                self.stream.stop()
            except Exception:
                pass
            self.stream = None

    def stop(self):
        self.pause()
        self.pos = 0

    def seek(self, seconds):
        if self.data is None:
            return
        self.pos = int(np.clip(seconds * SAMPLE_RATE, 0, len(self.data) - 1))

    def current_time(self):
        return self.pos / SAMPLE_RATE

    def duration(self):
        return len(self.data) / SAMPLE_RATE if self.data is not None else 0.0


class ScrollFrame(ttk.Frame):
    def __init__(self, parent, bg="#1e1e2e", **kwargs):
        super().__init__(parent, **kwargs)
        self.canvas = tk.Canvas(self, borderwidth=0, highlightthickness=0, bg=bg)
        self.vsb = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=self.vsb.set)
        self.vsb.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.inner = ttk.Frame(self.canvas)
        self._win = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", self._on_inner_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)
        self.canvas.bind("<Enter>", self._bind_wheel)
        self.canvas.bind("<Leave>", self._unbind_wheel)

    def _on_inner_configure(self, event):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas_configure(self, event):
        self.canvas.itemconfig(self._win, width=event.width)

    def _bind_wheel(self, _event):
        self.canvas.bind_all("<MouseWheel>", self._on_wheel)

    def _unbind_wheel(self, _event):
        self.canvas.unbind_all("<MouseWheel>")

    def _on_wheel(self, event):
        if event.delta:
            self.canvas.yview_scroll(int(-event.delta / 120), "units")


class AmbientApp:
    def __init__(self, root):
        self.root = root
        root.title("Ambient Music Generator")
        root.geometry("640x820")
        root.minsize(560, 600)
        root.configure(bg="#1e1e2e")

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass

        self.player = Player()
        self.generated_data = None
        self.generating = False

        self.seek_var = tk.DoubleVar(value=0)
        self._seeking = False
        self.slider_meta = {}
        self.vars = {}

        # --- Top menu bar ---
        menubar = ttk.Frame(root, padding=(10, 8))
        menubar.pack(side="top", fill="x")

        ttk.Button(menubar, text="🎲 Randomize All",
                   command=self.randomize_all).pack(side="left", padx=(0, 5))
        ttk.Button(menubar, text="↺ Reset Defaults",
                   command=self.reset_defaults).pack(side="left")

        self.status = ttk.Label(menubar, text="", foreground="#888")
        self.status.pack(side="right")

        info = ["ffmpeg: " + ("imageio-ffmpeg" if FFMPEG_PATH else "NOT FOUND")]
        if AudioSegment is None:
            info.append("pydub: MISSING")
        if not HAS_AUDIO:
            info.append("sounddevice: MISSING")
        self.status.config(text="  |  ".join(info))

        ttk.Separator(root, orient="horizontal").pack(side="top", fill="x")

        # --- Player panel ---
        top = ttk.LabelFrame(root, text=" Player ", padding=10)
        top.pack(side="top", fill="x", padx=10, pady=(10, 5))

        row1 = ttk.Frame(top)
        row1.pack(fill="x")
        self.gen_btn = ttk.Button(row1, text="Generate Preview",
                                  command=self.generate_preview)
        self.gen_btn.pack(side="left", fill="x", expand=True, padx=(0, 5))
        self.export_btn = ttk.Button(row1, text="Export MP3…",
                                     command=self.export_mp3, state="disabled")
        self.export_btn.pack(side="left", fill="x", expand=True)

        row2 = ttk.Frame(top)
        row2.pack(fill="x", pady=(8, 0))
        ttk.Button(row2, text="⏮ -10s", width=8,
                   command=lambda: self.nudge(-10)).pack(side="left", padx=2)
        self.play_btn = ttk.Button(row2, text="▶ Play", width=9,
                                   command=self.toggle_play)
        self.play_btn.pack(side="left", padx=2)
        ttk.Button(row2, text="⏹ Stop", width=8,
                   command=self.stop_playback).pack(side="left", padx=2)
        ttk.Button(row2, text="+10s ⏭", width=8,
                   command=lambda: self.nudge(10)).pack(side="left", padx=2)

        row3 = ttk.Frame(top)
        row3.pack(fill="x", pady=(8, 0))
        self.seek_scale = ttk.Scale(row3, from_=0, to=100, orient="horizontal",
                                    variable=self.seek_var,
                                    command=self._on_seek_drag)
        self.seek_scale.pack(fill="x")
        self.seek_scale.bind("<ButtonPress-1>",
                             lambda e: setattr(self, "_seeking", True))
        self.seek_scale.bind("<ButtonRelease-1>", self._on_seek_release)

        row4 = ttk.Frame(top)
        row4.pack(fill="x", pady=(4, 0))
        self.time_lbl = ttk.Label(row4, text="00:00 / 00:00")
        self.time_lbl.pack(side="left")

        # --- Scrollable parameters ---
        params_outer = ttk.LabelFrame(root, text=" Parameters ", padding=5)
        params_outer.pack(side="top", fill="both", expand=True, padx=10, pady=(5, 10))

        scroll = ScrollFrame(params_outer)
        scroll.pack(fill="both", expand=True)
        p = scroll.inner

        # Music section
        ttk.Label(p, text="— Music —", foreground="#aaa").pack(anchor="w", pady=(4, 0))

        self._slider(p, "Duration (s)", "duration", 10, 300, 60, 0)
        self._slider(p, "BPM", "bpm", 30, 180, 70, 0)

        # Root note
        ttk.Label(p, text="Root Note").pack(anchor="w", pady=(10, 0))
        rn_row = ttk.Frame(p)
        rn_row.pack(fill="x")
        self.root_note = tk.StringVar(value="A3")
        self.notes_list = [f"{n}{o}" for o in (2, 3, 4) for n in NOTE_NAMES]
        ttk.Combobox(rn_row, textvariable=self.root_note,
                     values=self.notes_list, state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(rn_row, text="🎲", width=3,
                   command=self.randomize_root_note
                   ).pack(side="left", padx=(5, 0))

        # Scale
        ttk.Label(p, text="Scale").pack(anchor="w", pady=(10, 0))
        sc_row = ttk.Frame(p)
        sc_row.pack(fill="x")
        self.scale_name = tk.StringVar(value="Minor")
        ttk.Combobox(sc_row, textvariable=self.scale_name,
                     values=list(SCALES.keys()), state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(sc_row, text="🎲", width=3,
                   command=self.randomize_scale
                   ).pack(side="left", padx=(5, 0))

        # Progression
        ttk.Label(p, text="Chord Progression").pack(anchor="w", pady=(10, 0))
        pr_row = ttk.Frame(p)
        pr_row.pack(fill="x")
        self.progression_name = tk.StringVar(value="i–VI–III–VII")
        ttk.Combobox(pr_row, textvariable=self.progression_name,
                     values=list(PROGRESSIONS.keys()), state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(pr_row, text="🎲", width=3,
                   command=self.randomize_progression
                   ).pack(side="left", padx=(5, 0))

        self._slider(p, "Random Seed", "seed", 0, 999, 42, 0)

        # Sound section
        ttk.Separator(p, orient="horizontal").pack(fill="x", pady=(15, 5))
        ttk.Label(p, text="— Sound —", foreground="#aaa").pack(anchor="w", pady=(4, 0))

        self._slider(p, "Pad Volume", "pad_volume", 0.0, 1.0, 0.6, 2)
        self._slider(p, "Drone Volume", "drone_volume", 0.0, 1.0, 0.5, 2)
        self._slider(p, "Percussion Volume", "perc_volume", 0.0, 1.0, 0.3, 2)
        self._slider(p, "Brightness", "brightness", 0.0, 1.0, 0.5, 2)
        self._slider(p, "Harmonic Rhythm (density)", "density", 0.0, 1.0, 0.4, 2)

        self.root.after(100, self._tick)

    def _slider(self, parent, label, key, lo, hi, default, decimals):
        ttk.Label(parent, text=label).pack(anchor="w", pady=(10, 0))
        var = tk.DoubleVar(value=default)
        self.vars[key] = var
        self.slider_meta[key] = dict(lo=lo, hi=hi, decimals=decimals)
        row = ttk.Frame(parent)
        row.pack(fill="x")
        ttk.Scale(row, from_=lo, to=hi, variable=var, orient="horizontal",
                  command=lambda v, k=key, d=decimals: self._update_label(k, d)
                  ).pack(side="left", fill="x", expand=True)
        lbl = ttk.Label(row, text=f"{default:.{decimals}f}", width=8)
        lbl.pack(side="left", padx=(4, 2))
        self.vars[f"{key}_lbl"] = lbl
        ttk.Button(row, text="🎲", width=3,
                   command=lambda k=key: self.randomize_slider(k)
                   ).pack(side="left")

    def _update_label(self, key, decimals):
        val = self.vars[key].get()
        self.vars[f"{key}_lbl"].config(text=f"{val:.{decimals}f}")

    def randomize_slider(self, key):
        meta = self.slider_meta[key]
        lo, hi, dec = meta["lo"], meta["hi"], meta["decimals"]
        if dec == 0:
            value = random.randint(int(lo), int(hi))
        else:
            value = round(random.uniform(lo, hi), dec)
        self.vars[key].set(value)
        self._update_label(key, dec)

    def randomize_root_note(self):
        self.root_note.set(random.choice(self.notes_list))

    def randomize_scale(self):
        self.scale_name.set(random.choice(list(SCALES.keys())))

    def randomize_progression(self):
        self.progression_name.set(random.choice(list(PROGRESSIONS.keys())))

    def randomize_all(self):
        for key in self.slider_meta.keys():
            self.randomize_slider(key)
        self.randomize_root_note()
        self.randomize_scale()
        self.randomize_progression()

    def reset_defaults(self):
        defaults = {
            "duration": 60, "bpm": 70,
            "pad_volume": 0.6, "drone_volume": 0.5, "perc_volume": 0.3,
            "brightness": 0.5, "density": 0.4, "seed": 42,
        }
        for key, val in defaults.items():
            self.vars[key].set(val)
            self._update_label(key, self.slider_meta[key]["decimals"])
        self.root_note.set("A3")
        self.scale_name.set("Minor")
        self.progression_name.set("i–VI–III–VII")

    def generate_preview(self):
        if self.generating:
            return
        self.generating = True
        self.gen_btn.config(state="disabled")
        self.export_btn.config(state="disabled")
        self.status.config(text="Generating…")
        self.player.stop()
        self._set_play_state(False)

        params = dict(
            duration=self.vars["duration"].get(),
            bpm=int(self.vars["bpm"].get()),
            root_note=self.root_note.get(),
            scale_name=self.scale_name.get(),
            progression_name=self.progression_name.get(),
            pad_volume=self.vars["pad_volume"].get(),
            drone_volume=self.vars["drone_volume"].get(),
            perc_volume=self.vars["perc_volume"].get(),
            brightness=self.vars["brightness"].get(),
            density=self.vars["density"].get(),
            seed=int(self.vars["seed"].get()),
        )

        def worker():
            try:
                data = generate_ambient(**params)
                self.root.after(0, lambda: self._on_generated(data))
            except Exception as e:
                msg = str(e)
                self.root.after(0, lambda: messagebox.showerror("Generate failed", msg))
                self.root.after(0, lambda: self.status.config(text="Generate failed"))
                self.root.after(0, self._finish_generate)

        threading.Thread(target=worker, daemon=True).start()

    def _on_generated(self, data):
        self.generated_data = data
        self.player.load(data)
        dur = self.player.duration()
        self.seek_scale.config(to=max(1.0, dur))
        self.seek_var.set(0)
        if AudioSegment is not None and FFMPEG_PATH:
            self.export_btn.config(state="normal")
        self.status.config(text=f"Generated {dur:.1f}s — press Play")
        self._finish_generate()

    def _finish_generate(self):
        self.gen_btn.config(state="normal")
        self.generating = False

    def toggle_play(self):
        if self.player.data is None:
            messagebox.showinfo("No audio", "Generate a preview first.")
            return
        if not HAS_AUDIO:
            messagebox.showerror("Playback unavailable",
                                 "sounddevice is not installed.")
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
        self.seek_var.set(0)

    def nudge(self, seconds):
        if self.player.data is None:
            return
        new_pos = self.player.current_time() + seconds
        new_pos = max(0.0, min(new_pos, self.player.duration()))
        was_playing = self.player.playing
        self.player.seek(new_pos)
        if was_playing:
            self.player.pause()
            self.player.play()
        self.seek_var.set(new_pos)

    def _set_play_state(self, playing):
        self.play_btn.config(text="⏸ Pause" if playing else "▶ Play")

    def _on_seek_drag(self, val):
        if self._seeking:
            self.seek_var.set(float(val))

    def _on_seek_release(self, event):
        self._seeking = False
        if self.player.data is not None:
            was_playing = self.player.playing
            self.player.seek(self.seek_var.get())
            if was_playing:
                self.player.pause()
                self.player.play()

    def _tick(self):
        if self.player.data is not None and not self._seeking:
            cur = self.player.current_time()
            total = self.player.duration()
            self.seek_var.set(cur)
            self.time_lbl.config(text=f"{self._fmt(cur)} / {self._fmt(total)}")
            if self.player.playing and cur >= total - 0.05:
                self._set_play_state(False)
        self.root.after(100, self._tick)

    @staticmethod
    def _fmt(sec):
        m, s = divmod(int(sec), 60)
        return f"{m:02d}:{s:02d}"

    def export_mp3(self):
        if self.generated_data is None:
            messagebox.showinfo("No audio", "Generate a preview first.")
            return
        if AudioSegment is None:
            messagebox.showerror("Missing", "pydub is not installed.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".mp3",
            filetypes=[("MP3 Audio", "*.mp3")],
            initialfile="ambient.mp3",
        )
        if not path:
            return
        self.status.config(text="Encoding MP3…")

        def worker():
            try:
                export_mp3(self.generated_data, path)
                self.root.after(0, lambda: self.status.config(text=f"Saved: {path}"))
            except Exception as e:
                msg = str(e)
                self.root.after(0, lambda: messagebox.showerror("Export failed", msg))
                self.root.after(0, lambda: self.status.config(text="Export failed"))

        threading.Thread(target=worker, daemon=True).start()


if __name__ == "__main__":
    if AudioSegment is None:
        print("Warning: pydub not available — MP3 export disabled.")
    if not HAS_AUDIO:
        print("Warning: sounddevice not available — playback disabled.")
    root = tk.Tk()
    AmbientApp(root)
    root.mainloop()