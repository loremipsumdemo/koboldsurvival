import os
import sys
import warnings
import random
import math

# ---------------------------------------------------------------------------
# ffmpeg setup
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
SCALES = {
    "Major":            [0, 2, 4, 5, 7, 9, 11],
    "Minor":            [0, 2, 3, 5, 7, 8, 10],
    "Dorian":           [0, 2, 3, 5, 7, 9, 10],
    "Lydian":           [0, 2, 4, 6, 7, 9, 11],
    "Mixolydian":       [0, 2, 4, 5, 7, 9, 10],
}

PROGRESSIONS = {
    "I–V–vi–IV":    [0, 4, 5, 3],
    "I–vi–IV–V":    [0, 5, 3, 4],
    "I–IV–vi–V":    [0, 3, 5, 4],
    "vi–IV–I–V":    [5, 3, 0, 4],
    "i–VI–III–VII": [0, 5, 2, 6],
    "i–iv–VI–V":    [0, 3, 5, 4],
    "i–VII–VI–VII": [0, 6, 5, 6],
    "I–ii–IV–I":    [0, 1, 3, 0],
}

# Instrument "presets" — different timbres for the melody voice
INSTRUMENTS = {
    "Pluck (guitar-ish)":  dict(attack=0.005, decay=0.35, sustain=0.15, release=0.4,
                                 harmonics=[(1, 1.0), (2, 0.5), (3, 0.25), (4, 0.12), (5, 0.06)]),
    "Bell (piano-ish)":    dict(attack=0.002, decay=0.6,  sustain=0.25, release=0.6,
                                 harmonics=[(1, 1.0), (2, 0.6), (3, 0.35), (4, 0.2), (5, 0.1), (6, 0.05)]),
    "Flute (soft)":        dict(attack=0.08,  decay=0.2,  sustain=0.7,  release=0.3,
                                 harmonics=[(1, 1.0), (2, 0.15), (3, 0.05)]),
    "Lead (synth)":        dict(attack=0.01,  decay=0.15, sustain=0.6,  release=0.2,
                                 harmonics=[(1, 1.0), (2, 0.4), (3, 0.25), (4, 0.15)]),
    "Marimba":             dict(attack=0.003, decay=0.4,  sustain=0.1,  release=0.35,
                                 harmonics=[(1, 1.0), (4, 0.4), (6, 0.15)]),
}

# Section layout: (name, weight, melody flags, pad flags)
#   melody: octave offset, note density multiplier, rhythmic subdivision, rests allowed
#   pad: octave offset, seventh?
SECTION_LAYOUT = [
    ("Intro",  0.08, dict(mel_oct=12, mel_density=0.0, mel_sub=2, rest_bias=0.7),
                     dict(pad_oct=0,  seventh=False)),
    ("Verse",  0.24, dict(mel_oct=12, mel_density=0.7, mel_sub=2, rest_bias=0.35),
                     dict(pad_oct=0,  seventh=False)),
    ("Chorus", 0.24, dict(mel_oct=24, mel_density=1.0, mel_sub=2, rest_bias=0.1),
                     dict(pad_oct=12, seventh=True)),
    ("Bridge", 0.14, dict(mel_oct=12, mel_density=0.5, mel_sub=3, rest_bias=0.5),
                     dict(pad_oct=0,  seventh=True)),
    ("Verse",  0.20, dict(mel_oct=12, mel_density=0.7, mel_sub=2, rest_bias=0.35),
                     dict(pad_oct=0,  seventh=False)),
    ("Outro",  0.10, dict(mel_oct=12, mel_density=0.3, mel_sub=2, rest_bias=0.7),
                     dict(pad_oct=0,  seventh=False)),
]

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def midi_to_freq(midi):
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def note_to_midi(note):
    return 12 * (int(note[-1]) + 1) + NOTE_NAMES.index(note[:-1])


# ---------------------------------------------------------------------------
# Chord construction (for accompaniment)
# ---------------------------------------------------------------------------
def chord_pitches(root_midi, scale_name, degree, seventh=False):
    """Return MIDI pitches for a chord on the given scale degree."""
    scale = SCALES[scale_name]
    n = len(scale)
    extended = []
    for octave in range(0, 3):
        for s in scale:
            extended.append(12 * octave + s)
    steps = [0, 2, 4, 6] if seventh else [0, 2, 4]
    notes = []
    for step in steps:
        idx = degree + step
        if idx >= len(extended):
            idx = idx % n
        notes.append(root_midi + extended[idx])
    return notes


# ---------------------------------------------------------------------------
# Synth voices
# ---------------------------------------------------------------------------
def synth_melody_note(midi, duration, instrument_name, sample_rate=SAMPLE_RATE):
    """Synthesize a single melody note with ADSR envelope + harmonics."""
    inst = INSTRUMENTS[instrument_name]
    freq = midi_to_freq(midi)
    n = int(duration * sample_rate)
    if n <= 0:
        return np.zeros(0)
    t = np.linspace(0, duration, n, endpoint=False)

    # Sum harmonics
    wave = np.zeros(n)
    for mult, amp in inst["harmonics"]:
        wave += amp * np.sin(2 * np.pi * freq * mult * t)

    # ADSR envelope
    a = min(inst["attack"], duration * 0.3)
    d = min(inst["decay"], duration * 0.3)
    r = min(inst["release"], duration * 0.4)
    s_amp = inst["sustain"]
    env = np.zeros(n)

    a_end = int(a * sample_rate)
    d_end = int((a + d) * sample_rate)
    r_start = max(d_end, n - int(r * sample_rate))

    if a_end > 0:
        env[:a_end] = np.linspace(0, 1, a_end)
    if d_end > a_end:
        env[a_end:d_end] = np.linspace(1, s_amp, d_end - a_end)
    if r_start > d_end:
        env[d_end:r_start] = s_amp
    if r_start < n:
        seg = n - r_start
        env[r_start:] = np.linspace(s_amp, 0, seg)

    return wave * env


def synth_pad_note(freq, duration, sample_rate=SAMPLE_RATE, amp=1.0):
    """Warm sustained pad note (used for chords)."""
    n = int(duration * sample_rate)
    if n <= 0:
        return np.zeros(0)
    t = np.linspace(0, duration, n, endpoint=False)
    wave = np.zeros(n)
    for mult, h_amp in [(1.0, 1.0), (2.0, 0.30), (3.0, 0.12), (4.0, 0.05)]:
        det = 1.0 + 0.003 * np.sin(2 * np.pi * 0.3 * t)
        wave += h_amp * np.sin(2 * np.pi * freq * mult * det * t)

    attack = min(0.6, duration * 0.3)
    release = min(1.2, duration * 0.5)
    env = np.ones(n)
    a_samples = int(attack * sample_rate)
    r_samples = int(release * sample_rate)
    if a_samples > 0:
        env[:a_samples] = np.linspace(0, 1, a_samples)
    if r_samples > 0:
        env[-r_samples:] *= np.linspace(1, 0, r_samples)
    return wave * env * amp


def synth_drone(freq, duration, sample_rate=SAMPLE_RATE):
    n = int(duration * sample_rate)
    if n <= 0:
        return np.zeros(0)
    t = np.linspace(0, duration, n, endpoint=False)
    lfo = 1.0 + 0.4 * np.sin(2 * np.pi * 0.05 * t)
    wave = (0.7 * np.sin(2 * np.pi * freq * t)
            + 0.3 * np.sin(2 * np.pi * freq * 2.01 * t)) * lfo
    fade = int(min(2.0, duration * 0.3) * sample_rate)
    env = np.ones(n)
    if fade < n // 2:
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
    return wave * env * 0.35


def render_percussion_segment(beats, bpm, sample_rate=SAMPLE_RATE):
    beat_dur = 60.0 / bpm
    n = int(beats * beat_dur * sample_rate)
    out = np.zeros(n)
    tick_len = int(0.12 * sample_rate)
    tt = np.linspace(0, 0.12, tick_len, endpoint=False)
    soft_tick = np.sin(2 * np.pi * 220 * tt) * np.exp(-tt * 25)
    hard_tick = (np.sin(2 * np.pi * 90 * tt) * np.exp(-tt * 20) * 0.7
                 + np.random.default_rng(0).normal(0, 0.4, tick_len)
                 * np.exp(-tt * 30) * 0.5)
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
# Melody generation (the new part)
# ---------------------------------------------------------------------------
def generate_melody_phrase(
    start_beat, beats, bpm, root_midi, scale_name, chord_degree,
    octave_offset, density, subdivision, rest_bias, instrument_name, rng,
):
    """Generate a melodic phrase over a set of beats.

    Returns a list of (start_beat_offset, duration_beats, midi, velocity).
    Musically driven: contour alternates direction, chord tones favored,
    phrase lands on a chord tone on strong beats.
    """
    scale = SCALES[scale_name]
    n_scale = len(scale)

    # Melody note grid: subdivide beats into `subdivision` slots
    slot_beats = 1.0 / subdivision
    total_slots = int(round(beats * subdivision))
    if total_slots <= 0:
        return []

    # Chord tones in the current chord (pitch classes from scale degrees)
    chord_semis = [scale[chord_degree % n_scale],
                   scale[(chord_degree + 2) % n_scale],
                   scale[(chord_degree + 4) % n_scale]]

    # Melody register: base around root + octave_offset, kept within 1 octave
    base_midi = root_midi + octave_offset
    low_bound = base_midi
    high_bound = base_midi + 12

    # Pitch pool: scale degrees across the register
    pitch_pool = []
    for oct_off in (0, 12):
        for s in scale:
            p = root_midi + s + oct_off + octave_offset - 12
            if low_bound <= p <= high_bound:
                pitch_pool.append(p)
    pitch_pool = sorted(set(pitch_pool))
    if not pitch_pool:
        return []

    # Start on a chord tone near the middle of the register
    chord_tones_in_range = [p for p in pitch_pool if (p - root_midi - octave_offset) % 12 in chord_semis]
    if chord_tones_in_range:
        last = rng.choice(chord_tones_in_range)
    else:
        last = pitch_pool[len(pitch_pool) // 2]

    events = []
    direction = 1  # +1 = rising phrase, -1 = falling
    current_slot = 0

    while current_slot < total_slots:
        # Rest?
        is_strong = (current_slot % 4 == 0)
        do_rest = (not is_strong) and rng.random() < (rest_bias * 0.4)
        if do_rest:
            current_slot += 1
            continue

        # Chord tone preference on strong beats
        if is_strong or rng.random() < 0.45:
            candidates = [p for p in pitch_pool
                          if (p - root_midi - octave_offset) % 12 in chord_semis]
            if not candidates:
                candidates = pitch_pool
        else:
            candidates = pitch_pool

        # Prefer stepwise motion in current direction
        if direction > 0:
            preferred = [p for p in candidates if p > last and p - last <= 4]
        else:
            preferred = [p for p in candidates if p < last and last - p <= 4]

        if not preferred:
            preferred = candidates

        pitch = rng.choice(preferred)

        # Occasionally flip direction to avoid running off the register
        if pitch >= high_bound - 1 and direction > 0:
            direction = -1
        elif pitch <= low_bound + 1 and direction < 0:
            direction = 1
        elif rng.random() < 0.15:
            direction *= -1

        # Note duration: 1 or 2 slots, occasionally 3
        dur_slots = 1
        r = rng.random()
        if r < 0.55:
            dur_slots = 1
        elif r < 0.9:
            dur_slots = 2
        else:
            dur_slots = min(3, total_slots - current_slot)

        # Strong beats can stretch
        if is_strong and rng.random() < 0.35:
            dur_slots = max(dur_slots, 2)

        # Velocity accent on downbeats
        velocity = 0.85 if is_strong else 0.7

        events.append((current_slot * slot_beats,
                       dur_slots * slot_beats,
                       int(pitch),
                       velocity))
        last = pitch
        current_slot += dur_slots

    # Force the phrase ending to land on a chord tone on the last slot
    if events:
        last_start, last_dur, last_pitch, _ = events[-1]
        if (last_pitch - root_midi - octave_offset) % 12 not in chord_semis:
            # Nudge down to nearest chord tone
            for offset in (1, -1, 2, -2):
                cand = last_pitch + offset
                if low_bound <= cand <= high_bound and \
                   (cand - root_midi - octave_offset) % 12 in chord_semis:
                    events[-1] = (last_start, last_dur, int(cand), 0.75)
                    break

    return events


# ---------------------------------------------------------------------------
# Main generator
# ---------------------------------------------------------------------------
def generate_song(
    duration=60.0,
    bpm=100,
    root_note="C4",
    scale_name="Major",
    progression_name="I–V–vi–IV",
    instrument_name="Pluck (guitar-ish)",
    melody_volume=0.7,
    chord_volume=0.5,
    drone_volume=0.3,
    perc_volume=0.3,
    brightness=0.5,
    density=0.6,
    seed=42,
):
    sample_rate = SAMPLE_RATE
    n = int(duration * sample_rate)
    rng = np.random.default_rng(int(seed))

    root_midi = note_to_midi(root_note)
    beat_dur = 60.0 / bpm

    prog = PROGRESSIONS.get(progression_name, [0, 4, 5, 3])
    scale = SCALES[scale_name]
    scale_len = len(scale)
    prog = [d % scale_len for d in prog]

    # One chord per bar (4 beats)
    beats_per_chord = 4

    # Buffers
    melody_buf = np.zeros(n)
    chord_buf = np.zeros(n)
    perc_buf = np.zeros(n)

    # Drone
    drone_freq = midi_to_freq(root_midi - 24)
    drone_sig = synth_drone(drone_freq, duration, sample_rate)
    if len(drone_sig) < n:
        drone_sig = np.pad(drone_sig, (0, n - len(drone_sig)))
    else:
        drone_sig = drone_sig[:n]

    # Section layout
    total_weight = sum(s[1] for s in SECTION_LAYOUT)
    section_spans = []
    cursor = 0.0
    for name, weight, mel_flags, pad_flags in SECTION_LAYOUT:
        length = duration * (weight / total_weight)
        section_spans.append((name, cursor, cursor + length, mel_flags, pad_flags))
        cursor += length

    # Iterate sections
    chord_index_global = 0
    for name, t_start, t_end, mel_flags, pad_flags in section_spans:
        sec_start = int(t_start * sample_rate)
        sec_end = min(int(t_end * sample_rate), n)
        sec_len = sec_end - sec_start
        if sec_len <= 0:
            continue
        sec_beats = (t_end - t_start) / beat_dur

        # Progression for this section
        this_prog = prog
        if name == "Bridge":
            this_prog = prog[::-1]

        pad_oct = pad_flags["pad_oct"]
        seventh = pad_flags["seventh"]

        # ---- Chords + Melody: loop over bars ----
        bar = 0
        local_chord_idx = 0
        while True:
            bar_start_beat = bar * beats_per_chord
            if bar_start_beat >= sec_beats:
                break
            bar_beats = min(beats_per_chord, sec_beats - bar_start_beat)
            if bar_beats <= 0:
                break

            degree = this_prog[local_chord_idx % len(this_prog)]
            chord_midis = chord_pitches(root_midi, scale_name, degree, seventh=seventh)

            # ---- Chord pad for this bar ----
            bar_start_sample = sec_start + int(bar_start_beat * beat_dur * sample_rate)
            bar_end_sample = min(bar_start_sample + int(bar_beats * beat_dur * sample_rate), sec_end)
            bar_len = bar_end_sample - bar_start_sample
            if bar_len > 0:
                for m in chord_midis:
                    m_shifted = m + pad_oct
                    amp = 0.5 if m_shifted <= root_midi + 12 else 0.35
                    sig = synth_pad_note(midi_to_freq(m_shifted),
                                          bar_len / sample_rate, sample_rate, amp=amp)
                    seg = min(len(sig), bar_len)
                    chord_buf[bar_start_sample:bar_start_sample + seg] += sig[:seg]

            # ---- Melody for this bar ----
            if mel_flags["mel_density"] > 0:
                mel_events = generate_melody_phrase(
                    start_beat=bar_start_beat,
                    beats=bar_beats,
                    bpm=bpm,
                    root_midi=root_midi,
                    scale_name=scale_name,
                    chord_degree=degree,
                    octave_offset=mel_flags["mel_oct"],
                    density=mel_flags["mel_density"] * density,
                    subdivision=mel_flags["mel_sub"],
                    rest_bias=mel_flags["rest_bias"],
                    instrument_name=instrument_name,
                    rng=rng,
                )
                for (off_beat, dur_beat, midi_pitch, vel) in mel_events:
                    evt_start = bar_start_sample + int(off_beat * beat_dur * sample_rate)
                    evt_dur = dur_beat * beat_dur
                    sig = synth_melody_note(midi_pitch, evt_dur,
                                             instrument_name, sample_rate)
                    # Apply velocity
                    sig = sig * vel
                    evt_end = min(evt_start + len(sig), n)
                    seg = evt_end - evt_start
                    if seg > 0:
                        melody_buf[evt_start:evt_end] += sig[:seg]

            local_chord_idx += 1
            bar += 1

        # ---- Percussion for section (except Intro/Outro/Bridge) ----
        if name in ("Verse", "Chorus") and perc_volume > 0:
            perc_seg = render_percussion_segment(sec_beats, bpm, sample_rate)
            seg_n = min(len(perc_seg), sec_len)
            perc_buf[sec_start:sec_start + seg_n] += perc_seg[:seg_n]

    # ---- Brightness filter ----
    brightness = float(np.clip(brightness, 0.0, 1.0))
    win = max(1, int((1.0 - brightness) * 250))
    if win > 1:
        kernel = np.ones(win) / win
        chord_buf = np.convolve(chord_buf, kernel, mode="same")
        # Lighter filter on melody so it stays articulate
        mel_win = max(1, win // 3)
        if mel_win > 1:
            mel_kernel = np.ones(mel_win) / mel_win
            melody_buf = np.convolve(melody_buf, mel_kernel, mode="same")

    # ---- Normalize each ----
    for buf in (melody_buf, chord_buf, perc_buf, drone_sig):
        peak = np.max(np.abs(buf)) or 1.0
        buf /= peak

    # ---- Mix ----
    mixed = (
        melody_buf * melody_volume
        + chord_buf * chord_volume * 0.7
        + drone_sig * drone_volume * 0.6
        + perc_buf * perc_volume * 0.5
    )

    # Fades
    fade_in = min(int(2.0 * sample_rate), n // 4)
    fade_out = min(int(2.0 * sample_rate), n // 4)
    if fade_in > 0:
        mixed[:fade_in] *= np.linspace(0, 1, fade_in)
    if fade_out > 0:
        mixed[-fade_out:] *= np.linspace(1, 0, fade_out)

    peak = np.max(np.abs(mixed)) or 1.0
    mixed = mixed / peak * 0.9

    # Stereo
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
        root.title("Song Generator")
        root.geometry("660x860")
        root.minsize(580, 620)
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

        # --- Top menu ---
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

        # --- Player ---
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

        # --- Params ---
        params_outer = ttk.LabelFrame(root, text=" Parameters ", padding=5)
        params_outer.pack(side="top", fill="both", expand=True, padx=10, pady=(5, 10))
        scroll = ScrollFrame(params_outer)
        scroll.pack(fill="both", expand=True)
        p = scroll.inner

        ttk.Label(p, text="— Music —", foreground="#aaa").pack(anchor="w", pady=(4, 0))
        self._slider(p, "Duration (s)", "duration", 10, 300, 60, 0)
        self._slider(p, "BPM", "bpm", 50, 180, 100, 0)

        ttk.Label(p, text="Root Note").pack(anchor="w", pady=(10, 0))
        rn_row = ttk.Frame(p)
        rn_row.pack(fill="x")
        self.root_note = tk.StringVar(value="C4")
        self.notes_list = [f"{n}{o}" for o in (3, 4, 5) for n in NOTE_NAMES]
        ttk.Combobox(rn_row, textvariable=self.root_note,
                     values=self.notes_list, state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(rn_row, text="🎲", width=3,
                   command=self.randomize_root_note).pack(side="left", padx=(5, 0))

        ttk.Label(p, text="Scale").pack(anchor="w", pady=(10, 0))
        sc_row = ttk.Frame(p)
        sc_row.pack(fill="x")
        self.scale_name = tk.StringVar(value="Major")
        ttk.Combobox(sc_row, textvariable=self.scale_name,
                     values=list(SCALES.keys()), state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(sc_row, text="🎲", width=3,
                   command=self.randomize_scale).pack(side="left", padx=(5, 0))

        ttk.Label(p, text="Chord Progression").pack(anchor="w", pady=(10, 0))
        pr_row = ttk.Frame(p)
        pr_row.pack(fill="x")
        self.progression_name = tk.StringVar(value="I–V–vi–IV")
        ttk.Combobox(pr_row, textvariable=self.progression_name,
                     values=list(PROGRESSIONS.keys()), state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(pr_row, text="🎲", width=3,
                   command=self.randomize_progression).pack(side="left", padx=(5, 0))

        ttk.Label(p, text="Central Instrument").pack(anchor="w", pady=(10, 0))
        in_row = ttk.Frame(p)
        in_row.pack(fill="x")
        self.instrument_name = tk.StringVar(value="Pluck (guitar-ish)")
        ttk.Combobox(in_row, textvariable=self.instrument_name,
                     values=list(INSTRUMENTS.keys()), state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(in_row, text="🎲", width=3,
                   command=self.randomize_instrument).pack(side="left", padx=(5, 0))

        self._slider(p, "Random Seed", "seed", 0, 999, 42, 0)

        ttk.Separator(p, orient="horizontal").pack(fill="x", pady=(15, 5))
        ttk.Label(p, text="— Mix —", foreground="#aaa").pack(anchor="w", pady=(4, 0))
        self._slider(p, "Melody Volume", "melody_volume", 0.0, 1.0, 0.7, 2)
        self._slider(p, "Chord Volume", "chord_volume", 0.0, 1.0, 0.5, 2)
        self._slider(p, "Drone Volume", "drone_volume", 0.0, 1.0, 0.3, 2)
        self._slider(p, "Percussion Volume", "perc_volume", 0.0, 1.0, 0.3, 2)

        ttk.Separator(p, orient="horizontal").pack(fill="x", pady=(15, 5))
        ttk.Label(p, text="— Character —", foreground="#aaa").pack(anchor="w", pady=(4, 0))
        self._slider(p, "Brightness", "brightness", 0.0, 1.0, 0.5, 2)
        self._slider(p, "Melody Density", "density", 0.0, 1.0, 0.6, 2)

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

    def randomize_instrument(self):
        self.instrument_name.set(random.choice(list(INSTRUMENTS.keys())))

    def randomize_all(self):
        for key in self.slider_meta.keys():
            self.randomize_slider(key)
        self.randomize_root_note()
        self.randomize_scale()
        self.randomize_progression()
        self.randomize_instrument()

    def reset_defaults(self):
        defaults = {
            "duration": 60, "bpm": 100,
            "melody_volume": 0.7, "chord_volume": 0.5,
            "drone_volume": 0.3, "perc_volume": 0.3,
            "brightness": 0.5, "density": 0.6, "seed": 42,
        }
        for key, val in defaults.items():
            self.vars[key].set(val)
            self._update_label(key, self.slider_meta[key]["decimals"])
        self.root_note.set("C4")
        self.scale_name.set("Major")
        self.progression_name.set("I–V–vi–IV")
        self.instrument_name.set("Pluck (guitar-ish)")

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
            instrument_name=self.instrument_name.get(),
            melody_volume=self.vars["melody_volume"].get(),
            chord_volume=self.vars["chord_volume"].get(),
            drone_volume=self.vars["drone_volume"].get(),
            perc_volume=self.vars["perc_volume"].get(),
            brightness=self.vars["brightness"].get(),
            density=self.vars["density"].get(),
            seed=int(self.vars["seed"].get()),
        )

        def worker():
            try:
                data = generate_song(**params)
                self.root.after(0, lambda: self._on_generated(data))
            except Exception as e:
                msg = str(e)
                import traceback
                traceback.print_exc()
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
            initialfile="song.mp3",
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