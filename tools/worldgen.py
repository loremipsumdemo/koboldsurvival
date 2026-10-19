from __future__ import annotations

import hashlib
import heapq
import json
import math
import os
import pickle
import random
import re
import sys
import time
import traceback
from collections import OrderedDict
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, asdict
from pathlib import Path

import numpy as np
from PyQt6.QtCore import QPointF, QRectF, QSettings, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import (
    QAction, QBrush, QColor, QFont, QImage, QPainter, QPen, QPixmap,
)
from PyQt6.QtWidgets import (
    QApplication, QComboBox, QProgressDialog, QDoubleSpinBox, QFileDialog, QFormLayout,
    QFrame, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QListWidget, QMainWindow,
    QMessageBox, QPushButton, QScrollArea, QSizePolicy, QSlider, QSpinBox,
    QStackedWidget, QTabWidget, QVBoxLayout, QWidget,
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

BIOMES = [
    ("Deep Ocean", (12, 30, 60)),
    ("Ocean",      (30, 70, 115)),
    ("Beach",      (198, 186, 140)),
    ("Snow",       (238, 243, 250)),
    ("Tundra",     (150, 160, 158)),
    ("Taiga",      (60, 96, 74)),
    ("Grassland",  (126, 160, 90)),
    ("Shrubland",  (160, 152, 100)),
    ("Desert",     (214, 190, 134)),
    ("Forest",     (64, 112, 66)),
    ("Rainforest", (34, 90, 58)),
    ("Mountain",   (126, 120, 112)),
    ("Lake",       (58, 118, 168)),
    ("Delta",      (132, 146, 84)),
    ("Cliff",      (92, 88, 84)),
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
VERSION = 2
GENERATOR_VERSION = "2.1"
VIEW_MODES = [("biome", "Biome"), ("height", "Height"), ("temperature", "Temperature"), ("moisture", "Moisture")]

def save_world(path: Path, settings: Settings, world: "World"):
    meta = {
        "magic": MAGIC.decode("ascii"),
        "version": VERSION,
        "settings": settings.to_dict(),
        "sea_level": world.sea_level,
        "river_threshold": world.river_threshold,
        "done": world.done,
        "generator": GENERATOR_VERSION,
    }
    payload = {
        "meta": meta,
        "overview": world.overview,
        "chunk_ready": world.chunk_ready,
        "lowres": world.lowres,
    }
    with open(path, "wb") as f:
        pickle.dump(payload, f, protocol=pickle.HIGHEST_PROTOCOL)

def load_world(path: Path):
    with open(path, "rb") as f:
        payload = pickle.load(f)
    meta = payload["meta"]
    if meta.get("magic") != MAGIC.decode("ascii"):
        raise ValueError("Not a Worldgen Lab file.")
    if meta.get("version") not in (1, 2):
        raise ValueError(f"Unsupported version: {meta.get('version')}")
    settings = Settings.from_dict(meta["settings"])
    return (
        settings,
        payload["overview"],
        payload["chunk_ready"],
        float(meta["sea_level"]),
        int(meta["river_threshold"]),
        int(meta["done"]),
        payload.get("lowres"),
        meta.get("generator", "1.0"),
    )

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
        n = fbm(np.asarray(fx, np.float64) * 14.0, np.asarray(fy, np.float64) * 14.0, S + 77, 4) - 0.5
        rock = (1.0 - 0.5 * geo) + geo * hard(fx, fy)
        hi = _smooth(0.0, 0.2, v - sea)
        mask = (1.0 - mask_k) + mask_k * hi
        return v + amp * n * rock * mask
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


def _wind_moisture(E, land, lat):
    R = E.shape[0]
    decay = 0.012 + 0.07 * np.exp(-((lat - 0.33) / 0.09) ** 2)
    row_lat = lat[:, 0]
    east = (row_lat >= 0.33) & (row_lat < 0.66)
    results = {}
    for direction in (1, -1):
        m = np.zeros(R)
        prev = None
        out = np.zeros((R, R))
        cols = range(R) if direction == 1 else range(R - 1, -1, -1)
        for i in cols:
            e = E[:, i]
            rise = np.zeros(R) if prev is None else np.clip(e - prev, 0.0, None)
            m = np.where(land[:, i], m, 1.0)
            rain = m * (1.0 - np.exp(-(4.0 * rise + 0.015)))
            m = m - rain
            out[:, i] = m
            m = m * (1.0 - decay[:, i])
            prev = e
        results[direction] = out
    return np.where(east[:, None], results[1], results[-1])


CLIMATE_RES = 160


def _climate_ranks(sampler, sea, S, R=96):
    c = (np.arange(R) + 0.5) / R
    FX, FY = np.meshgrid(c, c)
    E = np.asarray(sampler(FX, FY), np.float64)
    land = E >= sea
    lat = np.abs(FY * 2 - 1)
    alt = np.clip(E - sea, 0.0, None)
    wind = _wind_moisture(E, land, lat)
    T0 = (1 - lat ** 1.25 * 0.85 - alt * 1.0 - 0.3 * _smooth(0.78, 0.95, lat)
          + (fbm(FX * 3, FY * 3, S + 404, 3) - 0.5) * 0.12)
    M0 = wind + (fbm(FX * 3.5 + 13.7, FY * 3.5 + 7.1, S + 909, 4) - 0.5) * 0.12
    return _rank(T0, land), _rank(M0, land), _rank(E, land)


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
        self.river_threshold = river_threshold_for(settings)
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
                                    0.10 * float(settings.detail), float(settings.mask_strength),
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
        self.climate_t, self.climate_m, self.climate_e = _climate_ranks(self.sampler, self.sea_level, int(settings.seed), R=CLIMATE_RES)

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

        height = np.asarray(self.sampler(FX, FY), np.float32)

        hard = self.hard_fn(FX, FY)
        hi = _smooth(0.0, 0.2, height.astype(np.float64) - self.sea_level)
        mask = ((1.0 - s.mask_strength) + s.mask_strength * hi).astype(np.float32)
        if s.method == "eroded":
            self._erode(height, cx, cy, hard.astype(np.float32), mask)

        temp = _bilerp(self.climate_t, FX, FY).astype(np.float32)
        moist = _bilerp(self.climate_m, FX, FY).astype(np.float32)
        elev = _bilerp(self.climate_e, FX, FY)

        dx = np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)
        dy = np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)
        dx[:, 0] = 0; dx[:, -1] = 0
        dy[0, :] = 0; dy[-1, :] = 0
        slope = np.clip(np.sqrt(dx * dx + dy * dy) * 34, 0, 1).astype(np.float32)

        padded = np.pad(height, 1, constant_values=np.inf)
        NB = np.stack([
            padded[1 + oy:1 + oy + CH, 1 + ox:1 + ox + CH] for oy, ox in _NEIGH8
        ])
        k = np.argmin(NB, axis=0)
        lowest = np.take_along_axis(NB, k[None], axis=0)[0]
        yy, xx = np.indices((CH, CH))
        target = (yy + _NDY[k]) * CH + (xx + _NDX[k])
        best = np.where(lowest < height, target, -1).ravel().tolist()

        order = np.argsort(-height, axis=None).tolist()
        flow_l = [1.0] * (CH * CH)
        for i in order:
            b = best[i]
            if b >= 0:
                flow_l[b] += flow_l[i]
        flow = np.array(flow_l, np.float32).reshape(CH, CH)

        hf = height.astype(np.float64)
        tf = temp.astype(np.float64); mf = moist.astype(np.float64); sf = slope.astype(np.float64)
        blend = float(s.biome_blend)
        seed = int(s.seed)
        j1 = fbm(FX * 21.0 + 1.1, FY * 21.0 + 4.2, seed + 515, 3) - 0.5
        j2 = fbm(FX * 17.0 + 7.7, FY * 17.0 + 2.9, seed + 616, 3) - 0.5
        dither = fbm(FX * 40.0 + 9.0, FY * 40.0 + 9.0, seed + 727, 2) > 0.5
        biome_a = self._classify(elev, hf, tf, mf, sf, self.sea_level)
        biome_b = self._classify(elev, hf, tf + j1 * 0.3 * blend, mf + j2 * 0.3 * blend, sf, self.sea_level)
        biome = np.where(dither, biome_b, biome_a).astype(np.uint8)
        thr = self.river_threshold
        land = height >= self.sea_level
        sink = np.array(best).reshape(CH, CH) < 0
        sink[0, :] = sink[-1, :] = sink[:, 0] = sink[:, -1] = False
        lake = sink & land & (flow >= 0.5 * thr)
        delta = (flow > thr) & land & (height < self.sea_level + 0.025) & ~lake
        biome[lake] = 12
        biome[delta] = 13

        return {"height": height, "temp": temp, "moist": moist,
                "slope": slope, "flow": flow, "biome": biome}

    def _erode(self, H, cx, cy, hard, mask):
        CH = self.s.chunk
        geo = float(self.s.geology)
        rnd = mulberry32(int(self.s.seed) ^ (cx * 73856093) ^ (cy * 19349663))
        drops = int(CH * CH * 0.1)
        for _ in range(drops):
            px = 1 + rnd() * (CH - 3)
            py = 1 + rnd() * (CH - 3)
            dirx = diry = 0.0
            speed = 1.0; water = 1.0; sed = 0.0
            for _l in range(26):
                nx = int(px); ny = int(py)
                fx2 = px - nx; fy2 = py - ny
                h00 = H[ny, nx]; h10 = H[ny, nx + 1]
                h01 = H[ny + 1, nx]; h11 = H[ny + 1, nx + 1]
                gx = (h10 - h00) * (1 - fy2) + (h11 - h01) * fy2
                gy = (h01 - h00) * (1 - fx2) + (h11 - h10) * fx2
                dirx = dirx * 0.6 - gx * 500
                diry = diry * 0.6 - gy * 500
                ln = math.hypot(dirx, diry)
                if ln < 1e-5: break
                dirx /= ln; diry /= ln
                px += dirx; py += diry
                if px < 1 or px > CH - 2 or py < 1 or py > CH - 2: break
                nix = int(px); niy = int(py)
                dh = H[niy, nix] - H[ny, nx]
                cap = max(-dh, 0.0002) * speed * water * 0.7
                if sed > cap or dh > 0:
                    amt = min(dh, sed) if dh > 0 else (sed - cap) * 0.25
                    sed -= amt
                    H[ny, nx] += amt * 0.6
                else:
                    amt = min((cap - sed) * 0.25, -dh)
                    w = (1.0 - 0.7 * geo * float(hard[ny, nx])) * float(mask[ny, nx])
                    H[ny, nx] -= amt * w
                    sed += amt
                speed = math.sqrt(max(0.05, speed * speed + dh * 6))
                water *= 0.985

    def _classify(self, e, h, t, m, sl, sea):
        alt = h - sea
        conds = [
            h < sea - 0.055,
            h < sea,
            (h < sea + 0.05) & (sl < 0.8),
            (h < sea + 0.02) & (sl >= 0.8),
            (e > 0.86) & (sl > 0.06),
            (t < 0.05),
            (t < 0.14) & (alt > 0.22),
            t < 0.14,
            t < 0.30,
            (m < 0.22) & (t > 0.40),
            (m < 0.32) & (t > 0.40),
            m < 0.18,
            m < 0.50,
            (m < 0.86) | (t <= 0.55),
        ]
        choices = [0, 1, 2, 14, 11, 3, 3, 4, np.where(m > 0.4, 5, 4), 8, 7, 7, 6, 9]
        choices = [np.broadcast_to(np.asarray(c), h.shape) for c in choices]
        return np.select(conds, choices, default=10)

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
        h = lr["h"]
        sea = self.sea_level
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
        _frow(f, "Land height curve", self.gamma_slider, "live")
        _frow(f, "Fine detail", self.detail_slider, "live")
        _frow(f, "Geology contrast", self.geo_slider, "live")
        _frow(f, "Mask strength", self.mask_slider, "live")
        _frow(f, "Biome blend", self.blend_slider, "live")

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
                         self.mask_slider, self.blend_slider)
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
        self._dv["River threshold"].setText(f"{river_threshold_for(s)} flow cells / chunk")
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

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Worldgen Lab")
        self.setMinimumSize(960, 600)
        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)

        self.menu = StartMenu()
        self.menu.start_clicked.connect(self._on_start)
        self.menu.load_clicked.connect(self._on_load)
        self.stack.addWidget(self.menu)

        self.prefs = QSettings("WorldgenLab", "WorldgenLab")
        self.display_mode = str(self.prefs.value("display2/mode", "borderless"))
        self.display_w = _to_int(self.prefs.value("display2/width", 0))
        self.display_h = _to_int(self.prefs.value("display2/height", 0))
        self.menu.set_display(self.display_mode, self.display_w, self.display_h)
        self.menu.display_changed.connect(self._on_display_changed)

        self.explorer: Explorer | None = None
        self.last_save_path: Path | None = None

    def _on_display_changed(self, mode, w, h):
        self.display_mode = mode
        self.display_w = w
        self.display_h = h
        self.prefs.setValue("display2/mode", mode)
        self.prefs.setValue("display2/width", w)
        self.prefs.setValue("display2/height", h)
        self.prefs.sync()
        self.apply_display()

    def set_display_mode(self, mode):
        self.menu.set_display(mode, self.display_w, self.display_h)
        self._on_display_changed(mode, self.display_w, self.display_h)

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

    def keyPressEvent(self, event):
        k = event.key()
        if k == Qt.Key.Key_Escape and self.display_mode != "windowed":
            self.set_display_mode("windowed")
        elif k == Qt.Key.Key_F11:
            self.set_display_mode("windowed" if self.display_mode != "windowed" else "borderless")
        else:
            super().keyPressEvent(event)

    def _on_start(self, settings: Settings):
        try:
            if self.explorer is not None:
                self.stack.removeWidget(self.explorer)
                self.explorer.deleteLater()
            self.explorer = Explorer(settings)
            self.explorer.save_requested.connect(self._on_save)
            self.explorer.back_requested.connect(self.show_menu)
            self.stack.addWidget(self.explorer)
            self.stack.setCurrentWidget(self.explorer)
        except Exception:
            excepthook(*sys.exc_info())

    def _on_load(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Load World", "", "Worldgen Lab (*.wglb *.wg);;All files (*)"
        )
        if not path:
            return
        try:
            settings, overview, chunk_ready, sea, rt, done, lowres, _gen = load_world(Path(path))
            if _gen != GENERATOR_VERSION:
                QMessageBox.warning(
                    self, "Generator version differs",
                    f"This world was saved by generator version {_gen}, but this build is "
                    f"{GENERATOR_VERSION}. The terrain may not match the saved preview.")
            if self.explorer is not None:
                self.stack.removeWidget(self.explorer)
                self.explorer.deleteLater()
            self.explorer = Explorer(
                settings,
                restored=(overview, chunk_ready, sea, rt, done, lowres),
            )
            self.explorer.save_requested.connect(self._on_save)
            self.explorer.back_requested.connect(self.show_menu)
            self.stack.addWidget(self.explorer)
            self.stack.setCurrentWidget(self.explorer)
            self.menu.apply_settings(settings)
            self.last_save_path = Path(path)
        except Exception:
            excepthook(*sys.exc_info())

    def _on_save(self):
        if self.explorer is None:
            return
        if self.last_save_path is not None:
            default = str(self.last_save_path)
        else:
            default = str(Path.home() /
                          f"world_{self.explorer.s.seed}_{encode_code(self.explorer.s)}.wglb")
        path, _ = QFileDialog.getSaveFileName(
            self, "Save World", default,
            "Worldgen Lab (*.wglb);;All files (*)",
        )
        if not path:
            return
        try:
            save_world(Path(path), self.explorer.s, self.explorer.world)
            self.last_save_path = Path(path)
            QMessageBox.information(self, "Saved", f"Saved to:\n{path}")
        except Exception:
            excepthook(*sys.exc_info())

    def show_menu(self):
        self.stack.setCurrentWidget(self.menu)

def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = MainWindow()
    win.apply_display()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()


BIOME_BOUNDS = {
    "Desert": (0.04, 0.22), "Mountain": (0.05, 0.22), "Snow": (0.03, 0.14),
    "Forest": (0.12, 0.34), "Rainforest": (0.02, 0.16), "Grassland": (0.04, 0.20),
    "Taiga": (0.03, 0.16), "Tundra": (0.02, 0.12), "Shrubland": (0.03, 0.14),
    "Lake": (0.0, 0.02), "Delta": (0.0, 0.04),
}


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
    land = bs >= 2
    total_land = max(1, int(land.sum()))
    shares = {}
    for i, (name, _) in enumerate(BIOMES):
        if i < 2:
            continue
        share = float((bs == i).sum()) / total_land
        shares[name] = share
        if name in BIOME_BOUNDS:
            lo, hi = BIOME_BOUNDS[name]
            if not (lo <= share <= hi):
                issues.append(f"{name} share {share:.3f} outside [{lo}, {hi}]")
    water = float((bs < 2).mean())
    if water < 0.3 or water > 0.85:
        issues.append(f"water share {water:.3f} outside [0.3, 0.85]")
    return {"ok": not issues, "issues": issues, "height_min": float(hs.min()),
            "height_max": float(hs.max()), "water_share": water, "land_shares": shares,
            "seam_ratio": worst}

