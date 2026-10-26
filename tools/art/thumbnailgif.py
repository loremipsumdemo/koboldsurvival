#!/usr/bin/env python3
"""
bordergif.py - Outline a GIF with up to 3 layered colored edges (overlaid, not added).

GUI toggles:
  * Pre-resize to 315x250 (fill + center-crop) before outlining
  * Auto-compress output under 3 MB (requires gifsicle on PATH)

The outline is drawn *inside* the canvas, so the dimensions are not grown.
Requires: Pillow (pip install Pillow)
Optional: gifsicle (for the compression toggle)
"""

import os
import subprocess
import tkinter as tk
from tkinter import ttk, filedialog, colorchooser, messagebox

from PIL import Image, ImageSequence, ImageTk


TARGET_W, TARGET_H = 315, 250
TARGET_BYTES = 3 * 1024 * 1024


# ---------- core processing ----------

def fit_to_size(img, target_w, target_h):
    """Scale to fill target (aspect preserved), then center-crop to exact size."""
    sw, sh = img.size
    scale = max(target_w / sw, target_h / sh)
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    resized = img.resize((nw, nh), Image.LANCZOS)
    left = (nw - target_w) // 2
    top = (nh - target_h) // 2
    return resized.crop((left, top, left + target_w, top + target_h))


def outline_frame(frame, layers, pre_resize=False):
    """
    Draw layered outlines *inside* the canvas. Content inset by total thickness.
    layers: list of (rgb_tuple, thickness) outermost-first.
    """
    rgba = frame.convert("RGBA")
    if pre_resize:
        rgba = fit_to_size(rgba, TARGET_W, TARGET_H)

    w, h = rgba.size
    total = sum(t for _, t in layers)

    if total <= 0:
        return rgba

    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))

    inset = 0
    for color, thickness in layers:
        if thickness <= 0:
            continue
        lw, lh = max(1, w - inset * 2), max(1, h - inset * 2)
        rect = Image.new("RGBA", (lw, lh), color + (255,))
        canvas.paste(rect, (inset, inset), rect)
        inset += thickness

    # Paste the source content inside the innermost opening
    if w - total * 2 > 0 and h - total * 2 > 0:
        content = rgba.crop((total, total, w - total, h - total))
        canvas.paste(content, (total, total), content)

    return canvas


def process_gif(input_path, output_path, layers, pre_resize=False):
    with Image.open(input_path) as im:
        frames, durations = [], []
        loop = im.info.get("loop", 0)
        for frame in ImageSequence.Iterator(im):
            frames.append(outline_frame(frame, layers, pre_resize=pre_resize))
            durations.append(frame.info.get("duration", im.info.get("duration", 100)))

        frames[0].save(
            output_path,
            save_all=True,
            append_images=frames[1:],
            duration=durations,
            loop=loop,
            disposal=2,
            optimize=False,
        )
    return len(frames)


# ---------- compression ----------

def gifsicle_available():
    try:
        subprocess.run(["gifsicle", "--version"], capture_output=True, check=True)
        return True
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False


def compress_gif_to_target(input_path, output_path, target_bytes=TARGET_BYTES):
    """
    Iteratively compress using gifsicle. Overwrites output_path each attempt.
    Returns (success: bool, final_size: int, attempts_used: int).
    """
    # (lossy, colors) from mild -> extreme; last entry includes --scale
    attempts = [
        (0,   256, None),
        (40,  256, None),
        (80,  128, None),
        (120, 128, None),
        (150, 64,  None),
        (180, 64,  None),
        (200, 32,  None),
        (200, 32,  0.7),
        (200, 16,  0.5),
    ]

    last_size = -1
    for i, (lossy, colors, scale) in enumerate(attempts, start=1):
        cmd = ["gifsicle", "-O3", f"--lossy={lossy}", f"--colors={colors}"]
        if scale is not None:
            cmd.append(f"--scale={scale}")
        cmd += [input_path, "-o", output_path]

        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError:
            continue

        last_size = os.path.getsize(output_path)
        if last_size <= target_bytes:
            return True, last_size, i

    return False, last_size, len(attempts)


# ---------- GUI ----------

class BorderGifApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("bordergif - layered outline for GIFs")
        self.geometry("900x760")

        self.input_path = None
        self.frames = []
        self.durations = []
        self.preview_index = 0
        self.preview_job = None

        self.pre_resize_var = tk.BooleanVar(value=False)
        self.compress_var = tk.BooleanVar(value=False)

        self.layer_vars = []
        for default_color, default_thick in [
            ((255, 170, 30), 4),
            ((255, 120, 0), 3),
            ((120, 40, 0), 2),
        ]:
            color_var = tk.StringVar(value="#%02x%02x%02x" % default_color)
            thick_var = tk.IntVar(value=default_thick)
            self.layer_vars.append((color_var, thick_var))

        self._build_ui()

    def _build_ui(self):
        top = ttk.Frame(self, padding=8)
        top.pack(fill="x")
        ttk.Button(top, text="Open GIF...", command=self.open_gif).pack(side="left")
        self.path_label = ttk.Label(top, text="(no file selected)")
        self.path_label.pack(side="left", padx=8)

        # Layers
        layers_frame = ttk.LabelFrame(self, text="Outline layers (outer -> inner)", padding=8)
        layers_frame.pack(fill="x", padx=8, pady=6)
        for i, (color_var, thick_var) in enumerate(self.layer_vars, start=1):
            row = ttk.Frame(layers_frame)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=f"Layer {i} color:", width=14).pack(side="left")
            swatch = tk.Label(row, width=4, relief="solid", borderwidth=1, bg=color_var.get())
            swatch.pack(side="left", padx=(0, 6))

            def make_picker(idx=i, cv=color_var, sw=swatch):
                def pick():
                    c = colorchooser.askcolor(color=cv.get(), title=f"Layer {idx} color")[1]
                    if c:
                        cv.set(c); sw.config(bg=c); self.schedule_preview_update()
                return pick

            ttk.Button(row, text="Pick...", command=make_picker()).pack(side="left")
            ttk.Label(row, text="Thickness:").pack(side="left", padx=(12, 4))
            spin = ttk.Spinbox(row, from_=0, to=200, width=6,
                               textvariable=thick_var, command=self.schedule_preview_update)
            spin.pack(side="left")
            spin.bind("<KeyRelease>", lambda e: self.schedule_preview_update())

        # Options
        opts = ttk.LabelFrame(self, text="Options", padding=8)
        opts.pack(fill="x", padx=8, pady=6)
        ttk.Checkbutton(opts,
                        text=f"Pre-resize to {TARGET_W}x{TARGET_H} (fill & center-crop)",
                        variable=self.pre_resize_var,
                        command=self.schedule_preview_update).pack(anchor="w")
        ttk.Checkbutton(opts,
                        text=f"Compress output to under {TARGET_BYTES // (1024*1024)} MB "
                             f"(requires gifsicle on PATH)",
                        variable=self.compress_var).pack(anchor="w", pady=(4, 0))

        # Preview
        self.preview_frame = ttk.LabelFrame(self, text="Preview", padding=8)
        self.preview_frame.pack(fill="both", expand=True, padx=8, pady=6)
        self.preview_label = ttk.Label(self.preview_frame,
                                       text="Open a GIF to preview", anchor="center")
        self.preview_label.pack(fill="both", expand=True)

        bottom = ttk.Frame(self, padding=8)
        bottom.pack(fill="x")
        ttk.Button(bottom, text="Save As...", command=self.save_gif).pack(side="right")

    # helpers
    def get_layers(self):
        layers = []
        for color_var, thick_var in self.layer_vars:
            try:
                thick = int(thick_var.get())
            except (tk.TclError, ValueError):
                thick = 0
            c = color_var.get().lstrip("#")
            if len(c) != 6:
                continue
            rgb = tuple(int(c[i:i+2], 16) for i in (0, 2, 4))
            layers.append((rgb, thick))
        return layers

    def open_gif(self):
        path = filedialog.askopenfilename(
            title="Select GIF",
            filetypes=[("GIF files", "*.gif"), ("All files", "*.*")])
        if not path:
            return
        try:
            with Image.open(path) as im:
                self.frames = [f.convert("RGBA").copy() for f in ImageSequence.Iterator(im)]
                self.durations = [f.info.get("duration", im.info.get("duration", 100))
                                  for f in ImageSequence.Iterator(im)]
            self.input_path = path
            self.path_label.config(
                text=os.path.basename(path) +
                     f"  ({len(self.frames)} frames, "
                     f"{self.frames[0].size[0]}x{self.frames[0].size[1]})")
            self.preview_index = 0
            self.schedule_preview_update(immediate=True)
        except Exception as e:
            messagebox.showerror("Error", f"Could not open GIF:\n{e}")

    def schedule_preview_update(self, immediate=False):
        if self.preview_job:
            self.after_cancel(self.preview_job)
            self.preview_job = None
        if immediate:
            self.update_preview()
        else:
            self.preview_job = self.after(120, self.update_preview)

    def update_preview(self):
        self.preview_job = None
        if not self.frames:
            return
        try:
            src = self.frames[self.preview_index]
            img = outline_frame(src, self.get_layers(),
                                pre_resize=self.pre_resize_var.get())
        except Exception as e:
            self.preview_label.config(text=f"Preview error: {e}", image="")
            return

        area_w = max(1, self.preview_label.winfo_width() - 4)
        area_h = max(1, self.preview_label.winfo_height() - 4)
        if area_w < 10 or area_h < 10:
            area_w, area_h = 600, 400

        disp = img.copy()
        disp.thumbnail((area_w, area_h), Image.LANCZOS)
        bg = Image.new("RGBA", disp.size, (60, 60, 60, 255))
        bg.alpha_composite(disp)

        self._tk_img = ImageTk.PhotoImage(bg)
        self.preview_label.config(image=self._tk_img, text="")

        if len(self.frames) > 1:
            self.preview_index = (self.preview_index + 1) % len(self.frames)
            dur = max(30, self.durations[self.preview_index - 1]
                      if self.preview_index > 0 else self.durations[-1])
            self.preview_job = self.after(dur, self.update_preview)

    def save_gif(self):
        if not self.frames or not self.input_path:
            messagebox.showinfo("No file", "Open a GIF first.")
            return

        do_compress = self.compress_var.get()
        if do_compress and not gifsicle_available():
            if not messagebox.askyesno(
                    "gifsicle not found",
                    "gifsicle is not on PATH, so compression will be skipped.\n\n"
                    "Continue without compression?"):
                return
            do_compress = False

        layers = self.get_layers()
        if not any(t > 0 for _, t in layers):
            if not messagebox.askyesno("No border",
                                       "All thicknesses are 0. Save an outlined copy anyway?"):
                return

        base, ext = os.path.splitext(self.input_path)
        default_out = base + "_outlined" + (ext or ".gif")
        out_path = filedialog.asksaveasfilename(
            title="Save outlined GIF as...",
            defaultextension=".gif",
            initialfile=os.path.basename(default_out),
            filetypes=[("GIF", "*.gif")])
        if not out_path:
            return

        tmp_path = out_path + ".tmp.gif"
        try:
            self.config(cursor="watch")
            self.update_idletasks()

            n = process_gif(self.input_path, tmp_path, layers,
                            pre_resize=self.pre_resize_var.get())

            if do_compress:
                success, final_size, tries = compress_gif_to_target(tmp_path, out_path)
                try:
                    os.remove(tmp_path)
                except OSError:
                    pass
                if success:
                    messagebox.showinfo(
                        "Done",
                        f"Saved {n} frames to:\n{out_path}\n\n"
                        f"Compressed to {final_size/1024:.0f} KB "
                        f"in {tries} attempt(s).")
                else:
                    messagebox.showwarning(
                        "Could not reach 3 MB",
                        f"Saved {n} frames to:\n{out_path}\n\n"
                        f"Smallest achieved: {final_size/1024/1024:.2f} MB "
                        f"after {tries} attempts.\n\n"
                        f"You may need to lower the frame count or resolution.")
            else:
                os.replace(tmp_path, out_path)
                size_mb = os.path.getsize(out_path) / (1024 * 1024)
                messagebox.showinfo(
                    "Done",
                    f"Saved {n} frames to:\n{out_path}\n\n"
                    f"File size: {size_mb:.2f} MB")
        except Exception as e:
            messagebox.showerror("Error", f"Could not save:\n{e}")
            for p in (tmp_path,):
                try: os.remove(p)
                except OSError: pass
        finally:
            self.config(cursor="")


if __name__ == "__main__":
    BorderGifApp().mainloop()