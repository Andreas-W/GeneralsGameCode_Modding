# Generates cliffs from independent properties, on a blank map. Each feature gives a signed distance to its outline (positive inside);
# noise added to that distance moves the wall along its normal, and a soft profile turns the distance into height.
# Usage: cliff_gen.py <recipe> [seed]   Needs numpy, scipy and pillow. Works through the second WorldBuilder (town/start_wb.py), saves to work/gen_<recipe>/.
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
CELL = 10.0
RAW = 0.625            # world units per raw height step
WIDTH_PER_RISE = 1.5   # measured: a taller wall is wider, not steeper
RAMP_PEAK = 0.30       # steepest point of a ramp, whose average is then the 0.2 measured on reference maps; below BLOCK_SLOPE
# Moving a wall sideways by d moves its face by less along the normal, and a fit over 6 cells absorbs slow changes.
DISPLACE_PER_FACE = 3.8
# Measured: reference maps block walls well beyond the steep middle; their first passable cells have a slope of about 0.25, rarely above 0.45.
BLOCK_SLOPE = 0.40


def noise(shape, rng, scales):
    # Smooth noise of unit variance from (cells, weight) pairs.
    total = np.zeros(shape, dtype=np.float32)
    for size, weight in scales:
        n = ndimage.gaussian_filter(rng.standard_normal(shape).astype(np.float32), size, mode="wrap")
        total += weight * n / n.std()
    return total / total.std()


def chaikin(points, closed, rounds):
    p = np.array(points, dtype=np.float64)
    for _ in range(rounds):
        a = p
        b = np.roll(p, -1, axis=0)
        if not closed:
            a, b = p[:-1], p[1:]
        q = np.empty((len(a) * 2, 2))
        q[0::2] = 0.75 * a + 0.25 * b
        q[1::2] = 0.25 * a + 0.75 * b
        p = np.vstack([points[0], q, points[-1]]) if not closed else q
    return p


def segment_distance(x, y, points, closed):
    best = np.full(x.shape, np.inf, dtype=np.float32)
    segs = list(zip(points[:-1], points[1:])) + ([(points[-1], points[0])] if closed else [])
    for a, b in segs:
        ab = b - a
        t = np.clip(((x - a[0]) * ab[0] + (y - a[1]) * ab[1]) / max(1e-6, ab @ ab), 0, 1)
        best = np.minimum(best, np.hypot(x - a[0] - t * ab[0], y - a[1] - t * ab[1]))
    return best


def inside_polygon(x, y, points):
    inside = np.zeros(x.shape, dtype=bool)
    for a, b in zip(points, np.roll(points, -1, axis=0)):
        cross = (a[1] > y) != (b[1] > y)
        at = (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1] + 1e-9) + a[0]
        inside ^= cross & (x < at)
    return inside


def smooth_profile(t):
    # Soft at top and foot: each takes about a third of the width for a quarter of the rise, as measured.
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def signed_distance(f, x, y, size):
    # Coordinates in recipes are fractions of the playable map, so that a recipe fits any map size.
    kind = f["kind"]
    if kind == "frame":
        edge = np.minimum.reduce([x, size[0] - x, y, size[1] - y])
        return f["width"] * min(size) - edge
    pts = np.array(f["points"], dtype=np.float64) * size
    if kind == "region":
        pts = chaikin(pts, True, f.get("round", 0))
        d = segment_distance(x, y, pts, True)
        return np.where(inside_polygon(x, y, pts), d, -d)
    if kind == "ridge":
        pts = chaikin(pts, False, f.get("round", 0))
        return f["half_width"] - segment_distance(x, y, pts, False)
    raise ValueError(kind)


def apply_feature(z, blocked, opened, f, x, y, size, rng, shape, defaults):
    get = lambda key: f.get(key, defaults.get(key))
    rise = f["rise"]
    d = signed_distance(f, x, y, size)
    # Per cliff: the amount of detail changes slowly along walls, with the spread measured on reference maps as p90 / p10.
    sigma = math.log(max(1.01, get("face_spread"))) / 2.56
    amount = get("face") * DISPLACE_PER_FACE * np.exp(sigma * noise(shape, rng, [(35, 1)]))
    width = np.full(shape, abs(rise) * WIDTH_PER_RISE, dtype=np.float32)
    # The profile's steepest point is 1.5 times its average.
    ramp_width = abs(rise) * 1.5 / RAMP_PEAK
    calm = np.ones(shape, dtype=np.float32)
    ramps = list(f.get("ramps", []))
    if "ramp_toward" in f:
        # Rounding moves an outline away from where it was drawn, so this ramp is found on the final outline.
        tx, ty, radius = f["ramp_toward"]
        outline = chaikin(np.array(f["points"], dtype=np.float64) * size, True, f.get("round", 0))
        near = outline[np.argmin(np.hypot(outline[:, 0] - tx * size[0], outline[:, 1] - ty * size[1]))]
        ramps.append((near[0] / size[0], near[1] / size[1], radius))
    for rx, ry, radius in ramps:
        along = np.hypot(x - rx * size[0], y - ry * size[1])
        g = smooth_profile(1 - along / (radius * CELL))
        width = width + (ramp_width - width) * g
        calm = calm * (1 - g)
        # Noise must not close a ramp, so its middle stays passable whatever the slope.
        opened |= g > 0.55
    d = d + amount * calm * noise(shape, rng, [(1.5, 1.0), (3.5, 1.0), (7, 0.7)])
    if get("bend"):
        d = d + get("bend") * calm * noise(shape, rng, [(45, 1.0), (18, 0.35)])
    s = smooth_profile(d / width + 0.5)
    # A level names a height of its own, so that overlapping regions join instead of stacking.
    out = np.maximum(z, defaults["base"] + rise * s) if f.get("level") else z + rise * s
    # A mountain frame cannot be entered; a frame without relief is a plateau that can be played on.
    if f["kind"] == "frame" and f.get("relief"):
        blocked |= s > 0.5
    # Height changes along the wall stay close to it, so that the level itself remains flat and buildable.
    near = np.exp(-np.maximum(np.abs(d) - width / 2, 0) / 90.0)
    if get("top_var"):
        out += get("top_var") * noise(shape, rng, [(12, 1.0), (5, 0.4)]) * s * near * calm
    if get("foot_var"):
        out += get("foot_var") * noise(shape, rng, [(12, 1.0), (5, 0.4)]) * (1 - s) * near * calm
    if get("hills"):
        # Small separate mounds along the upper edge, not a rough surface.
        mounds = np.maximum(noise(shape, rng, [(6, 1.0)]) - 0.9, 0)
        out += get("hills") * mounds * s * np.exp(-np.maximum(d - width / 2, 0) / 70.0) * calm
    if f.get("relief"):
        # Mountains: tall detail that grows inside the mass.
        depth = np.clip(d / (width * 1.5), 0, 1)
        out += f["relief"] * depth * np.abs(noise(shape, rng, [(14, 1.0), (6, 0.5)]))
    return out


def corner(u, v, du, dv):
    return [[u, v], [u + du, v], [u + du, v + dv], [u, v + dv]]


# Each recipe follows what was measured on the reference map named after it. Rise, face and spread are its numbers; layout is by eye.
RECIPES = {
    "twilight": {
        "base": 97, "defaults": {"face": 1.4, "face_spread": 3.8, "top_var": 4, "foot_var": 1.5, "bend": 0},
        "theme": {"ground": "SandMediumType6", "cliff": "CliffLargeType10"},
        "features": [dict(kind="region", rise=55, points=corner(u, v, .36, .36), ramps=r) for u, v, r in (
            (-.1, -.1, [(.26, .18, 16), (.18, .26, 16)]), (.74, -.1, [(.74, .18, 16), (.82, .26, 16)]),
            (-.1, .74, [(.26, .82, 16), (.18, .74, 16)]), (.74, .74, [(.74, .82, 16), (.82, .74, 16)]))]},
    "desert": {
        "base": 42, "defaults": {"face": 5.5, "face_spread": 1.7, "top_var": 6, "foot_var": 2, "bend": 8},
        "theme": {"ground": "SandLargeType4", "cliff": "CliffLargeType12"},
        "features": [
            dict(kind="region", rise=20, points=[[-.1, .6], [1.1, .6], [1.1, 1.1], [-.1, 1.1]], ramps=[(.5, .6, 40)]),
            dict(kind="ridge", rise=30, half_width=45, points=[[.2, .28], [.2, .5], [.42, .5]]),
            dict(kind="ridge", rise=30, half_width=45, points=[[.8, .28], [.8, .5], [.58, .5]]),
            dict(kind="ridge", rise=30, half_width=45, points=[[.2, .9], [.2, .72], [.42, .72]]),
            dict(kind="ridge", rise=30, half_width=45, points=[[.8, .9], [.8, .72], [.58, .72]]),
            dict(kind="frame", rise=50, width=.035, relief=14)]},
    "overland": {
        "base": 32, "defaults": {"face": 2.1, "face_spread": 2.1, "top_var": 7, "foot_var": 1, "bend": 4, "hills": 16},
        "theme": {"ground": "GrassMediumType10", "cliff": "CliffMediumType12"},
        "features": [
            dict(kind="frame", rise=50, width=.14, ramps=[(.14, .5, 22), (.86, .5, 22), (.5, .14, 22), (.5, .86, 22)]),
            dict(kind="region", rise=50, round=3, points=[[.36, .36], [.64, .36], [.64, .64], [.36, .64]], ramps=[(.36, .5, 16), (.64, .5, 16)])]},
    "manic": {
        "base": 12, "defaults": {"face": 2.9, "face_spread": 2.7, "top_var": 8, "foot_var": 3, "bend": 6},
        "theme": {"ground": "GrassMediumType30", "cliff": "CliffMediumType5"},
        "features": [
            dict(kind="region", rise=80, points=corner(-.1, .5, .6, .6), ramps=[(.5, .75, 25), (.25, .5, 25)]),
            dict(kind="region", rise=80, points=corner(.5, -.1, .6, .6), ramps=[(.5, .25, 25), (.75, .5, 25)]),
            dict(kind="frame", rise=70, width=.09, relief=45, face=4)]},
    "flash": {
        "base": 1, "water": 7, "defaults": {"face": 2.9, "face_spread": 1.8, "top_var": 3, "foot_var": 0.5, "bend": 55},
        "theme": {"ground": "GrassMediumType8", "cliff": "CliffMediumType8b", "water": "SandMediumType9blue2"},
        "features": [
            dict(kind="region", level=True, rise=36, round=3, points=[[.08, .12], [.42, .08], [.36, .42], [.1, .48]], ramp_toward=(.5, .5, 18)),
            dict(kind="region", level=True, rise=36, round=3, points=[[.6, .08], [.92, .14], [.9, .46], [.62, .4]], ramp_toward=(.5, .5, 18)),
            dict(kind="region", level=True, rise=36, round=3, points=[[.1, .58], [.38, .6], [.42, .92], [.08, .88]], ramp_toward=(.5, .5, 18)),
            dict(kind="region", level=True, rise=36, round=3, points=[[.62, .6], [.9, .54], [.92, .9], [.58, .92]], ramp_toward=(.5, .5, 18)),
            # The low middle that joins the islands, gentle enough to walk on.
            dict(kind="region", level=True, rise=11, round=3, points=[[.24, .24], [.76, .24], [.76, .76], [.24, .76]], face=0.5, bend=20)]},
}


def build(recipe, cells, border, seed):
    rng = np.random.default_rng(seed)
    n = cells + 2 * border
    shape = (n, n)
    yy, xx = np.indices(shape, dtype=np.float32)
    x, y = (xx - border) * CELL, (yy - border) * CELL
    size = np.array([cells * CELL, cells * CELL])
    z = np.full(shape, float(recipe["base"]), dtype=np.float32)
    blocked = np.zeros(shape, dtype=bool)
    opened = np.zeros(shape, dtype=bool)
    defaults = dict(recipe["defaults"], base=recipe["base"])
    for f in recipe["features"]:
        z = apply_feature(z, blocked, opened, f, x, y, size, rng, shape, defaults)
    return z, blocked, opened


def impassable(raw, blocked, opened, water):
    z = raw * RAW
    gy, gx = np.gradient(z, CELL)
    m = ((np.hypot(gx, gy) >= BLOCK_SLOPE) & ~opened) | blocked
    if water is not None:
        m |= z < water
    return m


def impassable_mask(m, path):
    # One pixel per cell, top row first.
    Image.fromarray((m[:-1, :-1][::-1] * 255).astype(np.uint8)).save(path)


def passable_parts(m, raw, border):
    # Ground of a level that no ramp reaches is a mistake in the recipe, so this is reported every time. Sizes in cells, with their usual height.
    p = ~m
    p[:border] = p[-border:] = False
    p[:, :border] = p[:, -border:] = False
    lab, n = ndimage.label(p)
    parts = [(int((lab == i).sum()), i) for i in range(1, n + 1)]
    return [(c, int(np.median(raw[lab == i]) * RAW)) for c, i in sorted(parts, reverse=True) if c >= 200]


def cliff_texture_mask(raw, path):
    # Bumps leave small shelves in a wall, so painting by slope alone would leave holes of ground texture in the face.
    z = raw * RAW
    gy, gx = np.gradient(z, CELL)
    m = np.hypot(gx, gy) >= 0.6
    m = ndimage.binary_fill_holes(ndimage.binary_closing(m, iterations=2)) & ndimage.binary_dilation(m, iterations=2)
    m = ndimage.binary_opening(m, iterations=1)
    Image.fromarray((m[:-1, :-1][::-1] * 255).astype(np.uint8)).save(path)


def start_points(raw, blocked, water, border):
    # Two far apart spots in the middle of open flat ground, so that the map can be played as a skirmish.
    z = raw * RAW
    gy, gx = np.gradient(z, CELL)
    open_ground = (np.hypot(gx, gy) < 0.1) & ~blocked
    if water is not None:
        open_ground &= z > water
    open_ground[:border + 5] = open_ground[-border - 5:] = False
    open_ground[:, :border + 5] = open_ground[:, -border - 5:] = False
    room = ndimage.distance_transform_edt(open_ground)
    yy, xx = np.indices(raw.shape)
    points = []
    for _ in range(2):
        y, x = np.unravel_index(np.argmax(room), room.shape)
        points.append(((x - border) * CELL, (y - border) * CELL))
        room[np.hypot(xx - x, yy - y) < 120] = 0
    return points


def main():
    from wb_bridge import WorldBuilderBridge
    name = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    recipe = RECIPES[name]
    cells, border = 300, 30
    z, blocked, opened = build(recipe, cells, border, seed)
    raw = np.clip(np.round(z / RAW), 0, 255).astype(int)
    wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
    wb.call("map.new", width=cells, height=cells, border=border, initial_height=int(raw[0, 0]), discard_changes=True)
    n = raw.shape[1]
    per_call = max(1, 60000 // n)
    for y0 in range(0, raw.shape[0], per_call):
        wb.call("terrain.set_heights", x0=0, y0=y0, heights=raw[y0:y0 + per_call].tolist())
    d = os.path.join(HERE, "work", f"gen_{name}")
    os.makedirs(d, exist_ok=True)
    mask = os.path.join(d, "impassable.png")
    m = impassable(raw, blocked, opened, recipe.get("water"))
    print("passable parts", passable_parts(m, raw, border))
    impassable_mask(m, mask)
    wb.call("terrain.import_mask", path=mask, target="passability", fit="exact")
    theme = recipe["theme"]
    args = dict(seed=seed, base={"texture": theme["ground"]}, blend=False)
    if "water" in recipe:
        args["water"] = {"texture": theme["water"], "below": int(recipe["water"] / RAW) + 2}
        wb.call("polygons.add", kind="water", water_height=recipe["water"],
                points=[[-300, -300], [cells * CELL + 300, -300], [cells * CELL + 300, cells * CELL + 300], [-300, cells * CELL + 300]])
    print("textures", {k: v for k, v in wb.call("terrain.auto_texture", **args).items() if k.startswith("cells")})
    wall_mask = os.path.join(d, "wall_texture.png")
    cliff_texture_mask(raw, wall_mask)
    wb.call("terrain.import_mask", path=wall_mask, target="texture", texture=theme["cliff"], fit="exact")
    wb.call("terrain.reblend")
    report = wb.call("terrain.cliff_map", repair="fit")
    print("cliff map", {k: report[k] for k in ("cliff_cells", "cells_mapped", "outside_v", "outside_u", "mutants")}, report["repair"])
    for k, (px, py) in enumerate(start_points(raw, blocked, recipe.get("water"), border)):
        wb.call("waypoints.add", name=f"Player_{k + 1}_Start", x=float(px), y=float(py))
    wb.call("map.save", path=os.path.join(d, f"gen_{name}.map"), overwrite=True)
    # A copy under the user's maps can be played; the file above is the one that gets measured and viewed.
    title = "CliffGen" + name.capitalize()
    maps = os.path.join(os.path.expanduser("~"), "Documents", "Command and Conquer Generals Zero Hour Data", "Maps", title)
    os.makedirs(maps, exist_ok=True)
    wb.call("map.save", path=os.path.join(maps, title + ".map"), overwrite=True)
    import cliff_scan
    cliff_scan.scan_open(wb, f"gen_{name}", wb.call("map.info"), "generated")


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(HERE))
    main()
