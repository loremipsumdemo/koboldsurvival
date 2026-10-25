import os
import warnings
import threading
import tempfile

import numpy as np
import soundfile as sf
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

try:
    import sounddevice as sd
    HAS_AUDIO = True
except Exception:
    sd = None
    HAS_AUDIO = False

# ffmpeg setup
FFMPEG_PATH = None
try:
    import imageio_ffmpeg
    FFMPEG_PATH = imageio_ffmpeg.get_ffmpeg_exe()
    if FFMPEG_PATH and os.path.exists(FFMPEG_PATH):
        _ffdir = os.path.dirname(FFMPEG_PATH)
        os.environ["PATH"] = _ffdir + os.pathsep + os.environ.get("PATH", "")
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


SAMPLE_RATE = 44100
TRACK_COLORS = [
    "#5b8def", "#e07a5f", "#81b29a", "#f2cc8f", "#9b6bcc",
    "#4ea8de", "#f28482", "#a7c957", "#c77dff", "#ffb703",
]

# Pixels of edge "grab" zone for trimming
EDGE_GRAB_PX = 8


def load_audio_file(path):
    if AudioSegment is None:
        raise RuntimeError("pydub is not available")
    seg = AudioSegment.from_file(path)
    seg = seg.set_frame_rate(SAMPLE_RATE).set_channels(1)
    samples = np.array(seg.get_array_of_samples(), dtype=np.float32)
    if seg.sample_width == 2:
        samples /= 32768.0
    elif seg.sample_width == 1:
        samples = (samples - 128.0) / 128.0
    elif seg.sample_width == 4:
        samples /= 2147483648.0
    return samples


def get_visible_samples(track):
    """Extract the audible slice of a track."""
    s = int(round(track["trim_start"] * SAMPLE_RATE))
    e = int(round(track["trim_end"] * SAMPLE_RATE))
    return track["source_samples"][s:e]


def mix_tracks(tracks, total_length_samples=None):
    """Mix all audible tracks into a stereo float32 array."""
    audible = [t for t in tracks
               if (t["solo"] or (not t["mute"] and not any(o["solo"] for o in tracks)))]
    if not audible:
        if total_length_samples is None:
            return np.zeros((SAMPLE_RATE, 2), dtype=np.float32)
        return np.zeros((total_length_samples, 2), dtype=np.float32)

    end_samples = 0
    for t in audible:
        s = int(round(t["start"] * SAMPLE_RATE))
        visible = get_visible_samples(t)
        e = s + len(visible)
        if e > end_samples:
            end_samples = e
    if total_length_samples is not None:
        end_samples = max(end_samples, int(total_length_samples))

    mono = np.zeros(end_samples, dtype=np.float32)
    for t in audible:
        s = int(round(t["start"] * SAMPLE_RATE))
        visible = get_visible_samples(t)
        e = min(s + len(visible), end_samples)
        if e > s:
            mono[s:e] += visible[: e - s] * t["volume"]

    peak = float(np.max(np.abs(mono)))
    if peak > 0.95:
        mono = mono / peak * 0.95

    delay = int(0.008 * SAMPLE_RATE)
    left = mono
    right = np.concatenate([np.zeros(delay, dtype=np.float32), mono[:-delay]])
    stereo = np.stack([left, right], axis=1)
    return stereo.astype(np.float32)


def export_mp3(stereo, path):
    if AudioSegment is None:
        raise RuntimeError("pydub not available")
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    try:
        sf.write(tmp.name, stereo, SAMPLE_RATE, subtype="PCM_16")
        audio = AudioSegment.from_wav(tmp.name)
        audio.export(path, format="mp3", bitrate="192k")
    finally:
        try:
            os.unlink(tmp.name)
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


class TimelineCanvas(tk.Canvas):
    LABEL_W = 150
    RULER_H = 24
    TRACK_H = 60
    TRACK_GAP = 4

    def __init__(self, parent, app, **kwargs):
        super().__init__(parent, bg="#161622", highlightthickness=0, **kwargs)
        self.app = app
        self._drag_track = None
        self._drag_mode = None          # "move" | "trim_left" | "trim_right"
        self._drag_offset_px = 0
        self._orig_start = 0.0
        self._orig_trim_start = 0.0
        self._orig_trim_end = 0.0

        self.bind("<Configure>", lambda e: self.redraw())
        self.bind("<ButtonPress-1>", self._on_press)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<ButtonRelease-1>", self._on_release)
        self.bind("<Motion>", self._on_hover)

    # ---- coord helpers ----
    def _timeline_width(self):
        return max(1, self.winfo_width() - self.LABEL_W - 10)

    def _seconds_per_px(self):
        dur = self.app.timeline_duration()
        if dur <= 0:
            return 1.0
        return dur / self._timeline_width()

    def _px_per_second(self):
        return 1.0 / self._seconds_per_px()

    def _time_to_x(self, seconds):
        return self.LABEL_W + int(seconds * self._px_per_second())

    def _x_to_time(self, x):
        return max(0.0, (x - self.LABEL_W) * self._seconds_per_px())

    def _track_y(self, idx):
        return self.RULER_H + idx * (self.TRACK_H + self.TRACK_GAP)

    def _y_to_track(self, y):
        if y < self.RULER_H:
            return None
        rel = y - self.RULER_H
        idx = rel // (self.TRACK_H + self.TRACK_GAP)
        if idx < 0 or idx >= len(self.app.tracks):
            return None
        return int(idx)

    # ---- drawing ----
    def redraw(self):
        self.delete("all")
        w = self.winfo_width()
        h = self.winfo_height()
        if w < 10 or h < 10:
            return
        self.create_rectangle(0, 0, w, self.RULER_H, fill="#22222f", outline="")
        self._draw_ruler(w)
        self.create_rectangle(0, self.RULER_H, self.LABEL_W, h,
                              fill="#1a1a26", outline="")
        for i, t in enumerate(self.app.tracks):
            self._draw_track(i, t)

        if self.app.player.data is not None:
            playhead_x = self._time_to_x(self.app.player.current_time())
            if self.LABEL_W <= playhead_x <= w:
                self.create_line(playhead_x, 0, playhead_x, h,
                                 fill="#ff5c5c", width=2)

        if not self.app.tracks:
            self.create_text(w // 2, h // 2,
                             text="Click 'Add Track…' to import audio files",
                             fill="#666", font=("Segoe UI", 11))

    def _draw_ruler(self, w):
        dur = self.app.timeline_duration()
        if dur <= 0:
            return
        candidates = [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300]
        target_ticks = 10
        tick = next((c for c in candidates if dur / c <= target_ticks * 1.5),
                    candidates[-1])
        t = 0.0
        while t <= dur:
            x = self._time_to_x(t)
            if x > w:
                break
            self.create_line(x, self.RULER_H - 6, x, self.RULER_H, fill="#555")
            self.create_text(x + 3, 4, text=self._fmt(t), anchor="nw",
                             fill="#888", font=("Segoe UI", 8))
            t += tick

    def _draw_track(self, idx, track):
        y = self._track_y(idx)
        h = self.TRACK_H
        w = self.winfo_width()
        color = track["color"]

        self.create_rectangle(self.LABEL_W, y, w, y + h,
                              fill="#1f1f2e", outline="#2a2a3e")

        self.create_rectangle(0, y, self.LABEL_W - 1, y + h,
                              fill=color, outline="")
        name = track["name"]
        if len(name) > 20:
            name = name[:18] + "…"
        self.create_text(8, y + 8, text=name, anchor="nw",
                         fill="#0e0e18", font=("Segoe UI", 9, "bold"))
        dur_visible = track["trim_end"] - track["trim_start"]
        info = f"{dur_visible:.1f}s · start {track['start']:.2f}s"
        self.create_text(8, y + 26, text=info, anchor="nw",
                         fill="#0e0e18", font=("Segoe UI", 8))

        if track["waveform"] is not None:
            start_x = self._time_to_x(track["start"])
            end_x = self._time_to_x(track["start"] + dur_visible)
            visible_left = max(start_x, self.LABEL_W)
            visible_right = min(end_x, w)
            if visible_right > visible_left:
                self.create_rectangle(start_x, y + 2, end_x, y + h - 2,
                                      outline=color, width=2 if idx == self._drag_track else 1)
                self._draw_waveform(track, start_x, end_x, y, h)

                # Trim handles (visual only)
                handle_h = h - 4
                for edge_x, side in ((start_x, "left"), (end_x, "right")):
                    if self.LABEL_W <= edge_x <= w:
                        self.create_rectangle(
                            edge_x - 3, y + 4, edge_x + 3, y + 4 + handle_h,
                            fill=color, outline="#0e0e18")

    def _draw_waveform(self, track, start_x, end_x, y, h):
        # Waveform is computed over the SOURCE; here we slice to the visible window
        src_total = track["duration"]
        wf = track["waveform"]
        n = len(wf)
        if n == 0 or end_x <= start_x or src_total <= 0:
            return

        # Fraction of the source covered by trim
        f0 = track["trim_start"] / src_total
        f1 = track["trim_end"] / src_total
        i0 = int(f0 * n)
        i1 = int(f1 * n)
        if i1 <= i0:
            i1 = i0 + 1
        slice_wf = wf[i0:i1]
        n_visible = len(slice_wf)
        if n_visible == 0:
            return

        mid_y = y + h / 2
        amp = (h - 8) / 2
        color = track["color"]

        span_px = end_x - start_x
        if span_px < 2:
            return
        px = max(1, int(span_px))
        points_top = []
        points_bot = []

        for i in range(px):
            b0 = int(i / px * n_visible)
            b1 = int((i + 1) / px * n_visible)
            if b1 <= b0:
                b1 = b0 + 1
            chunk = slice_wf[b0:b1]
            if not chunk:
                continue
            lo = min(c[0] for c in chunk)
            hi = max(c[1] for c in chunk)
            x = start_x + i
            points_top.append((x, mid_y - hi * amp))
            points_bot.append((x, mid_y - lo * amp))

        if not points_top:
            return
        poly = points_top + list(reversed(points_bot))
        flat = [coord for p in poly for coord in p]
        self.create_polygon(flat, fill=color, outline="")

    def _fmt(self, sec):
        m, s = divmod(int(sec), 60)
        return f"{m}:{s:02d}" if m else f"{int(sec)}s"

    # ---- interaction ----
    def _hit_test(self, x, y):
        """Return (track_idx, mode) for the pixel under (x, y)."""
        if x < self.LABEL_W:
            return None, None
        idx = self._y_to_track(y)
        if idx is None:
            return None, None
        t = self.app.tracks[idx]
        start_x = self._time_to_x(t["start"])
        end_x = self._time_to_x(t["start"] + (t["trim_end"] - t["trim_start"]))
        if abs(x - start_x) <= EDGE_GRAB_PX:
            return idx, "trim_left"
        if abs(x - end_x) <= EDGE_GRAB_PX:
            return idx, "trim_right"
        if start_x < x < end_x:
            return idx, "move"
        return None, None

    def _on_hover(self, event):
        idx, mode = self._hit_test(event.x, event.y)
        if mode in ("trim_left", "trim_right"):
            self.config(cursor="sb_h_double_arrow")
        elif mode == "move":
            self.config(cursor="fleur")
        else:
            self.config(cursor="")

    def _on_press(self, event):
        idx, mode = self._hit_test(event.x, event.y)
        if idx is None:
            return
        t = self.app.tracks[idx]
        self._drag_track = idx
        self._drag_mode = mode
        self._orig_start = t["start"]
        self._orig_trim_start = t["trim_start"]
        self._orig_trim_end = t["trim_end"]
        self._drag_offset_px = event.x - self._time_to_x(t["start"])
        self.redraw()

    def _on_drag(self, event):
        if self._drag_track is None:
            return
        t = self.app.tracks[self._drag_track]
        if self._drag_mode == "move":
            new_x = event.x - self._drag_offset_px
            new_start = max(0.0, self._x_to_time(new_x))
            t["start"] = round(new_start, 3)
        elif self._drag_mode == "trim_left":
            mouse_time = self._x_to_time(event.x)
            delta = mouse_time - (self._orig_start)
            new_start = max(0.0, self._orig_start + delta)
            # Clamp so we don't push trim past end
            new_trim_start = self._orig_trim_start + (new_start - self._orig_start)
            new_trim_start = max(0.0, new_trim_start)
            if new_trim_start < self._orig_trim_end - 0.05:
                t["start"] = round(new_start, 3)
                t["trim_start"] = round(new_trim_start, 3)
        elif self._drag_mode == "trim_right":
            mouse_time = self._x_to_time(event.x)
            delta = mouse_time - (self._orig_start + (self._orig_trim_end - self._orig_trim_start))
            new_trim_end = self._orig_trim_end + delta
            new_trim_end = min(t["duration"], new_trim_end)
            if new_trim_end > self._orig_trim_start + 0.05:
                t["trim_end"] = round(new_trim_end, 3)
        if "start_var" in t:
            t["start_var"].set(t["start"])
        self.redraw()

    def _on_release(self, event):
        if self._drag_track is not None:
            self.app._schedule_rebuild()
        self._drag_track = None
        self._drag_mode = None
        self.config(cursor="")
        self.redraw()


class AudioEditorApp:
    def __init__(self, root):
        self.root = root
        root.title("Multi-Track Audio Editor")
        root.geometry("1100x720")
        root.minsize(900, 560)
        root.configure(bg="#1e1e2e")

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass

        self.player = Player()
        self.tracks = []
        self._next_color = 0
        self._loading_count = 0
        self._seek_var = tk.DoubleVar(value=0)
        self._seeking = False
        self._rebuild_job = None

        bar = ttk.Frame(root, padding=(10, 8))
        bar.pack(side="top", fill="x")
        ttk.Button(bar, text="+ Add Track…", command=self.add_track).pack(side="left", padx=(0, 5))
        ttk.Button(bar, text="Clear All", command=self.clear_all).pack(side="left", padx=(0, 20))

        self.status = ttk.Label(bar, text="", foreground="#888")
        self.status.pack(side="right")
        info = ["ffmpeg: " + ("imageio-ffmpeg" if FFMPEG_PATH else "NOT FOUND")]
        if AudioSegment is None:
            info.append("pydub: MISSING")
        if not HAS_AUDIO:
            info.append("sounddevice: MISSING")
        self.status.config(text="  |  ".join(info))

        ttk.Separator(root, orient="horizontal").pack(side="top", fill="x")

        top = ttk.LabelFrame(root, text=" Transport ", padding=8)
        top.pack(side="top", fill="x", padx=10, pady=(10, 5))

        row1 = ttk.Frame(top)
        row1.pack(fill="x")
        ttk.Button(row1, text="⏮ -5s", width=8,
                   command=lambda: self.nudge(-5)).pack(side="left", padx=2)
        self.play_btn = ttk.Button(row1, text="▶ Play", width=9,
                                   command=self.toggle_play)
        self.play_btn.pack(side="left", padx=2)
        ttk.Button(row1, text="⏹ Stop", width=8,
                   command=self.stop_playback).pack(side="left", padx=2)
        ttk.Button(row1, text="+5s ⏭", width=8,
                   command=lambda: self.nudge(5)).pack(side="left", padx=2)

        ttk.Label(row1, text="   ").pack(side="left")
        ttk.Button(row1, text="🔄 Rebuild Mix", command=self.rebuild_mix).pack(side="left", padx=2)
        ttk.Button(row1, text="📤 Export MP3…", command=self.export_mp3).pack(side="left", padx=2)

        self.time_lbl = ttk.Label(row1, text="00:00 / 00:00")
        self.time_lbl.pack(side="right", padx=8)

        row2 = ttk.Frame(top)
        row2.pack(fill="x", pady=(8, 0))
        self.seek_scale = ttk.Scale(row2, from_=0, to=1, orient="horizontal",
                                    variable=self._seek_var,
                                    command=self._on_seek_drag)
        self.seek_scale.pack(fill="x")
        self.seek_scale.bind("<ButtonPress-1>",
                             lambda e: setattr(self, "_seeking", True))
        self.seek_scale.bind("<ButtonRelease-1>", self._on_seek_release)

        tl_frame = ttk.LabelFrame(root, text=" Timeline ", padding=4)
        tl_frame.pack(side="top", fill="both", expand=True, padx=10, pady=(5, 5))

        self.canvas = TimelineCanvas(tl_frame, self)
        self.canvas.pack(fill="both", expand=True)

        ctrl_outer = ttk.LabelFrame(root, text=" Tracks ", padding=4)
        ctrl_outer.pack(side="top", fill="x", padx=10, pady=(0, 10))

        self.ctrl_frame = ttk.Frame(ctrl_outer)
        self.ctrl_frame.pack(fill="x")

        self.root.after(80, self._tick)

    def timeline_duration(self):
        if not self.tracks:
            return 30.0
        return max(t["start"] + (t["trim_end"] - t["trim_start"]) for t in self.tracks) + 1.0

    def _next_track_color(self):
        c = TRACK_COLORS[self._next_color % len(TRACK_COLORS)]
        self._next_color += 1
        return c

    def _compute_waveform(self, samples, buckets=1200):
        n = len(samples)
        if n == 0:
            return []
        buckets = min(buckets, n)
        chunk = max(1, n // buckets)
        wf = []
        for i in range(0, n, chunk):
            seg = samples[i:i + chunk]
            if len(seg) == 0:
                continue
            wf.append((float(np.min(seg)), float(np.max(seg))))
        return wf

    def add_track(self):
        paths = filedialog.askopenfilenames(
            title="Add audio files",
            filetypes=[("Audio files", "*.mp3 *.wav *.ogg *.flac *.m4a"), ("All files", "*.*")],
        )
        if not paths:
            return
        if AudioSegment is None:
            messagebox.showerror("Missing", "pydub is not installed.")
            return
        for path in paths:
            self._load_file_async(path)

    def _load_file_async(self, path):
        self._loading_count += 1
        self.status.config(text=f"Loading {os.path.basename(path)}…")

        def worker():
            try:
                samples = load_audio_file(path)
                dur = len(samples) / SAMPLE_RATE
                wf = self._compute_waveform(samples, buckets=1200)
                self.root.after(0, lambda: self._add_track_to_state(
                    path, samples, dur, wf))
            except Exception as e:
                msg = str(e)
                self.root.after(0, lambda: messagebox.showerror(
                    "Failed to load", f"{path}\n\n{msg}"))

        threading.Thread(target=worker, daemon=True).start()

    def _add_track_to_state(self, path, samples, duration, waveform):
        color = self._next_track_color()
        track = dict(
            name=os.path.basename(path),
            path=path,
            source_samples=samples,
            duration=duration,
            trim_start=0.0,
            trim_end=duration,
            start=0.0,
            volume=1.0,
            mute=False,
            solo=False,
            color=color,
            waveform=waveform,
        )
        self.tracks.append(track)
        self._build_track_controls()
        self.canvas.redraw()
        self._loading_count -= 1
        if self._loading_count <= 0:
            self.status.config(text=f"{len(self.tracks)} track(s) loaded")
        self.rebuild_mix()

    def _remove_track(self, idx):
        if 0 <= idx < len(self.tracks):
            del self.tracks[idx]
            self._build_track_controls()
            self.canvas.redraw()
            self.rebuild_mix()

    def clear_all(self):
        if not self.tracks:
            return
        if messagebox.askyesno("Clear All", "Remove all tracks?"):
            self.tracks = []
            self._build_track_controls()
            self.canvas.redraw()
            self.player.stop()
            self.player.load(None)
            self._set_play_state(False)
            self._seek_var.set(0)
            self.seek_scale.config(to=1.0)
            self.time_lbl.config(text="00:00 / 00:00")

    def _build_track_controls(self):
        for child in self.ctrl_frame.winfo_children():
            child.destroy()

        if not self.tracks:
            ttk.Label(self.ctrl_frame, text="No tracks yet.",
                      foreground="#666").pack(anchor="w", padx=5, pady=4)
            return

        for idx, t in enumerate(self.tracks):
            row = ttk.Frame(self.ctrl_frame)
            row.pack(fill="x", pady=2)

            swatch = tk.Label(row, bg=t["color"], width=2)
            swatch.pack(side="left", padx=(2, 6))

            name = t["name"]
            if len(name) > 26:
                name = name[:24] + "…"
            ttk.Label(row, text=name, width=28).pack(side="left")

            ttk.Label(row, text="Vol").pack(side="left", padx=(6, 2))
            vol_var = tk.DoubleVar(value=t["volume"])
            t["volume_var"] = vol_var

            def on_vol(v, track=t):
                track["volume"] = float(v)
                self._schedule_rebuild()

            ttk.Scale(row, from_=0, to=2, variable=vol_var, length=100,
                      orient="horizontal", command=on_vol).pack(side="left")

            ttk.Label(row, text="  Start (s)").pack(side="left", padx=(6, 2))
            start_var = tk.DoubleVar(value=t["start"])
            t["start_var"] = start_var

            def on_start(*args, track=t, var=start_var):
                try:
                    track["start"] = max(0.0, float(var.get()))
                except Exception:
                    return
                self.canvas.redraw()
                self._schedule_rebuild()

            start_var.trace_add("write", on_start)
            ttk.Spinbox(row, from_=0, to=9999, increment=0.1, width=7,
                        textvariable=start_var).pack(side="left")

            mute_var = tk.BooleanVar(value=t["mute"])
            t["mute_var"] = mute_var

            def on_mute(track=t, var=mute_var):
                track["mute"] = var.get()
                self._schedule_rebuild()

            ttk.Checkbutton(row, text="Mute", variable=mute_var,
                            command=on_mute).pack(side="left", padx=(8, 2))

            solo_var = tk.BooleanVar(value=t["solo"])
            t["solo_var"] = solo_var

            def on_solo(track=t, var=solo_var):
                track["solo"] = var.get()
                self._schedule_rebuild()

            ttk.Checkbutton(row, text="Solo", variable=solo_var,
                            command=on_solo).pack(side="left", padx=(2, 6))

            ttk.Button(row, text="✕", width=3,
                       command=lambda i=idx: self._remove_track(i)
                       ).pack(side="right", padx=2)

    def _schedule_rebuild(self):
        if self._rebuild_job:
            try:
                self.root.after_cancel(self._rebuild_job)
            except Exception:
                pass
        self._rebuild_job = self.root.after(150, self.rebuild_mix)

    def rebuild_mix(self):
        self._rebuild_job = None
        if not self.tracks:
            self.player.load(None)
            self._set_play_state(False)
            self.seek_scale.config(to=1.0)
            self._seek_var.set(0)
            self.time_lbl.config(text="00:00 / 00:00")
            self.canvas.redraw()
            return
        try:
            mixed = mix_tracks(self.tracks)
        except Exception as e:
            messagebox.showerror("Mix error", str(e))
            return

        was_playing = self.player.playing
        cur = self.player.current_time()
        self.player.load(mixed)
        self.player.seek(min(cur, self.player.duration()))
        if was_playing:
            self.player.play()
            self._set_play_state(True)
        dur = self.player.duration()
        self.seek_scale.config(to=max(0.1, dur))
        self.canvas.redraw()

    def toggle_play(self):
        if self.player.data is None:
            self.rebuild_mix()
        if self.player.data is None:
            messagebox.showinfo("No audio", "Add a track first.")
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
        self._seek_var.set(0)

    def nudge(self, seconds):
        if self.player.data is None:
            return
        new_pos = max(0.0, min(self.player.current_time() + seconds,
                                self.player.duration()))
        was = self.player.playing
        self.player.seek(new_pos)
        if was:
            self.player.pause()
            self.player.play()
        self._seek_var.set(new_pos)

    def _set_play_state(self, playing):
        self.play_btn.config(text="⏸ Pause" if playing else "▶ Play")

    def _on_seek_drag(self, val):
        if self._seeking:
            self._seek_var.set(float(val))

    def _on_seek_release(self, event):
        self._seeking = False
        if self.player.data is not None:
            was = self.player.playing
            self.player.seek(self._seek_var.get())
            if was:
                self.player.pause()
                self.player.play()

    def _tick(self):
        if self.player.data is not None and not self._seeking:
            cur = self.player.current_time()
            total = self.player.duration()
            self._seek_var.set(cur)
            self.time_lbl.config(text=f"{self._fmt(cur)} / {self._fmt(total)}")
            self.canvas.redraw()
            if self.player.playing and cur >= total - 0.05:
                self._set_play_state(False)
        self.root.after(120, self._tick)

    @staticmethod
    def _fmt(sec):
        m, s = divmod(int(sec), 60)
        return f"{m:02d}:{s:02d}"

    def export_mp3(self):
        if not self.tracks:
            messagebox.showinfo("Nothing to export", "Add at least one track.")
            return
        if AudioSegment is None:
            messagebox.showerror("Missing", "pydub is not installed.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".mp3",
            filetypes=[("MP3 Audio", "*.mp3")],
            initialfile="mix.mp3",
        )
        if not path:
            return

        self.status.config(text="Rendering mix…")
        tracks_copy = [dict(t) for t in self.tracks]

        def worker():
            try:
                mixed = mix_tracks(tracks_copy)
                self.root.after(0, lambda: self.status.config(text="Encoding MP3…"))
                export_mp3(mixed, path)
                self.root.after(0, lambda: self.status.config(text=f"Saved: {path}"))
            except Exception as e:
                msg = str(e)
                self.root.after(0, lambda: messagebox.showerror("Export failed", msg))
                self.root.after(0, lambda: self.status.config(text="Export failed"))

        threading.Thread(target=worker, daemon=True).start()


if __name__ == "__main__":
    if AudioSegment is None:
        print("Warning: pydub not available — import and export disabled.")
    if not HAS_AUDIO:
        print("Warning: sounddevice not available — playback disabled.")
    root = tk.Tk()
    AudioEditorApp(root)
    root.mainloop()