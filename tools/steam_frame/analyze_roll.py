#!/usr/bin/env python3
"""Does the HUD stay level when the head rolls? Takes dump directories made with different diag_roll values,
each as a pair: the picture with the HUD (ON) and the same with `hud_draw 0` (OFF), so the HUD is exactly what
differs between them (hands, torch and lamps are in both). It reports, for each roll against the first pair
(the reference, normally roll 0), how far the world turned and how far the HUD turned, both about the eye's
optical axis. In the eye image the head's roll turns the world, and a stabilised HUD has to turn with it
(a HUD that is head-locked does not turn at all).

  analyze_roll.py REF_ON REF_OFF  ROLL_ON ROLL_OFF  [ROLL_ON ROLL_OFF ...]

Needs numpy and pillow (a venv: python3 -m venv /tmp/v && /tmp/v/bin/pip install numpy pillow).
"""
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load(d, eye=0):
    b = Path(d, f"vr-eye{eye}.bmp").read_bytes()
    w, h = struct.unpack("<ii", b[18:26])
    off = struct.unpack("<I", b[10:14])[0]
    stride = (w * 3 + 3) & ~3
    a = np.frombuffer(b, dtype=np.uint8, count=stride * h, offset=off).reshape(h, stride)[:, : w * 3].reshape(h, w, 3)
    return Image.fromarray(a[::-1, :, ::-1].copy())


def state(d):
    t = Path(d, "vr-state.txt").read_text()
    m = re.search(r"fov_eye0 L (\S+) R (\S+) U (\S+) D (\S+)", t)
    l, r, u, dn = (float(x) for x in m.groups())
    import math
    size = int(re.search(r"eye_size (\d+)x", t).group(1))
    ax = -math.tan(l) / (math.tan(r) - math.tan(l)) * size
    ay = math.tan(u) / (math.tan(u) - math.tan(dn)) * size
    roll = float(re.search(r"diag_roll (\S+)", t).group(1))
    view = re.search(r"view_roll (\S+)", t)
    return ax, ay, roll, float(view.group(1)) if view else None, size


def small(img, f):
    return img.resize((img.width // f, img.height // f), Image.BILINEAR)


def best_angle(ref, other, centre, mask, span=40):
    """the rotation of ref (degrees, counter-clockwise on screen) that best matches other where mask is true"""
    ref_a = np.asarray(ref, dtype=np.float32)
    oth = np.asarray(other, dtype=np.float32)
    best = None
    for tenth in range(-span * 10, span * 10 + 1, 5):
        a = tenth / 10.0
        rot = np.asarray(ref.rotate(a, resample=Image.BILINEAR, center=centre), dtype=np.float32)
        e = float(np.mean(np.abs(rot - oth)[mask]))
        if best is None or e < best[0]:
            best = (e, a)
    return best[1], best[0]


def hud_mask(on, off, h, w):
    d = np.abs(np.asarray(on, dtype=np.float32) - np.asarray(off, dtype=np.float32)).sum(axis=2)
    return Image.fromarray(((d > 150) * 255).astype(np.uint8))


def main():
    dirs = sys.argv[1:]
    if len(dirs) < 4 or len(dirs) % 2:
        print(__doc__)
        return
    f = 4
    ax, ay, roll0, view0, size = state(dirs[0])
    ref = small(load(dirs[0]), f)
    h, w = np.asarray(ref).shape[:2]
    ref_mask = hud_mask(ref, small(load(dirs[1]), f), h, w)
    print(f"reference {dirs[0]}: diag_roll {roll0}, view roll {view0}, {int(np.asarray(ref_mask).astype(bool).sum())} HUD px")
    centre = (ax / f, ay / f)
    for i in range(2, len(dirs), 2):
        d_on, d_off = dirs[i], dirs[i + 1]
        _, _, roll, view, _ = state(d_on)
        img = small(load(d_on), f)
        mask = hud_mask(img, small(load(d_off), f), h, w)

        # the world: the upper middle, away from the HUD, hands and weapon
        world = np.zeros((h, w), dtype=bool)
        world[int(h * 0.12): int(h * 0.50), int(w * 0.22): int(w * 0.78)] = True
        wa, we = best_angle(ref, img, centre, world)

        best = None
        mimg = np.asarray(mask, dtype=np.float32) / 255.0
        for tenth in range(-400, 401, 5):
            a = tenth / 10.0
            rot = np.asarray(ref_mask.rotate(a, resample=Image.BILINEAR, center=centre), dtype=np.float32) / 255.0
            score = float((rot * mimg).sum())
            if best is None or score > best[0]:
                best = (score, a)
        print(f"{d_on}: diag_roll {roll}, view roll {view}: the world turned {wa:+.1f} deg (match error {we:.1f}), "
              f"the HUD turned {best[1]:+.1f} deg ({int(mimg.sum())} HUD px)")


if __name__ == "__main__":
    main()
