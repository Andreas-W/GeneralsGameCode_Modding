# Relief after stage A (DESIGN.md "Relief: the plan"): soft height changes on the levels, as the reference maps have them. Measured
# there (relief_measure.py): the ground inside a level is mostly flat, in patches at different heights joined by soft slopes, with a
# few hills and pits on top and little fine noise; starts are flat to some 450.
# 1. Where: each level's interior. Ground that belongs to the level structure (cliffs, ramps, beaches: anything sloped on the smoothed
#    terrain) and water fade the relief out, so ramps, beaches and shores keep their profile. Existing noise on the levels is taken off
#    first (flatten), so the result has the measured flat share.
# 2. Patches: warped cells of patch_size, each lifted or lowered by a random amount of patch_spread, smoothed into soft slopes of
#    soft_width: soft plateaus and soft pits.
# 3. Hills and pits at the measured rate, round or with a flat top; 4. fine noise.
# 5. Kept flat: starts (flat to start_flat[0], full relief from start_flat[1]), Tech, supplies and civilian buildings (keep_flat),
#    areas given in the plan (relief.keep: {"at", "flat", "fade"}). Starts are also levelled (start_level): the ground within the
#    first radius is set to the base's height, blending back by the second, even over a slope of the level structure (a beach top
#    reached into the bases of the fourth map: spread 3 to 7 within 450 against 0.2 on the references).
# 6. Checks: slopes above slope_max are softened, no new impassable ground, land stays above the water; the reference numbers of the
#    result are printed.
# Usage: relief.py <plan.json> <heights_world.npy> <objects.json> <out dir> [--seed N]
#   objects.json as scanned (work/maps/<name>/objects.json): waypoints count as starts on a map that has no other waypoints yet.
#   Writes heights_world.npy, heights.png (16 bit as stage A), impassable.png, preview.png, report.json into out dir.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from scipy.spatial import cKDTree

CELL, RAW = 10.0, 0.625
# Defaults the user approved on the fourth map ("matches what I had in mind for height variation in general"): the hilly references'
# numbers rather than the median map's, which is nearly flat; kept-flat areas as small as the references keep them.
DEFAULTS = {"patch_size": 600, "patch_spread": 12.0, "soft_width": 90, "warp": 0.35,
            "hills_per_km2": 1.2, "hill_radius": [90, 170], "hill_height": [8, 20],
            "pits_per_km2": 0.7, "pit_radius": [130, 240], "pit_depth": [6, 14], "flat_top_share": 0.3,
            "fine": 0.5, "slope_max": 0.2, "structure_slope": 0.1, "structure_fade": [40, 150], "water_fade": [60, 160],
            "start_flat": [350, 550], "start_level": [240, 380], "keep_flat": [150, 280], "flatten": True}


def smoothstep(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def field(shape, scale, rng):
    a = ndimage.gaussian_filter(rng.standard_normal(shape), scale / CELL)
    return a / (a.std() + 1e-9)


def slope_of(z):
    gy, gx = np.gradient(z, CELL)
    return np.hypot(gx, gy)


def weights(z, X, Y, P, levels, water, keep):
    """Where relief may go: 1 inside a level's interior, fading to 0 at the level structure and water (first result) and at kept areas
    (second result)."""
    smooth = ndimage.gaussian_filter(z, 3)
    structure = (slope_of(smooth) > P["structure_slope"]) | (z < water + 2)
    w = smoothstep((ndimage.distance_transform_edt(~structure) * CELL - P["structure_fade"][0]) / (P["structure_fade"][1] - P["structure_fade"][0]))
    wet = z < water
    w *= smoothstep((ndimage.distance_transform_edt(~wet) * CELL - P["water_fade"][0]) / (P["water_fade"][1] - P["water_fade"][0]))
    wk = np.ones_like(w)
    for k in keep:
        d = np.hypot(X - k["at"][0], Y - k["at"][1])
        wk *= smoothstep((d - k["flat"]) / max(1.0, k["fade"] - k["flat"]))
    return w, wk


def patches(shape, X, Y, P, w, rng):
    """Warped cells, each at its own height, smoothed into soft slopes. A patch takes one height all over, scaled by how free it is (the
    median of the weight in it; the lower quartile left most patches near a base low): fading the relief by the weight point by point tilted every patch that reached into the fade
    around a start, cliff or shore, and left 40 % of the ground flat against the references' 92 %."""
    size = P["patch_size"]
    xs = np.arange(X.min() - size, X.max() + size, size)
    ys = np.arange(Y.min() - size, Y.max() + size, size)
    seeds = np.array([(x + rng.uniform(-0.4, 0.4) * size, y + rng.uniform(-0.4, 0.4) * size) for x in xs for y in ys])
    wx = X + P["warp"] * size * field(shape, size * 0.8, rng)
    wy = Y + P["warp"] * size * field(shape, size * 0.8, rng)
    _, idx = cKDTree(seeds).query(np.stack([wx.ravel(), wy.ravel()], 1))
    off = np.clip(rng.normal(0, P["patch_spread"], len(seeds)), -2 * P["patch_spread"], 2 * P["patch_spread"])
    idx = idx.reshape(shape)
    factor = np.zeros(len(seeds))
    for i in np.unique(idx):
        factor[i] = np.median(w[idx == i])
    F = (off * factor)[idx]
    return ndimage.gaussian_filter(F, P["soft_width"] / 2.5 / CELL)


def bumps(shape, X, Y, P, w, rng):
    """Hills and pits on ground where the relief is at full strength."""
    out = np.zeros(shape)
    area_km2 = (w > 0.5).sum() * CELL * CELL / 1e6
    free = np.argwhere(w > 0.9)
    placed = []
    if not len(free):
        return out, placed
    for kind, rate, rr, hh, sign in (("hill", P["hills_per_km2"], P["hill_radius"], P["hill_height"], 1),
                                     ("pit", P["pits_per_km2"], P["pit_radius"], P["pit_depth"], -1)):
        for _ in range(int(round(rate * area_km2))):
            iy, ix = free[rng.integers(len(free))]
            x, y = X[iy, ix], Y[iy, ix]
            r, h = rng.uniform(*rr), rng.uniform(*hh)
            # Squashed and turned: no perfect circles.
            a, s = rng.uniform(0, math.pi), rng.uniform(0.7, 1.0)
            u = ((X - x) * math.cos(a) + (Y - y) * math.sin(a)) / r
            v = (-(X - x) * math.sin(a) + (Y - y) * math.cos(a)) / (r * s)
            d = np.hypot(u, v)
            top = 0.5 if rng.random() < P["flat_top_share"] else 0.0
            out += sign * h * smoothstep((1 - d) / (1 - top))
            placed.append({"kind": kind, "at": [round(float(x)), round(float(y))], "radius": round(r), "height": round(h, 1), "flat_top": bool(top)})
    return out, placed


def stats(z, water, interior):
    """The reference numbers (relief_measure.py) on a terrain: flat share, soft slopes, flat patches, inside the levels."""
    zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
    sl = slope_of(zc)
    deep = interior[:-1, :-1] & (zc >= water)
    if deep.sum() < 200:
        return {}
    flat = deep & (sl < 0.01)
    soft = deep & (sl >= 0.01)
    lab, n = ndimage.label(flat)
    sizes = ndimage.sum(np.ones_like(zc), lab, range(1, n + 1))
    big = [s for s in sizes if s >= 200]
    return {"flat_share": round(float(flat.sum() / deep.sum()), 3), "soft_slope_median": round(float(np.median(sl[soft])), 3) if soft.any() else 0,
            "soft_slope_p90": round(float(np.percentile(sl[soft], 90)), 3) if soft.any() else 0,
            "flat_patches_per_km2": round(len(big) / (deep.sum() * CELL * CELL / 1e6), 2), "flat_patch_cells_median": round(float(np.median(big))) if big else 0,
            "height_range_p5_p95": round(float(np.percentile(zc[deep], 95) - np.percentile(zc[deep], 5)), 1)}


def apply(z, border, levels, water, keep, params, seed=1):
    P = dict(DEFAULTS, **params)
    rng = np.random.default_rng(seed)
    yy, xx = np.indices(z.shape, dtype=np.float64)
    X, Y = (xx - border) * CELL, (yy - border) * CELL
    ws, wk = weights(z, X, Y, P, levels, water, keep)
    w = ws * wk
    base = z.copy()
    if P["flatten"] and levels:
        # Take stage A's noise off the level interiors, kept areas included (they are the flattest of all): each point back to its level,
        # as far as the level structure allows. Flattening only where relief may go left the noise in the starts.
        L = np.array(levels, float)
        nearest = L[np.argmin(np.abs(z[..., None] - L), axis=-1)]
        base = z + (nearest - z) * ws * (np.abs(z - nearest) < 15)
    for k in keep:
        if k.get("level"):
            d = np.hypot(X - k["at"][0], Y - k["at"][1])
            core = (d < k["level"][0]) & (base >= water + 3)
            if core.any():
                target = float(np.median(base[core]))
                t = smoothstep((d - k["level"][0]) / (k["level"][1] - k["level"][0]))
                # Only ground near the base's height: lifting a beach top into the level steepened the rest of the beach (0.31 to 0.55).
                base = np.where((base >= water + 3) & (slope_of(z) < 0.3) & (np.abs(base - target) < 6), target * (1 - t) + base * t, base)
    R = patches(z.shape, X, Y, P, w, rng)
    B_, placed = bumps(z.shape, X, Y, P, w, rng)
    R = R + B_
    # Fine noise only where the ground slopes anyway: on the references flat ground is flat, the fine variation lives on the slopes
    # (spread everywhere it left 21 % of the ground flat against their 92 %).
    R = (R + P["fine"] * field(z.shape, 30, rng) * smoothstep(slope_of(R) / 0.03)) * w
    # Soften where the change (levelling, flattening and relief together) makes ground steeper than slope_max, measured against stage A:
    # measured on the levelled ground, a level's edge cut into a slope counted as old and stayed (84 new impassable points).
    # Ground already steeper than slope_max (a beach at 0.35) may not get steeper either: skipping it left new impassable points there.
    s0 = slope_of(z)
    limit = np.where(s0 >= 0.4, np.inf, np.maximum(P["slope_max"], s0 + 0.02))
    D = base - z + R
    for _ in range(20):
        bad = slope_of(z + D) > limit
        if not bad.any():
            break
        near = ndimage.binary_dilation(bad, iterations=3)
        D = np.where(near, ndimage.gaussian_filter(D, 2), D)
    zn = z + D
    land = z >= water + 3
    zn = np.where(land, np.maximum(zn, water + 3), zn)
    zn = np.round(zn / RAW) * RAW
    imp0, imp1 = slope_of(z) >= 0.4, slope_of(zn) >= 0.4
    report = {"params": P, "features": placed, "new_impassable_vertices": int((imp1 & ~imp0).sum()),
              "before": stats(z, water, w > 0.5), "after": stats(zn, water, w > 0.5),
              "max_change": round(float(np.abs(zn - z).max()), 1)}
    return zn, w, report


def keep_from(objects, P):
    """Starts, Tech, supplies and civilian buildings of a scanned map, as kept areas."""
    keep = []
    for o in objects:
        if o["kind"] == "waypoint":
            keep.append({"at": [o["x"], o["y"]], "flat": P["start_flat"][0], "fade": P["start_flat"][1], "level": P["start_level"], "why": "start"})
        elif o["kind"] == "object" and (o["template"].startswith(("Tech", "Supply")) or "civilian" in o.get("owner", "").lower()) \
                and not o["template"].startswith(("Tree", "Bush", "Vine", "Shrub", "Rock", "Boulder", "Desert")):
            keep.append({"at": [o["x"], o["y"]], "flat": P["keep_flat"][0], "fade": P["keep_flat"][1], "why": o["template"]})
    return keep


def main():
    args = sys.argv[1:]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    plan_path, heights_path, objects_path, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:4]
    plan = json.load(open(plan_path))
    params = plan.get("relief", {})
    P = dict(DEFAULTS, **{k: v for k, v in params.items() if k != "keep"})
    z = np.load(heights_path).astype(np.float64)
    objs = json.load(open(objects_path))
    objs = objs["objects"]["objects"] if isinstance(objs, dict) else objs
    keep = keep_from(objs, P) + params.get("keep", [])
    levels = [l["z"] for l in plan["levels"] if isinstance(l["z"], (int, float))]
    water = plan["water"]["height"]
    zn, w, report = apply(z, plan["border"], levels, water, keep, {k: v for k, v in params.items() if k != "keep"}, seed)
    os.makedirs(out, exist_ok=True)
    np.save(os.path.join(out, "heights_world.npy"), zn.astype(np.float32))
    top = 255 * RAW * plan.get("height_scale", 1.0)
    Image.fromarray(np.round(zn / top * 65535).astype(np.uint16)[::-1]).save(os.path.join(out, "heights.png"))
    sl = slope_of(zn)
    cells = np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]]) >= 0.4
    Image.fromarray((cells[::-1] * 255).astype(np.uint8)).save(os.path.join(out, "impassable.png"))
    json.dump(report, open(os.path.join(out, "report.json"), "w"), indent=1)
    # Preview: hill shading of the result, kept areas outlined, where the relief reaches tinted.
    gy, gx = np.gradient(zn, CELL)
    shade = np.clip(0.55 + 2.2 * (-gx * 0.7 + gy * 0.7), 0, 1)
    t = np.clip((zn - water) / 100, 0, 1)
    rgb = np.stack([0.5 + 0.4 * t, 0.45 + 0.35 * t, 0.3 + 0.25 * t], -1) * shade[..., None]
    rgb[zn < water] = (0.1, 0.25, 0.5)
    rgb = rgb * (0.75 + 0.25 * w[..., None])
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1])
    d = ImageDraw.Draw(im)
    H = zn.shape[0]
    B = plan["border"]
    for k in keep:
        x, y = k["at"][0] / CELL + B, H - 1 - (k["at"][1] / CELL + B)
        r = k["flat"] / CELL
        d.ellipse([x - r, y - r, x + r, y + r], outline=(255, 255, 255))
    im = im.resize((im.width * 2, im.height * 2), Image.BILINEAR)
    im.save(os.path.join(out, "preview.png"))
    print(json.dumps({k: report[k] for k in ("before", "after", "new_impassable_vertices", "max_change")}))
    print("features", len(report["features"]), "kept areas", len(keep))


if __name__ == "__main__":
    main()
