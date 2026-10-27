#!/usr/bin/env python3
"""
yt-dlp GUI — professional desktop downloader.

Features:
  • Any YouTube (or other yt-dlp-supported) URL
  • Dropdown for quality / format presets (incl. MP3 only)
  • Toggle for time-range cut (OFF = full video by default)
  • Advanced settings panel (format string, output template, extra args)
  • "Update / Install Prerequisites" button:
        - Installs or updates yt-dlp (with default extras) via pip
        - Checks ffmpeg presence and reports status
  • Live log output, progress, cancel button
  • Downloads via the same Python interpreter (no PATH mismatch)
"""

import os
import queue
import shutil
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

APP_TITLE = "yt-dlp Downloader"

# ---- Presets -------------------------------------------------------------
# label -> (format selector, merge container, extra postprocess args)
QUALITY_PRESETS = {
    "Best quality (video+audio) → MP4": ("bv*+ba/b", "mp4", []),
    "Best quality (video+audio) → MKV": ("bv*+ba/b", "mkv", []),
    "Best quality (video+audio) → WebM": ("bv*+ba/b", "webm", []),
    "MP3 only (audio)":                 ("ba/b", None, ["-x", "--audio-format", "mp3"]),
    "Best video only (no audio)":       ("bv*", None, []),
    "Best audio only (original format)":("ba/b", None, []),
    "Up to 1080p → MP4":                ("bv*[height<=1080]+ba/b[height<=1080]", "mp4", []),
    "Up to 720p → MP4":                 ("bv*[height<=720]+ba/b[height<=720]", "mp4", []),
    "Up to 480p → MP4":                 ("bv*[height<=480]+ba/b[height<=480]", "mp4", []),
    "Custom (edit format below)":       (None, None, []),
}

DEFAULT_PRESET = "Best quality (video+audio) → MP4"

# Pip command for install-or-update (includes default extras for best YouTube support)
PIP_YTDLP_CMD = [sys.executable, "-m", "pip", "install", "-U", "yt-dlp[default]"]


class DownloaderApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("880x720")
        self.minsize(760, 620)

        self.proc = None          # yt-dlp download process
        self.updater_proc = None  # pip/update process
        self.proc_lock = threading.Lock()
        self.log_queue = queue.Queue()

        self._build_style()
        self._build_ui()
        self._poll_log()

    # ---- Styling --------------------------------------------------------
    def _build_style(self):
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Header.TLabel", font=("Segoe UI", 16, "bold"))
        style.configure("Sub.TLabel", foreground="#555")
        style.configure("TLabelframe.Label", font=("Segoe UI", 10, "bold"))
        style.configure("Accent.TButton", font=("Segoe UI", 10, "bold"))

    # ---- UI construction ------------------------------------------------
    def _build_ui(self):
        pad = {"padx": 12, "pady": 6}

        # Header
        header = ttk.Frame(self)
        header.pack(fill="x", padx=12, pady=(12, 0))
        ttk.Label(header, text="yt-dlp Downloader", style="Header.TLabel").pack(anchor="w")
        ttk.Label(
            header,
            text="Paste any video URL. Best-quality MP4 by default.",
            style="Sub.TLabel",
        ).pack(anchor="w")

        # URL
        url_frame = ttk.LabelFrame(self, text="Video URL")
        url_frame.pack(fill="x", **pad)
        self.url_var = tk.StringVar()
        url_entry = ttk.Entry(url_frame, textvariable=self.url_var)
        url_entry.pack(side="left", fill="x", expand=True, padx=(10, 6), pady=8)
        ttk.Button(url_frame, text="Paste", command=self._paste_url).pack(
            side="right", padx=(0, 10), pady=8
        )

        # Settings
        settings = ttk.LabelFrame(self, text="Settings")
        settings.pack(fill="x", **pad)
        settings.columnconfigure(1, weight=1)

        ttk.Label(settings, text="Quality / format:").grid(
            row=0, column=0, sticky="w", padx=10, pady=6
        )
        self.preset_var = tk.StringVar(value=DEFAULT_PRESET)
        self.preset_combo = ttk.Combobox(
            settings,
            textvariable=self.preset_var,
            values=list(QUALITY_PRESETS.keys()),
            state="readonly",
        )
        self.preset_combo.grid(row=0, column=1, sticky="ew", padx=(0, 10), pady=6)

        ttk.Label(settings, text="Save to:").grid(
            row=1, column=0, sticky="w", padx=10, pady=6
        )
        out_frame = ttk.Frame(settings)
        out_frame.grid(row=1, column=1, sticky="ew", padx=(0, 10), pady=6)
        self.outdir_var = tk.StringVar(value=os.path.abspath("."))
        ttk.Entry(out_frame, textvariable=self.outdir_var).pack(
            side="left", fill="x", expand=True
        )
        ttk.Button(out_frame, text="Browse…", command=self._browse_outdir).pack(
            side="right", padx=(6, 0)
        )

        # Time-range toggle
        self.cut_var = tk.BooleanVar(value=False)  # full video by default
        ttk.Checkbutton(
            settings,
            text="Cut to time range (off = download full video)",
            variable=self.cut_var,
            command=self._toggle_cut,
        ).grid(row=2, column=0, columnspan=2, sticky="w", padx=10, pady=(4, 0))

        range_frame = ttk.Frame(settings)
        range_frame.grid(row=3, column=1, sticky="ew", padx=(0, 10), pady=(0, 8))
        ttk.Label(range_frame, text="Start:").pack(side="left")
        self.start_var = tk.StringVar(value="0:04")
        self.start_entry = ttk.Entry(range_frame, textvariable=self.start_var, width=10)
        self.start_entry.pack(side="left", padx=(4, 14))
        ttk.Label(range_frame, text="End:").pack(side="left")
        self.end_var = tk.StringVar(value="0:21")
        self.end_entry = ttk.Entry(range_frame, textvariable=self.end_var, width=10)
        self.end_entry.pack(side="left", padx=4)
        ttk.Label(range_frame, text="(e.g. 0:04 or 00:00:04)").pack(side="left", padx=(8, 0))

        # Advanced panel (collapsible)
        self.adv_open = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            settings,
            text="Show advanced options",
            variable=self.adv_open,
            command=self._toggle_advanced,
        ).grid(row=4, column=0, columnspan=2, sticky="w", padx=10, pady=(0, 6))

        self.adv_frame = ttk.Frame(settings)
        self.adv_frame.grid(row=5, column=0, columnspan=2, sticky="ew", padx=10, pady=(0, 8))
        self.adv_frame.columnconfigure(1, weight=1)

        ttk.Label(self.adv_frame, text="Format selector:").grid(
            row=0, column=0, sticky="w", pady=3
        )
        self.format_var = tk.StringVar(value="bv*+ba/b")
        ttk.Entry(self.adv_frame, textvariable=self.format_var).grid(
            row=0, column=1, sticky="ew", padx=(6, 0), pady=3
        )

        ttk.Label(self.adv_frame, text="Extra yt-dlp args:").grid(
            row=1, column=0, sticky="w", pady=3
        )
        self.extra_var = tk.StringVar()
        ttk.Entry(self.adv_frame, textvariable=self.extra_var).grid(
            row=1, column=1, sticky="ew", padx=(6, 0), pady=3
        )

        self.adv_frame.grid_remove()  # hidden until toggled

        # Buttons
        btn_frame = ttk.Frame(self)
        btn_frame.pack(fill="x", **pad)
        self.download_btn = ttk.Button(
            btn_frame, text="Download", style="Accent.TButton",
            command=self._start_download,
        )
        self.download_btn.pack(side="left")
        self.cancel_btn = ttk.Button(
            btn_frame, text="Cancel", command=self._cancel_download, state="disabled"
        )
        self.cancel_btn.pack(side="left", padx=(8, 0))
        ttk.Button(btn_frame, text="Clear log", command=self._clear_log).pack(
            side="right"
        )

        # Update button (separate row, prominent)
        update_frame = ttk.Frame(self)
        update_frame.pack(fill="x", **pad)
        self.update_btn = ttk.Button(
            update_frame,
            text="⬆  Update / Install Prerequisites (yt-dlp + ffmpeg check)",
            command=self._start_update,
        )
        self.update_btn.pack(side="left")

        # Log
        log_frame = ttk.LabelFrame(self, text="Output")
        log_frame.pack(fill="both", expand=True, **pad)
        self.log_text = tk.Text(
            log_frame, wrap="word", height=14,
            font=("Consolas", 9), state="disabled",
        )
        self.log_text.pack(side="left", fill="both", expand=True, padx=(10, 0), pady=8)
        scroll = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        scroll.pack(side="right", fill="y", padx=(0, 10), pady=8)
        self.log_text.configure(yscrollcommand=scroll.set)

        # Status bar
        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(self, textvariable=self.status_var, style="Sub.TLabel").pack(
            fill="x", padx=12, pady=(0, 8)
        )

    # ---- Small helpers --------------------------------------------------
    def _paste_url(self):
        try:
            self.url_var.set(self.clipboard_get().strip())
        except tk.TclError:
            pass

    def _browse_outdir(self):
        d = filedialog.askdirectory(initialdir=self.outdir_var.get() or ".")
        if d:
            self.outdir_var.set(d)

    def _toggle_cut(self):
        state = "normal" if self.cut_var.get() else "disabled"
        self.start_entry.configure(state=state)
        self.end_entry.configure(state=state)

    def _toggle_advanced(self):
        if self.adv_open.get():
            self.adv_frame.grid()
        else:
            self.adv_frame.grid_remove()

    def _clear_log(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def _log(self, text):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text)
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    # ---- Log pumping (thread-safe) --------------------------------------
    def _poll_log(self):
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "text":
                    self._log(payload)
                elif kind == "done_download":
                    self._on_download_done(payload)
                elif kind == "done_update":
                    self._on_update_done(payload)
        except queue.Empty:
            pass
        self.after(100, self._poll_log)

    # ---- Validation -----------------------------------------------------
    def _build_command(self):
        url = self.url_var.get().strip()
        if not url:
            messagebox.showerror(APP_TITLE, "Please enter a video URL.")
            return None

        preset = self.preset_var.get()
        fmt, container, postargs = QUALITY_PRESETS[preset]

        if preset == "Custom (edit format below)":
            fmt = self.format_var.get().strip()
            container = None
            postargs = []

        if not fmt:
            messagebox.showerror(APP_TITLE, "Format selector is empty.")
            return None

        cmd = [sys.executable, "-m", "yt_dlp", "-f", fmt]

        if container:
            cmd += ["--merge-output-format", container]

        cmd += postargs

        if self.cut_var.get():
            start = self.start_var.get().strip()
            end = self.end_var.get().strip()
            if not start or not end:
                messagebox.showerror(APP_TITLE, "Please fill in both start and end times.")
                return None
            cmd += ["--download-sections", f"*{start}-{end}",
                    "--force-keyframes-at-cuts"]

        outdir = self.outdir_var.get().strip()
        if outdir:
            cmd += ["-P", outdir]

        extra = self.extra_var.get().strip()
        if extra:
            import shlex
            cmd += shlex.split(extra)

        cmd.append(url)
        return cmd

    # ---- Update / Install flow ------------------------------------------
    def _start_update(self):
        if self.updater_proc is not None:
            return  # already running

        self._clear_log()
        self.update_btn.configure(state="disabled")
        self.download_btn.configure(state="disabled")
        self.status_var.set("Updating / installing prerequisites…")

        self._log("=== Update / Install Prerequisites ===\n\n")
        self._log(f"Python interpreter: {sys.executable}\n\n")
        self._log(f"Step 1: Install / upgrade yt-dlp with default extras\n")
        self._log(f"  Command: {' '.join(PIP_YTDLP_CMD)}\n\n")

        threading.Thread(target=self._run_update, daemon=True).start()

    def _run_update(self):
        code = -1
        try:
            with self.proc_lock:
                self.updater_proc = subprocess.Popen(
                    PIP_YTDLP_CMD,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    encoding="utf-8",
                    errors="replace",
                )
            assert self.updater_proc.stdout is not None
            for line in self.updater_proc.stdout:
                self.log_queue.put(("text", line))
            code = self.updater_proc.wait()
        except Exception as e:
            self.log_queue.put(("text", f"\n[ERROR] {e}\n"))
            code = -1
        finally:
            with self.proc_lock:
                self.updater_proc = None

        # After pip finishes, check ffmpeg
        self.log_queue.put(("text", "\n\n=== ffmpeg check ===\n"))
        ffmpeg_path = shutil.which("ffmpeg")
        if ffmpeg_path:
            try:
                ver = subprocess.check_output(
                    ["ffmpeg", "-version"], text=True, stderr=subprocess.STDOUT
                ).splitlines()[0]
                self.log_queue.put(("text", f"✓ ffmpeg found: {ffmpeg_path}\n  {ver}\n"))
            except Exception:
                self.log_queue.put(("text", f"✓ ffmpeg found: {ffmpeg_path}\n"))
        else:
            self.log_queue.put((
                "text",
                "✗ ffmpeg NOT found on PATH.\n"
                "  MP3 extraction and high-quality merging require ffmpeg.\n"
                "  Install it:\n"
                "    Windows: winget install Gyan.FFmpeg\n"
                "    macOS:   brew install ffmpeg\n"
                "    Linux:   sudo apt install ffmpeg\n"
            ))

        self.log_queue.put(("done_update", code))

    def _on_update_done(self, code):
        self.update_btn.configure(state="normal")
        self.download_btn.configure(state="normal")

        # Final import check
        try:
            import yt_dlp
            ver = getattr(yt_dlp.version, "__version__", "unknown")
            self._log(f"\nyt-dlp import check: OK (version {ver})\n")
            import_ok = True
        except Exception as e:
            self._log(f"\nyt-dlp import check: FAILED ({e})\n")
            import_ok = False

        ffmpeg_ok = shutil.which("ffmpeg") is not None

        if code == 0 and import_ok:
            self.status_var.set("Update finished.")
            self._log("\n[Update finished successfully]\n")
            if not ffmpeg_ok:
                self._log(
                    "\nNOTE: ffmpeg is still missing. "
                    "MP3 conversion and best-quality merging will not work until it is installed.\n"
                )
        else:
            self.status_var.set("Update failed — see log.")
            self._log(f"\n[Update process exited with code {code}]\n")

    # ---- Download flow --------------------------------------------------
    def _start_download(self):
        if self.proc is not None or self.updater_proc is not None:
            return

        if shutil.which("ffmpeg") is None:
            messagebox.showerror(
                APP_TITLE,
                "ffmpeg not found on PATH.\n\n"
                "ffmpeg is required for merging high-quality streams and for MP3 conversion.\n\n"
                "Install it:\n"
                "  Windows: winget install Gyan.FFmpeg\n"
                "  macOS:   brew install ffmpeg\n"
                "  Linux:   sudo apt install ffmpeg",
            )
            return

        try:
            import yt_dlp  # noqa: F401
        except ImportError:
            messagebox.showerror(
                APP_TITLE,
                f"yt-dlp is not installed for this Python interpreter.\n\n"
                f"Use the Update button, or run:\n"
                f"  {sys.executable} -m pip install -U \"yt-dlp[default]\"",
            )
            return

        cmd = self._build_command()
        if not cmd:
            return

        self._clear_log()
        self._log("> " + " ".join(f'"{c}"' if " " in c else c for c in cmd) + "\n\n")

        self.download_btn.configure(state="disabled")
        self.update_btn.configure(state="disabled")
        self.cancel_btn.configure(state="normal")
        self.status_var.set("Downloading…")

        threading.Thread(target=self._run_download, args=(cmd,), daemon=True).start()

    def _run_download(self, cmd):
        try:
            with self.proc_lock:
                self.proc = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    encoding="utf-8",
                    errors="replace",
                )
            assert self.proc.stdout is not None
            for line in self.proc.stdout:
                self.log_queue.put(("text", line))
            code = self.proc.wait()
        except Exception as e:
            self.log_queue.put(("text", f"\n[ERROR] {e}\n"))
            code = -1
        finally:
            with self.proc_lock:
                self.proc = None
        self.log_queue.put(("done_download", code))

    def _on_download_done(self, code):
        self.download_btn.configure(state="normal")
        self.update_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        if code == 0:
            self.status_var.set("Done.")
            self._log("\n[Finished successfully]\n")
        elif code == -1:
            self.status_var.set("Failed.")
        else:
            self.status_var.set(f"Exited with code {code}.")
            self._log(f"\n[Process exited with code {code}]\n")

    def _cancel_download(self):
        with self.proc_lock:
            proc = self.proc
        if proc and proc.poll() is None:
            try:
                proc.terminate()
                self.status_var.set("Cancelling…")
            except Exception:
                pass


def main():
    app = DownloaderApp()
    app.mainloop()


if __name__ == "__main__":
    main()