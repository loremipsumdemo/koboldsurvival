import os
import warnings
import random
import numpy as np
import soundfile as sf
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading

try:
    from scipy.signal import butter, sosfilt
    HAS_SCIPY = True
except Exception:
    HAS_SCIPY = False

try:
    import sounddevice as sd
    HAS_AUDIO = True
except Exception:
    sd = None
    HAS_AUDIO = False

# ffmpeg setup (for MP3 export)
FFMPEG_PATH = None
try:
    import imageio_ffmpeg
    FFMPEG_PATH = imageio_ffmpeg.get_ffmpeg_exe()
    if FFMPEG_PATH and os.path.exists(FFMPEG_PATH):
        _ffdir = os.path.dirname(FFMPEG_PATH)
        os.environ["PATH"] = _ffdir + os.pathsep + os.environ.get("PATH", "")
        import tempfile, shutil
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


SAMPLE_RATE = 44100

PRESETS = [
    "Ocean Waves",
    "Rain",
    "Heavy Rain",
    "Wind",
    "Forest / Leaves",
    "Stream",
    "Fireplace",
    "Thunderstorm",
    "Underwater",
    "White Noise",
]


# ---------------------------------------------------------------------------
# Noise generators
# ---------------------------------------------------------------------------
def white_noise(n, rng):
    return rng.standard_normal(n)


def pink_noise(n, rng):
    """Voss-McCartney-ish pink noise — 1/f spectrum."""
    # Simple method: filter white noise with a cascade of one-pole filters
    white = rng.standard_normal(n)
    # FFT-based method: scale by 1/sqrt(f)
    freqs = np.fft.rfftfreq(n, d=1.0 / SAMPLE_RATE)
    freqs[0] = freqs[1] if len(freqs) > 1 else 1.0
    spectrum = np.fft.rfft(white)
    spectrum *= 1.0 / np.sqrt(freqs)
    pink = np.fft.irfft(spectrum, n)
    return pink


def brown_noise(n, rng):
    """Brown noise — 1/f² spectrum (integrated white noise)."""
    white = rng.standard_normal(n)
    freqs = np.fft.rfftfreq(n, d=1.0 / SAMPLE_RATE)
    freqs[0] = freqs[1] if len(freqs) > 1 else 1.0
    spectrum = np.fft.rfft(white)
    spectrum *= 1.0 / freqs
    brown = np.fft.irfft(spectrum, n)
    return brown


def lowpass(sig, cutoff, order=2):
    if not HAS_SCIPY:
        return sig
    sos = butter(order, cutoff, btype="low", fs=SAMPLE_RATE, output="sos")
    return sosfilt(sos, sig)


def highpass(sig, cutoff, order=2):
    if not HAS_SCIPY:
        return sig
    sos = butter(order, cutoff, btype="high", fs=SAMPLE_RATE, output="sos")
    return sosfilt(sos, sig)


def bandpass(sig, low, high, order=2):
    if not HAS_SCIPY:
        return sig
    sos = butter(order, [low, high], btype="band", fs=SAMPLE_RATE, output="sos")
    return sosfilt(sos, sig)


def _normalize(sig, target=0.9):
    peak = np.max(np.abs(sig)) or 1.0
    return sig / peak * target


# ---------------------------------------------------------------------------
# Texture generators
# ---------------------------------------------------------------------------
def gen_ocean(duration, rng, intensity=0.6, speed=0.5):
    """Ocean waves — brown noise shaped by slow wave envelopes."""
    n = int(duration * SAMPLE_RATE)
    base = brown_noise(n, rng)
    base = lowpass(base, 800 + 800 * intensity)

    # Wave envelopes: slow LFOs with irregular period
    t = np.arange(n) / SAMPLE_RATE
    # Wave period 6–12 seconds depending on speed
    period = 6 + (1 - speed) * 6
    wave_env = np.zeros(n)
    pos = 0.0
    cycle_len = int(period * SAMPLE_RATE)
    while pos < n:
        end = min(int(pos) + cycle_len, n)
        seg_len = end - int(pos)
        if seg_len <= 0:
            break
        # One wave: swell in, crash, recede
        u = np.linspace(0, 1, seg_len)
        # Asymmetric shape: longer swell, quick crash, medium recede
        env = np.where(u < 0.5, (u / 0.5) ** 2,
                       1.0 - ((u - 0.5) / 0.5) ** 1.3)
        env = np.clip(env, 0, 1)
        wave_env[int(pos):end] += env * (0.5 + 0.5 * rng.random())
        # Random period jitter
        pos += cycle_len * (0.85 + 0.3 * rng.random())
        cycle_len = int(period * SAMPLE_RATE * (0.85 + 0.3 * rng.random()))

    # Normalize envelope
    if wave_env.max() > 0:
        wave_env = wave_env / wave_env.max()
    # Minimum level so it never dies completely
    wave_env = 0.15 + 0.85 * wave_env

    ocean = base * wave_env

    # Add spray: highpassed noise burst at wave peaks
    spray = white_noise(n, rng)
    spray = highpass(spray, 4000)
    spray = spray * np.clip(wave_env - 0.5, 0, 1) * 2 * intensity

    return _normalize(ocean + spray * 0.3)


def gen_rain(duration, rng, intensity=0.6, tempo=0.5):
    """Rain — highpassed white noise + droplet transients."""
    n = int(duration * SAMPLE_RATE)
    # Base hiss
    hiss = white_noise(n, rng)
    hiss = highpass(hiss, 1500 + 1500 * intensity)
    hiss = lowpass(hiss, 12000)

    # Density of droplets: 30–120 per second
    drops_per_sec = 30 + 90 * intensity
    n_drops = int(duration * drops_per_sec)
    drops = np.zeros(n)

    for _ in range(n_drops):
        pos = rng.integers(0, n - 1)
        drop_len = int(rng.integers(80, 400))
        if pos + drop_len >= n:
            continue
        # Short filtered click
        click = rng.standard_normal(drop_len)
        t_local = np.arange(drop_len) / SAMPLE_RATE
        env = np.exp(-t_local * rng.uniform(80, 250))
        click = click * env
        drops[pos:pos + drop_len] += click * rng.uniform(0.3, 1.0)

    drops = highpass(drops, 800)
    drops = lowpass(drops, 10000)

    mixed = hiss * 0.5 + drops * 0.6
    return _normalize(mixed)


def gen_heavy_rain(duration, rng, intensity=0.8, tempo=0.6):
    """Denser, darker rain with low rumble."""
    n = int(duration * SAMPLE_RATE)
    # Lower rain base
    hiss = white_noise(n, rng)
    hiss = bandpass(hiss, 400, 9000)
    hiss *= 1.0 + 0.3 * np.sin(2 * np.pi * 0.1 * np.arange(n) / SAMPLE_RATE)

    # Low rumble from brown noise
    rumble = brown_noise(n, rng)
    rumble = lowpass(rumble, 200)
    rumble = rumble / (np.max(np.abs(rumble)) or 1.0) * 0.3

    # Droplets
    drops_per_sec = 60 + 80 * intensity
    n_drops = int(duration * drops_per_sec)
    drops = np.zeros(n)
    for _ in range(n_drops):
        pos = rng.integers(0, n - 1)
        drop_len = int(rng.integers(100, 500))
        if pos + drop_len >= n:
            continue
        click = rng.standard_normal(drop_len)
        t_local = np.arange(drop_len) / SAMPLE_RATE
        env = np.exp(-t_local * rng.uniform(60, 180))
        drops[pos:pos + drop_len] += click * env * rng.uniform(0.4, 1.0)
    drops = bandpass(drops, 600, 8000)

    mixed = hiss * 0.5 + drops * 0.5 + rumble
    return _normalize(mixed)


def gen_wind(duration, rng, intensity=0.6, speed=0.4):
    """Wind — slowly wandering bandpass over pink noise."""
    n = int(duration * SAMPLE_RATE)
    base = pink_noise(n, rng)
    base = base / (np.max(np.abs(base)) or 1.0)

    # Wander center frequency slowly (400–1400 Hz)
    t = np.arange(n) / SAMPLE_RATE
    # Multiple slow sine components
    lfo = (0.5 * np.sin(2 * np.pi * 0.07 * t + rng.uniform(0, 6))
           + 0.3 * np.sin(2 * np.pi * 0.13 * t + rng.uniform(0, 6))
           + 0.2 * np.sin(2 * np.pi * 0.03 * t))
    center = 600 + 400 * lfo

    # Since bandpass cutoff must be fixed for sosfilt, apply in blocks
    out = np.zeros(n)
    block = SAMPLE_RATE  # 1 second
    for i in range(0, n, block):
        b_end = min(i + block, n)
        c = center[i]
        lo = max(80, c * 0.4)
        hi = min(SAMPLE_RATE // 2 - 100, c * 1.8)
        sos = butter(2, [lo, hi], btype="band", fs=SAMPLE_RATE, output="sos")
        # Apply with an overlap to avoid clicks
        pre = max(0, i - 2000)
        post = min(n, b_end + 2000)
        filtered = sosfilt(sos, base[pre:post])
        # Take the middle segment corresponding to [i:b_end]
        seg = filtered[i - pre : (i - pre) + (b_end - i)]
        out[i:b_end] = seg

    # Amplitude swells
    swell = 0.5 + 0.5 * (0.6 * np.sin(2 * np.pi * 0.05 * t + rng.uniform(0, 6))
                         + 0.4 * np.sin(2 * np.pi * 0.11 * t + rng.uniform(0, 6)))
    swell = np.clip(swell, 0.1, 1.0)
    out *= swell

    # Add gusty higher-band content
    gust = white_noise(n, rng)
    gust = bandpass(gust, 800, 3000)
    gust *= swell ** 2 * intensity * 0.4

    mixed = out + gust
    return _normalize(mixed)


def gen_forest(duration, rng, intensity=0.5, speed=0.4):
    """Forest leaves — soft mid noise with rustles."""
    n = int(duration * SAMPLE_RATE)
    # Base: soft mid-band noise
    base = pink_noise(n, rng)
    base = bandpass(base, 300, 4000)
    base = base / (np.max(np.abs(base)) or 1.0) * 0.4

    # Rustles: short filtered noise bursts
    rustles = np.zeros(n)
    n_rustles = int(duration * (2 + 6 * intensity))
    for _ in range(n_rustles):
        pos = rng.integers(0, max(1, n - SAMPLE_RATE // 2))
        length = int(rng.uniform(0.1, 0.5) * SAMPLE_RATE)
        if pos + length >= n:
            continue
        burst = rng.standard_normal(length)
        burst = bandpass(burst, 2000, 8000)
        # Envelope: quick swell + decay
        u = np.linspace(0, 1, length)
        env = np.sin(np.pi * u) ** 2
        rustles[pos:pos + length] += burst * env * rng.uniform(0.3, 1.0)

    # Occasional distant bird-ish chirp (very subtle, optional)
    chirps = np.zeros(n)
    n_chirps = int(duration * 0.3)
    for _ in range(n_chirps):
        pos = rng.integers(0, max(1, n - 4410))
        length = int(rng.uniform(0.05, 0.15) * SAMPLE_RATE)
        freq = rng.uniform(2000, 4000)
        t_local = np.arange(length) / SAMPLE_RATE
        chirp = np.sin(2 * np.pi * freq * t_local) * np.exp(-t_local * 20)
        chirps[pos:pos + length] += chirp * 0.08

    mixed = base + rustles * 0.5 + chirps
    return _normalize(mixed)


def gen_stream(duration, rng, intensity=0.6, speed=0.5):
    """Stream — bandpass noise with watery warble."""
    n = int(duration * SAMPLE_RATE)
    base = white_noise(n, rng)
    base = bandpass(base, 800, 4000)
    base = base / (np.max(np.abs(base)) or 1.0)

    # Watery warble: subtle amplitude modulation + slight pitch-like wobble
    t = np.arange(n) / SAMPLE_RATE
    warble = 1.0 + 0.15 * np.sin(2 * np.pi * 4.5 * t + rng.uniform(0, 6))
    warble *= 1.0 + 0.1 * np.sin(2 * np.pi * 11 * t + rng.uniform(0, 6))

    # Bubble-esque occasional accents
    bubbles = np.zeros(n)
    n_bubbles = int(duration * (3 + 5 * intensity))
    for _ in range(n_bubbles):
        pos = rng.integers(0, max(1, n - 4410))
        length = int(rng.uniform(0.02, 0.08) * SAMPLE_RATE)
        freq = rng.uniform(600, 1500)
        t_local = np.arange(length) / SAMPLE_RATE
        bub = np.sin(2 * np.pi * freq * t_local) * np.exp(-t_local * 40)
        bubbles[pos:pos + length] += bub * rng.uniform(0.15, 0.35)

    # Lower-frequency stream bed
    bed = brown_noise(n, rng)
    bed = lowpass(bed, 300)
    bed = bed / (np.max(np.abs(bed)) or 1.0) * 0.3

    mixed = base * warble * 0.6 + bubbles + bed
    return _normalize(mixed)


def gen_fireplace(duration, rng, intensity=0.6, speed=0.4):
    """Fireplace — brown noise bed with pops and crackles."""
    n = int(duration * SAMPLE_RATE)
    # Bed
    bed = brown_noise(n, rng)
    bed = lowpass(bed, 800)
    bed = bed / (np.max(np.abs(bed)) or 1.0) * 0.4

    # Constant soft crackle layer
    crackle = white_noise(n, rng)
    crackle = bandpass(crackle, 1000, 6000)
    crackle *= 0.1

    # Pops
    pops = np.zeros(n)
    n_pops = int(duration * (2 + 6 * intensity))
    for _ in range(n_pops):
        pos = rng.integers(0, max(1, n - 4410))
        length = int(rng.uniform(0.005, 0.05) * SAMPLE_RATE)
        env = np.exp(-np.arange(length) / SAMPLE_RATE * rng.uniform(100, 400))
        pop = rng.standard_normal(length) * env
        pops[pos:pos + length] += pop * rng.uniform(0.4, 1.0)

    # Low rumble of the fire
    rumble = brown_noise(n, rng)
    rumble = lowpass(rumble, 150)
    rumble *= 0.3

    mixed = bed + crackle + pops * 0.8 + rumble
    return _normalize(mixed)


def gen_thunderstorm(duration, rng, intensity=0.7, speed=0.5):
    """Thunderstorm — rain base + periodic thunder rumbles."""
    n = int(duration * SAMPLE_RATE)
    rain = gen_rain(duration, rng, intensity=0.9, tempo=0.6)

    # Add thunder: deep brown noise bursts with slow decay
    thunder = np.zeros(n)
    n_strikes = max(1, int(duration / 15 * intensity * 2))
    for _ in range(n_strikes):
        pos = rng.integers(0, max(1, n - SAMPLE_RATE * 3))
        length = int(rng.uniform(1.5, 3.5) * SAMPLE_RATE)
        if pos + length >= n:
            continue
        # Thunder: two layers — sharp crack + long rumble
        t_local = np.arange(length) / SAMPLE_RATE
        rumble = brown_noise(length, rng)
        rumble = lowpass(rumble, 150)
        rumble /= (np.max(np.abs(rumble)) or 1.0)
        env = np.exp(-t_local * 0.8) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.3 * t_local))
        thunder[pos:pos + length] += rumble * env * rng.uniform(0.5, 1.0)

    mixed = rain * 0.7 + thunder * 0.6
    return _normalize(mixed)


def gen_underwater(duration, rng, intensity=0.5, speed=0.3):
    """Underwater — very lowpassed brown noise with pressure pulses."""
    n = int(duration * SAMPLE_RATE)
    base = brown_noise(n, rng)
    base = lowpass(base, 300 + 200 * intensity)
    base = base / (np.max(np.abs(base)) or 1.0)

    # Pressure pulses: slow heart-beat-like swells
    t = np.arange(n) / SAMPLE_RATE
    pulse_period = 4 + (1 - speed) * 4
    pulse = np.sin(2 * np.pi / pulse_period * t) ** 8  # sharp peaks
    pulse = 0.4 + 0.6 * pulse

    # Occasional distant whale-ish moans (optional, subtle)
    moans = np.zeros(n)
    n_moans = max(1, int(duration / 20))
    for _ in range(n_moans):
        pos = rng.integers(0, max(1, n - SAMPLE_RATE * 2))
        length = int(rng.uniform(1.0, 2.0) * SAMPLE_RATE)
        if pos + length >= n:
            continue
        t_local = np.arange(length) / SAMPLE_RATE
        freq_sweep = np.linspace(rng.uniform(80, 150), rng.uniform(60, 110), length)
        phase = np.cumsum(2 * np.pi * freq_sweep / SAMPLE_RATE)
        moan = np.sin(phase) * np.exp(-t_local * 1.5) * 0.15
        moans[pos:pos + length] += moan

    mixed = base * pulse + moans
    return _normalize(mixed)


def gen_white_noise(duration, rng, intensity=0.5, speed=0.5):
    """Plain filtered white noise."""
    n = int(duration * SAMPLE_RATE)
    base = white_noise(n, rng)
    base = bandpass(base, 20, 18000)
    return _normalize(base)


# ---------------------------------------------------------------------------
# Dispatcher
# ---------------------------------------------------------------------------
TEXTURES = {
    "Ocean Waves": gen_ocean,
    "Rain": gen_rain,
    "Heavy Rain": gen_heavy_rain,
    "Wind": gen_wind,
    "Forest / Leaves": gen_forest,
    "Stream": gen_stream,
    "Fireplace": gen_fireplace,
    "Thunderstorm": gen_thunderstorm,
    "Underwater": gen_underwater,
    "White Noise": gen_white_noise,
}


def generate_texture(name, duration, intensity, speed, seed=42, stereo_width=0.6):
    rng = np.random.default_rng(int(seed))
    # Generate two slightly decorrelated mono signals for stereo width
    left = TEXTURES[name](duration, rng, intensity=intensity, speed=speed)
    right_rng = np.random.default_rng(int(seed) + 1)
    right = TEXTURES[name](duration, right_rng, intensity=intensity, speed=speed)

    n = min(len(left), len(right))
    left = left[:n]
    right = right[:n]

    # Blend toward mono based on stereo_width
    mono = 0.5 * (left + right)
    left_out = mono * (1 - stereo_width) + left * stereo_width
    right_out = mono * (1 - stereo_width) + right * stereo_width

    # End fades to prevent clicks
    fade = min(int(2.0 * SAMPLE_RATE), n // 4)
    if fade > 0:
        left_out[:fade] *= np.linspace(0, 1, fade)
        left_out[-fade:] *= np.linspace(1, 0, fade)
        right_out[:fade] *= np.linspace(0, 1, fade)
        right_out[-fade:] *= np.linspace(1, 0, fade)

    stereo = np.stack([left_out, right_out], axis=1)
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


# ---------------------------------------------------------------------------
# Player
# ---------------------------------------------------------------------------
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
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(
            scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfig(
            self._win, width=e.width))
        self.canvas.bind("<Enter>", lambda e: self.canvas.bind_all(
            "<MouseWheel>", self._on_wheel))
        self.canvas.bind("<Leave>", lambda e: self.canvas.unbind_all("<MouseWheel>"))

    def _on_wheel(self, event):
        if event.delta:
            self.canvas.yview_scroll(int(-event.delta / 120), "units")


# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------
class NoiseApp:
    def __init__(self, root):
        self.root = root
        root.title("Ambient Noise Generator")
        root.geometry("600x800")
        root.minsize(520, 580)
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

        # Top menu
        menubar = ttk.Frame(root, padding=(10, 8))
        menubar.pack(side="top", fill="x")
        ttk.Button(menubar, text="🎲 Randomize All",
                   command=self.randomize_all).pack(side="left", padx=(0, 5))
        ttk.Button(menubar, text="↺ Reset Defaults",
                   command=self.reset_defaults).pack(side="left")
        self.status = ttk.Label(menubar, text="", foreground="#888")
        self.status.pack(side="right")

        info = []
        info.append("ffmpeg: " + ("imageio-ffmpeg" if FFMPEG_PATH else "NOT FOUND"))
        if AudioSegment is None:
            info.append("pydub: MISSING")
        if not HAS_AUDIO:
            info.append("sounddevice: MISSING")
        if not HAS_SCIPY:
            info.append("scipy: MISSING (basic filters disabled)")
        self.status.config(text="  |  ".join(info))

        ttk.Separator(root, orient="horizontal").pack(side="top", fill="x")

        # Player panel
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

        # Params
        params_outer = ttk.LabelFrame(root, text=" Parameters ", padding=5)
        params_outer.pack(side="top", fill="both", expand=True, padx=10, pady=(5, 10))
        scroll = ScrollFrame(params_outer)
        scroll.pack(fill="both", expand=True)
        p = scroll.inner

        ttk.Label(p, text="— Texture —", foreground="#aaa").pack(anchor="w", pady=(4, 0))

        ttk.Label(p, text="Sound").pack(anchor="w", pady=(10, 0))
        snd_row = ttk.Frame(p)
        snd_row.pack(fill="x")
        self.preset_name = tk.StringVar(value="Ocean Waves")
        ttk.Combobox(snd_row, textvariable=self.preset_name,
                     values=PRESETS, state="readonly"
                     ).pack(side="left", fill="x", expand=True)
        ttk.Button(snd_row, text="🎲", width=3,
                   command=self.randomize_preset).pack(side="left", padx=(5, 0))

        self._slider(p, "Duration (s)", "duration", 10, 600, 60, 0)

        ttk.Separator(p, orient="horizontal").pack(fill="x", pady=(15, 5))
        ttk.Label(p, text="— Shaping —", foreground="#aaa").pack(anchor="w", pady=(4, 0))

        self._slider(p, "Intensity", "intensity", 0.0, 1.0, 0.6, 2)
        self._slider(p, "Speed / Pace", "speed", 0.0, 1.0, 0.5, 2)
        self._slider(p, "Stereo Width", "stereo_width", 0.0, 1.0, 0.6, 2)
        self._slider(p, "Random Seed", "seed", 0, 9999, 42, 0)

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
        self.vars[f"{key}_lbl"].config(text=f"{self.vars[key].get():.{decimals}f}")

    def randomize_slider(self, key):
        m = self.slider_meta[key]
        v = (random.randint(int(m["lo"]), int(m["hi"])) if m["decimals"] == 0
             else round(random.uniform(m["lo"], m["hi"]), m["decimals"]))
        self.vars[key].set(v)
        self._update_label(key, m["decimals"])

    def randomize_preset(self):
        self.preset_name.set(random.choice(PRESETS))

    def randomize_all(self):
        for k in self.slider_meta: self.randomize_slider(k)
        self.randomize_preset()

    def reset_defaults(self):
        for k, v in {"duration": 60, "intensity": 0.6, "speed": 0.5,
                     "stereo_width": 0.6, "seed": 42}.items():
            self.vars[k].set(v)
            self._update_label(k, self.slider_meta[k]["decimals"])
        self.preset_name.set("Ocean Waves")

    def generate_preview(self):
        if self.generating: return
        self.generating = True
        self.gen_btn.config(state="disabled")
        self.export_btn.config(state="disabled")
        self.status.config(text="Generating…")
        self.player.stop()
        self._set_play_state(False)

        params = dict(
            name=self.preset_name.get(),
            duration=self.vars["duration"].get(),
            intensity=self.vars["intensity"].get(),
            speed=self.vars["speed"].get(),
            seed=int(self.vars["seed"].get()),
            stereo_width=self.vars["stereo_width"].get(),
        )

        def worker():
            try:
                data = generate_texture(**params)
                self.root.after(0, lambda: self._on_generated(data))
            except Exception as e:
                msg = str(e)
                import traceback; traceback.print_exc()
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
        if self.player.data is None: return
        new_pos = max(0.0, min(self.player.current_time() + seconds,
                                self.player.duration()))
        was = self.player.playing
        self.player.seek(new_pos)
        if was:
            self.player.pause(); self.player.play()
        self.seek_var.set(new_pos)

    def _set_play_state(self, playing):
        self.play_btn.config(text="⏸ Pause" if playing else "▶ Play")

    def _on_seek_drag(self, val):
        if self._seeking: self.seek_var.set(float(val))

    def _on_seek_release(self, event):
        self._seeking = False
        if self.player.data is not None:
            was = self.player.playing
            self.player.seek(self.seek_var.get())
            if was: self.player.pause(); self.player.play()

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
            initialfile="ambient_noise.mp3",
        )
        if not path: return
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
    if not HAS_SCIPY:
        print("Warning: scipy is not installed — filtering will be disabled.")
    if AudioSegment is None:
        print("Warning: pydub not available — MP3 export disabled.")
    if not HAS_AUDIO:
        print("Warning: sounddevice not available — playback disabled.")
    root = tk.Tk()
    NoiseApp(root)
    root.mainloop()