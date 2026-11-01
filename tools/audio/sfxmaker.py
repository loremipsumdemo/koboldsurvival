import os
import random
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import numpy as np
import sounddevice as sd
from scipy.signal import butter, lfilter
from pydub import AudioSegment  # requires ffmpeg for mp3

SAMPLE_RATE = 44100


# ----------------------------------------------------------------------------
# DSP / Sound synthesis
# ----------------------------------------------------------------------------

def _envelope(n, attack=0.01, decay=0.1, sustain=0.7, release=0.3):
    """Simple ADSR envelope over n samples (fractions of total length)."""
    total = n
    a = int(total * attack)
    d = int(total * decay)
    s = int(total * sustain)
    r = total - a - d - s
    r = max(r, 1)
    env = np.concatenate([
        np.linspace(0, 1, max(a, 1)),
        np.linspace(1, 0.7, max(d, 1)),
        np.full(max(s, 1), 0.7),
        np.linspace(0.7, 0, r),
    ])
    if len(env) < n:
        env = np.pad(env, (0, n - len(env)))
    return env[:n]


def _lowpass(data, cutoff, fs, order=4):
    nyq = 0.5 * fs
    b, a = butter(order, cutoff / nyq, btype="low")
    return lfilter(b, a, data)


def _highpass(data, cutoff, fs, order=4):
    nyq = 0.5 * fs
    b, a = butter(order, cutoff / nyq, btype="high")
    return lfilter(b, a, data)


def generate_sound(
    kind="laser",
    duration=0.6,
    base_freq=440.0,
    freq_sweep=1.0,        # end_freq / start_freq
    noise_amount=0.0,
    distortion=1.0,
    lowpass_hz=12000.0,
    highpass_hz=40.0,
    volume=0.8,
):
    """Generate a mono float32 numpy array of the requested sound."""
    n = int(SAMPLE_RATE * duration)
    t = np.linspace(0, duration, n, endpoint=False)

    if kind == "laser":
        f = base_freq * (freq_sweep ** (t / duration))
        phase = 2 * np.pi * np.cumsum(f) / SAMPLE_RATE
        wave = np.sin(phase)
        wave *= np.linspace(1, 0, n)

    elif kind == "explosion":
        noise = np.random.randn(n)
        wave = _lowpass(noise, 400, SAMPLE_RATE)
        wave *= np.exp(-4 * t / duration)

    elif kind == "coin":
        f1, f2 = base_freq, base_freq * max(freq_sweep, 1.2)
        half = n // 2
        seg1 = np.sin(2 * np.pi * f1 * t[:half])
        seg2 = np.sin(2 * np.pi * f2 * t[half:])
        wave = np.concatenate([seg1, seg2])
        wave *= np.exp(-3 * t / duration)

    elif kind == "powerup":
        f = base_freq * (1 + (freq_sweep - 1) * (t / duration)) ** 2
        phase = 2 * np.pi * np.cumsum(f) / SAMPLE_RATE
        wave = np.sin(phase) + 0.3 * np.sin(2 * phase)
        wave *= np.linspace(1, 0.2, n)

    elif kind == "hit":
        noise = np.random.randn(n)
        thump = np.sin(2 * np.pi * base_freq * t)
        wave = 0.6 * noise + 0.8 * thump
        wave *= np.exp(-8 * t / duration)

    elif kind == "jump":
        f = base_freq * (1 + (freq_sweep - 1) * (t / duration))
        phase = 2 * np.pi * np.cumsum(f) / SAMPLE_RATE
        wave = np.sin(phase)
        wave *= np.sin(np.pi * t / duration)

    elif kind == "sweep":
        f = np.linspace(base_freq, base_freq * freq_sweep, n)
        phase = 2 * np.pi * np.cumsum(f) / SAMPLE_RATE
        wave = np.sin(phase)
        wave *= _envelope(n)

    else:  # "noise" / fallback
        wave = np.random.randn(n) * 0.5

    # Mix in extra noise
    if noise_amount > 0 and kind != "noise":
        wave = (1 - noise_amount) * wave + noise_amount * np.random.randn(n)

    # Distortion (soft clip)
    if distortion != 1.0:
        wave = np.tanh(wave * distortion)

    # Filters
    if highpass_hz and highpass_hz > 20:
        wave = _highpass(wave, highpass_hz, SAMPLE_RATE)
    if lowpass_hz and lowpass_hz < SAMPLE_RATE / 2 - 100:
        wave = _lowpass(wave, lowpass_hz, SAMPLE_RATE)

    # Fade in/out to avoid clicks
    fade = int(0.005 * SAMPLE_RATE)
    if n > 2 * fade:
        wave[:fade] *= np.linspace(0, 1, fade)
        wave[-fade:] *= np.linspace(1, 0, fade)

    # Normalize & volume
    peak = np.max(np.abs(wave)) or 1.0
    wave = wave / peak * volume
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

KINDS = ["laser", "explosion", "coin", "powerup", "hit", "jump", "sweep", "noise"]


class SFXApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Sound Effects Generator")
        self.geometry("620x760")
        self.resizable(False, False)

        self.sliders = {}   # name -> (label, var, scale, formatter)
        self._exporting = False
        self._build_ui()
        self._randomize()

    # ---- UI construction ---------------------------------------------------

    def _build_ui(self):
        pad = {"padx": 8, "pady": 3}

        # Top: preview controls -------------------------------------------------
        top = tk.LabelFrame(self, text="Preview")
        top.pack(fill="x", **pad)

        self.kind_var = tk.StringVar(value="laser")
        tk.Label(top, text="Type:").grid(row=0, column=0, sticky="w")
        kind_cb = ttk.Combobox(
            top, textvariable=self.kind_var, values=KINDS,
            state="readonly", width=12,
        )
        kind_cb.grid(row=0, column=1, sticky="w")

        tk.Button(top, text="▶ Play Preview", command=self.play_preview,
                  bg="#4CAF50", fg="white", width=14).grid(
            row=0, column=2, padx=8)
        tk.Button(top, text="🎲 Randomize All", command=self._randomize,
                  bg="#2196F3", fg="white", width=14).grid(
            row=0, column=3, padx=4)
        tk.Button(top, text="💾 Export MP3", command=self.export_mp3,
                  bg="#FF9800", fg="white", width=14).grid(
            row=0, column=4, padx=4)

        # Batch export frame ----------------------------------------------------
        batch = tk.LabelFrame(self, text="Batch Export")
        batch.pack(fill="x", **pad)

        tk.Label(batch, text="Count:").pack(side="left", padx=(8, 2))
        self.batch_count_var = tk.IntVar(value=99)
        tk.Spinbox(batch, from_=1, to=999, textvariable=self.batch_count_var,
                   width=5).pack(side="left", padx=2)

        tk.Label(batch, text="Min ms:").pack(side="left", padx=(12, 2))
        self.batch_min_ms_var = tk.IntVar(value=100)
        tk.Spinbox(batch, from_=10, to=10000, textvariable=self.batch_min_ms_var,
                   width=6).pack(side="left", padx=2)

        tk.Label(batch, text="Max ms:").pack(side="left", padx=(12, 2))
        self.batch_max_ms_var = tk.IntVar(value=500)
        tk.Spinbox(batch, from_=10, to=10000, textvariable=self.batch_max_ms_var,
                   width=6).pack(side="left", padx=2)

        self.batch_btn = tk.Button(
            batch, text="📦 Export Batch (Random)", command=self.export_batch,
            bg="#9C27B0", fg="white", width=22,
        )
        self.batch_btn.pack(side="left", padx=12, pady=6)

        # Sliders frame ---------------------------------------------------------
        sliders_frame = tk.LabelFrame(self, text="Parameters")
        sliders_frame.pack(fill="both", expand=True, **pad)

        self._add_slider(sliders_frame, "duration", "Duration (s)", 0.05, 5.0, 0.6, "{:.2f}")
        self._add_slider(sliders_frame, "base_freq", "Base Freq (Hz)", 20, 2000, 440, "{:.0f}")
        self._add_slider(sliders_frame, "freq_sweep", "Freq Sweep (×)", 0.1, 10.0, 1.0, "{:.2f}")
        self._add_slider(sliders_frame, "noise_amount", "Noise Mix", 0.0, 1.0, 0.0, "{:.2f}")
        self._add_slider(sliders_frame, "distortion", "Distortion", 1.0, 8.0, 1.0, "{:.2f}")
        self._add_slider(sliders_frame, "lowpass_hz", "Low-pass (Hz)", 100, 20000, 12000, "{:.0f}")
        self._add_slider(sliders_frame, "highpass_hz", "High-pass (Hz)", 20, 5000, 40, "{:.0f}")
        self._add_slider(sliders_frame, "volume", "Volume", 0.0, 1.0, 0.8, "{:.2f}")

        # Status bar
        self.status = tk.StringVar(value="Ready")
        tk.Label(self, textvariable=self.status, anchor="w", relief="sunken").pack(
            fill="x", side="bottom")

    def _add_slider(self, parent, name, label_text, lo, hi, default, fmt):
        row = tk.Frame(parent)
        row.pack(fill="x", padx=8, pady=4)

        lbl = tk.Label(row, text=label_text, width=16, anchor="w")
        lbl.pack(side="left")

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
                # ensure UI label refresh
                scale.set(value)

    def _randomize(self):
        params = {
            "duration": round(random.uniform(0.15, 2.5), 2),
            "base_freq": random.uniform(60, 1500),
            "freq_sweep": random.uniform(0.3, 5.0),
            "noise_amount": random.uniform(0.0, 0.6),
            "distortion": random.uniform(1.0, 4.0),
            "lowpass_hz": random.uniform(800, 18000),
            "highpass_hz": random.uniform(20, 400),
            "volume": random.uniform(0.5, 1.0),
        }
        self._set_params(params)
        self.kind_var.set(random.choice(KINDS))
        self.status.set("Randomized all parameters")

    def _random_params_for_batch(self, min_ms, max_ms):
        """Fully random params with duration constrained to [min_ms, max_ms]."""
        duration = random.uniform(min_ms / 1000.0, max_ms / 1000.0)
        return {
            "kind": random.choice(KINDS),
            "duration": duration,
            "base_freq": random.uniform(40, 3000),
            "freq_sweep": random.uniform(0.2, 8.0),
            "noise_amount": random.uniform(0.0, 0.7),
            "distortion": random.uniform(1.0, 6.0),
            "lowpass_hz": random.uniform(300, 20000),
            "highpass_hz": random.uniform(20, 800),
            "volume": random.uniform(0.5, 1.0),
        }

    def _generate(self):
        try:
            params = self._get_params()
            return generate_sound(**params)
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

        if count < 1:
            messagebox.showwarning("Batch export", "Count must be at least 1.")
            return
        if min_ms < 10 or max_ms < 10 or min_ms > max_ms:
            messagebox.showwarning(
                "Batch export",
                "Duration range invalid: require 10 ≤ min ≤ max (ms).",
            )
            return

        out_dir = filedialog.askdirectory(title="Choose output folder for batch export")
        if not out_dir:
            return

        self._exporting = True
        self.batch_btn.config(state="disabled")
        self.status.set(f"Exporting {count} random SFX…")

        # Run in a thread so the UI stays responsive
        t = threading.Thread(
            target=self._batch_worker,
            args=(out_dir, count, min_ms, max_ms),
            daemon=True,
        )
        t.start()

    def _batch_worker(self, out_dir, count, min_ms, max_ms):
        errors = 0
        try:
            for i in range(count):
                try:
                    params = self._random_params_for_batch(min_ms, max_ms)
                    samples = generate_sound(**params)
                    # Zero-pad to 2 digits: sfx_01.mp3 … sfx_99.mp3
                    filename = f"sfx_{i + 1:02d}.mp3"
                    path = os.path.join(out_dir, filename)
                    save_mp3(samples, path)
                except Exception:
                    errors += 1

                # Update status from the worker thread safely
                self.after(
                    0,
                    lambda idx=i: self.status.set(
                        f"Exporting {idx + 1}/{count}…"
                    ),
                )

            self.after(
                0,
                lambda: self._batch_done(out_dir, count, errors),
            )
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
                f"Successfully exported {count} random SFX to:\n{out_dir}",
            )


if __name__ == "__main__":
    app = SFXApp()
    app.mainloop()