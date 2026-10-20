"""Worldmaker: generate one whole world, edit it, theme it and walk through it in 3D.

Tabs
  Generate  pick a seed and settings; the whole world is built in one pass
  Edit      raise, lower, smooth, erode and repaint the ground; warmer, wetter and so on
  Theme     sky, sun, fog, water, clouds and rock, previewed on the whole map
  Walk      walk or fly over the entire world

Files
  .wglb  the world: heights, ground, plants, climate (Ctrl+O / Ctrl+S)
  .wglv  a Map Theme on its own (Export and Import in the Theme tab)

Run:  python3 worldmaker.py [optional .wglb file]
Needs: PyQt6, PyOpenGL, numpy. This single file is all you need.
"""

from __future__ import annotations

# ===== generator, world format and editor =====



import copy
import json
import math
import random
from dataclasses import asdict, dataclass, field, fields

import numpy as np

MAGIC = "WGLB"
WORLD_VERSION = 7
WORK = 256             # coarse grid for weather and erosion; final tiles are sampled from it
EROSION_ITERS = 24
RESIDUAL = 0.15        # share of fine detail kept on top of the eroded shape
VS_FRAC = 0.12         # vertical scale of the 3D walk, as a share of the longer side
TALUS = 0.012
MAX_UNDO = 20

# ---------- ground types ----------
(SILT, SEABED_SAND, BEACH_SAND, ICE, FROZEN_CLAY, LOAM, CLAY, GRAVEL, DESERT_SAND,
 ARID_SOIL, BEDROCK, SCREE, FLOODPLAIN, DELTA_SILT, CLIFF_ROCK) = range(15)
BIOMES = [
    ("Deep silt", (92, 86, 78)), ("Seabed sand", (176, 162, 120)), ("Beach sand", (222, 206, 160)),
    ("Ice", (226, 236, 244)), ("Frozen clay", (150, 156, 158)), ("Loam", (120, 96, 70)),
    ("Clay", (166, 116, 88)), ("Gravel", (148, 140, 128)), ("Desert sand", (214, 190, 134)),
    ("Arid soil", (178, 148, 104)), ("Bedrock", (110, 106, 102)), ("Scree", (126, 120, 112)),
    ("Floodplain silt", (150, 138, 104)), ("Delta silt", (132, 124, 90)), ("Cliff rock", (92, 88, 84)),
]
BIOME_RGB = np.array([b[1] for b in BIOMES], np.float64)
HYDRO = (FLOODPLAIN, DELTA_SILT)
LIGHT = np.array([0.45, 0.8, 0.35]) / np.linalg.norm([0.45, 0.8, 0.35])

METHODS = [("fbm", "Fractal noise"), ("ridged", "Ridged mountains"), ("warp", "Domain-warped"),
           ("voronoi", "Voronoi cells")]


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


# ---------- noise ----------
def hash2(ix, iy, seed):
    h = (ix * 374761393) ^ (iy * 668265263) ^ (seed * 1274126177)
    h &= 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return (h & 0xFFFFFFFF) / 4294967295.0


def vnoise(x, y, seed):
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


def lattice_map(table, xs, ys):
    """Value noise over a whole grid, read from a random table. xs and ys are the 1D positions of
    the columns and rows, so the work is two passes rather than one lookup per tile."""
    P = table.shape[0]

    def cell(v):
        i0 = np.floor(v).astype(np.int64)
        f = (v - i0).astype(np.float32)
        return i0 & (P - 1), (i0 + 1) & (P - 1), f * f * (3.0 - 2.0 * f)

    i0, i1, u = cell(xs)
    j0, j1, v = cell(ys)
    rows = table[j0] * (1 - v)[:, None] + table[j1] * v[:, None]   # (rows, P)
    return rows[:, i0] * (1 - u) + rows[:, i1] * u                 # (rows, columns)


def fbm(x, y, seed, octaves=6):
    amp, f, s, n = 0.5, 1.0, 0.0, 0.0
    for i in range(octaves):
        s = s + amp * vnoise(x * f, y * f, seed + i * 131)
        n += amp
        amp *= 0.5; f *= 2
    return s / n


def ridged(x, y, seed, octaves=6):
    amp, f, s, n = 0.5, 1.0, 0.0, 0.0
    for i in range(octaves):
        v = 1.0 - np.abs(vnoise(x * f, y * f, seed + i * 7919) * 2 - 1)
        s = s + amp * v * v
        n += amp
        amp *= 0.5; f *= 2
    return s / n


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def soft_unit(v, knee=0.94):
    v = np.asarray(v, np.float64)
    out = v.copy()
    hi = v > knee
    lo = v < 1.0 - knee
    out[hi] = knee + (1 - knee) * np.tanh((v[hi] - knee) / (1 - knee))
    out[lo] = (1 - knee) - (1 - knee) * np.tanh(((1 - knee) - v[lo]) / (1 - knee))
    return out


def layered(S, x, y):
    """Fractal terrain built in layers: a continent shape, rolling hills, ridged mountains in
    belts, valleys cut through those belts, and fine rock detail. Each layer is warped a little,
    so the shapes read as ground rather than uniform noise."""
    wx = fbm(x * 1.6, y * 1.6, S + 11, 3) - 0.5
    wy = fbm(x * 1.6 + 5.2, y * 1.6 + 1.3, S + 12, 3) - 0.5
    xw = x + 0.45 * wx
    yw = y + 0.45 * wy
    continent = fbm(xw * 2.0, yw * 2.0, S + 21, 4)
    hills = fbm(xw * 6.0, yw * 6.0, S + 31, 5)
    belt = smooth(0.45, 0.75, fbm(xw * 2.6 + 3.0, yw * 2.6 + 7.0, S + 41, 3))
    ridges = ridged(xw * 5.0, yw * 5.0, S + 51, 5)
    valleys = ridged(xw * 7.0 + 2.0, yw * 7.0 + 9.0, S + 61, 4)
    fine = fbm(x * 18.0, y * 18.0, S + 71, 3)
    return (0.55 * continent + 0.25 * hills + 0.35 * belt * ridges
            - 0.12 * belt * valleys + 0.06 * (fine - 0.5))


def base_noise(method, S, x, y):
    if method == "ridged":
        return ridged(x * 10, y * 10, S + 17, 6) * 0.88 + fbm(x * 5, y * 5, S + 91, 3) * 0.24 - 0.05
    if method == "warp":
        wx = fbm(x * 8, y * 8, S + 11, 4) - 0.5
        wy = fbm(x * 8 + 31.7, y * 8 + 11.3, S + 22, 4) - 0.5
        return fbm(x * 12 + wx * 7, y * 12 + wy * 7, S + 33, 6)
    if method == "voronoi":
        G = 50
        cx = np.floor(x * G); cy = np.floor(y * G)
        ds = []
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                gx = cx + ox; gy = cy + oy
                gxi = gx.astype(np.int64); gyi = gy.astype(np.int64)
                jx = (gx + hash2(gxi, gyi, S + 1)) / G
                jy = (gy + hash2(gxi, gyi, S + 2)) / G
                ds.append(np.hypot(x - jx, y - jy))
        D = np.sort(np.stack(ds), axis=0)
        return (D[1] - D[0]) * 2.08 + fbm(x * 9, y * 9, S + 7, 4) * 0.30
    return layered(S, x, y)


def quantile_map(values):
    vals = np.sort(np.asarray(values, np.float64).ravel())
    ranks = np.linspace(0.0, 1.0, vals.size)
    return lambda v: np.interp(np.asarray(v, np.float64), vals, ranks)


def interp_matrix(n_out, n_in, a):
    """Weights that spread n_in grid values over n_out positions covering the domain length a.
    Each output position takes its two neighbours, weighted by distance (bilinear, one axis)."""
    p = (np.arange(n_out) + 0.5) / n_out * a
    g = np.clip(p / a * n_in - 0.5, 0, n_in - 1)
    i0 = np.floor(g).astype(np.int64)
    i1 = np.minimum(i0 + 1, n_in - 1)
    t = (g - i0).astype(np.float32)
    M = np.zeros((n_out, n_in), np.float32)
    rows = np.arange(n_out)
    np.add.at(M, (rows, i0), 1.0 - t)
    np.add.at(M, (rows, i1), t)
    return M


def sample_full(G, W, H, dom):
    """Bilinear stretch of a coarse grid G over a W x H map covering the domain: two matrix
    products, one per axis."""
    G = np.asarray(G, np.float32)
    GY, GX = G.shape
    My = interp_matrix(H, GY, dom[1])
    Mx = interp_matrix(W, GX, dom[0])
    return My @ G @ Mx.T


def grid(nx, ny, dom):
    ax, ay = dom
    FX = (np.arange(nx) + 0.5) / nx * ax
    FY = (np.arange(ny) + 0.5) / ny * ay
    return np.meshgrid(FX, FY)


# ---------- settings and world ----------
@dataclass
class Settings:
    method: str = "fbm"
    seed: int = 1
    width: int = 5120
    height: int = 5120
    water: float = 0.42
    scale: float = 1.0
    relief: float = 1.2
    erosion: float = 0.6
    lakes: float = 0.3
    detail: float = 0.7
    vegetation: float = 0.7
    lowlands: float = 0.3

    def to_dict(self):
        return asdict(self)

    @classmethod
    def from_dict(cls, d):
        names = {f.name for f in fields(cls)}
        return cls(**{k: v for k, v in d.items() if k in names})


def domain(s):
    L = float(max(s.width, s.height))
    return s.width / L, s.height / L


def work_shape(s):
    if s.width >= s.height:
        return WORK, max(8, round(WORK * s.height / s.width))
    return max(8, round(WORK * s.width / s.height)), WORK


@dataclass
class World:
    height: np.ndarray      # float32 (H, W), 0..1
    biome: np.ndarray       # uint8 ground type
    flora_kind: np.ndarray  # uint8: 0 none, 1 grass, 2 shrubs, 3 forest
    flora_amt: np.ndarray   # uint8 density of that plant cover
    temp: np.ndarray        # float32, 0..1
    moist: np.ndarray       # float32, 0..1
    sea: float


# ---------- hydrology ----------
_NEIGH8 = ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1))
_OUTLET = -1


def receivers(H):
    GY, GX = H.shape
    P = np.pad(H, 1, constant_values=-1.0)
    yy, xx = np.indices((GY, GX))
    best = np.zeros((GY, GX))
    rec = yy * GX + xx
    for oy, ox in _NEIGH8:
        N = P[1 + oy:1 + oy + GY, 1 + ox:1 + ox + GX]
        ny = yy + oy; nx = xx + ox
        off = (ny < 0) | (ny >= GY) | (nx < 0) | (nx >= GX)
        drop = (H - N) / (1.4142 if (oy and ox) else 1.0)
        better = drop > best
        best = np.where(better, drop, best)
        cand = np.where(off, _OUTLET, ny * GX + nx)
        rec = np.where(better, cand, rec)
    return rec.ravel()


def accumulate(rec, H, runoff):
    GY, GX = H.shape
    order = np.argsort(-H.ravel(), kind="stable").tolist()
    r = rec.tolist()
    A = np.asarray(runoff, np.float64).ravel().tolist()
    for c in order:
        t = r[c]
        if t != c and t >= 0:
            A[t] += A[c]
    return np.array(A).reshape(GY, GX)


def lake_mask(H, rec, A, land, lakes, fill):
    GY, GX = H.shape
    idx = np.arange(GY * GX).reshape(GY, GX)
    sink = (rec.reshape(GY, GX) == idx) & land
    q = 0.985 - 0.04 * float(lakes)
    thr = float(np.quantile(A[land], q)) if land.any() else np.inf
    core = (sink | ((fill > 0.004) & land)) & (A >= thr)
    level = np.where(core, H, np.inf)
    P = np.pad(level, 1, constant_values=np.inf)
    nb = np.min(np.stack([P[1 + oy:1 + oy + GY, 1 + ox:1 + ox + GX] for oy, ox in _NEIGH8]), axis=0)
    pool = np.isfinite(nb) & land & (H <= nb + 0.006)
    return core | pool


def erode_hydro(H, sea, W, strength, iters, runoff, on_step=None):
    GY, GX = H.shape
    idx = np.arange(GY * GX)
    K = 0.0013 * strength
    D = 0.12
    SILT = 0.0015
    Wf = W.ravel()
    fill = np.zeros(GY * GX)
    for step in range(iters):
        if on_step is not None:
            on_step(step)
        land = H >= sea
        landf = land.ravel()
        rec = receivers(H)
        A = accumulate(rec, H, runoff).ravel()
        Hf = H.ravel()
        rr = np.where(rec >= 0, rec, 0)
        below = np.where(rec >= 0, Hf[rr], sea)
        slope = np.clip(Hf - below, 0.0, None)
        dz = K * np.sqrt(A) * slope * Wf * landf
        Hn = np.where(landf, np.maximum(Hf - dz, sea + 1e-4), Hf)
        pit = (rec == idx) & landf & (A > 0)
        dep = np.where(pit, np.minimum(0.02, SILT * np.sqrt(A)), 0.0)
        Hn = Hn + dep
        fill += dep
        Hn = Hn.reshape(GY, GX)
        P = np.pad(Hn, 1, mode="edge")
        lap = P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] - 4.0 * Hn
        H = Hn + D * lap * land
    return H, fill.reshape(GY, GX)


def settle(H, sea, land_passes=14, sea_passes=120):
    for k in range(max(land_passes, sea_passes)):
        land = H >= sea
        P = np.pad(H, 1, mode="edge")
        lap = P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] - 4.0 * H
        if k < land_passes:
            H = np.where(land, np.maximum(H + 0.2 * lap, sea + 1e-4), H)
        if k < sea_passes:
            H = np.where(~land, np.minimum(H + 0.25 * lap, sea - 1e-4), H)
    return H


def hist_match(H, target):
    out = np.empty(H.size)
    out[np.argsort(H.ravel(), kind="stable")] = np.sort(target.ravel())
    return out.reshape(H.shape)


def rank(X, land):
    vals = np.sort(X[land]) if land.any() else np.sort(X.ravel())
    return np.clip(np.searchsorted(vals, X) / vals.size, 0.0, 1.0)


def diffuse(a, n):
    for _ in range(n):
        P = np.pad(a, 1, mode="edge")
        a = (P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] + 4.0 * a) / 8.0
    return a


# ---------- weather ----------
def wind_transport(E, land, lat, warmth):
    GY, GX = E.shape
    decay = 0.012 + 0.07 * np.exp(-((lat - 0.33) / 0.09) ** 2)
    row_lat = lat[:, 0]
    westerly = (row_lat >= 0.33) & (row_lat < 0.66)
    rain_by, carried_by = {}, {}
    for direction in (1, -1):
        m = np.zeros(GY)
        prev = None
        rain_out = np.zeros((GY, GX))
        carried_out = np.zeros((GY, GX))
        cols = range(GX) if direction == 1 else range(GX - 1, -1, -1)
        for i in cols:
            e = E[:, i]
            rise = np.zeros(GY) if prev is None else np.clip(e - prev, 0.0, None)
            m = np.where(land[:, i], m, warmth[:, i])
            rain = m * (1.0 - np.exp(-(4.0 * rise + 0.015)))
            m = m - rain
            rain_out[:, i] = rain
            carried_out[:, i] = m
            m = m * (1.0 - decay[:, i])
            prev = e
        rain_by[direction] = rain_out
        carried_by[direction] = carried_out
    pick = westerly[:, None]
    return (np.where(pick, rain_by[1], rain_by[-1]), np.where(pick, carried_by[1], carried_by[-1]))


def stage_weather(E, sea, S, dom, FX, FY):
    """Temperature and moisture from geography alone, plus the runoff that feeds the erosion."""
    land = E >= sea
    lat = np.abs(FY / dom[1] * 2 - 1)
    alt = np.clip(E - sea, 0.0, None)
    T0 = (1 - lat ** 1.25 * 0.85 - alt * 1.0 - 0.3 * smooth(0.78, 0.95, lat)
          + (fbm(FX * 3, FY * 3, S + 404, 3) - 0.5) * 0.12)
    warmth = np.clip(T0, 0.2, 1.0)
    rain, carried = wind_transport(E, land, lat, warmth)
    rain = diffuse(rain, 3)
    carried = diffuse(carried, 6)
    rain_peak = max(float(rain.max()), 1e-9)
    M0 = (0.6 * carried + 0.4 * rain / rain_peak
          + (fbm(FX * 3.5 + 13.7, FY * 3.5 + 7.1, S + 909, 4) - 0.5) * 0.12)
    evap = 0.2 + 0.6 * np.clip(T0, 0.0, 1.0)
    wet = np.where(land, rain * (1.0 - evap), 0.0)
    mean_wet = float(wet[land].mean()) if land.any() else 1.0
    runoff = np.where(land, 0.7 * wet / max(mean_wet, 1e-9) + 0.3, 0.0)
    return {"temp": rank(T0, land), "moist": rank(M0, land), "runoff": runoff}


# ---------- biomes ----------
def classify_biome(e, h, t, m, sl, sea):
    alt = h - sea
    geo_conds = [
        h < sea - 0.055,
        h < sea,
        (h < sea + 0.03) & (sl < 0.5),
        (h < sea + 0.02) & (sl >= 0.97),
        (e > 0.86) & (sl > 0.06),
        (sl > 0.6) & (alt > 0.12),
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
    choices = [np.broadcast_to(np.asarray(c), h.shape) for c in geo_choices + weather_choices]
    return np.select(geo_conds + weather_conds, choices, default=CLAY)


# ---------- world generation ----------
BEACH_TILES = 5.0    # width of the beach, in tiles
SHELF_TILES = 10.0   # width of the shallow sea floor beside the coast, in tiles
NON_VIABLE = (SILT, SEABED_SAND, BEACH_SAND, ICE, BEDROCK, SCREE, CLIFF_ROCK)
FLORA_NAMES = ["None", "Grass", "Shrubs", "Forest"]
FLORA_RGB = np.array([(0, 0, 0), (104, 150, 70), (92, 124, 56), (46, 102, 58)], np.float64)


def edge_distance(src):
    """Steps from each cell to the nearest cell where src is False. The map's border counts as open."""
    d = np.where(src, np.inf, 0.0)
    while True:
        p = np.pad(d, 1, constant_values=0.0)
        nb = np.minimum(np.minimum(p[:-2, 1:-1], p[2:, 1:-1]),
                        np.minimum(p[1:-1, :-2], p[1:-1, 2:])) + 1.0
        new = np.minimum(d, nb)
        if np.array_equal(new, d):
            break
        d = new
    d[np.isinf(d)] = float(max(d.shape))
    return d


def soft_range(x, lo, hi, edge=0.12):
    up = np.clip((x - (lo - edge)) / edge, 0.0, 1.0)
    down = np.clip(((hi + edge) - x) / edge, 0.0, 1.0)
    return np.minimum(up, down)


def vegetation(h, biome, temp, moist, slope, sea, S, dom, cover):
    """Plants on the land that can carry them: grass, shrubs and forest, each following its own
    patchy noise and the climate. Returns the plant kind per tile (0 for none) and its density 0..255."""
    if cover <= 0.0:
        z = np.zeros(h.shape, np.uint8)
        return z, z.copy()
    viable = ((h >= sea) & ~np.isin(biome, NON_VIABLE)).astype(np.float32)
    thr = 1.0 - cover
    H, W = h.shape
    # the plant patches are broad, so they are worked out on a grid of at most 512 points
    k = min(1.0, 512.0 / max(H, W))
    cw, ch = max(8, round(W * k)), max(8, round(H * k))
    FXc, FYc = grid(cw, ch, dom)
    up = lambda G: sample_full(G, W, H, dom)

    def patches(seed, scale):
        n = np.clip((fbm(FXc * scale, FYc * scale, seed, 4) - 0.2) / 0.6, 0.0, 1.0)
        return up(np.clip((n - thr) / 0.08, 0.0, 1.0))

    layers = (
        patches(S + 3300, 40.0) * soft_range(temp, 0.22, 0.84) * soft_range(moist, 0.28, 1.0),
        patches(S + 3400, 55.0) * soft_range(temp, 0.20, 0.80) * soft_range(moist, 0.22, 0.82),
        patches(S + 3500, 28.0) * soft_range(temp, 0.30, 0.76) * soft_range(moist, 0.46, 1.0)
        * (slope < 0.5),
    )
    best = np.zeros(h.shape, np.float32)
    kind = np.zeros(h.shape, np.uint8)
    for i, d in enumerate(layers):
        d = d * viable
        m = d > best                       # the densest plant wins each tile
        best[m] = d[m]
        kind[m] = i + 1
    kind[best <= 0] = 0
    return kind, np.round(best * 255).astype(np.uint8)


def build_world(s: Settings, report=None) -> World:
    """Build the whole world in one pass: shape, weather, erosion and lakes on a coarse grid,
    then the full tiles sampled from it, with the shore, ground, and plant cover.
    report(fraction, text), if given, is told how far along the build is."""
    def tell(frac, text):
        if report is not None:
            report(frac, text)

    tell(0.0, "Shaping the land")
    S = int(s.seed)
    dom = domain(s)
    sea = float(s.water)
    wx, wy = work_shape(s)
    FX, FY = grid(wx, wy, dom)
    raw = base_noise(s.method, S, FX * s.scale, FY * s.scale)
    norm = quantile_map(raw)
    E0 = norm(raw)

    ridge = ridged(FX * 5.0, FY * 5.0, S + 1904, 5)
    inland = smooth(sea + 0.02, sea + 0.30, E0)
    peak = np.maximum(ridge - 0.46, 0.0) / 0.54
    lifted = soft_unit(E0 + 0.25 * clamp(s.relief, 0.25, 2.0) * inland * peak)
    E = np.where(E0 >= sea, np.maximum(lifted, sea + 1e-4), E0)

    tell(0.12, "Working out the weather")
    weather = stage_weather(E, sea, S, dom, FX, FY)
    W = 0.5 + 0.5 * smooth(sea, sea + 0.2, E)
    tell(0.18, "Eroding valleys")
    Eg, fill = erode_hydro(E.copy(), sea, W, float(s.erosion), EROSION_ITERS, weather["runoff"],
                           on_step=lambda i: tell(0.18 + 0.5 * i / EROSION_ITERS, "Eroding valleys"))
    tell(0.68, "Settling lakes")
    Eg = hist_match(settle(Eg, sea), E)
    land_c = Eg >= sea
    rec = receivers(Eg)
    A = accumulate(rec, Eg, weather["runoff"])
    lake_c = lake_mask(Eg, rec, A, land_c, float(s.lakes), fill)
    elev_c = rank(Eg, land_c)
    # distance to the coast in tiles: positive inland, negative out to sea
    tiles_per_cell = max(s.width, s.height) / float(max(wx, wy))
    coast_c = np.where(land_c, edge_distance(land_c), -edge_distance(~land_c)) * tiles_per_cell

    tell(0.76, "Sampling the full map")
    co = lambda G: sample_full(G, s.width, s.height, dom)
    # fine detail: a cheap noise at a finer scale, kept only where the sketch misses it
    table = np.random.default_rng(S + 77).random((256, 256)).astype(np.float32)

    def fine_at(xs, ys):
        kx = xs * s.scale; ky = ys * s.scale
        return (0.6 * lattice_map(table, kx * 4.0, ky * 4.0)
                + 0.4 * lattice_map(table, kx * 8.0 + 3.1, ky * 8.0 + 1.7))

    axis = lambda n, a: (np.arange(n) + 0.5) / n * a
    fine_c = fine_at(axis(wx, dom[0]), axis(wy, dom[1]))
    nf = quantile_map(fine_c)
    h = np.clip(co(Eg) + RESIDUAL * 2 * s.detail * (nf(fine_at(axis(s.width, dom[0]), axis(s.height, dom[1])))
                                                   - co(nf(fine_c))), 0.0, 1.0)
    coast = co(coast_c)

    tell(0.84, "Shaping the shore")
    land = h >= sea
    beach = smooth(0.0, BEACH_TILES, coast)     # 0 at the waterline, 1 inland
    shelf = smooth(0.0, SHELF_TILES, -coast)    # 0 at the waterline, 1 out at sea
    h = np.where(land, sea + (h - sea) * (0.25 + 0.75 * beach),
                 sea - (sea - h) * (0.2 + 0.8 * shelf))
    if s.lowlands > 0:
        # push land toward the coast: a power curve sinks the middle heights and keeps the peaks
        L = clamp(s.lowlands, 0.0, 1.0)
        top = np.maximum(1.0 - sea, 1e-6)
        t = np.clip((h - sea) / top, 0.0, 1.0)
        curved = sea + top * t ** (1.0 + 2.5 * L)
        h = np.where(land, (1.0 - L) * h + L * curved, h)

    tell(0.90, "Classifying the ground")
    land = h >= sea
    gx = np.gradient(h, axis=1); gz = np.gradient(h, axis=0)
    slope = np.clip(np.hypot(gx, gz) * 68.0, 0.0, 1.0)
    temp = co(weather["temp"]); moist = co(weather["moist"])
    biome = classify_biome(co(elev_c), h, temp, moist, slope, sea).astype(np.uint8)
    lake = (co(lake_c.astype(np.float64)) > 0.5) & land
    biome[lake] = FLOODPLAIN
    biome[land & (coast < BEACH_TILES) & (slope < 0.5)] = BEACH_SAND
    biome[~land & (coast > -SHELF_TILES)] = SEABED_SAND

    tell(0.96, "Growing plants")
    fk, fa = vegetation(h, biome, temp, moist, slope, sea, S, dom, float(s.vegetation))
    tell(1.0, "Done")
    return World(h.astype(np.float32), biome, fk, fa,
                 temp.astype(np.float32), moist.astype(np.float32), sea)


def preview_settings(s: Settings) -> Settings:
    """The same world at a small size, for the live preview."""
    f = 192.0 / max(s.width, s.height)
    return Settings(**{**s.to_dict(), "width": max(16, round(s.width * f)),
                       "height": max(16, round(s.height * f))})


# ---------- shading and colours ----------
def shading(h):
    vs = max(h.shape) * VS_FRAC
    gx = np.gradient(h.astype(np.float64), axis=1) * vs
    gz = np.gradient(h.astype(np.float64), axis=0) * vs
    n = np.stack([-gx, np.ones_like(gx), -gz], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return 0.4 + 0.6 * np.clip(n @ LIGHT, 0, 1)


def _lerp(a, b, t):
    a = np.asarray(a, np.float64); b = np.asarray(b, np.float64)
    return a + (b - a) * t


def smooth_colour(rgb, passes=1):
    """Blend each colour with its neighbours, so one ground type turns into the next as a gradient."""
    for _ in range(passes):
        P = np.pad(rgb, ((1, 1), (1, 1), (0, 0)), mode="edge")
        rgb = (P[:-2, 1:-1] + P[2:, 1:-1] + P[1:-1, :-2] + P[1:-1, 2:] + 4.0 * P[1:-1, 1:-1]) / 8.0
    return rgb


def ground_colours(b, fk, fa, passes=1):
    """Ground colour with plant cover laid over it, blended into the neighbouring tiles."""
    a = (fa.astype(np.float64) / 255.0 * 0.85)[..., None]
    rgb = BIOME_RGB[b] * (1.0 - a) + FLORA_RGB[fk] * a
    return smooth_colour(rgb, passes)


def render_map(h, b, fk, fa, t, m, sea, mode):
    """Colour a map for the editor and the preview. mode: biome, height, temperature, moisture."""
    h64 = h.astype(np.float64)
    if mode == "height":
        land = np.clip((h64 - sea) / max(1e-6, 1 - sea), 0, 1)[..., None]
        deep = np.clip((sea - h64) / max(1e-6, sea), 0, 1)[..., None]
        out = np.where((h64 < sea)[..., None], _lerp([120, 170, 210], [12, 30, 60], deep),
                       _lerp([70, 140, 70], [230, 240, 220], land))
    elif mode == "temperature":
        tt = np.clip(t, 0, 1)[..., None].astype(np.float64)
        out = np.where(tt < 0.5, _lerp([40, 90, 200], [240, 240, 240], tt * 2),
                       _lerp([240, 240, 240], [215, 50, 40], (tt - 0.5) * 2))
    elif mode == "moisture":
        mm = np.clip(m, 0, 1)[..., None].astype(np.float64)
        out = _lerp([205, 180, 120], [30, 90, 160], mm)
    else:
        out = ground_colours(b, fk, fa) * shading(h64)[..., None]
        water = np.array([30, 90, 140.0]) * (0.6 + 0.4 * np.clip(h64 / max(sea, 1e-6), 0, 1))[..., None]
        out = np.where((h64 < sea)[..., None], water, out)
    return np.clip(out, 0, 255).astype(np.uint8)


# ---------- editor ----------
EDIT_OPS = [("raise", "Raise"), ("lower", "Lower"), ("smooth", "Smooth"), ("erode", "Erode"),
            ("warm", "Warmer"), ("cool", "Cooler"), ("wet", "Wetter"), ("dry", "Drier"),
            ("paint", "Paint ground")]
SHAPES = [("circle", "Circle"), ("square", "Square"), ("diamond", "Diamond"), ("soft", "Soft")]
VIEW_MODES = [("biome", "Biome"), ("height", "Height"), ("temperature", "Temperature"),
              ("moisture", "Moisture")]


def falloff(dx, dy, r, shape):
    if shape == "square":
        return ((np.abs(dx) <= r) & (np.abs(dy) <= r)).astype(np.float64)
    if shape == "diamond":
        return ((np.abs(dx) + np.abs(dy)) <= r).astype(np.float64)
    if shape == "soft":
        return np.clip(1.0 - (dx * dx + dy * dy) / (r * r), 0.0, 1.0)
    return ((dx * dx + dy * dy) <= r * r).astype(np.float64)


def mean8(a):
    P = np.pad(a, 1, mode="edge")
    return (P[:-2, :-2] + P[:-2, 1:-1] + P[:-2, 2:] + P[1:-1, :-2]
            + P[1:-1, 2:] + P[2:, :-2] + P[2:, 1:-1] + P[2:, 2:]) / 8.0


class Editor:
    """The world as it is being edited. Brush strokes change only the tiles they touch, and
    undo keeps whole snapshots, a few at most."""

    def __init__(self, world: World):
        self.sea = float(world.sea)
        self._set((world.height.copy(), world.biome.copy(), world.flora_kind.copy(),
                   world.flora_amt.copy(), world.temp.copy(), world.moist.copy()))
        self.undo_stack, self.redo_stack = [], []
        self._stroke = None
        self._changed = False
        self.version = 0
        self.saved_version = 0

    @property
    def shape(self):
        return self.h.shape

    def state(self):
        return (self.h.copy(), self.b.copy(), self.fk.copy(), self.fa.copy(), self.t.copy(), self.m.copy())

    def _set(self, st):
        self.h, self.b, self.fk, self.fa, self.t, self.m = st

    def unsaved(self):
        return self.version != self.saved_version

    def begin_stroke(self):
        self._stroke = self.state()
        self._changed = False

    def end_stroke(self):
        if self._stroke is not None and self._changed:
            self.undo_stack.append(self._stroke)
            # keep undo within about 1.5 GB of snapshots, however big the world is
            per_state = max(1, sum(a.nbytes for a in self._stroke))
            del self.undo_stack[:-max(2, min(MAX_UNDO, int(1.5e9 // per_state)))]
            self.redo_stack.clear()
            self.version += 1
        self._stroke = None
        self._changed = False

    def undo(self):
        if not self.undo_stack:
            return False
        self.redo_stack.append(self.state())
        self._set(self.undo_stack.pop())
        self.version += 1
        return True

    def redo(self):
        if not self.redo_stack:
            return False
        self.undo_stack.append(self.state())
        self._set(self.redo_stack.pop())
        self.version += 1
        return True

    def dab(self, op, gx, gy, radius, shape, strength, biome=0):
        """One brush dab centred on tile (gx, gy). Returns True if anything changed."""
        H, W = self.h.shape
        r = max(0.5, float(radius))
        y0, y1 = max(0, int(gy - r - 2)), min(H, int(gy + r + 3))
        x0, x1 = max(0, int(gx - r - 2)), min(W, int(gx + r + 3))
        if y0 >= y1 or x0 >= x1:
            return False
        ry0, ry1 = max(0, y0 - 1), min(H, y1 + 1)
        rx0, rx1 = max(0, x0 - 1), min(W, x1 + 1)
        reg = np.s_[ry0:ry1, rx0:rx1]
        core = np.s_[y0 - ry0:y1 - ry0, x0 - rx0:x1 - rx0]
        yy, xx = np.mgrid[y0:y1, x0:x1]
        w = falloff(xx - gx, yy - gy, r, shape) * float(strength)
        if not w.any():
            return False
        if op == "paint":
            sel = w >= 0.5
            if not sel.any():
                return False
            self.b[y0:y1, x0:x1][sel] = np.uint8(biome)
            if biome in NON_VIABLE:
                self.fk[y0:y1, x0:x1][sel] = 0
                self.fa[y0:y1, x0:x1][sel] = 0
        elif op in ("warm", "cool", "wet", "dry"):
            arr = self.t if op in ("warm", "cool") else self.m
            sign = 1.0 if op in ("warm", "wet") else -1.0
            v = arr[y0:y1, x0:x1]
            v += sign * 0.03 * w
            np.clip(v, 0.0, 1.0, out=v)
        else:
            hv = self.h[reg]
            if op == "raise":
                hv[core] = np.clip(hv[core] + 0.02 * w, 0, 1)
            elif op == "lower":
                hv[core] = np.clip(hv[core] - 0.02 * w, 0, 1)
            elif op == "smooth":
                hv[core] = hv[core] + w * 0.35 * (mean8(hv)[core] - hv[core])
            elif op == "erode":
                diff = hv - mean8(hv)
                excess = np.sign(diff) * np.clip(np.abs(diff) - TALUS, 0, None)
                hv[core] = np.clip(hv[core] - 0.5 * w * excess[core], 0, 1)
            else:
                return False
            self._reclassify(reg, core, w > 0)
        self._changed = True
        return True

    def _reclassify(self, reg, core, sel):
        hv = self.h[reg]
        gx = np.gradient(hv, axis=1); gz = np.gradient(hv, axis=0)
        slope = np.clip(np.hypot(gx, gz) * 68.0, 0, 1)
        new = classify_biome(hv, hv, self.t[reg], self.m[reg], slope, self.sea)
        bv = self.b[reg]
        m = sel & ~np.isin(bv[core], HYDRO)
        sub = bv[core]
        sub[m] = new[core][m].astype(np.uint8)
        dead = m & np.isin(sub, NON_VIABLE)
        for arr in (self.fk, self.fa):
            region = arr[reg]
            cells = region[core]
            cells[dead] = 0
            region[core] = cells

    def render(self, mode):
        return render_map(self.h, self.b, self.fk, self.fa, self.t, self.m, self.sea, mode)

    def describe(self, gx, gy):
        i, j = int(gx), int(gy)
        H, W = self.h.shape
        if not (0 <= i < W and 0 <= j < H):
            return ""
        return (f"x {i}, y {j} · {BIOMES[int(self.b[j, i])][0]} · {FLORA_NAMES[int(self.fk[j, i])]} · "
                f"{self.h[j, i] - self.sea:+.3f} vs sea · temp {self.t[j, i]:.2f} · "
                f"moisture {self.m[j, i]:.2f}")


# ---------- world file ----------
def save_world(path, s: Settings, ed: Editor):
    meta = {"magic": MAGIC, "version": WORLD_VERSION, "settings": s.to_dict(), "sea": ed.sea}
    with open(path, "wb") as f:
        np.savez_compressed(f, meta=np.array(json.dumps(meta)), height=ed.h.astype(np.float32),
                            biome=ed.b, flora_kind=ed.fk, flora_amt=ed.fa,
                            temp=ed.t.astype(np.float32), moist=ed.m.astype(np.float32))


def load_world(path):
    try:
        d = np.load(path, allow_pickle=False)
        meta = json.loads(str(d["meta"]))
    except Exception as ex:
        raise ValueError("This is not a readable world file. Worlds saved by older versions cannot be "
                         "opened; generate the world again.") from ex
    if meta.get("magic") != MAGIC or meta.get("version") != WORLD_VERSION:
        raise ValueError("Unsupported world file.")
    s = Settings.from_dict(meta["settings"])
    world = World(d["height"].astype(np.float32), d["biome"].astype(np.uint8),
                  d["flora_kind"].astype(np.uint8), d["flora_amt"].astype(np.uint8),
                  d["temp"].astype(np.float32), d["moist"].astype(np.float32), float(meta["sea"]))
    return s, world


# ---------- Map Theme ----------
LOOK_FORMAT = "worldmaker-look"
LOOK_VERSION = 1


@dataclass
class Look:
    seed: int = 1
    clock: float = 12.0
    day_cycle: bool = True
    day_length: float = 60.0
    day_ratio_day: float = 3.0
    day_ratio_night: float = 1.0
    sun_azimuth: float = 135.0
    sun_elevation: float = 48.0
    sun_strength: float = 0.9
    sun_rgb: list = field(default_factory=lambda: [1.0, 0.96, 0.82])
    ambient: float = 0.35
    sky_top: list = field(default_factory=lambda: [0.22, 0.42, 0.80])
    sky_horizon: list = field(default_factory=lambda: [0.72, 0.84, 0.94])
    night_top: list = field(default_factory=lambda: [0.02, 0.03, 0.07])
    night_horizon: list = field(default_factory=lambda: [0.05, 0.07, 0.13])
    fog: float = 0.10
    rock_rgb: list = field(default_factory=lambda: [0.46, 0.43, 0.39])
    rock_slope: float = 0.45
    surface_detail: float = 0.7
    water_rgb: list = field(default_factory=lambda: [0.12, 0.32, 0.50])
    water_alpha: float = 0.75
    water_density: float = 0.55
    water_wave: float = 0.6
    water_speed: float = 1.0
    cloud_rgb: list = field(default_factory=lambda: [1.0, 1.0, 1.0])
    cloud_cover: float = 0.55
    cloud_opacity: float = 0.9
    cloud_height: float = 2.2
    cloud_speed: float = 1.0
    cloud_direction: float = 35.0
    view_distance: float = 0.0
    player_scale: float = 1.0


# (kind, attribute, label, lo, hi, decimals) for sliders; the other kinds carry no range.
THEME_ITEMS = [
    ("heading", "Viewer"),
    ("slider", "view_distance", "View distance in tiles (0 = unlimited)", 0, 4000, 0),
    ("slider", "player_scale", "Player size (smaller makes the world feel bigger)", 0.1, 1.0, 2),
    ("heading", "Time"),
    ("time", "clock", "Time of day"),
    ("check", "day_cycle", "Run a day and night cycle"),
    ("slider", "day_length", "Seconds per day and night", 10, 1200, 0),
    ("ratio", "day_ratio_day", "Day : night", "day_ratio_night"),
    ("heading", "Sun and light"),
    ("slider", "sun_azimuth", "Direction at noon (°)", 0, 360, 0),
    ("slider", "sun_elevation", "Height at noon (°)", 5, 85, 0),
    ("slider", "sun_strength", "Strength", 0, 1.5, 2),
    ("colour", "sun_rgb", "Sun colour"),
    ("slider", "ambient", "Shadow light", 0, 1, 2),
    ("heading", "Sky and fog"),
    ("colour", "sky_top", "Day sky, overhead"),
    ("colour", "sky_horizon", "Day sky, horizon"),
    ("colour", "night_top", "Night sky, overhead"),
    ("colour", "night_horizon", "Night sky, horizon"),
    ("slider", "fog", "Fog density", 0.1, 4, 2),
    ("heading", "Ground and water"),
    ("colour", "rock_rgb", "Rock colour"),
    ("slider", "rock_slope", "Rock on steep slopes", 0.05, 0.95, 2),
    ("slider", "surface_detail", "Surface detail (grain on the ground)", 0.0, 1.0, 2),
    ("colour", "water_rgb", "Water colour"),
    ("slider", "water_alpha", "Water opacity", 0, 1, 2),
    ("slider", "water_density", "Surface pattern density", 0, 1, 2),
    ("slider", "water_wave", "Wave strength", 0, 1, 2),
    ("slider", "water_speed", "Surface motion", 0, 3, 2),
    ("heading", "Clouds"),
    ("colour", "cloud_rgb", "Cloud colour"),
    ("slider", "cloud_cover", "Cover", 0, 1, 2),
    ("slider", "cloud_opacity", "Opacity", 0, 1, 2),
    ("slider", "cloud_height", "Height", 1.2, 3.8, 2),
    ("slider", "cloud_speed", "Speed", 0, 3, 2),
    ("slider", "cloud_direction", "Direction (degrees)", 0, 360, 0),
]


def replace_look(dst: Look, src: Look):
    for f in fields(Look):
        setattr(dst, f.name, copy.deepcopy(getattr(src, f.name)))


def randomize_look(look: Look, rng=random):
    base = Look()
    look.seed = rng.randint(0, 99999)
    for item in THEME_ITEMS:
        if item[0] == "slider" and item[1] not in ("view_distance", "player_scale"):
            setattr(look, item[1], rng.uniform(item[3], item[4]))
        elif item[0] == "colour":
            setattr(look, item[1], [clamp(c + rng.uniform(-0.1, 0.1), 0.0, 1.0)
                                    for c in getattr(base, item[1])])


def look_to_dict(look):
    return {"format": LOOK_FORMAT, "version": LOOK_VERSION, "look": asdict(look)}


def look_from_dict(data):
    if not isinstance(data, dict) or data.get("format") != LOOK_FORMAT:
        raise ValueError("This is not a Worldmaker Map Theme file.")
    raw = data.get("look", {})
    look = Look()
    for f in fields(Look):
        if f.name not in raw:
            continue
        v, cur = raw[f.name], getattr(look, f.name)
        try:
            if isinstance(cur, list):
                vals = [clamp(float(x), 0.0, 1.0) for x in v]
                if len(vals) == 3:
                    setattr(look, f.name, vals)
            elif isinstance(cur, bool):
                setattr(look, f.name, bool(v))
            elif f.name == "clock":
                look.clock = clamp(float(v), 0.0, 24.0)
            elif f.name == "seed":
                look.seed = int(v)
            else:
                rng = next((it for it in THEME_ITEMS if it[0] == "slider" and it[1] == f.name), None)
                x = float(v)
                setattr(look, f.name, clamp(x, rng[3], rng[4]) if rng else x)
        except (TypeError, ValueError, IndexError):
            pass
    return look


def sky_state(look: Look):
    """Where the sun is and what colour the sky and light are at the current time."""
    a = math.radians(look.sun_azimuth + (look.clock - 12.0) * 15.0)
    e = math.radians(look.sun_elevation * math.sin(math.pi * (look.clock - 6.0) / 12.0))
    sun = np.array([math.cos(e) * math.sin(a), math.sin(e), -math.cos(e) * math.cos(a)])
    t = clamp((sun[1] + 0.08) / 0.18, 0.0, 1.0)
    day = t * t * (3.0 - 2.0 * t)
    top = np.array(look.night_top) * (1 - day) + np.array(look.sky_top) * day
    hor = np.array(look.night_horizon) * (1 - day) + np.array(look.sky_horizon) * day
    return {
        "sun": sun, "day": day, "top": top, "horizon": hor,
        "sun_k": look.sun_strength * day,
        "amb": look.ambient * (0.35 + 0.65 * day),
    }

# ===== 3D view =====



import math
import time
from ctypes import c_void_p

import numpy as np
from OpenGL import GL as gl
from OpenGL.GLU import gluLookAt, gluPerspective
from PyQt6.QtCore import QPoint, QPointF, Qt, QTimer
from PyQt6.QtGui import QCursor, QFont
from PyQt6.QtOpenGLWidgets import QOpenGLWidget
from PyQt6.QtWidgets import QLabel


def day_fraction(look):
    d = max(0.01, float(look.day_ratio_day))
    n = max(0.01, float(look.day_ratio_night))
    return d / (d + n)


def clock_from_phase(p, f):
    """Cycle position 0..1 to clock hours. The day part runs from sunrise (6) to sunset (18)."""
    if p < f:
        return 6.0 + 12.0 * p / f
    return (18.0 + 12.0 * (p - f) / (1.0 - f)) % 24.0


def phase_from_clock(c, f):
    c = c % 24.0
    if 6.0 <= c < 18.0:
        return f * (c - 6.0) / 12.0
    return f + (1.0 - f) * ((c - 18.0) % 24.0) / 12.0


EYE = 1.7
GRAVITY = 24.0
JUMP_V = 9.0
WALK_SPEED = 8.0
FLY_SPEED = 60.0
SENS = 0.0022
DOUBLE_TAP = 0.35   # seconds between two Space presses that count as a double tap

K = {n: int(getattr(Qt.Key, "Key_" + n)) for n in ("W", "A", "S", "D", "E", "Q", "C", "Space", "Shift", "F", "Escape")}

TERRAIN_VS = """
#version 120
attribute vec3 aPos; attribute vec3 aCol; attribute vec3 aNrm;
varying vec3 vPos; varying vec3 vCol; varying vec3 vNrm;
void main() {
    vPos = aPos; vCol = aCol; vNrm = aNrm;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);
}
"""

TERRAIN_FS = """
#version 120
uniform vec3 uSun; uniform float uSunK; uniform float uAmb; uniform vec3 uSunRGB;
uniform vec3 uFog; uniform float uFogDen; uniform vec3 uCam;
uniform vec3 uRock; uniform float uRockSlope; uniform float uDetail;
uniform float uShadow; uniform float uCloudY;
varying vec3 vPos; varying vec3 vCol; varying vec3 vNrm;
//CLOUD_LIB

// hash that stays accurate at large world coordinates
float hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i), b = hash2(i + vec2(1.0, 0.0));
    float c = hash2(i + vec2(0.0, 1.0)), d = hash2(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// grain at three sizes, from about one speck per tile down to one per fifth of a tile
float grain(vec2 p) {
    return 0.5 * vnoise(p * 3.1) + 0.3 * vnoise(p * 7.3 + 4.7) + 0.2 * vnoise(p * 15.9 + 9.1);
}
void main() {
    vec3 N = normalize(vNrm);
    float steep = 1.0 - N.y;
    float rock = smoothstep(uRockSlope - 0.08, uRockSlope + 0.08, steep);
    vec3 alb = mix(vCol, uRock, rock);
    float d = length(vPos - uCam);
    // the detail fades out with distance, so far ground stays calm
    float fade = uDetail * (1.0 - smoothstep(40.0, 220.0, d));
    vec2 p = vPos.xz;
    float g = grain(p);
    float tile = hash2(floor(p)) - 0.5;          // a slight tint per tile, crisp edges
    alb *= 1.0 + fade * (0.22 * (g - 0.5) + 0.06 * tile);
    float e = 0.2;
    float gx = grain(p + vec2(e, 0.0)) - g;
    float gz = grain(p + vec2(0.0, e)) - g;
    N = normalize(N + fade * vec3(-gx, 0.0, -gz) * 2.0);
    // the cloud directly above each point blocks its direct sunlight
    float shade = 1.0;
    if (uShadow > 0.0) {
        shade = 1.0 - uShadow * cloudDensity(vPos.xz, uTime, true);
    }
    float diff = max(dot(N, uSun), 0.0) * uSunK * shade;
    vec3 lit = alb * (uAmb + diff * uSunRGB);
    float f = clamp(1.0 - exp(-pow(d * uFogDen, 2.0)), 0.0, 1.0);
    gl_FragColor = vec4(mix(lit, uFog, f), 1.0);
}
"""

CLOUD_LIB = """
uniform float uTime; uniform float uCover; uniform vec2 uWind; uniform float uSeed;
uniform float uScale;
float cHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float cNoise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(cHash(i), cHash(i + vec2(1.0, 0.0)), u.x),
               mix(cHash(i + vec2(0.0, 1.0)), cHash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float cFbm(vec2 p, int octaves) {
    float s = 0.0; float a = 0.5; float n = 0.0;
    for (int i = 0; i < 5; i++) {
        if (i < octaves) { s += a * cNoise(p); n += a; }
        p = p * 2.03 + vec2(17.1, 9.3); a *= 0.5;
    }
    return s / n;
}
// Cloud density, 0..1, at a point on the cloud layer (x, z in tiles). The shape is
// domain-warped so it billows, it drifts with the wind, and the cover swells and thins over time.
// detailed=false gives a cheaper version for ground shadows.
float cloudDensity(vec2 xz, float t, bool detailed) {
    vec2 p = xz * uScale + uWind * t + vec2(uSeed * 7.7, uSeed * 3.1);
    if (detailed) {
        vec2 w = vec2(cFbm(p * 0.5 + 1.7, 3), cFbm(p * 0.5 + 9.2, 3));
        p += 0.9 * (w - 0.5);
    }
    float base = cFbm(p, detailed ? 5 : 3);
    float drift = cFbm(xz * uScale * 0.12 + vec2(t * 0.02, -t * 0.013) + uSeed, 3);
    float cover = clamp(uCover * (0.55 + 0.9 * drift), 0.0, 1.0);
    float thr = mix(0.72, 0.30, cover);
    return smoothstep(thr, thr + 0.2, base);
}
"""

CLOUD_FS = """
#version 120
uniform float uOpacity; uniform vec3 uLit; uniform vec3 uCloudRGB;
uniform vec3 uFog; uniform float uFogDen; uniform vec3 uCam;
varying vec3 vPos;
//CLOUD_LIB
void main() {
    float d = cloudDensity(vPos.xz, uTime, true);
    float a = d * uOpacity;
    if (a < 0.01) discard;
    // thicker parts a little brighter, thin edges a little darker
    vec3 col = uCloudRGB * uLit * mix(0.92, 1.04, d);
    float dist = length(vPos - uCam);
    float f = clamp(1.0 - exp(-pow(dist * uFogDen, 2.0)), 0.0, 1.0);
    gl_FragColor = vec4(mix(col, uFog, f), a * (1.0 - 0.5 * f));
}
"""

TERRAIN_FS = TERRAIN_FS.replace("//CLOUD_LIB", CLOUD_LIB)
CLOUD_FS = CLOUD_FS.replace("//CLOUD_LIB", CLOUD_LIB)

WATER_FS = """
#version 120
uniform float uTime; uniform vec2 uWind; uniform float uSeed;
uniform vec3 uWaterRGB; uniform float uAlpha; uniform float uDensity; uniform float uWave;
uniform vec3 uSun; uniform float uSunK; uniform float uAmb; uniform vec3 uSunRGB;
uniform vec3 uFog; uniform float uFogDen; uniform vec3 uCam;
uniform vec2 uMap; uniform float uDepthMax; uniform float uHasDepth;
uniform sampler2D uDepth;
varying vec3 vPos;
float wHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float wNoise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(wHash(i), wHash(i + vec2(1.0, 0.0)), u.x),
               mix(wHash(i + vec2(0.0, 1.0)), wHash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float wFbm(vec2 p) {
    float s = 0.0; float a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * wNoise(p); p = p * 2.02 + vec2(11.3, 7.1); a *= 0.5; }
    return s / 0.9375;
}
// The surface pattern: a domain-warped field, seeded per world like the clouds, drifting with the wind.
float wPattern(vec2 xz, float t) {
    vec2 p = xz * 0.09 + uWind * t + vec2(uSeed * 5.3, uSeed * 2.9);
    vec2 w = vec2(wFbm(p * 0.7 + 3.1 + 0.2 * t), wFbm(p * 0.7 + 8.4 - 0.15 * t));
    return wFbm(p + 1.2 * (w - 0.5));
}
void main() {
    float t = uTime;
    float c = wPattern(vPos.xz, t);
    float e = 0.35;
    float gx = wPattern(vPos.xz + vec2(e, 0.0), t) - c;
    float gz = wPattern(vPos.xz + vec2(0.0, e), t) - c;
    vec3 N = normalize(vec3(-gx * uWave * 14.0, 1.0, -gz * uWave * 14.0));

    // depth: 0 at the shore, 1 in deep water. Beyond the map the sea is deep.
    float depth = 1.0;
    if (uHasDepth > 0.5) {
        vec2 uv = vPos.xz / uMap;
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) depth = texture2D(uDepth, uv).r;
    }
    float depthF = smoothstep(0.0, 0.45, depth);

    // density: patches of deeper and lighter water, like the cloud cover
    float thr = mix(0.72, 0.36, uDensity);
    float d = smoothstep(thr, thr + 0.2, c);

    vec3 shallowC = mix(uWaterRGB, vec3(0.42, 0.84, 0.80), 0.45);
    vec3 col = mix(shallowC, uWaterRGB * 0.55, depthF);
    col *= 0.9 + 0.2 * d;

    // foam where the water meets the land, broken up by the pattern
    float foamMask = (1.0 - smoothstep(0.02, 0.10, depth))
                     * smoothstep(0.42, 0.8, c + 0.25 * wNoise(vPos.xz * 0.8 + t * 0.3));
    col = mix(col, vec3(0.95), clamp(foamMask, 0.0, 1.0) * 0.85);

    // light: sky reflection by the angle we look at the water, plus sun glints
    vec3 V = normalize(uCam - vPos);
    vec3 lit = col * (uAmb * 0.8 + uSunK * uSunRGB * 0.5);
    float F = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    lit = mix(lit, uFog, clamp(F * 0.7, 0.0, 1.0));
    vec3 H = normalize(uSun + V);
    lit += pow(max(dot(N, H), 0.0), 220.0) * uSunK * uSunRGB * 1.2;

    // transparency: clearer in the shallows and in the thin patches of the pattern
    float a = uAlpha * mix(0.45, 1.0, depthF) * (0.85 + 0.15 * d);
    a = mix(a, 0.95, clamp(foamMask, 0.0, 1.0));

    float dist = length(vPos - uCam);
    float f = clamp(1.0 - exp(-pow(dist * uFogDen, 2.0)), 0.0, 1.0);
    gl_FragColor = vec4(mix(lit, uFog, f), clamp(a * (1.0 - 0.5 * f), 0.0, 1.0));
}
"""

SUN_FS = """
#version 120
uniform float uTime; uniform float uCover; uniform vec2 uWind; uniform float uSeed;
uniform float uScale;
uniform vec3 uSunRGB; uniform float uAlpha; uniform vec2 uSunXZ; uniform float uOcc;
varying vec3 vPos;
//CLOUD_LIB
void main() {
    // where the line of sight to the sun crosses the cloud layer
    float d = uOcc > 0.5 ? cloudDensity(uSunXZ, uTime, true) : 0.0;
    float k = 1.0 - 0.9 * d;
    gl_FragColor = vec4(uSunRGB * k, uAlpha);
}
"""
SUN_FS = SUN_FS.replace("//CLOUD_LIB", CLOUD_LIB)

CLOUD_VS = """
#version 120
varying vec3 vPos;
void main() {
    vPos = gl_Vertex.xyz;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
"""


def _text(b):
    return b.decode(errors="replace") if isinstance(b, bytes) else str(b)


def link(vs_src, fs_src, attribs):
    prog = gl.glCreateProgram()
    for kind, src in ((gl.GL_VERTEX_SHADER, vs_src), (gl.GL_FRAGMENT_SHADER, fs_src)):
        sh = gl.glCreateShader(kind)
        gl.glShaderSource(sh, src)
        gl.glCompileShader(sh)
        if not gl.glGetShaderiv(sh, gl.GL_COMPILE_STATUS):
            raise RuntimeError(_text(gl.glGetShaderInfoLog(sh)))
        gl.glAttachShader(prog, sh)
    for i, name in enumerate(attribs):
        gl.glBindAttribLocation(prog, i, name)
    gl.glLinkProgram(prog)
    if not gl.glGetProgramiv(prog, gl.GL_LINK_STATUS):
        raise RuntimeError(_text(gl.glGetProgramInfoLog(prog)))
    return prog


LOD_STEPS = (1, 2, 4, 8)          # tiles between vertices at each level of detail, nearest first
LOD_RANGES = (80.0, 200.0, 450.0)  # tiles from the viewer where each step coarser begins


def block_size(W, H):
    """Tiles per block side: about 32 blocks across the longer side, at least 32 tiles wide.
    Smaller blocks let the viewer skip more of the world that is out of sight."""
    return max(32, 1 << max(0, int(math.ceil(math.log2(max(W, H) / 32.0)))))


def _quads(y0, y1, x0, x1, W, step):
    rows = np.unique(np.r_[np.arange(y0, y1, step), y1])
    cols = np.unique(np.r_[np.arange(x0, x1, step), x1])
    G = rows[:, None] * W + cols[None, :]
    a = G[:-1, :-1].ravel(); b = G[:-1, 1:].ravel()
    c = G[1:, :-1].ravel(); d = G[1:, 1:].ravel()
    return np.concatenate([np.stack([a, c, b], 1).ravel(), np.stack([b, c, d], 1).ravel()])


def build_lod_index(W, H):
    """One index buffer holding every block at every level of detail, block by block.
    Returns the buffer, each block's centre and bounds, and where each block's piece at each
    level starts and how long it is."""
    B = block_size(W, H)
    pieces, centres, bounds = [], [], []
    for y0 in range(0, H - 1, B):
        for x0 in range(0, W - 1, B):
            y1 = min(H - 1, y0 + B)
            x1 = min(W - 1, x0 + B)
            centres.append(((x0 + x1) / 2.0, (y0 + y1) / 2.0))
            bounds.append((x0, x1, y0, y1))
            pieces.append([_quads(y0, y1, x0, x1, W, st) for st in LOD_STEPS])
    nb, nl = len(centres), len(LOD_STEPS)
    starts = np.zeros((nb, nl), np.int64)
    counts = np.zeros((nb, nl), np.int64)
    flat, off = [], 0
    for L in range(nl):
        for k in range(nb):
            p = pieces[k][L]
            starts[k, L] = off; counts[k, L] = p.size; off += p.size
            flat.append(p)
    idx = np.concatenate(flat).astype(np.uint32)
    return idx, np.array(centres), np.array(bounds), starts, counts


def block_height_range(h, bounds):
    """Lowest and highest height inside each block, so the block's box can be tested against the view."""
    lo = np.array([h[y0:y1 + 1, x0:x1 + 1].min() for x0, x1, y0, y1 in bounds], np.float64)
    hi = np.array([h[y0:y1 + 1, x0:x1 + 1].max() for x0, x1, y0, y1 in bounds], np.float64)
    return lo, hi


def cull_blocks(eye, target, aspect, fovy, far, centres, lo, hi, half, vs, edge_d, vd):
    """Which blocks can be seen: inside the camera's view cone, nearer than the far plane, and
    within the view distance. Each block is a sphere around its box, so the test is conservative:
    it may keep a block that is just out of view, never drop one that is in view."""
    fwd = target - eye
    fwd = fwd / np.linalg.norm(fwd)
    right = np.cross(fwd, np.array([0.0, 1.0, 0.0]))
    right = right / np.linalg.norm(right)
    up = np.cross(right, fwd)
    tv = math.tan(math.radians(fovy) / 2.0)
    th = tv * aspect
    cy = (lo + hi) * 0.5 * vs
    ry = (hi - lo) * 0.5 * vs
    C = np.stack([centres[:, 0], cy, centres[:, 1]], 1)
    r = np.hypot(half * math.sqrt(2.0), ry) * 1.02 + 1.0
    v = C - eye
    z = v @ fwd
    x = v @ right
    y = v @ up
    ok = (z + r > 0.5) & (z - r < far)
    ok &= np.abs(x) - r * math.sqrt(1.0 + th * th) <= z * th
    ok &= np.abs(y) - r * math.sqrt(1.0 + tv * tv) <= z * tv
    ok |= np.linalg.norm(v, axis=1) < r          # the eye is inside the block
    if vd > 0:
        ok &= edge_d <= vd
    return ok


class ViewWidget(QOpenGLWidget):
    """Draws the whole world. interactive=True is the walk (mouse look, WASD, gravity, fly).
    interactive=False is a fixed overhead view for the Theme tab."""

    def __init__(self, look, interactive, parent=None):
        super().__init__(parent)
        self.look = look
        self.interactive = interactive
        self.h = None
        self.version = None
        self.mesh = None
        self.bufs = []
        self.nidx = 0
        self.progs = {}
        self.error = ""
        self.pos = np.zeros(3)
        self.yaw = 0.0
        self.pitch = -0.25
        self.vy = 0.0
        self.grounded = False
        self.fly = False
        self.jump = False
        self.last_space = -10.0
        self.speed = 1.0
        self.keys = set()
        self.captured = False
        self.t0 = self.last = time.perf_counter()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)
        self.setMinimumSize(320, 240)
        self.hud = QLabel(self)
        self.hud.setFont(QFont("monospace", 10))
        self.hud.setStyleSheet("color:#dfe9f5; background:rgba(8,12,18,170); padding:6px 8px; border-radius:6px;")
        self.hud.move(12, 12)
        self.hud.setVisible(interactive)

    # ----- data -----
    def sync(self, ed):
        """Rebuild the mesh if the editor has changed since this view last looked."""
        if self.version == ed.version and self.h is ed.h:
            return
        self.set_world(ed.h, ed.b, ed.fk, ed.fa, ed.sea, ed.version)

    def set_world(self, h, b, fk, fa, sea, version):
        self._set_world_inner(h, b, fk, fa, sea, version)

    def _set_world_inner(self, h, b, fk, fa, sea, version):
        first = self.h is None or self.h.shape != h.shape
        self.h = h
        self.b = b
        self.H, self.W = h.shape
        self.sea = float(sea)
        self.vs = max(self.W, self.H) * VS_FRAC
        self.version = version
        Y = h.astype(np.float64) * self.vs
        gx = np.gradient(Y, axis=1); gz = np.gradient(Y, axis=0)
        n = np.stack([-gx, np.ones_like(Y), -gz], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        X, Z = np.meshgrid(np.arange(self.W, dtype=np.float32), np.arange(self.H, dtype=np.float32))
        pos = np.stack([X, Y.astype(np.float32), Z], -1).reshape(-1, 3)
        col = ground_colours(b, fk, fa) / 255.0
        step = max(1, math.ceil(max(self.W, self.H) / 4096))
        depth = np.clip((self.sea - h[::step, ::step].astype(np.float64)) / max(self.sea, 1e-6), 0.0, 1.0)
        self.depth_src = np.ascontiguousarray((depth * 255.0).astype(np.uint8))
        self.depth_dirty = True
        idx, cen, bnd, starts, counts = build_lod_index(self.W, self.H)
        self.blk = (cen, starts, counts, *block_height_range(h, bnd))
        self.mesh = (np.ascontiguousarray(pos, np.float32),
                     np.ascontiguousarray(col.reshape(-1, 3), np.float32),
                     np.ascontiguousarray(n.reshape(-1, 3), np.float32),
                     idx)
        if first:
            self._spawn()
        self.update()

    def _spawn(self):
        """Start on land near the middle of the map."""
        H, W = self.H, self.W
        jj, ii = np.indices((H, W))
        d = (ii - W / 2) ** 2 + (jj - H / 2) ** 2
        d = np.where(self.h >= self.sea, d, np.inf)
        if np.isfinite(d).any():
            j, i = np.unravel_index(int(np.argmin(d)), d.shape)
        else:
            j, i = H // 2, W // 2
        self.pos = np.array([float(i), self.ground(i, j) + EYE * self.player_scale(), float(j)])

    def player_scale(self):
        return clamp(float(self.look.player_scale), 0.1, 1.0)

    def ground(self, x, z):
        if not (0.0 <= x <= self.W - 1.0 and 0.0 <= z <= self.H - 1.0):
            return self.sea * self.vs          # past the map edge the sea goes on
        x = clamp(float(x), 0.0, self.W - 1.001)
        z = clamp(float(z), 0.0, self.H - 1.001)
        i = int(x); j = int(z); fx = x - i; fz = z - j
        h = self.h
        a = float(h[j, i]); b = float(h[j, i + 1]); c = float(h[j + 1, i]); d = float(h[j + 1, i + 1])
        return ((a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz) * self.vs

    # ----- input -----
    def showEvent(self, e):
        super().showEvent(e)
        self.last = time.perf_counter()
        self.timer.start(16)
        self.setFocus()

    def hideEvent(self, e):
        self.timer.stop()
        self.release()
        super().hideEvent(e)

    def capture(self):
        self.captured = True
        self.setCursor(Qt.CursorShape.BlankCursor)
        QCursor.setPos(self.mapToGlobal(QPoint(self.width() // 2, self.height() // 2)))

    def release(self):
        self.captured = False
        self.unsetCursor()
        self.keys.clear()

    def mousePressEvent(self, e):
        self.setFocus()
        if self.interactive and e.button() == Qt.MouseButton.LeftButton and not self.captured:
            self.capture()

    def mouseMoveEvent(self, e):
        if self.captured:
            c = QPointF(self.width() / 2, self.height() / 2)
            d = e.position() - c
            self.yaw += d.x() * SENS
            self.pitch = clamp(self.pitch - d.y() * SENS, -1.5, 1.5)
            QCursor.setPos(self.mapToGlobal(QPoint(self.width() // 2, self.height() // 2)))

    def wheelEvent(self, e):
        dy = e.angleDelta().y()
        if dy:
            self.speed = clamp(self.speed * (1.15 if dy > 0 else 1 / 1.15), 0.1, 20.0)

    def keyPressEvent(self, e):
        if e.isAutoRepeat():
            return
        k = int(e.key())
        if k == K["Escape"]:
            self.release()
        elif k == K["F"]:
            self.toggle_fly()
        elif k == K["Space"]:
            now = time.perf_counter()
            if now - self.last_space < DOUBLE_TAP:
                self.last_space = -10.0
                self.toggle_fly()
            else:
                self.last_space = now
                if not self.fly:
                    self.jump = True
            self.keys.add(k)
        else:
            self.keys.add(k)

    def toggle_fly(self):
        self.fly = not self.fly
        self.vy = 0.0
        self.jump = False
        self.grounded = False

    def keyReleaseEvent(self, e):
        if not e.isAutoRepeat():
            self.keys.discard(int(e.key()))

    def focusOutEvent(self, e):
        self.keys.clear()
        super().focusOutEvent(e)

    # ----- camera and movement -----
    def camera(self):
        if self.interactive:
            cp = math.cos(self.pitch)
            eye = self.pos.copy()
            return eye, eye + np.array([math.sin(self.yaw) * cp, math.sin(self.pitch), -math.cos(self.yaw) * cp])
        cx, cz, cy = self.W / 2, self.H / 2, self.sea * self.vs
        a, p = 0.6, 0.6
        D = 1.1 * max(self.W, self.H)
        eye = np.array([cx + D * math.sin(a) * math.cos(p), cy + D * math.sin(p), cz - D * math.cos(a) * math.cos(p)])
        return eye, np.array([cx, cy, cz])

    def move(self, dt):
        k = self.keys
        f = (K["W"] in k) - (K["S"] in k)
        r = (K["D"] in k) - (K["A"] in k)
        boost = 4.0 if K["Shift"] in k else 1.0
        yaw = self.yaw
        ps = self.player_scale()
        if self.fly:
            cp = math.cos(self.pitch)
            fwd = np.array([math.sin(yaw) * cp, math.sin(self.pitch), -math.cos(yaw) * cp])
            right = np.array([math.cos(yaw), 0.0, math.sin(yaw)])
            up = ((K["Space"] in k) or (K["E"] in k)) - ((K["C"] in k) or (K["Q"] in k))
            v = fwd * f + right * r + np.array([0.0, float(up), 0.0])
            n = np.linalg.norm(v)
            if n > 0:
                self.pos += v / n * FLY_SPEED * ps * self.speed * boost * dt
            self.pos[1] = clamp(self.pos[1], -self.vs, 6 * self.vs)
            return
        v = np.array([math.sin(yaw), -math.cos(yaw)]) * f + np.array([math.cos(yaw), math.sin(yaw)]) * r
        n = np.linalg.norm(v)
        if n > 0:
            v = v / n * WALK_SPEED * ps * self.speed * boost * dt
            self.pos[0] += v[0]
            self.pos[2] += v[1]
        g = self.ground(self.pos[0], self.pos[2]) + EYE * ps
        if self.jump and self.grounded:
            self.vy = JUMP_V * ps
        self.jump = False
        self.vy -= GRAVITY * ps * dt
        self.pos[1] += self.vy * dt
        if self.pos[1] <= g:
            self.pos[1] = g
            self.vy = 0.0
            self.grounded = True
        else:
            self.grounded = False

    # ----- drawing -----
    def initializeGL(self):
        gl.glEnable(gl.GL_DEPTH_TEST)
        try:
            self.progs["terrain"] = link(TERRAIN_VS, TERRAIN_FS, ["aPos", "aCol", "aNrm"])
            self.progs["cloud"] = link(CLOUD_VS, CLOUD_FS, [])
        except RuntimeError as ex:
            self.error = str(ex)[:800]
        try:
            self.progs["water"] = link(CLOUD_VS, WATER_FS, [])
        except RuntimeError:
            self.progs.pop("water", None)       # the flat colour water is drawn instead
        try:
            self.progs["sun"] = link(CLOUD_VS, SUN_FS, [])
        except RuntimeError:
            self.progs.pop("sun", None)         # the plain sun disc is drawn instead

    def _upload_depth(self):
        if getattr(self, "depth_tex", None) is None:
            self.depth_tex = int(np.atleast_1d(gl.glGenTextures(1))[0])
        gl.glBindTexture(gl.GL_TEXTURE_2D, self.depth_tex)
        gl.glPixelStorei(gl.GL_UNPACK_ALIGNMENT, 1)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MIN_FILTER, gl.GL_LINEAR)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MAG_FILTER, gl.GL_LINEAR)
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_S, 0x812F)   # clamp to edge
        gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_T, 0x812F)
        dh, dw = self.depth_src.shape
        gl.glTexImage2D(gl.GL_TEXTURE_2D, 0, gl.GL_LUMINANCE, dw, dh, 0,
                        gl.GL_LUMINANCE, gl.GL_UNSIGNED_BYTE, self.depth_src)
        gl.glBindTexture(gl.GL_TEXTURE_2D, 0)
        self.depth_dirty = False

    def _upload(self):
        if not self.bufs:
            self.bufs = [int(v) for v in np.atleast_1d(gl.glGenBuffers(4))]
        pos, col, nrm, idx = self.mesh
        for buf, data, target in zip(self.bufs, (pos, col, nrm, idx),
                                     (gl.GL_ARRAY_BUFFER, gl.GL_ARRAY_BUFFER, gl.GL_ARRAY_BUFFER,
                                      gl.GL_ELEMENT_ARRAY_BUFFER)):
            gl.glBindBuffer(target, buf)
            gl.glBufferData(target, data.nbytes, data, gl.GL_STATIC_DRAW)
        self.nidx = idx.size
        self.mesh = None

    def paintGL(self):
        now = time.perf_counter()
        dt = min(0.05, now - self.last)
        self.last = now
        look = self.look
        if look.day_cycle:
            f = day_fraction(look)
            p = (phase_from_clock(look.clock, f) + dt / max(1.0, look.day_length)) % 1.0
            look.clock = clock_from_phase(p, f)
        if self.interactive:
            self.move(dt)
            self._update_hud()
        if self.mesh is not None:
            self._upload()
        ratio = self.devicePixelRatioF()
        gl.glViewport(0, 0, max(1, int(self.width() * ratio)), max(1, int(self.height() * ratio)))
        sk = sky_state(look)
        gl.glClearColor(*[float(c) for c in sk["horizon"]], 1.0)
        gl.glClear(gl.GL_COLOR_BUFFER_BIT | gl.GL_DEPTH_BUFFER_BIT)
        if self.h is None or self.error:
            return
        eye, target = self.camera()
        far = 4.0 * max(self.W, self.H) + 200.0
        vd = float(look.view_distance)
        if self.interactive and vd > 0:
            far = min(far, vd * 1.3 + 80.0)   # nothing past the view distance is drawn
        gl.glMatrixMode(gl.GL_PROJECTION)
        gl.glLoadIdentity()
        gluPerspective(65.0, max(1e-3, self.width() / max(1, self.height())), 0.5, far)
        gl.glMatrixMode(gl.GL_MODELVIEW)
        gl.glLoadIdentity()
        gluLookAt(*eye, *target, 0.0, 1.0, 0.0)
        self._draw_sky(sk)
        self._draw_sun(sk, eye, far)
        self._draw_terrain(sk, eye, far)
        if getattr(self, "depth_dirty", False) and getattr(self, "depth_src", None) is not None:
            self._upload_depth()
        self._draw_water(sk, eye, far)
        self._draw_clouds(sk, eye, far)

    def _update_hud(self):
        if self.h is None:
            return
        x, z = self.pos[0], self.pos[2]
        i = int(clamp(round(x), 0, self.W - 1)); j = int(clamp(round(z), 0, self.H - 1))
        mode = "FLY (F to walk)" if self.fly else "WALK (F to fly)"
        self.hud.setText(f"{mode} · {BIOMES[int(self.b[j, i])][0]}\n"
                         f"x {x:.0f}  z {z:.0f}  alt {self.pos[1] - self.sea * self.vs:+.0f}\n"
                         f"speed ×{self.speed:.1f} · time {self._clock_text()}\n"
                         "Click to look · Esc release · double-tap Space flies")
        self.hud.adjustSize()

    def _clock_text(self):
        t = int(round((self.look.clock % 24.0) * 60)) % 1440
        return f"{t // 60:02d}:{t % 60:02d}"

    def _draw_sky(self, sk):
        gl.glDisable(gl.GL_DEPTH_TEST)
        gl.glDepthMask(gl.GL_FALSE)
        gl.glMatrixMode(gl.GL_PROJECTION)
        gl.glPushMatrix(); gl.glLoadIdentity()
        gl.glMatrixMode(gl.GL_MODELVIEW)
        gl.glPushMatrix(); gl.glLoadIdentity()
        hor, top = sk["horizon"], sk["top"]
        gl.glBegin(gl.GL_QUADS)
        gl.glColor3f(*[float(c) for c in hor]); gl.glVertex2f(-1, -1); gl.glVertex2f(1, -1)
        gl.glColor3f(*[float(c) for c in top]); gl.glVertex2f(1, 1); gl.glVertex2f(-1, 1)
        gl.glEnd()
        gl.glMatrixMode(gl.GL_PROJECTION); gl.glPopMatrix()
        gl.glMatrixMode(gl.GL_MODELVIEW); gl.glPopMatrix()
        gl.glDepthMask(gl.GL_TRUE)
        gl.glEnable(gl.GL_DEPTH_TEST)

    def _draw_sun(self, sk, eye, far):
        sun = sk["sun"]
        alpha = sk["day"] * (1.0 if sun[1] > 0.02 else 0.0)
        if alpha < 0.02:
            return
        d = sun / np.linalg.norm(sun)
        t = np.cross(d, [0.0, 1.0, 0.0])
        if np.linalg.norm(t) < 1e-6:
            t = np.array([1.0, 0.0, 0.0])
        t /= np.linalg.norm(t)
        b = np.cross(d, t)
        c = eye + d * far * 0.8
        r = far * 0.8 * math.tan(math.radians(2.5))
        rgb = self.look.sun_rgb
        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)
        gl.glDepthMask(gl.GL_FALSE)
        prog = self.progs.get("sun")
        if prog is not None and self.look.cloud_cover >= 0.02:
            gl.glUseProgram(prog)
            self._cloud_uniforms(prog)
            self._u3(prog, "uSunRGB", rgb)
            self._u1(prog, "uAlpha", float(alpha))
            # the sun's line of sight meets the cloud layer here, if the layer is in front of us
            ty = (self.look.cloud_height * self.vs - eye[1]) / float(d[1]) if d[1] > 1e-3 else -1.0
            occ = ty > 0.0
            px = eye[0] + d[0] * ty if occ else 0.0
            pz = eye[2] + d[2] * ty if occ else 0.0
            self._u2(prog, "uSunXZ", px, pz)
            self._u1(prog, "uOcc", 1.0 if occ else 0.0)
        else:
            prog = None
            gl.glColor4f(float(rgb[0]), float(rgb[1]), float(rgb[2]), float(alpha))
        gl.glBegin(gl.GL_TRIANGLE_FAN)
        gl.glVertex3f(*c)
        for k in range(25):
            a = 2 * math.pi * k / 24
            p = c + r * (math.cos(a) * t + math.sin(a) * b)
            gl.glVertex3f(*p)
        gl.glEnd()
        if prog is not None:
            gl.glUseProgram(0)
        gl.glDepthMask(gl.GL_TRUE)
        gl.glDisable(gl.GL_BLEND)

    def _draw_terrain(self, sk, eye, far):
        prog = self.progs.get("terrain")
        if prog is None or not self.bufs:
            return
        look = self.look
        fog_den = self._fog_den()
        sun = sk["sun"]
        gl.glUseProgram(prog)
        self._u3(prog, "uSun", sun); self._u1(prog, "uSunK", sk["sun_k"]); self._u1(prog, "uAmb", sk["amb"])
        self._u3(prog, "uSunRGB", look.sun_rgb); self._u3(prog, "uFog", sk["horizon"])
        self._u1(prog, "uFogDen", fog_den); self._u3(prog, "uCam", eye)
        self._u3(prog, "uRock", look.rock_rgb); self._u1(prog, "uRockSlope", look.rock_slope)
        self._u1(prog, "uDetail", look.surface_detail)
        clouds_on = look.cloud_cover >= 0.02
        self._u1(prog, "uShadow", float(look.cloud_opacity) * 0.6 if clouds_on else 0.0)
        if clouds_on:
            self._cloud_uniforms(prog)
        pos, col, nrm, idx = self.bufs
        for i, (buf, size) in enumerate(((pos, 3), (col, 3), (nrm, 3))):
            gl.glEnableVertexAttribArray(i)
            gl.glBindBuffer(gl.GL_ARRAY_BUFFER, buf)
            gl.glVertexAttribPointer(i, size, gl.GL_FLOAT, gl.GL_FALSE, 0, None)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, idx)

        cen, starts, counts, lo, hi = self.blk
        half = block_size(self.W, self.H) / 2.0
        ex, ez = eye[0], eye[2]
        # horizontal distance from the eye to each block's edge; sets its level of detail
        edge_d = np.hypot(np.maximum(np.abs(cen[:, 0] - ex) - half, 0.0),
                          np.maximum(np.abs(cen[:, 1] - ez) - half, 0.0))
        level = np.searchsorted(LOD_RANGES, edge_d, side="right")
        vd = float(look.view_distance)
        if self.interactive:
            _, target = self.camera()
            aspect = max(1e-3, self.width() / max(1, self.height()))
            visible = cull_blocks(eye, target, aspect, 65.0, far, cen, lo, hi, half, self.vs,
                                  edge_d, vd)
        else:
            visible = np.ones(len(cen), bool)
        for k in np.nonzero(visible)[0]:
            L = level[k]
            gl.glDrawElements(gl.GL_TRIANGLES, int(counts[k, L]), gl.GL_UNSIGNED_INT,
                              c_void_p(int(starts[k, L]) * 4))
        for i in range(3):
            gl.glDisableVertexAttribArray(i)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, 0)
        gl.glUseProgram(0)

    def _draw_water(self, sk, eye, far):
        look = self.look
        y = self.sea * self.vs
        e = 4.0 * max(self.W, self.H) + 200.0
        x0, x1, z0, z1 = -e, self.W + e, -e, self.H + e
        prog = self.progs.get("water")
        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)
        gl.glDepthMask(gl.GL_FALSE)
        if prog is not None:
            gl.glUseProgram(prog)
            ang = math.radians(float(look.cloud_direction))
            speed = float(look.water_speed) * 0.03
            self._u1(prog, "uTime", time.perf_counter() - self.t0)
            self._u2(prog, "uWind", speed * math.cos(ang), speed * math.sin(ang))
            self._u1(prog, "uSeed", float(look.seed % 997))
            self._u3(prog, "uWaterRGB", look.water_rgb)
            self._u1(prog, "uAlpha", float(look.water_alpha))
            self._u1(prog, "uDensity", float(look.water_density))
            self._u1(prog, "uWave", float(look.water_wave))
            self._u3(prog, "uSun", sk["sun"]); self._u1(prog, "uSunK", sk["sun_k"])
            self._u1(prog, "uAmb", sk["amb"]); self._u3(prog, "uSunRGB", look.sun_rgb)
            self._u3(prog, "uFog", sk["horizon"]); self._u1(prog, "uFogDen", self._fog_den())
            self._u3(prog, "uCam", eye)
            self._u2(prog, "uMap", float(self.W), float(self.H))
            self._u1(prog, "uDepthMax", max(self.sea, 1e-3) * self.vs)
            tex = getattr(self, "depth_tex", None)
            self._u1(prog, "uHasDepth", 1.0 if tex else 0.0)
            if tex:
                gl.glActiveTexture(gl.GL_TEXTURE0)
                gl.glBindTexture(gl.GL_TEXTURE_2D, tex)
                loc = gl.glGetUniformLocation(prog, "uDepth")
                if loc >= 0:
                    gl.glUniform1i(loc, 0)
        else:
            r, g, b = look.water_rgb
            gl.glColor4f(float(r), float(g), float(b), float(look.water_alpha))
        gl.glBegin(gl.GL_QUADS)
        gl.glVertex3f(x0, y, z0); gl.glVertex3f(x1, y, z0)
        gl.glVertex3f(x1, y, z1); gl.glVertex3f(x0, y, z1)
        gl.glEnd()
        if prog is not None:
            gl.glBindTexture(gl.GL_TEXTURE_2D, 0)
            gl.glUseProgram(0)
        gl.glDepthMask(gl.GL_TRUE)
        gl.glDisable(gl.GL_BLEND)

    def _cloud_uniforms(self, prog):
        look = self.look
        ang = math.radians(float(look.cloud_direction))
        # the sky layer is far away, so its drift needs a larger step to read as motion
        speed = float(look.cloud_speed) * 0.2
        self._u1(prog, "uTime", time.perf_counter() - self.t0)
        self._u1(prog, "uCover", float(look.cloud_cover))
        self._u2(prog, "uWind", speed * math.cos(ang), speed * math.sin(ang))
        self._u1(prog, "uSeed", float(look.seed % 997))
        self._u1(prog, "uScale", 0.01)
        self._u1(prog, "uCloudY", float(look.cloud_height) * self.vs)

    def _draw_clouds(self, sk, eye, far):
        look = self.look
        prog = self.progs.get("cloud")
        if prog is None or look.cloud_cover < 0.02:
            return
        y = float(look.cloud_height) * self.vs
        amb, sk_k = sk["amb"], sk["sun_k"]
        lit = [amb + sk_k * 0.9 * float(c) for c in look.sun_rgb]
        gl.glUseProgram(prog)
        t = time.perf_counter() - self.t0
        self._cloud_uniforms(prog)
        self._u1(prog, "uOpacity", look.cloud_opacity)
        self._u3(prog, "uLit", lit)
        self._u3(prog, "uCloudRGB", look.cloud_rgb)
        self._u3(prog, "uFog", sk["horizon"])
        self._u1(prog, "uFogDen", self._fog_den())
        self._u3(prog, "uCam", eye)
        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)
        gl.glDepthMask(gl.GL_FALSE)
        R = far * 0.9
        x0, x1 = self.W / 2 - R, self.W / 2 + R
        z0, z1 = self.H / 2 - R, self.H / 2 + R
        gl.glBegin(gl.GL_QUADS)
        gl.glVertex3f(x0, y, z0); gl.glVertex3f(x1, y, z0)
        gl.glVertex3f(x1, y, z1); gl.glVertex3f(x0, y, z1)
        gl.glEnd()
        gl.glDepthMask(gl.GL_TRUE)
        gl.glDisable(gl.GL_BLEND)
        gl.glUseProgram(0)

    def _fog_den(self):
        """Fog thickens so that, with a view distance set, the terrain fades out at that distance."""
        vd = float(self.look.view_distance)
        if self.interactive and vd > 0:
            return 2.0 / vd * self.look.fog
        return 1.27 / max(self.W, self.H) * self.look.fog

    # ----- uniform helpers -----
    @staticmethod
    def _u1(prog, name, v):
        loc = gl.glGetUniformLocation(prog, name)
        if loc >= 0:
            gl.glUniform1f(loc, float(v))

    @staticmethod
    def _u2(prog, name, a, b):
        loc = gl.glGetUniformLocation(prog, name)
        if loc >= 0:
            gl.glUniform2f(loc, float(a), float(b))

    @staticmethod
    def _u3(prog, name, v):
        loc = gl.glGetUniformLocation(prog, name)
        if loc >= 0:
            gl.glUniform3f(loc, float(v[0]), float(v[1]), float(v[2]))


# ===== window =====
import json
import random
import sys

import numpy as np
from PyQt6.QtCore import QRectF, QThread, Qt, QTime, pyqtSignal
from PyQt6.QtGui import QAction, QColor, QImage, QKeySequence, QPainter, QPen, QPixmap, QSurfaceFormat
from PyQt6.QtWidgets import (
    QApplication, QButtonGroup, QCheckBox, QColorDialog, QComboBox, QDoubleSpinBox, QFileDialog, QFormLayout,
    QGroupBox, QHBoxLayout, QLabel, QMainWindow, QMessageBox, QProgressBar, QPushButton,
    QRadioButton, QScrollArea, QSizePolicy, QSlider, QSpinBox, QTabWidget, QTimeEdit,
    QVBoxLayout, QWidget,
)


STYLE = """
QWidget { background:#0b1016; color:#cfd9e6; font-size:12px; }
QGroupBox { border:1px solid #1d2a38; border-radius:8px; margin-top:14px; padding:12px 10px 8px 10px; }
QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; color:#8fe6c0; }
QPushButton { background:#131c27; border:1px solid #243446; border-radius:6px; padding:6px 14px; }
QPushButton:hover { background:#1a2a3a; }
QPushButton:disabled { color:#4a5a6c; }
QPushButton#primary { background:#12301f; border-color:#2f6a4d; color:#8fe6c0; font-weight:600; }
QComboBox, QSpinBox, QDoubleSpinBox, QTimeEdit { background:#131c27; border:1px solid #243446; border-radius:5px; padding:3px 6px; }
QComboBox QAbstractItemView { background:#131c27; selection-background-color:#1a2a3a; }
QTabWidget::pane { border:1px solid #1d2a38; border-radius:8px; }
QTabBar::tab { background:#0f151d; padding:8px 18px; border:1px solid #1d2a38; border-bottom:none; margin-right:2px; }
QTabBar::tab:selected { background:#0b1016; color:#8fe6c0; }
QTabBar::tab:disabled { color:#3a4a5c; }
QSlider::groove:horizontal { height:4px; background:#1d2a38; border-radius:2px; }
QSlider::sub-page:horizontal { background:#3b8f70; border-radius:2px; }
QSlider::handle:horizontal { background:#8fe6c0; width:12px; height:12px; margin:-4px 0; border-radius:6px; }
QProgressBar { border:1px solid #1d2a38; border-radius:4px; text-align:center; height:10px; }
QProgressBar::chunk { background:#3b8f70; }
"""


def _qtime(hours):
    h = float(hours) % 24.0
    return QTime(int(h), min(59, int(round((h - int(h)) * 60))))


class NumberSlider(QWidget):
    changed = pyqtSignal(float)

    Q = 1000

    def __init__(self, lo, hi, value, decimals=2, suffix=""):
        super().__init__()
        self.lo, self.hi, self.dec, self.suffix = float(lo), float(hi), decimals, suffix
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(0, self.Q)
        self.label = QLabel()
        self.label.setMinimumWidth(56)
        self.slider.setMinimumWidth(150)
        self.slider.setMinimumHeight(22)
        row.setSpacing(10)
        self.label.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        row.addWidget(self.slider, 1)
        row.addWidget(self.label)
        self.slider.valueChanged.connect(self._on_slide)
        self.set_value(value)

    def value(self):
        return self.lo + (self.hi - self.lo) * self.slider.value() / self.Q

    def set_value(self, v):
        v = clamp(float(v), self.lo, self.hi)
        self.slider.blockSignals(True)
        self.slider.setValue(round((v - self.lo) / (self.hi - self.lo) * self.Q))
        self.slider.blockSignals(False)
        self._show(self.value())

    def _show(self, v):
        self.label.setText(f"{v:.{self.dec}f}{self.suffix}")

    def _on_slide(self, _):
        v = self.value()
        self._show(v)
        self.changed.emit(v)


class ColorButton(QPushButton):
    changed = pyqtSignal()

    def __init__(self, title):
        super().__init__()
        self.title = title
        self.rgb = [0.0, 0.0, 0.0]
        self.clicked.connect(self._pick)

    def set_rgb(self, rgb):
        self.rgb = [float(c) for c in rgb]
        c = QColor.fromRgbF(*self.rgb)
        lum = 0.299 * self.rgb[0] + 0.587 * self.rgb[1] + 0.114 * self.rgb[2]
        ink = "#0c1118" if lum > 0.5 else "#f2f6fb"
        self.setText(f"{self.title}   {c.name().upper()}")
        self.setStyleSheet(f"background:{c.name()}; color:{ink}; border:1px solid #33465c; "
                           "border-radius:6px; padding:6px 10px; text-align:left;")

    def _pick(self):
        c = QColorDialog.getColor(QColor.fromRgbF(*self.rgb), self, self.title)
        if c.isValid():
            self.set_rgb([c.redF(), c.greenF(), c.blueF()])
            self.changed.emit()


class MapCanvas(QWidget):
    """A map image you can zoom (wheel, + and -), pan (right drag) and paint (left drag)."""
    stroke_started = pyqtSignal(float, float)
    painted = pyqtSignal(float, float)
    stroke_ended = pyqtSignal()
    hovered = pyqtSignal(float, float)

    def __init__(self):
        super().__init__()
        self.img = None
        self.scale = 1.0
        self.fit_scale = 1.0
        self.ox = self.oy = 0.0
        self.fitted = True
        self.cursor = None
        self.brush_r = 5.0
        self.shape = "circle"
        self.pan = None
        self.setMouseTracking(True)
        self.setMinimumSize(320, 320)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    def set_image(self, rgb):
        h, w = rgb.shape[:2]
        size_changed = self.img is None or self.img.width() != w or self.img.height() != h
        self.img = QImage(np.ascontiguousarray(rgb).tobytes(), w, h, 3 * w,
                          QImage.Format.Format_RGB888).copy()
        if size_changed or self.fitted:
            self.fit()
        self.update()

    def fit(self):
        if self.img is None:
            return
        self.fitted = True
        self.fit_scale = max(0.01, min((self.width() - 16) / self.img.width(),
                                       (self.height() - 16) / self.img.height()))
        self.scale = self.fit_scale
        self.ox = (self.width() - self.img.width() * self.scale) / 2
        self.oy = (self.height() - self.img.height() * self.scale) / 2
        self.update()

    def zoom_at(self, factor, px, py):
        if self.img is None:
            return
        gx, gy = (px - self.ox) / self.scale, (py - self.oy) / self.scale
        self.scale = clamp(self.scale * factor, self.fit_scale * 0.9, 48.0)
        self.ox = px - gx * self.scale
        self.oy = py - gy * self.scale
        self.fitted = False
        self.update()

    def to_map(self, pos):
        return (pos.x() - self.ox) / self.scale, (pos.y() - self.oy) / self.scale

    def paintEvent(self, _):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(12, 16, 22))
        if self.img is None:
            p.end()
            return
        p.translate(self.ox, self.oy)
        p.scale(self.scale, self.scale)
        p.drawImage(0, 0, self.img)
        if self.cursor is not None:
            cx, cy = self.cursor
            r = self.brush_r + 0.5
            p.setPen(QPen(QColor(255, 255, 255, 220), 1.0 / self.scale))
            p.setBrush(Qt.BrushStyle.NoBrush)
            box = QRectF(cx - r, cy - r, 2 * r, 2 * r)
            if self.shape == "square":
                p.drawRect(box)
            else:
                p.drawEllipse(box)
        p.end()

    def wheelEvent(self, e):
        dy = e.angleDelta().y()
        if dy:
            self.zoom_at(1.15 ** (dy / 120.0), e.position().x(), e.position().y())

    def keyPressEvent(self, e):
        if e.key() in (Qt.Key.Key_Plus, Qt.Key.Key_Equal):
            self.zoom_at(1.25, self.width() / 2, self.height() / 2)
        elif e.key() in (Qt.Key.Key_Minus, Qt.Key.Key_Underscore):
            self.zoom_at(0.8, self.width() / 2, self.height() / 2)
        elif e.key() in (Qt.Key.Key_F, Qt.Key.Key_0):
            self.fit()
        else:
            super().keyPressEvent(e)

    def mousePressEvent(self, e):
        self.setFocus()
        if e.button() == Qt.MouseButton.RightButton:
            self.pan = (e.position(), self.ox, self.oy)
        elif e.button() == Qt.MouseButton.LeftButton and self.img is not None:
            gx, gy = self.to_map(e.position())
            self.stroke_started.emit(gx, gy)

    def mouseMoveEvent(self, e):
        if self.pan is not None:
            p0, ox0, oy0 = self.pan
            self.ox = ox0 + e.position().x() - p0.x()
            self.oy = oy0 + e.position().y() - p0.y()
            self.fitted = False
            self.update()
            return
        if self.img is None:
            return
        gx, gy = self.to_map(e.position())
        inside = 0 <= gx < self.img.width() and 0 <= gy < self.img.height()
        self.cursor = (gx, gy) if inside else None
        if inside:
            self.hovered.emit(gx, gy)
            if e.buttons() & Qt.MouseButton.LeftButton:
                self.painted.emit(gx, gy)
        self.update()

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.MouseButton.RightButton:
            self.pan = None
        elif e.button() == Qt.MouseButton.LeftButton:
            self.stroke_ended.emit()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        if self.fitted:
            self.fit()


class BuildThread(QThread):
    done = pyqtSignal(str, object, object)   # purpose, settings, world
    progress = pyqtSignal(float, str)        # fraction done, what is happening

    def __init__(self, s, purpose):
        super().__init__()
        self.s = s
        self.purpose = purpose

    def run(self):
        world = build_world(self.s, lambda f, text: self.progress.emit(f, text))
        self.done.emit(self.purpose, self.s, world)


class ThemePanel(QWidget):
    changed = pyqtSignal()

    def __init__(self, look):
        super().__init__()
        self.look = look
        self.setFixedWidth(340)
        self.widgets = []
        outer = QVBoxLayout(self)
        title = QLabel("Map Theme")
        title.setStyleSheet("font-size:16px; font-weight:600;")
        note = QLabel("The look of the world. Saved as its own .wglv file, apart from the map.")
        note.setWordWrap(True)
        note.setStyleSheet("color:#8fa3b8;")
        outer.addWidget(title)
        outer.addWidget(note)

        body = QWidget()
        form = QFormLayout(body)
        form.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapAllRows)
        for item in THEME_ITEMS:
            kind = item[0]
            if kind == "heading":
                lab = QLabel(item[1])
                lab.setStyleSheet("font-weight:600; color:#8fe6c0; padding-top:8px;")
                form.addRow(lab)
                continue
            name = item[1]
            label = item[2]
            if kind == "slider":
                w = NumberSlider(item[3], item[4], getattr(look, name), item[5])
                w.changed.connect(lambda v, n=name: self._set(n, v))
                form.addRow(label, w)
            elif kind == "colour":
                w = ColorButton(label)
                w.set_rgb(getattr(look, name))
                w.changed.connect(lambda n=name, b=w: self._set(n, b.rgb))
                form.addRow(w)
            elif kind == "check":
                w = QCheckBox(label)
                w.setChecked(getattr(look, name))
                w.toggled.connect(lambda on, n=name: self._set(n, bool(on)))
                form.addRow(w)
            elif kind == "ratio":
                other = item[3]
                row_w = QWidget()
                r = QHBoxLayout(row_w)
                r.setContentsMargins(0, 0, 0, 0)
                a = QDoubleSpinBox()
                b = QDoubleSpinBox()
                for box, attr in ((a, name), (b, other)):
                    box.setRange(0.1, 100.0)
                    box.setDecimals(1)
                    box.setSingleStep(0.5)
                    box.setValue(float(getattr(look, attr)))
                a.valueChanged.connect(lambda v, n=name: self._set(n, float(v)))
                b.valueChanged.connect(lambda v, n=other: self._set(n, float(v)))
                r.addWidget(a, 1)
                colon = QLabel(":")
                colon.setAlignment(Qt.AlignmentFlag.AlignCenter)
                r.addWidget(colon)
                r.addWidget(b, 1)
                form.addRow(label, row_w)
                self.widgets.append(("ratio", name, (a, b, other)))
                continue
            else:  # time
                w = QTimeEdit()
                w.setDisplayFormat("HH:mm")
                w.setTime(_qtime(look.clock))
                w.timeChanged.connect(lambda t: self._set("clock", t.hour() + t.minute() / 60.0))
                form.addRow(label, w)
            self.widgets.append((kind, name, w))

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(body)
        outer.addWidget(scroll, 1)

        row1 = QHBoxLayout()
        self.random_btn = QPushButton("Random theme")
        self.reset_btn = QPushButton("Reset")
        row1.addWidget(self.random_btn)
        row1.addWidget(self.reset_btn)
        row2 = QHBoxLayout()
        self.export_btn = QPushButton("Export theme…")
        self.import_btn = QPushButton("Import theme…")
        row2.addWidget(self.export_btn)
        row2.addWidget(self.import_btn)
        outer.addLayout(row1)
        outer.addLayout(row2)
        self.status = QLabel("")
        self.status.setStyleSheet("color:#8fa3b8;")
        self.status.setWordWrap(True)
        outer.addWidget(self.status)

        self.random_btn.clicked.connect(self._random)
        self.reset_btn.clicked.connect(lambda: (replace_look(self.look, Look()), self.sync(), self.changed.emit()))
        self.export_btn.clicked.connect(self._export)
        self.import_btn.clicked.connect(self._import)

    def _set(self, name, value):
        if isinstance(value, list):
            value = [float(c) for c in value]
        setattr(self.look, name, value)
        self.changed.emit()

    def sync(self):
        for kind, name, w in self.widgets:
            if kind == "ratio":
                a, b, other = w
                a.blockSignals(True); b.blockSignals(True)
                a.setValue(float(getattr(self.look, name)))
                b.setValue(float(getattr(self.look, other)))
                a.blockSignals(False); b.blockSignals(False)
                continue
            v = getattr(self.look, name)
            w.blockSignals(True)
            if kind == "slider":
                w.set_value(v)
            elif kind == "colour":
                w.set_rgb(v)
            elif kind == "check":
                w.setChecked(bool(v))
            else:
                w.setTime(_qtime(v))
            w.blockSignals(False)

    def _random(self):
        randomize_look(self.look)
        self.sync()
        self.changed.emit()
        self.status.setText("Random theme applied.")

    def _export(self):
        path, _ = QFileDialog.getSaveFileName(self, "Export Map Theme", "map_theme.wglv", "Map Theme (*.wglv)")
        if not path:
            return
        if not path.endswith(".wglv"):
            path += ".wglv"
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(look_to_dict(self.look), fh, indent=2)
            self.status.setText(f"Saved {path}.")
        except OSError as ex:
            self.status.setText(f"Could not save: {ex}")

    def _import(self):
        path, _ = QFileDialog.getOpenFileName(self, "Import Map Theme", "", "Map Theme (*.wglv)")
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as fh:
                new = look_from_dict(json.load(fh))
        except (OSError, ValueError) as ex:
            self.status.setText(f"Could not load: {ex}")
            return
        replace_look(self.look, new)
        self.sync()
        self.changed.emit()
        self.status.setText("Theme loaded.")


class MainWindow(QMainWindow):
    TAB_GENERATE, TAB_EDIT, TAB_THEME, TAB_WALK = range(4)

    def __init__(self):
        super().__init__()
        screen = QApplication.primaryScreen().availableGeometry()
        w = min(1280, screen.width() - 40)
        h = min(820, screen.height() - 60)
        self.setMinimumSize(640, 480)
        self.resize(w, h)
        self.move(screen.left() + (screen.width() - w) // 2, screen.top() + 20)
        self.settings = Settings(seed=random.randint(1, 99999))
        self.look = Look()
        self.editor = None
        self.path = None
        self.thread = None
        self.pending = None
        self.preview_key = None
        self.preview_img = None

        self.tabs = QTabWidget()
        self.setCentralWidget(self.tabs)
        self._build_generate()
        self._build_edit()
        self._build_theme()
        self._build_walk()
        self.tabs.currentChanged.connect(self._on_tab)
        self._build_menu()
        self._set_world_tabs(False)
        self._size_changed()
        self._refresh_title()

    # ----- layout -----
    def _build_generate(self):
        w = QWidget()
        row = QHBoxLayout(w)
        side = QVBoxLayout()

        box = QGroupBox("World")
        form = QFormLayout(box)
        self.seed = QSpinBox()
        self.seed.setRange(0, 2147483647)
        self.seed.setValue(self.settings.seed)
        dice = QPushButton("Random")
        dice.clicked.connect(lambda: (self.seed.setValue(random.randint(0, 2147483647)), self._schedule_preview()))
        seed_row = QHBoxLayout()
        seed_row.addWidget(self.seed, 1)
        seed_row.addWidget(dice)
        self.seed.valueChanged.connect(self._schedule_preview)
        self.method = QComboBox()
        for key, label in METHODS:
            self.method.addItem(label, key)
        self.method.currentIndexChanged.connect(self._schedule_preview)
        self.width_box = QSpinBox()
        self.height_box = QSpinBox()
        for sp in (self.width_box, self.height_box):
            sp.setRange(8, 51200)
            sp.setValue(5120)
            sp.valueChanged.connect(lambda *_: self._size_changed())
        form.addRow("Seed", self._wrap(seed_row))
        form.addRow("Algorithm", self.method)
        form.addRow("Width (tiles)", self.width_box)
        form.addRow("Height (tiles)", self.height_box)
        self.size_note = QLabel()
        self.size_note.setWordWrap(True)
        self.size_note.setStyleSheet("color:#8fa3b8;")
        form.addRow(self.size_note)
        side.addWidget(box)

        shape = QGroupBox("Shape")
        sf = QFormLayout(shape)
        sf.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapAllRows)
        self.sliders = {}
        for key, label, lo, hi, val, dec in (
            ("water", "Water coverage", 0.05, 0.95, 0.42, 2),
            ("scale", "Feature size", 0.4, 3.0, 1.0, 2),
            ("relief", "Relief", 0.25, 2.0, 1.2, 2),
            ("erosion", "Erosion", 0.0, 1.0, 0.6, 2),
            ("lakes", "Lakes", 0.0, 1.0, 0.3, 2),
            ("detail", "Fine detail", 0.0, 1.0, 0.7, 2),
            ("vegetation", "Vegetation", 0.0, 1.0, 0.7, 2),
            ("lowlands", "Lowland bias", 0.0, 1.0, 0.3, 2),
        ):
            s = NumberSlider(lo, hi, val, dec)
            s.changed.connect(self._schedule_preview)
            self.sliders[key] = s
            sf.addRow(label, s)
        side.addWidget(shape)

        self.random_theme = QCheckBox("Random Map Theme for each new world")
        side.addWidget(self.random_theme)
        side.addStretch(1)

        self.gen_btn = QPushButton("Generate world")
        self.gen_btn.setObjectName("primary")
        self.gen_btn.clicked.connect(self._generate)
        self.edit_btn = QPushButton("Edit map →")
        self.edit_btn.clicked.connect(lambda: self.tabs.setCurrentIndex(self.TAB_EDIT))
        self.progress = QProgressBar()
        self.progress.setVisible(False)
        self.gen_status = QLabel("Preview shows the world at a small size.")
        self.gen_status.setStyleSheet("color:#8fa3b8;")
        self.gen_status.setWordWrap(True)
        btns = QHBoxLayout()
        btns.addWidget(self.gen_btn, 1)
        btns.addWidget(self.edit_btn)
        side.insertLayout(0, btns)
        side.insertWidget(1, self.progress)
        side.insertWidget(2, self.gen_status)

        row.addWidget(self._scroll_side(side, 330))
        self.preview_label = QLabel()
        self.preview_label.setMinimumSize(160, 160)
        self.preview_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview_label.setStyleSheet("background:#05070a; border:1px solid #1d2a38; border-radius:8px;")
        self.preview_label.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        row.addWidget(self.preview_label, 1)
        self.tabs.addTab(w, "Generate")

    def _scroll_side(self, side, width):
        """A side panel at a fixed width that scrolls vertically when the window is short."""
        panel = QWidget()
        panel.setLayout(side)
        panel.setFixedWidth(width)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(panel)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setFixedWidth(width + 18)
        scroll.setFrameShape(QScrollArea.Shape.NoFrame)
        return scroll

    def _wrap(self, layout):
        w = QWidget()
        w.setLayout(layout)
        layout.setContentsMargins(0, 0, 0, 0)
        return w

    def _build_edit(self):
        w = QWidget()
        row = QHBoxLayout(w)
        side = QVBoxLayout()
        box = QGroupBox("Tool")
        bl = QVBoxLayout(box)
        self.op_group = QButtonGroup(self)
        self.op_buttons = {}
        for key, label in EDIT_OPS:
            rb = QRadioButton(label)
            self.op_group.addButton(rb)
            self.op_buttons[key] = rb
            bl.addWidget(rb)
        self.op_buttons["raise"].setChecked(True)
        self.biome_box = QComboBox()
        for i, (name, _) in enumerate(BIOMES):
            self.biome_box.addItem(name, i)
        self.biome_box.setCurrentIndex(5)
        bl.addWidget(self.biome_box)
        side.addWidget(box)

        brush = QGroupBox("Brush")
        bf = QFormLayout(brush)
        self.size_box = QSpinBox()
        self.size_box.setRange(1, 40)
        self.size_box.setValue(5)
        self.shape_box = QComboBox()
        for key, label in SHAPES:
            self.shape_box.addItem(label, key)
        self.strength_box = QSpinBox()
        self.strength_box.setRange(5, 100)
        self.strength_box.setValue(50)
        self.strength_box.setSuffix(" %")
        bf.addRow("Size", self.size_box)
        bf.addRow("Shape", self.shape_box)
        bf.addRow("Strength", self.strength_box)
        side.addWidget(brush)

        view_row = QHBoxLayout()
        view_row.addWidget(QLabel("View"))
        self.view_box = QComboBox()
        for key, label in VIEW_MODES:
            self.view_box.addItem(label, key)
        self.view_box.currentIndexChanged.connect(lambda *_: self._redraw_map())
        view_row.addWidget(self.view_box, 1)
        side.addLayout(view_row)

        hist = QHBoxLayout()
        self.undo_btn = QPushButton("Undo")
        self.redo_btn = QPushButton("Redo")
        self.undo_btn.clicked.connect(self._undo)
        self.redo_btn.clicked.connect(self._redo)
        hist.addWidget(self.undo_btn)
        hist.addWidget(self.redo_btn)
        side.addLayout(hist)

        self.edit_status = QLabel("")
        self.edit_status.setStyleSheet("color:#8fa3b8;")
        self.edit_status.setWordWrap(True)
        side.addWidget(self.edit_status)
        self.hover_label = QLabel("Point at a tile to see it.")
        self.hover_label.setWordWrap(True)
        self.hover_label.setStyleSheet("color:#c4cfdc;")
        side.addWidget(self.hover_label)
        side.addStretch(1)
        walk_btn = QPushButton("Theme →")
        walk_btn.clicked.connect(lambda: self.tabs.setCurrentIndex(self.TAB_THEME))
        side.addWidget(walk_btn)

        row.addWidget(self._scroll_side(side, 300))

        self.canvas = MapCanvas()
        self.canvas.stroke_started.connect(self._stroke_begin)
        self.canvas.painted.connect(self._dab)
        self.canvas.stroke_ended.connect(self._stroke_end)
        self.canvas.hovered.connect(self._hover)
        self.size_box.valueChanged.connect(lambda v: setattr(self.canvas, "brush_r", float(v)))
        self.shape_box.currentIndexChanged.connect(lambda *_: setattr(self.canvas, "shape", self.shape_box.currentData()))
        row.addWidget(self.canvas, 1)
        self.tabs.addTab(w, "Edit")

    def _build_theme(self):
        w = QWidget()
        row = QHBoxLayout(w)
        self.theme_panel = ThemePanel(self.look)
        row.addWidget(self.theme_panel)
        self.theme_view = ViewWidget(self.look, interactive=False)
        row.addWidget(self.theme_view, 1)
        self.tabs.addTab(w, "Theme")

    def _build_walk(self):
        w = QWidget()
        col = QVBoxLayout(w)
        col.setContentsMargins(0, 0, 0, 0)
        self.walk_view = ViewWidget(self.look, interactive=True)
        col.addWidget(self.walk_view, 1)
        hint = QLabel("WASD move · mouse look (click, Esc releases) · Space jump · double-tap Space "
                      "or F toggles fly · Shift 4× speed · wheel changes speed")
        hint.setStyleSheet("color:#8fa3b8; padding:4px;")
        col.addWidget(hint)
        self.tabs.addTab(w, "Walk")

    def _build_menu(self):
        bar = self.menuBar()
        f = bar.addMenu("&File")
        self._act(f, "&New world", lambda: self.tabs.setCurrentIndex(self.TAB_GENERATE), "Ctrl+N")
        self._act(f, "&Import world…", self.import_world, "Ctrl+O")
        self._act(f, "&Export world…", self.export_world, "Ctrl+S")
        f.addSeparator()
        self._act(f, "&Quit", self.close, "Ctrl+Q")
        e = bar.addMenu("&Edit")
        self._act(e, "&Undo", self._undo, "Ctrl+Z")
        self._act(e, "&Redo", self._redo, "Ctrl+Shift+Z")
        g = bar.addMenu("&Go")
        for i, name in enumerate(("Generate", "Edit", "Theme", "Walk")):
            self._act(g, name, lambda _=False, k=i: self.tabs.setCurrentIndex(k), f"Ctrl+{i + 1}")

    def _act(self, menu, text, slot, shortcut=None):
        a = QAction(text, self)
        if shortcut:
            a.setShortcut(QKeySequence(shortcut))
        a.triggered.connect(lambda *_: slot())
        menu.addAction(a)

    # ----- generation and preview -----
    def _size_changed(self):
        w, h = self.width_box.value(), self.height_box.value()
        mb = w * h * 60 / 1e6   # walk mesh: positions, colours, normals and indices
        ram_gb = w * h * 110 / 1e9   # measured peak while building, about 110 bytes per tile
        note = (f"{w} × {h} = {w * h:,} tiles · about {ram_gb:,.1f} GB of RAM to build · "
                f"about {mb:,.0f} MB of graphics memory to walk it")
        if w * h > 4096 * 4096:
            note += ". Very large: building and walking will be slow and may run out of memory."
        self.size_note.setText(note)
        self._schedule_preview()

    def _current_settings(self):
        vals = {k: s.value() for k, s in self.sliders.items()}
        return Settings(method=self.method.currentData(), seed=self.seed.value(),
                        width=self.width_box.value(), height=self.height_box.value(), **vals)

    def _schedule_preview(self, *_):
        s = preview_settings(self._current_settings())
        key = json.dumps(s.to_dict(), sort_keys=True)
        if key == self.preview_key:
            return
        self.preview_key = key
        self._start_build(s, "preview")

    def _generate(self):
        s = self._current_settings()
        self.gen_btn.setEnabled(False)
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setFormat("Starting…")
        self.progress.setVisible(True)
        self.gen_status.setText(f"Building {s.width} × {s.height} tiles…")
        self._start_build(s, "full")

    def _build_progress(self, frac, text):
        done = int(round(clamp(frac, 0.0, 1.0) * 100))
        self.progress.setValue(done)
        self.progress.setFormat(f"{text} · {done}% done · {100 - done}% left")

    def _start_build(self, s, purpose):
        if self.thread is not None:
            if self.pending is None or not (self.pending[1] == "full" and purpose == "preview"):
                self.pending = (s, purpose)
            return
        self.thread = BuildThread(s, purpose)
        self.thread.done.connect(self._build_done)
        if purpose == "full":
            self.thread.progress.connect(self._build_progress)
        self.thread.start()

    def _build_done(self, purpose, s, world):
        self.thread = None
        if purpose == "full":
            self.progress.setVisible(False)
            self.gen_btn.setEnabled(True)
            if self.random_theme.isChecked():
                randomize_look(self.look)
                self.theme_panel.sync()
            self._adopt(s, world)
            self.gen_status.setText("World built. Edit it, theme it, then walk it.")
        else:
            self._show_preview(s, world)
        if self.pending is not None:
            nxt = self.pending
            self.pending = None
            self._start_build(*nxt)

    def _show_preview(self, s, world):
        img = render_map(world.height, world.biome, world.flora_kind, world.flora_amt, world.temp, world.moist, world.sea, "biome")
        h, w = img.shape[:2]
        self.preview_img = QImage(np.ascontiguousarray(img).tobytes(), w, h, 3 * w,
                                  QImage.Format.Format_RGB888).copy()
        self._fit_preview()

    def _fit_preview(self):
        if self.preview_img is None:
            return
        pm = QPixmap.fromImage(self.preview_img).scaled(
            self.preview_label.size(), Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation)
        self.preview_label.setPixmap(pm)

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self._fit_preview()

    # ----- world state -----
    def _adopt(self, s, world, path=None):
        self.settings = s
        self.editor = Editor(world)
        self.path = path
        self.canvas.set_image(self.editor.render(self.view_box.currentData() or "biome"))
        self._set_world_tabs(True)
        self.tabs.setCurrentIndex(self.TAB_EDIT)
        self._update_edit_status()
        self._refresh_title()

    def _set_world_tabs(self, on):
        for i in (self.TAB_EDIT, self.TAB_THEME, self.TAB_WALK):
            self.tabs.setTabEnabled(i, on)

    def _refresh_title(self):
        name = self.path.split("/")[-1] if self.path else f"world_{self.settings.seed}.wglb"
        mark = " *" if self.editor is not None and self.editor.unsaved() else ""
        self.setWindowTitle(f"Worldmaker — {name}{mark}" if self.editor is not None else "Worldmaker")

    def _update_edit_status(self):
        if self.editor is None:
            return
        H, W = self.editor.shape
        self.undo_btn.setEnabled(bool(self.editor.undo_stack))
        self.redo_btn.setEnabled(bool(self.editor.redo_stack))
        state = "unsaved changes" if self.editor.unsaved() else "saved"
        self.edit_status.setText(f"{W} × {H} tiles · {len(self.editor.undo_stack)} steps to undo · {state}")
        self._refresh_title()

    def _current_op(self):
        for key, rb in self.op_buttons.items():
            if rb.isChecked():
                return key
        return "raise"

    def _redraw_map(self):
        if self.editor is not None:
            self.canvas.set_image(self.editor.render(self.view_box.currentData() or "biome"))

    def _stroke_begin(self, gx, gy):
        if self.editor is None:
            return
        self.editor.begin_stroke()
        self._dab(gx, gy)

    def _dab(self, gx, gy):
        if self.editor is None or self.editor._stroke is None:
            return
        biome = self.biome_box.currentData() or 0
        if self.editor.dab(self._current_op(), gx, gy, self.size_box.value(),
                           self.shape_box.currentData(), self.strength_box.value() / 100.0, biome):
            self._redraw_map()

    def _stroke_end(self):
        if self.editor is None:
            return
        self.editor.end_stroke()
        self._redraw_map()
        self._update_edit_status()

    def _hover(self, gx, gy):
        if self.editor is not None:
            self.hover_label.setText(self.editor.describe(int(gx), int(gy)))

    def _undo(self):
        if self.editor is not None and self.editor.undo():
            self._redraw_map()
            self._update_edit_status()

    def _redo(self):
        if self.editor is not None and self.editor.redo():
            self._redraw_map()
            self._update_edit_status()

    def _on_tab(self, idx):
        if self.editor is None:
            return
        if idx == self.TAB_WALK:
            self.walk_view.sync(self.editor)
        elif idx == self.TAB_THEME:
            self.theme_view.sync(self.editor)
            self.theme_panel.sync()

    # ----- files -----
    def _confirm_discard(self):
        if self.editor is None or not self.editor.unsaved():
            return True
        ans = QMessageBox.question(self, "Unsaved edits", "The world has edits that have not been saved. Discard them?",
                                   QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                                   QMessageBox.StandardButton.No)
        return ans == QMessageBox.StandardButton.Yes

    def import_world(self, path=None):
        if not self._confirm_discard():
            return
        if not path:
            path, _ = QFileDialog.getOpenFileName(self, "Import world", "", "Worldmaker (*.wglb)")
        if not path:
            return
        try:
            s, world = load_world(path)
        except (OSError, ValueError, KeyError) as ex:
            QMessageBox.warning(self, "Could not import world", str(ex))
            return
        self._adopt(s, world, path)

    def export_world(self):
        if self.editor is None:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Export world", self.path or f"world_{self.settings.seed}.wglb",
                                              "Worldmaker (*.wglb)")
        if not path:
            return
        if not path.endswith(".wglb"):
            path += ".wglb"
        try:
            save_world(path, self.settings, self.editor)
        except OSError as ex:
            QMessageBox.warning(self, "Export failed", str(ex))
            return
        self.path = path
        self.editor.saved_version = self.editor.version
        self._update_edit_status()
        self.statusBar().showMessage(f"Saved {path}", 6000)

    def closeEvent(self, e):
        if not self._confirm_discard():
            e.ignore()
            return
        super().closeEvent(e)


def main():
    fmt = QSurfaceFormat()
    fmt.setVersion(2, 1)
    fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
    fmt.setDepthBufferSize(24)
    fmt.setSamples(4)
    QSurfaceFormat.setDefaultFormat(fmt)
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet(STYLE)
    win = MainWindow()
    win.show()
    if len(sys.argv) > 1:
        win.import_world(sys.argv[1])
    sys.exit(app.exec())


if __name__ == "__main__":
    main()