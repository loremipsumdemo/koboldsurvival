import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import numpy as np
import tempfile
import os
import threading
import time
from scipy import signal
import scipy.io.wavfile as wavfile
import sounddevice as sd

try:
    from pydub import AudioSegment
    PYDUB_AVAILABLE = True
except ImportError:
    PYDUB_AVAILABLE = False


# ----------------------------------------------------------------------
# Theme
# ----------------------------------------------------------------------
BG        = "#0f1419"
PANEL     = "#1a2028"
PANEL_HI  = "#232b36"
BORDER    = "#2d3844"
ACCENT    = "#4fc3f7"
ACCENT_HI = "#81d4fa"
TEXT      = "#e6edf3"
TEXT_DIM  = "#8b98a5"
GREEN     = "#4ade80"
RED       = "#f87171"


# ----------------------------------------------------------------------
# Noise generators
# ----------------------------------------------------------------------
def pink_noise(n, rng):
    """Paul Kellet's refined pink filter (~ -3 dB/oct)."""
    white = rng.standard_normal(n)
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0
    out = np.empty(n, dtype=np.float64)
    for i in range(n):
        w = white[i]
        b0 = 0.99886 * b0 + w * 0.0555179
        b1 = 0.99332 * b1 + w * 0.0750759
        b2 = 0.96900 * b2 + w * 0.1538520
        b3 = 0.86650 * b3 + w * 0.3104856
        b4 = 0.55000 * b4 + w * 0.5329522
        b5 = -0.7616 * b5 - w * 0.0168980
        out[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362
        b6 = w * 0.115926
    return out / (np.max(np.abs(out)) + 1e-9)


def brown_noise(n, rng):
    """1/f^2 (brownian) via FFT shaping."""
    white = rng.standard_normal(n)
    X = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n)
    mag = np.zeros_like(freqs)
    mag[1:] = 1.0 / (freqs[1:] ** 2.0)
    mag[0] = 0.0
    X *= mag
    out = np.fft.irfft(X, n=n)
    peak = np.max(np.abs(out))
    return out / (peak + 1e-9)


# ----------------------------------------------------------------------
# Filters
# ----------------------------------------------------------------------
def make_bandpass(sr, lo, hi, order=4):
    b, a = signal.butter(order, [lo / (sr / 2), hi / (sr / 2)], btype='band')
    return b, a


def make_lowpass(sr, cutoff, order=2):
    b, a = signal.butter(order, cutoff / (sr / 2), btype='low')
    return b, a


def make_highpass(sr, cutoff, order=2):
    b, a = signal.butter(order, cutoff / (sr / 2), btype='high')
    return b, a


# ----------------------------------------------------------------------
# Impulse responses
# ----------------------------------------------------------------------
def make_beach_ir(sr, duration=2.5, rt60=2.2, seed=0):
    """
    Synthesize a stereo impulse response that sounds like an outdoor
    beach: early reflections, then a diffuse exponentially decaying
    tail with frequency-dependent decay.
    """
    rng = np.random.default_rng(seed)
    n = int(sr * duration)
    t = np.arange(n) / sr
    decay = np.exp(-6.9078 * t / rt60)  # ln(1000) ≈ 6.9078

    ir = []
    for ch in range(2):
        noise = rng.standard_normal(n) * decay
        b, a = make_bandpass(sr, 120, min(9000, sr / 2 - 500), order=2)
        noise = signal.lfilter(b, a, noise)
        bl, al = make_lowpass(sr, 400, order=2)
        low = signal.lfilter(bl, al, rng.standard_normal(n) * decay) * 0.6
        tail = noise + low
        pre = np.zeros(n)
        for _ in range(8):
            d = int(sr * rng.uniform(0.008, 0.09))
            if d < n:
                pre[d] += rng.uniform(-0.4, 0.4)
        tail += pre
        tail /= (np.max(np.abs(tail)) + 1e-9)
        ir.append(tail)

    ir = np.stack(ir, axis=1)
    ir[:, 1] = ir[:, 1] * 0.98 + np.roll(ir[:, 1], 17) * 0.02
    ir /= (np.max(np.abs(ir)) + 1e-9)
    return ir.astype(np.float32)


# ----------------------------------------------------------------------
# Wave event synthesizer
# ----------------------------------------------------------------------
class WaveEvent:
    """One discrete breaking-wave event, pre-rendered to stereo float32."""
    __slots__ = ("audio", "start_sample", "end_sample")

    def __init__(self, audio, start_sample):
        self.audio = audio
        self.start_sample = start_sample
        self.end_sample = start_sample + audio.shape[0]

    @staticmethod
    def render(sr, rng, size=1.0, brightness=1.0, big=False):
        if big:
            dur = rng.uniform(4.5, 7.0) * size
        else:
            dur = rng.uniform(2.2, 4.5) * size
        n = int(dur * sr)
        if n < 64:
            return None

        t = np.arange(n) / sr

        # 1. Impact transient
        att = rng.uniform(0.025, 0.07)
        trans_env = np.exp(-t / att) * (1 - np.exp(-t / 0.004))
        bp_lo = rng.uniform(400, 700)
        bp_hi = min(rng.uniform(3500, 7500) * brightness, sr / 2 - 500)
        b, a = make_bandpass(sr, bp_lo, bp_hi, order=3)
        crash = signal.lfilter(b, a, rng.standard_normal(n))
        crash *= trans_env
        crash[: int(sr * 0.005)] += (
            rng.standard_normal(int(sr * 0.005)) *
            np.linspace(1.0, 0.0, int(sr * 0.005))
        )
        crash /= (np.max(np.abs(crash)) + 1e-9)

        # 2. Body swell
        attack = rng.uniform(0.15, 0.45)
        body_env = np.exp(-np.maximum(t - attack, 0) / rng.uniform(0.8, 1.8))
        body_env *= (1 - np.exp(-t / attack))
        pink = pink_noise(n, rng)
        b, a = make_bandpass(sr, 80, 2500, order=3)
        body = signal.lfilter(b, a, pink)
        body *= body_env
        body /= (np.max(np.abs(body)) + 1e-9)

        # 3. Low-frequency surge
        f0 = rng.uniform(38, 62) if big else rng.uniform(48, 78)
        glide = np.linspace(1.0, 0.85, n)
        phase = np.cumsum(2 * np.pi * f0 * glide / sr)
        surge = np.sin(phase) * body_env
        surge *= rng.uniform(0.25, 0.55) if big else rng.uniform(0.12, 0.3)
        surge /= (np.max(np.abs(surge)) + 1e-9)

        # 4. Foam tail
        foam_delay = rng.uniform(0.2, 0.5)
        foam_tau = rng.uniform(1.2, 2.8)
        foam_env = np.zeros(n)
        start_i = int(foam_delay * sr)
        if start_i < n:
            tf = t[start_i:] - t[start_i]
            foam_env[start_i:] = (
                (1 - np.exp(-tf / 0.08)) * np.exp(-tf / foam_tau)
            )
        hp_cut = min(rng.uniform(3500, 5500) * brightness, sr / 2 - 500)
        b, a = make_highpass(sr, hp_cut, order=3)
        foam = signal.lfilter(b, a, rng.standard_normal(n)) * foam_env
        foam /= (np.max(np.abs(foam)) + 1e-9)

        # 5. Mix to stereo with equal-power pans
        pan_center = rng.uniform(-0.25, 0.25)
        pan_crash = pan_center + rng.uniform(-0.4, 0.4)
        pan_foam = pan_center + rng.uniform(-0.5, 0.5)
        pan_surge = pan_center + rng.uniform(-0.2, 0.2)

        def eq_pan(x, p):
            ang = (p + 1) * 0.25 * np.pi
            return x * np.cos(ang), x * np.sin(ang)

        L = np.zeros(n); R = np.zeros(n)
        for sig_, pan_, gain in (
            (crash, pan_crash, 0.55),
            (body,  pan_center, 0.85),
            (surge, pan_surge, 0.9),
            (foam,  pan_foam, 0.7),
        ):
            gl, gr = eq_pan(sig_, pan_)
            L += gl * gain
            R += gr * gain

        stereo = np.stack([L, R], axis=1).astype(np.float32)
        peak = np.max(np.abs(stereo)) + 1e-9
        stereo /= peak

        gain = rng.uniform(0.85, 1.0) if big else rng.uniform(0.55, 0.9)
        stereo *= gain
        return stereo


# ----------------------------------------------------------------------
# Partitioned convolution (overlap-add) for realtime reverb
# ----------------------------------------------------------------------
class _Convolver:
    def __init__(self, ir, block=1024):
        self.ir = ir.astype(np.float32)
        self.block = block
        self.ir_len = self.ir.shape[0]
        self.n_part = int(np.ceil(self.ir_len / block))
        self._IR = []
        for p in range(self.n_part):
            seg = self.ir[p * block:(p + 1) * block]
            if seg.shape[0] < block:
                pad = np.zeros((block - seg.shape[0], 2), dtype=np.float32)
                seg = np.concatenate([seg, pad], axis=0)
            f = np.fft.rfft(seg, n=2 * block, axis=0)
            self._IR.append(f.astype(np.complex64))
        self._history = [np.zeros((2 * block, 2), dtype=np.float32)
                         for _ in range(self.n_part)]
        self._overlap = np.zeros((block, 2), dtype=np.float32)

    def reset(self):
        for h in self._history:
            h[:] = 0
        self._overlap[:] = 0

    def process(self, x):
        n = x.shape[0]
        if n < self.block:
            pad = np.zeros((self.block - n, 2), dtype=np.float32)
            x = np.concatenate([x, pad], axis=0)
        elif n > self.block:
            x = x[:self.block]
        self._history.insert(0, x.astype(np.float32))
        self._history.pop()
        Y = np.zeros((self.block + 1, 2), dtype=np.complex64)
        for h, ir_f in zip(self._history, self._IR):
            H = np.fft.rfft(h, n=2 * self.block, axis=0).astype(np.complex64)
            Y += H * ir_f
        y_full = np.fft.irfft(Y, n=2 * self.block, axis=0)
        y = y_full[:self.block]
        out = y + self._overlap
        self._overlap = y_full[self.block:]
        return out.astype(np.float32)


# ----------------------------------------------------------------------
# Ocean engine
# ----------------------------------------------------------------------
class OceanEngine:
    def __init__(self, sample_rate=44100):
        self.sample_rate = sample_rate
        self.stream = None
        self._lock = threading.Lock()

        self.intensity = 0.65
        self.speed = 0.35
        self.loop = True
        self.lerp_active = False
        self.lerp_period = 8.0
        self.lerp_looping = True
        self.echo_on = True
        self.echo_decay = 0.55
        self.echo_delay_ms = 250

        self._finished = False
        self._samples_since_start = 0
        self._duration_samples = -1

        self._rng = np.random.default_rng()
        self._next_event_sample = 0
        self._active_events = []
        self._event_buffer = np.zeros((0, 2), dtype=np.float32)

        self._ir = make_beach_ir(sample_rate)
        self._convolver = _Convolver(self._ir)

        self._bed_chunk = self._make_bed_chunk()

        self._b_hf, self._a_hf = make_lowpass(sample_rate, 13500, order=2)
        self._zi_hf = [np.zeros(max(len(self._a_hf), len(self._b_hf)) - 1,
                                dtype=np.float64) for _ in range(2)]
        self._env = 1.0

        self._lerp_t0 = 0.0
        self._base_int = self.intensity
        self._base_spd = self.speed
        self._int_amp = 0.25
        self._spd_amp = 0.15

    def _make_bed_chunk(self, seconds=8.0):
        n = int(seconds * self.sample_rate)
        rng = np.random.default_rng(1234)
        p = pink_noise(n, rng).astype(np.float32)
        b, a = make_bandpass(self.sample_rate, 180, 1200, order=3)
        p = signal.lfilter(b, a, p).astype(np.float32)
        p /= (np.max(np.abs(p)) + 1e-9)
        p2 = np.roll(p, int(self.sample_rate * 0.013))
        return np.stack([p, p2], axis=1)

    def start(self, duration):
        self._finished = False
        self._samples_since_start = 0
        self._duration_samples = (
            int(duration * self.sample_rate) if not self.loop else -1
        )
        self._rng = np.random.default_rng()
        self._active_events.clear()
        self._event_buffer = np.zeros((0, 2), dtype=np.float32)
        self._next_event_sample = int(
            self.sample_rate * self._rng.uniform(0.05, 0.4)
        )
        self._convolver.reset()
        self._env = 1.0
        self._lerp_t0 = time.time()
        self._base_int = self.intensity
        self._base_spd = self.speed
        self._int_amp = min(0.3, self._base_int * 0.7, (1 - self._base_int) * 0.7)
        self._spd_amp = min(0.25, self._base_spd * 0.7)

        self.stream = sd.OutputStream(
            samplerate=self.sample_rate, channels=2, dtype='float32',
            callback=self._callback, blocksize=1024,
        )
        self.stream.start()

    def stop(self):
        s = self.stream
        self.stream = None
        if s is not None:
            try:
                s.stop()
                s.close()
            except Exception:
                pass

    def _event_interval_samples(self, speed):
        base = 14.0 - 11.0 * speed
        jitter = self._rng.uniform(0.6, 1.4)
        if self._rng.random() < 0.15:
            jitter *= 0.5
        return int(base * jitter * self.sample_rate)

    def _schedule_next_event(self, now_sample, speed):
        self._next_event_sample = now_sample + self._event_interval_samples(speed)

    def _spawn_event(self, speed, intensity):
        big = self._rng.random() < 0.18
        size = self._rng.uniform(0.75, 1.2)
        if big:
            size *= self._rng.uniform(1.1, 1.4)
        brightness = 0.7 + 0.7 * self._rng.random() * (0.6 + 0.6 * speed)
        audio = WaveEvent.render(self.sample_rate, self._rng,
                                 size=size, brightness=brightness, big=big)
        if audio is None:
            return
        audio *= 0.35 + 0.65 * intensity
        ev = WaveEvent(audio, self._samples_since_start)
        self._mix_event_into_buffer(ev)
        self._active_events.append(ev)

    def _mix_event_into_buffer(self, ev):
        # Trim if event starts "before" the write head (shouldn't happen but safe)
        offset = ev.start_sample - self._samples_since_start
        if offset < 0:
            trim = -offset
            ev.audio = ev.audio[trim:]
            ev.start_sample += trim
            offset = 0
        need = offset + ev.audio.shape[0]
        if self._event_buffer.shape[0] < need:
            grow = max(need, self._event_buffer.shape[0] + 8192)
            new = np.zeros((grow, 2), dtype=np.float32)
            if self._event_buffer.shape[0] > 0:
                new[: self._event_buffer.shape[0]] = self._event_buffer
            self._event_buffer = new
        self._event_buffer[offset:need] += ev.audio

    def _callback(self, outdata, frames, time_info, status):
        with self._lock:
            intensity = self.intensity
            speed = self.speed
            loop = self.loop
            lerp_active = self.lerp_active
            lerp_period = self.lerp_period
            lerp_looping = self.lerp_looping
            reverb_on = self.echo_on
            wet = self.echo_decay
            lerp_t0 = self._lerp_t0
            base_int = self._base_int
            base_spd = self._base_spd
            int_amp = self._int_amp
            spd_amp = self._spd_amp

        if lerp_active:
            elapsed = time.time() - lerp_t0
            phase = (elapsed % lerp_period) / lerp_period
            if not lerp_looping and elapsed >= lerp_period:
                phase = 1.0
                self.lerp_active = False
            tri = 1.0 - abs(phase * 2 - 1)
            intensity = float(np.clip(base_int + int_amp * (tri * 2 - 1), 0, 1))
            speed = float(np.clip(base_spd + spd_amp * (1 - tri * 2), 0.05, 1))

        sr = self.sample_rate

        # Schedule events whose trigger time is within this block
        horizon = self._samples_since_start + frames
        while self._next_event_sample <= horizon:
            self._spawn_event(speed, intensity)
            self._schedule_next_event(horizon, speed)

        # Slice event buffer
        if self._event_buffer.shape[0] >= frames:
            events = self._event_buffer[:frames].copy()
            self._event_buffer = self._event_buffer[frames:]
        else:
            events = np.zeros((frames, 2), dtype=np.float32)
            have = self._event_buffer.shape[0]
            if have:
                events[:have] = self._event_buffer
                self._event_buffer = np.zeros((0, 2), dtype=np.float32)

        self._active_events = [
            e for e in self._active_events
            if e.end_sample > self._samples_since_start
        ]

        # Pink bed ambience
        bed = np.empty((frames, 2), dtype=np.float32)
        bed_len = self._bed_chunk.shape[0]
        pos = self._samples_since_start % bed_len
        if pos + frames <= bed_len:
            bed[:] = self._bed_chunk[pos:pos + frames]
        else:
            take = bed_len - pos
            bed[:take] = self._bed_chunk[pos:]
            bed[take:] = self._bed_chunk[:frames - take]
        bed *= 0.06 + 0.12 * intensity

        dry = events + bed

        if reverb_on:
            wet_sig = self._convolver.process(dry)
            mix = dry * (1 - 0.35 * wet) + wet_sig * wet
        else:
            mix = dry

        # Master rolloff
        y0, self._zi_hf[0] = signal.lfilter(
            self._b_hf, self._a_hf, mix[:, 0], zi=self._zi_hf[0])
        y1, self._zi_hf[1] = signal.lfilter(
            self._b_hf, self._a_hf, mix[:, 1], zi=self._zi_hf[1])
        mix = np.stack([y0, y1], axis=1).astype(np.float32)

        # Soft limiter
        peak = float(np.max(np.abs(mix))) + 1e-9
        target = 0.9
        if peak > target:
            desired = target / peak
            self._env = min(desired, self._env * 0.7 + desired * 0.3)
        else:
            self._env = self._env * 0.99 + 0.01
        mix *= self._env
        mix = np.tanh(mix * 1.15) * 0.87

        self._samples_since_start += frames
        if (not loop) and self._duration_samples > 0:
            remaining = self._duration_samples - self._samples_since_start
            if remaining <= 0:
                mix *= 0.0
                self._finished = True
                self.lerp_active = False
            elif remaining < int(0.5 * sr):
                ramp = np.linspace(remaining / (0.5 * sr), 0, frames,
                                   dtype=np.float32)[:, None]
                mix *= ramp

        np.clip(mix, -1.0, 1.0, out=mix)
        outdata[:] = mix


# ----------------------------------------------------------------------
# Offline renderer (same synthesis path)
# ----------------------------------------------------------------------
def render_offline(sr, duration, intensity, speed, reverb_on, wet, seed=0):
    rng = np.random.default_rng(seed)
    n = int(duration * sr)
    if n <= 0:
        return np.zeros((0, 2), dtype=np.float32)

    events_mix = np.zeros((n, 2), dtype=np.float32)
    pos = int(rng.uniform(0.05, 0.4) * sr)
    while pos < n:
        big = rng.random() < 0.18
        size = rng.uniform(0.75, 1.2) * (rng.uniform(1.1, 1.4) if big else 1.0)
        brightness = 0.7 + 0.7 * rng.random() * (0.6 + 0.6 * speed)
        audio = WaveEvent.render(sr, rng, size=size,
                                 brightness=brightness, big=big)
        if audio is not None:
            audio *= 0.35 + 0.65 * intensity
            end = min(pos + audio.shape[0], n)
            events_mix[pos:end] += audio[: end - pos]
        base = 14.0 - 11.0 * speed
        jitter = rng.uniform(0.6, 1.4)
        if rng.random() < 0.15:
            jitter *= 0.5
        pos += int(base * jitter * sr)

    # Bed
    bed = np.empty((n, 2), dtype=np.float32)
    bed_rng = np.random.default_rng(seed + 42)
    pL = pink_noise(n, bed_rng)
    pR = pink_noise(n, bed_rng)
    b, a = make_bandpass(sr, 180, 1200, order=3)
    pL = signal.lfilter(b, a, pL)
    pR = signal.lfilter(b, a, pR)
    pL /= (np.max(np.abs(pL)) + 1e-9)
    pR /= (np.max(np.abs(pR)) + 1e-9)
    bed[:, 0] = pL.astype(np.float32)
    bed[:, 1] = pR.astype(np.float32)
    bed *= (0.06 + 0.12 * intensity)

    dry = events_mix + bed

    if reverb_on:
        ir = make_beach_ir(sr, seed=seed + 1)
        wet_sig = np.stack([
            signal.fftconvolve(dry[:, ch], ir[:, ch])[:n]
            for ch in range(2)
        ], axis=1).astype(np.float32)
        wp = np.max(np.abs(wet_sig)) + 1e-9
        dp = np.max(np.abs(dry)) + 1e-9
        wet_sig *= dp / wp
        mix = dry * (1 - 0.35 * wet) + wet_sig * wet
    else:
        mix = dry

    bh, ah = make_lowpass(sr, 13500, order=2)
    mix = signal.lfilter(bh, ah, mix, axis=0).astype(np.float32)

    peak = np.max(np.abs(mix)) + 1e-9
    if peak > 0.9:
        mix *= 0.9 / peak
    mix = np.tanh(mix * 1.15) * 0.87

    fade = int(0.5 * sr)
    if n > 2 * fade:
        ramp = np.linspace(0, 1, fade, dtype=np.float32)
        mix[:fade] *= ramp[:, None]
        mix[-fade:] *= ramp[::-1, None]

    return mix.astype(np.float32)


# ----------------------------------------------------------------------
# Widgets
# ----------------------------------------------------------------------
class Card(tk.Frame):
    def __init__(self, parent, title, **kw):
        super().__init__(parent, bg=PANEL, highlightbackground=BORDER,
                         highlightthickness=1, **kw)
        if title:
            header = tk.Frame(self, bg=PANEL)
            header.pack(fill=tk.X, padx=14, pady=(10, 0))
            tk.Label(header, text=title, bg=PANEL, fg=ACCENT,
                     font=("Segoe UI", 10, "bold")).pack(side=tk.LEFT)
            tk.Frame(self, bg=BORDER, height=1).pack(fill=tk.X, padx=14, pady=(6, 0))
        self.body = tk.Frame(self, bg=PANEL)
        self.body.pack(fill=tk.BOTH, expand=True, padx=14, pady=10)


class StyledButton(tk.Button):
    def __init__(self, parent, text, command, variant="default", **kw):
        colors = {
            "default": (PANEL_HI, TEXT,      BORDER,    ACCENT),
            "primary": (ACCENT,   "#0f1419", ACCENT,    ACCENT_HI),
            "danger":  ("#3a1f24", RED,      "#5a2f36", RED),
        }[variant]
        bg, fg, bd, hov = colors
        super().__init__(
            parent, text=text, command=command,
            bg=bg, fg=fg, activebackground=hov, activeforeground=fg,
            relief=tk.FLAT, bd=0, highlightthickness=1,
            highlightbackground=bd, highlightcolor=bd,
            font=("Segoe UI", 10, "bold"), cursor="hand2",
            padx=14, pady=8, **kw
        )
        self._bg = bg
        self._hov = hov
        self.bind("<Enter>", lambda e: self.config(bg=self._hov))
        self.bind("<Leave>", lambda e: self.config(bg=self._bg))


class StyledSlider(tk.Frame):
    def __init__(self, parent, label, var, from_, to, unit="",
                 resolution=None, fmt="{:.2f}"):
        super().__init__(parent, bg=PANEL)
        self.var = var
        self.fmt = fmt
        self.unit = unit

        top = tk.Frame(self, bg=PANEL)
        top.pack(fill=tk.X)
        tk.Label(top, text=label, bg=PANEL, fg=TEXT,
                 font=("Segoe UI", 9)).pack(side=tk.LEFT)
        self.value_lbl = tk.Label(top, text=self._fmt(var.get()),
                                  bg=PANEL, fg=ACCENT,
                                  font=("Consolas", 9, "bold"))
        self.value_lbl.pack(side=tk.RIGHT)

        self.scale = tk.Scale(
            self, from_=from_, to=to, orient=tk.HORIZONTAL,
            variable=var, showvalue=False, resolution=resolution or 0.01,
            bg=PANEL, fg=TEXT, troughcolor=BG, highlightthickness=0,
            bd=0, sliderrelief=tk.FLAT, activebackground=ACCENT,
            sliderlength=14, width=6, command=self._on_move
        )
        self.scale.pack(fill=tk.X, pady=(2, 8))
        var.trace_add("write",
                      lambda *_: self.value_lbl.config(text=self._fmt(var.get())))

    def _fmt(self, v):
        try:
            return self.fmt.format(float(v)) + self.unit
        except Exception:
            return str(v)

    def _on_move(self, _):
        self.value_lbl.config(text=self._fmt(self.var.get()))


class ToggleSwitch(tk.Frame):
    def __init__(self, parent, text, var, command=None):
        super().__init__(parent, bg=PANEL, cursor="hand2")
        self.var = var
        self.command = command
        self.canvas = tk.Canvas(self, width=38, height=20, bg=PANEL,
                                highlightthickness=0, cursor="hand2")
        self.canvas.pack(side=tk.LEFT)
        self.lbl = tk.Label(self, text=text, bg=PANEL, fg=TEXT,
                            font=("Segoe UI", 9), cursor="hand2")
        self.lbl.pack(side=tk.LEFT, padx=(8, 0))
        self.canvas.bind("<Button-1>", self._click)
        self.lbl.bind("<Button-1>", self._click)
        var.trace_add("write", lambda *_: self._draw())
        self._draw()

    def _click(self, _):
        self.var.set(not self.var.get())
        if self.command:
            self.command()

    def _draw(self):
        self.canvas.delete("all")
        on = self.var.get()
        track = ACCENT if on else "#3a4553"
        x0, y0, x1, y1 = 1, 2, 37, 18
        self.canvas.create_oval(x0, y0, y0 + 16, y1, fill=track, outline=track)
        self.canvas.create_oval(x1 - 16, y0, x1, y1, fill=track, outline=track)
        self.canvas.create_rectangle(x0 + 8, y0, x1 - 8, y1, fill=track, outline=track)
        kx = 26 if on else 12
        self.canvas.create_oval(kx - 6, 4, kx + 6, 16, fill="#ffffff", outline="")


# ----------------------------------------------------------------------
# Main app
# ----------------------------------------------------------------------
class OceanSoundGenerator:
    def __init__(self, root):
        self.root = root
        self.root.title("Ocean Noise Studio")
        self.root.geometry("560x740")
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        self.sample_rate = 44100
        self.engine = OceanEngine(self.sample_rate)
        self.is_playing = False
        self._watchdog_id = None

        self.loop_enabled = tk.BooleanVar(value=True)

        self._setup_styles()
        self._build_ui()
        self.root.after(100, lambda: self.set_status("Ready", "idle"))
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _setup_styles(self):
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass
        style.configure("TSpinbox",
                        fieldbackground=PANEL_HI, background=PANEL_HI,
                        foreground=TEXT, arrowcolor=ACCENT,
                        bordercolor=BORDER, lightcolor=PANEL_HI,
                        darkcolor=PANEL_HI, insertcolor=TEXT)
        style.map("TSpinbox",
                  fieldbackground=[("focus", PANEL_HI)],
                  bordercolor=[("focus", ACCENT)])

    def _build_ui(self):
        header = tk.Frame(self.root, bg=BG)
        header.pack(fill=tk.X, padx=20, pady=(18, 8))
        tk.Label(header, text="🌊", bg=BG, fg=ACCENT,
                 font=("Segoe UI Emoji", 22)).pack(side=tk.LEFT)
        titles = tk.Frame(header, bg=BG)
        titles.pack(side=tk.LEFT, padx=(10, 0))
        tk.Label(titles, text="Ocean Noise Studio", bg=BG, fg=TEXT,
                 font=("Segoe UI", 16, "bold")).pack(anchor="w")
        tk.Label(titles, text="Event-driven surf · convolution reverb · true stereo",
                 bg=BG, fg=TEXT_DIM, font=("Segoe UI", 9)).pack(anchor="w")

        body = tk.Frame(self.root, bg=BG)
        body.pack(fill=tk.BOTH, expand=True, padx=20, pady=(4, 0))

        sound_card = Card(body, "SURF")
        sound_card.pack(fill=tk.X, pady=(0, 10))
        s = sound_card.body
        dur_row = tk.Frame(s, bg=PANEL)
        dur_row.pack(fill=tk.X, pady=(0, 8))
        tk.Label(dur_row, text="Duration", bg=PANEL, fg=TEXT,
                 font=("Segoe UI", 9)).pack(side=tk.LEFT)
        tk.Label(dur_row, text="seconds", bg=PANEL, fg=TEXT_DIM,
                 font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=(6, 0))
        self.duration_var = tk.IntVar(value=30)
        ttk.Spinbox(dur_row, from_=5, to=300, textvariable=self.duration_var,
                    width=8, font=("Consolas", 10)).pack(side=tk.RIGHT)

        self.intensity_var = tk.DoubleVar(value=0.65)
        StyledSlider(s, "Wave Intensity", self.intensity_var, 0.0, 1.0).pack(fill=tk.X)

        self.speed_var = tk.DoubleVar(value=0.35)
        StyledSlider(s, "Wave Rate", self.speed_var, 0.05, 1.0).pack(fill=tk.X)

        echo_card = Card(body, "REVERB")
        echo_card.pack(fill=tk.X, pady=(0, 10))
        e = echo_card.body
        self.echo_on = tk.BooleanVar(value=True)
        ToggleSwitch(e, "Enable reverb", self.echo_on).pack(anchor="w", pady=(0, 8))
        self.echo_decay = tk.DoubleVar(value=0.55)
        StyledSlider(e, "Wet amount", self.echo_decay, 0.0, 1.0).pack(fill=tk.X)
        self.echo_delay = tk.IntVar(value=250)

        lerp_card = Card(body, "LERP · SMOOTH MODULATION")
        lerp_card.pack(fill=tk.X, pady=(0, 10))
        l = lerp_card.body
        self.lerp_on = tk.BooleanVar(value=False)
        ToggleSwitch(l, "Enable lerp", self.lerp_on).pack(anchor="w", pady=(0, 8))
        self.lerp_duration = tk.DoubleVar(value=8.0)
        StyledSlider(l, "Period", self.lerp_duration, 1.0, 30.0,
                     unit="s", resolution=0.5, fmt="{:.1f}").pack(fill=tk.X)
        self.lerp_looping = tk.BooleanVar(value=True)
        ToggleSwitch(l, "Continuously oscillate", self.lerp_looping).pack(anchor="w")

        actions = tk.Frame(body, bg=BG)
        actions.pack(fill=tk.X, pady=(4, 10))
        StyledButton(actions, "🎲  Randomize", self.randomize).pack(
            side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4))
        self.preview_btn = StyledButton(actions, "▶  Preview",
                                        self.toggle_preview, variant="primary")
        self.preview_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=4)
        StyledButton(actions, "💾  Export", self.export_mp3).pack(
            side=tk.LEFT, expand=True, fill=tk.X, padx=(4, 0))

        loop_row = tk.Frame(body, bg=BG)
        loop_row.pack(fill=tk.X, pady=(0, 10))
        ToggleSwitch(loop_row, "🔁  Loop preview playback",
                     self.loop_enabled).pack(anchor="w")

        status_frame = tk.Frame(self.root, bg=PANEL,
                                highlightbackground=BORDER, highlightthickness=1)
        status_frame.pack(fill=tk.X, side=tk.BOTTOM)
        self.status_dot = tk.Canvas(status_frame, width=10, height=10,
                                    bg=PANEL, highlightthickness=0)
        self.status_dot.pack(side=tk.LEFT, padx=(14, 6), pady=8)
        self._set_status_dot(TEXT_DIM)
        self.status_var = tk.StringVar(value="Ready")
        tk.Label(status_frame, textvariable=self.status_var, bg=PANEL,
                 fg=TEXT_DIM, font=("Segoe UI", 9), anchor="w").pack(
            side=tk.LEFT, fill=tk.X, expand=True, pady=8, padx=(0, 14))

    def _set_status_dot(self, color):
        self.status_dot.delete("all")
        self.status_dot.create_oval(1, 1, 9, 9, fill=color, outline=color)

    def set_status(self, text, kind="idle"):
        self.status_var.set(text)
        self._set_status_dot({"idle": TEXT_DIM, "ok": GREEN,
                              "play": ACCENT, "err": RED}.get(kind, TEXT_DIM))

    def randomize(self):
        self.duration_var.set(int(np.random.choice([15, 20, 30, 45, 60])))
        self.intensity_var.set(round(np.random.uniform(0.4, 0.9), 2))
        self.speed_var.set(round(np.random.uniform(0.15, 0.8), 2))
        self.echo_on.set(bool(np.random.rand() > 0.2))
        self.echo_decay.set(round(np.random.uniform(0.35, 0.75), 2))
        self._push_params()
        self.set_status("Randomized", "ok")

    def _push_params(self):
        eng = self.engine
        with eng._lock:
            eng.intensity = float(self.intensity_var.get())
            eng.speed = float(self.speed_var.get())
            eng.loop = bool(self.loop_enabled.get())
            eng.lerp_active = bool(self.lerp_on.get())
            eng.lerp_period = float(self.lerp_duration.get())
            eng.lerp_looping = bool(self.lerp_looping.get())
            eng.echo_on = bool(self.echo_on.get())
            eng.echo_decay = float(self.echo_decay.get())
            eng.echo_delay_ms = int(self.echo_delay.get())

    def toggle_preview(self):
        if self.is_playing:
            self.stop_preview()
        else:
            self.start_preview()

    def start_preview(self):
        try:
            self._push_params()
            duration = max(1, int(self.duration_var.get()))
            self.engine.start(duration)
            self.is_playing = True
            self.preview_btn.config(text="⏹  Stop", bg="#3a1f24", fg=RED,
                                    activebackground=RED, activeforeground="#0f1419")
            self.preview_btn._bg = "#3a1f24"
            self.preview_btn._hov = "#5a2f36"
            self.set_status(
                "Playing" + (" · loop" if self.loop_enabled.get() else "") +
                (" · lerp" if self.lerp_on.get() else ""), "play")
            self._start_watchdog()
        except Exception as e:
            self.set_status(f"Playback error: {e}", "err")
            messagebox.showerror("Playback error", str(e))

    def _start_watchdog(self):
        if self._watchdog_id is not None:
            self.root.after_cancel(self._watchdog_id)
        def tick():
            if not self.is_playing:
                return
            finished = getattr(self.engine, "_finished", False)
            s = self.engine.stream
            stream_ok = s is not None and getattr(s, "active", False)
            if finished or not stream_ok:
                self.stop_preview(from_watchdog=True)
                return
            self._watchdog_id = self.root.after(100, tick)
        self._watchdog_id = self.root.after(100, tick)

    def stop_preview(self, from_watchdog=False):
        self.is_playing = False
        if self._watchdog_id is not None:
            try:
                self.root.after_cancel(self._watchdog_id)
            except Exception:
                pass
            self._watchdog_id = None
        try:
            self.engine.stop()
        except Exception:
            pass
        self.preview_btn.config(text="▶  Preview", bg=ACCENT, fg="#0f1419",
                                activebackground=ACCENT_HI, activeforeground="#0f1419")
        self.preview_btn._bg = ACCENT
        self.preview_btn._hov = ACCENT_HI
        self.set_status("Stopped", "idle")

    def export_mp3(self):
        if self.is_playing:
            messagebox.showinfo("Playing", "Stop preview before exporting.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".mp3",
            filetypes=[("MP3 files", "*.mp3"), ("WAV files", "*.wav")],
            initialfile="ocean_white_noise.mp3")
        if not path:
            return
        self.set_status("Rendering audio…", "play")
        self.root.update_idletasks()
        try:
            duration = max(1, int(self.duration_var.get()))
            mix = render_offline(
                self.sample_rate, duration,
                float(self.intensity_var.get()),
                float(self.speed_var.get()),
                bool(self.echo_on.get()),
                float(self.echo_decay.get()),
            )
            audio_int16 = (np.clip(mix, -1, 1) * 32767 * 0.95).astype(np.int16)

            tmp_wav = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
            tmp_wav.close()
            wavfile.write(tmp_wav.name, self.sample_rate, audio_int16)

            if path.lower().endswith(".mp3"):
                if not PYDUB_AVAILABLE:
                    messagebox.showerror(
                        "MP3 export unavailable",
                        "pydub is required.\nInstall: pip install pydub\n"
                        "And install ffmpeg (https://ffmpeg.org/).\n\n"
                        "Saving as WAV instead.")
                    path = path.replace(".mp3", ".wav")
                if path.lower().endswith(".mp3"):
                    AudioSegment.from_wav(tmp_wav.name).export(
                        path, format="mp3", bitrate="192k")
                else:
                    import shutil
                    shutil.copy(tmp_wav.name, path)
            else:
                import shutil
                shutil.copy(tmp_wav.name, path)

            os.unlink(tmp_wav.name)
            self.set_status(f"Exported → {os.path.basename(path)}", "ok")
            messagebox.showinfo("Success", f"Saved:\n{path}")
        except Exception as e:
            self.set_status(f"Export failed: {e}", "err")
            messagebox.showerror("Export error", str(e))

    def on_close(self):
        self.is_playing = False
        if self._watchdog_id is not None:
            try:
                self.root.after_cancel(self._watchdog_id)
            except Exception:
                pass
        try:
            self.engine.stop()
        except Exception:
            pass
        self.root.destroy()


def main():
    root = tk.Tk()
    OceanSoundGenerator(root)
    root.mainloop()


if __name__ == "__main__":
    main()