#!/usr/bin/env python3
"""What the compositor shows for the main menu: the screen quad (the eye 0 dump as its picture), then the
menu laser's beam and dot quads over it, in the order the app submits them (layer_order in vr-state.txt),
seen from the left eye. The runtime composites for real; this models it from the poses the app logged, so
it checks the maths and the order, not SteamVR. Dev tool.

  composite_menu.py DUMP_DIR OUT.png

Needs numpy and pillow (a venv: python3 -m venv /tmp/v && /tmp/v/bin/pip install numpy pillow).
"""
import math
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

BEAM_COLOUR = (0.15, 0.75, 1.0)  # what TBXR_PaintPointerTexture clears the pointer swapchain to (sRGB)


def load_bmp(d):
    b = Path(d, "vr-eye0.bmp").read_bytes()
    w, h = struct.unpack("<ii", b[18:26])
    off = struct.unpack("<I", b[10:14])[0]
    stride = (w * 3 + 3) & ~3
    a = np.frombuffer(b, dtype=np.uint8, count=stride * h, offset=off).reshape(h, stride)[:, : w * 3].reshape(h, w, 3)
    return a[::-1, :, ::-1].copy()


def rotate(q, v):
    """rotate vectors v (..., 3) by quaternion q (x, y, z, w)"""
    x, y, z, w = q
    u = np.array([x, y, z])
    return v + 2.0 * np.cross(u, np.cross(u, v) + w * v)


def conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def vec(text, key, n):
    m = re.search(key + r" (.*)", text)
    return [float(x) for x in re.findall(r"-?\d+\.\d+|-?\d+", m.group(1))][:n]


def layer(text, name):
    m = re.search(r"layer_%s_pose (\S+) (\S+) (\S+) quat (\S+) (\S+) (\S+) (\S+) size (\S+) (\S+)" % name, text)
    if not m:
        return None
    v = [float(x) for x in m.groups()]
    return np.array(v[0:3]), tuple(v[3:7]), (v[7], v[8])


def hit_quad(origin, dirs, pos, quat, size):
    """ray/quad: returns u (0..1 across, left to right), v (0..1 down), t, inside (all per ray)"""
    normal = rotate(quat, np.array([0.0, 0.0, 1.0]))
    denom = dirs @ normal
    with np.errstate(divide="ignore", invalid="ignore"):
        t = ((pos - origin) @ normal) / denom
    p = origin + dirs * t[..., None]
    local = rotate(conj(quat), p - pos)
    u = local[..., 0] / size[0] + 0.5
    v = 0.5 - local[..., 1] / size[1]
    inside = (t > 0) & (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
    return u, v, t, inside


def main():
    d, out = sys.argv[1], sys.argv[2]
    text = Path(d, "vr-state.txt").read_text()
    menu = load_bmp(d)
    size = menu.shape[0]
    head = np.array(vec(text, "head_position", 3))
    eye = head + np.array(vec(text, "eye0_in_head_mm", 3)) / 1000.0
    l, r, u_, dn = vec(text, "fov_eye0 L", 4)
    n = 1080
    xs = (np.arange(n) + 0.5) / n
    tx = math.tan(l) + xs * (math.tan(r) - math.tan(l))
    ty = math.tan(u_) + xs * (math.tan(dn) - math.tan(u_))
    dirs = np.stack(np.broadcast_arrays(tx[None, :], ty[:, None], -np.ones((n, n))), axis=-1)
    dirs = dirs / np.linalg.norm(dirs, axis=-1, keepdims=True)
    img = np.zeros((n, n, 3), np.float32)
    order = re.search(r"layer_order (.*)", text).group(1).split()
    colour = np.array(BEAM_COLOUR) * 255.0
    drawn = {}
    for name in order:
        lay = layer(text, name)
        if lay is None:
            drawn[name] = "none"
            continue
        pos, quat, sz = lay
        u, v, t, inside = hit_quad(eye, dirs, pos, quat, sz)
        if name == "screen":
            px = np.clip((u * (size - 1)).astype(int), 0, size - 1)
            py = np.clip((v * (size - 1)).astype(int), 0, size - 1)
            img[inside] = menu[py[inside], px[inside]]
        else:
            img[inside] = colour
        drawn[name] = int(inside.sum())
    pic = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    pic.save(out)
    # a crop around the dot, enlarged, for the eye
    dot = layer(text, "dot")
    if dot is not None:
        rel = dot[0] - eye
        fx = rel[0] / -rel[2]
        fy = rel[1] / -rel[2]
        px = (fx - math.tan(l)) / (math.tan(r) - math.tan(l)) * n
        py = (fy - math.tan(u_)) / (math.tan(dn) - math.tan(u_)) * n
        c = 90
        box = (int(px) - c, int(py) - c, int(px) + c, int(py) + c)
        pic.crop(box).resize((c * 6, c * 6), Image.NEAREST).save(out.replace(".png", "-dot.png"))
        print("dot at pixel %.0f, %.0f of %d" % (px, py, n))
    print("layers in order %s, pixels covered: %s" % (" -> ".join(order), drawn))


main()
