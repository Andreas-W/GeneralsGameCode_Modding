# The ground a generator builds on. Without a site file: the flat, empty scratch map of the samples. With one (site=<file>, written by
# town_tool.py): the area and map extents, plus what is in the way on the real map: water, cliffs, steep ground, existing objects and roads.
import json, math
import numpy as np

class Site:
    def __init__(self, path=None, default_poly=None):
        self.live = bool(path)
        self.poly = default_poly
        self.B = 30
        self.nx = self.ny = 300     # height map vertices
        self.blocked = None         # cells, row 0 = south, border included
        self.exit_radius = 900
        if path:
            d = json.load(open(path))
            self.poly = [tuple(p) for p in d["polygon"]]
            self.B, self.nx, self.ny = d["border"], d["x_extent"], d["y_extent"]
            self.blocked = np.load(d["blocked"])
            cx, cy = self.center_of_poly()
            self.exit_radius = max(math.dist(p, (cx, cy)) for p in self.poly) + 100
        self.GN = max(self.nx, self.ny)
        self.xmax = (self.nx - 2 * self.B - 1) * 10
        self.ymax = (self.ny - 2 * self.B - 1) * 10

    def center_of_poly(self):
        return float(np.mean([p[0] for p in self.poly])), float(np.mean([p[1] for p in self.poly]))

    def clamp(self, q):
        return (min(max(q[0], 60), self.xmax - 60), min(max(q[1], 60), self.ymax - 60))

    def in_map(self, x, y):
        return 60 <= x <= self.xmax - 60 and 60 <= y <= self.ymax - 60

    def free(self, x, y):
        if self.blocked is None:
            return True
        i, j = int(x / 10 + self.B), int(y / 10 + self.B)
        return 0 <= j < self.blocked.shape[0] and 0 <= i < self.blocked.shape[1] and not self.blocked[j, i]

    def line_ok(self, a, b):
        if self.blocked is None:
            return True
        n = max(2, int(math.dist(a, b) / 5))
        return all(self.free(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) for t in np.linspace(0, 1, n))

    def clip(self, a, b):
        """As far along a-b as the ground allows."""
        if self.blocked is None:
            return b
        L = math.dist(a, b)
        last = a
        for t in np.linspace(0, 1, max(2, int(L / 5))):
            q = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
            if not self.free(*q):
                break
            last = q
        return last

    def usable_edges(self, edges, nodes):
        """Streets that can be built, and of those the largest connected part: (edges, nodes)."""
        if self.blocked is None:
            return edges, nodes
        ok = {e for e in edges if self.line_ok(nodes[e[0]], nodes[e[1]])}
        adj = {}
        for a, b in ok:
            adj.setdefault(a, set()).add(b)
            adj.setdefault(b, set()).add(a)
        best, seen = set(), set()
        for n in adj:
            if n in seen:
                continue
            comp, st = set(), [n]
            while st:
                m = st.pop()
                if m not in comp:
                    comp.add(m)
                    st.extend(adj[m] - comp)
            seen |= comp
            if len(comp) > len(best):
                best = comp
        return {e for e in ok if e[0] in best}, {n: nodes[n] for n in best}

    def best_grid(self, build, rng, angle, spacing, angle_free):
        """A street grid that suits the ground: (edges, nodes). The flat samples take the grid as it is. On a real map obstacles cut a
        fixed grid to pieces, so try other positions, and other angles unless the angle was asked for, and keep the most blocks."""
        if not self.live:
            return build(angle, 0, 0)
        angles = [angle]
        if angle_free:
            angles += [angle + math.radians(d) for d in (15, -15, 30, -30, 45)]
        best, best_score = None, (-1, -1)
        for t in range(90):
            ou = 0 if t == 0 else rng.uniform(-spacing[0] / 2, spacing[0] / 2)
            ov = 0 if t == 0 else rng.uniform(-spacing[1] / 2, spacing[1] / 2)
            e, n = self.usable_edges(*build(angles[t % len(angles)], ou, ov))
            score = (len(e) - len(n) + 1, len(e))   # independent loops, then streets
            if score > best_score:
                best, best_score = (e, n), score
        return best

    def free_center(self):
        """The middle of the area, or the nearest free spot to it."""
        c = self.center_of_poly()
        if self.free(*c):
            return c
        for r in range(10, 800, 10):
            for k in range(16):
                q = (c[0] + r * math.cos(k * math.pi / 8), c[1] + r * math.sin(k * math.pi / 8))
                if self.free(*q):
                    return q
        raise SystemExit("no free ground in the area")

    def stamp(self, occ, OS):
        """Marks what is in the way on an occupancy grid with cells of OS units."""
        if self.blocked is None:
            return
        n = occ.shape[0]
        idx = np.minimum((np.arange(n) * OS / 10).astype(int), self.GN - 1)
        full = np.ones((self.GN, self.GN), bool)
        h, w = self.blocked.shape
        full[:h, :w] = self.blocked
        occ |= full[np.ix_(idx, idx)]

    def crop(self, m):
        """A mask as a picture: one pixel per cell of this map, north up."""
        return m[:self.ny - 1, :self.nx - 1][::-1]
