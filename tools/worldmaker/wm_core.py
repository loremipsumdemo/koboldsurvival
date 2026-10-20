"""Worldmaker core: one whole-world generator, the world file format and the map editor.

No chunks. A world is one grid of tiles. Erosion and weather run on a fixed coarse grid,
and the final tiles are sampled from it, so a big map stays fast.
"""

from __future__ import annotations

import copy
import json
import math
import random
from dataclasses import asdict, dataclass, field, fields

import numpy as np

MAGIC = "WGLB"
WORLD_VERSION = 6
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
    return fbm(x * 12, y * 12, S, 6)


def quantile_map(values):
    vals = np.sort(np.asarray(values, np.float64).ravel())
    ranks = np.linspace(0.0, 1.0, vals.size)
    return lambda v: np.interp(np.asarray(v, np.float64), vals, ranks)


def bilerp(G, FX, FY, dom):
    """Bilinear lookup in a grid covering the domain. FX, FY are domain positions."""
    GY, GX = G.shape
    ax, ay = dom
    gx = np.clip(FX / ax * GX - 0.5, 0, GX - 1)
    gy = np.clip(FY / ay * GY - 0.5, 0, GY - 1)
    i0 = np.floor(gx).astype(np.int64); j0 = np.floor(gy).astype(np.int64)
    i1 = np.minimum(i0 + 1, GX - 1); j1 = np.minimum(j0 + 1, GY - 1)
    tx = gx - i0; ty = gy - j0
    a = G[j0, i0]; b = G[j0, i1]; c = G[j1, i0]; d = G[j1, i1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


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
    width: int = 512
    height: int = 512
    water: float = 0.42
    scale: float = 1.0
    relief: float = 1.0
    erosion: float = 0.5
    lakes: float = 0.2
    detail: float = 0.5

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
    height: np.ndarray   # float32 (H, W), 0..1
    biome: np.ndarray    # uint8
    river: np.ndarray    # bool
    temp: np.ndarray     # float32, 0..1
    moist: np.ndarray    # float32, 0..1
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
    """Temperature and moisture from geography alone, plus the runoff that feeds rivers."""
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
def build_world(s: Settings, report=None) -> World:
    """Build the whole world in one pass. Shape comes from the noise, weather and erosion run
    on a coarse grid, and the final tiles are sampled from that grid plus fine detail.
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

    # mountains: ridged belts that rise inland from the shore
    ridge = ridged(FX * 5.0, FY * 5.0, S + 1904, 5)
    inland = smooth(sea + 0.02, sea + 0.30, E0)
    peak = np.maximum(ridge - 0.46, 0.0) / 0.54
    lifted = soft_unit(E0 + 0.25 * clamp(s.relief, 0.25, 2.0) * inland * peak)
    E = np.where(E0 >= sea, np.maximum(lifted, sea + 1e-4), E0)

    tell(0.12, "Working out the weather")
    weather = stage_weather(E, sea, S, dom, FX, FY)
    W = 0.5 + 0.5 * smooth(sea, sea + 0.2, E)
    tell(0.18, "Eroding valleys")
    Eg, fill = erode_hydro(
        E.copy(), sea, W, float(s.erosion), EROSION_ITERS, weather["runoff"],
        on_step=lambda i: tell(0.18 + 0.6 * i / EROSION_ITERS, "Eroding valleys"))
    tell(0.78, "Routing rivers and lakes")
    Eg = hist_match(settle(Eg, sea), E)
    land_c = Eg >= sea
    rec = receivers(Eg)
    A = accumulate(rec, Eg, weather["runoff"])
    lake_c = lake_mask(Eg, rec, A, land_c, float(s.lakes), fill)
    thr = float(np.quantile(A[land_c], 0.985)) if land_c.any() else 1.0
    elev_c = rank(Eg, land_c)

    # sample the final tiles from the coarse grid, adding fine detail from the raw noise
    tell(0.84, "Sampling the full map")
    FXf, FYf = grid(s.width, s.height, dom)
    raw_f = norm(base_noise(s.method, S, FXf * s.scale, FYf * s.scale))
    co = lambda G: bilerp(G, FXf, FYf, dom)
    h = np.clip(co(Eg) + RESIDUAL * 2 * s.detail * (raw_f - co(E0)), 0.0, 1.0)

    tell(0.94, "Classifying the ground")
    land = h >= sea
    gx = np.gradient(h, axis=1); gz = np.gradient(h, axis=0)
    slope = np.clip(np.hypot(gx, gz) * 68.0, 0.0, 1.0)
    temp = co(weather["temp"]); moist = co(weather["moist"])
    biome = classify_biome(co(elev_c), h, temp, moist, slope, sea).astype(np.uint8)
    flow = co(A)
    lake = (co(lake_c.astype(np.float64)) > 0.5) & land
    biome[lake] = FLOODPLAIN
    biome[(flow > thr) & land & (h < sea + 0.025) & ~lake] = DELTA_SILT
    river = (flow > thr) & land & ~lake
    tell(1.0, "Done")
    return World(h.astype(np.float32), biome, river, temp.astype(np.float32),
                 moist.astype(np.float32), sea)


def preview_settings(s: Settings) -> Settings:
    """The same world at a small size, for the live preview."""
    f = 192.0 / max(s.width, s.height)
    return Settings(**{**s.to_dict(), "width": max(16, round(s.width * f)),
                       "height": max(16, round(s.height * f))})


# ---------- shading and map colours ----------
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


def render_map(h, b, t, m, river, sea, mode):
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
        out = BIOME_RGB[b] * shading(h64)[..., None]
        water = np.array([30, 90, 140.0]) * (0.6 + 0.4 * np.clip(h64 / max(sea, 1e-6), 0, 1))[..., None]
        out = np.where((h64 < sea)[..., None], water, out)
        out[river] = (70, 150, 215)
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
        self.river = world.river
        self.sea = float(world.sea)
        self._set((world.height.copy(), world.biome.copy(), world.temp.copy(), world.moist.copy()))
        self.undo_stack, self.redo_stack = [], []
        self._stroke = None
        self._changed = False
        self.version = 0
        self.saved_version = 0

    @property
    def shape(self):
        return self.h.shape

    def state(self):
        return (self.h.copy(), self.b.copy(), self.t.copy(), self.m.copy())

    def _set(self, st):
        self.h, self.b, self.t, self.m = st

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

    def render(self, mode):
        return render_map(self.h, self.b, self.t, self.m, self.river, self.sea, mode)

    def describe(self, gx, gy):
        i, j = int(gx), int(gy)
        H, W = self.h.shape
        if not (0 <= i < W and 0 <= j < H):
            return ""
        river = ", river" if self.river[j, i] else ""
        return (f"x {i}, y {j} · {BIOMES[int(self.b[j, i])][0]} · "
                f"{self.h[j, i] - self.sea:+.3f} vs sea · temp {self.t[j, i]:.2f} · "
                f"moisture {self.m[j, i]:.2f}{river}")


# ---------- world file ----------
def save_world(path, s: Settings, ed: Editor):
    meta = {"magic": MAGIC, "version": WORLD_VERSION, "settings": s.to_dict(), "sea": ed.sea}
    with open(path, "wb") as f:
        np.savez_compressed(f, meta=np.array(json.dumps(meta)),
                            height=ed.h.astype(np.float32), biome=ed.b, river=ed.river,
                            temp=ed.t.astype(np.float32), moist=ed.m.astype(np.float32))


def load_world(path):
    try:
        d = np.load(path, allow_pickle=False)
        meta = json.loads(str(d["meta"]))
    except Exception as ex:
        raise ValueError("This is not a readable world file. Worlds saved by the old chunked "
                         "version cannot be opened; generate the world again.") from ex
    if meta.get("magic") != MAGIC or meta.get("version") != WORLD_VERSION:
        raise ValueError("Unsupported world file.")
    s = Settings.from_dict(meta["settings"])
    world = World(d["height"].astype(np.float32), d["biome"].astype(np.uint8),
                  d["river"].astype(bool), d["temp"].astype(np.float32),
                  d["moist"].astype(np.float32), float(meta["sea"]))
    return s, world


# ---------- Map Theme ----------
LOOK_FORMAT = "worldmaker-look"
LOOK_VERSION = 1


@dataclass
class Look:
    seed: int = 1
    clock: float = 12.0
    day_cycle: bool = False
    day_length: float = 120.0
    sun_azimuth: float = 135.0
    sun_elevation: float = 48.0
    sun_strength: float = 0.9
    sun_rgb: list = field(default_factory=lambda: [1.0, 0.96, 0.82])
    ambient: float = 0.35
    sky_top: list = field(default_factory=lambda: [0.22, 0.42, 0.80])
    sky_horizon: list = field(default_factory=lambda: [0.72, 0.84, 0.94])
    night_top: list = field(default_factory=lambda: [0.02, 0.03, 0.07])
    night_horizon: list = field(default_factory=lambda: [0.05, 0.07, 0.13])
    fog: float = 1.0
    rock_rgb: list = field(default_factory=lambda: [0.46, 0.43, 0.39])
    rock_slope: float = 0.45
    water_rgb: list = field(default_factory=lambda: [0.12, 0.32, 0.50])
    water_alpha: float = 0.6
    cloud_rgb: list = field(default_factory=lambda: [1.0, 1.0, 1.0])
    cloud_cover: float = 0.45
    cloud_opacity: float = 0.85
    cloud_height: float = 2.2
    cloud_speed: float = 0.6


# (kind, attribute, label, lo, hi, decimals) for sliders; the other kinds carry no range.
THEME_ITEMS = [
    ("heading", "Time"),
    ("time", "clock", "Time of day"),
    ("check", "day_cycle", "Run a day and night cycle"),
    ("slider", "day_length", "Seconds per day", 10, 1200, 0),
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
    ("colour", "water_rgb", "Water colour"),
    ("slider", "water_alpha", "Water opacity", 0, 1, 2),
    ("heading", "Clouds"),
    ("colour", "cloud_rgb", "Cloud colour"),
    ("slider", "cloud_cover", "Cover", 0, 1, 2),
    ("slider", "cloud_opacity", "Opacity", 0, 1, 2),
    ("slider", "cloud_height", "Height", 1.2, 3.8, 2),
    ("slider", "cloud_speed", "Speed", 0, 3, 2),
]


def replace_look(dst: Look, src: Look):
    for f in fields(Look):
        setattr(dst, f.name, copy.deepcopy(getattr(src, f.name)))


def randomize_look(look: Look, rng=random):
    base = Look()
    look.seed = rng.randint(0, 99999)
    for item in THEME_ITEMS:
        if item[0] == "slider":
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
