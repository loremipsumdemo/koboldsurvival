# pip install dearpygui trimesh numpy scipy pillow moderngl fast-simplification DearPyGui-DragAndDrop
"""
GLB Check - inspect and optimize .glb / .gltf models.

Open a model (button, Ctrl+O, drag-and-drop on Windows, or pass a path on the
command line) to get a lit, textured preview, key stats and a list of common
asset problems. The Optimize panel can then cut the polygon count, shrink and
re-encode textures, clean up geometry and fix units / pivot, with a side-by-side
before/after comparison and one-click save to a new .glb.

Requirements:
    pip install dearpygui trimesh numpy scipy pillow moderngl fast-simplification
    pip install DearPyGui-DragAndDrop      # optional, Windows drag-and-drop

Usage:
    python glbcheck.py [model.glb]
"""
import contextlib
import copy
import io
import json
import math
import os
import struct
import subprocess
import sys
import threading
import time

import numpy as np
import trimesh
import dearpygui.dearpygui as dpg

try:
    import moderngl
except ImportError:  # preview falls back to a placeholder
    moderngl = None

try:
    import fast_simplification
except ImportError:  # decimation disabled, other optimizations still work
    fast_simplification = None

try:
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
except ImportError:  # normals after decimation fall back to fully smooth
    connected_components = None

try:
    import DearPyGui_DragAndDrop as dpg_dnd
except ImportError:
    dpg_dnd = None

APP_NAME = "GLB Check"
SIDEBAR_W = 360
STATUS_H = 26
FOV_Y = math.radians(45.0)
WORLD_UP = np.array([0.0, 1.0, 0.0])
RENDER_MODES = ["Shaded", "Shaded + Wireframe", "Wireframe"]
DEFAULT_COLOR = (0.78, 0.78, 0.80, 1.0)
DEFAULT_YAW, DEFAULT_PITCH = 0.65, 0.38

COL_TEXT = (222, 224, 230)
COL_MUTED = (140, 146, 158)
COL_ACCENT = (120, 170, 255)
COL_GOOD = (120, 205, 130)
COL_BAD = (235, 120, 110)
LEVEL_COLORS = {"ok": COL_GOOD, "info": (150, 175, 215), "warn": (235, 185, 85)}
LEVEL_TAGS = {"ok": "OK  ", "info": "INFO", "warn": "WARN"}

# ---- optimizer options ----
TEX_SIZES = ["Keep", "4096", "2048", "1024", "512", "256"]
TEX_FORMATS = ["Keep", "JPEG (opaque only)", "PNG"]
SCALES = {
    "No change": 1.0,
    "cm -> m  (x0.01)": 0.01,
    "mm -> m  (x0.001)": 0.001,
    "inches -> m  (x0.0254)": 0.0254,
    "m -> cm  (x100)": 100.0,
}
PRESETS = {
    "Custom": None,
    "Cleanup only": dict(pct=100, tex="Keep", pot=False, fmt="Keep"),
    "Desktop / VR": dict(target=300_000, tex="2048", pot=True, fmt="Keep"),
    "Web / mobile": dict(target=60_000, tex="1024", pot=True, fmt="JPEG (opaque only)"),
    "Aggressive": dict(target=15_000, tex="512", pot=True, fmt="JPEG (opaque only)"),
}
TEX_ATTRS = ("baseColorTexture", "metallicRoughnessTexture", "normalTexture",
             "occlusionTexture", "emissiveTexture", "image")
JPEG_QUALITY = 90
INPUT_TAGS = ("opt_target", "opt_min_tris")

STATE = {
    "renderer": None,
    "renderer_error": None,
    # camera
    "focus": np.zeros(3),
    "distance": 4.0,
    "yaw": DEFAULT_YAW,
    "pitch": DEFAULT_PITCH,
    "fit_radius": 1.0,
    "fit_center": np.zeros(3),
    "bounds": None,
    # view options
    "render_mode": RENDER_MODES[0],
    "show_grid": True,
    "show_back": False,
    "grid_half": 2.0,
    "grid_step": 0.5,
    # preview texture plumbing
    "preview_size": (0, 0),
    "preview_tex": None,
    "preview_img": None,
    "tex_counter": 0,
    "delete_queue": [],
    "frame": 0,
    "dirty": True,
    "last_frame": None,
    # input
    "drag": None,  # None | "orbit" | "pan"
    "last_mouse": (0.0, 0.0),
    "dialog_open": False,
    # model
    "path": None,
    "scene": None,          # original trimesh scene, input for the optimizer
    "versions": {},         # "Original"/"Optimized" -> prepared data
    "view": "Original",
    "report": None,         # report of the version being shown
    "loading": False,
    "load_result": None,
    # optimizer
    "opt_running": False,
    "opt_progress": "",
    "opt_result": None,
    "opt_bytes": None,
    "opt_opts": None,
    "saved_path": None,
}
LOCK = threading.Lock()


def _keys(*names):
    """Key constants differ between DearPyGui 1.x and 2.x; collect what exists."""
    return [getattr(dpg, n) for n in names if hasattr(dpg, n)]


CTRL_KEYS = _keys("mvKey_LControl", "mvKey_RControl", "mvKey_Control", "mvKey_ModCtrl")
SHIFT_KEYS = _keys("mvKey_LShift", "mvKey_RShift", "mvKey_Shift", "mvKey_ModShift")


def any_down(keys):
    return any(dpg.is_key_down(k) for k in keys)


def fmt_len(v):
    return f"{v:.4g} m"


def fmt_mb(b):
    return f"{b / 1024 / 1024:.2f} MB"


# =====================================================================
# glTF JSON (for things trimesh doesn't report: animations, skins...)
# =====================================================================
def _gltf_json_from_bytes(data):
    if data[:4] == b"glTF":
        length, ctype = struct.unpack_from("<I4s", data, 12)
        if ctype != b"JSON":
            return {}
        return json.loads(data[20:20 + length].decode("utf-8"))
    return json.loads(data.decode("utf-8-sig"))


def read_gltf_json(path):
    try:
        with open(path, "rb") as f:
            head = f.read(20)
            if head[:4] == b"glTF":
                length = struct.unpack_from("<I", head, 12)[0]
                return _gltf_json_from_bytes(head + f.read(length))
            return _gltf_json_from_bytes(head + f.read())
    except Exception:
        return {}


def model_size(path, js):
    """File size, including the .bin and image files a .gltf refers to."""
    total = os.path.getsize(path)
    if path.lower().endswith(".gltf") and isinstance(js, dict):
        from urllib.parse import unquote
        seen = set()
        for item in js.get("buffers", []) + js.get("images", []):
            uri = item.get("uri") if isinstance(item, dict) else None
            if not uri or uri.startswith("data:"):
                continue
            f = os.path.normpath(os.path.join(os.path.dirname(path), unquote(uri)))
            if f not in seen and os.path.isfile(f):
                seen.add(f)
                total += os.path.getsize(f)
    return total


def gltf_info(js):
    meshes = js.get("meshes", []) if isinstance(js, dict) else []
    return {
        "animations": len(js.get("animations", [])) if js else 0,
        "skins": len(js.get("skins", [])) if js else 0,
        "morphs": sum(1 for m in meshes for p in m.get("primitives", []) if p.get("targets")),
        "extensions": sorted(set(js.get("extensionsUsed", []))) if js else [],
        "generator": (js.get("asset", {}) or {}).get("generator", "") if js else "",
    }


# =====================================================================
# Loading & analysis (runs on a worker thread; no GL or UI calls here)
# =====================================================================
def _color_factor(value, default=(1.0, 1.0, 1.0, 1.0)):
    if value is None:
        return np.array(default, dtype="f4")
    c = np.asarray(value).ravel()
    if c.size not in (3, 4):
        return np.array(default, dtype="f4")
    as_int = np.issubdtype(c.dtype, np.integer) or c.max() > 1.0
    c = c.astype(np.float64) / (255.0 if as_int else 1.0)
    if c.size == 3:
        c = np.append(c, 1.0)
    return c.astype("f4")


def _material_texture(mat):
    if mat is None:
        return None
    for attr in ("baseColorTexture", "image"):
        img = getattr(mat, attr, None)
        if img is not None:
            return img
    return None


def _material_base(mat, has_image):
    if mat is None:
        return np.array(DEFAULT_COLOR, dtype="f4")
    if hasattr(mat, "baseColorFactor"):
        return _color_factor(mat.baseColorFactor)
    if has_image:  # SimpleMaterial: the texture already carries the color
        return np.ones(4, dtype="f4")
    return _color_factor(getattr(mat, "main_color", None), DEFAULT_COLOR)


def _image_to_rgba(img):
    """PIL image -> bottom-up RGBA bytes ready for a GL texture."""
    try:
        from PIL import Image
        flip = getattr(getattr(Image, "Transpose", Image), "FLIP_TOP_BOTTOM")
        im = img.convert("RGBA").transpose(flip)
        return {"size": im.size, "data": im.tobytes()}
    except Exception:
        return None


def _all_images(mat):
    out = []
    for attr in TEX_ATTRS:
        img = getattr(mat, attr, None) if mat is not None else None
        if img is not None and hasattr(img, "size"):
            out.append(img)
    return out


def prepare_scene(scene, name, folder, file_bytes, info):
    """Turn a trimesh Scene into GPU-ready arrays plus a stats/checks report."""
    geoms, images = {}, {}
    mat_keys, all_tex = set(), {}
    skipped = no_uv = degenerate = verts = tris = 0

    for gname, g in scene.geometry.items():
        if not isinstance(g, trimesh.Trimesh) or len(g.faces) == 0:
            skipped += 1
            continue
        nv = len(g.vertices)
        uv = np.zeros((nv, 2), dtype="f4")
        col = np.ones((nv, 4), dtype="f4")
        base = np.array(DEFAULT_COLOR, dtype="f4")
        img_key = None
        vis = g.visual

        if isinstance(vis, trimesh.visual.TextureVisuals):
            mat = vis.material
            if mat is not None:
                mat_keys.add(id(mat))
                for im in _all_images(mat):
                    all_tex[id(im)] = tuple(im.size)
            has_uv = vis.uv is not None and len(vis.uv) == nv
            if has_uv:
                uv = np.asarray(vis.uv, dtype="f4")
            else:
                no_uv += 1
            img = _material_texture(mat)
            if img is not None and has_uv:
                key = id(img)
                if key not in images:
                    images[key] = _image_to_rgba(img)
                if images[key] is not None:
                    img_key = key
            base = _material_base(mat, img_key is not None)
        elif getattr(vis, "kind", None) in ("vertex", "face"):
            col = np.asarray(vis.vertex_colors, dtype="f4")[:, :4] / 255.0
            base = np.ones(4, dtype="f4")

        scale = max(float(g.scale), 1e-12)
        degenerate += int(np.count_nonzero(g.area_faces <= (scale * 1e-7) ** 2))
        verts += nv
        tris += len(g.faces)

        data = np.hstack([
            np.asarray(g.vertices, dtype="f4"),
            np.asarray(g.vertex_normals, dtype="f4"),
            uv, col,
        ]).astype("f4")
        geoms[gname] = {
            "data": np.ascontiguousarray(data),
            "faces": np.ascontiguousarray(g.faces, dtype="u4"),
            "base": base,
            "image": img_key,
            "tris": len(g.faces),
        }

    instances = []
    for node in scene.graph.nodes_geometry:
        transform, gname = scene.graph[node]
        if gname in geoms:
            instances.append((gname, np.asarray(transform, dtype=np.float64)))

    if not instances:
        raise ValueError("The file contains no triangle meshes to display.")

    bounds = scene.bounds
    bounds = np.asarray(bounds) if bounds is not None else np.array([[-1, 0, -1], [1, 1, 1.0]])
    dims = bounds[1] - bounds[0]
    tex_sizes = list(all_tex.values())
    npot = sum(1 for w, h in tex_sizes if (w & (w - 1)) or (h & (h - 1)))

    report = {
        "name": name,
        "dir": folder,
        "file_bytes": file_bytes,
        "meshes": len(geoms),
        "instances": len(instances),
        "verts": verts,
        "tris": tris,
        "tris_rendered": sum(geoms[g]["tris"] for g, _ in instances),
        "materials": len(mat_keys),
        "textures": len(tex_sizes),
        "tex_pixels": sum(w * h for w, h in tex_sizes),
        "max_tex": max((max(s) for s in tex_sizes), default=0),
        "max_tex_size": max(tex_sizes, key=lambda s: s[0] * s[1], default=None),
        "npot_textures": npot,
        "no_uv": no_uv,
        "degenerate": degenerate,
        "skipped": skipped,
        "bounds": bounds,
        "dims": dims,
        "max_dim": float(np.max(dims)),
        "info": info,
    }
    report["checks"] = run_checks(report)
    return {"geoms": geoms, "images": images, "instances": instances, "report": report}


def run_checks(r):
    c = []
    tris = r["tris_rendered"]
    if tris > 1_000_000:
        c.append(("warn", f"Heavy: {tris:,} triangles rendered. Use Optimize to decimate."))
    elif tris > 150_000:
        c.append(("info", f"{tris:,} triangles: fine on desktop, heavy for mobile/web."))
    if r["max_tex"] > 4096:
        c.append(("warn", f"Texture up to {r['max_tex']}px. Most real-time targets cap at 4096."))
    elif r["max_tex"] > 2048:
        c.append(("info", f"Texture up to {r['max_tex']}px. Consider 2048 for web/mobile."))
    if r["npot_textures"]:
        c.append(("info", f"{r['npot_textures']} texture(s) are not power-of-two sized."))
    if r["no_uv"]:
        c.append(("info", f"{r['no_uv']} mesh(es) have no UV coordinates."))
    if r["degenerate"]:
        c.append(("warn", f"{r['degenerate']:,} degenerate (zero-area) triangles."))
    if r["skipped"]:
        c.append(("info", f"{r['skipped']} non-mesh geometry item(s) (points/lines) not shown."))

    size = max(r["max_dim"], 1e-12)
    if size > 500:
        c.append(("warn", f"Largest dimension is {fmt_len(size)}. glTF is in meters; "
                          "this may have been exported in cm or mm."))
    elif size < 0.01:
        c.append(("warn", f"Model is tiny ({fmt_len(size)}). Check export units/scale."))

    lo, hi = r["bounds"]
    if lo[1] < -0.02 * size:
        c.append(("info", f"Extends below the ground plane (min Y = {fmt_len(lo[1])})."))
    elif lo[1] > 0.02 * size:
        c.append(("info", f"Floats above the ground plane (min Y = {fmt_len(lo[1])})."))
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    footprint = max(hi[0] - lo[0], hi[2] - lo[2], 1e-12)
    if math.hypot(cx, cz) > 0.5 * footprint:
        c.append(("info", f"Not centered on the origin (XZ offset {fmt_len(math.hypot(cx, cz))})."))

    if r["instances"] > 500:
        c.append(("info", f"{r['instances']:,} mesh instances: many draw calls."))
    if r["materials"] > 32:
        c.append(("info", f"{r['materials']} materials: consider merging/atlasing."))
    if r["file_bytes"] > 50 * 1024 * 1024:
        c.append(("warn", f"Large file ({fmt_mb(r['file_bytes'])}). Use Optimize to shrink "
                          "textures and geometry."))
    info = r.get("info") or {}
    kept = [f"{info[k]} {label}" for k, label in
            (("animations", "animation(s)"), ("skins", "skin(s)"), ("morphs", "morph-target mesh(es)"))
            if info.get(k)]
    if kept:
        c.append(("info", "Has " + ", ".join(kept) + ". Shown static here; an optimized "
                  "copy will not keep them."))
    if not c:
        c.append(("ok", "No issues found."))
    return c


def _load_worker(path):
    t0 = time.perf_counter()
    js = read_gltf_json(path)
    info = gltf_info(js)
    try:
        scene = trimesh.load(path, force="scene")
        prepared = prepare_scene(scene, os.path.basename(path), os.path.dirname(path),
                                 model_size(path, js), info)
        prepared["report"]["load_s"] = time.perf_counter() - t0
        result = ("ok", path, (scene, prepared))
    except Exception as e:  # report anything to the status bar
        if "KHR_draco_mesh_compression" in info["extensions"]:
            e = RuntimeError("Draco-compressed meshes aren't supported. Decompress it first "
                             "(e.g. gltf-transform copy in.glb out.glb).")
        result = ("err", path, e)
    with LOCK:
        STATE["load_result"] = result


# =====================================================================
# Optimizer (worker thread)
# =====================================================================
def _extract(g):
    """Trimesh -> plain arrays we can edit without trimesh re-processing them."""
    d = {
        "pos": np.asarray(g.vertices, dtype=np.float64),
        "nrm": np.asarray(g.vertex_normals, dtype=np.float64),
        "faces": np.asarray(g.faces, dtype=np.int64),
        "uv": None, "col": None, "material": None, "textured": False,
    }
    vis = g.visual
    if isinstance(vis, trimesh.visual.TextureVisuals):
        d["textured"] = True
        d["material"] = vis.material
        if vis.uv is not None and len(vis.uv) == len(d["pos"]):
            d["uv"] = np.asarray(vis.uv, dtype=np.float64)
    elif getattr(vis, "kind", None) in ("vertex", "face"):
        d["col"] = np.asarray(vis.vertex_colors, dtype=np.uint8)
    return d


def _take(d, idx, faces):
    out = dict(d)
    out["pos"], out["nrm"], out["faces"] = d["pos"][idx], d["nrm"][idx], faces
    if d["uv"] is not None:
        out["uv"] = d["uv"][idx]
    if d["col"] is not None:
        out["col"] = d["col"][idx]
    return out


def _weld(d, with_normals):
    """Merge vertices that share position (+UV, +color, optionally +normal)."""
    if len(d["pos"]) == 0:
        return d
    ext = float(np.ptp(d["pos"], axis=0).max()) or 1.0
    parts = [np.round(d["pos"] / (ext * 1e-6))]
    if with_normals:
        parts.append(np.round(d["nrm"] * 1e3))
    if d["uv"] is not None:
        parts.append(np.round(d["uv"] * 1e5))
    if d["col"] is not None:
        parts.append(d["col"].astype(np.float64))
    key = np.hstack(parts).astype(np.int64)
    _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    if len(first) == len(d["pos"]):
        return d
    return _take(d, first, inv.ravel()[d["faces"]])


def _clean(d):
    """Drop degenerate/duplicate triangles and unused vertices."""
    f = d["faces"]
    if len(f) == 0:
        return d
    ok = (f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 0] != f[:, 2])
    p = d["pos"]
    ext = float(np.ptp(p, axis=0).max()) or 1.0
    area2 = np.linalg.norm(np.cross(p[f[:, 1]] - p[f[:, 0]], p[f[:, 2]] - p[f[:, 0]]), axis=1)
    ok &= area2 > (ext * 1e-7) ** 2
    f = f[ok]
    if len(f):
        # same triangle with the same winding twice (rotate so lowest index is first);
        # back-to-back pairs with opposite winding are deliberate and kept
        r = np.argmin(f, axis=1)
        canon = np.stack([f[np.arange(len(f)), (r + k) % 3] for k in range(3)], axis=1)
        _, keep = np.unique(canon, axis=0, return_index=True)
        f = f[np.sort(keep)]
    used = np.unique(f)
    remap = np.full(len(p), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return _take(d, used, remap[f])


def _smooth_normals(d, angle_deg):
    """Recompute normals, splitting vertices at edges sharper than angle_deg."""
    p, f = d["pos"], d["faces"]
    fn = np.cross(p[f[:, 1]] - p[f[:, 0]], p[f[:, 2]] - p[f[:, 0]])  # area weighted
    labels = np.zeros(len(f), dtype=np.int64)
    if connected_components is not None and angle_deg < 180 and len(f) > 1:
        unit = fn / np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-30)
        adj = trimesh.graph.face_adjacency(faces=f)
        if len(adj):
            smooth = (unit[adj[:, 0]] * unit[adj[:, 1]]).sum(axis=1) > math.cos(math.radians(angle_deg))
            a = adj[smooth]
            graph = coo_matrix((np.ones(len(a)), (a[:, 0], a[:, 1])), shape=(len(f), len(f)))
            _, labels = connected_components(graph, directed=False)
    corner_v = f.ravel()
    key = corner_v * (int(labels.max()) + 1) + np.repeat(labels, 3)
    _, first, inv = np.unique(key, return_index=True, return_inverse=True)
    inv = inv.ravel()
    corner_n = np.repeat(fn, 3, axis=0)
    nrm = np.stack([np.bincount(inv, weights=corner_n[:, k], minlength=len(first))
                    for k in range(3)], axis=1)
    lens = np.linalg.norm(nrm, axis=1, keepdims=True)
    nrm = np.where(lens > 1e-30, nrm / np.maximum(lens, 1e-30), np.array([0.0, 1.0, 0.0]))
    out = _take(d, corner_v[first], inv.reshape(-1, 3))
    out["nrm"] = nrm
    return out


def _decimate(d, target, preserve_border, smooth_angle):
    if len(d["faces"]) <= target:
        return d
    work = _weld(d, with_normals=False)  # flat-shaded meshes need shared vertices
    if len(work["pos"]) < 0.5 * len(d["pos"]):
        smooth_angle = 0  # mesh was flat shaded (low-poly look): keep it faceted
    pts = np.ascontiguousarray(work["pos"], dtype=np.float64)
    tri = np.ascontiguousarray(work["faces"], dtype=np.int32)
    kw = {"target_count": int(target), "return_collapses": True}
    try:
        out = fast_simplification.simplify(pts, tri, preserve_border=preserve_border, **kw)
    except TypeError:  # older fast-simplification without preserve_border
        out = fast_simplification.simplify(pts, tri, **kw)
    collapses = out[2]
    if len(collapses) == 0:
        return d
    new_pts, new_tri, mapping = fast_simplification.replay_simplification(pts, tri, collapses)
    new_pts = np.asarray(new_pts, dtype=np.float64)
    new_tri = np.asarray(new_tri, dtype=np.int64).reshape(-1, 3)
    mapping = np.asarray(mapping, dtype=np.int64).ravel()

    # each new vertex takes UV/color from the closest original vertex merged into it
    valid = np.flatnonzero((mapping >= 0) & (mapping < len(new_pts)))
    dist = np.linalg.norm(pts[valid] - new_pts[mapping[valid]], axis=1)
    order = np.lexsort((dist, mapping[valid]))
    grp = mapping[valid][order]
    first = np.ones(len(grp), dtype=bool)
    first[1:] = grp[1:] != grp[:-1]
    rep = np.zeros(len(new_pts), dtype=np.int64)
    rep[grp[first]] = valid[order][first]

    out = _take(work, rep, new_tri)
    out["pos"] = new_pts
    out = _clean(out)
    return _smooth_normals(out, smooth_angle)


def _pow2(n):
    return 1 << max(0, int(round(math.log2(max(n, 1)))))


def _floor_pow2(n):
    return 1 << max(0, int(math.floor(math.log2(max(n, 1)))))


def _with_jpeg_quality(im, quality):
    """trimesh re-saves JPEG textures with PIL defaults; pin a better quality."""
    orig_save = im.save

    def save(fp, format=None, **params):
        if str(format).upper() in ("JPEG", "JPG"):
            params.setdefault("quality", quality)
        return orig_save(fp, format=format, **params)

    with contextlib.suppress(Exception):
        im.save = save
    return im


def _process_image(img, opts):
    from PIL import Image
    w, h = img.size
    nw, nh = w, h
    max_size = opts["tex_max"]
    if max_size and max(w, h) > max_size:
        s = max_size / max(w, h)
        nw, nh = max(1, round(w * s)), max(1, round(h * s))
    if opts["tex_pot"]:
        nw, nh = _pow2(nw), _pow2(nh)
        if max_size:
            nw, nh = min(nw, _floor_pow2(max_size)), min(nh, _floor_pow2(max_size))
    resized = (nw, nh) != (w, h)

    src_fmt = getattr(img, "format", None)
    fmt = opts["tex_fmt"]
    want = "JPEG" if (fmt.startswith("JPEG") or (fmt == "Keep" and src_fmt == "JPEG")) else "PNG"
    if want == "JPEG" and img.mode in ("RGBA", "LA", "PA", "P"):
        alpha = img.convert("RGBA").getchannel("A")
        if alpha.getextrema()[0] < 255:
            want = "PNG"  # has real transparency

    if not resized and want == src_fmt:
        return _with_jpeg_quality(img, JPEG_QUALITY) if want == "JPEG" else img

    resample = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
    out = img.resize((nw, nh), resample) if resized else img
    if want == "JPEG":
        buf = io.BytesIO()
        out.convert("RGB").save(buf, format="JPEG", quality=JPEG_QUALITY)
        buf.seek(0)
        out = Image.open(buf)
        out.load()
        return _with_jpeg_quality(out, JPEG_QUALITY)
    out = out.copy() if out is img else out  # drops .format -> exported as PNG
    if out.mode not in ("RGB", "RGBA", "L", "LA"):
        out = out.convert("RGBA")
    return out


def _new_material(mat, img_cache, mat_cache, opts):
    if mat is None:
        return None
    if id(mat) in mat_cache:
        return mat_cache[id(mat)]
    new = copy.copy(mat)
    if isinstance(getattr(mat, "_data", None), dict):
        new._data = dict(mat._data)  # PBRMaterial keeps its fields in a dict
    tex_changes = opts["tex_max"] or opts["tex_pot"] or opts["tex_fmt"] != "Keep"
    for attr in TEX_ATTRS:
        img = getattr(mat, attr, None)
        if img is None or not hasattr(img, "size"):
            continue
        if id(img) not in img_cache:
            img_cache[id(img)] = _process_image(img, opts) if tex_changes else \
                (_with_jpeg_quality(img, JPEG_QUALITY) if getattr(img, "format", None) == "JPEG" else img)
        with contextlib.suppress(Exception):
            setattr(new, attr, img_cache[id(img)])
    mat_cache[id(mat)] = new
    return new


def _build(d, material):
    if d["textured"]:
        visual = trimesh.visual.TextureVisuals(uv=d["uv"], material=material)
    elif d["col"] is not None:
        visual = trimesh.visual.ColorVisuals(vertex_colors=d["col"])
    else:
        visual = None
    return trimesh.Trimesh(vertices=d["pos"], faces=d["faces"], vertex_normals=d["nrm"],
                           visual=visual, process=False)


def optimize_scene(scene, opts, progress):
    log = []
    names = [n for n, g in scene.geometry.items()
             if isinstance(g, trimesh.Trimesh) and len(g.faces)]
    uses = {}
    for node in scene.graph.nodes_geometry:
        uses[scene.graph[node][1]] = uses.get(scene.graph[node][1], 0) + 1

    progress("Preparing geometry...")
    data = {}
    for n in names:
        d = _extract(scene.geometry[n])
        if opts["clean"]:
            d = _clean(_weld(d, with_normals=True))
        data[n] = d

    # one ratio for every mesh large enough to decimate, so the total hits the target
    if opts["ratio"] < 0.999 and fast_simplification is not None:
        total = sum(len(data[n]["faces"]) * uses.get(n, 0) for n in names)
        small = sum(len(data[n]["faces"]) * uses.get(n, 0) for n in names
                    if len(data[n]["faces"]) < opts["min_tris"])
        target_total = opts["ratio"] * opts["orig_tris"]
        big = total - small
        ratio = 1.0 if big <= 0 else min(1.0, max(0.0005, (target_total - small) / big))
        if ratio < 0.999:
            todo = [n for n in names if len(data[n]["faces"]) >= opts["min_tris"]]
            for i, n in enumerate(todo):
                progress(f"Decimating mesh {i + 1}/{len(todo)}...")
                t = max(4, int(len(data[n]["faces"]) * ratio))
                try:
                    data[n] = _decimate(data[n], t, opts["borders"], opts["smooth_angle"])
                except Exception as e:
                    log.append(f"Mesh '{n}' could not be decimated ({e}); kept as is.")
            reached = sum(len(data[n]["faces"]) * uses.get(n, 0) for n in names)
            if reached > target_total * 1.1 and small <= target_total:
                hint = (" 'Protect edges' is holding UV seams and open borders in place; "
                        "turning it off goes further but can tear textures or open holes."
                        if opts["borders"] else "")
                log.append(f"Reached {reached:,} of the {int(target_total):,} triangle target.{hint}")
        if small > target_total:
            log.append(f"Meshes under {opts['min_tris']} triangles alone exceed the target; "
                       "lower that limit to go further.")
    elif opts["ratio"] < 0.999:
        log.append("Polygon reduction skipped: pip install fast-simplification")

    progress("Processing textures...")
    img_cache, mat_cache = {}, {}
    new = trimesh.Scene(base_frame=scene.graph.base_frame)
    for n, g in scene.geometry.items():
        if n in data:
            d = data[n]
            new.geometry[n] = _build(d, _new_material(d["material"], img_cache, mat_cache, opts))
        else:
            new.geometry[n] = g
    new.graph = scene.graph.copy()
    with contextlib.suppress(Exception):
        new.metadata.update(copy.deepcopy(scene.metadata))

    if opts["scale"] != 1.0:
        new.apply_transform(np.diag([opts["scale"]] * 3 + [1.0]))
    if opts["center"] or opts["ground"]:
        lo, hi = new.bounds
        offset = np.zeros(3)
        if opts["center"]:
            offset[0], offset[2] = -(lo[0] + hi[0]) / 2, -(lo[2] + hi[2]) / 2
        if opts["ground"]:
            offset[1] = -lo[1]
        T = np.eye(4)
        T[:3, 3] = offset
        new.apply_transform(T)
    return new, log


def _optimize_worker(scene, opts, name, folder):
    t0 = time.perf_counter()

    def progress(msg):
        STATE["opt_progress"] = msg

    try:
        new, log = optimize_scene(scene, opts, progress)
        progress("Writing GLB...")
        data = new.export(file_type="glb", include_normals=True)
        progress("Verifying result...")
        check = trimesh.load(io.BytesIO(data), file_type="glb", force="scene")
        prepared = prepare_scene(check, name, folder, len(data),
                                 gltf_info(_gltf_json_from_bytes(data)))
        prepared["report"]["load_s"] = time.perf_counter() - t0
        result = ("ok", prepared, data, log)
    except Exception as e:
        result = ("err", e, None, [])
    with LOCK:
        STATE["opt_result"] = result


# =====================================================================
# Math helpers
# =====================================================================
def glmat(m):
    """Row-major numpy matrix -> column-major float32 bytes for GLSL."""
    return np.ascontiguousarray(np.asarray(m).T, dtype="f4").tobytes()


def look_at(eye, target, up):
    f = target - eye
    f = f / np.linalg.norm(f)
    s = np.cross(f, up)
    ns = np.linalg.norm(s)
    s = s / ns if ns > 1e-9 else np.array([1.0, 0.0, 0.0])
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3] = s, u, -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


def perspective(fovy, aspect, near, far):
    f = 1.0 / math.tan(fovy / 2.0)
    m = np.zeros((4, 4))
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = 2 * far * near / (near - far)
    m[3, 2] = -1.0
    return m


def build_grid(bounds):
    """Ground grid at y=0 centered on the world origin, scaled to the model."""
    lo, hi = (np.array([-1, 0, -1.0]), np.array([1, 1, 1.0])) if bounds is None else bounds
    size = max(float(np.max(hi - lo)), 1e-4)
    max_xz = max(abs(lo[0]), abs(hi[0]), abs(lo[2]), abs(hi[2]), size * 0.5)

    major = 10 ** math.floor(math.log10(size))
    step = major / 10 if size / major < 2.5 else major / 2
    while max_xz * 1.3 / step > 200:  # model far from origin: keep line count sane
        step *= 10
    block = step * 5
    half = math.ceil(max(max_xz * 1.3, step * 10) / block) * block
    n = int(round(half / step))

    minor_c, major_c = (0.55, 0.57, 0.63, 0.20), (0.62, 0.64, 0.70, 0.42)
    rows = []
    for i in range(-n, n + 1):
        if i == 0:
            continue
        t = i * step
        c = major_c if i % 5 == 0 else minor_c
        rows += [(t, 0, -half, *c), (t, 0, half, *c), (-half, 0, t, *c), (half, 0, t, *c)]
    x_c, y_c, z_c = (0.90, 0.32, 0.32, 0.9), (0.40, 0.80, 0.40, 0.9), (0.32, 0.50, 0.95, 0.9)
    rows += [(-half, 0, 0, *x_c), (half, 0, 0, *x_c),
             (0, 0, -half, *z_c), (0, 0, half, *z_c),
             (0, 0, 0, *y_c), (0, size * 0.35, 0, *y_c)]
    return np.array(rows, dtype="f4"), step, half


# =====================================================================
# Offscreen renderer (ModernGL)
# =====================================================================
MESH_VS = """
#version 330
uniform mat4 u_viewproj;
uniform mat4 u_model;
uniform mat3 u_normal_mat;
uniform float u_depth_bias;
in vec3 in_pos; in vec3 in_normal; in vec2 in_uv; in vec4 in_color;
out vec3 v_world; out vec3 v_normal; out vec2 v_uv; out vec4 v_color;
void main() {
    vec4 world = u_model * vec4(in_pos, 1.0);
    v_world = world.xyz;
    v_normal = u_normal_mat * in_normal;
    v_uv = in_uv;
    v_color = in_color;
    gl_Position = u_viewproj * world;
    gl_Position.z -= u_depth_bias * gl_Position.w;
}
"""
MESH_FS = """
#version 330
uniform sampler2D u_tex;
uniform vec4 u_base_color;
uniform vec3 u_eye;
uniform vec3 u_light_dir;
uniform int u_wire;
uniform int u_show_back;
uniform vec4 u_wire_color;
in vec3 v_world; in vec3 v_normal; in vec2 v_uv; in vec4 v_color;
out vec4 f_color;
void main() {
    if (u_wire == 1) { f_color = u_wire_color; return; }
    vec4 albedo = texture(u_tex, v_uv) * v_color * u_base_color;
    if (albedo.a < 0.02) discard;
    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(u_eye - v_world);
    vec3 l = normalize(u_light_dir);
    float diff = max(dot(n, l), 0.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 ambient = mix(vec3(0.20, 0.19, 0.18), vec3(0.42, 0.44, 0.50), hemi);
    float spec = pow(max(dot(n, normalize(l + v)), 0.0), 48.0) * 0.15;
    vec3 color = albedo.rgb * (ambient + 0.85 * diff) + vec3(spec);
    if (u_show_back == 1 && !gl_FrontFacing) color = mix(color, vec3(0.95, 0.15, 0.15), 0.75);
    f_color = vec4(color, 1.0);
}
"""
LINE_VS = """
#version 330
uniform mat4 u_viewproj;
in vec3 in_pos; in vec4 in_color;
out vec4 v_color; out vec3 v_world;
void main() { v_color = in_color; v_world = in_pos; gl_Position = u_viewproj * vec4(in_pos, 1.0); }
"""
LINE_FS = """
#version 330
uniform float u_fade;
in vec4 v_color; in vec3 v_world;
out vec4 f_color;
void main() {
    float a = 1.0 - smoothstep(u_fade * 0.55, u_fade, length(v_world.xz));
    f_color = vec4(v_color.rgb, v_color.a * a);
}
"""
BG_VS = """
#version 330
in vec2 in_pos; out float v_y;
void main() { v_y = in_pos.y * 0.5 + 0.5; gl_Position = vec4(in_pos, 0.0, 1.0); }
"""
BG_FS = """
#version 330
in float v_y; out vec4 f_color;
void main() { f_color = vec4(mix(vec3(0.075, 0.078, 0.09), vec3(0.20, 0.215, 0.25), v_y), 1.0); }
"""


def _set(prog, name, value):
    u = prog.get(name, None)
    if u is None:
        return
    if isinstance(value, bytes):
        u.write(value)
    else:
        u.value = value


def _create_context():
    try:
        return moderngl.create_standalone_context(require=330)
    except Exception:
        if sys.platform.startswith("linux"):  # headless / Wayland: try EGL
            return moderngl.create_standalone_context(require=330, backend="egl")
        raise


class Renderer:
    def __init__(self):
        self.ctx = _create_context()
        ctx = self.ctx
        self.mesh_prog = ctx.program(vertex_shader=MESH_VS, fragment_shader=MESH_FS)
        self.line_prog = ctx.program(vertex_shader=LINE_VS, fragment_shader=LINE_FS)
        self.bg_prog = ctx.program(vertex_shader=BG_VS, fragment_shader=BG_FS)
        self.bg_vbo = ctx.buffer(np.array([-1, -1, 3, -1, -1, 3], dtype="f4").tobytes())
        self.bg_vao = ctx.vertex_array(self.bg_prog, [(self.bg_vbo, "2f", "in_pos")])
        self.white = ctx.texture((1, 1), 4, b"\xff\xff\xff\xff")
        _set(self.mesh_prog, "u_tex", 0)
        self.gl_name = ctx.info.get("GL_RENDERER", "OpenGL")
        self.size = None
        self.fbo = self.fbo_ms = None
        self._fbo_objs = []
        self.meshes, self.textures, self.instances = {}, {}, []
        self.grid_vbo = self.grid_vao = None
        self.grid_fade = 1.0

    def active(self):
        """Make our context current and restore the previous one afterwards
        (matters on Linux, where DearPyGui also uses OpenGL)."""
        return self.ctx if hasattr(self.ctx, "__enter__") else contextlib.nullcontext()

    def resize(self, w, h):
        if self.size == (w, h):
            return
        for o in self._fbo_objs:
            o.release()
        ctx, objs = self.ctx, []
        color, depth = ctx.renderbuffer((w, h), 4), ctx.depth_renderbuffer((w, h))
        objs += [color, depth]
        self.fbo = ctx.framebuffer([color], depth)
        objs.append(self.fbo)
        self.fbo_ms = None
        for samples in (4, 2):
            if ctx.max_samples < samples:
                continue
            try:
                c = ctx.renderbuffer((w, h), 4, samples=samples)
                d = ctx.depth_renderbuffer((w, h), samples=samples)
                self.fbo_ms = ctx.framebuffer([c], d)
                objs += [c, d, self.fbo_ms]
                break
            except Exception:
                self.fbo_ms = None
        self._fbo_objs = objs
        self.size = (w, h)

    def clear_scene(self):
        for m in self.meshes.values():
            for k in ("vao", "vbo", "ibo"):
                m[k].release()
        for t in self.textures.values():
            t.release()
        self.meshes, self.textures, self.instances = {}, {}, []

    def upload(self, prepared):
        self.clear_scene()
        ctx = self.ctx
        for key, im in prepared["images"].items():
            if im is None:
                continue
            tex = ctx.texture(im["size"], 4, im["data"])
            tex.build_mipmaps()
            tex.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
            tex.repeat_x = tex.repeat_y = True
            with contextlib.suppress(Exception):
                tex.anisotropy = 8.0
            self.textures[key] = tex
        for name, g in prepared["geoms"].items():
            vbo = ctx.buffer(g["data"].tobytes())
            ibo = ctx.buffer(g["faces"].tobytes())
            vao = ctx.vertex_array(
                self.mesh_prog,
                [(vbo, "3f 3f 2f 4f", "in_pos", "in_normal", "in_uv", "in_color")],
                ibo,
            )
            self.meshes[name] = {"vao": vao, "vbo": vbo, "ibo": ibo,
                                 "base": tuple(float(x) for x in g["base"]),
                                 "tex": self.textures.get(g["image"])}
        for gname, T in prepared["instances"]:
            A = T[:3, :3]
            try:
                nm = np.linalg.inv(A).T
            except np.linalg.LinAlgError:
                nm = np.eye(3)
            self.instances.append((gname, glmat(T), glmat(nm), np.linalg.det(A) < 0))

    def set_grid(self, verts, fade):
        if self.grid_vao:
            self.grid_vao.release()
            self.grid_vbo.release()
        self.grid_vbo = self.ctx.buffer(verts.tobytes())
        self.grid_vao = self.ctx.vertex_array(
            self.line_prog, [(self.grid_vbo, "3f 4f", "in_pos", "in_color")])
        self.grid_fade = fade

    def _draw_meshes(self, wire):
        p = self.mesh_prog
        for gname, model, nmat, flipped in self.instances:
            m = self.meshes[gname]
            _set(p, "u_model", model)
            _set(p, "u_normal_mat", nmat)
            self.ctx.front_face = "cw" if flipped else "ccw"
            if not wire:
                (m["tex"] or self.white).use(0)
                _set(p, "u_base_color", m["base"])
            m["vao"].render()

    def render(self, viewproj, eye, light_dir, mode, show_grid, show_back):
        ctx = self.ctx
        target = self.fbo_ms or self.fbo
        target.use()
        ctx.viewport = (0, 0, *self.size)
        target.clear(0.0, 0.0, 0.0, 1.0, depth=1.0)
        ctx.disable(moderngl.DEPTH_TEST | moderngl.BLEND | moderngl.CULL_FACE)
        self.bg_vao.render(moderngl.TRIANGLES)
        ctx.enable(moderngl.DEPTH_TEST)
        vp = glmat(viewproj)

        if self.instances:
            p = self.mesh_prog
            _set(p, "u_viewproj", vp)
            _set(p, "u_eye", tuple(float(x) for x in eye))
            _set(p, "u_light_dir", tuple(float(x) for x in light_dir))
            _set(p, "u_show_back", int(show_back))
            if mode != "Wireframe":
                _set(p, "u_wire", 0)
                _set(p, "u_depth_bias", 0.0)
                self._draw_meshes(wire=False)
            if mode != "Shaded":
                overlay = mode != "Wireframe"
                _set(p, "u_wire", 1)
                _set(p, "u_depth_bias", 0.00015 if overlay else 0.0)
                _set(p, "u_wire_color", (0.92, 0.95, 1.0, 0.28) if overlay
                     else (0.72, 0.80, 0.95, 0.85))
                ctx.enable(moderngl.BLEND)
                ctx.wireframe = True
                self._draw_meshes(wire=True)
                ctx.wireframe = False
                ctx.disable(moderngl.BLEND)
            ctx.front_face = "ccw"

        if show_grid and self.grid_vao:
            _set(self.line_prog, "u_viewproj", vp)
            _set(self.line_prog, "u_fade", float(self.grid_fade))
            ctx.enable(moderngl.BLEND)
            self.grid_vao.render(moderngl.LINES)
            ctx.disable(moderngl.BLEND)

        if self.fbo_ms:
            ctx.copy_framebuffer(self.fbo, self.fbo_ms)
        w, h = self.size
        raw = self.fbo.read(components=4, dtype="f4")
        return np.flipud(np.frombuffer(raw, dtype="f4").reshape(h, w, 4))


def placeholder_frame(w, h):
    arr = np.empty((h, w, 4), dtype="f4")
    ramp = np.linspace(0.20, 0.075, h, dtype="f4")[:, None]
    arr[..., 0] = ramp
    arr[..., 1] = ramp * 1.05
    arr[..., 2] = ramp * 1.2
    arr[..., 3] = 1.0
    return arr


# =====================================================================
# Camera
# =====================================================================
def camera_eye():
    d, y, p = STATE["distance"], STATE["yaw"], STATE["pitch"]
    offset = np.array([math.cos(p) * math.sin(y), math.sin(p), math.cos(p) * math.cos(y)])
    return STATE["focus"] + offset * d


def camera_basis():
    forward = STATE["focus"] - camera_eye()
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, WORLD_UP)
    n = np.linalg.norm(right)
    right = right / n if n > 1e-6 else np.array([1.0, 0.0, 0.0])
    return forward, right, np.cross(right, forward)


def camera_matrices(w, h):
    eye = camera_eye()
    view = look_at(eye, STATE["focus"], WORLD_UP)
    d = STATE["distance"]
    reach = max(STATE["fit_radius"], STATE["grid_half"])
    near = max(d * 0.01, 1e-6)
    far = d * 5 + reach * 6
    return perspective(FOV_Y, w / max(h, 1), near, far) @ view, eye


def frame_model():
    """Center the model and fit it to the view, keeping the current angle."""
    STATE["focus"] = STATE["fit_center"].copy()
    STATE["distance"] = STATE["fit_radius"] / math.sin(FOV_Y / 2) * 1.08
    STATE["dirty"] = True


def set_view(yaw, pitch):
    STATE["yaw"], STATE["pitch"] = yaw, pitch
    frame_model()


def reset_view():
    set_view(DEFAULT_YAW, DEFAULT_PITCH)


def apply_bounds(bounds):
    if bounds is None:
        STATE["fit_center"], STATE["fit_radius"] = np.array([0.0, 0.5, 0.0]), 1.0
    else:
        STATE["fit_center"] = (bounds[0] + bounds[1]) / 2.0
        STATE["fit_radius"] = max(float(np.linalg.norm(bounds[1] - bounds[0])) / 2.0, 1e-6)
    STATE["bounds"] = bounds
    verts, step, half = build_grid(bounds)
    STATE["grid_step"], STATE["grid_half"] = step, half
    r = STATE["renderer"]
    if r:
        with r.active():
            r.set_grid(verts, half)
    if dpg.does_item_exist("grid_info"):
        dpg.set_value("grid_info", f"Grid cell: {fmt_len(step)}  (bold every {fmt_len(step * 5)})")


# =====================================================================
# Preview texture + rendering
# =====================================================================
def sync_preview_size():
    w, h = (int(v) for v in dpg.get_item_rect_size("preview_panel"))
    if w < 16 or h < 16 or (w, h) == STATE["preview_size"]:
        return
    STATE["preview_size"] = (w, h)
    STATE["tex_counter"] += 1
    n = STATE["tex_counter"]
    tex_tag, img_tag = f"preview_tex_{n}", f"preview_img_{n}"
    dpg.add_raw_texture(w, h, np.zeros(w * h * 4, dtype="f4"),
                        format=dpg.mvFormat_Float_rgba, tag=tex_tag, parent="tex_registry")
    dpg.configure_item("preview_drawlist", width=w, height=h)
    if STATE["preview_img"]:
        dpg.delete_item(STATE["preview_img"])
    dpg.draw_image(tex_tag, (0, 0), (w, h), parent="layer_image", tag=img_tag)
    if STATE["preview_tex"]:  # free the old texture once it's surely unused
        STATE["delete_queue"].append((STATE["preview_tex"], STATE["frame"] + 3))
    STATE["preview_tex"], STATE["preview_img"] = tex_tag, img_tag
    dpg.configure_item("empty_hint", pos=(w / 2 - 150, h / 2 - 10))
    r = STATE["renderer"]
    if r:
        with r.active():
            r.resize(w, h)
    STATE["dirty"] = True


def redraw():
    w, h = STATE["preview_size"]
    if not STATE["preview_tex"]:
        return
    r = STATE["renderer"]
    if r:
        viewproj, eye = camera_matrices(w, h)
        forward, right, up = camera_basis()
        light = -forward + up * 0.7 - right * 0.45
        with r.active():
            arr = r.render(viewproj, eye, light / np.linalg.norm(light), STATE["render_mode"],
                           STATE["show_grid"], STATE["show_back"])
    else:
        arr = placeholder_frame(w, h)
    arr = np.ascontiguousarray(arr)
    dpg.set_value(STATE["preview_tex"], arr.ravel())
    STATE["last_frame"] = arr
    update_hud()


def update_hud():
    rep = STATE["report"]
    w, h = STATE["preview_size"]
    if STATE["renderer"] is None:
        text = f"Preview unavailable: {STATE['renderer_error']}"
    elif rep:
        text = f"{rep['tris_rendered']:,} tris  |  {STATE['render_mode']}  |  {w}x{h}"
    else:
        text = f"{w}x{h}"
    dpg.configure_item("hud_text", text=text)
    has_opt = "Optimized" in STATE["versions"]
    dpg.configure_item("hud_version", show=has_opt,
                       text=f"{STATE['view'].upper()}   (V to switch)",
                       color=COL_GOOD if STATE["view"] == "Optimized" else (200, 204, 214))


def process_delete_queue():
    keep = []
    for tag, frame in STATE["delete_queue"]:
        if STATE["frame"] >= frame:
            if dpg.does_item_exist(tag):
                dpg.delete_item(tag)
        else:
            keep.append((tag, frame))
    STATE["delete_queue"] = keep


# =====================================================================
# File handling
# =====================================================================
def set_status(text, level="info"):
    dpg.set_value("status_text", text)
    dpg.configure_item("status_text", color=LEVEL_COLORS.get(level, COL_TEXT)
                       if level != "info" else COL_MUTED)


def open_model(path):
    path = os.path.abspath(os.path.expanduser(str(path).strip().strip('"')))
    if not path.lower().endswith((".glb", ".gltf")):
        set_status(f"Not a glTF file: {os.path.basename(path)}", "warn")
        return
    if not os.path.isfile(path):
        set_status(f"File not found: {path}", "warn")
        return
    if STATE["loading"] or STATE["opt_running"]:
        set_status("Busy - wait for the current job to finish.", "warn")
        return
    STATE["loading"] = True
    set_status(f"Loading {os.path.basename(path)}...")
    dpg.configure_item("hud_text", text="Loading...")
    threading.Thread(target=_load_worker, args=(path,), daemon=True).start()


def show_version(name, refit=False):
    prepared = STATE["versions"].get(name)
    if prepared is None:
        return
    r = STATE["renderer"]
    if r:
        try:
            with r.active():
                r.upload(prepared)
        except Exception as e:
            set_status(f"GPU upload failed: {e}", "warn")
            return
    old = STATE["bounds"]
    rep = prepared["report"]
    STATE["view"], STATE["report"] = name, rep
    apply_bounds(rep["bounds"])
    moved = old is None or not np.allclose(old, rep["bounds"],
                                           atol=1e-3 * max(rep["max_dim"], 1e-9))
    if refit or moved:
        frame_model() if not refit else reset_view()
    update_sidebar(rep)
    if dpg.does_item_exist("view_radio"):
        dpg.set_value("view_radio", name)
    STATE["dirty"] = True


def poll_load_result():
    with LOCK:
        result, STATE["load_result"] = STATE["load_result"], None
    if result is None:
        return
    STATE["loading"] = False
    status, path, payload = result
    if status == "err":
        set_status(f"Could not load {os.path.basename(path)}: {payload}", "warn")
        update_hud()
        return
    scene, prepared = payload
    rep = prepared["report"]
    STATE["path"], STATE["scene"] = path, scene
    STATE["versions"] = {"Original": prepared}
    STATE["opt_bytes"] = STATE["opt_opts"] = STATE["saved_path"] = None
    STATE["bounds"] = None
    show_version("Original", refit=True)
    reset_optimizer_ui(rep)
    dpg.configure_item("empty_hint", show=False)
    dpg.enable_item("btn_reload")
    dpg.set_viewport_title(f"{rep['name']} - {APP_NAME}")
    warns = sum(1 for lvl, _ in rep["checks"] if lvl == "warn")
    set_status(f"Loaded {rep['name']} in {rep['load_s']:.2f} s"
               + (f"  |  {warns} warning(s)" if warns else ""), "warn" if warns else "ok")


def reload_model():
    if STATE["path"]:
        open_model(STATE["path"])


def open_browser():
    STATE["dialog_open"] = True
    if STATE["path"]:
        dpg.configure_item("file_dialog", default_path=os.path.dirname(STATE["path"]))
    dpg.show_item("file_dialog")


def on_file_selected(sender, app_data):
    STATE["dialog_open"] = False
    selections = app_data.get("selections") or {}
    path = next(iter(selections.values()), None) or app_data.get("file_path_name")
    if path:
        open_model(path)


def on_file_cancel(*_):
    STATE["dialog_open"] = False


def on_file_drop(data, keys):
    if isinstance(data, (list, tuple)) and data:
        open_model(data[0])
    elif isinstance(data, str):
        open_model(data)


def _unique_path(path):
    stem, ext = os.path.splitext(path)
    out, i = path, 2
    while os.path.exists(out):
        out = f"{stem}_{i}{ext}"
        i += 1
    return out


def _write_beside_model(suffix, writer):
    """Write next to the open model, or the working folder if that fails."""
    if STATE["path"]:
        stem = os.path.splitext(os.path.basename(STATE["path"]))[0]
        folders = [os.path.dirname(STATE["path"]), os.getcwd()]
    else:
        stem, folders = "glbcheck", [os.getcwd()]
    for folder in folders:
        out = _unique_path(os.path.join(folder, stem + suffix))
        try:
            writer(out)
            return out
        except OSError:
            continue
    return None


def save_screenshot():
    arr = STATE["last_frame"]
    if arr is None:
        return
    try:
        from PIL import Image
    except ImportError:
        set_status("Screenshots need Pillow: pip install pillow", "warn")
        return
    img = Image.fromarray(np.clip(arr[..., :3] * 255, 0, 255).astype(np.uint8), "RGB")
    suffix = "_optimized_preview.png" if STATE["view"] == "Optimized" else "_preview.png"
    out = _write_beside_model(suffix, img.save)
    if out:
        set_status(f"Saved screenshot: {out}", "ok")
    else:
        set_status("Could not save screenshot.", "warn")


def reveal_in_folder(path):
    if not path or not os.path.exists(path):
        return
    try:
        if sys.platform.startswith("win"):
            subprocess.Popen(["explorer", "/select,", os.path.normpath(path)])
        elif sys.platform == "darwin":
            subprocess.Popen(["open", "-R", path])
        else:
            subprocess.Popen(["xdg-open", os.path.dirname(path)])
    except OSError as e:
        set_status(f"Could not open folder: {e}", "warn")


# =====================================================================
# Optimizer UI
# =====================================================================
def orig_tris():
    v = STATE["versions"].get("Original")
    return v["report"]["tris_rendered"] if v else 0


def get_opts():
    total = max(orig_tris(), 1)
    tex = dpg.get_value("opt_tex_max")
    return {
        "ratio": min(1.0, max(1, dpg.get_value("opt_target")) / total),
        "orig_tris": total,
        "min_tris": max(0, dpg.get_value("opt_min_tris")),
        "borders": dpg.get_value("opt_borders"),
        "smooth_angle": dpg.get_value("opt_smooth"),
        "clean": dpg.get_value("opt_clean"),
        "tex_max": 0 if tex == "Keep" else int(tex),
        "tex_pot": dpg.get_value("opt_tex_pot"),
        "tex_fmt": dpg.get_value("opt_tex_fmt"),
        "scale": SCALES[dpg.get_value("opt_scale")],
        "center": dpg.get_value("opt_center"),
        "ground": dpg.get_value("opt_ground"),
    }


def update_stale_hint():
    stale = STATE["opt_opts"] is not None and get_opts() != STATE["opt_opts"]
    dpg.configure_item("opt_stale", show=stale)


def on_opt_changed(*_):
    if dpg.get_value("opt_preset") != "Custom":
        dpg.set_value("opt_preset", "Custom")
    update_stale_hint()


def set_target(tris):
    total = max(orig_tris(), 1)
    tris = int(min(max(tris, 1), total))
    dpg.set_value("opt_target", tris)
    dpg.set_value("opt_pct", max(1, int(round(100 * tris / total))))


def on_target_changed(sender, value):
    set_target(value)
    on_opt_changed()


def on_pct_changed(sender, value):
    total = max(orig_tris(), 1)
    dpg.set_value("opt_target", max(1, int(total * value / 100)))
    on_opt_changed()


def apply_preset(sender, name):
    p = PRESETS.get(name)
    if not p:
        return
    total = orig_tris()
    set_target(p["target"] if "target" in p else total * p["pct"] / 100)
    dpg.set_value("opt_tex_max", p["tex"])
    dpg.set_value("opt_tex_pot", p["pot"])
    dpg.set_value("opt_tex_fmt", p["fmt"])
    update_stale_hint()


def reset_optimizer_ui(rep):
    dpg.enable_item("btn_optimize")
    dpg.disable_item("btn_save")
    dpg.configure_item("btn_reveal", show=False)
    dpg.configure_item("view_radio", show=False)
    dpg.configure_item("opt_stale", show=False)
    dpg.delete_item("compare_group", children_only=True)
    dpg.delete_item("opt_log", children_only=True)
    preset = dpg.get_value("opt_preset")
    if PRESETS.get(preset):
        apply_preset(None, preset)
    else:
        set_target(rep["tris_rendered"] // 2)
    info = rep.get("info") or {}
    lost = [k for k in ("animations", "skins", "morphs") if info.get(k)]
    dpg.configure_item("opt_anim_warn", show=bool(lost))
    if lost:
        dpg.set_value("opt_anim_warn", "This file has " + ", ".join(lost) +
                      ". They will be dropped from the optimized copy.")


def run_optimize():
    if STATE["scene"] is None or STATE["opt_running"] or STATE["loading"]:
        return
    opts = get_opts()
    STATE["opt_running"] = True
    STATE["opt_progress"] = "Starting..."
    STATE["opt_opts"] = opts
    dpg.disable_item("btn_optimize")
    stem = os.path.splitext(os.path.basename(STATE["path"]))[0]
    threading.Thread(target=_optimize_worker, daemon=True,
                     args=(STATE["scene"], opts, f"{stem}_optimized.glb (unsaved)",
                           os.path.dirname(STATE["path"]))).start()


def poll_optimize():
    if STATE["opt_running"]:
        dpg.configure_item("btn_optimize", label=STATE["opt_progress"] or "Working...")
    with LOCK:
        result, STATE["opt_result"] = STATE["opt_result"], None
    if result is None:
        return
    STATE["opt_running"] = False
    dpg.configure_item("btn_optimize", label="Optimize")
    dpg.enable_item("btn_optimize")
    status, payload, data, log = result
    if status == "err":
        STATE["opt_opts"] = None
        set_status(f"Optimization failed: {payload}", "warn")
        return
    STATE["versions"]["Optimized"] = payload
    STATE["opt_bytes"] = data
    STATE["saved_path"] = None
    dpg.enable_item("btn_save")
    dpg.configure_item("btn_reveal", show=False)
    dpg.configure_item("view_radio", show=True)
    update_stale_hint()
    show_version("Optimized")
    update_compare(log)
    a, b = STATE["versions"]["Original"]["report"], payload["report"]
    set_status(f"Optimized in {b['load_s']:.1f} s: {a['tris_rendered']:,} -> {b['tris_rendered']:,} tris, "
               f"{fmt_mb(a['file_bytes'])} -> {fmt_mb(b['file_bytes'])}.  Press V to compare.", "ok")


def update_compare(log):
    a = STATE["versions"]["Original"]["report"]
    b = STATE["versions"]["Optimized"]["report"]

    def tex(r):
        return f"{r['max_tex_size'][0]}x{r['max_tex_size'][1]}" if r["max_tex_size"] else "-"

    def tex_mem(r):  # uncompressed RGBA + mips, what the GPU actually holds
        return r["tex_pixels"] * 4 * 4 / 3

    rows = [
        ("Triangles", a["tris_rendered"], b["tris_rendered"], lambda v: f"{v:,}"),
        ("Vertices", a["verts"], b["verts"], lambda v: f"{v:,}"),
        ("File size", a["file_bytes"], b["file_bytes"], fmt_mb),
        ("Texture VRAM", tex_mem(a), tex_mem(b), fmt_mb),
        ("Largest tex", None, None, None),
        ("Size (max)", a["max_dim"], b["max_dim"], fmt_len),
    ]
    dpg.delete_item("compare_group", children_only=True)
    with dpg.table(parent="compare_group", header_row=True, policy=dpg.mvTable_SizingStretchProp,
                   borders_innerH=False, row_background=False):
        for label, w in (("", 0.30), ("Before", 0.26), ("After", 0.26), ("", 0.18)):
            dpg.add_table_column(label=label, init_width_or_weight=w)
        for label, va, vb, f in rows:
            with dpg.table_row():
                dpg.add_text(label, color=COL_MUTED)
                if f is None:
                    dpg.add_text(tex(a))
                    dpg.add_text(tex(b))
                    dpg.add_text("")
                    continue
                dpg.add_text(f(va))
                dpg.add_text(f(vb))
                if label.startswith("Size") or not va:
                    dpg.add_text("")
                else:
                    pct = (vb - va) / va * 100
                    color = COL_GOOD if pct < -0.5 else COL_BAD if pct > 0.5 else COL_MUTED
                    dpg.add_text(f"{pct:+.0f}%", color=color)
    dpg.delete_item("opt_log", children_only=True)
    for line in log:
        dpg.add_text(line, parent="opt_log", color=LEVEL_COLORS["warn"], wrap=SIDEBAR_W - 30)


def save_optimized():
    data = STATE["opt_bytes"]
    if not data:
        return

    def write(path):
        with open(path, "wb") as f:
            f.write(data)

    out = _write_beside_model("_optimized.glb", write)
    if out is None:
        set_status("Could not save the optimized file.", "warn")
        return
    STATE["saved_path"] = out
    rep = STATE["versions"]["Optimized"]["report"]
    rep["name"], rep["dir"] = os.path.basename(out), os.path.dirname(out)
    if STATE["view"] == "Optimized":
        update_sidebar(rep)
    dpg.configure_item("btn_reveal", show=True)
    set_status(f"Saved {out}", "ok")


def toggle_version():
    if "Optimized" not in STATE["versions"]:
        return
    show_version("Original" if STATE["view"] == "Optimized" else "Optimized")


# =====================================================================
# Sidebar
# =====================================================================
def update_sidebar(rep):
    dpg.set_value("file_name", rep["name"])
    dpg.set_value("file_dir", rep["dir"])
    d = rep["dims"]
    tris = f"{rep['tris']:,}"
    if rep["tris_rendered"] != rep["tris"]:
        tris += f"  ({rep['tris_rendered']:,} drawn)"
    tex = str(rep["textures"])
    if rep["max_tex_size"]:
        tex += f"  (max {rep['max_tex_size'][0]}x{rep['max_tex_size'][1]})"
    info = rep.get("info") or {}
    rows = [
        ("File size", fmt_mb(rep["file_bytes"])),
        ("Vertices", f"{rep['verts']:,}"),
        ("Triangles", tris),
        ("Meshes", f"{rep['meshes']}  ({rep['instances']} instances)"),
        ("Materials", str(rep["materials"])),
        ("Textures", tex),
        ("Width (X)", fmt_len(d[0])),
        ("Height (Y)", fmt_len(d[1])),
        ("Depth (Z)", fmt_len(d[2])),
    ]
    if info.get("animations"):
        rows.append(("Animations", str(info["animations"])))
    if info.get("generator"):
        rows.append(("Exported by", info["generator"]))
    if info.get("extensions"):
        rows.append(("Extensions", ", ".join(e.replace("KHR_", "") for e in info["extensions"])))
    dpg.delete_item("stats_group", children_only=True)
    with dpg.table(parent="stats_group", header_row=False, policy=dpg.mvTable_SizingStretchProp):
        dpg.add_table_column(init_width_or_weight=0.36)
        dpg.add_table_column(init_width_or_weight=0.64)
        for k, v in rows:
            with dpg.table_row():
                dpg.add_text(k, color=COL_MUTED)
                dpg.add_text(v, wrap=SIDEBAR_W - 150)

    dpg.delete_item("checks_group", children_only=True)
    for level, msg in rep["checks"]:
        with dpg.group(horizontal=True, parent="checks_group"):
            dpg.add_text(LEVEL_TAGS[level], color=LEVEL_COLORS[level])
            dpg.add_text(msg, wrap=SIDEBAR_W - 90)


# =====================================================================
# Input
# =====================================================================
def set_render_mode(mode):
    STATE["render_mode"] = mode
    dpg.set_value("render_mode", mode)
    STATE["dirty"] = True


def toggle(key, widget):
    STATE[key] = not STATE[key]
    dpg.set_value(widget, STATE[key])
    STATE["dirty"] = True


def typing():
    return any(dpg.does_item_exist(t) and dpg.is_item_active(t) for t in INPUT_TAGS)


def on_mouse_click(sender, button):
    if not dpg.is_item_hovered("preview_drawlist"):
        return
    pan = button == dpg.mvMouseButton_Middle or any_down(SHIFT_KEYS)
    STATE["drag"] = "pan" if pan else "orbit"
    STATE["last_mouse"] = tuple(dpg.get_mouse_pos(local=False))


def on_mouse_release(*_):
    STATE["drag"] = None


def on_double_click(*_):
    if dpg.is_item_hovered("preview_drawlist"):
        frame_model()


def on_scroll(sender, app_data):
    if not dpg.is_item_hovered("preview_drawlist"):
        return
    delta = app_data[1] if isinstance(app_data, (list, tuple)) else app_data
    r = STATE["fit_radius"]
    STATE["distance"] = min(max(STATE["distance"] * 0.87 ** delta, r * 0.01), r * 300)
    STATE["dirty"] = True


def process_mouse():
    mx, my = dpg.get_mouse_pos(local=False)
    lx, ly = STATE["last_mouse"]
    STATE["last_mouse"] = (mx, my)
    if not STATE["drag"]:
        return
    if not any(dpg.is_mouse_button_down(b) for b in
               (dpg.mvMouseButton_Left, dpg.mvMouseButton_Right, dpg.mvMouseButton_Middle)):
        STATE["drag"] = None
        return
    dx, dy = mx - lx, my - ly
    if dx == 0 and dy == 0:
        return
    if STATE["drag"] == "orbit":
        STATE["yaw"] -= dx * 0.008
        STATE["pitch"] = min(1.55, max(-1.55, STATE["pitch"] + dy * 0.008))
    else:
        _, right, up = camera_basis()
        per_px = 2 * STATE["distance"] * math.tan(FOV_Y / 2) / max(STATE["preview_size"][1], 1)
        STATE["focus"] = STATE["focus"] - right * dx * per_px + up * dy * per_px
    STATE["dirty"] = True


def process_keys(dt):
    if STATE["dialog_open"] or any_down(CTRL_KEYS) or typing():
        return
    forward, right, _ = camera_basis()
    move = np.zeros(3)
    for key, vec in ((dpg.mvKey_W, forward), (dpg.mvKey_S, -forward),
                     (dpg.mvKey_D, right), (dpg.mvKey_A, -right),
                     (dpg.mvKey_E, WORLD_UP), (dpg.mvKey_Q, -WORLD_UP)):
        if dpg.is_key_down(key):
            move += vec
    n = np.linalg.norm(move)
    if n > 0:
        speed = STATE["distance"] * 0.8 * (3.0 if any_down(SHIFT_KEYS) else 1.0)
        STATE["focus"] = STATE["focus"] + move / n * speed * min(dt, 0.1)
        STATE["dirty"] = True


def on_key_press(sender, key):
    if STATE["dialog_open"] or typing():
        return
    if any_down(CTRL_KEYS):
        if key == dpg.mvKey_O:
            open_browser()
        elif key == dpg.mvKey_S:
            save_optimized()
        return
    actions = {
        dpg.mvKey_F: frame_model,
        dpg.mvKey_R: reset_view,
        dpg.mvKey_G: lambda: toggle("show_grid", "show_grid"),
        dpg.mvKey_B: lambda: toggle("show_back", "show_back"),
        dpg.mvKey_V: toggle_version,
        dpg.mvKey_Z: lambda: set_render_mode(
            RENDER_MODES[(RENDER_MODES.index(STATE["render_mode"]) + 1) % len(RENDER_MODES)]),
        dpg.mvKey_F5: reload_model,
        dpg.mvKey_F12: save_screenshot,
        dpg.mvKey_1: lambda: set_view(0.0, 0.0),
        dpg.mvKey_2: lambda: set_view(math.pi / 2, 0.0),
        dpg.mvKey_3: lambda: set_view(0.0, 1.55),
        dpg.mvKey_4: reset_view,
    }
    action = actions.get(key)
    if action:
        action()


# =====================================================================
# UI
# =====================================================================
CONTROLS_TEXT = (
    "Left/Right drag   orbit\n"
    "Middle drag       pan  (or Shift+drag)\n"
    "Scroll            zoom\n"
    "Double-click      frame model\n"
    "W A S D / Q E     fly  (Shift = fast)\n"
    "F frame   R reset   Z render mode\n"
    "G grid    B back faces\n"
    "V original / optimized\n"
    "1 front  2 right  3 top  4 3/4 view\n"
    "Ctrl+O open   Ctrl+S save optimized\n"
    "F5 reload   F12 screenshot"
)


def load_font():
    candidates = [
        "C:/Windows/Fonts/segoeui.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                with dpg.font_registry():
                    font = dpg.add_font(path, 16)
                dpg.bind_font(font)
                return
            except Exception:
                continue


def apply_theme():
    with dpg.theme() as theme:
        with dpg.theme_component(dpg.mvAll):
            for var, val in ((dpg.mvStyleVar_FrameRounding, 4), (dpg.mvStyleVar_ChildRounding, 6),
                             (dpg.mvStyleVar_GrabRounding, 4), (dpg.mvStyleVar_PopupRounding, 4),
                             (dpg.mvStyleVar_WindowRounding, 6)):
                dpg.add_theme_style(var, val)
            dpg.add_theme_style(dpg.mvStyleVar_FramePadding, 8, 5)
            dpg.add_theme_style(dpg.mvStyleVar_ItemSpacing, 8, 6)
            dpg.add_theme_style(dpg.mvStyleVar_WindowPadding, 10, 10)
            for col, val in (
                (dpg.mvThemeCol_WindowBg, (22, 23, 27)),
                (dpg.mvThemeCol_ChildBg, (29, 30, 35)),
                (dpg.mvThemeCol_PopupBg, (32, 34, 40)),
                (dpg.mvThemeCol_Text, COL_TEXT),
                (dpg.mvThemeCol_Border, (48, 50, 58)),
                (dpg.mvThemeCol_FrameBg, (42, 44, 51)),
                (dpg.mvThemeCol_FrameBgHovered, (52, 55, 64)),
                (dpg.mvThemeCol_FrameBgActive, (60, 64, 75)),
                (dpg.mvThemeCol_Button, (50, 54, 64)),
                (dpg.mvThemeCol_ButtonHovered, (64, 70, 84)),
                (dpg.mvThemeCol_ButtonActive, (70, 105, 165)),
                (dpg.mvThemeCol_Header, (40, 43, 51)),
                (dpg.mvThemeCol_HeaderHovered, (52, 56, 67)),
                (dpg.mvThemeCol_HeaderActive, (60, 66, 80)),
                (dpg.mvThemeCol_CheckMark, COL_ACCENT),
                (dpg.mvThemeCol_SliderGrab, COL_ACCENT),
                (dpg.mvThemeCol_SliderGrabActive, (150, 190, 255)),
                (dpg.mvThemeCol_TableRowBgAlt, (33, 34, 40)),
                (dpg.mvThemeCol_TitleBgActive, (36, 38, 46)),
            ):
                dpg.add_theme_color(col, val)
        with dpg.theme_component(dpg.mvButton, enabled_state=False):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (95, 99, 110))
            dpg.add_theme_color(dpg.mvThemeCol_Button, (38, 40, 47))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (38, 40, 47))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (38, 40, 47))
    dpg.bind_theme(theme)

    with dpg.theme(tag="preview_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_style(dpg.mvStyleVar_WindowPadding, 0, 0)
            dpg.add_theme_color(dpg.mvThemeCol_ChildBg, (18, 19, 22))

    with dpg.theme(tag="primary_btn"):
        with dpg.theme_component(dpg.mvButton):
            dpg.add_theme_color(dpg.mvThemeCol_Button, (52, 92, 160))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (66, 110, 185))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (80, 128, 210))
        with dpg.theme_component(dpg.mvButton, enabled_state=False):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (170, 180, 200))
            dpg.add_theme_color(dpg.mvThemeCol_Button, (44, 60, 92))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (44, 60, 92))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (44, 60, 92))


@contextlib.contextmanager
def form():
    with dpg.table(header_row=False, policy=dpg.mvTable_SizingStretchProp):
        dpg.add_table_column(init_width_or_weight=0.40)
        dpg.add_table_column(init_width_or_weight=0.60)
        yield


@contextlib.contextmanager
def form_row(label, tip=None):
    with dpg.table_row():
        t = dpg.add_text(label, color=COL_MUTED)
        if tip:
            with dpg.tooltip(t):
                dpg.add_text(tip, wrap=300)
        yield


def subheading(text):
    dpg.add_spacer(height=2)
    dpg.add_text(text, color=COL_ACCENT)


def build_optimize_panel(inner):
    decim_ok = fast_simplification is not None
    with dpg.collapsing_header(label="Optimize", default_open=True):
        with form():
            with form_row("Preset"):
                dpg.add_combo(list(PRESETS), default_value="Custom", tag="opt_preset", width=-1,
                              callback=apply_preset)

        subheading("Polygons")
        with form():
            with form_row("Target tris", "Total triangles drawn after optimization."):
                dpg.add_input_int(tag="opt_target", default_value=1, step=0, min_value=1,
                                  min_clamped=True, width=-1, callback=on_target_changed,
                                  enabled=decim_ok)
            with form_row("Keep"):
                dpg.add_slider_int(tag="opt_pct", default_value=50, min_value=1, max_value=100,
                                   format="%d%%", width=-1, callback=on_pct_changed,
                                   enabled=decim_ok)
            with form_row("Protect edges", "Keep open borders and UV seams in place. Avoids "
                          "texture tearing and holes but limits how far a mesh can be reduced."):
                dpg.add_checkbox(tag="opt_borders", default_value=True, callback=on_opt_changed)
            with form_row("Hard-edge angle", "After reduction, edges sharper than this get "
                          "crisp shading; smaller angles look smoother."):
                dpg.add_slider_int(tag="opt_smooth", default_value=50, min_value=0, max_value=180,
                                   format="%d deg", width=-1, callback=on_opt_changed)
            with form_row("Skip under", "Meshes with fewer triangles than this are left alone "
                          "(small details, logos, etc)."):
                dpg.add_input_int(tag="opt_min_tris", default_value=100, step=0, min_value=0,
                                  min_clamped=True, width=-1, callback=on_opt_changed)
            with form_row("Clean geometry", "Weld duplicate vertices, remove zero-area and "
                          "duplicate triangles and unused vertices."):
                dpg.add_checkbox(tag="opt_clean", default_value=True, callback=on_opt_changed)
        if not decim_ok:
            dpg.add_text("Polygon reduction needs: pip install fast-simplification",
                         color=LEVEL_COLORS["warn"], wrap=inner)

        subheading("Textures")
        with form():
            with form_row("Max size"):
                dpg.add_combo(TEX_SIZES, default_value="Keep", tag="opt_tex_max", width=-1,
                              callback=on_opt_changed)
            with form_row("Format", "JPEG is much smaller. Textures with transparency stay PNG."):
                dpg.add_combo(TEX_FORMATS, default_value="Keep", tag="opt_tex_fmt", width=-1,
                              callback=on_opt_changed)
            with form_row("Power of two", "Round sizes to 256/512/1024... for older GPUs "
                          "and mipmapping."):
                dpg.add_checkbox(tag="opt_tex_pot", default_value=False, callback=on_opt_changed)

        subheading("Transform")
        with form():
            with form_row("Scale"):
                dpg.add_combo(list(SCALES), default_value="No change", tag="opt_scale", width=-1,
                              callback=on_opt_changed)
            with form_row("Center on origin"):
                dpg.add_checkbox(tag="opt_center", default_value=False, callback=on_opt_changed)
            with form_row("Place on ground"):
                dpg.add_checkbox(tag="opt_ground", default_value=False, callback=on_opt_changed)

        dpg.add_spacer(height=4)
        dpg.add_text("", tag="opt_anim_warn", color=LEVEL_COLORS["warn"], wrap=inner, show=False)
        dpg.add_button(label="Optimize", tag="btn_optimize", width=-1, height=30,
                       enabled=False, callback=lambda *_: run_optimize())
        dpg.bind_item_theme("btn_optimize", "primary_btn")
        dpg.add_text("Settings changed since the last run.", tag="opt_stale",
                     color=COL_MUTED, show=False)
        dpg.add_radio_button(["Original", "Optimized"], tag="view_radio", horizontal=True,
                             default_value="Original", show=False,
                             callback=lambda s, a: show_version(a))
        dpg.add_group(tag="compare_group")
        dpg.add_group(tag="opt_log")
        with dpg.group(horizontal=True):
            half = (inner - 8) // 2
            dpg.add_button(label="Save optimized .glb", tag="btn_save", width=half,
                           enabled=False, callback=lambda *_: save_optimized())
            dpg.add_button(label="Show in folder", tag="btn_reveal", width=-1, show=False,
                           callback=lambda *_: reveal_in_folder(STATE["saved_path"]))


def build_ui(dnd_ok):
    inner = SIDEBAR_W - 24
    btn4 = (inner - 3 * 8) // 4
    with dpg.window(tag="main"):
        with dpg.group(horizontal=True):
            with dpg.child_window(width=SIDEBAR_W, height=-STATUS_H, border=False):
                dpg.add_text(APP_NAME, color=COL_ACCENT)
                with dpg.group(horizontal=True):
                    dpg.add_button(label="Open...", width=btn4, callback=lambda *_: open_browser())
                    dpg.add_button(label="Reload", width=btn4, tag="btn_reload", enabled=False,
                                   callback=lambda *_: reload_model())
                    dpg.add_button(label="Screenshot", width=-1, callback=lambda *_: save_screenshot())
                dpg.add_text("No file loaded", tag="file_name", wrap=inner)
                dpg.add_text("Open a .glb/.gltf" + (" or drop one on the window." if dnd_ok else "."),
                             tag="file_dir", wrap=inner, color=COL_MUTED)
                dpg.add_spacer(height=2)

                with dpg.collapsing_header(label="Model", default_open=True):
                    dpg.add_group(tag="stats_group")
                    dpg.add_text("-", parent="stats_group", color=COL_MUTED)
                with dpg.collapsing_header(label="Checks", default_open=True):
                    dpg.add_group(tag="checks_group")
                    dpg.add_text("-", parent="checks_group", color=COL_MUTED)
                build_optimize_panel(inner)
                with dpg.collapsing_header(label="View", default_open=False):
                    dpg.add_combo(RENDER_MODES, default_value=RENDER_MODES[0], tag="render_mode",
                                  width=-1, callback=lambda s, a: set_render_mode(a))
                    dpg.add_checkbox(label="Ground grid (G)", tag="show_grid", default_value=True,
                                     callback=lambda s, a: (STATE.__setitem__("show_grid", a),
                                                            STATE.__setitem__("dirty", True)))
                    dpg.add_checkbox(label="Highlight back faces (B)", tag="show_back",
                                     callback=lambda s, a: (STATE.__setitem__("show_back", a),
                                                            STATE.__setitem__("dirty", True)))
                    with dpg.group(horizontal=True):
                        dpg.add_button(label="Front", width=btn4, callback=lambda *_: set_view(0.0, 0.0))
                        dpg.add_button(label="Back", width=btn4, callback=lambda *_: set_view(math.pi, 0.0))
                        dpg.add_button(label="Left", width=btn4, callback=lambda *_: set_view(-math.pi / 2, 0.0))
                        dpg.add_button(label="Right", width=-1, callback=lambda *_: set_view(math.pi / 2, 0.0))
                    with dpg.group(horizontal=True):
                        dpg.add_button(label="Top", width=btn4, callback=lambda *_: set_view(0.0, 1.55))
                        dpg.add_button(label="Bottom", width=btn4, callback=lambda *_: set_view(0.0, -1.55))
                        dpg.add_button(label="3/4", width=btn4, callback=lambda *_: reset_view())
                        dpg.add_button(label="Frame", width=-1, callback=lambda *_: frame_model())
                    dpg.add_text("", tag="grid_info", color=COL_MUTED, wrap=inner)
                with dpg.collapsing_header(label="Controls", default_open=False):
                    dpg.add_text(CONTROLS_TEXT, color=COL_MUTED)

            with dpg.child_window(tag="preview_panel", width=-1, height=-STATUS_H,
                                  border=False, no_scrollbar=True):
                with dpg.drawlist(width=800, height=600, tag="preview_drawlist"):
                    dpg.add_draw_layer(tag="layer_image")
                    with dpg.draw_layer(tag="layer_hud"):
                        dpg.draw_text((12, 10), "", tag="hud_text", size=15, color=(200, 204, 214))
                        dpg.draw_text((12, 30), "", tag="hud_version", size=15, show=False)
                        dpg.draw_text((250, 290), "Open or drop a .glb file   (Ctrl+O)",
                                      tag="empty_hint", size=18, color=(150, 156, 170))
            dpg.bind_item_theme("preview_panel", "preview_theme")

        dpg.add_text("Ready", tag="status_text", color=COL_MUTED)

    with dpg.file_dialog(directory_selector=False, show=False, modal=True,
                         callback=on_file_selected, cancel_callback=on_file_cancel,
                         tag="file_dialog", width=760, height=460):
        dpg.add_file_extension("glTF models (*.glb *.gltf){.glb,.gltf}", color=(150, 255, 150, 255))
        dpg.add_file_extension(".*")

    with dpg.handler_registry():
        dpg.add_mouse_click_handler(callback=on_mouse_click)
        dpg.add_mouse_release_handler(callback=on_mouse_release)
        dpg.add_mouse_double_click_handler(button=dpg.mvMouseButton_Left, callback=on_double_click)
        dpg.add_mouse_wheel_handler(callback=on_scroll)
        dpg.add_key_press_handler(callback=on_key_press)


def init_renderer():
    if moderngl is None:
        STATE["renderer_error"] = "pip install moderngl"
        return
    try:
        STATE["renderer"] = Renderer()
    except Exception as e:
        STATE["renderer_error"] = f"no OpenGL 3.3 context ({e})"


def main():
    dpg.create_context()
    dnd_ok = False
    if dpg_dnd is not None:
        try:
            dpg_dnd.initialize()
            dpg_dnd.set_drop(on_file_drop)
            dnd_ok = True
        except Exception:
            pass

    load_font()
    apply_theme()
    dpg.add_texture_registry(tag="tex_registry")
    build_ui(dnd_ok)
    init_renderer()  # before the viewport so its GL context doesn't clash with DPG's
    apply_bounds(None)
    reset_view()

    dpg.create_viewport(title=APP_NAME, width=1360, height=880, min_width=860, min_height=560)
    dpg.set_viewport_clear_color((22, 23, 27, 255))
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("main", True)

    r = STATE["renderer"]
    if r:
        set_status(f"Renderer: {r.gl_name}")
    else:
        set_status(f"Preview disabled: {STATE['renderer_error']}", "warn")
    if len(sys.argv) > 1:
        open_model(sys.argv[1])

    while dpg.is_dearpygui_running():
        STATE["frame"] += 1
        poll_load_result()
        poll_optimize()
        sync_preview_size()
        process_mouse()
        process_keys(dpg.get_delta_time())
        if STATE["dirty"]:
            STATE["dirty"] = False
            redraw()
        process_delete_queue()
        dpg.render_dearpygui_frame()

    if r:
        with r.active():
            r.clear_scene()
    dpg.destroy_context()


if __name__ == "__main__":
    main()
