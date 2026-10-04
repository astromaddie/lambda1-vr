#!/usr/bin/env python3
"""Stereo numbers from a dump directory (vr-eye0.bmp, vr-eye1.bmp, vr-state.txt written by dump_frame).

Block-matches the right eye against the left along rows and reports the disparity between the optical
axes, the way a viewer's eyes would see it (positive: nearer than infinity). The asymmetric-frustum
offset between the two eye images is taken out first, from the fov in vr-state.txt.

  analyze_stereo.py DIR                       floor model and a few regions
  analyze_stereo.py DIR --box x0 y0 x1 y1     disparity of one box (pixels of the left eye image)

Needs numpy and pillow (a venv: python3 -m venv /tmp/v && /tmp/v/bin/pip install numpy pillow).
The floor model: for a flat floor h below the eyes, the disparity at a row y is ipd * (y - yaxis) / h, so a fit
over the floor rows gives h (it should be the eye height) whatever the texture is.
"""
import math
import re
import struct
import sys
from pathlib import Path

import numpy as np


def load_bmp(path):
    d = Path(path).read_bytes()
    w, h = struct.unpack("<ii", d[18:26])
    off = struct.unpack("<I", d[10:14])[0]
    stride = (w * 3 + 3) & ~3
    a = np.frombuffer(d, dtype=np.uint8, count=stride * h, offset=off).reshape(h, stride)[:, : w * 3].reshape(h, w, 3)
    return a[::-1, :, ::-1].astype(np.float32)  # top row first, RGB


def luma(a):
    return a @ np.array([0.299, 0.587, 0.114], dtype=np.float32)


def parse_state(path):
    t = Path(path).read_text()
    st = {}
    for eye in (0, 1):
        m = re.search(rf"fov_eye{eye} L (\S+) R (\S+) U (\S+) D (\S+)", t)
        st[f"fov{eye}"] = tuple(float(x) for x in m.groups())
        m = re.search(rf"eye{eye}_in_head_mm (\S+) (\S+) (\S+)", t)
        st[f"pos{eye}"] = tuple(float(x) / 1000.0 for x in m.groups()) if m else None
    m = re.search(r"eye_size (\d+)x(\d+)", t)
    st["size"] = (int(m.group(1)), int(m.group(2)))
    return st


def axis_px(fov, size):
    l, r, u, d = fov
    w, h = size
    return (-math.tan(l) / (math.tan(r) - math.tan(l)) * w, math.tan(u) / (math.tan(u) - math.tan(d)) * h)


def focal(fov, size):
    l, r, u, d = fov
    return size[0] / (math.tan(r) - math.tan(l)), size[1] / (math.tan(u) - math.tan(d))


def match_row_blocks(g0, g1, y0, y1, x0, x1, centre=0, search=110, block=33, predict=None):
    """For each block in the left image find the shift s (x1 = x0 + s) that best matches the right image."""
    res = []
    half = block // 2
    for y in range(y0 + half, y1 - half, block):
        for x in range(x0 + half, x1 - half, block):
            patch = g0[y - half : y + half + 1, x - half : x + half + 1]
            if patch.std() < 6.0:
                continue
            best, bs = None, 0
            c = centre if predict is None else int(round(centre - predict(y)))
            for s in range(c - search, c + search + 1):
                xs = x + s
                if xs - half < 0 or xs + half + 1 > g1.shape[1]:
                    continue
                cand = g1[y - half : y + half + 1, xs - half : xs + half + 1]
                e = float(np.mean((patch - cand) ** 2))
                if best is None or e < best:
                    best, bs = e, s
            res.append((x, y, bs, best))
    return res


def main():
    args = sys.argv[1:]
    d = Path(args[0])
    st = parse_state(d / "vr-state.txt")
    g0 = luma(load_bmp(d / "vr-eye0.bmp"))
    g1 = luma(load_bmp(d / "vr-eye1.bmp"))
    size = st["size"]
    ax0, ay0 = axis_px(st["fov0"], size)
    ax1, ay1 = axis_px(st["fov1"], size)
    f0x, f0y = focal(st["fov0"], size)
    f1x, f1y = focal(st["fov1"], size)
    p0, p1 = st["pos0"], st["pos1"]
    ipd = (p1[0] - p0[0]) if p0 and p1 else 0.063
    print(f"axis pixel: eye0 ({ax0:.1f}, {ay0:.1f})  eye1 ({ax1:.1f}, {ay1:.1f});  focal x {f0x:.1f} / {f1x:.1f} px, y {f0y:.1f} / {f1y:.1f} px")
    print(f"eye separation {ipd * 1000:.1f} mm; disparity at 1 m would be {ipd * f0x:.1f} px, at 5 m {ipd * f0x / 5:.1f} px")

    centre = int(round(ax1 - ax0))	# where the infinity shift lies

    def to_disp(x0, s):
        # a point at x0 in the left image sits at x0 + s in the right: from the axes, d = (x0 - ax0) - (x0 + s - ax1)
        return (x0 - ax0) - (x0 + s - ax1)

    probes = [(float(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4)))
              for m in re.finditer(r"probe (\d+) (\d+) depth_m (\S+) disparity_px (\S+)", (d / "vr-state.txt").read_text())]
    if probes:
        # ground truth: the engine traced a ray through each of these pixels of the left eye
        print(f"{len(probes)} traced probes. u v depth_m predicted measured error (px)")
        rows = []
        for u, v, depth, pred in probes:
            half = 12 if depth < 4.0 else 20
            xu, yv = int(u), int(v)
            if yv - half < 0 or yv + half + 1 > g0.shape[0] or xu - half < 0 or xu + half + 1 > g0.shape[1]:
                continue
            patch = g0[yv - half : yv + half + 1, xu - half : xu + half + 1]
            if patch.std() < 7.0:
                continue
            c = int(round(centre - pred))
            errs = {}
            for sh in range(c - 30, c + 31):
                xs = xu + sh
                if xs - half < 0 or xs + half + 1 > g1.shape[1]:
                    continue
                errs[sh] = float(np.mean((patch - g1[yv - half : yv + half + 1, xs - half : xs + half + 1]) ** 2))
            if len(errs) < 40:
                continue
            bs = min(errs, key=errs.get)
            best = errs[bs]
            # a match counts only when it's clearly better than any other place (the floor texture repeats)
            others = [e for sh, e in errs.items() if abs(sh - bs) >= 4]
            if not others or best > 0.75 * min(others):
                continue
            # sub-pixel: a parabola through the match and its two neighbours
            frac = 0.0
            if bs - 1 in errs and bs + 1 in errs:
                a, b, c2 = errs[bs - 1], errs[bs], errs[bs + 1]
                den = a - 2 * b + c2
                if den > 0:
                    frac = 0.5 * (a - c2) / den
            meas = to_disp(xu, bs + frac)
            rows.append((depth, pred, meas, u, v))
        rows.sort()
        for depth, pred, meas, u, v in rows:
            print(f"  {u:5.0f} {v:5.0f} {depth:7.2f} m  {pred:6.2f}  {meas:6.1f}  {meas - pred:+5.1f}")
        for name, lo, hi in (("far (> 6 m)", 6.0, 1e9), ("mid (2 to 6 m)", 2.0, 6.0), ("near (< 2 m)", 0.0, 2.0)):
            sel = [(p_, m_) for dep, p_, m_, _, _ in rows if lo <= dep < hi]
            if sel:
                err = np.array([m_ - p_ for p_, m_ in sel])
                print(f"{name}: {len(sel)} probes, predicted {np.mean([p_ for p_, _ in sel]):.1f} px, measured {np.mean([m_ for _, m_ in sel]):.1f} px, "
                      f"mean error {err.mean():+.2f} px, median {np.median(err):+.2f}, sd {err.std():.2f}")
        return

    if "--box" in args:
        i = args.index("--box")
        bx0, by0, bx1, by1 = (int(v) for v in args[i + 1 : i + 5])
        res = match_row_blocks(g0, g1, by0, by1, bx0, bx1, centre, block=17)
        ds = [to_disp(x, s) for x, y, s, e in res]
        if ds:
            print(f"box {bx0},{by0}-{bx1},{by1}: {len(ds)} blocks, disparity median {np.median(ds):.1f} px (min {min(ds):.1f}, max {max(ds):.1f})"
                  f" -> distance {ipd * f0x / max(np.median(ds), 1e-3):.2f} m")
        else:
            print("no textured blocks")
        return

    # floor model over the lower part of the picture
    # the floor repeats its texture, so look only near what a floor 1.6 m down would give (and fit h afterwards)
    res = match_row_blocks(g0, g1, int(size[1] * 0.62), int(size[1] * 0.97), int(size[0] * 0.30), int(size[0] * 0.70), centre,
                           search=18, block=65, predict=lambda y: ipd * (y - ay0) / 1.6)
    if res:
        ys = np.array([y for x, y, s, e in res], dtype=np.float32)
        ds = np.array([to_disp(x, s) for x, y, s, e in res], dtype=np.float32)
        slope, icpt = np.polyfit(ys - ay0, ds, 1)
        resid = ds - (slope * (ys - ay0) + icpt)
        print(f"floor: {len(res)} blocks, fit d = {slope:.4f} * (y - yaxis) + {icpt:.2f}; residual sd {resid.std():.2f} px")
        print(f"       eye height above the floor from the slope: {ipd / slope:.3f} m (expected ~1.6 m); offset at the axis row {icpt:.2f} px (expected 0)")
        for lo, hi in ((0.62, 0.72), (0.72, 0.85), (0.85, 0.97)):
            sel = [(y, dd) for y, dd in zip(ys, ds) if lo * size[1] <= y < hi * size[1]]
            if sel:
                m = float(np.median([dd for _, dd in sel]))
                print(f"       rows {lo:.2f}-{hi:.2f} of the height: disparity {m:.1f} px, i.e. {ipd * f0x / max(m, 1e-3):.2f} m away")
    # the far region around the centre, above the floor
    res = match_row_blocks(g0, g1, int(size[1] * 0.28), int(size[1] * 0.52), int(size[0] * 0.40), int(size[0] * 0.60), centre, block=17)
    ds = [to_disp(x, s) for x, y, s, e in res]
    if ds:
        print(f"centre (far end): {len(ds)} blocks, disparity median {np.median(ds):.1f} px -> {ipd * f0x / max(np.median(ds), 1e-3):.1f} m")


if __name__ == "__main__":
    main()
