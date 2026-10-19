import math
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from OpenGL import GL as gl
from OpenGL.GLU import gluLookAt, gluPerspective
from PyQt6.QtCore import QPoint, QPointF, Qt, QThread, QTimer, pyqtSignal
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
MAX_SIDE = 640
VS_FRAC = 0.12
SENS = 0.0022
SKY = (0.55, 0.72, 0.9)
DEEP = (0.12, 0.3, 0.45)
WATER = (0.12, 0.32, 0.5, 0.6)
LIGHT = np.array([0.45, 0.8, 0.35]) / np.linalg.norm([0.45, 0.8, 0.35])
RING = np.arange(8) * (math.pi / 4)

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
    "actual ground. Water is not solid, so you can walk along the seabed beneath it."
)


class Terrain:
    def __init__(self, Hs, Bd, st, vs, sea_y):
        self.Hs = np.ascontiguousarray(Hs, dtype=np.float32)
        self.Bd = np.asarray(Bd, dtype=np.uint8)
        self.st = float(st)
        self.M = self.Hs.shape[0]
        self.W = (self.M - 1) * self.st
        self.vs = float(vs)
        self.sea_y = float(sea_y)
        self.verts, self.colors, self.indices = self._mesh()

    def _mesh(self):
        M, st = self.M, self.st
        Hs64 = self.Hs.astype(np.float64)
        X, Z = np.meshgrid(np.arange(M) * st, np.arange(M) * st)
        verts = np.stack([X, Hs64, Z], -1).astype(np.float32).reshape(-1, 3)
        dhdx = np.gradient(Hs64, st, axis=1)
        dhdz = np.gradient(Hs64, st, axis=0)
        n = np.stack([-dhdx, np.ones_like(dhdx), -dhdz], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        shade = 0.4 + 0.6 * np.clip(n @ LIGHT, 0.0, 1.0)
        rgb = BIOME_RGB_F[self.Bd] / 255.0
        colors = (rgb * shade[..., None]).reshape(-1, 3).astype(np.float32)
        a = (np.arange(M - 1)[None, :] + np.arange(M - 1)[:, None] * M).ravel()
        tris = np.concatenate([
            np.stack([a, a + 1, a + M], -1),
            np.stack([a + 1, a + M + 1, a + M], -1),
        ])
        indices = tris.astype(np.uint32).ravel()
        return verts, colors, indices

    def ground(self, x, z):
        hi = self.M - 1.000001
        gi = np.clip(np.asarray(x, np.float64) / self.st, 0.0, hi)
        gj = np.clip(np.asarray(z, np.float64) / self.st, 0.0, hi)
        i0 = np.floor(gi).astype(np.int64)
        j0 = np.floor(gj).astype(np.int64)
        fi = gi - i0
        fj = gj - j0
        H = self.Hs
        h00 = H[j0, i0]
        h01 = H[j0, i0 + 1]
        h10 = H[j0 + 1, i0]
        h11 = H[j0 + 1, i0 + 1]
        return (1 - fj) * ((1 - fi) * h00 + fi * h01) + fj * ((1 - fi) * h10 + fi * h11)

    def biome_name(self, x, z):
        i = int(min(max(round(x / self.st), 0), self.M - 1))
        j = int(min(max(round(z / self.st), 0), self.M - 1))
        return BIOMES[int(self.Bd[j, i])][0]

    def spawn_point(self):
        c = (self.M - 1) / 2.0
        jj, ii = np.indices(self.Bd.shape)
        land = self.Bd >= 2
        if land.any():
            d = np.where(land, (ii - c) ** 2 + (jj - c) ** 2, np.inf)
            j, i = np.unravel_index(int(np.argmin(d)), d.shape)
        else:
            j = i = int(c)
        x = i * self.st
        z = j * self.st
        return np.array([x, float(self.ground(x, z)) + EYE_R + 2.0, z])


class Walker:
    def __init__(self, terrain):
        self.t = terrain
        self.pos = terrain.spawn_point()
        self.vy = 0.0
        self.fly = False
        self.grounded = False

    def support(self, x, z):
        r = EYE_R * 0.5
        xs = np.append(x + r * np.cos(RING), x)
        zs = np.append(z + r * np.sin(RING), z)
        return float(self.t.ground(xs, zs).max())

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
                p[1] = max(p[1], self.support(p[0], p[2]) + EYE_R)
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
        self.vy -= GRAVITY * dt
        p[1] += self.vy * dt
        s = self.support(p[0], p[2])
        gap = p[1] - EYE_R - s
        if gap <= 0.0:
            p[1] = s + EYE_R
            self.vy = 0.0
            self.grounded = True
        else:
            self.grounded = gap < 0.12 and self.vy <= 0.0
        p[:] = self._bounded(p)


class ViewWidget(QOpenGLWidget):
    back_requested = pyqtSignal()

    def __init__(self, terrain, parent=None):
        super().__init__(parent)
        self.t = terrain
        self.walker = Walker(terrain)
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
        self._bufs = None
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
        self.menu.adjustSize()
        self.menu.move((self.width() - self.menu.width()) // 2,
                       (self.height() - self.menu.height()) // 2)
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
        self._upload()

    def _upload(self):
        v_buf, c_buf, i_buf = (int(b) for b in gl.glGenBuffers(3))
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, v_buf)
        gl.glBufferData(gl.GL_ARRAY_BUFFER, self.t.verts.nbytes, self.t.verts, gl.GL_STATIC_DRAW)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, c_buf)
        gl.glBufferData(gl.GL_ARRAY_BUFFER, self.t.colors.nbytes, self.t.colors, gl.GL_STATIC_DRAW)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, i_buf)
        gl.glBufferData(gl.GL_ELEMENT_ARRAY_BUFFER, self.t.indices.nbytes, self.t.indices, gl.GL_STATIC_DRAW)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, 0)
        self._bufs = (v_buf, c_buf, i_buf)

    def paintGL(self):
        now = time.perf_counter()
        dt = min(0.05, now - self._last)
        self._last = now
        if not self.paused:
            self._control(dt)

        p = self.walker.pos
        ratio = self.devicePixelRatioF()
        gl.glViewport(0, 0, max(1, int(self.width() * ratio)), max(1, int(self.height() * ratio)))

        if p[1] < self.t.sea_y:
            col, fog_start, fog_end = DEEP, 0.0, 55.0
        else:
            col, fog_start, fog_end = SKY, self.t.W * 0.45, self.t.W * 1.1
        gl.glClearColor(*col, 1.0)
        gl.glClear(gl.GL_COLOR_BUFFER_BIT | gl.GL_DEPTH_BUFFER_BIT)
        gl.glFogfv(gl.GL_FOG_COLOR, [*col, 1.0])
        gl.glFogf(gl.GL_FOG_START, fog_start)
        gl.glFogf(gl.GL_FOG_END, fog_end)

        aspect = max(1e-3, self.width() / max(1, self.height()))
        gl.glMatrixMode(gl.GL_PROJECTION)
        gl.glLoadIdentity()
        gluPerspective(70.0, aspect, 0.3, self.t.W * 2.5)
        gl.glMatrixMode(gl.GL_MODELVIEW)
        gl.glLoadIdentity()
        cp = math.cos(self.pitch)
        look = p + np.array([math.sin(self.yaw) * cp, math.sin(self.pitch), -math.cos(self.yaw) * cp])
        gluLookAt(p[0], p[1], p[2], look[0], look[1], look[2], 0.0, 1.0, 0.0)

        v_buf, c_buf, i_buf = self._bufs
        gl.glEnableClientState(gl.GL_VERTEX_ARRAY)
        gl.glEnableClientState(gl.GL_COLOR_ARRAY)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, v_buf)
        gl.glVertexPointer(3, gl.GL_FLOAT, 0, None)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, c_buf)
        gl.glColorPointer(3, gl.GL_FLOAT, 0, None)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, i_buf)
        gl.glDrawElements(gl.GL_TRIANGLES, self.t.indices.size, gl.GL_UNSIGNED_INT, None)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, 0)
        gl.glDisableClientState(gl.GL_COLOR_ARRAY)
        gl.glDisableClientState(gl.GL_VERTEX_ARRAY)

        W, y = self.t.W, self.t.sea_y
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
        alt = self.walker.support(p[0], p[2]) - self.t.sea_y
        hint = "Esc menu · double-tap Space toggles mode" if self.captured else "Click to look around · Esc menu"
        self.hud.setText(
            f"{mode}\n"
            f"{self.t.biome_name(p[0], p[2])}\n"
            f"x {p[0]:.0f}   z {p[2]:.0f}\n"
            f"ground {alt:+.0f} m vs sea\n"
            f"fly speed ×{self.fly_mult:.1f}\n"
            f"{hint}"
        )
        self.hud.adjustSize()


def build_terrain(path, report):
    settings, _overview, _ready, sea, _river, _done = load_world(Path(path))
    world = World(settings)
    world.sea_level = sea
    N = settings.world_w
    CH = settings.chunk
    K = settings.chunks
    H = np.empty((N, N), np.float32)
    B = np.empty((N, N), np.uint8)
    total = K * K
    n = 0
    for cy in range(K):
        for cx in range(K):
            ch = world.generate_chunk(cx, cy)
            H[cy * CH:(cy + 1) * CH, cx * CH:(cx + 1) * CH] = ch["height"]
            B[cy * CH:(cy + 1) * CH, cx * CH:(cx + 1) * CH] = ch["biome"]
            n += 1
            report(n, total)
    st = -(-N // MAX_SIDE)
    idx = np.arange(0, N, st)
    vs = N * VS_FRAC
    Hs = H[np.ix_(idx, idx)] * vs
    Bd = B[np.ix_(idx, idx)]
    return Terrain(Hs, Bd, st, vs, sea * vs)


class BuildWorker(QThread):
    progress = pyqtSignal(int, int)
    done = pyqtSignal(object)
    failed = pyqtSignal(str)

    def __init__(self, path):
        super().__init__()
        self.path = path

    def run(self):
        try:
            terrain = build_terrain(self.path, self.progress.emit)
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.done.emit(terrain)


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
        self.label = QLabel("Building terrain…")
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
        self.label.setText(f"Building terrain… chunk {n} of {total}")


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
        self.worker = None

    def open_world(self, path):
        if not path:
            return
        self.loading.set_progress(0, 1)
        self.stack.setCurrentWidget(self.loading)
        self.worker = BuildWorker(path)
        self.worker.progress.connect(self.loading.set_progress)
        self.worker.done.connect(self._show_world)
        self.worker.failed.connect(self._failed)
        self.worker.start()

    def _failed(self, msg):
        self.stack.setCurrentWidget(self.start)
        QMessageBox.warning(self, "Could not load world", msg)

    def _show_world(self, terrain):
        view = ViewWidget(terrain)
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
        self.stack.setCurrentWidget(self.start)

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
