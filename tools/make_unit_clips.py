"""Adds the consume, dance and run clips to the rigged unit models (data/model/unit/f1.glb and m1.glb).

    python tools/make_unit_clips.py                 add the clips to both models (running it again replaces them)
    python tools/make_unit_clips.py --preview DIR   also draw each clip's frames to DIR/<model>_<clip>.png (side view)

The clips are made from the models' own idle and walk clips, with turns added to named joints (the tool needs numpy and, for the
preview, Pillow). A turn is given in the model's space (x is its left, y up, z forward), added on top of what the joints above
it do, so "the forearm bends forward 90 degrees" means the same whichever way the upper arm is turned.

  consume  2 seconds, plays once (the renderer maps its time to the seconds a unit spends eating): a hand to the mouth, a few bites
  dance    2 seconds, loops: a hop and sway with the arms going up in turn and the tail swinging
  run      0.6 seconds, loops: the walk faster, wider, leaning forward
"""
import json
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
UNIT_DIR = os.path.join(HERE, '..', 'data', 'model', 'unit')
MODELS = ['f1', 'm1']
FPS = 30
NEW_CLIPS = ['consume', 'dance', 'run']


# ---------------------------------------------------------------- quaternions (x, y, z, w)
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz])


def qconj(a):
    return np.array([-a[0], -a[1], -a[2], a[3]])


def qnorm(a):
    n = np.linalg.norm(a)
    return a / n if n > 1e-9 else np.array([0.0, 0.0, 0.0, 1.0])


def qaxis(axis, degrees):
    h = math.radians(degrees) * 0.5
    s = math.sin(h)
    return np.array([axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h)])


def rx(d): return qaxis((1, 0, 0), d)
def ry(d): return qaxis((0, 1, 0), d)
def rz(d): return qaxis((0, 0, 1), d)


def slerp(a, b, t):
    d = float(np.dot(a, b))
    if d < 0:
        b = -b
        d = -d
    if d > 0.9995:
        return qnorm(a + (b - a) * t)
    th = math.acos(max(-1.0, min(1.0, d)))
    return (a * math.sin((1 - t) * th) + b * math.sin(t * th)) / math.sin(th)


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


# ---------------------------------------------------------------- the file
class Glb:
    def __init__(self, path):
        d = open(path, 'rb').read()
        n = struct.unpack('<I', d[12:16])[0]
        self.j = json.loads(d[20:20 + n])
        m = struct.unpack('<I', d[20 + n:24 + n])[0]
        self.bin = bytearray(d[28 + n:28 + n + m])
        j = self.j
        self.skin = j['skins'][0]
        self.joints = self.skin['joints']
        self.names = [j['nodes'][i]['name'] for i in self.joints]
        self.index = {nm: k for k, nm in enumerate(self.names)}
        node_parent = {}
        for i, nd in enumerate(j['nodes']):
            for c in nd.get('children', []):
                node_parent[c] = i
        self.parent = [self.joints.index(node_parent[i]) if node_parent.get(i) in self.joints else -1 for i in self.joints]
        self.rest_t = [np.array(j['nodes'][i].get('translation', [0, 0, 0]), dtype=float) for i in self.joints]
        self.rest_r = [np.array(j['nodes'][i].get('rotation', [0, 0, 0, 1]), dtype=float) for i in self.joints]
        self._drop_old_clips()

    def _drop_old_clips(self):
        j = self.j
        old = [a for a in j.get('animations', []) if a['name'] in NEW_CLIPS]
        if not old:
            return
        first_acc = min(min(s['input'], s['output']) for a in old for s in a['samplers'])
        first_view = j['accessors'][first_acc]['bufferView']
        cut = j['bufferViews'][first_view]['byteOffset']
        j['animations'] = [a for a in j['animations'] if a['name'] not in NEW_CLIPS]
        del j['accessors'][first_acc:]
        del j['bufferViews'][first_view:]
        del self.bin[cut:]
        j['buffers'][0]['byteLength'] = len(self.bin)

    def read(self, acc):
        a = self.j['accessors'][acc]
        v = self.j['bufferViews'][a['bufferView']]
        comps = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        arr = np.frombuffer(self.bin, dtype=dt, count=a['count'] * comps, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0))
        return arr.reshape(a['count'], comps).astype(np.float64)

    def clip(self, name):
        """{joint: (times, quats)} and the hips' (times, translations)"""
        for a in self.j['animations']:
            if a['name'] != name:
                continue
            rots, trans = {}, {}
            for ch in a['channels']:
                s = a['samplers'][ch['sampler']]
                joint = self.joints.index(ch['target']['node'])
                (rots if ch['target']['path'] == 'rotation' else trans)[joint] = (self.read(s['input'])[:, 0], self.read(s['output']))
            return rots, trans
        raise KeyError(name)

    def add_clip(self, name, times, rot_keys, hip_keys):
        """rot_keys: {joint: [quat per frame]}, hip_keys: [translation per frame]"""
        j = self.j

        def put(arr, kind, count, comps, lo=None, hi=None):
            while len(self.bin) % 4:
                self.bin.append(0)
            data = np.asarray(arr, dtype=np.float32).tobytes()
            j['bufferViews'].append({'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)})
            self.bin.extend(data)
            acc = {'bufferView': len(j['bufferViews']) - 1, 'componentType': 5126, 'count': count, 'type': kind}
            if lo is not None:
                acc['min'], acc['max'] = [float(x) for x in lo], [float(x) for x in hi]
            j['accessors'].append(acc)
            return len(j['accessors']) - 1

        t_acc = put(times, 'SCALAR', len(times), 1, [times[0]], [times[-1]])
        samplers, channels = [], []
        for joint in sorted(rot_keys):
            out = put(np.array(rot_keys[joint]), 'VEC4', len(times), 4)
            samplers.append({'input': t_acc, 'output': out, 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': self.joints[joint], 'path': 'rotation'}})
        out = put(np.array(hip_keys), 'VEC3', len(times), 3)
        samplers.append({'input': t_acc, 'output': out, 'interpolation': 'LINEAR'})
        channels.append({'sampler': len(samplers) - 1, 'target': {'node': self.joints[self.index['hips']], 'path': 'translation'}})
        j.setdefault('animations', []).append({'name': name, 'samplers': samplers, 'channels': channels})

    def save(self, path):
        j = self.j
        while len(self.bin) % 4:
            self.bin.append(0)
        j['buffers'][0]['byteLength'] = len(self.bin)
        js = json.dumps(j, separators=(',', ':')).encode()
        js += b' ' * ((4 - len(js) % 4) % 4)
        out = bytearray(b'glTF' + struct.pack('<II', 2, 0))
        out += struct.pack('<I', len(js)) + b'JSON' + js
        out += struct.pack('<I', len(self.bin)) + b'BIN\x00' + bytes(self.bin)
        out[8:12] = struct.pack('<I', len(out))
        open(path, 'wb').write(out)


def sample(clip, joint, t, default):
    times, vals = clip[joint]
    dur = times[-1]
    t = t % dur if dur > 0 else 0.0
    k = int(np.searchsorted(times, t, side='right')) - 1
    k = max(0, min(k, len(times) - 2))
    span = times[k + 1] - times[k]
    f = (t - times[k]) / span if span > 1e-9 else 0.0
    if vals.shape[1] == 4:
        return slerp(vals[k], vals[k + 1], f)
    return vals[k] + (vals[k + 1] - vals[k]) * f


def mean_quat(clip, joint):
    q0 = clip[joint][1][0]
    acc = np.zeros(4)
    for q in clip[joint][1]:
        acc += q if np.dot(q, q0) >= 0 else -q
    return qnorm(acc)


# ---------------------------------------------------------------- posing
def apply_turns(g, base, turns):
    """base: a local rotation per joint; turns: {joint name: quat in model space}. Returns the new local rotations."""
    n = len(g.joints)
    new = [None] * n
    glob = [None] * n
    for k in range(n):   # parents come before their children in a glTF skin
        p = g.parent[k]
        pg = glob[p] if p >= 0 else np.array([0.0, 0.0, 0.0, 1.0])
        turn = turns.get(g.names[k])
        local = base[k]
        if turn is not None:
            local = qmul(qconj(pg), qmul(turn, qmul(pg, base[k])))
        new[k] = qnorm(local)
        glob[k] = qnorm(qmul(pg, new[k]))
    return new


def frames(g, count, dur, make):
    """make(t) -> (base rotations, hips translation, turns). Returns times, {joint: [quats]}, [hips translations]."""
    times = [dur * i / count for i in range(count)] + [dur]
    rot = {k: [] for k in range(len(g.joints)) if g.names[k] != 'root'}
    hips = []
    for t in times:
        base, hip, turns = make(t)
        local = apply_turns(g, base, turns)
        for k in rot:
            rot[k].append(local[k])
        hips.append(hip)
    return times, rot, hips


def build_clips(g):
    idle_r, idle_t = g.clip('idle')
    walk_r, walk_t = g.clip('walk')
    hips = g.index['hips']
    idle_hip0 = sample(idle_t, hips, 0.0, None)
    clips = {}

    def rest_base():   # the first frame of the idle: where a unit stands before and after
        return [sample(idle_r, k, 0.0, None) if k in idle_r else g.rest_r[k] for k in range(len(g.joints))]

    # ---- consume: a hand to the mouth, a few bites
    def consume(t):
        u = t / 2.0
        env = smooth(u / 0.22) * (1 - smooth((u - 0.78) / 0.22))
        bite = 0.5 - 0.5 * math.cos(2 * math.pi * 3 * u)   # three bites
        # the arm and head are worked out so the free hand ends in front of the mouth (the kobold's arm is short and its head large,
        # so the head bows to meet it and the body leans in)
        turns = {
            'upper_arm_L': qmul(ry(-55 * env), qmul(rz(26 * env), rx((-110 - 10 * bite) * env))),
            'forearm_L': qmul(rz(-18 * env), rx(-4 * env)),
            'spine': rx(10 * env),
            'chest': rx(6 * env),
            'head': rx((12 + 6 * bite) * env),
        }
        return rest_base(), idle_hip0 + np.array([0.0, -0.01 * env, 0.0]), turns
    clips['consume'] = frames(g, 60, 2.0, consume)

    # ---- dance: hop and sway, arms up in turn, the tail swinging
    def dance(t):
        u = t / 2.0
        s1, s2 = math.sin(2 * math.pi * u), math.sin(4 * math.pi * u)
        c2 = math.cos(4 * math.pi * u)
        left_up, right_up = 0.5 + 0.5 * s1, 0.5 - 0.5 * s1
        lift_l, lift_r = max(0.0, s1), max(0.0, -s1)
        turns = {
            'hips': qmul(rz(7 * s1), ry(14 * s1)),
            'spine': qmul(ry(-9 * s1), rz(-4 * s1)),
            'chest': rz(-5 * s1),
            'head': qmul(rz(9 * s2), ry(-6 * s1)),
            'upper_arm_L': qmul(rz(125 * left_up), rx(-10)),
            'forearm_L': qmul(rz(18 * s2), rx(-25 * left_up)),
            'upper_arm_R': qmul(rz(-60 * right_up), rx(-25 - 15 * right_up)),   # the spear arm goes up less, or the spear sweeps across the face
            'forearm_R': qmul(rz(-10 * s2), rx(-20 * right_up)),
            'thigh_L': rx(-30 * lift_l),
            'shin_L': rx(40 * lift_l),
            'thigh_R': rx(-30 * lift_r),
            'shin_R': rx(40 * lift_r),
        }
        for i, nm in enumerate(['tail1', 'tail2', 'tail3', 'tail4']):
            turns[nm] = ry(16 * math.sin(2 * math.pi * u * 2 + 0.7 * i))
        hip = idle_hip0 + np.array([0.04 * s1, -0.045 * (0.5 + 0.5 * c2), 0.0])
        return rest_base(), hip, turns
    clips['dance'] = frames(g, 60, 2.0, dance)

    # ---- run: the walk made faster and wider, leaning forward
    means = {k: mean_quat(walk_r, k) for k in walk_r}
    hip_mean = np.mean(walk_t[hips][1], axis=0)
    amplify = {'thigh_L': 1.7, 'thigh_R': 1.7, 'shin_L': 1.6, 'shin_R': 1.6, 'foot_L': 1.4, 'foot_R': 1.4,
               'upper_arm_L': 1.6, 'upper_arm_R': 1.6, 'forearm_L': 1.4, 'forearm_R': 1.4}

    def run(t):
        wt = (t / 0.6) * walk_r[hips][0][-1]
        base = []
        for k in range(len(g.joints)):
            q = sample(walk_r, k, wt, None) if k in walk_r else g.rest_r[k]
            f = amplify.get(g.names[k])
            if f and k in means:
                q = slerp(means[k], q, f)
            base.append(q)
        hip = sample(walk_t, hips, wt, None)
        hip = hip_mean + (hip - hip_mean) * np.array([1.0, 1.7, 1.0])
        turns = {'spine': rx(11), 'chest': rx(8), 'head': rx(-14),
                 'forearm_L': rx(-38), 'forearm_R': rx(-38),
                 'tail1': rx(-8), 'tail2': rx(-6)}
        return base, hip, turns
    clips['run'] = frames(g, 18, 0.6, run)
    return clips


# ---------------------------------------------------------------- preview
def preview(g, clips, path, label):
    from PIL import Image, ImageDraw
    j = g.j
    prim = j['meshes'][0]['primitives'][0]
    pos = g.read(prim['attributes']['POSITION'])
    jts = g.read(prim['attributes']['JOINTS_0']).astype(int)
    wts = g.read(prim['attributes']['WEIGHTS_0'])
    idx = g.read(prim['indices']).astype(int).reshape(-1)
    inv = g.read(g.skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)   # glTF is column-major
    col = None
    if 'COLOR_0' in prim['attributes']:
        col = g.read(prim['attributes']['COLOR_0'])
    n = len(g.joints)

    def pose_mats(rots, hip):
        mats = [None] * n
        for k in range(n):
            r = rots.get(k, g.rest_r[k])
            m = np.eye(4)
            m[:3, :3] = qmat(r)
            m[:3, 3] = hip if g.names[k] == 'hips' else g.rest_t[k]
            p = g.parent[k]
            mats[k] = (mats[p] @ m) if p >= 0 else m
        return mats

    tris = idx.reshape(-1, 3)
    out = []
    for name, (times, rot, hips) in clips.items():
        picks = list(range(0, len(times) - 1, max(1, (len(times) - 1) // 8)))[:6]
        W = 230
        sheet = Image.new('RGB', (W * len(picks), 2 * 340), (40, 44, 52))
        dr = ImageDraw.Draw(sheet)
        for col_i, f in enumerate(picks):
            mats = pose_mats({k: rot[k][f] for k in rot}, hips[f])
            skin = [mats[k] @ inv[k] for k in range(n)]
            P = np.zeros((len(pos), 3))
            for c in range(4):
                M = np.array([skin[k] for k in jts[:, c]])
                P += wts[:, c:c + 1] * (M[:, :3, :3] @ pos.T.T[:, :, None])[:, :, 0] + wts[:, c:c + 1] * M[:, :3, 3]
            nrm = np.cross(P[tris[:, 1]] - P[tris[:, 0]], P[tris[:, 2]] - P[tris[:, 0]])
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
            for row, (hx, depth) in enumerate([(2, 0), (0, 2)]):   # side view (z across), front view (x across)
                sc = 210.0
                ox = col_i * W + W / 2
                oy = row * 340 + 325
                xy = np.stack([ox + P[:, hx] * sc, oy - P[:, 1] * sc], 1)
                z = P[:, depth]
                light = np.array([-1.0, 0.5, 0.3]) if row == 0 else np.array([0.3, 0.5, 1.0])
                order = np.argsort([z[t].mean() * (-1 if row == 0 else 1) for t in tris])   # far ones first
                for ti in order:
                    t = tris[ti]
                    shade = 70 + int(150 * max(0.0, float(nrm[ti] @ (light / np.linalg.norm(light)))))
                    dr.polygon([tuple(xy[v]) for v in t], fill=(shade, shade, shade))
            dr.text((col_i * W + 4, 2), f'{label} {name} {times[f]:.2f}s', fill=(255, 255, 255))
        out.append((name, sheet))
    for name, sheet in out:
        sheet.save(os.path.join(path, f'{label}_{name}.png'))


def main():
    args = sys.argv[1:]
    prev = args[args.index('--preview') + 1] if '--preview' in args else None
    only_preview = '--no-write' in args
    for model in MODELS:
        path = os.path.join(UNIT_DIR, model + '.glb')
        g = Glb(path)
        clips = build_clips(g)
        for name in NEW_CLIPS:
            times, rot, hips = clips[name]
            g.add_clip(name, times, rot, hips)
        if prev:
            os.makedirs(prev, exist_ok=True)
            preview(g, clips, prev, model)
        if not only_preview:
            g.save(path)
            print(model, 'clips:', [a['name'] for a in g.j['animations']], os.path.getsize(path), 'bytes')


if __name__ == '__main__':
    main()
