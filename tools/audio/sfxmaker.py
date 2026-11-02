"""
Pleasant SFX Maker
------------------
Generates soft, musical UI / game sound effects and batch-exports them as
sfx_01.mp3 … sfx_99.mp3.

Design rules (why nothing here sounds harsh):
  * Every pitch is snapped to a major-pentatonic scale, so any two sounds
    from a batch are consonant with each other.
  * Only sine partials (fundamental + octave + fifth + double octave) are
    used. No sawtooth, no square, no distortion, no clipping.
  * The only noise source is a narrow band-passed, low-passed "whoosh",
    which reads as air rather than static.
  * Every sound gets a smooth attack, exponential decay and a 6 ms
    fade-out, so there are no clicks or DC pops.
  * Output is peak-normalised with headroom, so volumes stay even.
"""

import os
import random
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import numpy as np
import sounddevice as sd
from scipy.signal import butter, sosfilt
from pydub import AudioSegment  # requires ffmpeg for mp3

SAMPLE_RATE = 44100

# Target RMS level at volume = 1.0. Every sound is matched to this, so no
# single file in a batch jumps out louder than the rest.
TARGET_RMS = 0.18


# ----------------------------------------------------------------------------
# Musical scale
# ----------------------------------------------------------------------------

# Major pentatonic — no semitone clashes, so random notes always agree.
_PENTATONIC = [0, 2, 4, 7, 9]

NOTE_ROOTS = {
    "C": 261.63,
    "D": 293.66,
    "E": 329.63,
    "F": 349.23,
    "G": 392.00,
    "A": 440.00,
}


def build_scale(root_hz, octaves=4):
    """Pentatonic frequencies starting an octave below the root."""
    base = root_hz / 2.0
    return [
        base * 2 ** ((semi + 12 * octave) / 12.0)
        for octave in range(octaves)
        for semi in _PENTATONIC
    ]


SCALE = build_scale(NOTE_ROOTS["C"])


def set_key(note_name):
    global SCALE
    SCALE = build_scale(NOTE_ROOTS.get(note_name, 261.63))
    return SCALE


def quantize(freq):
    """Snap an arbitrary frequency to the nearest scale note."""
    return min(SCALE, key=lambda f: abs(np.log2(f) - np.log2(max(freq, 1e-6))))


def _scale_step(freq, steps):
    """Move `steps` scale degrees away from `freq` (stays in the scale)."""
    nearest = quantize(freq)
    idx = SCALE.index(nearest)
    return SCALE[int(np.clip(idx + steps, 0, len(SCALE) - 1))]


# ----------------------------------------------------------------------------
# DSP helpers
# ----------------------------------------------------------------------------

def _lowpass(x, cutoff, order=4):
    cutoff = float(np.clip(cutoff, 200.0, SAMPLE_RATE / 2 - 1500))
    sos = butter(order, cutoff / (SAMPLE_RATE / 2), btype="low", output="sos")
    return sosfilt(sos, x)


def _highpass(x, cutoff=90.0, order=2):
    cutoff = float(np.clip(cutoff, 20.0, 1000.0))
    sos = butter(order, cutoff / (SAMPLE_RATE / 2), btype="high", output="sos")
    return sosfilt(sos, x)


def _bandpass(x, lo, hi, order=2):
    lo = float(np.clip(lo, 60.0, SAMPLE_RATE / 2 - 2000))
    hi = float(np.clip(hi, lo + 100.0, SAMPLE_RATE / 2 - 1000))
    sos = butter(order, [lo / (SAMPLE_RATE / 2), hi / (SAMPLE_RATE / 2)],
                 btype="band", output="sos")
    return sosfilt(sos, x)


def _env(n, attack=0.008, decay=4.0, release=0.02):
    """Soft attack, exponential body, cosine release. Never clicks."""
    n = max(int(n), 8)
    env = np.exp(-float(decay) * np.linspace(0.0, 1.0, n))

    a = int(np.clip(attack * SAMPLE_RATE, 16, max(n // 2, 16)))
    env[:a] *= np.sin(np.linspace(0, np.pi / 2, a)) ** 2

    r = int(np.clip(release * SAMPLE_RATE, 32, max(n // 2, 32)))
    env[-r:] *= np.cos(np.linspace(0, np.pi / 2, r)) ** 2
    return env


def _note(freq, dur, brightness=0.3, decay=4.0, attack=0.008):
    """A single soft tone: sine fundamental plus gentle consonant partials."""
    n = max(int(SAMPLE_RATE * dur), 16)
    t = np.arange(n) / SAMPLE_RATE
    phase = 2 * np.pi * freq * t

    wave = np.sin(phase)
    b = float(np.clip(brightness, 0.0, 1.0))
    if freq * 2 < SAMPLE_RATE / 2:
        wave += 0.45 * b * np.sin(2 * phase)          # octave
    if freq * 3 < SAMPLE_RATE / 2:
        wave += 0.18 * b * np.sin(3 * phase)          # fifth above that
    if freq * 4 < SAMPLE_RATE / 2:
        wave += 0.08 * b * np.sin(4 * phase)          # double octave

    return wave * _env(n, attack=attack, decay=decay)


def _glide(f0, f1, dur, brightness=0.25, decay=4.0, attack=0.008):
    """Smooth exponential pitch slide — bubbles, pops, bloops."""
    n = max(int(SAMPLE_RATE * dur), 16)
    t = np.arange(n) / SAMPLE_RATE
    f = f0 * (max(f1, 1e-6) / max(f0, 1e-6)) ** (t / max(dur, 1e-6))
    phase = 2 * np.pi * np.cumsum(f) / SAMPLE_RATE

    wave = np.sin(phase)
    b = float(np.clip(brightness, 0.0, 1.0))
    wave += 0.3 * b * np.sin(2 * phase)

    return wave * _env(n, attack=attack, decay=decay)


def _sequence(freqs, total_dur, brightness, decay, overlap=0.45):
    """Play notes in order inside total_dur, letting each ring on a little.

    Spacing is chosen so the last note's ring-out lands exactly on
    total_dur — the sound never runs longer than you asked for.
    """
    count = max(len(freqs), 1)
    step = max(total_dur / (count + overlap), 0.02)
    note_dur = step * (1.0 + overlap)
    out = np.zeros(int(SAMPLE_RATE * (step * count + note_dur)) + 1)

    for i, f in enumerate(freqs):
        seg = _note(f, note_dur, brightness=brightness, decay=decay)
        start = int(i * step * SAMPLE_RATE)
        end = min(start + len(seg), len(out))
        out[start:end] += seg[:end - start]

    return out


# ----------------------------------------------------------------------------
# Sound kinds — all gentle by construction
# ----------------------------------------------------------------------------

KINDS = [
    "blip",       # single soft beep (menu move)
    "click",      # very short tick (button press)
    "pop",        # bubble-up
    "bloop",      # bubble-down
    "chime",      # bell-ish, rings out
    "coin",       # classic two-note up
    "confirm",    # three-note up
    "cancel",     # two-note down, polite
    "notify",     # arpeggio, attention without alarm
    "powerup",    # five-note run up the scale
    "whoosh",     # airy filtered sweep (the only noise-based sound)
]

# Kinds that need room to breathe (seconds).
_MIN_DUR = {
    "click": 0.03,
    "blip": 0.05,
    "pop": 0.06,
    "bloop": 0.06,
    "chime": 0.25,
    "coin": 0.14,
    "cancel": 0.18,
    "confirm": 0.25,
    "notify": 0.3,
    "powerup": 0.4,
    "whoosh": 0.18,
}


def generate_sound(
    kind="blip",
    pitch=523.25,
    duration=0.25,
    brightness=0.35,
    decay=4.0,
    volume=0.8,
):
    """Generate a mono float32 numpy array. Always musical, never harsh."""
    kind = kind if kind in KINDS else "blip"
    duration = float(max(duration, _MIN_DUR.get(kind, 0.05)))
    f = quantize(pitch)
    brightness = float(np.clip(brightness, 0.0, 1.0))
    decay = float(np.clip(decay, 1.0, 12.0))

    if kind == "blip":
        wave = _note(f, duration, brightness, decay)

    elif kind == "click":
        d = min(duration, 0.09)
        wave = _note(min(f * 2, 2200.0), d, brightness * 0.5,
                     decay=22.0, attack=0.003)

    elif kind == "pop":
        d = min(duration, 0.22)
        wave = _glide(f * 0.6, f * 1.25, d, brightness, decay=9.0)

    elif kind == "bloop":
        d = min(duration, 0.3)
        wave = _glide(f * 1.25, f * 0.62, d, brightness, decay=7.0)

    elif kind == "chime":
        wave = (
            _note(f, duration, brightness, decay * 0.4)
            + 0.5 * _note(_scale_step(f, 2), duration, brightness * 0.7, decay * 0.5)
            + 0.3 * _note(f * 2, duration, brightness * 0.5, decay * 0.7)
        )

    elif kind == "coin":
        wave = _sequence([f, _scale_step(f, 3)], duration, brightness, decay * 1.3)

    elif kind == "confirm":
        wave = _sequence(
            [f, _scale_step(f, 2), _scale_step(f, 4)],
            duration, brightness, decay,
        )

    elif kind == "cancel":
        wave = _sequence(
            [_scale_step(f, 2), f], duration, brightness * 0.7, decay * 1.1,
        )

    elif kind == "notify":
        wave = _sequence(
            [f, _scale_step(f, 3), _scale_step(f, 5)],
            duration, brightness, decay * 0.8, overlap=0.9,
        )

    elif kind == "powerup":
        steps = [0, 1, 2, 3, 5]
        wave = _sequence(
            [_scale_step(f, s) for s in steps],
            duration, brightness, decay * 1.2, overlap=0.6,
        )

    else:  # whoosh
        n = int(SAMPLE_RATE * duration)
        noise = np.random.randn(n)
        centre = float(np.clip(f, 300.0, 1400.0))
        wave = _bandpass(noise, centre * 0.6, centre * 2.2)
        swell = np.sin(np.pi * np.linspace(0, 1, n)) ** 1.6
        wave = wave * swell * 0.9
        wave = _lowpass(wave, 3200.0 + brightness * 1500.0)

    # --- global polish ------------------------------------------------------
    wave = _highpass(wave, 90.0)                      # drop rumble
    top = float(np.clip(max(f * 4.0, 2500.0 + brightness * 7000.0),
                        2500.0, 15000.0))
    wave = _lowpass(wave, top)                        # tame any fizz

    fade = int(0.006 * SAMPLE_RATE)
    if len(wave) > 2 * fade:
        wave[:fade] *= np.linspace(0, 1, fade)
        wave[-fade:] *= np.linspace(1, 0, fade)

    # Loudness-match on RMS (not just peak) so a whole batch plays at an
    # even level — a short click and a long chime end up equally present.
    vol = float(np.clip(volume, 0.0, 1.0))
    peak = float(np.max(np.abs(wave))) or 1.0
    wave = wave / peak
    rms = float(np.sqrt(np.mean(wave ** 2))) or 1.0
    wave *= min(TARGET_RMS * vol / rms, 0.95 * vol)   # peak ceiling wins
    return wave.astype(np.float32)


def save_mp3(samples, path):
    """Save float32 mono samples to an MP3 file."""
    pcm = (np.clip(samples, -1, 1) * 32767).astype(np.int16).tobytes()
    audio = AudioSegment(
        data=pcm,
        sample_width=2,
        frame_rate=SAMPLE_RATE,
        channels=1,
    )
    audio.export(path, format="mp3")


# ----------------------------------------------------------------------------
# GUI
# ----------------------------------------------------------------------------

class SFXApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Pleasant SFX Maker")
        self.geometry("620x520")
        self.resizable(False, False)

        self.sliders = {}
        self._exporting = False
        self._build_ui()
        self._randomize()

    # ---- UI construction ---------------------------------------------------

    def _build_ui(self):
        pad = {"padx": 8, "pady": 3}

        top = tk.LabelFrame(self, text="Preview")
        top.pack(fill="x", **pad)

        self.kind_var = tk.StringVar(value="blip")
        tk.Label(top, text="Type:").grid(row=0, column=0, sticky="w", padx=(8, 2))
        ttk.Combobox(top, textvariable=self.kind_var, values=KINDS,
                     state="readonly", width=10).grid(row=0, column=1, sticky="w")

        tk.Label(top, text="Key:").grid(row=0, column=2, sticky="w", padx=(12, 2))
        self.key_var = tk.StringVar(value="C")
        key_cb = ttk.Combobox(top, textvariable=self.key_var,
                              values=list(NOTE_ROOTS), state="readonly", width=4)
        key_cb.grid(row=0, column=3, sticky="w")
        key_cb.bind("<<ComboboxSelected>>", lambda _e: set_key(self.key_var.get()))

        tk.Button(top, text="▶ Play", command=self.play_preview,
                  bg="#4CAF50", fg="white", width=10).grid(row=0, column=4, padx=6, pady=6)
        tk.Button(top, text="🎲 Randomize", command=self._randomize,
                  bg="#2196F3", fg="white", width=12).grid(row=0, column=5, padx=4)
        tk.Button(top, text="💾 Export MP3", command=self.export_mp3,
                  bg="#FF9800", fg="white", width=12).grid(row=0, column=6, padx=4)

        batch = tk.LabelFrame(self, text="Batch Export  (sfx_01.mp3 … sfx_99.mp3)")
        batch.pack(fill="x", **pad)

        tk.Label(batch, text="Count:").pack(side="left", padx=(8, 2))
        self.batch_count_var = tk.IntVar(value=99)
        tk.Spinbox(batch, from_=1, to=99, textvariable=self.batch_count_var,
                   width=5).pack(side="left", padx=2)

        tk.Label(batch, text="Min ms:").pack(side="left", padx=(12, 2))
        self.batch_min_ms_var = tk.IntVar(value=80)
        tk.Spinbox(batch, from_=30, to=2000, textvariable=self.batch_min_ms_var,
                   width=6).pack(side="left", padx=2)

        tk.Label(batch, text="Max ms:").pack(side="left", padx=(12, 2))
        self.batch_max_ms_var = tk.IntVar(value=600)
        tk.Spinbox(batch, from_=30, to=2000, textvariable=self.batch_max_ms_var,
                   width=6).pack(side="left", padx=2)

        self.batch_btn = tk.Button(
            batch, text="📦 Export Batch", command=self.export_batch,
            bg="#9C27B0", fg="white", width=16,
        )
        self.batch_btn.pack(side="left", padx=12, pady=6)

        sliders_frame = tk.LabelFrame(self, text="Parameters")
        sliders_frame.pack(fill="both", expand=True, **pad)

        self._add_slider(sliders_frame, "pitch", "Pitch (Hz)", 130, 2100, 523, "{:.0f}")
        self._add_slider(sliders_frame, "duration", "Length (s)", 0.03, 1.5, 0.25, "{:.2f}")
        self._add_slider(sliders_frame, "brightness", "Brightness", 0.0, 1.0, 0.35, "{:.2f}")
        self._add_slider(sliders_frame, "decay", "Decay", 1.0, 12.0, 4.0, "{:.1f}")
        self._add_slider(sliders_frame, "volume", "Volume", 0.1, 1.0, 0.8, "{:.2f}")

        tk.Label(
            sliders_frame,
            text="Pitch snaps to the selected key's pentatonic scale — "
                 "every sound in a batch stays in tune.",
            fg="#666", wraplength=560, justify="left",
        ).pack(anchor="w", padx=10, pady=(6, 2))

        self.status = tk.StringVar(value="Ready")
        tk.Label(self, textvariable=self.status, anchor="w", relief="sunken").pack(
            fill="x", side="bottom")

    def _add_slider(self, parent, name, label_text, lo, hi, default, fmt):
        row = tk.Frame(parent)
        row.pack(fill="x", padx=8, pady=4)

        tk.Label(row, text=label_text, width=14, anchor="w").pack(side="left")

        var = tk.DoubleVar(value=default)
        val_lbl = tk.Label(row, text=fmt.format(default), width=9, anchor="e")
        val_lbl.pack(side="right")

        def on_change(_v, v=var, l=val_lbl, f=fmt):
            try:
                l.config(text=f.format(v.get()))
            except Exception:
                pass

        scale = tk.Scale(
            row, from_=lo, to=hi, resolution=(hi - lo) / 500,
            orient="horizontal", variable=var, showvalue=False,
            command=on_change,
        )
        scale.pack(side="left", fill="x", expand=True, padx=6)

        self.sliders[name] = (var, scale, fmt)

    # ---- Helpers ----------------------------------------------------------

    def _get_params(self):
        p = {name: var.get() for name, (var, _, _) in self.sliders.items()}
        p["kind"] = self.kind_var.get()
        return p

    def _set_params(self, params):
        for name, value in params.items():
            if name in self.sliders:
                var, scale, _ = self.sliders[name]
                var.set(value)
                scale.set(value)

    def _randomize(self):
        set_key(self.key_var.get())
        self._set_params({
            "pitch": random.choice([f for f in SCALE if 200 <= f <= 1600]),
            "duration": round(random.uniform(0.08, 0.7), 2),
            "brightness": round(random.uniform(0.15, 0.7), 2),
            "decay": round(random.uniform(2.5, 7.0), 1),
            "volume": round(random.uniform(0.7, 0.9), 2),
        })
        self.kind_var.set(random.choice(KINDS))
        self.status.set("Randomized")

    def _random_params_for_batch(self, min_ms, max_ms):
        kind = random.choice(KINDS)
        duration = random.uniform(min_ms / 1000.0, max_ms / 1000.0)
        candidates = [f for f in SCALE if 200 <= f <= 1600] or SCALE
        return {
            "kind": kind,
            "pitch": random.choice(candidates),
            "duration": max(duration, _MIN_DUR.get(kind, 0.05)),
            "brightness": random.uniform(0.15, 0.7),
            "decay": random.uniform(2.5, 7.0),
            "volume": random.uniform(0.75, 0.9),
        }

    def _generate(self):
        try:
            return generate_sound(**self._get_params())
        except Exception as e:
            messagebox.showerror("Generation error", str(e))
            return None

    def play_preview(self):
        samples = self._generate()
        if samples is None:
            return
        sd.stop()
        sd.play(samples, SAMPLE_RATE)
        self.status.set("Playing preview…")

    def export_mp3(self):
        samples = self._generate()
        if samples is None:
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".mp3",
            filetypes=[("MP3 audio", "*.mp3"), ("All files", "*.*")],
            initialfile=f"{self.kind_var.get()}_sfx.mp3",
        )
        if not path:
            return
        try:
            save_mp3(samples, path)
            self.status.set(f"Exported: {os.path.basename(path)}")
        except Exception as e:
            messagebox.showerror("Export error", str(e))

    # ---- Batch export -----------------------------------------------------

    def export_batch(self):
        if self._exporting:
            return

        count = self.batch_count_var.get()
        min_ms = self.batch_min_ms_var.get()
        max_ms = self.batch_max_ms_var.get()

        if not 1 <= count <= 99:
            messagebox.showwarning("Batch export", "Count must be between 1 and 99.")
            return
        if min_ms < 30 or max_ms < 30 or min_ms > max_ms:
            messagebox.showwarning(
                "Batch export",
                "Duration range invalid: require 30 ≤ min ≤ max (ms).",
            )
            return

        out_dir = filedialog.askdirectory(title="Choose output folder for batch export")
        if not out_dir:
            return

        set_key(self.key_var.get())
        self._exporting = True
        self.batch_btn.config(state="disabled")
        self.status.set(f"Exporting {count} SFX…")

        threading.Thread(
            target=self._batch_worker,
            args=(out_dir, count, min_ms, max_ms),
            daemon=True,
        ).start()

    def _batch_worker(self, out_dir, count, min_ms, max_ms):
        errors = 0
        try:
            for i in range(count):
                try:
                    params = self._random_params_for_batch(min_ms, max_ms)
                    samples = generate_sound(**params)
                    path = os.path.join(out_dir, f"sfx_{i + 1:02d}.mp3")
                    save_mp3(samples, path)
                except Exception:
                    errors += 1

                self.after(
                    0,
                    lambda idx=i: self.status.set(f"Exporting {idx + 1}/{count}…"),
                )

            self.after(0, lambda: self._batch_done(out_dir, count, errors))
        finally:
            self._exporting = False

    def _batch_done(self, out_dir, count, errors):
        self.batch_btn.config(state="normal")
        if errors:
            self.status.set(
                f"Batch complete: {count - errors}/{count} exported "
                f"({errors} failed) → {out_dir}"
            )
            messagebox.showwarning(
                "Batch export",
                f"Exported {count - errors} of {count} files.\n"
                f"{errors} failed. See status bar for folder.",
            )
        else:
            self.status.set(f"Batch complete: {count} files → {out_dir}")
            messagebox.showinfo(
                "Batch export",
                f"Exported {count} sounds (sfx_01.mp3 … sfx_{count:02d}.mp3) to:\n{out_dir}",
            )


if __name__ == "__main__":
    app = SFXApp()
    app.mainloop()
