# Exact symmetry for square maps by folding: every point takes the value of its image in a fundamental part of the map. Used to
# generate one part (an octant for 8 players) and fold heights, cell masks and object lists onto the rest, before importing.
# Kinds: "d4" (mirror across both axes and both diagonals: 8 equal parts; the part used is 0 <= y' <= x', x' and y' measured from
# the centre, i.e. the octant from east to north-east).
import math
import numpy as np


def fold_grid(a, kind="d4"):
    """Folds a square grid (row 0 south, centred on the map's centre) so that every cell takes the value of its image in the fundamental
    octant. Works for vertex grids (odd size, centre on a vertex) and cell grids (even size, centre between cells)."""
    assert a.shape[0] == a.shape[1], "square maps only"
    n = a.shape[0]
    c = (n - 1) / 2.0
    j, i = np.indices(a.shape)
    u, v = np.abs(i - c), np.abs(j - c)          # x' and y' from the centre
    u, v = np.maximum(u, v), np.minimum(u, v)     # into 0 <= y' <= x'
    # Back to indices in the north-east octant.
    return a[np.round(c + v).astype(int), np.round(c + u).astype(int)]


def images(x, y, centre, angle=0.0, kind="d4"):
    """The 8 images of a point (world units) and of an angle (degrees) under the d4 group, as (x, y, angle). A point on a mirror line
    gives duplicates; dedupe() drops them."""
    cx, cy = centre
    dx, dy = x - cx, y - cy
    out = []
    for sx in (1, -1):
        for sy in (1, -1):
            for swap in (False, True):
                ux, uy = (dy, dx) if swap else (dx, dy)
                ux, uy = ux * sx, uy * sy
                # The angle goes with the same mapping: a swap mirrors it across the diagonal, a sign mirrors it across an axis.
                a = (90 - angle) if swap else angle
                a = (180 - a) if sx < 0 else a
                a = -a if sy < 0 else a
                out.append((cx + ux, cy + uy, a % 360))
    return out


def dedupe(points, tol=5.0):
    kept = []
    for p in points:
        if all(math.hypot(p[0] - q[0], p[1] - q[1]) > tol for q in kept):
            kept.append(p)
    return kept
