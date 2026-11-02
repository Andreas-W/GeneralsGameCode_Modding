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
DISPLACE_PER_FACE = 4.0
# Measured: reference maps block walls well beyond the steep middle; their first passable cells have a slope of about 0.25, rarely above 0.45.
BLOCK_SLOPE = 0.40


def noise(shape, rng, scales):
    # Smooth noise of unit variance from (cells, weight) pairs.
    total = np.zeros(shape, dtype=np.float32)
    for size, weight in scales:
        n = ndimage.gaussian_filter(rng.standard_normal(shape).astype(np.float32), size, mode="wrap")
        total += weight * n / n.std()
    return total / total.std()


# Bump sizes of a wall in cells. One size gives mounds at an even spacing like a row of eggs, so sizes span a wide range, larger ones weaker.
# Sizes above the 6 cells of the face measure barely count as deviation, and taking weight from the small ones flattens the wall.
WALL_SCALES = [(1.5, 1.0), (3.5, 1.0), (7, 0.8), (14, 0.5), (28, 0.35)]
# Reference walls have calm stretches and rough clusters within one run. Part of that shows in the spread between runs, which the per-cliff amount then leaves out.
LOCAL_SPREAD = 0.5
LOCAL_SHARE = 0.12


def wall_noise(shape, rng, scales=WALL_SCALES):
    # Sizes mix and the strength of the bumps also changes from place to place, so that neither size nor height repeats.
    n = noise(shape, rng, scales)
    # Its median stays 1, since face deviation is measured as a median per run.
    return n * np.exp(LOCAL_SPREAD * noise(shape, rng, [(12, 1.0), (30, 0.6)]))


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
    if "parts" in f:
        # One outline around several shapes, so that overlapping ones do not leave walls where they meet.
        one = {k: v for k, v in f.items() if k != "parts"}
        return np.maximum.reduce([signed_distance(dict(one, points=p), x, y, size) for p in f["parts"]])
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
    sigma = math.sqrt(max(0.0, (math.log(max(1.01, get("face_spread"))) / 2.56) ** 2 - LOCAL_SHARE))
    # Its mean, not its median, stays at the recipe's face: with a median of 1, maps with a wide spread came out too rough.
    amount = get("face") * DISPLACE_PER_FACE * np.exp(sigma * noise(shape, rng, [(35, 1)]) - sigma * sigma / 2)
    # A gentle slope such as a mountain's flank names its own width in world units.
    width = np.full(shape, f.get("wall_width", abs(rise) * WIDTH_PER_RISE), dtype=np.float32)
    # The profile's steepest point is 1.5 times its average.
    ramp_width = abs(rise) * 1.5 / RAMP_PEAK
    calm = np.ones(shape, dtype=np.float32)
    ramps = list(f.get("ramps", []))
    toward = f.get("ramp_toward", [])
    for tx, ty, radius in [toward] if toward and not isinstance(toward[0], (list, tuple)) else toward:
        # Rounding moves an outline away from where it was drawn, so this ramp is found on the final outline.
        # Points along the edges too, not only corners, so that a ramp lands where it was asked for.
        loops = [chaikin(np.array(p, dtype=np.float64) * size, True, f.get("round", 0)) for p in f.get("parts") or [f["points"]]]
        outline = np.vstack([a + (b - a) * t for p in loops for a, b in zip(p, np.roll(p, -1, axis=0)) for t in np.linspace(0, 1, 12, endpoint=False)])
        near = outline[np.argmin(np.hypot(outline[:, 0] - tx * size[0], outline[:, 1] - ty * size[1]))]
        ramps.append((near[0] / size[0], near[1] / size[1], radius))
    for rx, ry, radius in ramps:
        along = np.hypot(x - rx * size[0], y - ry * size[1])
        g = smooth_profile(1 - along / (radius * CELL))
        width = width + (ramp_width - width) * g
        calm = calm * (1 - g)
        # Noise must not close a ramp, so its middle stays passable whatever the slope.
        opened |= g > 0.55
    d = d + amount * calm * wall_noise(shape, rng)
    if get("bend"):
        d = d + get("bend") * calm * noise(shape, rng, [(45, 1.0), (18, 0.35)])
    s = smooth_profile(d / width + 0.5)
    # A level names a height of its own, so that overlapping regions join instead of stacking.
    out = np.maximum(z, defaults["base"] + rise * s) if f.get("level") else z + rise * s
    # Mountains (a frame, a ridge or a mass with relief) cannot be entered; without relief a feature is a plateau that can be played on.
    if f.get("relief"):
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


def img(points):
    # Points read off a relief picture, which shows north up: y runs down there.
    return [[u, 1 - v] for u, v in points]


def circle(u, v, r, sides=16):
    return [[u + r * math.cos(2 * math.pi * k / sides), v + r * math.sin(2 * math.pi * k / sides)] for k in range(sides)]


def mass(u, v, r, rise, relief):
    # A lone rock or mountain: a round region with relief, which cannot be entered.
    return dict(kind="region", rise=rise, round=2, points=circle(u, v, r, 9), relief=relief)


# Tournament Continent's land: four lobes that overlap a large middle, reaching the map edge in the north and south.
LOBES = [(.4, .82, .21), (.8, .64, .19), (.56, .19, .21), (.17, .42, .19), (.5, .5, .3)]


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
    "dust": {
        # Two plateaus in opposite corners, walled only on part of their edge, and mountains on the edges beside them.
        "base": 32, "defaults": {"face": 3.0, "face_spread": 6.0, "top_var": 10, "foot_var": 5, "bend": 6},
        "theme": {"ground": "SandLargeType3Grassy", "cliff": "CliffMediumType5b"},
        "features": [
            dict(kind="region", rise=30, round=2, points=[[-.1, 1.1], [.52, 1.1], [.45, .8], [.3, .64], [.14, .46], [-.1, .44]], ramp_toward=[(.5, .95, 25), (.03, .45, 14)]),
            dict(kind="region", rise=15, round=2, points=[[-.1, .6], [.17, .62], [.16, .76], [-.1, .78]], ramp_toward=(.1, .78, 12)),
            dict(kind="region", rise=30, round=2, points=[[.42, -.1], [.48, .12], [.62, .3], [.72, .43], [.8, .55], [1.1, .57], [1.1, -.1]], ramp_toward=[(.42, .02, 14), (.95, .56, 20)]),
            dict(kind="region", rise=15, round=2, points=circle(.9, .27, .07), ramp_toward=(.97, .27, 10)),
            dict(kind="ridge", rise=40, half_width=70, relief=25, points=[[0, .4], [0, 1], [.55, 1]]),
            dict(kind="ridge", rise=40, half_width=70, relief=25, points=[[.4, 0], [1, 0], [1, .6]])]},
    "continent": {
        # Four round lobes of land around a raised middle, in the sea, with rocks on the shore.
        "base": 0, "water": 7, "defaults": {"face": 3.2, "face_spread": 2.45, "top_var": 6, "foot_var": 5, "bend": 30},
        "theme": {"ground": "SandLargeType3Light", "cliff": "CliffLargeType7", "water": "SandMediumType13b"},
        "features": [dict(kind="region", rise=20, round=2, parts=[circle(u, v, r) for u, v, r in LOBES])] + [
            dict(kind="region", rise=42, round=2, points=[[.38, .66], [.62, .66], [.65, .38], [.55, .35], [.4, .38], [.35, .6]],
                 ramp_toward=[(.68, .7, 16), (.3, .3, 16), (.3, .66, 12), (.72, .34, 12)])] + [
            # Rocks sit on the shore, half in the water.
            mass(LOBES[k][0] + LOBES[k][2] * math.cos(math.radians(a)), LOBES[k][1] + LOBES[k][2] * math.sin(math.radians(a)), .025, 30, 15)
            for k, a in ((0, 40), (0, 150), (1, 70), (1, -20), (2, -50), (2, 210), (3, 190))]},
    "hostile": {
        # High ground with a sunken path through it: two corners and a ring around an island, mountains where the path meets the edges.
        "base": 52, "defaults": {"face": 3.9, "face_spread": 2.4, "top_var": 10, "foot_var": 10, "bend": 20},
        "theme": {"ground": "GrassMediumType11", "cliff": "CliffMediumType15"},
        "features": [
            dict(kind="region", rise=-40, parts=[corner(-.1, .63, .47, .47), corner(.3, .3, .4, .4), corner(.63, -.1, .47, .47)],
                 ramp_toward=[(.3, .45, 10), (.7, .55, 10), (.5, .3, 10), (.5, .7, 10), (.37, .75, 10), (.63, .25, 10)]),
            dict(kind="region", rise=40, round=2, points=circle(.5, .5, .08), ramp_toward=(.5, .42, 11)),
            dict(kind="region", rise=40, round=2, points=circle(.15, .85, .07), ramp_toward=(.22, .85, 11)),
            dict(kind="region", rise=40, round=2, points=circle(.85, .15, .07), ramp_toward=(.78, .15, 11))] + [
            mass(u, v, r, 45, 30) for u, v, r in ((.37, .94, .06), (.04, .64, .06), (.63, .06, .06), (.96, .36, .06), (.77, .37, .05), (.23, .63, .05))]},
    "killing": {
        # High ground cut by a river with a branch, mountains along its banks. The reference is a tall map; this one is square.
        "base": 80, "water": 15, "defaults": {"face": 5.1, "face_spread": 1.9, "top_var": 15, "foot_var": 15, "bend": 40},
        "theme": {"ground": "GrassMediumType11", "cliff": "CliffMediumType12", "water": "SandMediumType9blue2"},
        "features": [
            dict(kind="ridge", rise=-70, half_width=150, parts=[
                img([(.17, -.05), (.19, .15), (.2, .3), (.4, .42), (.52, .5), (.45, .65), (.42, .75), (.48, .9), (.5, 1.05)]),
                img([(.25, .32), (-.05, .31)])])] + [
            mass(u, 1 - v, .07, 30, 35) for u, v in ((.4, .05), (.03, .12), (.7, .36), (.85, .48), (.72, .62), (.97, .6), (.3, .58), (.33, .83), (.6, .88))]},
    "homeland": {
        # One level with plateaus at the edges and curved river pieces, at the foot of the plateaus and on their own.
        "base": 37, "water": 21, "defaults": {"face": 5.2, "face_spread": 3.1, "top_var": 10, "foot_var": 5, "bend": 40},
        "theme": {"ground": "GrassMediumType31", "cliff": "CliffMediumType15b", "water": "SandMediumType9blue2"},
        "features": [
            # River pieces are calmer than the walls of the plateaus: their banks hold a clean strip of water.
            dict(kind="ridge", rise=-24, half_width=45, face=2.5, bend=0, parts=[img(p) for p in (
                [(.42, -.02), (.43, .08), (.38, .14), (.3, .14)], [(.56, -.02), (.57, .1), (.62, .18), (.7, .17)],
                [(.42, 1.02), (.42, .9), (.36, .84), (.29, .86)], [(.55, 1.02), (.55, .88), (.62, .83), (.7, .86)],
                [(.34, .2), (.31, .22), (.31, .27)], [(.25, .32), (.17, .38), (.16, .42), (.21, .46)],
                [(.61, .21), (.64, .24), (.64, .29)], [(.69, .33), (.77, .36), (.76, .46)],
                [(.21, .53), (.19, .6), (.26, .67)], [(.31, .71), (.35, .75)],
                [(.75, .52), (.76, .6), (.7, .65)], [(.63, .69), (.62, .77)])])] + [
            # Each plateau has its ramp on the side away from its river.
            dict(kind="region", rise=43, round=1, points=img(p), ramp_toward=img([r])[0] + [16]) for p, r in (
                ([(.29, -.05), (.43, -.05), (.43, .08), (.37, .13), (.29, .12)], (.27, .06)), ([(.55, -.05), (.7, -.05), (.7, .15), (.62, .17), (.57, .1)], (.72, .08)),
                ([(.28, 1.05), (.42, 1.05), (.42, .9), (.35, .83), (.28, .86)], (.26, .94)), ([(.55, 1.05), (.7, 1.05), (.7, .88), (.62, .83), (.55, .88)], (.72, .94)),
                ([(-.05, .27), (.12, .27), (.13, .37), (-.05, .39)], (.16, .32)), ([(-.05, .62), (.12, .63), (.13, .71), (-.05, .74)], (.16, .68)),
                ([(1.05, .29), (.87, .29), (.88, .38), (1.05, .39)], (.84, .34)), ([(1.05, .51), (.88, .52), (.88, .61), (1.05, .61)], (.85, .56)))]},
    "lake": {
        # A wide gentle mountain with a crater lake near the top and a broken ring of small walls on its flank.
        "base": 6, "water": 125, "water_area": circle(.5, .53, .11),
        "defaults": {"face": 2.2, "face_spread": 5.25, "top_var": 5, "foot_var": 5, "bend": 4},
        "theme": {"ground": "GrassMediumType11", "cliff": "CliffMediumType12", "water": "SandMediumType9blue2"},
        "features": [
            dict(kind="region", rise=112, wall_width=600, points=circle(.5, .5, .2, 24), face=1.0),
            # The ring on the flank is one step facing outward, broken where ramps lead up.
            dict(kind="region", rise=18, points=circle(.5, .5, .19, 24),
                 ramps=[(.5 + .19 * math.cos(math.radians(a)), .5 + .19 * math.sin(math.radians(a)), 11) for a in (32, 78, 120, 162, 208, 250, 295, 340)]),
            dict(kind="region", rise=-25, points=circle(.5, .53, .085))]},
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


def wet(raw, recipe, border):
    # Below the water height, or only inside water_area for water that lies above other ground, like a crater lake on a mountain.
    if "water" not in recipe:
        return None
    m = raw * RAW < recipe["water"]
    if "water_area" in recipe:
        yy, xx = np.indices(raw.shape, dtype=np.float32)
        pts = np.array(recipe["water_area"], dtype=np.float64) * (raw.shape[1] - 1 - 2 * border) * CELL
        m &= inside_polygon((xx - border) * CELL, (yy - border) * CELL, pts)
    return m


def impassable(raw, blocked, opened, water):
    z = raw * RAW
    gy, gx = np.gradient(z, CELL)
    m = ((np.hypot(gx, gy) >= BLOCK_SLOPE) & ~opened) | blocked
    if water is not None:
        m |= water
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
        open_ground &= ~water
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
    # A new map comes with a water area at height 7, which would flood ground lower than that.
    for p in wb.call("polygons.list").get("polygons", []):
        if p.get("name") == "Default Water":
            wb.call("polygons.delete", id=p["id"])
    n = raw.shape[1]
    per_call = max(1, 60000 // n)
    for y0 in range(0, raw.shape[0], per_call):
        wb.call("terrain.set_heights", x0=0, y0=y0, heights=raw[y0:y0 + per_call].tolist())
    d = os.path.join(HERE, "work", f"gen_{name}")
    os.makedirs(d, exist_ok=True)
    mask = os.path.join(d, "impassable.png")
    water = wet(raw, recipe, border)
    m = impassable(raw, blocked, opened, water)
    print("passable parts", passable_parts(m, raw, border))
    impassable_mask(m, mask)
    wb.call("terrain.import_mask", path=mask, target="passability", fit="exact")
    theme = recipe["theme"]
    args = dict(seed=seed, base={"texture": theme["ground"]}, blend=False)
    if "water" in recipe:
        area = np.array(recipe.get("water_area", [[-.1, -.1], [1.1, -.1], [1.1, 1.1], [-.1, 1.1]])) * cells * CELL
        wb.call("polygons.add", kind="water", water_height=recipe["water"], points=area.tolist())
        if "water_area" not in recipe:
            args["water"] = {"texture": theme["water"], "below": int(recipe["water"] / RAW) + 2}
    print("textures", {k: v for k, v in wb.call("terrain.auto_texture", **args).items() if k.startswith("cells")})
    if "water_area" in recipe:
        # Painting below a height would cover the whole map under a lake that lies high up.
        lake = os.path.join(d, "water_texture.png")
        Image.fromarray((ndimage.binary_dilation(water, iterations=2)[:-1, :-1][::-1] * 255).astype(np.uint8)).save(lake)
        wb.call("terrain.import_mask", path=lake, target="texture", texture=theme["water"], fit="exact")
    wall_mask = os.path.join(d, "wall_texture.png")
    cliff_texture_mask(raw, wall_mask)
    wb.call("terrain.import_mask", path=wall_mask, target="texture", texture=theme["cliff"], fit="exact")
    wb.call("terrain.reblend")
    report = wb.call("terrain.cliff_map", repair="fit")
    print("cliff map", {k: report[k] for k in ("cliff_cells", "cells_mapped", "outside_v", "outside_u", "mutants")}, report["repair"])
    for k, (px, py) in enumerate(start_points(raw, blocked, water, border)):
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
