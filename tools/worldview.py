import math
import sys
import threading
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from OpenGL import GL as gl
from OpenGL.GLU import gluLookAt, gluPerspective
from PyQt6.QtCore import QObject, QPoint, QPointF, Qt, QThread, QTimer, pyqtSignal
from PyQt6.QtGui import QCursor, QFont, QSurfaceFormat
from PyQt6.QtOpenGLWidgets import QOpenGLWidget
from PyQt6.QtWidgets import (
    QApplication, QFileDialog, QFrame, QLabel, QMainWindow, QMessageBox,
    QProgressBar, QPushButton, QStackedWidget, QVBoxLayout, QWidget,
)

from worldgen import BIOME_RGB_F, BIOMES, World, load_world

EYE_R = 1.6
GRAVITY = 24.0
JUMP_V = 9.0
STEP_UP = 0.35
MAX_SLOPE = 1.2
WALK_SPEED = 7.0
FLY_SPEED = 30.0
SPRINT = 2.2
DOUBLE_TAP = 0.35
VS_FRAC = 0.12
LOAD_R = 2
UNLOAD_R = 3
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

    def store(self, key, H, B):
        self.data[key] = ((np.asarray(H, np.float64) * self.vs).astype(np.float32),
                          np.asarray(B, np.uint8))
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
        own_H, own_B = self.data[key]
        Hp = _extend(own_H, R and R[0], D and D[0], G and G[0], rx, dy).astype(np.float64)
        Bp = _extend(own_B, R and R[1], D and D[1], G and G[1], rx, dy)
        m = n + 1
        X, Z = np.meshgrid(cx * n + np.arange(m), cy * n + np.arange(m))
        verts = np.stack([X.astype(np.float64), Hp, Z.astype(np.float64)], -1).astype(np.float32).reshape(-1, 3)
        dhdx = np.gradient(Hp, 1.0, axis=1)
        dhdz = np.gradient(Hp, 1.0, axis=0)
        nrm = np.stack([-dhdx, np.ones_like(dhdx), -dhdz], -1)
        nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        shade = 0.4 + 0.6 * np.clip(nrm @ LIGHT, 0.0, 1.0)
        rgb = BIOME_RGB_F[Bp] / 255.0
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
        H, B = self.data[key]
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

    def __init__(self, path):
        super().__init__()
        self.path = path
        self._cond = threading.Condition()
        self._wanted = []
        self._generated = set()
        self._halt = False

    def run(self):
        try:
            settings, _o, _r, sea, _rt, _d = load_world(Path(self.path))
            world = World(settings)
            world.sea_level = sea
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.initialized.emit({"settings": settings, "sea": sea})
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
            except Exception as exc:
                self.failed.emit(str(exc))
                return
            self.chunk_ready.emit(key[0], key[1], (ch["height"], ch["biome"]))

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

    def __init__(self, path):
        super().__init__()
        self.terrain = None
        self.spawn_keys = []
        self.spawn_done = False
        self.player_c = (0, 0)
        self.gen = Generator(path)
        self.gen.initialized.connect(self.on_initialized)
        self.gen.chunk_ready.connect(self.on_chunk)
        self.gen.failed.connect(self.failed)
        self.gen.start()

    def center_key(self):
        c = self.terrain.K // 2
        return (c, c)

    def on_initialized(self, info):
        self.terrain = Terrain(info["settings"], info["sea"])
        cx, cy = self.center_key()
        K = self.terrain.K
        keys = [(x, y) for x in range(max(0, cx - 1), min(K, cx + 2))
                for y in range(max(0, cy - 1), min(K, cy + 2))]
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
        H, B = payload
        self.terrain.store(key, H, B)
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


class ViewWidget(QOpenGLWidget):
    back_requested = pyqtSignal()

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
        back = QPushButton("Back to start")
        back.clicked.connect(self.back_requested.emit)
        lay.addWidget(title)
        lay.addWidget(resume)
        lay.addWidget(back)
        return box

    def resizeEvent(self, e):
        super().resizeEvent(e)
        if not hasattr(self, "menu"):
            return
        self._center_menu()

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
            col, fog_start, fog_end = SKY, view_dist * 0.6, view_dist * 0.95
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


class StartPage(QWidget):
    open_requested = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.setAcceptDrops(True)
        lay = QVBoxLayout(self)
        lay.addStretch()

        title = QLabel("Worldgen Viewer")
        title.setStyleSheet("font-size:34px; font-weight:600;")
        sub = QLabel("Import a world saved from Worldgen Lab and walk through it.")
        btn = QPushButton("Import world (.wglb)…")
        btn.setFixedWidth(260)
        btn.clicked.connect(self._pick)
        drop = QLabel("or drop a .wglb file anywhere on this page")
        drop.setStyleSheet("color:#8a97a8;")
        controls = QLabel(CONTROLS)
        controls.setStyleSheet("color:#8a97a8; font-size:12px;")
        controls.setAlignment(Qt.AlignmentFlag.AlignCenter)

        for w in (title, sub):
            lay.addWidget(w, 0, Qt.AlignmentFlag.AlignHCenter)
        lay.addSpacing(18)
        lay.addWidget(btn, 0, Qt.AlignmentFlag.AlignHCenter)
        lay.addWidget(drop, 0, Qt.AlignmentFlag.AlignHCenter)
        lay.addSpacing(28)
        lay.addWidget(controls, 0, Qt.AlignmentFlag.AlignHCenter)
        lay.addStretch()

    def _pick(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Import world", "", "Worldgen Lab (*.wglb *.wg);;All files (*)"
        )
        if path:
            self.open_requested.emit(path)

    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e):
        urls = e.mimeData().urls()
        if urls:
            self.open_requested.emit(urls[0].toLocalFile())


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


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Worldgen Viewer")
        self.resize(1280, 800)
        self.stack = QStackedWidget()
        self.start = StartPage()
        self.start.open_requested.connect(self.open_world)
        self.loading = LoadingPage()
        self.stack.addWidget(self.start)
        self.stack.addWidget(self.loading)
        self.setCentralWidget(self.stack)
        self.view = None
        self.streamer = None

    def open_world(self, path):
        if not path:
            return
        self._stop_streamer()
        self.loading.set_progress(0, 1)
        self.stack.setCurrentWidget(self.loading)
        self.streamer = Streamer(path)
        self.streamer.progress.connect(self.loading.set_progress)
        self.streamer.spawn_ready.connect(self._show_world)
        self.streamer.failed.connect(self._failed)

    def _stop_streamer(self):
        if self.streamer is not None:
            self.streamer.shutdown()
            self.streamer = None

    def _failed(self, msg):
        self._stop_streamer()
        self.stack.setCurrentWidget(self.start)
        QMessageBox.warning(self, "Could not load world", msg)

    def _show_world(self):
        if self.streamer is None:
            return
        view = ViewWidget(self.streamer, self.streamer.spawn())
        view.back_requested.connect(self.show_start)
        if self.view is not None:
            self.view.shutdown()
            self.stack.removeWidget(self.view)
            self.view.deleteLater()
        self.view = view
        self.stack.addWidget(view)
        self.stack.setCurrentWidget(view)
        view.setFocus()

    def show_start(self):
        if self.view is not None:
            self.view.shutdown()
        self._stop_streamer()
        self.stack.setCurrentWidget(self.start)

    def closeEvent(self, e):
        self._stop_streamer()
        super().closeEvent(e)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key.Key_F11:
            if self.isFullScreen():
                self.showNormal()
            else:
                self.showFullScreen()
        else:
            super().keyPressEvent(e)


def main():
    fmt = QSurfaceFormat()
    fmt.setVersion(2, 1)
    fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
    fmt.setDepthBufferSize(24)
    fmt.setSamples(4)
    QSurfaceFormat.setDefaultFormat(fmt)
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    if len(sys.argv) > 1:
        win.open_world(sys.argv[1])
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
