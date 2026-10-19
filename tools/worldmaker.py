"""Worldmaker: generate, edit, walk and export Worldgen Lab worlds from one window.

Tabs
  Generate  choose settings and build a world chunk by chunk
  Edit      shape the map: terrain, climate and ground brushes, undo/redo
  Flora     lay plant cover over the ground that can carry it
  Walk      walk the world in 3D, with the edits and plant cover applied

Files
  .wglb is the world format. Import and Export are under the File menu (Ctrl+O / Ctrl+S).
  An exported file keeps the untouched original next to the edits, so the 3D view can
  apply them on top of the regenerated terrain.

Run:  python3 worldmaker.py [optional path to a .wglb file]
Needs: PyQt6, PyOpenGL, numpy
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import pickle
import random
import re
import sys
import threading
import time
import traceback
from collections import OrderedDict
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, asdict
from pathlib import Path

import numpy as np
from OpenGL import GL as gl
from OpenGL.GLU import gluLookAt, gluPerspective
from PyQt6.QtCore import QObject, QPoint, QPointF, QRectF, QSettings, Qt, QThread, QTimer, pyqtSignal
from PyQt6.QtGui import (
    QAction, QBrush, QColor, QCursor, QFont, QImage, QKeySequence, QPainter, QPen, QPixmap,
    QPolygonF, QShortcut, QSurfaceFormat,
)
from PyQt6.QtOpenGLWidgets import QOpenGLWidget
from PyQt6.QtWidgets import (
    QApplication, QButtonGroup, QCheckBox, QComboBox, QDoubleSpinBox, QFileDialog, QFormLayout, QFrame,
    QGroupBox, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QProgressBar,
    QProgressDialog, QPushButton, QScrollArea, QSizePolicy, QSlider, QSpinBox, QStackedWidget,
    QTabWidget, QVBoxLayout, QWidget,
)

def excepthook(exc_type, exc_value, exc_tb):
    traceback.print_exception(exc_type, exc_value, exc_tb)
    try:
        QMessageBox.critical(
            None, "Unexpected error",
            "".join(traceback.format_exception(exc_type, exc_value, exc_tb)),
        )
    except Exception:
        pass

sys.excepthook = excepthook

def clamp(v, a, b):
    return a if v < a else (b if v > b else v)

def lerp_arr(a, b, t):
    a = np.asarray(a, np.float64); b = np.asarray(b, np.float64)
    return a + (b - a) * t


def lerp(a, b, t):
    return a + (b - a) * t

def mulberry32(a):
    a = int(a) & 0xFFFFFFFF
    def rnd():
        nonlocal a
        a = (a + 0x6D2B79F5) & 0xFFFFFFFF
        t = a
        t = (t ^ (t >> 15)) * (1 | t) & 0xFFFFFFFF
        t = (t ^ (t >> 7)) + t & 0xFFFFFFFF
        t = t ^ ((t >> 14) & 0xFFFFFFFF)
        return (t & 0xFFFFFFFF) / 4294967296.0
    return rnd

def hash2(ix, iy, seed):
    h = (ix * 374761393) ^ (iy * 668265263) ^ (seed * 1274126177)
    h &= 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return (h & 0xFFFFFFFF) / 4294967295.0

def vnoise(x, y, seed):
    x = np.asarray(x, np.float64)
    y = np.asarray(y, np.float64)
    ix = np.floor(x); iy = np.floor(y)
    fx = x - ix; fy = y - iy
    ii = ix.astype(np.int64); jj = iy.astype(np.int64)
    u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
    v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    a = hash2(ii, jj, seed); b = hash2(ii + 1, jj, seed)
    c = hash2(ii, jj + 1, seed); d = hash2(ii + 1, jj + 1, seed)
    t1 = a + (b - a) * u
    t2 = c + (d - c) * u
    return t1 + (t2 - t1) * v

def fbm(x, y, seed, oct=6):
    amp = 0.5; f = 1.0; s = 0.0; n = 0.0
    for i in range(oct):
        s = s + amp * vnoise(x * f, y * f, seed + i * 131)
        n += amp
        amp *= 0.5; f *= 2
    return s / n

def ridged(x, y, seed, oct=6):
    amp = 0.5; f = 1.0; s = 0.0; n = 0.0
    for i in range(oct):
        v = vnoise(x * f, y * f, seed + i * 7919)
        v = 1.0 - np.abs(v * 2 - 1); v = v * v
        s = s + amp * v; n += amp
        amp *= 0.5; f *= 2
    return s / n

# Terrain is inorganic substrate only: what the ground is made of. Water is not a terrain
# type. Any tile below sea level is drawn as water, so lowering or raising land never
# leaves a water or grass tile behind. Flora is a separate layer (see FloraMap).
(SILT, SEABED_SAND, BEACH_SAND, ICE, FROZEN_CLAY, LOAM, CLAY, GRAVEL, DESERT_SAND,
 ARID_SOIL, BEDROCK, SCREE, FLOODPLAIN, DELTA_SILT, CLIFF_ROCK) = range(15)
BIOMES = [
    ("Deep silt",        (92, 86, 78)),
    ("Seabed sand",      (176, 162, 120)),
    ("Beach sand",       (222, 206, 160)),
    ("Ice",              (226, 236, 244)),
    ("Frozen clay",      (150, 156, 158)),
    ("Loam",             (120, 96, 70)),
    ("Clay",             (166, 116, 88)),
    ("Gravel",           (148, 140, 128)),
    ("Desert sand",      (214, 190, 134)),
    ("Arid soil",        (178, 148, 104)),
    ("Bedrock",          (110, 106, 102)),
    ("Scree",            (126, 120, 112)),
    ("Floodplain silt",  (150, 138, 104)),
    ("Delta silt",       (132, 124, 90)),
    ("Cliff rock",       (92, 88, 84)),
]
BIOME_RGB = np.array([b[1] for b in BIOMES], dtype=np.float32)
BIOME_RGB_F = BIOME_RGB.astype(np.float64)

LAND_STOPS = [
    (0.00, (200, 190, 152)),
    (0.05, (132, 162, 102)),
    (0.28, (82, 126, 76)),
    (0.52, (96, 112, 82)),
    (0.72, (130, 120, 106)),
    (0.88, (168, 166, 162)),
    (1.00, (248, 251, 255)),
]
def _stops_arr(stops):
    pos = np.array([s[0] for s in stops], np.float64)
    col = np.array([s[1] for s in stops], np.float64)
    return pos, col

LAND_A = _stops_arr(LAND_STOPS)

def ramp_rgb(arr, t):
    pos, col = arr
    out = np.empty(t.shape + (3,), np.float64)
    for k in range(3):
        out[..., k] = np.interp(t, pos, col[:, k])
    return out

LX, LY, LZ = -0.503, 0.754, -0.422

_NEIGH8 = ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1))
_NDY = np.array([o[0] for o in _NEIGH8], np.int64)
_NDX = np.array([o[1] for o in _NEIGH8], np.int64)

_BLOCK = 4096

RIVER_BASE = 95.0
MAX_OVERVIEW = 4096
OVERVIEW_PX = 48

@dataclass
class Settings:
    method: str = "fbm"
    seed: int = 0
    chunk: int = 256
    num_chunks: int = 10
    sea_percentile: float = 0.55
    shape_strength: float = 1.0
    landmass_count: float = 0.5
    landmass_size: float = 0.5
    coast_rough: float = 0.5
    inlets: float = 0.3
    lakes: float = 0.2
    elongation: float = 0.0
    edge_water: float = 0.0
    frequency: float = 1.0
    relief: float = 1.0
    tectonic: float = 0.0
    land_gamma: float = 1.0
    geology: float = 0.5
    detail: float = 0.5
    mask_strength: float = 0.5
    biome_blend: float = 0.5
    erosion: float = 0.5

    @property
    def world_w(self):
        return self.chunk * self.num_chunks

    @property
    def chunks(self):
        return self.num_chunks

    @property
    def total_chunks(self):
        return self.num_chunks * self.num_chunks

    @property
    def overview_ts(self):
        return int(clamp(OVERVIEW_PX, 1, max(1, MAX_OVERVIEW // self.num_chunks)))

    @property
    def overview_size(self):
        return self.overview_ts * self.num_chunks

    @property
    def preview_r(self):
        return int(clamp(self.chunk // 4, 16, 256))

    def to_dict(self):
        d = asdict(self)
        return d

    @classmethod
    def from_dict(cls, d):
        return cls(**{k: v for k, v in d.items() if k in cls.__dataclass_fields__})

MAGIC = b"WGLB"
VERSION = 2          # plain saves
VERSION_EDITED = 3   # edited saves also carry base_lowres: the untouched original, used for edit diffs
VERSION_FLORA = 4    # saves that also carry flora layers
GENERATOR_VERSION = "5.0"
RESIDUAL_DETAIL = 0.15   # share of sub-grid detail kept after erosion (the rest is smoothed away)
VIEW_MODES = [("biome", "Biome"), ("height", "Height"), ("temperature", "Temperature"), ("moisture", "Moisture")]

def write_world_file(path, settings, overview, chunk_ready, sea_level, river_threshold,
                     done, lowres, base_lowres=None, generator=None, flora=None):
    if flora is not None:
        version = VERSION_FLORA
    elif base_lowres is not None:
        version = VERSION_EDITED
    else:
        version = VERSION
    meta = {
        "magic": MAGIC.decode("ascii"),
        "version": version,
        "settings": settings.to_dict(),
        "sea_level": float(sea_level),
        "river_threshold": river_threshold,
        "done": int(done),
        "generator": generator or GENERATOR_VERSION,
        "edited": base_lowres is not None,
        "flora": flora is not None,
    }
    payload = {
        "meta": meta,
        "overview": overview,
        "chunk_ready": chunk_ready,
        "lowres": lowres,
    }
    if base_lowres is not None:
        payload["base_lowres"] = base_lowres
    if flora is not None:
        payload["flora"] = flora
    with open(path, "wb") as f:
        pickle.dump(payload, f, protocol=pickle.HIGHEST_PROTOCOL)

def save_world(path: Path, settings: Settings, world: "World", base_lowres=None):
    write_world_file(path, settings, world.overview, world.chunk_ready, world.sea_level,
                     world.river_threshold, world.done, world.lowres, base_lowres)

def load_world_full(path: Path):
    with open(path, "rb") as f:
        payload = pickle.load(f)
    meta = payload["meta"]
    if meta.get("magic") != MAGIC.decode("ascii"):
        raise ValueError("Not a Worldgen Lab file.")
    if meta.get("version") not in (1, 2, 3, 4):
        raise ValueError(f"Unsupported version: {meta.get('version')}")
    return {
        "settings": Settings.from_dict(meta["settings"]),
        "overview": payload["overview"],
        "chunk_ready": payload["chunk_ready"],
        "sea": float(meta["sea_level"]),
        "river_threshold": int(meta["river_threshold"]),
        "done": int(meta["done"]),
        "lowres": payload.get("lowres"),
        "base_lowres": payload.get("base_lowres"),
        "flora": payload.get("flora"),
        "generator": meta.get("generator", "1.0"),
    }

def load_world(path: Path):
    d = load_world_full(path)
    return (d["settings"], d["overview"], d["chunk_ready"], d["sea"],
            d["river_threshold"], d["done"], d["lowres"], d["generator"])

def build_sampler(method: str, seed: int):
    S = int(seed)

    if method == "tectonic":
        P = 200
        rnd = mulberry32(S * 7 + 13)
        px = np.array([rnd() for _ in range(P)])
        py = np.array([rnd() for _ in range(P)])
        ph = np.array([rnd() for _ in range(P)])
        pm = np.array([rnd() for _ in range(P)])

        def sampler(fx, fy):
            fx = np.asarray(fx, np.float64); fy = np.asarray(fy, np.float64)
            fxf = fx.ravel(); fyf = fy.ravel()
            out = np.empty(fxf.size)
            for s0 in range(0, fxf.size, _BLOCK):
                s1 = min(fxf.size, s0 + _BLOCK)
                bx = fxf[s0:s1]; by = fyf[s0:s1]
                dx = bx[:, None] - px; dy = by[:, None] - py
                d = dx * dx + dy * dy
                part = np.argpartition(d, 1, axis=1)[:, :2]
                dv = np.take_along_axis(d, part, axis=1)
                flip = dv[:, 1] < dv[:, 0]
                part[flip] = part[flip][:, ::-1]
                i1 = part[:, 0]; i2 = part[:, 1]
                rows = np.arange(s1 - s0)
                d1 = np.sqrt(d[rows, i1]); d2 = np.sqrt(d[rows, i2])
                b = np.exp(-((d2 - d1) * 24) ** 2)
                conv = 1 - np.abs(pm[i1] - pm[i2])
                h = 0.24 + ph[i1] * 0.45 + b * (conv * 0.52 - (1 - conv) * 0.28)
                h += (fbm(bx * 9, by * 9, S + 5, 4) - 0.5) * 0.13
                out[s0:s1] = h
            return out.reshape(fx.shape)
        return sampler

    if method == "voronoi":
        G = 50
        def sampler(fx, fy):
            fx = np.asarray(fx, np.float64); fy = np.asarray(fy, np.float64)
            cx = np.floor(fx * G); cy = np.floor(fy * G)
            ds = []
            for oy in (-1, 0, 1):
                for ox in (-1, 0, 1):
                    gx = cx + ox; gy = cy + oy
                    gxi = gx.astype(np.int64); gyi = gy.astype(np.int64)
                    jx = (gx + hash2(gxi, gyi, S + 1)) / G
                    jy = (gy + hash2(gxi, gyi, S + 2)) / G
                    dx = fx - jx; dy = fy - jy
                    ds.append(np.sqrt(dx * dx + dy * dy))
            D = np.sort(np.stack(ds), axis=0)
            v = (D[1] - D[0]) * 2.6
            v = v * 0.8 + fbm(fx * 9, fy * 9, S + 7, 4) * 0.30
            return v
        return sampler

    if method == "warp":
        def sampler(fx, fy):
            wx = fbm(fx * 8, fy * 8, S + 11, 4) - 0.5
            wy = fbm(fx * 8 + 31.7, fy * 8 + 11.3, S + 22, 4) - 0.5
            return fbm(fx * 12 + wx * 7, fy * 12 + wy * 7, S + 33, 6)
        return sampler

    if method == "ridged":
        def sampler(fx, fy):
            return ridged(fx * 10, fy * 10, S + 17, 6) * 0.88 \
                 + fbm(fx * 5, fy * 5, S + 91, 3) * 0.24 - 0.05
        return sampler

    if method == "billow":
        def sampler(fx, fy):
            fx = np.asarray(fx, np.float64); fy = np.asarray(fy, np.float64)
            v = np.zeros(fx.shape); amp = 1.0; tot = 0.0; fr = 4.0
            for o in range(5):
                n = vnoise(fx * fr, fy * fr, S + 201 + o)
                v += amp * np.abs(n * 2 - 1)
                tot += amp; amp *= 0.5; fr *= 2.02
            return v / tot
        return sampler

    if method == "terraces":
        def sampler(fx, fy):
            base = np.clip(fbm(fx * 6, fy * 6, S + 301, 5), 0, 1) * 7
            k = np.floor(base); f = base - k
            return (k + _smooth(0.6, 1.0, f)) / 7
        return sampler

    if method == "cellular":
        G = 10
        def sampler(fx, fy):
            fx = np.asarray(fx, np.float64); fy = np.asarray(fy, np.float64)
            d1 = np.full(fx.shape, 9.0)
            for jx, jy, _ in _sites(fx, fy, G, S + 501):
                d1 = np.minimum(d1, np.hypot(fx - jx, fy - jy))
            return np.clip(1 - d1 * G * 0.6, 0, 1) * 0.85 + fbm(fx * 8, fy * 8, S + 502, 3) * 0.25
        return sampler

    if method == "plateau":
        def sampler(fx, fy):
            p = np.clip(fbm(fx * 5, fy * 5, S + 601, 5), 0, 1)
            return np.round(p * 4) / 4 * 0.75 + p * 0.25
        return sampler

    if method == "ranges":
        def sampler(fx, fy):
            wx = fbm(fx * 3, fy * 3, S + 701, 3) - 0.5
            wy = fbm(fx * 3 + 9.1, fy * 3 + 4.7, S + 702, 3) - 0.5
            r = ridged(fx * 6 + wx * 2.5, fy * 6 + wy * 2.5, S + 703, 5)
            return r * 0.85 + fbm(fx * 2, fy * 2, S + 704, 3) * 0.2 - 0.05
        return sampler

    if method == "turbulence":
        def sampler(fx, fy):
            fx = np.asarray(fx, np.float64); fy = np.asarray(fy, np.float64)
            wx = fbm(fx * 4, fy * 4, S + 801, 3) - 0.5
            wy = fbm(fx * 4 + 5.2, fy * 4 + 1.3, S + 802, 3) - 0.5
            X = fx * 7 + wx * 1.5; Y = fy * 7 + wy * 1.5
            v = np.zeros(fx.shape); amp = 1.0; tot = 0.0; fr = 1.0
            for o in range(5):
                v += amp * np.abs(vnoise(X * fr, Y * fr, S + 803 + o) * 2 - 1)
                tot += amp; amp *= 0.55; fr *= 2.1
            return v / tot
        return sampler

    if method == "hills":
        def sampler(fx, fy):
            return fbm(fx * 4, fy * 4, S + 901, 3) * 0.8 + fbm(fx * 11, fy * 11, S + 902, 2) * 0.2
        return sampler

    def sampler(fx, fy):
        return fbm(fx * 12, fy * 12, S, 6)
    return sampler


def river_threshold_for(s: "Settings"):
    return int(round(RIVER_BASE * (s.chunk * s.chunk) / 65536))

def soft_unit(v, knee=0.94):
    v = np.asarray(v, np.float64)
    out = v.copy()
    hi = v > knee
    lo = v < 1.0 - knee
    out[hi] = knee + (1.0 - knee) * np.tanh((v[hi] - knee) / (1.0 - knee))
    out[lo] = (1.0 - knee) - (1.0 - knee) * np.tanh(((1.0 - knee) - v[lo]) / (1.0 - knee))
    return out


def _remap_wrap(base, S, p, lo, hi, a, gamma=1.0, tail=64):
    S = np.asarray(S, np.float64)
    edges = np.linspace(S[0], S[-1], 4097)
    hist, _ = np.histogram(S, bins=edges)
    cdf = (np.cumsum(hist) - hist / 2.0) / S.size
    k = np.exp(-0.5 * (np.arange(-120, 121) / 40.0) ** 2)
    k /= k.sum()
    grid_q = np.maximum.accumulate(np.convolve(np.pad(cdf, 120, mode="edge"), k, mode="valid"))
    S = (edges[1:] + edges[:-1]) / 2.0
    top_dq = (grid_q[-1] - grid_q[-tail]) / max(1e-12, S[-1] - S[-tail])
    bot_dq = (grid_q[tail - 1] - grid_q[0]) / max(1e-12, S[tail - 1] - S[0])
    norm = 1.0 - np.exp(-a)

    def sampler(fx, fy):
        v = np.asarray(base(fx, fy), np.float64)
        q = np.interp(v, S, grid_q)
        q = np.where(v > S[-1], 1.0 + (v - S[-1]) * top_dq, q)
        q = np.where(v < S[0], (v - S[0]) * bot_dq, q)
        out = np.empty_like(q)
        land = q > p
        u = np.power(np.clip((q[land] - p) / max(1e-9, 1.0 - p), 0.0, None), gamma)
        out[land] = p + (hi - p) * (1.0 - np.exp(-a * u)) / norm
        sea = ~land
        w = q[sea] / max(1e-9, p)
        out[sea] = lo + (p - lo) * (1.0 - np.exp(-a * w)) / norm
        return out
    return sampler


def _geology(S):
    def hard(fx, fy):
        n = fbm(np.asarray(fx, np.float64) * 5.0 + 3.3, np.asarray(fy, np.float64) * 5.0 + 1.7, S + 3131, 4)
        return np.clip(0.5 + (n - 0.5) * 2.2, 0.0, 1.0)
    return hard


def _detail_wrap(base, S, hard, sea, amp, mask_k, geo):
    def sampler(fx, fy):
        v = np.asarray(base(fx, fy), np.float64)
        n = fbm(np.asarray(fx, np.float64) * 10.0, np.asarray(fy, np.float64) * 10.0, S + 77, 3) - 0.5
        rock = (1.0 - 0.5 * geo) + geo * hard(fx, fy)
        hi = _smooth(0.25, 0.6, v - sea)
        mask = (1.0 - mask_k) + mask_k * (1.0 - hi)
        return v + amp * n * rock * mask
    return sampler


EROSION_RES = 256
EROSION_ITERS = 24


def _diffuse(a, n):
    for _ in range(n):
        P = np.pad(a, 1, mode="edge")
        a = (P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] + 4.0 * a) / 8.0
    return a


_OUTLET = -1  # receiver index for water that runs off the edge of the map


def _receivers(H):
    """Steepest-descent routing (D8): every cell passes its water to whichever
    neighbour is lowest. A cell with no lower neighbour is a pit and keeps its water.
    Water that steps off the map edge leaves the world."""
    G = H.shape[0]
    P = np.pad(H, 1, constant_values=-1.0)
    yy, xx = np.indices((G, G))
    best = np.zeros((G, G))
    rec = yy * G + xx
    for oy, ox in _NEIGH8:
        N = P[1 + oy:1 + oy + G, 1 + ox:1 + ox + G]
        ny = yy + oy
        nx = xx + ox
        off = (ny < 0) | (ny >= G) | (nx < 0) | (nx >= G)
        drop = (H - N) / (1.4142 if (oy and ox) else 1.0)
        better = drop > best
        best = np.where(better, drop, best)
        cand = np.where(off, _OUTLET, ny * G + nx)
        rec = np.where(better, cand, rec)
    return rec.ravel()


def _accumulate(rec, H, runoff):
    """Total runoff that passes through each cell, summed from high ground down."""
    G = H.shape[0]
    order = np.argsort(-H.ravel(), kind="stable").tolist()
    r = rec.tolist()
    A = np.asarray(runoff, np.float64).ravel().tolist()
    for c in order:
        t = r[c]
        if t != c and t >= 0:
            A[t] += A[c]
    return np.array(A).reshape(G, G)


def _lake_mask(H, rec, A, land, lakes, fill):
    """Lakes are basins where the water collects. A basin counts as a lake when it
    is still a pit, or when sediment has filled it, and it receives enough runoff
    to hold water. The pool spreads into neighbouring ground at about the same level."""
    G = H.shape[0]
    idx = np.arange(G * G).reshape(G, G)
    sink = (rec.reshape(G, G) == idx) & land
    q = 0.985 - 0.04 * float(lakes)
    thr = float(np.quantile(A[land], q)) if land.any() else np.inf
    core = (sink | ((fill > 0.004) & land)) & (A >= thr)
    level = np.where(core, H, np.inf)
    P = np.pad(level, 1, constant_values=np.inf)
    nb = np.min(np.stack([P[1 + oy:1 + oy + G, 1 + ox:1 + ox + G] for oy, ox in _NEIGH8]), axis=0)
    pool = np.isfinite(nb) & land & (H <= nb + 0.006)
    return core | pool


def _erode_hydro(H, sea, W, strength, iters, runoff):
    """Each pass routes runoff over the current surface and does two things:
    water carves valleys where it concentrates, and sediment settles in any pit
    the water has not yet left, raising its floor. A pit fills until it spills over
    its lowest rim, after which its water runs on downhill. Returns the eroded
    surface and how much sediment settled at each cell (used to find lakes)."""
    G = H.shape[0]
    idx = np.arange(G * G)
    K = 0.0013 * strength
    D = 0.12
    SILT = 0.0015
    Wf = W.ravel()
    fill = np.zeros(G * G)
    for _ in range(iters):
        land = H >= sea
        landf = land.ravel()
        rec = _receivers(H)
        A = _accumulate(rec, H, runoff).ravel()
        Hf = H.ravel()
        rr = np.where(rec >= 0, rec, 0)
        below = np.where(rec >= 0, Hf[rr], sea)  # water running off the map drops to sea level
        slope = np.clip(Hf - below, 0.0, None)
        dz = K * np.sqrt(A) * slope * Wf * landf
        Hn = np.where(landf, np.maximum(Hf - dz, sea + 1e-4), Hf)
        pit = (rec == idx) & landf & (A > 0)
        dep = np.where(pit, np.minimum(0.02, SILT * np.sqrt(A)), 0.0)
        Hn = Hn + dep
        fill += dep
        Hn = Hn.reshape(G, G)
        P = np.pad(Hn, 1, mode="edge")
        lap = P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] - 4.0 * Hn
        H = Hn + D * lap * land
    return H, fill.reshape(G, G)


def _hist_match(H, target):
    G = H.shape[0]
    out = np.empty(G * G)
    out[np.argsort(H.ravel(), kind="stable")] = np.sort(target.ravel())
    return out.reshape(G, G)


def _settle(H, sea, land_passes=14, sea_passes=120):
    """Erosion settles the land gently: weathering and sediment level low ground and round
    off steep faces. Under the sea the seabed is smoothed heavily, since waves and
    sediment wear every feature there flat. Land stays at sea level or above, and the
    seabed stays below it."""
    for k in range(max(land_passes, sea_passes)):
        land = H >= sea
        P = np.pad(H, 1, mode="edge")
        lap = P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] - 4.0 * H
        if k < land_passes:
            H = np.where(land, np.maximum(H + 0.2 * lap, sea + 1e-4), H)
        if k < sea_passes:
            H = np.where(~land, np.minimum(H + 0.25 * lap, sea - 1e-4), H)
    return H


def _global_pass(pre, s, sea, weather):
    """Stage 3 of the pipeline: erode the land by the runoff the weather produced,
    settle sediment into pits, then route the runoff downhill to find rivers and the
    lakes where it collects."""
    Gpre = weather["elev"]
    G = Gpre.shape[0]
    c = (np.arange(G) + 0.5) / G
    FX, FY = np.meshgrid(c, c)
    hard = _geology(int(s.seed))(FX, FY)
    hi = _smooth(0.0, 0.2, Gpre - sea)
    mask = (1.0 - float(s.mask_strength)) + float(s.mask_strength) * hi
    W = mask * (1.0 - 0.7 * float(s.geology) * hard)
    runoff = weather["runoff"]
    Eg, fill = _erode_hydro(Gpre.copy(), sea, W, float(s.erosion), EROSION_ITERS, runoff)
    Eg = _settle(Eg, sea)
    Eg = _hist_match(Eg, Gpre)
    land = Eg >= sea
    rec = _receivers(Eg)
    A = _accumulate(rec, Eg, runoff)
    lake = _lake_mask(Eg, rec, A, land, float(s.lakes), fill).astype(np.float64)
    thr = float(np.quantile(A[land], 0.985)) if land.any() else 1.0
    return _erosion_wrap(pre, Eg, Gpre), lake, A, thr, Eg


def _erosion_wrap(pre, Eg, Gpre):
    def sampler(fx, fy):
        base = _bilerp(Eg, fx, fy) + RESIDUAL_DETAIL * (np.asarray(pre(fx, fy), np.float64) - _bilerp(Gpre, fx, fy))
        return np.clip(base, 0.0, 1.0)
    return sampler


def _smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def _sites(fx, fy, G, S):
    cx = np.floor(fx * G); cy = np.floor(fy * G)
    out = []
    for oy in (-2, -1, 0, 1, 2):
        for ox in (-2, -1, 0, 1, 2):
            gx = cx + ox; gy = cy + oy
            gxi = gx.astype(np.int64); gyi = gy.astype(np.int64)
            jx = (gx + hash2(gxi, gyi, S + 1)) / G
            jy = (gy + hash2(gxi, gyi, S + 2)) / G
            out.append((jx, jy, hash2(gxi, gyi, S + 3)))
    return out

def _resample_nearest(arr, n):
    m = arr.shape[0]
    idx = np.minimum(np.floor((np.arange(n) + 0.5) * m / n).astype(np.int64), m - 1)
    return np.ascontiguousarray(arr[idx][:, idx])

def _rim_wrap(base, sea, S):
    def sampler(fx, fy):
        v = np.asarray(base(fx, fy), np.float64)
        dist = (np.abs(fx - 0.5) ** 4 + np.abs(fy - 0.5) ** 4) ** 0.25
        dist = dist + 0.05 * (fbm(fx * 4, fy * 4, S + 1801, 4) - 0.5)
        m = _smooth(0.36, 0.46, dist)
        return v * (1.0 - m) + m * np.minimum(v, 0.4 * sea)
    return sampler


def _ocean_dist(land):
    R = land.shape[0]
    dist = np.where(land, np.inf, 0.0)
    while True:
        p = np.pad(dist, 1, constant_values=np.inf)
        nb = np.minimum(np.minimum(p[:-2, 1:-1], p[2:, 1:-1]),
                        np.minimum(p[1:-1, :-2], p[1:-1, 2:])) + 1
        new = np.minimum(dist, nb)
        if np.array_equal(new, dist):
            break
        dist = new
    dist[np.isinf(dist)] = R
    return dist


def _rank(X, land):
    vals = np.sort(X[land]) if land.any() else np.sort(X.ravel())
    return np.clip(np.searchsorted(vals, X) / vals.size, 0.0, 1.0)


def _wind_transport(E, land, lat, warmth):
    """Carry moisture across the map in the prevailing wind belts. The westerly belt
    (mid-latitudes) blows toward +x and the trade and polar belts toward -x. Air
    picks up moisture over the sea, scaled by how warm the sea is, and drops it on
    land, most where the ground rises into its path.
    Returns (rain, carried): rainfall per cell, and the moisture left in the air after it."""
    R = E.shape[0]
    decay = 0.012 + 0.07 * np.exp(-((lat - 0.33) / 0.09) ** 2)
    row_lat = lat[:, 0]
    westerly = (row_lat >= 0.33) & (row_lat < 0.66)
    rain_by_dir = {}
    carried_by_dir = {}
    for direction in (1, -1):
        m = np.zeros(R)
        prev = None
        rain_out = np.zeros((R, R))
        carried_out = np.zeros((R, R))
        cols = range(R) if direction == 1 else range(R - 1, -1, -1)
        for i in cols:
            e = E[:, i]
            rise = np.zeros(R) if prev is None else np.clip(e - prev, 0.0, None)
            m = np.where(land[:, i], m, warmth[:, i])
            rain = m * (1.0 - np.exp(-(4.0 * rise + 0.015)))
            m = m - rain
            rain_out[:, i] = rain
            carried_out[:, i] = m
            m = m * (1.0 - decay[:, i])
            prev = e
        rain_by_dir[direction] = rain_out
        carried_by_dir[direction] = carried_out
    pick = westerly[:, None]
    return (np.where(pick, rain_by_dir[1], rain_by_dir[-1]),
            np.where(pick, carried_by_dir[1], carried_by_dir[-1]))


def _stage_weather(pre, sea, S, G=None):
    """Stage 2 of the pipeline: weather, worked out from the geography alone.
    Temperature falls with latitude and altitude. Moisture comes from warm seas,
    travels with the wind belts and falls as rain on the way inland. Whatever rain
    is not lost to evaporation runs off the land and feeds the hydrology stage."""
    G = G or EROSION_RES
    c = (np.arange(G) + 0.5) / G
    FX, FY = np.meshgrid(c, c)
    E = np.asarray(pre(FX, FY), np.float64)
    land = E >= sea
    lat = np.abs(FY * 2 - 1)  # 0 on the equator row, 1 at the poles
    alt = np.clip(E - sea, 0.0, None)
    T0 = (1 - lat ** 1.25 * 0.85 - alt * 1.0 - 0.3 * _smooth(0.78, 0.95, lat)
          + (fbm(FX * 3, FY * 3, S + 404, 3) - 0.5) * 0.12)
    warmth = np.clip(T0, 0.2, 1.0)
    rain, carried = _wind_transport(E, land, lat, warmth)
    rain = _diffuse(rain, 3)
    carried = _diffuse(carried, 6)
    rain_peak = max(float(rain.max()), 1e-9)
    M0 = (0.6 * carried + 0.4 * rain / rain_peak
          + (fbm(FX * 3.5 + 13.7, FY * 3.5 + 7.1, S + 909, 4) - 0.5) * 0.12)
    evap = 0.2 + 0.6 * np.clip(T0, 0.0, 1.0)
    wet = np.where(land, rain * (1.0 - evap), 0.0)
    mean_wet = float(wet[land].mean()) if land.any() else 1.0
    # Rain-fed runoff, plus a steady baseflow from snowmelt and groundwater everywhere
    # on land so dry interiors still drain. Normalised so the land averages 1.0.
    runoff = np.where(land, 0.7 * wet / max(mean_wet, 1e-9) + 0.3, 0.0)
    return {
        "elev": E,
        "land": land,
        "temp": _rank(T0, land),
        "moist": _rank(M0, land),
        "runoff": runoff,
    }


def classify_biome(e, h, t, m, sl, sea):
    """Stage 4: the substrate of each tile, decided from the stages before it. Only
    inorganic ground comes out: rock, sand, gravel, clay, loam and ice.
    Geography first: what lies under the water, the shore, cliffs, bare rock on high
    ground and loose rock on steep slopes. Then weather for the land that is left."""
    alt = h - sea
    geo_conds = [
        h < sea - 0.055,                      # deep water: silt settles on the floor
        h < sea,                              # shallow water: sand on the seabed
        (h < sea + 0.03) & (sl < 0.5),        # beach
        (h < sea + 0.02) & (sl >= 0.97),      # cliff at the shoreline
        (e > 0.86) & (sl > 0.06),             # high ground: bare bedrock
        (sl > 0.6) & (alt > 0.12),            # steep slopes shed loose rock
    ]
    geo_choices = [SILT, SEABED_SAND, BEACH_SAND, CLIFF_ROCK, BEDROCK, SCREE]
    weather_conds = [
        t < 0.05,
        (t < 0.14) & (alt > 0.22),
        t < 0.14,
        t < 0.30,
        (m < 0.22) & (t > 0.40),
        (m < 0.32) & (t > 0.40),
        m < 0.18,
        m < 0.50,
        (m < 0.86) | (t <= 0.55),
    ]
    weather_choices = [ICE, ICE, FROZEN_CLAY, np.where(m > 0.4, CLAY, GRAVEL), DESERT_SAND,
                       ARID_SOIL, GRAVEL, LOAM, LOAM]
    conds = geo_conds + weather_conds
    choices = geo_choices + weather_choices
    choices = [np.broadcast_to(np.asarray(c), h.shape) for c in choices]
    return np.select(conds, choices, default=CLAY)


def render_lowres_rgb(lr, mode, sea):
    """Colour a block of low-resolution samples for the overview, minimap and editor."""
    h = lr["h"]
    if mode == "height":
        land = np.clip((h - sea) / max(1e-6, 1 - sea), 0, 1)
        deep = np.clip((sea - h) / max(1e-6, sea), 0, 1)
        land_c = np.stack([lerp(70, 230, land), lerp(140, 240, land), lerp(70, 220, land)], -1)
        sea_c = np.stack([lerp(120, 12, deep), lerp(170, 30, deep), lerp(210, 60, deep)], -1)
        out = np.where((h < sea)[..., None], sea_c, land_c)
    elif mode == "temperature":
        t = np.clip(lr["t"], 0, 1)[..., None]
        out = np.where(t < 0.5, lerp_arr([40, 90, 200], [240, 240, 240], t * 2),
                       lerp_arr([240, 240, 240], [215, 50, 40], (t - 0.5) * 2))
    elif mode == "moisture":
        m = np.clip(lr["m"], 0, 1)[..., None]
        out = lerp_arr([205, 180, 120], [30, 90, 160], m)
    else:
        tw = h / sea
        wc = np.stack([lerp(10, 44, tw), lerp(26, 102, tw), lerp(56, 146, tw)], -1)
        lc = BIOME_RGB_F[lr["b"]] * lr["shade"][..., None]
        out = np.where((h < sea)[..., None], wc, lc)
        out = np.where((lr["riv"] & (h > sea))[..., None], [70, 150, 215], out)
    return np.clip(out, 0, 255).astype(np.uint8)


def _shelf_wrap(base, sea, grid):
    def sampler(fx, fy):
        v = np.asarray(base(fx, fy), np.float64)
        f = _bilerp(grid, fx, fy)
        return np.where(v < sea, sea - (sea - v) * f, v)
    return sampler


def build_ridges(s):
    if float(s.shape_strength) <= 0.001:
        return None
    S = int(s.seed)
    rnd = mulberry32(S * 97 + 41)
    P = 14
    px = np.array([0.05 + 0.9 * rnd() for _ in range(P)])
    py = np.array([0.05 + 0.9 * rnd() for _ in range(P)])

    def ridge(fx, fy):
        fx = np.asarray(fx, np.float64); fy = np.asarray(fy, np.float64)
        d1 = np.full(fx.shape, 9.0); d2 = np.full(fx.shape, 9.0)
        for x, y in zip(px, py):
            d = np.hypot(fx - x, fy - y)
            d2 = np.where(d < d1, d1, np.minimum(d2, d))
            d1 = np.minimum(d1, d)
        b = np.exp(-((d2 - d1) * 16) ** 2)
        return b * (0.55 + 0.45 * fbm(fx * 6, fy * 6, S + 4401, 3))
    return ridge


def _ocean_shelf_grid(sampler, sea, R=96):
    c = (np.arange(R) + 0.5) / R
    FX, FY = np.meshgrid(c, c)
    land = np.asarray(sampler(FX, FY), np.float64) >= sea
    dn = _ocean_dist(~land) / R
    return 0.3 + 0.7 * np.clip(dn / 0.12, 0.0, 1.0)


def _edge_wrap(base, edge):
    band = 0.05 + 0.35 * edge

    def sampler(fx, fy):
        v = np.asarray(base(fx, fy), np.float64)
        d = np.minimum(np.minimum(fx, 1.0 - fx), np.minimum(fy, 1.0 - fy))
        return soft_unit(v * (1.0 - 0.98 * edge * (1.0 - _smooth(0.0, band, d))))
    return sampler


def build_layout(s):
    if float(s.shape_strength) <= 0.001:
        return None
    S = int(s.seed)
    f = float(s.frequency)
    rnd = mulberry32(S * 53 + 19)
    n_major = 3 + int(clamp(float(s.landmass_count), 0.0, 0.999) * 4)
    size_f = 0.45 + 0.9 * clamp(float(s.landmass_size), 0.0, 1.0)
    elong = clamp(float(s.elongation), 0.0, 1.0)
    warp = 0.03 + 0.15 * clamp(float(s.inlets), 0.0, 1.0)
    rough = clamp(float(s.coast_rough), 0.0, 1.0)
    lakes = clamp(float(s.lakes), 0.0, 1.0)
    lake_t = 0.82 - 0.3 * lakes

    majors = []
    for _ in range(400):
        if len(majors) >= n_major:
            break
        cx = 0.18 + 0.64 * rnd(); cy = 0.18 + 0.64 * rnd()
        if all(math.hypot(cx - x, cy - y) > 0.3 for x, y, _ in majors):
            majors.append((cx, cy, 0.17 + 0.08 * rnd()))
    blobs = [(cx, cy, sig * size_f, 1.0 + elong * (0.8 + 0.4 * rnd()))
             for cx, cy, sig in majors]
    for _ in range(5 + int(rnd() * 8)):
        cx = 0.05 + 0.90 * rnd(); cy = 0.05 + 0.90 * rnd()
        sig = 0.035 + 0.04 * rnd() ** 2
        blobs.append((cx, cy, sig * size_f, 1.0))

    def sh(fx, fy):
        wx = fx + (fbm(fx * 9 * f, fy * 9 * f, S + 133, 4) - 0.5) * 2 * warp
        wy = fy + (fbm(fx * 9 * f, fy * 9 * f, S + 134, 4) - 0.5) * 2 * warp
        coast = (fbm(fx * 8 * f, fy * 8 * f, S + 131, 5) - 0.5) * rough
        fine = (fbm(fx * 18 * f, fy * 18 * f, S + 132, 4) - 0.5) * rough * 0.5
        field = np.zeros(np.shape(fx))
        for cx, cy, sig, ax in blobs:
            d = np.hypot((wx - cx) * ax, wy - cy) / sig + coast * 2 + fine
            field = np.maximum(field, np.exp(-d * d))
        if lakes > 0.001:
            water = _smooth(lake_t, lake_t + 0.1, fbm(fx * 8 * f, fy * 8 * f, S + 232, 4))
            field = field * (1.0 - water)
        return field
    return sh

def planet_active(s):
    return s.tectonic > 0


def planet_resolution(s):
    return int(clamp(s.num_chunks * 10, 64, 256))


def _bilerp(G, FX, FY):
    R = G.shape[0]
    gx = np.clip(np.asarray(FX, np.float64) * R - 0.5, 0, R - 1)
    gy = np.clip(np.asarray(FY, np.float64) * R - 0.5, 0, R - 1)
    i0 = np.floor(gx).astype(np.int64); j0 = np.floor(gy).astype(np.int64)
    i1 = np.minimum(i0 + 1, R - 1); j1 = np.minimum(j0 + 1, R - 1)
    tx = gx - i0; ty = gy - j0
    a = G[j0, i0]; b = G[j0, i1]; c = G[j1, i0]; d = G[j1, i1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


class PlanetFields:
    def __init__(self, s, base):
        R = self.R = planet_resolution(s)
        S = int(s.seed)
        c = (np.arange(R) + 0.5) / R
        FX, FY = np.meshgrid(c, c)
        E0 = soft_unit(np.asarray(base(FX, FY), np.float64))
        rnd = mulberry32(S * 97 + 11)
        N = 16 + int(rnd() * 8)
        px = np.array([rnd() for _ in range(N)])
        py = np.array([rnd() for _ in range(N)])
        ang = np.array([rnd() * 2 * math.pi for _ in range(N)])
        spd = np.array([0.3 + 0.7 * rnd() for _ in range(N)])
        cont = np.array([rnd() < 0.4 for _ in range(N)])
        vx = np.cos(ang) * spd; vy = np.sin(ang) * spd

        D = np.stack([np.hypot(FX - px[k], FY - py[k]) for k in range(N)])
        order = np.argsort(D, axis=0)
        i1 = order[0]; i2 = order[1]
        d1 = np.take_along_axis(D, order[:1], 0)[0]
        d2 = np.take_along_axis(D, order[1:2], 0)[0]
        nx = px[i2] - px[i1]; ny = py[i2] - py[i1]
        nl = np.hypot(nx, ny) + 1e-9
        nx /= nl; ny /= nl
        rel = (vx[i1] - vx[i2]) * nx + (vy[i1] - vy[i2]) * ny
        rel = np.clip(rel / (np.abs(rel).max() + 1e-9), -1, 1)
        conv = np.clip(rel, 0, 1)
        div = np.clip(-rel, 0, 1)
        shear = 1 - np.abs(rel)

        dist = (d2 - d1) + 0.025 * (fbm(FX * 9, FY * 9, S + 1302, 4) - 0.5)
        wide = 1 - _smooth(0.0, 0.11, dist)
        narrow = 1 - _smooth(0.0, 0.035, dist)
        arc_band = _smooth(0.0, 0.02, dist) * np.exp(-((dist - 0.03) / 0.02) ** 2)
        ridge = ridged(FX * 7, FY * 7, S + 1303, 4)
        detail = fbm(FX * 30, FY * 30, S + 1304, 3) - 0.5

        ci = cont[i1]; cj = cont[i2]
        both_land = ci & cj
        mixed = ci ^ cj
        both_sea = ~ci & ~cj
        on_land = ci

        plate = np.where(ci, 0.13, -0.13) * _smooth(0.0, 0.12, dist)
        collide = conv * wide * both_land * (0.15 + 0.75 * ridge)
        arc = conv * arc_band * mixed * np.where(on_land, 0.34, -0.3)
        island = conv * arc_band * both_sea * (fbm(FX * 14, FY * 14, S + 1305, 4) > 0.5) * 0.28
        rift = -div * wide * both_land * 0.26
        ridge_sea = div * narrow * both_sea * 0.14
        fault = shear * narrow * detail * 0.2

        self.delta = s.tectonic * (plate + collide + arc + island + rift + ridge_sea + fault)
        for _ in range(8):
            P = np.pad(self.delta, 1, mode="edge")
            self.delta = (P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] + 4 * P[1:-1, 1:-1]) / 8.0
        E1 = soft_unit(E0 + self.delta)
        srt = np.sort(E1.ravel())
        self.sea = float(srt[int(srt.size * s.sea_percentile)])

    def wrap(self, base):
        def sampler(fx, fy):
            return soft_unit(np.asarray(base(fx, fy), np.float64) + _bilerp(self.delta, fx, fy))
        return sampler


class World:
    _CACHE_MAX = 36

    def __init__(self, settings: Settings):
        self.s = settings
        self.sea_level = 0.42
        self.river_threshold = 1.0
        self.overview = np.full(
            (settings.overview_size, settings.overview_size, 3), (6, 9, 13), np.uint8
        )
        self.chunk_ready = np.zeros((settings.chunks, settings.chunks), bool)
        self.done = 0
        self.total = settings.total_chunks
        self._cache = OrderedDict()
        self.lowres = {}
        self.view_mode = "biome"

        raw = build_sampler(settings.method, settings.seed)
        shape = build_layout(settings)
        ridges = build_ridges(settings)
        self.sampler, self.sea_level = self._compute_baselines(raw, shape, ridges)
        if planet_active(settings):
            planet = PlanetFields(settings, self.sampler)
            self.sampler = planet.wrap(self.sampler)
            self.sea_level = planet.sea
        edge = clamp(float(settings.edge_water), 0.0, 1.0)
        if edge > 0.001:
            self.sampler = _edge_wrap(self.sampler, edge)
        self.hard_fn = _geology(int(settings.seed))
        self.sampler = _detail_wrap(self.sampler, int(settings.seed), self.hard_fn, self.sea_level,
                                    0.06 * float(settings.detail), float(settings.mask_strength),
                                    float(settings.geology))
        self.sampler = _rim_wrap(self.sampler, self.sea_level, int(settings.seed))
        self.sampler = _shelf_wrap(self.sampler, self.sea_level, _ocean_shelf_grid(self.sampler, self.sea_level))
        steps = 200
        c = (np.arange(steps) + 0.5) / steps
        FXr, FYr = np.meshgrid(c, c)
        S = np.sort(np.asarray(self.sampler(FXr, FYr), np.float64).ravel())
        self.sampler = _remap_wrap(self.sampler, S, float(settings.sea_percentile),
                                   0.02, 0.90, 0.6 / max(0.25, float(settings.relief)),
                                   gamma=float(settings.land_gamma))
        self.sea_level = float(settings.sea_percentile)
        # Stage 2 - weather, worked out from the geography alone.
        weather = _stage_weather(self.sampler, self.sea_level, int(settings.seed))
        # Stage 3 - erosion and hydrology, driven by that weather.
        (self.sampler, self.lake_grid, self.flow_grid, self.river_threshold,
         eroded) = _global_pass(self.sampler, settings, self.sea_level, weather)
        self.climate_t = weather["temp"]
        self.climate_m = weather["moist"]
        self.climate_e = _rank(eroded, eroded >= self.sea_level)

    def _compute_baselines(self, raw, shape, ridges=None):
        s = self.s
        f = float(s.frequency)
        k = clamp(float(s.shape_strength), 0.0, 1.0) if shape is not None else 0.0
        steps = int(clamp(round(200 * max(1.0, f)), 200, 400))
        c = (np.arange(steps) + 0.5) / steps
        FX, FY = np.meshgrid(c, c)
        raw_s = np.asarray(raw(FX * f, FY * f), np.float64)
        mn = float(np.percentile(raw_s, 0.5)); mx = float(np.percentile(raw_s, 99.5))
        rng = (mx - mn) or 1.0

        def field(fx, fy):
            base = (raw(fx * f, fy * f) - mn) / rng
            if shape is not None:
                sv = shape(fx, fy)
                base = base * (1.0 - k) + sv * k
                if ridges is not None:
                    base = base + 0.22 * ridges(fx, fy) * sv * (1.0 - np.clip(base, 0.0, 1.0))
            return soft_unit(base)

        samples = np.asarray(field(FX, FY), np.float64).ravel()
        sea = float(np.sort(samples)[int(len(samples) * s.sea_percentile)])
        return field, sea

    def regenerate_from_loaded(self, overview, chunk_ready, sea_level,
                               river_threshold, done, lowres=None):
        self.sea_level = sea_level
        self.river_threshold = river_threshold
        self._cache.clear()
        self.chunk_ready = chunk_ready.copy()
        self.lowres = dict(lowres) if lowres else {}
        if overview.shape[:2] == self.overview.shape[:2]:
            self.overview = overview.copy()
            self.done = done
            return
        self.overview = _resample_nearest(overview, self.overview.shape[0])
        self.done = int(chunk_ready.sum())

    def place_overview(self, cx, cy, img):
        ts = self.s.overview_ts
        px = cx * ts; py = cy * ts
        self.overview[py:py + ts, px:px + ts] = img

    def get_chunk(self, cx, cy):
        if not self.chunk_ready[cy, cx]:
            return None
        key = (cx, cy)
        ch = self._cache.get(key)
        if ch is not None:
            self._cache.move_to_end(key)
            return ch
        ch = self.generate_chunk(cx, cy)
        self._cache[key] = ch
        if len(self._cache) > self._CACHE_MAX:
            self._cache.popitem(last=False)
        return ch

    def generate_chunk(self, cx, cy):
        s = self.s
        CH = s.chunk
        S = int(s.seed)
        WW = s.world_w
        c = np.arange(CH) + 0.5
        FX, FY = np.meshgrid((cx * CH + c) / WW, (cy * CH + c) / WW)

        # Stage 1 - geography: the eroded elevation.
        height = np.asarray(self.sampler(FX, FY), np.float32)

        # Stage 2 - weather: temperature, moisture and the elevation rank.
        temp = _bilerp(self.climate_t, FX, FY).astype(np.float32)
        moist = _bilerp(self.climate_m, FX, FY).astype(np.float32)
        elev = _bilerp(self.climate_e, FX, FY)

        # Stage 3 - hydrology: river flow and where water collects.
        flow = _bilerp(self.flow_grid, FX, FY).astype(np.float32)
        lake_cover = _bilerp(self.lake_grid, FX, FY) > 0.5
        thr = self.river_threshold

        dx = np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)
        dy = np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)
        dx[:, 0] = 0; dx[:, -1] = 0
        dy[0, :] = 0; dy[-1, :] = 0
        slope = np.clip(np.sqrt(dx * dx + dy * dy) * 34, 0, 1).astype(np.float32)

        hf = height.astype(np.float64)
        tf = temp.astype(np.float64); mf = moist.astype(np.float64); sf = slope.astype(np.float64)
        blend = float(s.biome_blend)
        seed = int(s.seed)
        j1 = fbm(FX * 9.0 + 1.1, FY * 9.0 + 4.2, seed + 515, 3) - 0.5
        j2 = fbm(FX * 8.0 + 7.7, FY * 8.0 + 2.9, seed + 616, 3) - 0.5
        dither = fbm(FX * 14.0 + 9.0, FY * 14.0 + 9.0, seed + 727, 2) > 0.5

        # Stage 4 - terrain: each tile's class follows from the stages above.
        biome_a = classify_biome(elev, hf, tf, mf, sf, self.sea_level)
        biome_b = classify_biome(elev, hf, tf + j1 * 0.3 * blend, mf + j2 * 0.3 * blend, sf, self.sea_level)
        biome = np.where(dither, biome_b, biome_a).astype(np.uint8)
        land = height >= self.sea_level
        lake = lake_cover & land
        delta = (flow > thr) & land & (height < self.sea_level + 0.025) & ~lake
        biome[lake] = FLOODPLAIN
        biome[delta] = DELTA_SILT

        return {"height": height, "temp": temp, "moist": moist,
                "slope": slope, "flow": flow, "biome": biome}

    def chunk_overview_size(self):
        return self.s.overview_ts

    def render_chunk_to_overview(self, chunk, cx, cy):
        s = self.s
        ts = self.chunk_overview_size()
        CH = s.chunk
        step = max(1, CH // ts)
        idx = np.minimum(np.arange(ts) * step, CH - 1)
        Y, X = np.meshgrid(idx, idx, indexing="ij")
        H = chunk["height"]
        lr = {
            "h": H[Y, X].astype(np.float64),
            "b": chunk["biome"][Y, X],
            "t": chunk["temp"][Y, X].astype(np.float64),
            "m": chunk["moist"][Y, X].astype(np.float64),
            "riv": (chunk["flow"][Y, X] > self.river_threshold),
            "shade": self._hillshade_grid(H, X, Y),
        }
        self.lowres[(cx, cy)] = lr
        return self.render_lowres(lr, self.view_mode)

    def render_lowres(self, lr, mode):
        return render_lowres_rgb(lr, mode, self.sea_level)

    def redraw_overview(self, mode):
        self.view_mode = mode
        for (cx, cy), lr in self.lowres.items():
            self.place_overview(cx, cy, self.render_lowres(lr, mode))

    def _hillshade_grid(self, H, X, Y):
        CH = self.s.chunk
        xm = np.maximum(0, X - 1); xp = np.minimum(CH - 1, X + 1)
        ym = np.maximum(0, Y - 1); yp = np.minimum(CH - 1, Y + 1)
        dhdx = (H[Y, xp].astype(np.float64) - H[Y, xm]) * 0.5
        dhdz = (H[yp, X].astype(np.float64) - H[ym, X]) * 0.5
        nx = -dhdx * 70; nz = -dhdz * 70; ny = 1.0
        il = 1 / np.sqrt(nx * nx + ny * ny + nz * nz)
        ndl = (nx * LX + ny * LY + nz * LZ) * il
        return 0.45 + 0.85 * np.maximum(0, ndl)

    def sample_height(self, wx, wy):
        s = self.s
        CH = s.chunk
        ww = s.world_w
        wx, wy = np.broadcast_arrays(
            np.clip(np.asarray(wx, np.float64), 0, ww - 1.001),
            np.clip(np.asarray(wy, np.float64), 0, ww - 1.001),
        )
        shape = wx.shape
        wx = wx.ravel(); wy = wy.ravel()
        out = np.zeros(wx.size)
        if wx.size == 0:
            return out.reshape(shape)
        cxi = (wx // CH).astype(np.int64); cyi = (wy // CH).astype(np.int64)
        for cy in range(int(cyi.min()), int(cyi.max()) + 1):
            for cx in range(int(cxi.min()), int(cxi.max()) + 1):
                m = (cxi == cx) & (cyi == cy)
                if not m.any():
                    continue
                ch = self.get_chunk(cx, cy)
                if ch is None:
                    continue
                H = ch["height"]
                lx = wx[m] - cx * CH; ly = wy[m] - cy * CH
                ix = lx.astype(np.int64); iy = ly.astype(np.int64)
                fx = lx - ix; fy = ly - iy
                ix1 = np.minimum(ix + 1, CH - 1); iy1 = np.minimum(iy + 1, CH - 1)
                a = H[iy, ix].astype(np.float64); b = H[iy, ix1].astype(np.float64)
                c = H[iy1, ix].astype(np.float64); d = H[iy1, ix1].astype(np.float64)
                t1 = a + (b - a) * fx
                t2 = c + (d - c) * fx
                out[m] = t1 + (t2 - t1) * fy
        return out.reshape(shape)

    def sample_biome(self, wx, wy):
        s = self.s
        CH = s.chunk
        ww = s.world_w
        wx, wy = np.broadcast_arrays(
            np.clip(np.asarray(wx, np.float64), 0, ww - 1),
            np.clip(np.asarray(wy, np.float64), 0, ww - 1),
        )
        shape = wx.shape
        wx = wx.ravel(); wy = wy.ravel()
        out = np.zeros(wx.size, np.int64)
        if wx.size == 0:
            return out.reshape(shape)
        cxi = (wx // CH).astype(np.int64); cyi = (wy // CH).astype(np.int64)
        for cy in range(int(cyi.min()), int(cyi.max()) + 1):
            for cx in range(int(cxi.min()), int(cxi.max()) + 1):
                m = (cxi == cx) & (cyi == cy)
                if not m.any():
                    continue
                ch = self.get_chunk(cx, cy)
                if ch is None:
                    continue
                ix = (wx[m] - cx * CH).astype(np.int64)
                iy = (wy[m] - cy * CH).astype(np.int64)
                out[m] = ch["biome"][iy, ix]
        return out.reshape(shape)

    def sample_cell(self, wx, wy):
        s = self.s
        ww = s.world_w
        wx = clamp(wx, 0, ww - 1)
        wy = clamp(wy, 0, ww - 1)
        cx = int(wx // s.chunk); cy = int(wy // s.chunk)
        ch = self.get_chunk(cx, cy)
        if ch is None: return None
        ix = int(wx - cx * s.chunk); iy = int(wy - cy * s.chunk)
        return {"chunk": ch, "x": ix, "y": iy}

    def preview_window_ready(self, cx, cy):
        s = self.s
        x0 = cx - s.preview_r; x1 = cx + s.preview_r
        y0 = cy - s.preview_r; y1 = cy + s.preview_r
        ccx0 = max(0, int(x0 // s.chunk)); ccx1 = min(s.chunks - 1, int(x1 // s.chunk))
        ccy0 = max(0, int(y0 // s.chunk)); ccy1 = min(s.chunks - 1, int(y1 // s.chunk))
        for yy in range(ccy0, ccy1 + 1):
            for xx in range(ccx0, ccx1 + 1):
                if not self.chunk_ready[yy, xx]:
                    return False
        return True

class WindowField:
    def __init__(self, world, cx, cy, R):
        ww = world.s.world_w
        self.X0 = int(max(0, math.floor(cx - R)))
        self.Y0 = int(max(0, math.floor(cy - R)))
        X1 = int(min(ww - 1, math.ceil(cx + R)))
        Y1 = int(min(ww - 1, math.ceil(cy + R)))
        self.nx = X1 - self.X0 + 1
        self.ny = Y1 - self.Y0 + 1
        xs = np.arange(self.X0, X1 + 1, dtype=np.float64)
        ys = np.arange(self.Y0, Y1 + 1, dtype=np.float64)
        GX, GY = np.meshgrid(xs, ys)
        self.H = np.asarray(world.sample_height(GX, GY), np.float64)
        self.B = np.asarray(world.sample_biome(GX, GY), np.int64)
        self.sea_level = world.sea_level

    def _uv(self, wx, wy):
        u = np.clip(np.asarray(wx, np.float64) - self.X0, 0.0, self.nx - 1.000001)
        v = np.clip(np.asarray(wy, np.float64) - self.Y0, 0.0, self.ny - 1.000001)
        return u, v

    def sample_height(self, wx, wy):
        u, v = self._uv(wx, wy)
        i = np.floor(u).astype(np.int64); j = np.floor(v).astype(np.int64)
        fu = u - i; fv = v - j
        i1 = np.minimum(i + 1, self.nx - 1); j1 = np.minimum(j + 1, self.ny - 1)
        H = self.H
        a = H[j, i]; b = H[j, i1]; c = H[j1, i]; d = H[j1, i1]
        t1 = a + (b - a) * fu
        t2 = c + (d - c) * fu
        return t1 + (t2 - t1) * fv

    def sample_biome(self, wx, wy):
        u, v = self._uv(wx, wy)
        i = np.floor(u).astype(np.int64); j = np.floor(v).astype(np.int64)
        return self.B[j, i]

def _slab(cam, d, lo, hi, tmin, tmax, ok):
    small = np.abs(d) < 1e-9
    dsafe = np.where(small, 1.0, d)
    a = (lo - cam) / dsafe; b = (hi - cam) / dsafe
    a, b = np.minimum(a, b), np.maximum(a, b)
    ok = ok & (~small | ((cam >= lo) & (cam <= hi)))
    tmin = np.where(small, tmin, np.maximum(tmin, a))
    tmax = np.where(small, tmax, np.minimum(tmax, b))
    return tmin, tmax, ok

def orbit_eye(yaw, pitch, dist):
    cp = math.cos(pitch); sp = math.sin(pitch)
    return (dist * cp * math.sin(yaw), dist * sp, dist * cp * math.cos(yaw))

def look_forward(yaw, pitch):
    cp = math.cos(pitch)
    return (cp * math.sin(yaw), math.sin(pitch), cp * math.cos(yaw))

def orbit_forward(world, cx, cy, R, eye):
    HS = 0.55
    span = 2 * R
    x0 = cx - R; y0 = cy - R
    g = np.arange(9) / 8
    GX, GY = np.meshgrid(x0 + g * span, y0 + g * span)
    local_max = max(0.0, float(world.sample_height(GX, GY).max()))
    centreH = float(world.sample_height(cx, cy))
    targetY = (centreH * 0.55 + local_max * 0.45) * HS
    fx = -eye[0]; fy = targetY - eye[1]; fz = -eye[2]
    l = math.sqrt(fx * fx + fy * fy + fz * fz) or 1
    return (fx / l, fy / l, fz / l)

def ray_march(world, W, H, cx, cy, R, eye, fwd, max_steps=150, refine=7):
    span = 2 * R
    x0 = cx - R; y0 = cy - R
    HS = 0.55
    FOV = 50 * math.pi / 180
    tan_half = math.tan(FOV / 2)
    sea = world.sea_level
    waterY = sea * HS

    camX, camY, camZ = eye
    fx, fy, fz = fwd
    rx = -fz; ry = 0; rz = fx
    l = math.sqrt(rx * rx + rz * rz) or 1
    rx /= l; rz /= l
    ux = ry * fz - rz * fy
    uy = rz * fx - rx * fz
    uz = rx * fy - ry * fx

    sky_top = (16, 26, 44); sky_bot = (70, 96, 124)
    aspect = W / H

    px = np.arange(W); py = np.arange(H)
    sx = (2 * (px + 0.5) / W - 1) * tan_half * aspect
    sy = (1 - 2 * (py + 0.5) / H) * tan_half
    SX = sx[None, :]; SY = sy[:, None]
    dx = fx + rx * SX + ux * SY
    dy = fy + ry * SX + uy * SY
    dz = fz + rz * SX + uz * SY
    l = np.sqrt(dx * dx + dy * dy + dz * dz)
    dx = (dx / l).ravel(); dy = (dy / l).ravel(); dz = (dz / l).ravel()
    N = H * W

    mixy = py / H
    skyR = np.repeat(np.floor(lerp(sky_top[0], sky_bot[0], mixy)), W)
    skyG = np.repeat(np.floor(lerp(sky_top[1], sky_bot[1], mixy)), W)
    skyB = np.repeat(np.floor(lerp(sky_top[2], sky_bot[2], mixy)), W)

    tmin = np.full(N, 0.02); tmax = np.full(N, 40.0); ok = np.ones(N, bool)
    tmin, tmax, ok = _slab(camX, dx, -1.0, 1.0, tmin, tmax, ok)
    tmin, tmax, ok = _slab(camY, dy, -0.15, HS + 0.15, tmin, tmax, ok)
    tmin, tmax, ok = _slab(camZ, dz, -1.0, 1.0, tmin, tmax, ok)
    valid = ok & (tmin <= tmax)

    dy_neg = dy < -1e-6
    dyS = np.where(dy_neg, dy, -1.0)
    tW = (waterY - camY) / dyS
    tWater = np.where(dy_neg & (tW > 0), tW, -1.0)

    hit = np.full(N, -1.0)
    idx = np.nonzero(valid)[0]
    t = tmin[idx] + 0.0005
    prev = t.copy()
    for _ in range(max_steps):
        if idx.size == 0:
            break
        dxi = dx[idx]; dyi = dy[idx]; dzi = dz[idx]
        pxw = camX + dxi * t; pyw = camY + dyi * t; pzw = camZ + dzi * t
        wxm = x0 + (pxw * 0.5 + 0.5) * span
        wzm = y0 + (pzw * 0.5 + 0.5) * span
        hh = world.sample_height(wxm, wzm) * HS
        crossed = pyw <= hh
        if crossed.any():
            ci = idx[crossed]
            lo = prev[crossed]; hi = t[crossed]
            cdx = dxi[crossed]; cdy = dyi[crossed]; cdz = dzi[crossed]
            for _k in range(refine):
                mt = (lo + hi) * 0.5
                mh = world.sample_height(
                    x0 + ((camX + cdx * mt) * 0.5 + 0.5) * span,
                    y0 + ((camZ + cdz * mt) * 0.5 + 0.5) * span,
                ) * HS
                below = camY + cdy * mt <= mh
                hi = np.where(below, mt, hi)
                lo = np.where(below, lo, mt)
            hit[ci] = hi
        keep = ~crossed
        idx = idx[keep]
        t_old = t[keep]
        adv = np.clip((pyw[keep] - hh[keep]) * 0.5, 0.006, 0.11)
        prev = t_old
        t = t_old + adv
        alive = t <= tmax[idx]
        idx = idx[alive]; t = t[alive]; prev = prev[alive]

    hit_w = valid & (tWater > 0) & ((hit < 0) | (tWater < hit))
    hit_t = valid & ~hit_w & (hit > 0)

    out = np.empty((N, 3))
    out[:, 0] = skyR; out[:, 1] = skyG; out[:, 2] = skyB

    wi = np.nonzero(hit_w)[0]
    if wi.size:
        tw = tWater[wi]
        wxm = x0 + ((camX + dx[wi] * tw) * 0.5 + 0.5) * span
        wzm = y0 + ((camZ + dz[wi] * tw) * 0.5 + 0.5) * span
        th = world.sample_height(wxm, wzm)
        depth = np.clip((sea - th) * 9, 0, 1)
        cr = lerp(70, 14, 1 - depth)
        cg = lerp(150, 42, 1 - depth)
        cb = lerp(190, 86, 1 - depth)
        cosT = np.abs(dy[wi])
        fres = (1 - cosT) ** 4 * 0.85 + 0.05
        r = lerp(cr, skyR[wi], fres)
        g = lerp(cg, skyG[wi], fres)
        b = lerp(cb, skyB[wi], fres)
        fog = np.clip((tw - 2.0) / 7, 0, 0.8)
        r = lerp(r, sky_bot[0] * 0.8, fog)
        g = lerp(g, sky_bot[1] * 0.8, fog)
        b = lerp(b, sky_bot[2] * 0.8, fog)
        out[wi, 0] = r; out[wi, 1] = g; out[wi, 2] = b

    ti = np.nonzero(hit_t)[0]
    if ti.size:
        ht = hit[ti]
        hx = camX + dx[ti] * ht; hz = camZ + dz[ti] * ht
        wxm = x0 + (hx * 0.5 + 0.5) * span
        wzm = y0 + (hz * 0.5 + 0.5) * span
        col = BIOME_RGB_F[world.sample_biome(wxm, wzm)]
        e = 0.012
        hL = world.sample_height(wxm - e, wzm) * HS
        hR = world.sample_height(wxm + e, wzm) * HS
        hD = world.sample_height(wxm, wzm - e) * HS
        hU = world.sample_height(wxm, wzm + e) * HS
        nx = (hL - hR) / (2 * e); nz = (hD - hU) / (2 * e)
        ny = np.ones_like(nx)
        ln = np.sqrt(nx * nx + ny * ny + nz * nz)
        nx = nx / ln; ny = ny / ln; nz = nz / ln
        ndl = np.maximum(0, nx * LX + ny * LY + nz * LZ)
        shade = 0.34 + 0.92 * ndl
        r = col[:, 0] * shade; g = col[:, 1] * shade; b = col[:, 2] * shade
        fog = np.clip((ht - 1.8) / 7, 0, 0.85)
        r = lerp(r, sky_bot[0] * 0.85, fog)
        g = lerp(g, sky_bot[1] * 0.85, fog)
        b = lerp(b, sky_bot[2] * 0.85, fog)
        out[ti, 0] = r; out[ti, 1] = g; out[ti, 2] = b

    return np.clip(out, 0, 255).astype(np.uint8).reshape(H, W, 3)

class MinimapWidget(QWidget):
    moved = pyqtSignal(float, float)
    released = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(300, 300)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.qimg: QImage | None = None
        self._ov = None
        self.px = 0.0
        self.py = 0.0
        self.ready = False
        self.chunks = 10
        self.world_w = 12800
        self.preview_r = 64
        self._dragging = False

    def configure(self, world_w, chunks, preview_r):
        self.world_w = world_w; self.chunks = chunks; self.preview_r = preview_r

    def set_overview(self, arr: np.ndarray):
        arr = np.ascontiguousarray(arr)
        h, w = arr.shape[:2]
        self._ov = arr
        self.qimg = QImage(arr.data, w, h, w * 3, QImage.Format.Format_RGB888)
        self.update()

    def refresh(self):
        self.update()

    def set_preview(self, x, y, ready):
        self.px = x; self.py = y; self.ready = ready
        self.update()

    def _rects(self):
        S = min(self.width(), self.height())
        return S, (self.width() - S) // 2, (self.height() - S) // 2

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#07090d"))
        S, ox, oy = self._rects()
        if S <= 2: return
        rect = QRectF(ox, oy, S, S)
        if self.qimg is not None:
            p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
            p.drawImage(rect, self.qimg)

        p.setPen(QPen(QColor(143, 230, 192, 26), 1))
        for i in range(1, self.chunks):
            t = i / self.chunks
            p.drawLine(QPointF(ox + t * S, oy), QPointF(ox + t * S, oy + S))
            p.drawLine(QPointF(ox, oy + t * S), QPointF(ox + S, oy + t * S))

        mx = ox + (self.px / self.world_w) * S
        my = oy + (self.py / self.world_w) * S
        half = (self.preview_r / self.world_w) * S
        pen = QColor(143, 230, 192, 240) if self.ready else QColor(230, 180, 103, 240)
        fill = QColor(143, 230, 192, 26) if self.ready else QColor(230, 180, 103, 26)
        p.setPen(QPen(pen, 1.5))
        p.setBrush(QBrush(fill))
        p.drawRect(QRectF(mx - half, my - half, half * 2, half * 2))

        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(QColor(255, 255, 255, 220), 1))
        k = S * 0.018
        p.drawLine(QPointF(mx - k, my), QPointF(mx + k, my))
        p.drawLine(QPointF(mx, my - k), QPointF(mx, my + k))
        p.setPen(QPen(QColor("#8fe6c0") if self.ready else QColor("#e6b467"), 1.5))
        p.drawEllipse(QPointF(mx, my), max(3.0, S * 0.012), max(3.0, S * 0.012))
        p.setPen(QPen(QColor(255, 255, 255, 26), 1))
        p.drawRect(rect)

    def _pick(self, pos):
        S, ox, oy = self._rects()
        u = clamp((pos.x() - ox) / S, 0, 1)
        v = clamp((pos.y() - oy) / S, 0, 1)
        self.moved.emit(u * self.world_w, v * self.world_w)

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._dragging = True
            self._pick(e.position())

    def mouseMoveEvent(self, e):
        if self._dragging and (e.buttons() & Qt.MouseButton.LeftButton):
            self._pick(e.position())

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton and self._dragging:
            self._dragging = False
            self.released.emit()

_FLY_KEYS = {n: getattr(Qt.Key, "Key_" + n).value
             for n in ("W", "A", "S", "D", "E", "Q", "C", "Space", "Shift")}
_FLY_KEY_VALUES = set(_FLY_KEYS.values())

class Preview3DWidget(QWidget):
    _LOWRES_SCALE = 2
    _DEFAULT_YAW = 0.65
    _DEFAULT_PITCH = 0.50
    _DEFAULT_DIST = 2.75
    _FLY_PITCH_LIMIT = 1.5
    _FRAME_BUDGET = 0.03
    _MIN_SCALE = 1.5
    _MAX_SCALE = 8.0
    _INTERACT_STEPS = 64
    _INTERACT_REFINE = 3

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(320, 220)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.cx = 0.0
        self.cy = 0.0
        self.mode = "orbit"
        self.yaw = self._DEFAULT_YAW
        self.pitch = self._DEFAULT_PITCH
        self.dist = self._DEFAULT_DIST
        self.fly_pos = (0.0, 0.0, 0.0)
        self.fly_yaw = 0.0
        self.fly_pitch = 0.0
        self.fly_speed = 0.6
        self._keys = set()
        self._last_tick = 0.0
        self._fly_timer = QTimer(self)
        self._fly_timer.setInterval(16)
        self._fly_timer.timeout.connect(self._fly_tick)
        self.placeholder = True
        self.progress_pct = 0
        self._img: QImage | None = None
        self._last_size = (0, 0)
        self._world: World | None = None
        self._drag = None
        self._lowres = False
        self._lowres_timer = QTimer(self)
        self._lowres_timer.setSingleShot(True)
        self._lowres_timer.setInterval(150)
        self._lowres_timer.timeout.connect(self._end_interaction)
        self._frame_timer = QTimer(self)
        self._frame_timer.setSingleShot(True)
        self._frame_timer.setInterval(16)
        self._frame_timer.timeout.connect(self.update)
        self._field = None
        self._field_key = None
        self._need_render = False
        self._scale = 2.0

    def set_world(self, world: World):
        self._world = world
        self._field = None
        self._field_key = None
        self._img = None
        self._last_size = (0, 0)
        self.update()

    def set_center(self, cx, cy):
        if (cx, cy) != (self.cx, self.cy):
            self.cx = cx; self.cy = cy
            self.reset_camera()

    def set_mode(self, mode):
        if mode == self.mode:
            return
        if mode == "fly":
            self._sync_fly_from_orbit()
            self.mode = "fly"
            self.setFocus()
        else:
            self.mode = "orbit"
            self._stop_fly()
        self._lowres = False
        self._img = None
        self.update()

    def reset_camera(self):
        self.yaw = self._DEFAULT_YAW
        self.pitch = self._DEFAULT_PITCH
        self.dist = self._DEFAULT_DIST
        if self.mode == "fly":
            self._sync_fly_from_orbit()
            self.setFocus()
        self._lowres = False
        self._img = None
        self.update()

    def _ensure_field(self):
        if self._world is None:
            return None
        R = self._world.s.preview_r
        key = (self.cx, self.cy, R)
        if self._field is None or self._field_key != key:
            if not self._world.preview_window_ready(self.cx, self.cy):
                return None
            self._field = WindowField(self._world, self.cx, self.cy, R)
            self._field_key = key
        return self._field

    def _request_frame(self):
        self._lowres = True
        self._need_render = True
        if not self._frame_timer.isActive():
            self._frame_timer.start()

    def _sync_fly_from_orbit(self):
        src = self._ensure_field()
        if src is None:
            return
        eye = orbit_eye(self.yaw, self.pitch, self.dist)
        fwd = orbit_forward(src, self.cx, self.cy, self._world.s.preview_r, eye)
        self.fly_pos = eye
        self.fly_yaw = math.atan2(fwd[0], fwd[2])
        self.fly_pitch = math.asin(clamp(fwd[1], -1.0, 1.0))

    def _camera(self):
        src = self._ensure_field()
        if src is None:
            return None, None
        if self.mode == "fly":
            return self.fly_pos, look_forward(self.fly_yaw, self.fly_pitch)
        eye = orbit_eye(self.yaw, self.pitch, self.dist)
        return eye, orbit_forward(src, self.cx, self.cy, self._world.s.preview_r, eye)

    def _stop_fly(self):
        self._fly_timer.stop()
        self._keys.clear()

    def _end_interaction(self):
        self._lowres = False
        self._need_render = True
        self._frame_timer.stop()
        self._img = None
        self.update()

    def _fly_tick(self):
        now = time.monotonic()
        dt = min(0.05, now - self._last_tick) if self._last_tick else 0.016
        self._last_tick = now
        fx, fy, fz = look_forward(self.fly_yaw, self.fly_pitch)
        rx, rz = -fz, fx
        rl = math.hypot(rx, rz) or 1.0
        rx /= rl; rz /= rl
        K = _FLY_KEYS
        held = self._keys
        mx = my = mz = 0.0
        if K["W"] in held: mx += fx; my += fy; mz += fz
        if K["S"] in held: mx -= fx; my -= fy; mz -= fz
        if K["D"] in held: mx += rx; mz += rz
        if K["A"] in held: mx -= rx; mz -= rz
        if K["E"] in held or K["Space"] in held: my += 1.0
        if K["Q"] in held or K["C"] in held: my -= 1.0
        ml = math.sqrt(mx * mx + my * my + mz * mz)
        if ml > 0:
            step = self.fly_speed * (3.0 if K["Shift"] in held else 1.0) * dt / ml
            px, py, pz = self.fly_pos
            self.fly_pos = (
                clamp(px + mx * step, -4.0, 4.0),
                clamp(py + my * step, -0.2, 2.0),
                clamp(pz + mz * step, -4.0, 4.0),
            )
        self._request_frame()

    def keyPressEvent(self, e):
        if self.mode == "fly" and not e.isAutoRepeat() and e.key() in _FLY_KEY_VALUES:
            self._keys.add(e.key())
            if not self._fly_timer.isActive():
                self._last_tick = 0.0
                self._lowres_timer.stop()
                self._fly_timer.start()
            return
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        if e.key() in self._keys and not e.isAutoRepeat():
            self._keys.discard(e.key())
            if not self._keys:
                self._fly_timer.stop()
                self._lowres_timer.start()
            return
        super().keyReleaseEvent(e)

    def focusOutEvent(self, e):
        self._stop_fly()
        self._lowres_timer.start()
        super().focusOutEvent(e)

    def set_placeholder(self, on, pct=0):
        if on == self.placeholder and pct == self.progress_pct:
            return
        self.placeholder = on
        self.progress_pct = pct
        self._img = None
        self._last_size = (0, 0)
        self.update()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self._img = None
        self._last_size = (0, 0)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        p.fillRect(self.rect(), QColor("#0a1017"))

        if self.placeholder or self._world is None:
            w, h = self.width(), self.height()
            p.setPen(QPen(QColor(20, 28, 40), 2))
            for x in range(-h, w, 20):
                p.drawLine(x, 0, x + h, h)
            p.setPen(QColor("#e6b467"))
            p.setFont(QFont("monospace", 11))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                       f"waiting for chunks…\nworld {self.progress_pct}% generated")
            p.setPen(QPen(QColor(255, 255, 255, 26), 1))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRect(self.rect().adjusted(0, 0, -1, -1))
            return

        if self._need_render or self._img is None or self._last_size != (self.width(), self.height()):
            self._render()

        if self._img is not None:
            p.drawImage(self.rect(), self._img)

        p.setPen(QPen(QColor(255, 255, 255, 26), 1))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawRect(self.rect().adjusted(0, 0, -1, -1))

    def _render(self):
        src = self._ensure_field()
        if src is None:
            return
        interactive = self._lowres
        scale = self._scale if interactive else 1.0
        w = max(4, int(self.width() / scale))
        h = max(4, int(self.height() / scale))
        self._last_size = (self.width(), self.height())
        eye, fwd = self._camera()
        R = self._world.s.preview_r
        steps = self._INTERACT_STEPS if interactive else 150
        refine = self._INTERACT_REFINE if interactive else 7
        t0 = time.perf_counter()
        arr = np.ascontiguousarray(ray_march(
            src, w, h, self.cx, self.cy, R, eye, fwd, max_steps=steps, refine=refine,
        ))
        dt = time.perf_counter() - t0
        if interactive:
            if dt > self._FRAME_BUDGET:
                self._scale = min(self._MAX_SCALE, self._scale * 1.3)
            elif dt < self._FRAME_BUDGET * 0.4 and self._scale > self._MIN_SCALE:
                self._scale = max(self._MIN_SCALE, self._scale / 1.15)
        self._img = QImage(arr.data, w, h, w * 3, QImage.Format.Format_RGB888).copy()
        self._need_render = False

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self.setFocus()
            self._drag = e.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, e):
        if self._drag is not None and (e.buttons() & Qt.MouseButton.LeftButton):
            d = e.position() - self._drag
            self._drag = e.position()
            if self.mode == "fly":
                self.fly_yaw -= d.x() * 0.008
                self.fly_pitch = clamp(self.fly_pitch - d.y() * 0.006,
                                       -self._FLY_PITCH_LIMIT, self._FLY_PITCH_LIMIT)
            else:
                self.yaw -= d.x() * 0.008
                self.pitch = clamp(self.pitch + d.y() * 0.006, 0.06, 1.35)
            self._request_frame()

    def mouseReleaseEvent(self, _):
        self._drag = None
        self.setCursor(Qt.CursorShape.ArrowCursor)
        self._end_interaction()

    def wheelEvent(self, e):
        delta = 1 if e.angleDelta().y() > 0 else -1
        if self.mode == "fly":
            self.fly_speed = clamp(self.fly_speed * (1.2 if delta > 0 else 1 / 1.2), 0.05, 4.0)
        else:
            self.dist = clamp(self.dist * (1 - delta * 0.09), 1.3, 7.5)
        self._request_frame()
        self._lowres_timer.start()

METHOD_CHOICES = [
    ("fbm", "Fractal noise (fBm)"),
    ("ridged", "Ridged mountains"),
    ("warp", "Domain-warped noise"),
    ("voronoi", "Voronoi cells"),
    ("tectonic", "Tectonic plates"),
    ("eroded", "Hydraulic erosion"),
    ("billow", "Billow clouds"),
    ("terraces", "Terraced steps"),
    ("cellular", "Cellular domes"),
    ("plateau", "Plateaus & mesas"),
    ("ranges", "Mountain ranges"),
    ("turbulence", "Turbulent flow"),
    ("hills", "Rolling hills"),
]

# Full table kept so existing share codes still decode; only MENU_METHODS is offered.
MENU_METHODS = ["fbm", "tectonic", "ranges", "voronoi"]
METHOD_ORDER = ["fbm", "ridged", "warp", "voronoi", "tectonic", "eroded", "billow", "terraces",
                "cellular", "plateau", "ranges", "turbulence", "hills"]

START_CSS = """
#startRoot{background:#07090d;}
QWidget{color:#cfd9e6;font-family:'Inter','Segoe UI','Helvetica Neue',Arial,sans-serif;font-size:12px;}
QLabel{background:transparent;color:#9fb0c3;}
QLabel#title{color:#8fe6c0;font-size:24px;font-weight:700;letter-spacing:5px;}
QLabel#subtitle{color:#6d8298;font-size:12px;}
QLabel#hint{color:#5d7089;font-size:11px;}
QLabel#value{color:#8fe6c0;font-family:monospace;}
QLabel#summary{color:#8fe6c0;font-family:monospace;font-size:11px;}
QGroupBox{background:#0b1016;border:1px solid #16202d;border-radius:10px;margin-top:16px;padding:16px 14px 12px 14px;}
QGroupBox::title{subcontrol-origin:margin;subcontrol-position:top left;left:12px;padding:0 6px;color:#8fe6c0;font-size:10px;font-weight:700;letter-spacing:2px;}
QComboBox,QSpinBox,QDoubleSpinBox{background:#0f151d;color:#dce6f2;border:1px solid #22303f;border-radius:5px;padding:4px 8px;min-height:22px;selection-background-color:#12261d;}
QComboBox:hover,QSpinBox:hover,QDoubleSpinBox:hover{border-color:#2f4a5e;}
QComboBox::drop-down{border:0;width:18px;}
QComboBox QAbstractItemView{background:#0f151d;color:#dce6f2;border:1px solid #22303f;selection-background-color:#12261d;selection-color:#8fe6c0;}
QSlider::groove:horizontal{height:4px;background:#1a2634;border-radius:2px;}
QSlider::sub-page:horizontal{background:#3b8f70;border-radius:2px;}
QSlider::handle:horizontal{background:#8fe6c0;width:14px;height:14px;margin:-5px 0;border-radius:7px;}
QTabWidget::pane{border:1px solid #16202d;border-radius:10px;background:#0b1016;top:-1px;}
QTabBar::tab{background:#0a0e14;color:#6d8298;padding:8px 18px;border:1px solid #16202d;border-bottom:none;border-top-left-radius:7px;border-top-right-radius:7px;margin-right:3px;}
QTabBar::tab:selected{background:#0b1016;color:#8fe6c0;}
QTabBar::tab:hover:!selected{color:#cfd9e6;}
QListWidget{background:#0b1016;border:1px solid #16202d;border-radius:10px;padding:6px;outline:0;}
QListWidget::item{padding:8px 10px;border-radius:6px;color:#cfd9e6;}
QListWidget::item:hover{background:#111a24;}
QListWidget::item:selected{background:#12261d;color:#8fe6c0;}
QPushButton{background:#111a24;color:#8fe6c0;border:1px solid #22303f;border-radius:7px;padding:7px 18px;font-size:12px;letter-spacing:1px;}
QPushButton:hover{background:#12261d;border-color:#3b6f5c;}
QPushButton:pressed{background:#0b1611;}
QPushButton#primary{background:#0e1a15;border-color:#25463a;padding:8px 26px;font-weight:700;}
QPushButton#primary:hover{background:#15301f;}
QPushButton#ghost{background:transparent;color:#6d8298;border:1px solid #22303f;padding:4px 10px;}
QPushButton#ghost:hover{color:#8fe6c0;}
QPushButton#dice{padding:4px 0;min-width:26px;max-width:30px;font-size:13px;}
"""


def preview_pixels(s: Settings, n: int = 160):
    w = World(s)
    c = (np.arange(n) + 0.5) / n
    FX, FY = np.meshgrid(c, c)
    h = np.asarray(w.sampler(FX, FY), np.float64)
    sea = max(w.sea_level, 1e-6)
    land = ramp_rgb(LAND_A, np.clip((h - w.sea_level) / max(1 - w.sea_level, 1e-6), 0, 1))
    wt = np.clip(h / sea, 0, 1)
    water = np.stack([lerp(12, 52, wt), lerp(24, 104, wt), lerp(52, 152, wt)], -1)
    out = np.where((h < w.sea_level)[..., None], water, land)
    return np.clip(out, 0, 255).astype(np.uint8)


class SliderSpin(QWidget):
    valueChanged = pyqtSignal(float)
    committed = pyqtSignal()
    _Q = 1000

    def __init__(self, lo, hi, step, decimals, value, suffix="", mul=1.0, parent=None):
        super().__init__(parent)
        self._mul = float(mul)
        self._v = float(value)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(10)
        self._lo = float(lo)
        self._hi = float(hi)
        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(round(lo * self._Q), round(hi * self._Q))
        self.slider.setSingleStep(max(1, round(step * self._Q)))
        self.spin = QDoubleSpinBox()
        self.spin.setRange(lo * self._mul, hi * self._mul)
        self.spin.setDecimals(decimals)
        self.spin.setSingleStep(step * self._mul)
        self.spin.setSuffix(suffix)
        self.spin.setFixedWidth(104)
        lay.addWidget(self.slider, 1)
        lay.addWidget(self.spin)
        self.dice = QPushButton("⟳")
        self.dice.setObjectName("dice")
        self.dice.setToolTip("Randomize this value")
        self.dice.clicked.connect(self.randomize)
        lay.addWidget(self.dice)
        self.lock = QPushButton("🔓")
        self.lock.setObjectName("dice")
        self.lock.setCheckable(True)
        self.lock.setToolTip("Lock: Randomize all keeps this value")
        self.lock.toggled.connect(lambda on: self.lock.setText("🔒" if on else "🔓"))
        lay.addWidget(self.lock)
        self.slider.valueChanged.connect(self._on_slider)
        self.spin.valueChanged.connect(self._on_spin)
        self.slider.sliderReleased.connect(self.committed.emit)
        self.spin.editingFinished.connect(self.committed.emit)
        self.setValue(value)

    def _on_slider(self, q):
        v = q / self._Q
        self._v = v
        self.spin.blockSignals(True)
        self.spin.setValue(v * self._mul)
        self.spin.blockSignals(False)
        self.valueChanged.emit(v)

    def _on_spin(self, d):
        v = d / self._mul
        self._v = v
        self.slider.blockSignals(True)
        self.slider.setValue(round(v * self._Q))
        self.slider.blockSignals(False)
        self.valueChanged.emit(v)

    def value(self):
        return self._v

    def randomize(self, *_, emit=True):
        v = random.uniform(self._lo, self._hi)
        self.setValue(v)
        if emit:
            self.valueChanged.emit(v)
            self.committed.emit()

    def setValue(self, v):
        self._v = float(v)
        self.slider.blockSignals(True)
        self.slider.setValue(round(self._v * self._Q))
        self.slider.blockSignals(False)
        self.spin.blockSignals(True)
        self.spin.setValue(self._v * self._mul)
        self.spin.blockSignals(False)


MODE_CHOICES = [
    ("borderless", "Borderless window (default)"),
    ("fullscreen", "Fullscreen"),
    ("windowed", "Windowed"),
]

RES_CHOICES = [
    ("0x0", "Maximized"),
    ("1280x720", "1280 × 720"),
    ("1600x900", "1600 × 900"),
    ("1920x1080", "1920 × 1080"),
    ("2560x1440", "2560 × 1440"),
]


CODE_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
CODE_CHECK_BITS = 10


def _codec_fields():
    return [
        ("method", "choice", METHOD_ORDER),
        ("seed", "int", (0, 1, 2 ** 31)),
        ("num_chunks", "int", (2, 1, 511)),
        ("sea_percentile", "grid", (0.05, 0.01, 91)),
        ("shape_strength", "grid", (0.0, 0.01, 101)),
        ("landmass_count", "grid", (0.0, 0.01, 101)),
        ("landmass_size", "grid", (0.0, 0.01, 101)),
        ("coast_rough", "grid", (0.0, 0.01, 101)),
        ("inlets", "grid", (0.0, 0.01, 101)),
        ("lakes", "grid", (0.0, 0.01, 101)),
        ("elongation", "grid", (0.0, 0.01, 101)),
        ("edge_water", "grid", (0.0, 0.01, 101)),
        ("frequency", "grid", (0.4, 0.05, 53)),
        ("relief", "grid", (0.4, 0.05, 43)),
        ("tectonic", "grid", (0.0, 0.01, 101)),
        ("land_gamma", "grid", (0.5, 0.05, 31)),
        ("geology", "grid", (0.0, 0.01, 101)),
        ("detail", "grid", (0.0, 0.01, 101)),
        ("mask_strength", "grid", (0.0, 0.01, 101)),
        ("biome_blend", "grid", (0.0, 0.01, 101)),
        ("erosion", "grid", (0.0, 0.01, 101)),
    ]


def _radix(spec):
    return len(spec[2]) if spec[1] == "choice" else spec[2][2]


def _code_chars():
    total = 1
    for spec in _codec_fields():
        total *= _radix(spec)
    chars = -(-((total - 1).bit_length() + CODE_CHECK_BITS) // 5)
    return -(-chars // 4) * 4


CODE_CHARS = _code_chars()


def _field_index(spec, value):
    kind, arg = spec[1], spec[2]
    if kind == "choice":
        if value not in arg:
            raise ValueError(f"Unknown {spec[0]}: {value}")
        return arg.index(value)
    lo, step, count = arg
    k = round((float(value) - lo) / step)
    return int(min(max(k, 0), count - 1))


def _field_value(spec, k):
    kind, arg = spec[1], spec[2]
    if kind == "choice":
        return arg[k]
    lo, step, _count = arg
    if kind == "int":
        return int(lo + k * step)
    return round(lo + k * step, 4)


def _checksum(P):
    digest = hashlib.sha256(P.to_bytes(24, "big")).digest()
    return int.from_bytes(digest[:2], "big") & ((1 << CODE_CHECK_BITS) - 1)


def encode_code(s: "Settings") -> str:
    P = 0
    for spec in _codec_fields():
        P = P * _radix(spec) + _field_index(spec, getattr(s, spec[0]))
    T = (P << CODE_CHECK_BITS) | _checksum(P)
    chars = []
    for _ in range(CODE_CHARS):
        chars.append(CODE_ALPHABET[T & 31])
        T >>= 5
    text = "".join(reversed(chars))
    return "-".join(text[i:i + 4] for i in range(0, CODE_CHARS, 4))


def decode_code(text: str) -> "Settings":
    clean = re.sub(r"[\s-]", "", str(text)).upper()
    clean = clean.replace("O", "0").replace("I", "1").replace("L", "1")
    if len(clean) != CODE_CHARS:
        raise ValueError(f"A world code has {CODE_CHARS} characters (got {len(clean)}).")
    T = 0
    for ch in clean:
        idx = CODE_ALPHABET.find(ch)
        if idx < 0:
            raise ValueError(f"'{ch}' is not a valid world code character.")
        T = T * 32 + idx
    P = T >> CODE_CHECK_BITS
    if (T & ((1 << CODE_CHECK_BITS) - 1)) != _checksum(P):
        raise ValueError("That world code has a typo (checksum mismatch).")
    values = {}
    for spec in reversed(_codec_fields()):
        r = _radix(spec)
        values[spec[0]] = _field_value(spec, P % r)
        P //= r
    if P != 0:
        raise ValueError("That world code is not valid.")
    return Settings(**values)


def snap_settings(s: "Settings") -> "Settings":
    return decode_code(encode_code(s))


def _to_int(v, default=0):
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


_POOL_WORLD = None


def _pool_init(settings_dict, sea):
    global _POOL_WORLD
    _POOL_WORLD = World(Settings.from_dict(settings_dict))
    _POOL_WORLD.sea_level = sea


def _pool_chunk(key):
    ch = _POOL_WORLD.generate_chunk(key[0], key[1])
    return key, ch["height"], ch["biome"]


def _combo(choices):
    c = QComboBox()
    for key, label in choices:
        c.addItem(label, key)
    return c


def _hwrap(*widgets):
    w = QWidget()
    h = QHBoxLayout(w)
    h.setContentsMargins(0, 0, 0, 0)
    h.setSpacing(8)
    for i, x in enumerate(widgets):
        h.addWidget(x, 1 if i == 0 else 0)
    return w


_TAGS = {
    "live": ('<span style="color:#8fe6c0;">● live</span>', "appears in the preview"),
    "gen": ('<span style="color:#e0a050;">○ after generate</span>', "only applies when the world is built"),
    "app": ('<span style="color:#5d7089;">○ app</span>', "window setting, not terrain"),
}


def _frow(form, text, widget, tag):
    lab = QLabel(f'{text}:  <span style="font-size:10px;">{_TAGS[tag][0]}</span>')
    lab.setTextFormat(Qt.TextFormat.RichText)
    lab.setToolTip(_TAGS[tag][1])
    form.addRow(lab, widget)


def _group(title):
    box = QGroupBox(title.upper())
    form = QFormLayout(box)
    form.setLabelAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    form.setHorizontalSpacing(16)
    form.setVerticalSpacing(10)
    form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.AllNonFixedFieldsGrow)
    return box, form


def _hint(text):
    h = QLabel(text)
    h.setObjectName("hint")
    h.setWordWrap(True)
    return h


class StartMenu(QWidget):
    start_clicked = pyqtSignal(Settings)
    load_clicked = pyqtSignal()
    display_changed = pyqtSignal(str, int, int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._quiet = False
        self.setObjectName("startRoot")
        self.setStyleSheet(START_CSS)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(40, 28, 40, 22)
        outer.setSpacing(12)

        head = QVBoxLayout()
        head.setSpacing(4)
        title = QLabel("◈  WORLDGEN LAB")
        title.setObjectName("title")
        sub = QLabel("Tune the settings, check the live preview, then generate.")
        sub.setObjectName("subtitle")
        head.addWidget(title)
        head.addWidget(sub)
        outer.addLayout(head)

        rule = QFrame()
        rule.setFixedHeight(1)
        rule.setStyleSheet("background:#16202d;border:0;")
        outer.addWidget(rule)

        body = QHBoxLayout()
        body.setSpacing(22)
        outer.addLayout(body, 1)

        side = QWidget()
        side.setFixedWidth(300)
        sv = QVBoxLayout(side)
        sv.setContentsMargins(0, 0, 0, 0)
        sv.setSpacing(14)
        pbox = QGroupBox("PREVIEW")
        pl = QVBoxLayout(pbox)
        pl.setSpacing(10)
        self.preview_img = QLabel("Preview updates when you release a slider.")
        self.preview_img.setWordWrap(True)
        self.preview_img.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview_img.setFixedSize(268, 268)
        self.preview_img.setStyleSheet(
            "background:#05070a;border:1px solid #16202d;border-radius:6px;color:#5d7089;")
        pl.addWidget(self.preview_img, 0, Qt.AlignmentFlag.AlignCenter)
        pl.addWidget(_hint("Shows terrain height and water. Settings tagged ○ are applied only "
                           "when the world generates."))
        sv.addWidget(pbox)

        dbox = QGroupBox("DERIVED")
        df = QFormLayout(dbox)
        df.setVerticalSpacing(8)
        self._dv = {}
        for key in ("World size", "Total chunks", "Overview image",
                    "Preview window", "River threshold"):
            lab = QLabel()
            lab.setObjectName("value")
            lab.setWordWrap(True)
            df.addRow(key + ":", lab)
            self._dv[key] = lab
        sv.addWidget(dbox)
        sv.addStretch(1)
        body.addWidget(side)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setStyleSheet("QScrollArea{background:#07090d;border:0;}")
        scroll.viewport().setStyleSheet("background:#07090d;")
        inner = QWidget()
        inner.setStyleSheet("background:#07090d;")
        col = QVBoxLayout(inner)
        col.setContentsMargins(4, 4, 14, 8)
        col.setSpacing(14)
        legend = QLabel("<span style='color:#8fe6c0;'>● live</span> &nbsp;appears in the preview"
                        "&nbsp;&nbsp;&nbsp;<span style='color:#e0a050;'>○ after generate</span>"
                        " &nbsp;only applies when the world is built"
                        "&nbsp;&nbsp;&nbsp;<span style='color:#5d7089;'>○ app</span>"
                        " &nbsp;window setting, not terrain")
        legend.setTextFormat(Qt.TextFormat.RichText)
        legend.setObjectName("hint")
        col.addWidget(legend)
        body.addWidget(scroll, 1)
        scroll.setWidget(inner)

        def section(title, hint_text=None):
            box, f = _group(title)
            col.addWidget(box)
            if hint_text:
                f.addRow(_hint(hint_text))
            return f

        f = section("Seed & algorithm")
        self.method_combo = _combo([c for c in METHOD_CHOICES if c[0] in MENU_METHODS])
        self.method_dice = QPushButton("⟳")
        self.method_dice.setObjectName("dice")
        self.method_dice.setToolTip("Pick a different algorithm")
        self.method_dice.clicked.connect(self._randomize_method)
        self.method_lock = QPushButton("🔓")
        self.method_lock.setObjectName("dice")
        self.method_lock.setCheckable(True)
        self.method_lock.setToolTip("Lock: Randomize all keeps this algorithm")
        self.method_lock.toggled.connect(lambda on: self.method_lock.setText("🔒" if on else "🔓"))
        self.seed_spin = QSpinBox()
        self.seed_spin.setRange(0, 2_147_483_647)
        self.seed_spin.setValue(int(time.time() * 1000) % 1_000_000_000)
        self.dice_btn = QPushButton("Randomize")
        self.dice_btn.setObjectName("ghost")
        self.dice_btn.clicked.connect(self._randomize_seed)
        _frow(f, "Algorithm", _hwrap(self.method_combo, self.method_dice, self.method_lock), "live")
        _frow(f, "Seed", _hwrap(self.seed_spin, self.dice_btn), "live")

        f = section("Terrain shape", "Frequency rescales every feature. Relief stretches "
                                     "elevation away from sea level without changing water share.")
        self.freq_slider = SliderSpin(0.4, 3.0, 0.05, 2, 1.0, "×")
        self.relief_slider = SliderSpin(0.4, 2.5, 0.05, 2, 1.0, "×")
        _frow(f, "Feature frequency", self.freq_slider, "live")
        _frow(f, "Relief", self.relief_slider, "live")

        f = section("Surface detail", "Detail adds fine relief that is strongest on highlands. "
                                      "Geology sets how much rock types differ. Mask strength confines "
                                      "detail and erosion to high ground. Biome blend softens class "
                                      "boundaries. Land curve sets how often high ground occurs.")
        self.gamma_slider = SliderSpin(0.5, 2.0, 0.05, 2, 1.0, "×")
        self.detail_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.geo_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.mask_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.blend_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.erosion_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        _frow(f, "Land height curve", self.gamma_slider, "live")
        _frow(f, "Fine detail", self.detail_slider, "live")
        _frow(f, "Geology contrast", self.geo_slider, "live")
        _frow(f, "Mask strength", self.mask_slider, "live")
        _frow(f, "Biome blend", self.blend_slider, "live")
        _frow(f, "Erosion and rivers", self.erosion_slider, "live")

        f = section("Water", "Water coverage is the share of the surface below sea level. Sea "
                             "level is solved from the terrain, so the target holds for any seed.")
        self.water_slider = SliderSpin(0.05, 0.95, 0.01, 0, 0.42, "%", mul=100)
        _frow(f, "Water coverage", self.water_slider, "live")

        f = section("Landmass layout", "Layout strength 0% gives natural terrain. Above 0%, the "
                                       "sliders below shape where land forms.")
        self.shape_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.0, "%", mul=100)
        self.count_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.lsize_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.coast_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.5, "%", mul=100)
        self.lake_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.2, "%", mul=100)
        self.edge_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.0, "%", mul=100)
        _frow(f, "Layout strength", self.shape_slider, "live")
        _frow(f, "Landmass count", self.count_slider, "live")
        _frow(f, "Landmass size", self.lsize_slider, "live")
        _frow(f, "Coast ruggedness", self.coast_slider, "live")
        _frow(f, "Inland lakes", self.lake_slider, "live")
        _frow(f, "Edge water", self.edge_slider, "live")

        f = section("Tectonics")
        self.tectonic_slider = SliderSpin(0.0, 1.0, 0.01, 0, 0.0, "%", mul=100)
        _frow(f, "Tectonic activity", self.tectonic_slider, "live")

        f = section("World size", "The world is a square grid of chunks. Chunks per side sets the "
                                  "total size.")
        self.num_chunks_spin = QSpinBox()
        self.num_chunks_spin.setRange(2, 512)
        self.num_chunks_spin.setValue(10)
        _frow(f, "Chunks per side", self.num_chunks_spin, "gen")

        f = section("Window", "Borderless fills the work area with no frame. Fullscreen covers "
                              "the monitor. Esc returns to windowed; F11 toggles borderless.")
        self.mode_combo = _combo(MODE_CHOICES)
        self.res_combo = _combo(RES_CHOICES)
        _frow(f, "Display mode", self.mode_combo, "app")
        _frow(f, "Windowed size", self.res_combo, "app")
        self.mode_combo.currentIndexChanged.connect(self._emit_display)
        self.res_combo.currentIndexChanged.connect(self._emit_display)

        f = section("World code", "Codes encode every setting and the seed, so the same code "
                                  "always rebuilds the same world.")
        self.code_label = QLabel()
        self.code_label.setObjectName("value")
        self.code_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.code_label.setWordWrap(True)
        copy_btn = QPushButton("Copy")
        copy_btn.setObjectName("ghost")
        copy_btn.clicked.connect(self._copy_code)
        f.addRow("Current:", _hwrap(self.code_label, copy_btn))
        self.code_edit = QLineEdit()
        self.code_edit.setPlaceholderText("Paste or type a world code")
        self.code_edit.returnPressed.connect(self._apply_code)
        apply_btn = QPushButton("Apply code")
        apply_btn.setObjectName("ghost")
        apply_btn.clicked.connect(self._apply_code)
        f.addRow("Load code:", _hwrap(self.code_edit, apply_btn))
        self.code_status = _hint("")
        f.addRow(self.code_status)
        save_set_btn = QPushButton("Save settings…")
        save_set_btn.setObjectName("ghost")
        save_set_btn.clicked.connect(self._save_settings)
        load_set_btn = QPushButton("Load settings…")
        load_set_btn.setObjectName("ghost")
        load_set_btn.clicked.connect(self._load_settings)
        f.addRow("Settings file:", _hwrap(save_set_btn, load_set_btn))
        col.addStretch(1)

        bar = QHBoxLayout()
        bar.setSpacing(10)
        self.summary = QLabel()
        self.summary.setObjectName("summary")
        self.load_btn = QPushButton("Load world…")
        self.load_btn.setMinimumHeight(38)
        self.load_btn.clicked.connect(self.load_clicked.emit)
        self.gen_btn = QPushButton("Generate world →")
        self.gen_btn.setObjectName("primary")
        self.gen_btn.setMinimumHeight(38)
        self.gen_btn.clicked.connect(self._emit_settings)
        self.rand_all_btn = QPushButton("🎲 Randomize all")
        self.rand_all_btn.setMinimumHeight(38)
        self.rand_all_btn.clicked.connect(self._randomize_all)
        self.preview_btn = QPushButton("Refresh preview")
        self.preview_btn.setMinimumHeight(38)
        self.preview_btn.clicked.connect(self._show_preview)
        bar.addWidget(self.summary, 1)
        bar.addWidget(self.load_btn)
        bar.addWidget(self.rand_all_btn)
        bar.addWidget(self.preview_btn)
        bar.addWidget(self.gen_btn)
        outer.addLayout(bar)

        self._sliders = (self.freq_slider, self.relief_slider, self.water_slider,
                         self.shape_slider, self.count_slider, self.lsize_slider,
                         self.coast_slider, self.lake_slider,
                         self.tectonic_slider, self.edge_slider,
                         self.gamma_slider, self.detail_slider, self.geo_slider,
                         self.mask_slider, self.blend_slider, self.erosion_slider)
        self.seed_spin.valueChanged.connect(self._on_form_changed)
        self.num_chunks_spin.valueChanged.connect(self._on_form_changed)
        for w in self._sliders:
            w.valueChanged.connect(self._on_form_changed)
            w.committed.connect(self._show_preview)
        self.method_combo.currentIndexChanged.connect(self._on_form_changed)
        self.method_combo.currentIndexChanged.connect(self._show_preview)
        self.seed_spin.editingFinished.connect(self._show_preview)

        self.apply_settings(Settings(seed=int(self.seed_spin.value())))

    def _randomize_seed(self):
        self.seed_spin.setValue(random.randint(0, 2_147_483_647))
        self._show_preview()

    def _randomize_method(self):
        n = self.method_combo.count()
        cur = self.method_combo.currentIndex()
        if n > 1:
            pick = random.randrange(n - 1)
            self.method_combo.setCurrentIndex(pick + (pick >= cur))
        self._show_preview()

    def _randomize_all(self):
        self._quiet = True
        self.seed_spin.setValue(random.randint(0, 2_147_483_647))
        if not self.method_lock.isChecked():
            self.method_combo.setCurrentIndex(random.randrange(self.method_combo.count()))
        for w in self._sliders:
            if not w.lock.isChecked():
                w.randomize(emit=False)
        self._quiet = False
        self._on_form_changed()
        self._show_preview()

    def set_display(self, mode, w, h):
        for c in (self.mode_combo, self.res_combo):
            c.blockSignals(True)
        self.mode_combo.setCurrentIndex(max(0, self.mode_combo.findData(mode)))
        key = f"{w}x{h}" if w and h else "0x0"
        self.res_combo.setCurrentIndex(max(0, self.res_combo.findData(key)))
        for c in (self.mode_combo, self.res_combo):
            c.blockSignals(False)
        self.res_combo.setEnabled(mode == "windowed")

    def _emit_display(self, *_):
        mode = self.mode_combo.currentData()
        w, h = (int(x) for x in (self.res_combo.currentData() or "0x0").split("x"))
        self.res_combo.setEnabled(mode == "windowed")
        self.display_changed.emit(mode, w, h)

    def _on_form_changed(self, *_):
        self._update_derived()

    def _update_derived(self):
        s = self._collect()
        self.code_label.setText(encode_code(s))
        ww = s.world_w
        self._dv["World size"].setText(f"{ww:,} × {ww:,} cells")
        self._dv["Total chunks"].setText(f"{s.total_chunks:,}  ({s.num_chunks} × {s.num_chunks})")
        self._dv["Overview image"].setText(
            f"{s.overview_size} × {s.overview_size} px  ({s.overview_ts} px / chunk)")
        self._dv["Preview window"].setText(f"{2 * s.preview_r} × {2 * s.preview_r} cells")
        self._dv["River threshold"].setText("top 1.5 % of land by drainage")
        self.summary.setText(f"{ww:,}² cells · {s.total_chunks:,} chunks · "
                             f"{s.overview_size}² overview")

    def _collect(self):
        return snap_settings(Settings(
            method=self.method_combo.currentData(),
            seed=int(self.seed_spin.value()),
            num_chunks=int(self.num_chunks_spin.value()),
            sea_percentile=self.water_slider.value(),
            shape_strength=self.shape_slider.value(),
            landmass_count=self.count_slider.value(),
            landmass_size=self.lsize_slider.value(),
            coast_rough=self.coast_slider.value(),
            inlets=self.coast_slider.value(),
            lakes=self.lake_slider.value(),
            edge_water=self.edge_slider.value(),
            frequency=self.freq_slider.value(),
            relief=self.relief_slider.value(),
            tectonic=self.tectonic_slider.value(),
            land_gamma=self.gamma_slider.value(),
            detail=self.detail_slider.value(),
            geology=self.geo_slider.value(),
            mask_strength=self.mask_slider.value(),
            biome_blend=self.blend_slider.value(),
            erosion=self.erosion_slider.value(),
        ))

    def _save_settings(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save settings", "worldgen_settings.json", "Settings (*.json)")
        if not path:
            return
        Path(path).write_text(json.dumps(self._collect().to_dict(), indent=2), encoding="utf-8")
        self.code_status.setText("Settings saved.")

    def _load_settings(self):
        path, _ = QFileDialog.getOpenFileName(self, "Load settings", "", "Settings (*.json)")
        if not path:
            return
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
            self.apply_settings(snap_settings(Settings.from_dict(data)))
            self.code_status.setText("Settings loaded.")
        except (OSError, ValueError, TypeError) as exc:
            self.code_status.setText(f"Could not load settings: {exc}")

    def _show_preview(self, *_):
        if self._quiet:
            return
        px = preview_pixels(self._collect(), 160)
        h, w = px.shape[:2]
        img = QImage(px.tobytes(), w, h, w * 3, QImage.Format.Format_RGB888).copy()
        pix = QPixmap.fromImage(img).scaled(
            228, 228, Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation)
        self.preview_img.setPixmap(pix)

    def _copy_code(self):
        QApplication.clipboard().setText(self.code_label.text())
        self._code_message("Copied to clipboard.", ok=True)

    def _code_message(self, text, ok):
        self.code_status.setText(text)
        self.code_status.setStyleSheet("color:#8fe6c0;" if ok else "color:#e07a6a;")

    def _apply_code(self):
        text = self.code_edit.text().strip()
        if not text:
            return
        try:
            s = decode_code(text)
        except ValueError as e:
            self._code_message(str(e), ok=False)
            return
        self.apply_settings(s)
        self.code_edit.clear()
        self._code_message(f"Loaded world {encode_code(s)}.", ok=True)

    def _emit_settings(self):
        self.start_clicked.emit(self._collect())

    def apply_settings(self, s: Settings):
        self._quiet = True
        if self.method_combo.findData(s.method) < 0:
            name = dict(METHOD_CHOICES).get(s.method, s.method)
            self.method_combo.addItem(name, s.method)
        self.method_combo.setCurrentIndex(self.method_combo.findData(s.method))
        self.seed_spin.setValue(int(s.seed))
        self.num_chunks_spin.setValue(int(s.num_chunks))
        self.water_slider.setValue(s.sea_percentile)
        self.shape_slider.setValue(s.shape_strength)
        self.count_slider.setValue(s.landmass_count)
        self.lsize_slider.setValue(s.landmass_size)
        self.coast_slider.setValue(s.coast_rough)
        self.lake_slider.setValue(s.lakes)
        self.edge_slider.setValue(s.edge_water)
        self.freq_slider.setValue(s.frequency)
        self.relief_slider.setValue(s.relief)
        self.tectonic_slider.setValue(s.tectonic)
        self.gamma_slider.setValue(s.land_gamma)
        self.detail_slider.setValue(s.detail)
        self.geo_slider.setValue(s.geology)
        self.mask_slider.setValue(s.mask_strength)
        self.blend_slider.setValue(s.biome_blend)
        self.erosion_slider.setValue(s.erosion)
        self._quiet = False
        self._on_form_changed()
        self._show_preview()


class Explorer(QWidget):
    save_requested = pyqtSignal()
    back_requested = pyqtSignal()

    def __init__(self, settings: Settings, restored=None, parent=None):
        super().__init__(parent)
        self.s = settings
        self.world = World(settings)
        self.world_cx = settings.world_w * 0.5
        self.world_cy = settings.world_w * 0.5
        self.gen_cx = 0
        self.gen_cy = 0
        self.gen_active = True
        self.gen_start = time.time()

        if restored is not None:
            overview, chunk_ready, sea, rt, done, lowres = restored
            self.world.regenerate_from_loaded(overview, chunk_ready, sea, rt, done, lowres)
            self._seek_to_first_unready()
            self.gen_active = self.world.done < self.world.total

        self._build_ui()
        if self.gen_active:
            self._start_generation()
        else:
            self._finish_ui_immediately()

    def _seek_to_first_unready(self):
        for cy in range(self.s.chunks):
            for cx in range(self.s.chunks):
                if not self.world.chunk_ready[cy, cx]:
                    self.gen_cx = cx
                    self.gen_cy = cy
                    return
        self.gen_cx = self.s.chunks
        self.gen_cy = self.s.chunks

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        header = QFrame()
        header.setStyleSheet("background:#0a0e14;border-bottom:1px solid #16202d;")
        h = QHBoxLayout(header)
        h.setContentsMargins(18, 14, 18, 14)
        h.setSpacing(12)

        back_btn = QPushButton("← Menu")
        back_btn.setStyleSheet(
            "QPushButton{background:#111a24;color:#8fe6c0;border:1px solid #22303f;"
            "border-radius:5px;padding:4px 10px;}"
            "QPushButton:hover{background:#12261d;}"
        )
        back_btn.clicked.connect(self.back_requested.emit)
        h.addWidget(back_btn)

        save_btn = QPushButton("💾 Save")
        save_btn.setStyleSheet(back_btn.styleSheet())
        save_btn.clicked.connect(self.save_requested.emit)
        export_btn = QPushButton("⇪ Export…")
        export_btn.setStyleSheet(back_btn.styleSheet())
        export_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        export_btn.clicked.connect(self._export)
        h.addWidget(save_btn)
        h.addWidget(export_btn)

        mode_label = "SHAPED LAYOUT" if self.s.shape_strength > 0.001 else "NATURAL TERRAIN"
        title = QLabel(f"◈ {mode_label} · "
                       f"{self.s.method.upper()} · seed {self.s.seed}")
        title.setStyleSheet("color:#8fe6c0;font-weight:700;letter-spacing:1px;")
        h.addWidget(title)
        code_lbl = QLabel(encode_code(self.s))
        code_lbl.setStyleSheet("color:#6d8298;font-family:monospace;font-size:10px;")
        code_lbl.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        h.addWidget(code_lbl)

        self.progress_label = QLabel(f"{self.world.done} / {self.world.total}")
        self.progress_label.setStyleSheet("color:#6d8298;font-size:10px;")
        h.addWidget(self.progress_label)

        self.bar = QFrame()
        self.bar.setFixedSize(180, 8)
        self.bar.setStyleSheet("background:#0a1017;border:1px solid #1a2634;border-radius:4px;")
        self.fill = QFrame(self.bar)
        self.fill.setGeometry(0, 0, 0, 8)
        self.fill.setStyleSheet(
            "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #2b5c4a,stop:0.5 #3b8f70,stop:1 #8fe6c0);border-radius:4px;"
        )
        h.addWidget(self.bar)

        self.pct_label = QLabel("0%")
        self.pct_label.setStyleSheet("color:#8fe6c0;min-width:42px;text-align:right;")
        h.addWidget(self.pct_label)

        h.addStretch(1)

        self.status_label = QLabel("starting…")
        self.status_label.setStyleSheet("color:#6d8298;font-size:11px;")
        h.addWidget(self.status_label)

        layout.addWidget(header)

        body = QHBoxLayout()
        body.setContentsMargins(18, 18, 18, 24)
        body.setSpacing(12)
        layout.addLayout(body, 1)

        self.minimap = MinimapWidget()
        self.minimap.configure(self.s.world_w, self.s.chunks, self.s.preview_r)
        self.minimap.moved.connect(self._on_map_moved)
        self.minimap.released.connect(self._on_map_released)
        self.minimap.set_overview(self.world.overview)
        self.minimap.set_preview(self.world_cx, self.world_cy, False)
        body.addWidget(self.minimap, 3)

        right = QVBoxLayout()
        right.setSpacing(12)
        body.addLayout(right, 2)

        cam_style = back_btn.styleSheet() + \
            "QPushButton:checked{background:#12261d;border-color:#3b6f5c;}"
        view_row = QHBoxLayout()
        view_row.setSpacing(8)
        view_lbl = QLabel("Map view")
        view_lbl.setStyleSheet("color:#8fa0b4;")
        self.view_combo = _combo(VIEW_MODES)
        self.view_combo.setCurrentIndex(max(0, self.view_combo.findData(self.world.view_mode)))
        self.view_combo.currentIndexChanged.connect(self._on_view_mode)
        view_row.addWidget(view_lbl)
        view_row.addWidget(self.view_combo, 1)
        right.addLayout(view_row)

        cam_row = QHBoxLayout()
        cam_row.setSpacing(8)
        self.orbit_btn = QPushButton("Orbit")
        self.fly_btn = QPushButton("Fly")
        self.reset_cam_btn = QPushButton("Reset camera")
        for b in (self.orbit_btn, self.fly_btn, self.reset_cam_btn):
            b.setStyleSheet(cam_style)
            b.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.orbit_btn.setCheckable(True)
        self.fly_btn.setCheckable(True)
        self.orbit_btn.setChecked(True)
        self.orbit_btn.clicked.connect(lambda: self._set_camera_mode("orbit"))
        self.fly_btn.clicked.connect(lambda: self._set_camera_mode("fly"))
        self.reset_cam_btn.clicked.connect(self._reset_camera)
        cam_row.addWidget(self.orbit_btn)
        cam_row.addWidget(self.fly_btn)
        cam_row.addWidget(self.reset_cam_btn)
        cam_row.addStretch(1)
        right.addLayout(cam_row)

        cam_hint = QLabel(
            "Fly: W/S forward · A/D strafe · E/Space up · Q/C down · "
            "Shift sprint · drag to look · wheel changes speed"
        )
        cam_hint.setWordWrap(True)
        cam_hint.setStyleSheet("color:#6d8298;font-size:10px;")
        right.addWidget(cam_hint)

        self.preview3d = Preview3DWidget()
        self.preview3d.set_world(self.world)
        self.preview3d.set_center(self.world_cx, self.world_cy)
        self.preview3d.set_placeholder(True, 0)
        right.addWidget(self.preview3d, 3)

        self.readout = QLabel("")
        self.readout.setWordWrap(True)
        self.readout.setStyleSheet(
            "background:#0a0e14;border:1px solid #16202d;border-radius:10px;"
            "padding:10px 12px;color:#cfe3d8;font-size:11px;"
        )
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.readout)
        scroll.setStyleSheet("background:transparent;border:none;")
        right.addWidget(scroll, 4)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self._tick)
        self.timer.setInterval(16)

    def _set_camera_mode(self, mode):
        self.orbit_btn.setChecked(mode == "orbit")
        self.fly_btn.setChecked(mode == "fly")
        self.preview3d.set_mode(mode)

    def _reset_camera(self):
        self.preview3d.reset_camera()

    def _start_generation(self):
        self.timer.start()
        self.status_label.setText("generating all chunks…")
        self._update_progress()

    def _finish_ui_immediately(self):
        self._update_progress()
        self.status_label.setText("ready (loaded)")
        self.preview3d.set_placeholder(False, 100)
        self.minimap.set_overview(self.world.overview)
        self._refresh_preview()
        self._update_readout()

    def _export(self):
        folder = QFileDialog.getExistingDirectory(self, "Export world to folder")
        if not folder:
            return
        s = self.s
        N, CH, K = s.world_w, s.chunk, s.chunks
        H = np.empty((N, N), np.float32)
        B = np.empty((N, N), np.uint8)
        keys = [(cx, cy) for cy in range(K) for cx in range(K)]
        dlg = QProgressDialog("Generating terrain for export…", "Cancel", 0, len(keys), self)
        dlg.setWindowModality(Qt.WindowModality.WindowModal)
        dlg.setMinimumDuration(0)
        done = 0
        with ProcessPoolExecutor(max_workers=os.cpu_count() or 1, initializer=_pool_init,
                                 initargs=(s.to_dict(), self.world.sea_level)) as ex:
            for batch_start in range(0, len(keys), 32):
                batch = keys[batch_start:batch_start + 32]
                for (cx, cy), hh, bb in ex.map(_pool_chunk, batch, chunksize=2):
                    H[cy * CH:(cy + 1) * CH, cx * CH:(cx + 1) * CH] = hh
                    B[cy * CH:(cy + 1) * CH, cx * CH:(cx + 1) * CH] = bb
                    done += 1
                dlg.setValue(done)
                QApplication.processEvents()
                if dlg.wasCanceled():
                    dlg.close()
                    return
        dlg.close()
        base = Path(folder) / f"world_{s.seed}"
        h16 = np.clip(H.astype(np.float64) * 65535.0, 0, 65535).astype("<u2")
        h16.tofile(base.with_suffix(".r16"))
        img = QImage(h16.tobytes(), N, N, N * 2, QImage.Format.Format_Grayscale16).copy()
        img.save(str(base) + "_height16.png")
        rgb = np.ascontiguousarray(BIOME_RGB_F[B].astype(np.uint8))
        QImage(rgb.tobytes(), N, N, N * 3, QImage.Format.Format_RGB888).copy().save(str(base) + "_biome.png")
        meta = {"seed": s.seed, "world_size": N, "sea_level": self.world.sea_level,
                "height_scale": "value/65535 of full range; sea_level is relative to the same range",
                "settings": s.to_dict()}
        Path(str(base) + "_export.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
        QMessageBox.information(self, "Export complete", f"Saved to:\n{base}_*")

    def _on_view_mode(self, *_):
        self.world.redraw_overview(self.view_combo.currentData())
        self.minimap.set_overview(self.world.overview)

    def _tick(self):
        if not self.gen_active:
            return
        t0 = time.time()
        budget = 0.014
        generated = 0
        total = self.world.total
        n = self.s.chunks

        while self.world.done < total:
            if time.time() - t0 > budget:
                break
            cx = self.gen_cx
            cy = self.gen_cy
            chunk = self.world.generate_chunk(cx, cy)
            img = self.world.render_chunk_to_overview(chunk, cx, cy)
            self.world.place_overview(cx, cy, img)
            self.world.chunk_ready[cy, cx] = True
            self.world.done += 1
            generated += 1
            self.gen_cx += 1
            if self.gen_cx >= n:
                self.gen_cx = 0
                self.gen_cy += 1
                if self.gen_cy >= n:
                    break

        if generated > 0:
            self.minimap.refresh()
            self.minimap.set_preview(
                self.world_cx, self.world_cy,
                self.world.preview_window_ready(self.world_cx, self.world_cy),
            )
            self._update_progress()

        if self.world.done >= total:
            self.gen_active = False
            self.timer.stop()
            self.status_label.setText("ready")
            self._update_progress()
            self.preview3d.set_placeholder(False, 100)
            self._update_readout()
        else:
            self.status_label.setText(f"generating ({self.gen_cx},{self.gen_cy})…")

    def _update_progress(self):
        pct = self.world.done / self.world.total
        self.fill.setGeometry(0, 0, int(self.bar.width() * pct), 8)
        self.pct_label.setText(f"{pct * 100:.0f}%")
        self.progress_label.setText(f"{self.world.done} / {self.world.total}")

    def _refresh_preview(self):
        ready = self.world.preview_window_ready(self.world_cx, self.world_cy)
        self.preview3d.set_center(self.world_cx, self.world_cy)
        self.preview3d.set_placeholder(
            not ready,
            int(self.world.done / self.world.total * 100),
        )
        self.minimap.set_preview(self.world_cx, self.world_cy, ready)

    def _on_map_moved(self, wx, wy):
        self.world_cx = clamp(wx, 0, self.s.world_w - 1)
        self.world_cy = clamp(wy, 0, self.s.world_w - 1)
        self._refresh_preview()
        self._update_readout()

    def _on_map_released(self):
        self._refresh_preview()

    def _update_readout(self):
        cx = int(clamp(self.world_cx, 0, self.s.world_w - 1))
        cy = int(clamp(self.world_cy, 0, self.s.world_w - 1))
        cell = self.world.sample_cell(cx, cy)
        if cell is None:
            self.readout.setText(
                "<b style='color:#8fe6c0'>location</b><br>"
                f"coords: {cx} , {cy}<br>"
                "<span style='color:#e6b467'>chunk not generated yet</span><br><br>"
                "<b style='color:#8fe6c0'>world</b><br>"
                f"{self.world.done} / {self.world.total} chunks"
            )
            return
        ch = cell["chunk"]; x = cell["x"]; y = cell["y"]
        h = float(ch["height"][y, x])
        elev = int((h - self.world.sea_level) * 4200)
        bi = int(ch["biome"][y, x])
        bname, brgb = BIOMES[bi]
        t = float(ch["temp"][y, x]); m = float(ch["moist"][y, x]); s = float(ch["slope"][y, x])
        sw = f"rgb({brgb[0]},{brgb[1]},{brgb[2]})"
        clim = (
            "polar" if t < 0.16 else "tundra" if t < 0.28 else
            "boreal" if t < 0.42 else "temperate" if t < 0.58 else
            "subtropical" if t < 0.72 else "tropical"
        )
        wt = "ocean" if h < self.world.sea_level - 0.02 else (
            "coast" if h < self.world.sea_level else (
                "river" if ch["flow"][y, x] > self.world.river_threshold else "dry"
            )
        )
        self.readout.setText(
            "<b style='color:#8fe6c0'>location</b><br>"
            f"coords: {cx} , {cy}<br>"
            f"chunk: ({cx // self.s.chunk},{cy // self.s.chunk})<br>"
            f"elevation: {elev} m<br>"
            f"biome: <span style='background:{sw}'>&nbsp;&nbsp;&nbsp;</span> {bname}<br>"
            f"climate: {clim}<br>"
            f"water: {wt}<br><br>"
            "<b style='color:#8fe6c0'>variety</b><br>"
            f"warmth: {int(t * 100)} % (cold to hot)<br>"
            f"moisture: {int(m * 100)} %<br>"
            f"slope: {int(s * 100)} %<br><br>"
            "<b style='color:#8fe6c0'>world</b><br>"
            f"{self.world.done} / {self.world.total} chunks"
        )


HYDRO_BIOMES = (12, 13)   # Lake and Delta come from hydrology; reclassification leaves them alone
TALUS = 0.012             # height step per sample above which the erode brush moves soil

# operation id -> (label, group)
OPS = {
    "raise": ("Raise", "Terrain"),
    "lower": ("Lower", "Terrain"),
    "smooth": ("Smooth", "Terrain"),
    "erode": ("Erode", "Terrain"),
    "warm": ("Warmer", "Climate"),
    "cool": ("Cooler", "Climate"),
    "wet": ("Wetter", "Climate"),
    "dry": ("Drier", "Climate"),
    "paint": ("Paint ground", "Ground"),
}
SHAPES = {"circle": "Circle", "square": "Square", "diamond": "Diamond", "soft": "Soft"}


def _mean8(a):
    P = np.pad(a, 1, mode="edge")
    return (P[:-2, :-2] + P[:-2, 1:-1] + P[:-2, 2:] + P[1:-1, :-2]
            + P[1:-1, 2:] + P[2:, :-2] + P[2:, 1:-1] + P[2:, 2:]) / 8.0


def _bilinear(G, GX, GY):
    n = G.shape[0]
    x = np.clip(GX, 0, n - 1)
    y = np.clip(GY, 0, n - 1)
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    x1 = np.minimum(x0 + 1, n - 1)
    y1 = np.minimum(y0 + 1, n - 1)
    tx = x - x0
    ty = y - y0
    return ((G[y0, x0] * (1 - tx) + G[y0, x1] * tx) * (1 - ty)
            + (G[y1, x0] * (1 - tx) + G[y1, x1] * tx) * ty)


def _sample_coord(T, CH, ts, step):
    """Continuous sample coordinate of global tile coordinates T. Each chunk's samples
    sit at local tiles 0, step, 2*step ..., and chunk c's samples start at c * ts."""
    T = np.asarray(T, np.float64)
    c = np.floor(T / CH)
    loc = np.minimum((T - c * CH) / step, ts - 1)
    return c * ts + loc


class EditDelta:
    """What the editor changed, packed for the 3D walk. Holds copies, so it is safe to
    hand to a background thread. Height changes are interpolated smoothly; biome
    changes are applied at the nearest sample, only where the biome was changed."""

    def __init__(self, dh, changed, biome, step, ts):
        self.dh = dh
        self.changed = changed
        self.biome = biome
        self.step = int(step)
        self.ts = int(ts)
        self.n = dh.shape[0]

    def chunk(self, cx, cy, CH):
        """Return (dheight float32 CHxCH, biome_mask bool CHxCH, biome uint8 CHxCH) or None."""
        tx = cx * CH + np.arange(CH)
        ty = cy * CH + np.arange(CH)
        GX, GY = np.meshgrid(_sample_coord(tx, CH, self.ts, self.step),
                             _sample_coord(ty, CH, self.ts, self.step))
        dh = _bilinear(self.dh, GX, GY)
        kx = np.clip(np.rint(GX).astype(np.int64), 0, self.n - 1)
        ky = np.clip(np.rint(GY).astype(np.int64), 0, self.n - 1)
        mask = self.changed[ky, kx]
        if not np.any(dh) and not mask.any():
            return None
        return dh.astype(np.float32), mask, self.biome[ky, kx]


class EditableMap:
    MAX_UNDO = 25
    FIELDS = ("h", "b", "t", "m", "riv")

    def __init__(self, settings, lowres, base_lowres, sea):
        self.K = int(settings.chunks)
        self.ts = int(settings.overview_ts)
        self.n = self.K * self.ts
        self.step = int(settings.chunk) // self.ts   # world tiles per sample
        self.sea = float(sea)
        self.base = self._grids(base_lowres if base_lowres is not None else lowres)
        self.cur = self._grids(lowres)
        self.undo_stack = []
        self.redo_stack = []
        self._stroke = None
        self._stroke_changed = False
        self.version = 0        # bumps on every change that can be undone
        self.saved_version = 0  # the version last exported

    # ----- conversion between per-chunk dicts and whole-map grids -----
    def _grids(self, lowres):
        n, ts = self.n, self.ts
        g = {"h": np.zeros((n, n)), "b": np.zeros((n, n), np.uint8), "t": np.zeros((n, n)),
             "m": np.zeros((n, n)), "riv": np.zeros((n, n), bool)}
        for (cx, cy), lr in lowres.items():
            sl = (slice(cy * ts, (cy + 1) * ts), slice(cx * ts, (cx + 1) * ts))
            g["h"][sl] = lr["h"]
            g["b"][sl] = lr["b"]
            g["t"][sl] = lr["t"]
            g["m"][sl] = lr["m"]
            g["riv"][sl] = lr["riv"]
        return g

    def lowres_chunks(self):
        """The edited map in the per-chunk form the file stores."""
        ts = self.ts
        shade = self.shade(self.cur["h"])
        out = {}
        for cy in range(self.K):
            for cx in range(self.K):
                sl = (slice(cy * ts, (cy + 1) * ts), slice(cx * ts, (cx + 1) * ts))
                out[(cx, cy)] = {
                    "h": self.cur["h"][sl].copy(),
                    "b": self.cur["b"][sl].copy(),
                    "t": self.cur["t"][sl].copy(),
                    "m": self.cur["m"][sl].copy(),
                    "riv": self.cur["riv"][sl].copy(),
                    "shade": shade[sl].copy(),
                }
        return out

    def base_chunks(self):
        """The original map as the file stores it (kept so later edits can be diffed)."""
        ts = self.ts
        out = {}
        for cy in range(self.K):
            for cx in range(self.K):
                sl = (slice(cy * ts, (cy + 1) * ts), slice(cx * ts, (cx + 1) * ts))
                out[(cx, cy)] = {f: self.base[f][sl].copy() for f in self.FIELDS}
        return out

    # ----- derived quantities -----
    def shade(self, H):
        P = np.pad(H, 1, mode="edge")
        gx = (P[1:-1, 2:] - P[1:-1, :-2]) / (2 * self.step)
        gz = (P[2:, 1:-1] - P[:-2, 1:-1]) / (2 * self.step)
        nx = -gx * 70
        nz = -gz * 70
        il = 1 / np.sqrt(nx * nx + 1.0 + nz * nz)
        ndl = (nx * LX + LY + nz * LZ) * il
        return 0.45 + 0.85 * np.maximum(0, ndl)

    def slope(self, H):
        P = np.pad(H, 1, mode="edge")
        dx = (P[1:-1, 2:] - P[1:-1, :-2]) / self.step
        dy = (P[2:, 1:-1] - P[:-2, 1:-1]) / self.step
        return np.clip(np.sqrt(dx * dx + dy * dy) * 34, 0, 1)

    def render_rgb(self, mode):
        c = self.cur
        lr = {"h": c["h"], "b": c["b"], "t": c["t"], "m": c["m"], "riv": c["riv"],
              "shade": self.shade(c["h"])}
        return render_lowres_rgb(lr, mode, self.sea)

    def changed_cells(self):
        c, b = self.cur, self.base
        m = np.zeros((self.n, self.n), bool)
        for f in self.FIELDS:
            m |= c[f] != b[f]
        return m

    def is_dirty(self):
        """True if the map differs from the original file at all."""
        return bool(self.changed_cells().any())

    def unsaved(self):
        """True if there are changes since the last export."""
        return self.version != self.saved_version

    def mark_saved(self):
        self.saved_version = self.version

    def delta(self):
        """Edits relative to the original, or None if nothing has changed."""
        dh = self.cur["h"] - self.base["h"]
        bchanged = self.cur["b"] != self.base["b"]
        if not np.any(dh != 0) and not bchanged.any():
            return None
        return EditDelta(dh.copy(), bchanged.copy(), self.cur["b"].copy(), self.step, self.ts)

    def cell_info(self, gx, gy):
        i, j = int(gx), int(gy)
        if not (0 <= i < self.n and 0 <= j < self.n):
            return None
        c = self.cur
        return {
            "chunk": (i // self.ts, j // self.ts),
            "biome": BIOMES[int(c["b"][j, i])][0],
            "height": float(c["h"][j, i]),
            "above_sea": float(c["h"][j, i] - self.sea),
            "temp": float(c["t"][j, i]),
            "moist": float(c["m"][j, i]),
            "river": bool(c["riv"][j, i]),
        }

    # ----- brushes -----
    def weights(self, gx, gy, radius, shape):
        n = self.n
        r = max(0.5, float(radius))
        xx = np.arange(n)[None, :] - float(gx)
        yy = np.arange(n)[:, None] - float(gy)
        if shape == "square":
            w = (np.abs(xx) <= r) & (np.abs(yy) <= r)
            return w.astype(np.float64)
        if shape == "diamond":
            w = (np.abs(xx) + np.abs(yy)) <= r
            return w.astype(np.float64)
        d2 = xx * xx + yy * yy
        if shape == "soft":
            return np.clip(1.0 - d2 / (r * r), 0.0, 1.0)
        return (d2 <= r * r).astype(np.float64)

    def begin_stroke(self):
        self._stroke = {k: v.copy() for k, v in self.cur.items()}
        self._stroke_changed = False

    def end_stroke(self):
        if self._stroke is None:
            return
        if self._stroke_changed:
            self.undo_stack.append(self._stroke)
            if len(self.undo_stack) > self.MAX_UNDO:
                self.undo_stack.pop(0)
            self.redo_stack.clear()
            self.version += 1
        self._stroke = None
        self._stroke_changed = False

    def apply(self, op, gx, gy, radius, shape="circle", strength=0.5, biome=0):
        """Apply one brush dab centred on sample (gx, gy). Returns True if anything changed."""
        if op not in OPS:
            raise ValueError(f"unknown operation {op!r}")
        w = self.weights(gx, gy, radius, shape) * float(np.clip(strength, 0.0, 1.0))
        if not w.any():
            return False
        c = self.cur
        if op == "raise":
            c["h"] = np.clip(c["h"] + 0.02 * w, 0.0, 1.0)
        elif op == "lower":
            c["h"] = np.clip(c["h"] - 0.02 * w, 0.0, 1.0)
        elif op == "smooth":
            c["h"] = c["h"] + w * 0.35 * (_mean8(c["h"]) - c["h"])
        elif op == "erode":
            # Thermal erosion: material on slopes steeper than the talus angle slides downhill.
            diff = c["h"] - _mean8(c["h"])
            excess = np.sign(diff) * np.clip(np.abs(diff) - TALUS, 0.0, None)
            c["h"] = np.clip(c["h"] - 0.5 * w * excess, 0.0, 1.0)
        elif op == "warm":
            c["t"] = np.clip(c["t"] + 0.03 * w, 0.0, 1.0)
        elif op == "cool":
            c["t"] = np.clip(c["t"] - 0.03 * w, 0.0, 1.0)
        elif op == "wet":
            c["m"] = np.clip(c["m"] + 0.03 * w, 0.0, 1.0)
        elif op == "dry":
            c["m"] = np.clip(c["m"] - 0.03 * w, 0.0, 1.0)
        elif op == "paint":
            sel = w >= 0.5
            if not sel.any():
                return False
            c["b"][sel] = np.uint8(int(biome) % len(BIOMES))
            self._stroke_changed = True
            return True
        self._reclassify(w > 0)
        self._stroke_changed = True
        return True

    def _reclassify(self, mask):
        """Re-derive the biome of touched samples from their (edited) values, using the
        same rules as generation. Lake and Delta tags are kept."""
        c = self.cur
        H = c["h"]
        land = H >= self.sea
        e = _rank(H, land)
        new = classify_biome(e, H, c["t"], c["m"], self.slope(H), self.sea)
        keep = np.isin(c["b"], HYDRO_BIOMES)
        sel = mask & ~keep
        c["b"][sel] = new[sel].astype(np.uint8)

    # ----- undo / redo -----
    def undo(self):
        if not self.undo_stack:
            return False
        self.redo_stack.append({k: v.copy() for k, v in self.cur.items()})
        self.cur = self.undo_stack.pop()
        self.version += 1
        return True

    def redo(self):
        if not self.redo_stack:
            return False
        self.undo_stack.append({k: v.copy() for k, v in self.cur.items()})
        self.cur = self.redo_stack.pop()
        self.version += 1
        return True

EYE_R = 1.6
GRAVITY = 24.0
JUMP_V = 9.0
STEP_UP = 0.35
MAX_SLOPE = 1.2
WALK_SPEED = 7.0
FLY_SPEED = 60.0
SPRINT = 2.2
DOUBLE_TAP = 0.35
VS_FRAC = 0.12
VIEW_R = 2                 # chunks each side of the player: 2 gives a 5 x 5 area
LOAD_R = VIEW_R
UNLOAD_R = VIEW_R + 1
SENS = 0.0022
SKY = (0.55, 0.72, 0.9)
DEEP = (0.12, 0.3, 0.45)
WATER = (0.12, 0.32, 0.5, 0.6)
LIGHT = np.array([0.45, 0.8, 0.35]) / np.linalg.norm([0.45, 0.8, 0.35])
RING = [(math.cos(a * math.pi / 4), math.sin(a * math.pi / 4)) for a in range(8)]
NAN = float("nan")

K_W = int(Qt.Key.Key_W)
K_A = int(Qt.Key.Key_A)
K_S = int(Qt.Key.Key_S)
K_D = int(Qt.Key.Key_D)
K_E = int(Qt.Key.Key_E)
K_Q = int(Qt.Key.Key_Q)
K_C = int(Qt.Key.Key_C)
K_SPACE = int(Qt.Key.Key_Space)
K_SHIFT = int(Qt.Key.Key_Shift)
K_ESC = int(Qt.Key.Key_Escape)

CONTROLS = (
    "WASD move  ·  mouse look  ·  Shift sprint\n"
    "Space jump  ·  double-tap Space toggles walk ⇄ fly\n"
    "Fly: Space / E up, C / Q down  ·  wheel changes fly speed\n"
    "Esc pause  ·  F11 fullscreen\n\n"
    "You are the centre of an invisible collision sphere (radius 1.6 m) that rests on the\n"
    "actual ground. Water is not solid, so you can walk along the seabed beneath it.\n"
    "Terrain streams in around you as you move."
)


def _gen_buffers(n):
    return [int(b) for b in np.asarray(gl.glGenBuffers(n)).ravel()]


def _extend(own, R, D, G, rx, dy):
    n = own.shape[0]
    P = np.empty((n + 1, n + 1), own.dtype)
    P[:n, :n] = own
    P[:n, n] = R[:, 0] if rx else own[:, n - 1]
    P[n, :n] = D[0, :] if dy else own[n - 1, :]
    P[n, n] = G[0, 0] if (rx and dy) else P[n, n - 1]
    return P


class Terrain:
    def __init__(self, settings, sea):
        self.CH = int(settings.chunk)
        self.K = int(settings.chunks)
        self.N = int(settings.world_w)
        self.W = float(self.N - 1)
        self.vs = self.N * VS_FRAC
        self.sea_y = float(sea) * self.vs
        self.data = {}
        self.meshes = {}
        self._dirty = []
        self._gone = []
        n = self.CH
        a = (np.arange(n)[None, :] + np.arange(n)[:, None] * (n + 1)).ravel()
        tris = np.concatenate([
            np.stack([a, a + 1, a + n + 1], -1),
            np.stack([a + 1, a + n + 2, a + n + 1], -1),
        ])
        self.indices = tris.astype(np.uint32).ravel()

    def has(self, key):
        return key in self.data

    def store(self, key, H, B, F=None, D=None):
        n = self.CH
        if F is None:
            F = np.zeros((n, n), np.uint8)
        if D is None:
            D = np.zeros((n, n), np.uint8)
        self.data[key] = ((np.asarray(H, np.float64) * self.vs).astype(np.float32),
                          np.asarray(B, np.uint8), np.asarray(F, np.uint8), np.asarray(D, np.uint8))
        cx, cy = key
        for k in (key, (cx - 1, cy), (cx, cy - 1), (cx - 1, cy - 1)):
            if k[0] >= 0 and k[1] >= 0:
                self._mesh(k)

    def drop(self, key):
        self.data.pop(key, None)
        self.meshes.pop(key, None)
        self._gone.append(key)

    def _mesh(self, key):
        if key not in self.data:
            return
        cx, cy = key
        K, n = self.K, self.CH
        rx = cx + 1 < K
        dy = cy + 1 < K
        R = self.data.get((cx + 1, cy)) if rx else None
        D = self.data.get((cx, cy + 1)) if dy else None
        G = self.data.get((cx + 1, cy + 1)) if rx and dy else None
        if (rx and R is None) or (dy and D is None) or (rx and dy and G is None):
            return
        own_H, own_B, own_F, own_D = self.data[key]
        Hp = _extend(own_H, R and R[0], D and D[0], G and G[0], rx, dy).astype(np.float64)
        Bp = _extend(own_B, R and R[1], D and D[1], G and G[1], rx, dy)
        Fp = _extend(own_F, R and R[2], D and D[2], G and G[2], rx, dy)
        Dp = _extend(own_D, R and R[3], D and D[3], G and G[3], rx, dy)
        m = n + 1
        X, Z = np.meshgrid(cx * n + np.arange(m), cy * n + np.arange(m))
        verts = np.stack([X.astype(np.float64), Hp, Z.astype(np.float64)], -1).astype(np.float32).reshape(-1, 3)
        dhdx = np.gradient(Hp, 1.0, axis=1)
        dhdz = np.gradient(Hp, 1.0, axis=0)
        nrm = np.stack([-dhdx, np.ones_like(dhdx), -dhdz], -1)
        nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        shade = 0.4 + 0.6 * np.clip(nrm @ LIGHT, 0.0, 1.0)
        rgb = BIOME_RGB_F[Bp] / 255.0
        cover = FLORA_PALETTE[Fp] / 255.0
        a = (Dp.astype(np.float64) / 255.0 * 0.85)[..., None] * (Fp > 0)[..., None]
        rgb = rgb * (1.0 - a) + cover * a
        for _ in range(2):
            Q = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode="edge")
            rgb = (Q[:-2, 1:-1] + Q[2:, 1:-1] + Q[1:-1, :-2] + Q[1:-1, 2:] + 4.0 * Q[1:-1, 1:-1]) / 8.0
        colors = (rgb * shade[..., None]).reshape(-1, 3).astype(np.float32)
        self.meshes[key] = (verts, colors)
        self._dirty.append(key)

    def take_dirty(self, limit):
        out = []
        while self._dirty and len(out) < limit:
            k = self._dirty.pop(0)
            if k in self.meshes and k not in out:
                out.append(k)
        return out

    def take_gone(self):
        out = self._gone
        self._gone = []
        return out

    def cell(self, i, j):
        n = self.CH
        key = (i // n, j // n)
        d = self.data.get(key)
        if d is None:
            return NAN
        return float(d[0][j - key[1] * n, i - key[0] * n])

    def ground(self, x, z):
        x = min(max(float(x), 0.0), self.W - 1e-6)
        z = min(max(float(z), 0.0), self.W - 1e-6)
        i0 = int(x)
        j0 = int(z)
        fi = x - i0
        fj = z - j0
        h00 = self.cell(i0, j0)
        h01 = self.cell(i0 + 1, j0)
        h10 = self.cell(i0, j0 + 1)
        h11 = self.cell(i0 + 1, j0 + 1)
        return (1 - fj) * ((1 - fi) * h00 + fi * h01) + fj * ((1 - fi) * h10 + fi * h11)

    def biome_at(self, x, z):
        i = int(min(max(round(x), 0), self.N - 1))
        j = int(min(max(round(z), 0), self.N - 1))
        n = self.CH
        d = self.data.get((i // n, j // n))
        if d is None:
            return None
        return BIOMES[int(d[1][j % n, i % n])][0]

    def spawn_point(self, key):
        n = self.CH
        cx, cy = key
        B = self.data[key][1]
        c = (n - 1) / 2.0
        jj, ii = np.indices(B.shape)
        land = B >= 2
        if land.any():
            d = np.where(land, (ii - c) ** 2 + (jj - c) ** 2, np.inf)
            j, i = np.unravel_index(int(np.argmin(d)), d.shape)
        else:
            i = j = int(c)
        x = cx * n + float(i)
        z = cy * n + float(j)
        return np.array([x, self.ground(x, z) + EYE_R + 2.0, z])


class Walker:
    def __init__(self, terrain, spawn):
        self.t = terrain
        self.pos = np.asarray(spawn, np.float64).copy()
        self.vy = 0.0
        self.fly = False
        self.grounded = False

    def support(self, x, z):
        r = EYE_R * 0.5
        vals = [self.t.ground(x, z)]
        vals += [self.t.ground(x + r * c, z + r * s) for c, s in RING]
        if any(math.isnan(v) for v in vals):
            return NAN
        return max(vals)

    def _bounded(self, a):
        out = a.copy()
        out[0] = min(max(out[0], 0.0), self.t.W)
        out[2] = min(max(out[2], 0.0), self.t.W)
        return out

    def step(self, dt, move2, move3, sprint, jump, toggle, mult):
        p = self.pos
        if toggle:
            self.fly = not self.fly
            self.vy = 0.0
            self.grounded = False
            if not self.fly:
                s = self.support(p[0], p[2])
                if not math.isnan(s):
                    p[1] = max(p[1], s + EYE_R)
        boost = SPRINT if sprint else 1.0

        if self.fly:
            p += np.asarray(move3, np.float64) * (FLY_SPEED * mult * boost * dt)
            p[1] = min(max(p[1], -self.t.vs), 4.0 * self.t.vs)
            p[:] = self._bounded(p)
            return

        for ax, d in ((0, float(move2[0])), (2, float(move2[1]))):
            d *= WALK_SPEED * boost * dt
            if d == 0.0:
                continue
            cand = p.copy()
            cand[ax] += d
            cand = self._bounded(cand)
            rise = self.support(cand[0], cand[2]) - (p[1] - EYE_R)
            if rise <= STEP_UP or rise <= MAX_SLOPE * abs(d):
                p[:] = cand

        if jump and self.grounded:
            self.vy = JUMP_V
        s = self.support(p[0], p[2])
        if math.isnan(s):
            self.vy = 0.0
            self.grounded = False
            return
        self.vy -= GRAVITY * dt
        p[1] += self.vy * dt
        gap = p[1] - EYE_R - s
        if gap <= 0.0:
            p[1] = s + EYE_R
            self.vy = 0.0
            self.grounded = True
        else:
            self.grounded = gap < 0.12 and self.vy <= 0.0
        p[:] = self._bounded(p)


class Generator(QThread):
    initialized = pyqtSignal(object)
    chunk_ready = pyqtSignal(int, int, object)
    failed = pyqtSignal(str)

    def __init__(self, info):
        """info: {"settings", "sea", "generator", "delta"}. delta is the editor's
        changes (an EditDelta or None), already copied so this thread can read it safely."""
        super().__init__()
        self.info = info
        self._cond = threading.Condition()
        self._wanted = []
        self._generated = set()
        self._halt = False

    def run(self):
        try:
            settings = self.info["settings"]
            sea = self.info["sea"]
            delta = self.info.get("delta")
            flora = self.info.get("flora")
            world = World(settings)
            world.sea_level = sea
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.initialized.emit({"settings": settings, "sea": sea, "generator": self.info["generator"]})
        while True:
            with self._cond:
                while not self._halt and self._next_locked() is None:
                    self._cond.wait()
                if self._halt:
                    return
                key = self._next_locked()
                self._generated.add(key)
            try:
                ch = world.generate_chunk(key[0], key[1])
                H, B = ch["height"], ch["biome"]
                if delta is not None:
                    patch = delta.chunk(key[0], key[1], settings.chunk)
                    if patch is not None:
                        dh, mask, cb = patch
                        H = (H + dh).astype(np.float32)
                        B = np.where(mask, cb, B).astype(np.uint8)
                F, D = _flora_chunk(flora, key[0], key[1], settings.chunk, H, B, sea)
            except Exception as exc:
                self.failed.emit(str(exc))
                return
            self.chunk_ready.emit(key[0], key[1], (H, B, F, D))

    def _next_locked(self):
        for k in self._wanted:
            if k not in self._generated:
                return k
        return None

    def set_wanted(self, keys):
        with self._cond:
            self._wanted = list(keys)
            self._cond.notify_all()

    def forget(self, key):
        with self._cond:
            self._generated.discard(key)

    def halt(self):
        with self._cond:
            self._halt = True
            self._cond.notify_all()


class Streamer(QObject):
    progress = pyqtSignal(int, int)
    spawn_ready = pyqtSignal()
    failed = pyqtSignal(str)
    version_mismatch = pyqtSignal(str, str)

    def __init__(self, info):
        super().__init__()
        self.terrain = None
        self.spawn_keys = []
        self.spawn_done = False
        self.player_c = (0, 0)
        self.gen = Generator(info)
        self.gen.initialized.connect(self.on_initialized)
        self.gen.chunk_ready.connect(self.on_chunk)
        self.gen.failed.connect(self.failed)
        self.gen.start()

    def center_key(self):
        c = self.terrain.K // 2
        return (c, c)

    def on_initialized(self, info):
        if info["generator"] != GENERATOR_VERSION:
            self.version_mismatch.emit(str(info["generator"]), GENERATOR_VERSION)
        self.terrain = Terrain(info["settings"], info["sea"])
        cx, cy = self.center_key()
        K = self.terrain.K
        keys = [(x, y) for x in range(max(0, cx - VIEW_R), min(K, cx + VIEW_R + 1))
                for y in range(max(0, cy - VIEW_R), min(K, cy + VIEW_R + 1))]
        keys.sort(key=lambda k: (k[0] - cx) ** 2 + (k[1] - cy) ** 2)
        self.spawn_keys = keys
        self.player_c = (cx, cy)
        self.gen.set_wanted(keys)
        self.progress.emit(0, len(keys))

    def on_chunk(self, cx, cy, payload):
        if self.terrain is None:
            return
        key = (cx, cy)
        pcx, pcy = self.player_c
        if max(abs(cx - pcx), abs(cy - pcy)) > UNLOAD_R:
            self.gen.forget(key)
            return
        H, B, F, D = payload
        self.terrain.store(key, H, B, F, D)
        if not self.spawn_done:
            n = sum(1 for k in self.spawn_keys if self.terrain.has(k))
            self.progress.emit(n, len(self.spawn_keys))
            if n == len(self.spawn_keys):
                self.spawn_done = True
                self.spawn_ready.emit()

    def spawn(self):
        return self.terrain.spawn_point(self.center_key())

    def update(self, px, pz):
        if self.terrain is None or not self.spawn_done:
            return
        t = self.terrain
        n, K = t.CH, t.K
        pcx = int(min(max(px // n, 0), K - 1))
        pcz = int(min(max(pz // n, 0), K - 1))
        self.player_c = (pcx, pcz)
        keys = [(cx, cy)
                for cx in range(max(0, pcx - LOAD_R), min(K, pcx + LOAD_R + 1))
                for cy in range(max(0, pcz - LOAD_R), min(K, pcz + LOAD_R + 1))]
        keys.sort(key=lambda k: (k[0] - pcx) ** 2 + (k[1] - pcz) ** 2)
        self.gen.set_wanted([k for k in keys if not t.has(k)])
        for k in list(t.data.keys()):
            if max(abs(k[0] - pcx), abs(k[1] - pcz)) > UNLOAD_R:
                t.drop(k)
                self.gen.forget(k)

    def shutdown(self):
        self.gen.halt()


class Compass(QWidget):
    """Shows which way the camera faces. The ring turns with the camera, so each letter
    sits where its direction is. North is the -Z direction, the way the camera starts out."""
    SIZE = 92

    def __init__(self, parent):
        super().__init__(parent)
        self.setFixedSize(self.SIZE, self.SIZE)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.yaw = 0.0

    def set_heading(self, yaw):
        yaw = yaw % (2.0 * math.pi)
        if abs(yaw - self.yaw) > 1e-4:
            self.yaw = yaw
            self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        c = self.SIZE / 2.0
        r = c - 3.0
        p.setPen(QPen(QColor("#33465c"), 1))
        p.setBrush(QColor(8, 12, 18, 190))
        p.drawEllipse(QPointF(c, c), r, r)
        font = QFont("sans-serif", 10)
        font.setBold(True)
        p.setFont(font)
        for label, bearing, color in (("N", 0.0, "#ff6b6b"), ("E", math.pi / 2, "#dfe9f5"),
                                      ("S", math.pi, "#dfe9f5"), ("W", 1.5 * math.pi, "#dfe9f5")):
            a = bearing - self.yaw
            x = c + (r - 14.0) * math.sin(a)
            y = c - (r - 14.0) * math.cos(a)
            p.setPen(QColor(color))
            p.drawText(QRectF(x - 10, y - 10, 20, 20), Qt.AlignmentFlag.AlignCenter, label)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor("#8fe6c0"))
        p.drawPolygon(QPolygonF([QPointF(c, 3.0), QPointF(c - 5.0, 11.0), QPointF(c + 5.0, 11.0)]))
        small = QFont("sans-serif", 8)
        p.setFont(small)
        p.setPen(QColor("#c4cfdc"))
        deg = math.degrees(self.yaw) % 360.0
        p.drawText(QRectF(c - 25, c - 8, 50, 16), Qt.AlignmentFlag.AlignCenter, f"{deg:03.0f}°")


class ViewWidget(QOpenGLWidget):
    back_requested = pyqtSignal()
    editor_requested = pyqtSignal()

    def __init__(self, streamer, spawn, parent=None):
        super().__init__(parent)
        self.stream = streamer
        self.t = streamer.terrain
        self.walker = Walker(self.t, spawn)
        self.yaw = 0.0
        self.pitch = 0.0
        self.keys = set()
        self.captured = False
        self.paused = False
        self.fly_mult = 1.0
        self._jump = False
        self._toggle = False
        self._last_space = -10.0
        self._last = time.perf_counter()
        self._gl = {}
        self._ibo = None
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)

        self.hud = QLabel(self)
        self.hud.setFont(QFont("monospace", 10))
        self.hud.setStyleSheet(
            "color:#dfe9f5; background:rgba(8,12,18,170); padding:8px 10px; border-radius:6px;"
        )
        self.hud.move(12, 12)
        self.compass = Compass(self)

        self.menu = self._build_menu()
        self.menu.hide()

        self.hud_timer = QTimer(self)
        self.hud_timer.timeout.connect(self._refresh_hud)
        self.hud_timer.start(150)
        self.frame_timer = QTimer(self)
        self.frame_timer.timeout.connect(self.update)
        self.frame_timer.start(16)

    def _build_menu(self):
        box = QFrame(self)
        box.setStyleSheet(
            "QFrame{background:rgba(8,12,18,235); border-radius:10px;}"
            "QLabel{color:#dfe9f5;}"
            "QPushButton{background:#1d2a38; color:#dfe9f5; border:1px solid #33465c;"
            " border-radius:6px; padding:8px 18px; min-width:160px;}"
            "QPushButton:hover{background:#263a4f;}"
        )
        lay = QVBoxLayout(box)
        title = QLabel("Paused")
        title.setStyleSheet("font-size:18px; font-weight:600;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        resume = QPushButton("Resume")
        resume.clicked.connect(self.resume)
        editor = QPushButton("Back to editor")
        editor.clicked.connect(self.editor_requested.emit)
        back = QPushButton("Back to start")
        back.clicked.connect(self.back_requested.emit)
        lay.addWidget(title)
        lay.addWidget(resume)
        lay.addWidget(editor)
        lay.addWidget(back)
        return box

    def resizeEvent(self, e):
        super().resizeEvent(e)
        if not hasattr(self, "menu"):
            return
        self._center_menu()
        self._place_compass()

    def _place_compass(self):
        self.compass.move(self.width() - self.compass.width() - 12, 12)

    def _center_menu(self):
        self.menu.adjustSize()
        self.menu.move((self.width() - self.menu.width()) // 2,
                       (self.height() - self.menu.height()) // 2)

    def _center_global(self):
        return self.mapToGlobal(QPoint(self.width() // 2, self.height() // 2))

    def _capture(self):
        self.captured = True
        self.setCursor(Qt.CursorShape.BlankCursor)
        QCursor.setPos(self._center_global())

    def _release(self):
        self.captured = False
        self.unsetCursor()

    def pause(self):
        self.paused = True
        self.keys.clear()
        self._jump = False
        self._toggle = False
        self._release()
        self._center_menu()
        self.menu.show()
        self.menu.raise_()

    def resume(self):
        self.paused = False
        self.menu.hide()
        self.setFocus()
        self._capture()

    def shutdown(self):
        self.frame_timer.stop()
        self.hud_timer.stop()
        self._release()

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton and not self.captured and not self.paused:
            self._capture()
        super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self.captured:
            c = QPointF(self.width() / 2, self.height() / 2)
            d = e.position() - c
            self.yaw += d.x() * SENS
            self.pitch = min(max(self.pitch - d.y() * SENS, -1.5), 1.5)
            QCursor.setPos(self._center_global())
        super().mouseMoveEvent(e)

    def wheelEvent(self, e):
        dy = e.angleDelta().y()
        if dy:
            self.fly_mult = min(max(self.fly_mult * (1.15 if dy > 0 else 1 / 1.15), 0.1), 20.0)
        super().wheelEvent(e)

    def keyPressEvent(self, e):
        k = int(e.key())
        if k == K_ESC and not e.isAutoRepeat():
            if self.paused:
                self.resume()
            else:
                self.pause()
            return
        if not e.isAutoRepeat():
            if k == K_SPACE:
                now = time.perf_counter()
                if now - self._last_space < DOUBLE_TAP:
                    self._toggle = True
                    self._jump = False
                    self._last_space = -10.0
                else:
                    self._jump = True
                    self._last_space = now
            self.keys.add(k)
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        if not e.isAutoRepeat():
            self.keys.discard(int(e.key()))
        super().keyReleaseEvent(e)

    def _control(self, dt):
        k = self.keys
        f = (K_W in k) - (K_S in k)
        r = (K_D in k) - (K_A in k)
        sprint = K_SHIFT in k
        yaw = self.yaw
        fwd = np.array([math.sin(yaw), -math.cos(yaw)])
        right = np.array([math.cos(yaw), math.sin(yaw)])
        mv2 = fwd * f + right * r
        n = np.linalg.norm(mv2)
        if n > 0:
            mv2 = mv2 / n

        cp = math.cos(self.pitch)
        fwd3 = np.array([math.sin(yaw) * cp, math.sin(self.pitch), -math.cos(yaw) * cp])
        right3 = np.array([right[0], 0.0, right[1]])
        up = ((K_SPACE in k) or (K_E in k)) - ((K_C in k) or (K_Q in k))
        mv3 = fwd3 * f + right3 * r + np.array([0.0, 1.0, 0.0]) * up
        n = np.linalg.norm(mv3)
        if n > 0:
            mv3 = mv3 / n

        self.walker.step(dt, mv2, mv3, sprint, self._jump, self._toggle, self.fly_mult)
        self._jump = False
        self._toggle = False

    def initializeGL(self):
        gl.glEnable(gl.GL_DEPTH_TEST)
        gl.glEnable(gl.GL_FOG)
        gl.glFogi(gl.GL_FOG_MODE, gl.GL_LINEAR)
        gl.glClearColor(*SKY, 1.0)
        self._ibo = _gen_buffers(1)[0]
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, self._ibo)
        gl.glBufferData(gl.GL_ELEMENT_ARRAY_BUFFER, self.t.indices.nbytes, self.t.indices, gl.GL_STATIC_DRAW)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, 0)

    def _sync_gl(self):
        for key in self.t.take_gone():
            if key in self._gl:
                v, c = self._gl.pop(key)
                gl.glDeleteBuffers(2, np.array([v, c], np.uint32))
        for key in self.t.take_dirty(6):
            verts, colors = self.t.meshes[key]
            if key not in self._gl:
                self._gl[key] = tuple(_gen_buffers(2))
            v, c = self._gl[key]
            gl.glBindBuffer(gl.GL_ARRAY_BUFFER, v)
            gl.glBufferData(gl.GL_ARRAY_BUFFER, verts.nbytes, verts, gl.GL_STATIC_DRAW)
            gl.glBindBuffer(gl.GL_ARRAY_BUFFER, c)
            gl.glBufferData(gl.GL_ARRAY_BUFFER, colors.nbytes, colors, gl.GL_STATIC_DRAW)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)

    def paintGL(self):
        self.compass.set_heading(self.yaw)
        now = time.perf_counter()
        dt = min(0.05, now - self._last)
        self._last = now
        if not self.paused:
            self._control(dt)
        p = self.walker.pos
        if not self.paused:
            self.stream.update(p[0], p[2])
        self._sync_gl()

        ratio = self.devicePixelRatioF()
        gl.glViewport(0, 0, max(1, int(self.width() * ratio)), max(1, int(self.height() * ratio)))

        view_dist = LOAD_R * self.t.CH
        if p[1] < self.t.sea_y:
            col, fog_start, fog_end = DEEP, 0.0, 55.0
        else:
            col, fog_start, fog_end = SKY, view_dist * 0.8, view_dist * 1.4
        gl.glClearColor(*col, 1.0)
        gl.glClear(gl.GL_COLOR_BUFFER_BIT | gl.GL_DEPTH_BUFFER_BIT)
        gl.glFogfv(gl.GL_FOG_COLOR, [*col, 1.0])
        gl.glFogf(gl.GL_FOG_START, fog_start)
        gl.glFogf(gl.GL_FOG_END, fog_end)

        aspect = max(1e-3, self.width() / max(1, self.height()))
        gl.glMatrixMode(gl.GL_PROJECTION)
        gl.glLoadIdentity()
        gluPerspective(70.0, aspect, 0.3, (LOAD_R + 1) * self.t.CH * 1.5)
        gl.glMatrixMode(gl.GL_MODELVIEW)
        gl.glLoadIdentity()
        cp = math.cos(self.pitch)
        look = p + np.array([math.sin(self.yaw) * cp, math.sin(self.pitch), -math.cos(self.yaw) * cp])
        gluLookAt(p[0], p[1], p[2], look[0], look[1], look[2], 0.0, 1.0, 0.0)

        gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
        gl.glEnableClientState(gl.GL_COLOR_ARRAY)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, self._ibo)
        for v, c in self._gl.values():
            gl.glBindBuffer(gl.GL_ARRAY_BUFFER, v)
            gl.glVertexPointer(3, gl.GL_FLOAT, 0, None)
            gl.glBindBuffer(gl.GL_ARRAY_BUFFER, c)
            gl.glColorPointer(3, gl.GL_FLOAT, 0, None)
            gl.glDrawElements(gl.GL_TRIANGLES, self.t.indices.size, gl.GL_UNSIGNED_INT, None)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, 0)
        gl.glDisableClientState(gl.GL_COLOR_ARRAY)
        gl.glDisableClientState(gl.GL_VERTEX_ARRAY)

        W = self.t.W
        y = self.t.sea_y
        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)
        gl.glDepthMask(gl.GL_FALSE)
        gl.glColor4f(*WATER)
        gl.glBegin(gl.GL_QUADS)
        gl.glVertex3f(0.0, y, 0.0)
        gl.glVertex3f(W, y, 0.0)
        gl.glVertex3f(W, y, W)
        gl.glVertex3f(0.0, y, W)
        gl.glEnd()
        gl.glDepthMask(gl.GL_TRUE)
        gl.glDisable(gl.GL_BLEND)

    def resizeGL(self, w, h):
        pass

    def _refresh_hud(self):
        p = self.walker.pos
        mode = "FLY · no gravity, no collision" if self.walker.fly else "WALK · gravity + collision"
        biome = self.t.biome_at(p[0], p[2]) or "loading terrain…"
        s = self.walker.support(p[0], p[2])
        alt = "—" if math.isnan(s) else f"{s - self.t.sea_y:+.0f} m vs sea"
        hint = "Esc menu · double-tap Space toggles mode" if self.captured else "Click to look around · Esc menu"
        self.hud.setText(
            f"{mode}\n"
            f"{biome}\n"
            f"x {p[0]:.0f}   z {p[2]:.0f}\n"
            f"ground {alt}\n"
            f"fly speed ×{self.fly_mult:.1f}\n"
            f"{hint}"
        )
        self.hud.adjustSize()


class LoadingPage(QWidget):
    def __init__(self):
        super().__init__()
        lay = QVBoxLayout(self)
        lay.addStretch()
        self.label = QLabel("Loading the area around the spawn point…")
        self.label.setStyleSheet("font-size:16px;")
        self.bar = QProgressBar()
        self.bar.setFixedWidth(420)
        self.bar.setRange(0, 100)
        lay.addWidget(self.label, 0, Qt.AlignmentFlag.AlignHCenter)
        lay.addSpacing(10)
        lay.addWidget(self.bar, 0, Qt.AlignmentFlag.AlignHCenter)
        lay.addStretch()

    def set_progress(self, n, total):
        self.bar.setValue(int(100 * n / max(1, total)))
        self.label.setText(f"Loading the area around the spawn point… {n} of {total}")


class MapCanvas(QWidget):
    pressed = pyqtSignal(float, float)
    dragged = pyqtSignal(float, float)
    released = pyqtSignal()
    hovered = pyqtSignal(float, float)

    def __init__(self):
        super().__init__()
        self._img = None
        self._n = 0
        self._cursor = None
        self._radius = 0.0
        self._shape = "circle"
        self.setMouseTracking(True)
        self.setMinimumSize(320, 320)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setCursor(Qt.CursorShape.CrossCursor)

    def set_image(self, rgb, n):
        rgb = np.ascontiguousarray(rgb, dtype=np.uint8)
        self._n = int(n)
        self._img = QImage(rgb.tobytes(), self._n, self._n, 3 * self._n,
                           QImage.Format.Format_RGB888).copy()
        self.update()

    def set_brush(self, radius, shape):
        self._radius = float(radius)
        self._shape = shape
        self.update()

    def _rect(self):
        side = max(1.0, min(self.width(), self.height()) - 8.0)
        return QRectF((self.width() - side) / 2.0, (self.height() - side) / 2.0, side, side)

    def _grid_at(self, pos):
        """Map a widget position to continuous sample coordinates, or None outside the map."""
        if not self._n:
            return None
        r = self._rect()
        s = r.width() / self._n
        gx = (pos.x() - r.x()) / s - 0.5
        gy = (pos.y() - r.y()) / s - 0.5
        if -0.5 <= gx < self._n - 0.5 and -0.5 <= gy < self._n - 0.5:
            return gx, gy
        return None

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(12, 16, 22))
        if self._img is None:
            p.end()
            return
        r = self._rect()
        p.drawImage(r, self._img)
        if self._cursor is not None and self._radius > 0:
            s = r.width() / self._n
            cx = r.x() + (self._cursor[0] + 0.5) * s
            cy = r.y() + (self._cursor[1] + 0.5) * s
            rad = (self._radius + 0.5) * s
            p.setPen(QPen(QColor(255, 255, 255, 220), 1.5))
            p.setBrush(Qt.BrushStyle.NoBrush)
            if self._shape == "square":
                p.drawRect(QRectF(cx - rad, cy - rad, 2 * rad, 2 * rad))
            elif self._shape == "diamond":
                p.drawPolygon(QPolygonF([QPointF(cx, cy - rad), QPointF(cx + rad, cy),
                                         QPointF(cx, cy + rad), QPointF(cx - rad, cy)]))
            else:
                p.drawEllipse(QPointF(cx, cy), rad, rad)
        p.end()

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            g = self._grid_at(e.position())
            if g is not None:
                self.pressed.emit(*g)

    def mouseMoveEvent(self, e):
        g = self._grid_at(e.position())
        self._cursor = g
        if g is not None:
            self.hovered.emit(*g)
            if e.buttons() & Qt.MouseButton.LeftButton:
                self.dragged.emit(*g)
        self.update()

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self.released.emit()

    def leaveEvent(self, e):
        self._cursor = None
        self.update()


class MapEditor(QWidget):
    next_requested = pyqtSignal()
    export_requested = pyqtSignal()
    back_requested = pyqtSignal()

    def __init__(self, emap, title):
        super().__init__()
        self.map = emap
        self._title = title
        self._op = "raise"
        self._stroking = False

        root = QHBoxLayout(self)
        panel = QVBoxLayout()
        panel.setSpacing(8)

        head = QLabel(title)
        head.setStyleSheet("font-size:16px; font-weight:600;")
        head.setWordWrap(True)
        self.status = QLabel("")
        self.status.setStyleSheet("color:#8a97a8;")
        self.status.setWordWrap(True)
        panel.addWidget(head)
        panel.addWidget(self.status)

        self._op_buttons = {}
        self._group = QButtonGroup(self)
        self._group.setExclusive(True)
        for title_text, names in (
            ("Terrain", ("raise", "lower", "smooth", "erode")),
            ("Climate", ("warm", "cool", "wet", "dry")),
            ("Ground", ("paint",)),
        ):
            box = QGroupBox(title_text)
            lay = QVBoxLayout(box)
            for op in names:
                b = QPushButton(OPS[op][0])
                b.setCheckable(True)
                b.setProperty("op", op)
                b.clicked.connect(lambda _=False, o=op: self._set_op(o))
                self._group.addButton(b)
                self._op_buttons[op] = b
                lay.addWidget(b)
                if op == "paint":
                    self.biome_box = QComboBox()
                    for i, (name, _rgb) in enumerate(BIOMES):
                        self.biome_box.addItem(name, i)
                    self.biome_box.setCurrentIndex(6)
                    lay.addWidget(self.biome_box)
            panel.addWidget(box)
        self._op_buttons[self._op].setChecked(True)

        brush = QGroupBox("Brush")
        bl = QFormLayout(brush)
        self.size_slider = QSlider(Qt.Orientation.Horizontal)
        self.size_slider.setRange(1, 40)
        self.size_slider.setValue(5)
        self.size_label = QLabel("5")
        self.shape_box = QComboBox()
        for key, label in SHAPES.items():
            self.shape_box.addItem(label, key)
        self.strength_slider = QSlider(Qt.Orientation.Horizontal)
        self.strength_slider.setRange(5, 100)
        self.strength_slider.setValue(50)
        self.strength_label = QLabel("50%")
        bl.addRow("Size", self._row(self.size_slider, self.size_label))
        bl.addRow("Shape", self.shape_box)
        bl.addRow("Strength", self._row(self.strength_slider, self.strength_label))
        panel.addWidget(brush)

        view_row = QHBoxLayout()
        view_row.addWidget(QLabel("View"))
        self.view_box = QComboBox()
        for key, label in VIEW_MODES:
            self.view_box.addItem(label, key)
        view_row.addWidget(self.view_box, 1)
        panel.addLayout(view_row)

        hist = QHBoxLayout()
        self.undo_btn = QPushButton("Undo")
        self.redo_btn = QPushButton("Redo")
        hist.addWidget(self.undo_btn)
        hist.addWidget(self.redo_btn)
        panel.addLayout(hist)

        panel.addStretch()
        self.walk_btn = QPushButton("Next: Flora →")
        self.export_btn = QPushButton("Export edited file…")
        self.back_btn = QPushButton("Back to start")
        for b in (self.walk_btn, self.export_btn, self.back_btn):
            panel.addWidget(b)

        self.hint = QLabel("Drag on the map to paint. Ctrl+Z undoes, Ctrl+Shift+Z redoes, "
                           "Ctrl+S exports. Brushes change only heights, climate values and "
                           "ground types that already exist in the file.")
        self.hint.setWordWrap(True)
        self.hint.setStyleSheet("color:#8a97a8; font-size:11px;")
        panel.addWidget(self.hint)

        side = QWidget()
        side.setLayout(panel)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setMinimumWidth(300)
        scroll.setMaximumWidth(340)
        scroll.setWidget(side)
        root.addWidget(scroll)
        self.canvas = MapCanvas()
        root.addWidget(self.canvas, 1)

        self.size_slider.valueChanged.connect(self._on_brush_changed)
        self.shape_box.currentIndexChanged.connect(self._on_brush_changed)
        self.strength_slider.valueChanged.connect(self._on_brush_changed)
        self.view_box.currentIndexChanged.connect(lambda *_: self.refresh())
        self.undo_btn.clicked.connect(self.undo)
        self.redo_btn.clicked.connect(self.redo)
        self.walk_btn.clicked.connect(self.next_requested.emit)
        self.export_btn.clicked.connect(self.export_requested.emit)
        self.back_btn.clicked.connect(self.back_requested.emit)

        self.canvas.pressed.connect(self._press)
        self.canvas.dragged.connect(self._drag)
        self.canvas.released.connect(self._release)
        self.canvas.hovered.connect(self._hover)

        QShortcut(QKeySequence(QKeySequence.StandardKey.Undo), self).activated.connect(self.undo)
        QShortcut(QKeySequence(QKeySequence.StandardKey.Redo), self).activated.connect(self.redo)

        self._on_brush_changed()
        self.refresh()

    @staticmethod
    def _row(slider, label):
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(slider, 1)
        label.setMinimumWidth(40)
        lay.addWidget(label)
        return w

    # ----- state helpers -----
    def _set_op(self, op):
        self._op = op
        self._op_buttons[op].setChecked(True)

    def _on_brush_changed(self, *_):
        radius = self.size_slider.value()
        strength = self.strength_slider.value()
        self.size_label.setText(str(radius))
        self.strength_label.setText(f"{strength}%")
        self.canvas.set_brush(radius, self.shape_box.currentData())

    def _brush(self):
        return self.size_slider.value(), self.shape_box.currentData(), self.strength_slider.value() / 100.0

    def _update_status(self):
        n_edit = int(self.map.changed_cells().sum())
        state = "unsaved changes" if self.map.unsaved() else ("edited" if n_edit else "unchanged")
        self.status.setText(f"{self._title}\n{n_edit} samples changed · {state}")
        self.undo_btn.setEnabled(bool(self.map.undo_stack))
        self.redo_btn.setEnabled(bool(self.map.redo_stack))

    def refresh(self):
        mode = self.view_box.currentData() or "biome"
        self.canvas.set_image(self.map.render_rgb(mode), self.map.n)

    # ----- painting -----
    def _press(self, gx, gy):
        self._stroking = True
        self.map.begin_stroke()
        self._paint(gx, gy)

    def _drag(self, gx, gy):
        if self._stroking:
            self._paint(gx, gy)

    def _paint(self, gx, gy):
        radius, shape, strength = self._brush()
        if self.map.apply(self._op, gx, gy, radius, shape, strength,
                          biome=self.biome_box.currentData()):
            self.refresh()

    def _release(self):
        if not self._stroking:
            return
        self._stroking = False
        self.map.end_stroke()
        self.refresh()
        self._update_status()

    def _hover(self, gx, gy):
        info = self.map.cell_info(round(gx), round(gy))
        if info is None:
            return
        river = ", river" if info["river"] else ""
        self.hint.setText(
            f"Chunk {info['chunk'][0]},{info['chunk'][1]} · {info['biome']} · "
            f"{info['above_sea']:+.3f} vs sea · temp {info['temp']:.2f} · "
            f"moisture {info['moist']:.2f}{river}"
        )

    # ----- undo / redo -----
    def undo(self):
        if self.map.undo():
            self.refresh()
            self._update_status()

    def redo(self):
        if self.map.redo():
            self.refresh()
            self._update_status()

    def showEvent(self, e):
        super().showEvent(e)
        self._update_status()



# ======================================================================
# Flora: plant layers laid over an edited map, as a step after editing.
# Each layer has a noise pattern and a coverage setting. A plant only grows
# on land that can carry it, and climate sets how well it suits each spot.
# Plants are a layer of their own: terrain edits change where they may grow,
# but never leave a water or ground tile behind.
# ======================================================================

# key, label, colour, warmth range, moisture range, noise scale, seed offset, default coverage
FLORA_LAYERS = (
    ("grass", "Grass", (104, 150, 70), (0.22, 0.84), (0.28, 1.00), 7.0, 1100, 0.55),
    ("shrubs", "Shrubs", (92, 124, 56), (0.20, 0.80), (0.22, 0.82), 10.0, 1300, 0.30),
    ("forest", "Forest", (46, 102, 58), (0.30, 0.76), (0.46, 1.00), 5.0, 1500, 0.25),
    ("reeds", "Reeds", (132, 156, 80), (0.30, 0.92), (0.55, 1.00), 12.0, 1700, 0.15),
)
FLORA_BY_KEY = {row[0]: row for row in FLORA_LAYERS}
# colour per flora index: 0 is bare ground, 1.. follow FLORA_LAYERS order
FLORA_PALETTE = np.array([(0, 0, 0)] + [row[2] for row in FLORA_LAYERS], np.float64)
# ground no plant can grow on, whatever the climate
NON_VIABLE = (SILT, SEABED_SAND, BEACH_SAND, ICE, BEDROCK, SCREE, CLIFF_ROCK)


def _soft_range(x, lo, hi, edge=0.12):
    """1 inside [lo, hi], fading to 0 over `edge` on either side."""
    up = np.clip((x - (lo - edge)) / edge, 0.0, 1.0)
    down = np.clip(((hi + edge) - x) / edge, 0.0, 1.0)
    return np.minimum(up, down)


def _flora_noise(n, scale, seed):
    """Noise over the whole map, spread roughly evenly over 0..255 so it stores compactly."""
    x = np.arange(n, dtype=np.float64) * (scale / n)
    X, Y = np.meshgrid(x, x)
    v = fbm(X, Y, int(seed), 3)
    z = (v - v.mean()) / max(float(v.std()), 1e-9)
    u = 1.0 / (1.0 + np.exp(-1.702 * z))   # logistic fit to a normal CDF: spreads values evenly over 0..1
    return np.round(u * 255).astype(np.uint8)


def _flora_density(key, noise, coverage, t, m, ok):
    """How densely a layer grows, 0..1. noise is 0..255, coverage 0..1, t and m are
    warmth and moisture, ok is 1 where the ground can carry plants. Shared by the map
    preview and the walk, so both show the same plants."""
    row = FLORA_BY_KEY[key]
    nz = np.asarray(noise, np.float32) * np.float32(1.0 / 255.0)
    cover = np.clip((nz - (1.0 - coverage)) / 0.05, 0.0, 1.0)
    suit = _soft_range(t, row[3][0], row[3][1]) * _soft_range(m, row[4][0], row[4][1])
    return (cover * suit * ok).astype(np.float32)


def _flora_chunk(snap, cx, cy, CH, H, B, sea):
    """Plant layer for one chunk of the walk, at tile resolution. Noise is smoothly
    interpolated; warmth and moisture come from the nearest sample; whether a tile can
    carry plants is decided from the height and ground the walk actually draws."""
    if snap is None:
        return None, None
    ts, step = snap["ts"], snap["step"]
    tx = cx * CH + np.arange(CH)
    ty = cy * CH + np.arange(CH)
    GX, GY = np.meshgrid(_sample_coord(tx, CH, ts, step), _sample_coord(ty, CH, ts, step))
    n = snap["t"].shape[0]
    kx = np.clip(np.rint(GX).astype(np.int64), 0, n - 1)
    ky = np.clip(np.rint(GY).astype(np.int64), 0, n - 1)
    ok = ((H >= sea) & ~np.isin(B, NON_VIABLE)).astype(np.float32)
    best = np.zeros((CH, CH), np.float32)
    idx = np.zeros((CH, CH), np.uint8)
    for i, row in enumerate(FLORA_LAYERS, start=1):
        noise, coverage = snap["layers"][row[0]]
        nz = _bilinear(noise.astype(np.float64), GX, GY)
        d = _flora_density(row[0], nz, coverage, snap["t"][ky, kx], snap["m"][ky, kx], ok)
        better = d > best
        best = np.where(better, d, best)
        idx = np.where(better, np.uint8(i), idx)
    dens = np.round(best * 255.0).astype(np.uint8)
    return np.where(dens > 0, idx, np.uint8(0)), dens


class FloraMap:
    """Plant layers for one edited map. Each layer keeps its noise pattern, so a saved
    file reloads to the same plants, and its coverage setting. Where plants grow is worked
    out from the map as it stands, so terrain edits made after planting show up at once."""

    def __init__(self, emap, payload=None, seed=0):
        self.map = emap
        self.n = int(emap.n)
        self.dirty = False
        self.layers = {}
        if payload is not None and int(payload.get("n", -1)) != self.n:
            raise ValueError("The flora layers do not match the size of this map.")
        stored = payload.get("layers", {}) if payload is not None else {}
        for row in FLORA_LAYERS:
            key, scale, offset, cov = row[0], row[5], row[6], row[7]
            entry = stored.get(key)
            if entry is not None:
                noise = np.asarray(entry["noise"], np.uint8)
                if noise.shape != (self.n, self.n):
                    raise ValueError(f"Flora layer {key} has the wrong shape.")
                self.layers[key] = {"seed": int(entry["seed"]),
                                    "coverage": float(entry["coverage"]), "noise": noise}
            else:
                s = int(seed) % 900000 + offset
                self.layers[key] = {"seed": s, "coverage": cov, "noise": _flora_noise(self.n, scale, s)}

    def reroll(self, key):
        row = FLORA_BY_KEY[key]
        seed = int(np.random.default_rng().integers(1, 900000))
        self.layers[key]["seed"] = seed
        self.layers[key]["noise"] = _flora_noise(self.n, row[5], seed)
        self.dirty = True

    def set_coverage(self, key, value):
        self.layers[key]["coverage"] = float(value)
        self.dirty = True

    def viable(self, sl):
        """1 where the ground can carry plants at all: land, and not ice, bare rock or sand."""
        c = self.map.cur
        ok = (c["h"][sl] >= self.map.sea) & ~np.isin(c["b"][sl], NON_VIABLE)
        return ok.astype(np.float32)

    def density(self, key, sl):
        """How densely the layer grows on each tile in the slice, from 0 to 1."""
        c = self.map.cur
        L = self.layers[key]
        return _flora_density(key, L["noise"][sl], L["coverage"], c["t"][sl], c["m"][sl], self.viable(sl))

    def preview(self, show, step):
        """The map with the shown layers laid over it, every layer's density, and the share
        of the map that can carry plants. Works on every `step`-th tile to stay quick."""
        sl = np.s_[::step, ::step]
        c = self.map.cur
        lr = {"h": c["h"][sl], "b": c["b"][sl], "t": c["t"][sl], "m": c["m"][sl],
              "riv": c["riv"][sl], "shade": self.map.shade(c["h"])[sl]}
        rgb = render_lowres_rgb(lr, "biome", self.map.sea).astype(np.float32)
        dens = {}
        for row in FLORA_LAYERS:
            d = self.density(row[0], sl)
            dens[row[0]] = d
            if row[0] in show:
                a = (d * 0.85)[..., None]
                rgb = rgb * (1.0 - a) + np.array(row[2], np.float32) * a
        return np.clip(rgb, 0, 255).astype(np.uint8), dens, float(self.viable(sl).mean())

    def snapshot(self):
        """What the 3D walk needs for plants: the noise and coverage of each layer, and the
        warmth and moisture of the map. Copies, so a walk thread can read them safely."""
        c = self.map.cur
        return {
            "ts": int(self.map.ts),
            "step": int(self.map.step),
            "t": c["t"].astype(np.float32),
            "m": c["m"].astype(np.float32),
            "layers": {k: (L["noise"].copy(), float(L["coverage"])) for k, L in self.layers.items()},
        }

    def to_payload(self):
        return {"n": self.n,
                "layers": {k: {"seed": L["seed"], "coverage": L["coverage"], "noise": L["noise"]}
                           for k, L in self.layers.items()}}


class FloraPanel(QWidget):
    back_requested = pyqtSignal()
    walk_requested = pyqtSignal()
    export_requested = pyqtSignal()
    PREVIEW_TILES = 1024

    def __init__(self, doc):
        super().__init__()
        self.doc = doc
        self.flora = doc.ensure_flora()
        self.step = max(1, self.flora.n // self.PREVIEW_TILES)
        self._rows = {}

        root = QHBoxLayout(self)
        panel = QVBoxLayout()
        panel.setSpacing(8)
        head = QLabel("Flora")
        head.setStyleSheet("font-size:16px; font-weight:600;")
        self.status = QLabel("")
        self.status.setStyleSheet("color:#8a97a8;")
        self.status.setWordWrap(True)
        panel.addWidget(head)
        panel.addWidget(self.status)

        for row in FLORA_LAYERS:
            key, label = row[0], row[1]
            box = QGroupBox(label)
            bl = QVBoxLayout(box)
            top = QHBoxLayout()
            show = QCheckBox("Show")
            show.setChecked(True)
            reroll = QPushButton("Reroll pattern")
            top.addWidget(show, 1)
            top.addWidget(reroll)
            bl.addLayout(top)
            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(0, 100)
            slider.setValue(int(round(self.flora.layers[key]["coverage"] * 100)))
            pct = QLabel(f"{slider.value()}%")
            pct.setMinimumWidth(40)
            srow = QHBoxLayout()
            srow.addWidget(QLabel("Coverage"))
            srow.addWidget(slider, 1)
            srow.addWidget(pct)
            bl.addLayout(srow)
            share = QLabel("")
            share.setStyleSheet("color:#8a97a8; font-size:11px;")
            bl.addWidget(share)
            panel.addWidget(box)
            self._rows[key] = {"show": show, "pct": pct, "share": share}
            show.toggled.connect(lambda *_: self.refresh())
            slider.valueChanged.connect(lambda v, k=key: self._coverage(k, v))
            reroll.clicked.connect(lambda _=False, k=key: self._busy(lambda: self.flora.reroll(k)))

        reroll_all = QPushButton("Reroll all layers")
        reroll_all.clicked.connect(lambda: self._busy(lambda: [self.flora.reroll(r[0]) for r in FLORA_LAYERS]))
        panel.addWidget(reroll_all)

        self.hover = QLabel("Point at a tile to see its ground and plant cover.")
        self.hover.setWordWrap(True)
        self.hover.setStyleSheet("color:#c4cfdc;")
        panel.addWidget(self.hover)

        panel.addStretch()
        self.back_btn = QPushButton("← Back to edit")
        self.walk_btn = QPushButton("Walk the world →")
        self.export_btn = QPushButton("Export edited file…")
        panel.addWidget(self.back_btn)
        panel.addWidget(self.walk_btn)
        panel.addWidget(self.export_btn)
        self.back_btn.clicked.connect(self.back_requested.emit)
        self.walk_btn.clicked.connect(self.walk_requested.emit)
        self.export_btn.clicked.connect(self.export_requested.emit)

        note = QLabel("Plants grow only on land that can carry them: not on ice, bedrock, scree, "
                      "cliff rock, silt or sand. Climate sets how well each plant suits a spot. "
                      "Plants show on this map; the 3D walk does not draw them yet.")
        note.setWordWrap(True)
        note.setStyleSheet("color:#8a97a8; font-size:11px;")
        panel.addWidget(note)

        side = QWidget()
        side.setLayout(panel)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setMinimumWidth(300)
        scroll.setMaximumWidth(340)
        scroll.setWidget(side)
        root.addWidget(scroll)
        self.canvas = MapCanvas()
        root.addWidget(self.canvas, 1)
        self.canvas.hovered.connect(self._hover)
        self.refresh()

    def _coverage(self, key, value):
        self.flora.set_coverage(key, value / 100.0)
        self._rows[key]["pct"].setText(f"{value}%")
        self.refresh()

    def _busy(self, fn):
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            fn()
        finally:
            QApplication.restoreOverrideCursor()
        self.refresh()

    def refresh(self):
        show = {k for k, r in self._rows.items() if r["show"].isChecked()}
        rgb, dens, viable = self.flora.preview(show, self.step)
        self.canvas.set_image(rgb, rgb.shape[0])
        for k, r in self._rows.items():
            r["share"].setText(f"{float((dens[k] > 0.5).mean()) * 100:.0f}% of the map")
        state = "unsaved changes" if self.doc.unsaved() else "saved"
        self.status.setText(f"{self.doc.name}\n{viable * 100:.0f}% of the map can carry plants · {state}")

    def _hover(self, gx, gy):
        n = self.flora.n
        i = min(n - 1, max(0, int(gx * self.step)))
        j = min(n - 1, max(0, int(gy * self.step)))
        sl = np.s_[j:j + 1, i:i + 1]
        tile = BIOMES[int(self.flora.map.cur["b"][j, i])][0]
        parts = [f"{row[1]} {float(self.flora.density(row[0], sl)[0, 0]) * 100:.0f}%" for row in FLORA_LAYERS]
        self.hover.setText(f"{tile} · " + " · ".join(parts))


# ======================================================================
# Worldmaker: one window for generating, editing and walking a world.
# ======================================================================

class WorldDoc:
    """The world the window is working on. Generation, the editor, the walk and export
    all read from one of these, so every tab shows the same state."""

    def __init__(self, settings, sea, river_threshold, done, chunk_ready, lowres,
                 base_lowres, generator, path=None, flora=None):
        self.settings = settings
        self.sea = float(sea)
        self.river_threshold = river_threshold
        self.done = int(done)
        self.chunk_ready = np.array(chunk_ready, bool)
        self.generator = generator
        self.path = Path(path) if path else None
        self.map = EditableMap(settings, lowres, base_lowres, sea)
        self.flora = FloraMap(self.map, flora) if flora is not None else None

    def ensure_flora(self):
        if self.flora is None:
            self.flora = FloraMap(self.map, None, seed=self.settings.seed)
        return self.flora

    def unsaved(self):
        return self.map.unsaved() or bool(self.flora and self.flora.dirty)

    @property
    def name(self):
        return self.path.name if self.path else f"world_{self.settings.seed}.wglb"

    @property
    def complete(self):
        return bool(np.all(self.chunk_ready))

    @classmethod
    def from_file(cls, path):
        d = load_world_full(Path(path))
        if d["lowres"] is None or not np.all(d["chunk_ready"]):
            raise ValueError("This world has not finished generating, so there is no complete map to edit.")
        return cls(d["settings"], d["sea"], d["river_threshold"], d["done"], d["chunk_ready"],
                   d["lowres"], d["base_lowres"], d["generator"], path, d["flora"])

    @classmethod
    def from_world(cls, world, settings):
        return cls(settings, world.sea_level, world.river_threshold, world.done,
                   world.chunk_ready, world.lowres, None, GENERATOR_VERSION)

    def walk_info(self):
        """Everything the 3D walk needs, as it stands now: the terrain edits, and the flora."""
        return {
            "settings": self.settings,
            "sea": self.sea,
            "generator": self.generator,
            "delta": self.map.delta(),
            "flora": self.flora.snapshot() if self.flora is not None else None,
        }

    def export(self, path):
        flora = self.flora.to_payload() if self.flora is not None else None
        write_world_file(Path(path), self.settings, self.map.render_rgb("biome"), self.chunk_ready,
                         self.sea, self.river_threshold, self.done, self.map.lowres_chunks(),
                         self.map.base_chunks(), generator=self.generator, flora=flora)
        self.path = Path(path)
        self.map.mark_saved()
        if self.flora is not None:
            self.flora.dirty = False


class StartPage(QWidget):
    new_requested = pyqtSignal()
    import_requested = pyqtSignal()
    continue_requested = pyqtSignal()
    path_dropped = pyqtSignal(str)

    STEPS = (
        ("1  Generate", "Pick a seed and settings. The world is built chunk by chunk: "
                        "geography first, then weather, then the rivers and lakes that weather feeds."),
        ("2  Edit", "Raise and lower land, shift temperature and moisture, and paint the ground. "
                    "Brushes only change values the map file already holds."),
        ("3  Flora", "Lay plant cover over the ground that can carry it. Noise sets each pattern, "
                     "and coverage sets how much."),
        ("4  Walk and export", "Explore the edited world in 3D, then export it as a .wglb file "
                               "you can import again later."),
    )

    def __init__(self):
        super().__init__()
        self.setAcceptDrops(True)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(40, 30, 40, 30)
        outer.addStretch()

        title = QLabel("Worldmaker")
        title.setStyleSheet("font-size:38px; font-weight:600;")
        sub = QLabel("Generate a world, shape its map, then walk through it.")
        sub.setStyleSheet("color:#8a97a8; font-size:15px;")
        for w in (title, sub):
            outer.addWidget(w, 0, Qt.AlignmentFlag.AlignHCenter)
        outer.addSpacing(26)

        cards = QHBoxLayout()
        cards.setSpacing(16)
        for head, body in self.STEPS:
            card = QFrame()
            card.setStyleSheet("QFrame{background:#131c27; border:1px solid #243446; border-radius:10px;}"
                               "QLabel{border:none; background:transparent;}")
            card.setMinimumWidth(200)
            cl = QVBoxLayout(card)
            h = QLabel(head)
            h.setStyleSheet("font-size:17px; font-weight:600; color:#8fe6c0;")
            b = QLabel(body)
            b.setWordWrap(True)
            b.setStyleSheet("color:#c4cfdc; font-size:13px;")
            cl.addWidget(h)
            cl.addWidget(b)
            cards.addWidget(card)
        outer.addLayout(cards)
        outer.addSpacing(28)

        row = QHBoxLayout()
        row.addStretch()
        self.new_btn = QPushButton("New world")
        self.import_btn = QPushButton("Import world (.wglb)…")
        self.continue_btn = QPushButton("Continue")
        self.continue_btn.setEnabled(False)
        for b in (self.new_btn, self.import_btn, self.continue_btn):
            b.setMinimumWidth(190)
            b.setMinimumHeight(38)
            row.addWidget(b)
        row.addStretch()
        outer.addLayout(row)
        self.drop_hint = QLabel("or drop a .wglb file anywhere on this page")
        self.drop_hint.setStyleSheet("color:#8a97a8;")
        outer.addWidget(self.drop_hint, 0, Qt.AlignmentFlag.AlignHCenter)
        outer.addStretch()

        self.new_btn.clicked.connect(lambda: self.new_requested.emit())
        self.import_btn.clicked.connect(lambda: self.import_requested.emit())
        self.continue_btn.clicked.connect(lambda: self.continue_requested.emit())

    def set_continue(self, name):
        self.continue_btn.setEnabled(name is not None)
        self.continue_btn.setText(f"Continue with {name}" if name else "Continue")

    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e):
        urls = e.mimeData().urls()
        if urls:
            self.path_dropped.emit(urls[0].toLocalFile())


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Worldmaker")
        self.setMinimumSize(960, 600)
        self.doc = None
        self.explorer = None
        self.editor = None
        self.streamer = None
        self.view = None
        self.flora_panel = None

        self.prefs = QSettings("Worldmaker", "Worldmaker")
        self.display_mode = str(self.prefs.value("display2/mode", "borderless"))
        self.display_w = _to_int(self.prefs.value("display2/width", 0))
        self.display_h = _to_int(self.prefs.value("display2/height", 0))

        self.screens = QStackedWidget()
        self.setCentralWidget(self.screens)
        self.start_page = StartPage()
        self.start_page.new_requested.connect(self.new_world)
        self.start_page.import_requested.connect(self.import_world)
        self.start_page.continue_requested.connect(lambda: self.show_tab(self.edit_tab))
        self.start_page.path_dropped.connect(self.import_world)
        self.screens.addWidget(self.start_page)

        self.workspace = QWidget()
        ws = QVBoxLayout(self.workspace)
        ws.setContentsMargins(0, 0, 0, 0)
        self.tabs = QTabWidget()
        self.tabs.setDocumentMode(True)
        home = QPushButton("Start page")
        home.setFlat(True)
        home.clicked.connect(self.go_home)
        self.tabs.setCornerWidget(home, Qt.Corner.TopLeftCorner)
        ws.addWidget(self.tabs)
        self.screens.addWidget(self.workspace)

        self._build_generate_tab()
        self._build_edit_tab()
        self._build_flora_tab()
        self._build_view_tab()
        self.tabs.addTab(self.generate_tab, "Generate")
        self.tabs.addTab(self.edit_tab, "Edit")
        self.tabs.addTab(self.flora_tab, "Flora")
        self.tabs.addTab(self.view_tab, "Walk")
        self.tabs.currentChanged.connect(self._on_tab)

        self._build_menu()
        self.setup.set_display(self.display_mode, self.display_w, self.display_h)
        self.setup.display_changed.connect(self._on_display_changed)

        self.poll = QTimer(self)
        self.poll.timeout.connect(self._poll)
        self.poll.start(400)
        self._refresh_nav()

    # ----- layout -----
    def _build_generate_tab(self):
        self.generate_tab = QWidget()
        lay = QVBoxLayout(self.generate_tab)
        lay.setContentsMargins(0, 0, 0, 0)
        self.gen_stack = QStackedWidget()
        self.setup = StartMenu()
        self.setup.start_clicked.connect(self._start_generation)
        self.setup.load_clicked.connect(self.import_world)
        self.gen_stack.addWidget(self.setup)
        lay.addWidget(self.gen_stack, 1)

        self.gen_bar = QFrame()
        self.gen_bar.setStyleSheet("QFrame{background:#0f1620; border-top:1px solid #243446;}")
        bar = QHBoxLayout(self.gen_bar)
        self.gen_status = QLabel("")
        self.gen_status.setStyleSheet("color:#c4cfdc;")
        self.gen_back_btn = QPushButton("Back to settings")
        self.gen_edit_btn = QPushButton("Edit map →")
        self.gen_flora_btn = QPushButton("Flora →")
        self.gen_view_btn = QPushButton("Walk →")
        bar.addWidget(self.gen_status, 1)
        for b in (self.gen_back_btn, self.gen_edit_btn, self.gen_flora_btn, self.gen_view_btn):
            bar.addWidget(b)
        self.gen_back_btn.clicked.connect(self._gen_back)
        self.gen_edit_btn.clicked.connect(lambda: self.show_tab(self.edit_tab))
        self.gen_flora_btn.clicked.connect(lambda: self.show_tab(self.flora_tab))
        self.gen_view_btn.clicked.connect(lambda: self.show_tab(self.view_tab))
        self.gen_bar.hide()
        lay.addWidget(self.gen_bar)

    def _build_edit_tab(self):
        self.edit_tab = QWidget()
        lay = QVBoxLayout(self.edit_tab)
        lay.setContentsMargins(0, 0, 0, 0)
        self.edit_stack = QStackedWidget()
        empty = QLabel("No world yet. Generate one, or import a .wglb file.")
        empty.setAlignment(Qt.AlignmentFlag.AlignCenter)
        empty.setStyleSheet("color:#8a97a8; font-size:15px;")
        self.edit_stack.addWidget(empty)
        self.edit_host = QWidget()
        self.edit_host_lay = QVBoxLayout(self.edit_host)
        self.edit_host_lay.setContentsMargins(0, 0, 0, 0)
        self.edit_stack.addWidget(self.edit_host)
        lay.addWidget(self.edit_stack)

    def _build_flora_tab(self):
        self.flora_tab = QWidget()
        lay = QVBoxLayout(self.flora_tab)
        lay.setContentsMargins(0, 0, 0, 0)
        self.flora_stack = QStackedWidget()
        empty = QLabel("No world yet. Generate one, or import a .wglb file.")
        empty.setAlignment(Qt.AlignmentFlag.AlignCenter)
        empty.setStyleSheet("color:#8a97a8; font-size:15px;")
        self.flora_stack.addWidget(empty)
        self.flora_host = QWidget()
        self.flora_host_lay = QVBoxLayout(self.flora_host)
        self.flora_host_lay.setContentsMargins(0, 0, 0, 0)
        self.flora_stack.addWidget(self.flora_host)
        lay.addWidget(self.flora_stack)

    def _build_view_tab(self):
        self.view_tab = QWidget()
        lay = QVBoxLayout(self.view_tab)
        lay.setContentsMargins(0, 0, 0, 0)
        self.view_host = QStackedWidget()
        self.view_loading = LoadingPage()
        self.view_host.addWidget(self.view_loading)
        lay.addWidget(self.view_host)

    def _build_menu(self):
        bar = self.menuBar()
        f = bar.addMenu("&File")
        self._act(f, "&New world", self.new_world, "Ctrl+N")
        self._act(f, "&Import world…", self.import_world, "Ctrl+O")
        self.export_action = self._act(f, "&Export world…", self.export_world, "Ctrl+S")
        f.addSeparator()
        self._act(f, "&Start page", self.go_home)
        f.addSeparator()
        self._act(f, "&Quit", self.close, "Ctrl+Q")
        g = bar.addMenu("&Go")
        self._act(g, "&Generate", lambda: self.show_tab(self.generate_tab), "Ctrl+1")
        self._act(g, "&Edit", lambda: self.show_tab(self.edit_tab), "Ctrl+2")
        self._act(g, "&Flora", lambda: self.show_tab(self.flora_tab), "Ctrl+3")
        self._act(g, "&Walk", lambda: self.show_tab(self.view_tab), "Ctrl+4")

    def _act(self, menu, text, slot, shortcut=None):
        a = QAction(text, self)
        if shortcut:
            a.setShortcut(QKeySequence(shortcut))
        a.triggered.connect(lambda *_: slot())
        menu.addAction(a)
        return a

    # ----- navigation and state -----
    def show_tab(self, widget):
        self.screens.setCurrentWidget(self.workspace)
        if self.doc is not None or widget is self.generate_tab:
            self.tabs.setCurrentWidget(widget)

    def go_home(self):
        self._stop_walk()
        self.screens.setCurrentWidget(self.start_page)
        self._refresh_nav()

    def _refresh_nav(self):
        has = self.doc is not None
        self.tabs.setTabEnabled(self.tabs.indexOf(self.edit_tab), has)
        self.tabs.setTabEnabled(self.tabs.indexOf(self.view_tab), has)
        self.tabs.setTabEnabled(self.tabs.indexOf(self.flora_tab), has)
        self.gen_edit_btn.setEnabled(has)
        self.gen_flora_btn.setEnabled(has)
        self.gen_view_btn.setEnabled(has)
        self.export_action.setEnabled(has)
        self.start_page.set_continue(self.doc.name if has else None)
        self._refresh_title()

    def _refresh_title(self):
        if self.doc is None:
            self.setWindowTitle("Worldmaker")
            return
        mark = " *" if self.doc.unsaved() else ""
        self.setWindowTitle(f"Worldmaker — {self.doc.name}{mark}")

    def _on_tab(self, idx):
        w = self.tabs.widget(idx)
        if w is self.view_tab:
            self.start_walk()
        else:
            self._stop_walk()
        if w is self.flora_tab:
            self._show_flora()

    def _show_flora(self):
        if self.doc is None:
            return
        if self.flora_panel is None:
            self.flora_panel = FloraPanel(self.doc)
            self.flora_panel.walk_requested.connect(lambda: self.show_tab(self.view_tab))
            self.flora_panel.back_requested.connect(lambda: self.show_tab(self.edit_tab))
            self.flora_panel.export_requested.connect(self.export_world)
            self.flora_host_lay.addWidget(self.flora_panel)
        self.flora_stack.setCurrentWidget(self.flora_host)
        self.flora_panel.refresh()

    def _confirm_replace(self):
        """Ask before throwing away a world with unexported edits."""
        if self.doc is None or not self.doc.unsaved():
            return True
        answer = QMessageBox.question(
            self, "Unsaved edits",
            f"{self.doc.name} has edits that have not been exported. Discard them?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No)
        return answer == QMessageBox.StandardButton.Yes

    def _set_doc(self, doc):
        self._stop_walk()
        self._drop_editor()
        self.doc = doc
        self.editor = MapEditor(doc.map, doc.name)
        self.editor.next_requested.connect(lambda: self.show_tab(self.flora_tab))
        self.editor.export_requested.connect(self.export_world)
        self.editor.back_requested.connect(self.go_home)
        self.edit_host_lay.addWidget(self.editor)
        self.edit_stack.setCurrentWidget(self.edit_host)
        self._refresh_nav()
        if self.tabs.currentWidget() is self.flora_tab:
            self._show_flora()

    def _drop_flora(self):
        if self.flora_panel is not None:
            self.flora_host_lay.removeWidget(self.flora_panel)
            self.flora_panel.deleteLater()
            self.flora_panel = None
        self.flora_stack.setCurrentIndex(0)

    def _drop_editor(self):
        self._drop_flora()
        if self.editor is not None:
            self.edit_host_lay.removeWidget(self.editor)
            self.editor.deleteLater()
            self.editor = None

    def _clear_doc(self):
        self._stop_walk()
        self._drop_editor()
        self.doc = None
        self.edit_stack.setCurrentIndex(0)
        if self.tabs.currentWidget() in (self.edit_tab, self.flora_tab, self.view_tab):
            self.tabs.setCurrentWidget(self.generate_tab)
        self._refresh_nav()

    # ----- generate -----
    def new_world(self):
        self.show_tab(self.generate_tab)
        self.gen_stack.setCurrentWidget(self.setup)

    def _start_generation(self, settings):
        if not self._confirm_replace():
            return
        self._clear_doc()
        self._clear_explorer()
        self.explorer = Explorer(settings)
        self.explorer.back_requested.connect(self._gen_back)
        self.explorer.save_requested.connect(self.export_world)
        self.gen_stack.addWidget(self.explorer)
        self.gen_stack.setCurrentWidget(self.explorer)
        self.gen_bar.show()
        self.gen_status.setText("Generating…")

    def _clear_explorer(self):
        if self.explorer is not None:
            self.gen_stack.removeWidget(self.explorer)
            self.explorer.deleteLater()
            self.explorer = None
        self.gen_bar.hide()

    def _gen_back(self):
        self.gen_stack.setCurrentWidget(self.setup)
        self.gen_bar.setVisible(self.explorer is not None)

    def _poll(self):
        if self.explorer is not None and self.doc is None:
            w = self.explorer.world
            if w.done >= w.total:
                self._set_doc(WorldDoc.from_world(w, self.explorer.s))
                self.gen_status.setText("World ready. Edit the map, or go on to Flora or Walk.")
            else:
                self.gen_status.setText(f"Generating… {w.done} / {w.total} chunks")
        self._refresh_title()

    # ----- import / export -----
    def import_world(self, path=None):
        if not isinstance(path, str) or not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "Import world", "", "Worldgen Lab (*.wglb *.wg);;All files (*)")
        if not path:
            return
        if not self._confirm_replace():
            return
        try:
            doc = WorldDoc.from_file(path)
        except Exception as exc:
            QMessageBox.warning(self, "Could not import world", str(exc))
            return
        if doc.generator != GENERATOR_VERSION:
            QMessageBox.warning(
                self, "Generator version differs",
                f"This world was saved by generator version {doc.generator}, but this build is "
                f"{GENERATOR_VERSION}. The terrain may not match the saved map.")
        self._clear_explorer()
        self.setup.apply_settings(doc.settings)
        self._set_doc(doc)
        self.show_tab(self.edit_tab)

    def export_world(self):
        if self.doc is None or not self.doc.complete:
            QMessageBox.information(
                self, "Nothing to export",
                "Generate or import a world first. Generation has to finish before it can be saved.")
            return
        src = self.doc.path
        default = str(src.with_name(src.stem + "_edited.wglb")) if src else f"world_{self.doc.settings.seed}.wglb"
        path, _ = QFileDialog.getSaveFileName(
            self, "Export world", default, "Worldgen Lab (*.wglb);;All files (*)")
        if not path:
            return
        if not path.lower().endswith(".wglb"):
            path += ".wglb"
        try:
            self.doc.export(path)
        except Exception as exc:
            QMessageBox.warning(self, "Export failed", str(exc))
            return
        if self.editor is not None:
            self.editor._update_status()
        if self.flora_panel is not None:
            self.flora_panel.refresh()
        self._refresh_title()
        self.statusBar().showMessage(f"Exported {path}", 6000)

    # ----- walk (Walk tab) -----
    def start_walk(self):
        if self.doc is None:
            return
        self._stop_walk()
        d = self.doc
        info = d.walk_info()
        self.view_loading.set_progress(0, 1)
        self.view_host.setCurrentWidget(self.view_loading)
        s = Streamer(info)
        self.streamer = s
        s.progress.connect(self.view_loading.set_progress)
        s.spawn_ready.connect(lambda: self._show_world(s))
        s.failed.connect(self._walk_failed)

    def _show_world(self, streamer):
        if streamer is not self.streamer:
            return
        view = ViewWidget(streamer, streamer.spawn())
        view.back_requested.connect(self.go_home)
        view.editor_requested.connect(lambda: self.show_tab(self.edit_tab))
        if self.view is not None:
            self.view.shutdown()
            self.view_host.removeWidget(self.view)
            self.view.deleteLater()
        self.view = view
        self.view_host.addWidget(view)
        self.view_host.setCurrentWidget(view)
        view.setFocus()

    def _walk_failed(self, msg):
        self._stop_walk()
        QMessageBox.warning(self, "Could not walk this world", msg)

    def _stop_walk(self):
        if self.view is not None:
            self.view.shutdown()
            self.view_host.removeWidget(self.view)
            self.view.deleteLater()
            self.view = None
        if self.streamer is not None:
            self.streamer.shutdown()
            self.streamer = None
        self.view_host.setCurrentWidget(self.view_loading)

    # ----- display and window -----
    def _on_display_changed(self, mode, w, h):
        self.display_mode = mode
        self.display_w = w
        self.display_h = h
        self.prefs.setValue("display2/mode", mode)
        self.prefs.setValue("display2/width", w)
        self.prefs.setValue("display2/height", h)
        self.prefs.sync()
        self.apply_display()

    def available_rect(self):
        return self.screen().availableGeometry()

    def apply_display(self):
        mode = self.display_mode
        if mode == "fullscreen":
            self.setWindowFlags(Qt.WindowType.Window)
            self.show()
            self.showFullScreen()
            return
        if mode == "borderless":
            self.setWindowFlags(Qt.WindowType.FramelessWindowHint)
            self.show()
            self.setWindowState(Qt.WindowState.WindowNoState)
            self.setGeometry(self.available_rect())
            return
        self.setWindowFlags(Qt.WindowType.Window)
        self.show()
        self.setWindowState(Qt.WindowState.WindowNoState)
        if not (self.display_w and self.display_h):
            self.showMaximized()
            return
        avail = self.available_rect()
        extra_w = self.frameGeometry().width() - self.geometry().width()
        extra_h = self.frameGeometry().height() - self.geometry().height()
        w = min(self.display_w, avail.width() - extra_w)
        h = min(self.display_h, avail.height() - extra_h)
        self.resize(w, h)
        fr = self.frameGeometry()
        left = self.x() - fr.x()
        top = self.y() - fr.y()
        x = avail.x() + (avail.width() - fr.width()) // 2
        y = avail.y() + (avail.height() - fr.height()) // 2
        self.move(x + left, y + top)

    def closeEvent(self, e):
        if not self._confirm_replace():
            e.ignore()
            return
        self._stop_walk()
        super().closeEvent(e)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_F11:
            if self.isFullScreen():
                self.showNormal()
            else:
                self.showFullScreen()
        else:
            super().keyPressEvent(event)


def main():
    fmt = QSurfaceFormat()
    fmt.setVersion(2, 1)
    fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
    fmt.setDepthBufferSize(24)
    fmt.setSamples(4)
    QSurfaceFormat.setDefaultFormat(fmt)
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = MainWindow()
    win.apply_display()
    if len(sys.argv) > 1:
        win.import_world(sys.argv[1])
    sys.exit(app.exec())



BIOME_BOUNDS = {}


def validate_world(world: "World"):
    s = world.s
    CH = s.chunk
    n = s.num_chunks
    issues = []
    grid = {}
    for cy in range(n):
        for cx in range(n):
            grid[(cx, cy)] = world.generate_chunk(cx, cy)
    hs = np.concatenate([grid[k]["height"].ravel() for k in grid]).astype(np.float64)
    bs = np.concatenate([grid[k]["biome"].ravel() for k in grid])
    if hs.max() >= 0.95 or hs.min() <= 0.0:
        issues.append(f"height outside (0, 0.95): min {hs.min():.4f} max {hs.max():.4f}")
    worst = 0.0
    for (cx, cy), a in grid.items():
        A = a["height"].astype(np.float64)
        inner = max(np.abs(np.diff(A, axis=1)).max(), 1e-9)
        if (cx + 1, cy) in grid:
            d = np.abs(A[:, -1] - grid[(cx + 1, cy)]["height"][:, 0].astype(np.float64)).max()
            worst = max(worst, d / max(inner * 4, 0.05))
        if (cx, cy + 1) in grid:
            d = np.abs(A[-1, :] - grid[(cx, cy + 1)]["height"][0, :].astype(np.float64)).max()
            worst = max(worst, d / max(inner * 4, 0.05))
    if worst > 1.0:
        issues.append(f"chunk seam jump {worst:.2f}x the allowed limit")
    land = hs >= world.sea_level
    total_land = max(1, int(land.sum()))
    shares = {}
    for i, (name, _) in enumerate(BIOMES):
        share = float((bs == i).sum()) / total_land
        shares[name] = share
        if name in BIOME_BOUNDS:
            lo, hi = BIOME_BOUNDS[name]
            if not (lo <= share <= hi):
                issues.append(f"{name} share {share:.3f} outside [{lo}, {hi}]")
    water = float((hs < world.sea_level).mean())
    if water < 0.3 or water > 0.85:
        issues.append(f"water share {water:.3f} outside [0.3, 0.85]")
    return {"ok": not issues, "issues": issues, "height_min": float(hs.min()),
            "height_max": float(hs.max()), "water_share": water, "land_shares": shares,
            "seam_ratio": worst}


if __name__ == "__main__":
    main()
