"""Worldmaker 3D view: the whole map as one mesh, drawn with OpenGL 2.1 shaders."""

from __future__ import annotations

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

from wm_core import BIOMES, BIOME_RGB, VS_FRAC, clamp, sky_state

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
uniform vec3 uRock; uniform float uRockSlope;
varying vec3 vPos; varying vec3 vCol; varying vec3 vNrm;
void main() {
    vec3 N = normalize(vNrm);
    float steep = 1.0 - N.y;
    float rock = smoothstep(uRockSlope - 0.08, uRockSlope + 0.08, steep);
    vec3 alb = mix(vCol, uRock, rock);
    float diff = max(dot(N, uSun), 0.0) * uSunK;
    vec3 lit = alb * (uAmb + diff * uSunRGB);
    float d = length(vPos - uCam);
    float f = clamp(1.0 - exp(-pow(d * uFogDen, 2.0)), 0.0, 1.0);
    gl_FragColor = vec4(mix(lit, uFog, f), 1.0);
}
"""

CLOUD_FS = """
#version 120
uniform float uTime; uniform float uCover; uniform float uOpacity; uniform vec2 uWind;
uniform float uSeed; uniform vec3 uLit; uniform vec3 uCloudRGB;
uniform vec3 uFog; uniform float uFogDen; uniform vec3 uCam;
varying vec3 vPos;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0; float a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.3); a *= 0.5; }
    return s;
}
void main() {
    vec2 p = vPos.xz * 0.012 + uWind * uTime + vec2(uSeed * 7.7, uSeed * 3.1);
    float d = fbm(p);
    float thr = mix(0.78, 0.42, uCover);
    float a = smoothstep(thr, thr + 0.15, d) * uOpacity;
    if (a < 0.01) discard;
    vec3 col = uCloudRGB * uLit;
    float dist = length(vPos - uCam);
    float f = clamp(1.0 - exp(-pow(dist * uFogDen, 2.0)), 0.0, 1.0);
    gl_FragColor = vec4(mix(col, uFog, f), a * (1.0 - 0.5 * f));
}
"""

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


LOD_NEAR = 160.0   # tiles: blocks this close to the player draw at full detail
LOD_STEP = 4       # tiles between vertices in the distant, coarse mesh


def block_size(W, H):
    """Tiles per block side: the map is cut into about 16 x 16 blocks, at least 32 tiles wide."""
    return max(32, 1 << int(math.ceil(math.log2(max(W, H) / 16.0))))


def _quads(y0, y1, x0, x1, W, step):
    rows = np.unique(np.r_[np.arange(y0, y1, step), y1])
    cols = np.unique(np.r_[np.arange(x0, x1, step), x1])
    G = rows[:, None] * W + cols[None, :]
    a = G[:-1, :-1].ravel(); b = G[:-1, 1:].ravel()
    c = G[1:, :-1].ravel(); d = G[1:, 1:].ravel()
    return np.concatenate([np.stack([a, c, b], 1).ravel(), np.stack([b, c, d], 1).ravel()])


def build_lod_index(W, H):
    """One index buffer holding every block twice: at full detail, then coarse.
    Returns the buffer, the block centres, and where each block's two pieces start and how long they are."""
    B = block_size(W, H)
    full, far, centres = [], [], []
    for y0 in range(0, H - 1, B):
        for x0 in range(0, W - 1, B):
            y1 = min(H - 1, y0 + B)
            x1 = min(W - 1, x0 + B)
            full.append(_quads(y0, y1, x0, x1, W, 1))
            far.append(_quads(y0, y1, x0, x1, W, LOD_STEP))
            centres.append(((x0 + x1) / 2.0, (y0 + y1) / 2.0))
    fl = np.array([p.size for p in full], np.int64)
    fa = np.array([p.size for p in far], np.int64)
    fst = np.concatenate([[0], np.cumsum(fl)[:-1]])
    cst = int(fl.sum()) + np.concatenate([[0], np.cumsum(fa)[:-1]])
    idx = np.concatenate(full + far).astype(np.uint32)
    return idx, np.array(centres), fst, fl, cst, fa


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
        self.set_world(ed.h, ed.b, ed.river, ed.sea, ed.version)

    def set_world(self, h, b, river, sea, version):
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
        col = BIOME_RGB[b] / 255.0
        col[river] = (0.27, 0.59, 0.84)
        idx, *self.blk = build_lod_index(self.W, self.H)
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
        self.pos = np.array([float(i), self.ground(i, j) + EYE, float(j)])

    def ground(self, x, z):
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
        if self.fly:
            cp = math.cos(self.pitch)
            fwd = np.array([math.sin(yaw) * cp, math.sin(self.pitch), -math.cos(yaw) * cp])
            right = np.array([math.cos(yaw), 0.0, math.sin(yaw)])
            up = ((K["Space"] in k) or (K["E"] in k)) - ((K["C"] in k) or (K["Q"] in k))
            v = fwd * f + right * r + np.array([0.0, float(up), 0.0])
            n = np.linalg.norm(v)
            if n > 0:
                self.pos += v / n * FLY_SPEED * self.speed * boost * dt
            self.pos[1] = clamp(self.pos[1], -self.vs, 6 * self.vs)
            return
        v = np.array([math.sin(yaw), -math.cos(yaw)]) * f + np.array([math.cos(yaw), math.sin(yaw)]) * r
        n = np.linalg.norm(v)
        if n > 0:
            v = v / n * WALK_SPEED * self.speed * boost * dt
            self.pos[0] += v[0]
            self.pos[2] += v[1]
        self.pos[0] = clamp(self.pos[0], 0.0, self.W - 1.0)
        self.pos[2] = clamp(self.pos[2], 0.0, self.H - 1.0)
        g = self.ground(self.pos[0], self.pos[2]) + EYE
        if self.jump and self.grounded:
            self.vy = JUMP_V
        self.jump = False
        self.vy -= GRAVITY * dt
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
            look.clock = (look.clock + dt * 24.0 / max(1.0, look.day_length)) % 24.0
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
        gl.glMatrixMode(gl.GL_PROJECTION)
        gl.glLoadIdentity()
        gluPerspective(65.0, max(1e-3, self.width() / max(1, self.height())), 0.5, far)
        gl.glMatrixMode(gl.GL_MODELVIEW)
        gl.glLoadIdentity()
        gluLookAt(*eye, *target, 0.0, 1.0, 0.0)
        self._draw_sky(sk)
        self._draw_sun(sk, eye, far)
        self._draw_terrain(sk, eye, far)
        self._draw_water()
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
        gl.glColor4f(float(rgb[0]), float(rgb[1]), float(rgb[2]), float(alpha))
        gl.glBegin(gl.GL_TRIANGLE_FAN)
        gl.glVertex3f(*c)
        for k in range(25):
            a = 2 * math.pi * k / 24
            p = c + r * (math.cos(a) * t + math.sin(a) * b)
            gl.glVertex3f(*p)
        gl.glEnd()
        gl.glDepthMask(gl.GL_TRUE)
        gl.glDisable(gl.GL_BLEND)

    def _draw_terrain(self, sk, eye, far):
        prog = self.progs.get("terrain")
        if prog is None or not self.bufs:
            return
        look = self.look
        M = max(self.W, self.H)
        fog_den = 1.27 / M * look.fog
        sun = sk["sun"]
        gl.glUseProgram(prog)
        self._u3(prog, "uSun", sun); self._u1(prog, "uSunK", sk["sun_k"]); self._u1(prog, "uAmb", sk["amb"])
        self._u3(prog, "uSunRGB", look.sun_rgb); self._u3(prog, "uFog", sk["horizon"])
        self._u1(prog, "uFogDen", fog_den); self._u3(prog, "uCam", eye)
        self._u3(prog, "uRock", look.rock_rgb); self._u1(prog, "uRockSlope", look.rock_slope)
        pos, col, nrm, idx = self.bufs
        for i, (buf, size) in enumerate(((pos, 3), (col, 3), (nrm, 3))):
            gl.glEnableVertexAttribArray(i)
            gl.glBindBuffer(gl.GL_ARRAY_BUFFER, buf)
            gl.glVertexAttribPointer(i, size, gl.GL_FLOAT, gl.GL_FALSE, 0, None)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, idx)
        cen, fst, fl, cst, fa = self.blk
        if self.interactive:
            d = np.hypot(cen[:, 0] - eye[0], cen[:, 1] - eye[2])
            near = d < LOD_NEAR + block_size(self.W, self.H)
        else:
            near = np.ones(len(cen), bool)
        for k in range(len(cen)):
            if near[k]:
                start, count = fst[k], fl[k]
            else:
                start, count = cst[k], fa[k]
            gl.glDrawElements(gl.GL_TRIANGLES, int(count), gl.GL_UNSIGNED_INT, c_void_p(int(start) * 4))
        for i in range(3):
            gl.glDisableVertexAttribArray(i)
        gl.glBindBuffer(gl.GL_ARRAY_BUFFER, 0)
        gl.glBindBuffer(gl.GL_ELEMENT_ARRAY_BUFFER, 0)
        gl.glUseProgram(0)

    def _draw_water(self):
        y = self.sea * self.vs
        r, g, b = self.look.water_rgb
        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)
        gl.glDepthMask(gl.GL_FALSE)
        gl.glColor4f(float(r), float(g), float(b), float(self.look.water_alpha))
        W, H = float(self.W), float(self.H)
        gl.glBegin(gl.GL_QUADS)
        gl.glVertex3f(0.0, y, 0.0); gl.glVertex3f(W, y, 0.0)
        gl.glVertex3f(W, y, H); gl.glVertex3f(0.0, y, H)
        gl.glEnd()
        gl.glDepthMask(gl.GL_TRUE)
        gl.glDisable(gl.GL_BLEND)

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
        self._u1(prog, "uTime", t)
        self._u1(prog, "uCover", look.cloud_cover)
        self._u1(prog, "uOpacity", look.cloud_opacity)
        self._u2(prog, "uWind", look.cloud_speed * 0.02, look.cloud_speed * 0.008)
        self._u1(prog, "uSeed", look.seed % 997)
        self._u3(prog, "uLit", lit)
        self._u3(prog, "uCloudRGB", look.cloud_rgb)
        self._u3(prog, "uFog", sk["horizon"])
        self._u1(prog, "uFogDen", 1.27 / max(self.W, self.H) * look.fog)
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

