# Stage A of map generation (DESIGN.md): the terrain shape from a mockup. The mockup's levels give the outlines; each step between
# levels becomes a wall in a cliff style (cliffs/cliff_gen.py: signed distance, wall noise, soft profile), ramps open where the mockup's
# passable ground climbs across a step, where roads cross one, and wherever large ground the mockup connects would be cut apart. A wide
# passable crossing of the mockup stays a slope, as painted but never steeper than a ramp. Shores of the low level become beaches, other
# shores cliffs. Mountains are a generated frame over the high ground the mockup marks, rising above the ground they stand on. Hills
# inside a level stay as painted.
# Usage: terrain_from_mockup.py <plan.json> <heights.npy (raw, lifted)> <mock.json (objects as read)> <mockup passability.png (cells)> <out dir> [seed]
# Writes heights.png (16 bit, 0..65535 = 0..height_raw_limit raw, top row north), impassable.png (cells), preview.png and report.json.
# Importing them is up to the caller, with range [0, 255 * height_scale].
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "cliffs"))
from cliff_gen import CELL, RAW, WIDTH_PER_RISE, BLOCK_SLOPE, DISPLACE_PER_FACE, noise, wall_noise, smooth_profile

# Highest world height; set from the plan's height_scale (map.ini HeightMapScale).
TOP = 255 * RAW
# Kept objects need flat ground: the wall noise is calm within this distance.
KEEP_CALM = 100.0
# A ramp is a straight corridor across a step: this wide (world units), climbing at this average slope (its steepest point is 1.5 times
# that, below BLOCK_SLOPE), blending into the ground around it over BLEND. Inside it there is no noise and no painted hill.
RAMP_WIDTH = 120.0
RAMP_AVG = 0.2
BLEND = 60.0
ROAD_RAMP = 10
# A ramp ending on a shelf continues down a further step that lies within this distance (world units) below its lower end.
CHAIN = 300.0
# A stretch of a step where the mockup climbs at GENTLE_AVG or less on average is a crossing. Those running at least SLOPE_ALONG (world
# units) along the step are slopes, kept as painted within CROSS_BAND of the step and up to SLOPE_MAX; those of at least RAMP_ALONG ramps.
GENTLE_AVG = 0.3
CROSS_BAND = 150.0
SLOPE_ALONG = 200.0
RAMP_ALONG = 40.0
SLOPE_MAX = 0.3
# High ground smaller than this (cells) is a hill, larger a mountain.
MOUNTAIN_CELLS = 400
# A strip of a middle level narrower than this (world units) is no level the user meant, only the mockup climbing through it: it joins the
# level above, so that one cliff climbs both steps instead of two cliffs with a ledge between them.
THIN_LEVEL = 80.0
# Painted hills are softened to this slope at most, below BLOCK_SLOPE.
HILL_SLOPE = 0.3
# A piece of passable ground at least this large (cells) must connect to the rest of what the mockup connects.
SPLIT_MIN = 400


def signed(mask, sigma):
    # Smoothed outline of a mask, as a signed distance in world units, positive inside. Brush outlines are blobby.
    m = ndimage.gaussian_filter(mask.astype(np.float32), sigma) > 0.5
    return (ndimage.distance_transform_edt(m) - ndimage.distance_transform_edt(~m)) * CELL, m


def bump(dist, radius):
    return smooth_profile(1 - dist / radius)


def build(plan, z0, mock_pass, objs, border, seed, extra_ramps):
    rng = np.random.default_rng(seed)
    shape = z0.shape
    yy, xx = np.indices(shape, dtype=np.float32)
    X, Y = (xx - border) * CELL, (yy - border) * CELL
    water = plan["water"]["height"]
    levels = [l["z"] for l in plan["levels"] if isinstance(l["z"], (int, float))]
    cliff = plan["styles"]["cliff_params"]
    sea = z0 < water
    land_d, land_m = signed(~sea, 1.5)

    # Kept objects, bridge ends and starts calm the noise around them.
    keep = [(o["x"], o["y"]) for o in objs if o["kind"] == "waypoint" or (o["kind"] == "object" and o["template"].startswith(("Tech", "Supply")))]
    keep += [(o["x"], o["y"]) for o in objs if o["kind"] == "bridge_point"]
    calm_keep = np.ones(shape, np.float32)
    for kx, ky in keep:
        calm_keep *= 1 - bump(np.hypot(X - kx, Y - ky), KEEP_CALM)
    # Shape edits from the plan: "calm" scales the wall and shore noise within "radius" of "at" (an island the user found too jagged).
    for e in plan.get("shape_edits", []):
        if "calm" in e:
            inside = smooth_profile((e["radius"] - np.hypot(X - e["at"][0], Y - e["at"][1])) / 200 + 0.5)
            calm_keep *= 1 - (1 - e["calm"]) * inside

    # Mountains are large high areas; a small one is a hill that the user painted passable. Ramps inside or at the foot of one are useless.
    high, nh = ndimage.label(z0 > plan["mountain_above"])
    sizes = ndimage.sum(np.ones(shape), high, range(1, nh + 1))
    mountain = np.isin(high, [i + 1 for i, s in enumerate(sizes) if s >= MOUNTAIN_CELLS])
    near_mountain = ndimage.binary_dilation(mountain, iterations=int(plan["mountains"]["flank"] * 0.3 / CELL))

    # Steps between levels: masks of the mockup above the midpoints, nested.
    outlines = [signed(z0 > (levels[k - 1] + levels[k]) / 2, 2.0)[0] for k in range(1, len(levels))]

    def nearest_step(x, y):
        iy, ix = int(round(y / CELL + border)), int(round(x / CELL + border))
        return 1 + int(np.argmin([abs(o[iy, ix]) for o in outlines]))

    steps, report = [], {"steps": [], "ramps": [], "slopes": []}
    roads = [o for o in objs if o["kind"] == "road_point"]
    road_segs = [((a["x"], a["y"]), (b["x"], b["y"])) for a, b in zip(roads[0::2], roads[1::2])]
    gy0, gx0 = np.gradient(z0 * 1.0, CELL)
    slope0 = np.hypot(gx0, gy0)
    above = [ndimage.gaussian_filter((z0 > (levels[k - 1] + levels[k]) / 2).astype(np.float32), 2.0) > 0.5 for k in range(1, len(levels))]
    for j in range(len(above) - 2, -1, -1):
        middle = above[j] & ~above[j + 1]
        wide = ndimage.binary_opening(middle, iterations=max(1, int(THIN_LEVEL / CELL / 2)))
        above[j] = above[j + 1] | wide | (middle & ~ndimage.binary_dilation(above[j + 1], iterations=int(THIN_LEVEL / CELL)))
    for k in range(1, len(levels)):
        t = (levels[k - 1] + levels[k]) / 2
        m = above[k - 1]
        d = (ndimage.distance_transform_edt(m) - ndimage.distance_transform_edt(~m)) * CELL
        rise = levels[k] - levels[k - 1]
        ramps = []
        zone = np.zeros(shape, bool)
        # Roads crossing the step.
        for (ax, ay), (bx, by) in road_segs:
            n = max(2, int(math.hypot(bx - ax, by - ay) / 10))
            prev = None
            for i in range(n + 1):
                px, py = ax + (bx - ax) * i / n, ay + (by - ay) * i / n
                ix, iy = int(round(px / CELL + border)), int(round(py / CELL + border))
                if not (0 <= ix < shape[1] and 0 <= iy < shape[0]) or sea[iy, ix]:
                    prev = None
                    continue
                cur = bool(m[iy, ix])
                if prev is not None and cur != prev:
                    ramps.append((px, py, ROAD_RAMP, "road"))
                prev = cur
        # The mockup's own crossings: stretches of the step where the mockup climbs gently. Its passability says little, since the game lets
        # units climb far steeper than a cliff looks; how wide the climb is tells a cliff from a slope. Long gentle stretches are slopes,
        # short ones ramps; the rest of the step is cliff.
        low_end = ndimage.distance_transform_edt(~(z0 < levels[k - 1] + 0.15 * rise)) * CELL
        high_end = ndimage.distance_transform_edt(~(z0 > levels[k] - 0.15 * rise)) * CELL
        gentle = 0.7 * rise / np.maximum(low_end + high_end, CELL) <= GENTLE_AVG
        # On the mockup's own outline of this step, not the merged one: there a cliff climbing two steps would look like a gentle one.
        d_own = signed(z0 > t, 2.0)[0]
        playable = np.zeros(shape, bool)
        playable[border:-border, border:-border] = True
        runs, nr = ndimage.label((np.abs(d_own) <= CELL) & ~sea & gentle & playable & ~near_mountain, structure=np.ones((3, 3)))
        for i, sl in enumerate(ndimage.find_objects(runs)):
            run = np.argwhere(runs[sl] == i + 1) + [sl[0].start, sl[1].start]
            cy, cx = run.mean(0)
            along = len(run) * CELL / 2
            if along >= SLOPE_ALONG:
                near_run = np.zeros(shape, bool)
                near_run[run[:, 0], run[:, 1]] = True
                zone |= ndimage.binary_dilation(near_run, iterations=int(CROSS_BAND / CELL)) & ~sea
                report["slopes"].append({"step": k, "at": [round((cx - border) * CELL), round((cy - border) * CELL)], "along": along})
            elif along >= RAMP_ALONG:
                ramps.append(((cx - border) * CELL, (cy - border) * CELL, 10, "mockup"))
        ramps += [(x, y, r, "reach") for kk, x, y, r in extra_ramps if kk == k]
        ramps += [(x, y, r, "reach") for kk, x, y, r in extra_ramps if kk is None and nearest_step(x, y) == k]
        ramps = [r for r in ramps if not near_mountain[int(round(r[1] / CELL + border)), int(round(r[0] / CELL + border))]]
        steps.append((k, d, m, rise, ramps, zone))
        report["steps"].append({"step": k, "from": levels[k - 1], "to": levels[k], "ramps": len(ramps)})
        report["ramps"] += [{"step": k, "at": [round(x), round(y)], "radius": r, "why": why} for x, y, r, why in ramps]

    # Land heights: the low level everywhere, each step on top with its own profile and noise.
    hland = np.full(shape, levels[0], np.float32)
    ideal = np.full(shape, levels[0], np.float32)
    s_prev = np.ones(shape, np.float32)
    edge_dist = np.full(shape, 1e9, np.float32)
    sigma = math.sqrt(max(0.0, (math.log(max(1.01, cliff["face_spread"])) / 2.56) ** 2 - 0.12))
    # One noise for all steps, so that cliffs lying together move together; calm around every ramp and slope, whichever step it belongs to.
    amount = cliff["face"] * DISPLACE_PER_FACE * np.exp(sigma * noise(shape, rng, [(35, 1)]) - sigma * sigma / 2)
    shared = amount * wall_noise(shape, rng) + cliff["bend"] * noise(shape, rng, [(45, 1.0), (18, 0.35)])
    calm = calm_keep.copy()
    for k, d, m, rise, ramps, zone in steps:
        for rx, ry, rr, _ in ramps:
            calm *= 1 - bump(np.hypot(X - rx, Y - ry), 2 * (levels[-1] - levels[0]) / RAMP_AVG / 2 + RAMP_WIDTH)
        calm *= 1 - np.clip(ndimage.gaussian_filter(zone.astype(np.float32), 4) * 2, 0, 1)
    for k, d, m, rise, ramps, zone in steps:
        width = np.full(shape, rise * WIDTH_PER_RISE, np.float32)
        dn = d + calm * shared
        s = smooth_profile(dn / width + 0.5) * s_prev
        hland += rise * s
        ideal += rise * m
        s_prev = s
        edge_dist = np.minimum(edge_dist, np.abs(d) - width / 2)

    # Hills inside a level stay as painted, away from the walls. Where a painted hill is steep enough to wall off ground, its slope is
    # softened until it stays passable: a brush stroke is no cliff the user meant.
    resid = ndimage.gaussian_filter(np.clip(z0 - ideal, -40, 40), 1.0)
    ry, rx = np.gradient(resid, CELL)
    steepness = ndimage.maximum_filter(np.hypot(rx, ry), 5)
    resid *= np.minimum(1.0, HILL_SLOPE / np.maximum(steepness, 1e-6))
    hland += resid * np.clip(edge_dist / 30.0, 0, 1) * ~ndimage.binary_dilation(mountain, iterations=3)

    # Mountains only mark ground that is closed off; the mockup's painted shape is loose. So they are generated: a broadly smoothed outline
    # with irregular foot, a steep flank up to a crest height, and ridged relief growing inside. They cannot be entered.
    mt = plan["mountains"]
    md, mm = signed(mountain, 5.0)
    # Smoothing must not drop a narrow strip of the painted mountains: that left flat pockets between the frame and the map edge.
    md = np.maximum(md, signed(mountain, 1.0)[0])
    md = md + mt["bend"] * noise(shape, rng, [(40, 1.0), (15, 0.4)]) + cliff["face"] * DISPLACE_PER_FACE * wall_noise(shape, rng)
    flank = smooth_profile(md / mt["flank"] + 0.5)
    depth = np.clip(md / (mt["flank"] * 2), 0, 1)
    ridges = 1 - np.abs(noise(shape, rng, [(18, 1.0), (7, 0.5)]))
    # The crest rises above the level the frame stands on: a fixed crest height left the frame barely above the upper level.
    foot = ndimage.gaussian_filter(hland, 8)
    crest = np.minimum(foot + mt["rise"] + mt["relief"] * (depth * ridges - 0.3), TOP - 2)
    hland = hland + (np.maximum(crest, hland) - hland) * flank
    # Noise only widens the frame: where the mockup is mountain, it stays mountain.
    hland = np.where(mountain, np.maximum(hland, crest), hland)
    blocked = (md > -mt["flank"] * 0.25) | mountain

    # Slopes: where the mockup crosses a step on a wide front, its own heights, smoothed until no steeper than SLOPE_MAX.
    zones = np.zeros(shape, bool)
    for st in steps:
        zones |= st[5]
    if zones.any():
        gentle = ndimage.gaussian_filter(z0, 2.0)
        near = ndimage.binary_dilation(zones, iterations=6)
        for _ in range(12):
            gy_, gx_ = np.gradient(gentle, CELL)
            if np.hypot(gx_, gy_)[near].max() <= SLOPE_MAX:
                break
            gentle = np.where(near, ndimage.gaussian_filter(gentle, 2.0), gentle)
        w = np.clip(ndimage.gaussian_filter(ndimage.binary_dilation(zones, iterations=3).astype(np.float32), 2.5) * 1.5, 0, 1) * ~blocked
        hland = hland * (1 - w) + gentle * w

    # Ramps: a corridor across each step, laid over everything above, so that nothing roughens it.
    placed = []
    ramp_area = np.zeros(shape, bool)
    land_at = lambda qx, qy: (0 <= int(round(qx / CELL + border)) < shape[1] and 0 <= int(round(qy / CELL + border)) < shape[0]
                              and not sea[int(round(qy / CELL + border)), int(round(qx / CELL + border))])

    def corridor(px, py, nx, ny, k, rise, why):
        # Lays one corridor across step k at (px, py), climbing along (nx, ny). Returns its lower end (x, y, height) or None.
        nonlocal hland, ramp_area
        # One corridor per spot: where two steps lie together, the first one climbs both.
        if any(math.hypot(px - q["at"][0], py - q["at"][1]) < RAMP_WIDTH for q in placed):
            return None
        # The length follows the heights the corridor actually joins: where the mockup skips a level, it climbs two steps at once.
        length = rise / RAMP_AVG
        for _ in range(4):
            # On a narrow strip by the sea, the corridor moves up the slope until its lower end lies on land.
            reach_ = length / 2 + BLEND
            dry = next((s_ for s_ in np.arange(0, reach_ + CELL, CELL) if not land_at(px - s_ * nx, py - s_ * ny)), None)
            if dry is not None:
                px, py = px + (reach_ - dry + CELL) * nx, py + (reach_ - dry + CELL) * ny
            ends = []
            for sgn in (-1, 1):
                ex, ey = px + sgn * (length / 2 + BLEND) * nx, py + sgn * (length / 2 + BLEND) * ny
                jy, jx = int(round(ey / CELL + border)), int(round(ex / CELL + border))
                if not (0 <= jx < shape[1] and 0 <= jy < shape[0]) or sea[jy, jx] or blocked[jy, jx]:
                    break
                ends.append(float(np.median(hland[max(0, jy - 2):jy + 3, max(0, jx - 2):jx + 3])))
            if len(ends) < 2 or (ends[1] - ends[0]) / RAMP_AVG <= length + CELL:
                break
            length = (ends[1] - ends[0]) / RAMP_AVG
        if len(ends) < 2 or ends[1] - ends[0] < rise * 0.5:
            return None
        along = (X - px) * nx + (Y - py) * ny
        across = -(X - px) * ny + (Y - py) * nx
        h = ends[0] + (ends[1] - ends[0]) * smooth_profile(along / length + 0.5)
        w = smooth_profile((RAMP_WIDTH / 2 + BLEND - np.abs(across)) / BLEND) * smooth_profile((length / 2 + BLEND - np.abs(along)) / BLEND)
        hland = hland * (1 - w) + h * w
        ramp_area |= w > 0.05
        placed.append({"step": k, "at": [round(px), round(py)], "radius": int(RAMP_WIDTH / 2 / CELL), "why": why, "from": round(ends[0]), "to": round(ends[1])})
        return px - (length / 2 + BLEND) * nx, py - (length / 2 + BLEND) * ny, ends[0]

    for k, d, m, rise, ramps, zone in steps:
        ds_ = ndimage.gaussian_filter(d, 2.0)
        gy_, gx_ = np.gradient(ds_, CELL)
        for rx, ry, rr, why in ramps:
            iy, ix = int(round(ry / CELL + border)), int(round(rx / CELL + border))
            nx, ny = gx_[iy, ix], gy_[iy, ix]
            nn = math.hypot(nx, ny)
            if nn < 1e-3:
                continue
            nx, ny = nx / nn, ny / nn
            # Onto the step's outline, along its normal.
            low = corridor(rx - ds_[iy, ix] * nx, ry - ds_[iy, ix] * ny, nx, ny, k, rise, why)
            # A ramp that ends on a shelf above another step continues down it, so that it leads somewhere instead of to a wall.
            kk = k - 1
            while low is not None and kk >= 1:
                lx, ly, lh = low
                drop = next((s_ for s_ in np.arange(0, CHAIN + CELL, CELL)
                             if land_at(lx - s_ * nx, ly - s_ * ny)
                             and hland[int(round((ly - s_ * ny) / CELL + border)), int(round((lx - s_ * nx) / CELL + border))] < lh - 0.5 * (levels[kk] - levels[kk - 1])), None)
                if drop is None:
                    break
                low = corridor(lx - drop * nx, ly - drop * ny, nx, ny, kk, levels[kk] - levels[kk - 1], "chain")
                kk -= 1
    report["ramps"] = placed

    # Shores: beaches where the low level meets the sea, cliffs where a higher level does. Land heights continue into the sea by nearest land.
    idx = ndimage.distance_transform_edt(~land_m, return_distances=False, return_indices=True)
    h_at_shore = hland[idx[0], idx[1]]
    # A ramp that ends near the water meets it as a beach; as a cliff shore it ended in a small wall.
    beach = (h_at_shore < (levels[0] + levels[1]) / 2) | ramp_area[idx[0], idx[1]]
    # A cliff's foot lies some way under water. A beach runs down to the real seabed next to it: down to a fixed depth, while the sea
    # beside it kept the mockup's shallower floor, it left a ditch along the waterline with a dam in front.
    sea_idx = ndimage.distance_transform_edt(sea, return_distances=False, return_indices=True)
    near_floor = np.minimum(z0[sea_idx[0], sea_idx[1]], water - 1.0)
    floor = np.where(sea, z0, np.where(beach, near_floor, water - 12.0))
    w_shore = np.where(beach, plan["beach_width"], np.maximum(30.0, (h_at_shore - floor) * WIDTH_PER_RISE))
    shore_noise = np.where(beach, 1.0, cliff["face"]) * DISPLACE_PER_FACE * wall_noise(shape, rng) * calm_keep
    ds = land_d + shore_noise + cliff["bend"] * noise(shape, rng, [(45, 1.0), (18, 0.35)]) * calm_keep
    # The waterline of a beach stays on the mockup's shore: its gentle slope lies mostly in the shallows, so that land on a narrow shelf stays dry.
    p = smooth_profile(np.where(beach, ds / w_shore + 0.65, ds / w_shore + 0.5))
    z = floor + (np.where(land_m, hland, h_at_shore) - floor) * p
    z = np.where(sea & (land_d < -plan["shore_reach"]), z0, z)
    # Heights snap to the map's height steps.
    step = TOP / 255
    z = np.round(np.clip(z, 0, TOP) / step) * step
    return z.astype(np.float32), blocked, report


def impassable(z, blocked):
    # As the game sees it: steep ground and mountains. No cell is opened by hand, so a ramp that is too steep shows up here.
    gy, gx = np.gradient(z, CELL)
    return (np.hypot(gx, gy) >= BLOCK_SLOPE) | blocked


def passable_with_bridges(ok, objs, border):
    pil = Image.fromarray(ok.astype(np.uint8))
    d = ImageDraw.Draw(pil)
    bridges = [o for o in objs if o["kind"] == "bridge_point"]
    for a, b in zip(bridges[0::2], bridges[1::2]):
        d.line([(a["x"] / CELL + border, a["y"] / CELL + border), (b["x"] / CELL + border, b["y"] / CELL + border)], fill=1, width=3)
    return np.array(pil).astype(bool)


def gaps(z, imp, objs, border, water, mlab, comps, region):
    region_playable = np.zeros(z.shape, bool)
    region_playable[border:-border, border:-border] = True
    # Every area the mockup connects (with bridges) must stay one piece. Returns the label map and, for each split, a cell of the cut-off
    # piece (a large piece, or the one under a start, Tech or Supply object) and the nearest cell of the main piece.
    lab, _ = ndimage.label(passable_with_bridges(~imp & (z >= water), objs, border))
    keep = [(int(round(o["y"] / CELL + border)), int(round(o["x"] / CELL + border))) for o in objs
            if o["kind"] == "waypoint" or (o["kind"] == "object" and o["template"].startswith(("Tech", "Supply")))]
    out = []
    for c in comps:
        inside = (mlab == c) & region
        ids, counts = np.unique(lab[inside & (lab > 0)], return_counts=True)
        if not len(ids):
            continue
        main = ids[np.argmax(counts)]
        need = {i for i, n in zip(ids, counts) if n >= SPLIT_MIN and i != main}
        for iy, ix in keep:
            if inside[iy, ix]:
                near = lab[max(0, iy - 4):iy + 5, max(0, ix - 4):ix + 5]
                if main not in near and near.max() > 0:
                    need.add(int(np.bincount(near[near > 0]).argmax()))
                elif main not in near:
                    need.add(-1 - len(out))
                    out.append(((iy, ix), None))
        if not need:
            continue
        dist, (ty, tx) = ndimage.distance_transform_edt(lab != main, return_indices=True)
        for i in need:
            if i < 0:
                continue
            cells = np.argwhere(lab == i)
            # Ground behind the mountain frame that runs into the map border is closed off on purpose.
            if (~region_playable[cells[:, 0], cells[:, 1]]).any():
                continue
            j = np.argmin(dist[cells[:, 0], cells[:, 1]])
            a = tuple(cells[j])
            out.append((a, (ty[a], tx[a])))
    # A kept object standing on impassable ground: its nearest main ground is found the same way.
    fixed = []
    for a, b in out:
        if b is None:
            c = mlab[a]
            ids, counts = np.unique(lab[(mlab == c) & (lab > 0)], return_counts=True)
            main = ids[np.argmax(counts)]
            _, (ty, tx) = ndimage.distance_transform_edt(lab != main, return_indices=True)
            b = (ty[a], tx[a])
        fixed.append((a, b))
    return lab, fixed


def main():
    global TOP
    plan = json.load(open(sys.argv[1]))
    TOP = 255 * RAW * plan.get("height_scale", 1.0)
    z0 = np.load(sys.argv[2]).astype(np.float32) * RAW
    # Shape edits from the plan: "grow" pushes the whole profile of the piece of land under "at" outward by that many world units, so
    # its top grows with its outline (the height of the nearest edge would only add a rim at the waterline).
    for e in plan.get("shape_edits", []):
        if "grow" in e:
            border = plan["border"]
            lab, _ = ndimage.label(z0 >= plan["water"]["height"])
            piece = lab == lab[int(round(e["at"][1] / CELL + border)), int(round(e["at"][0] / CELL + border))]
            r = int(round(e["grow"] / CELL))
            disk = np.hypot(*np.mgrid[-r:r + 1, -r:r + 1]) <= r
            pushed = ndimage.grey_dilation(np.where(piece, z0, 0), footprint=disk)
            near = ndimage.distance_transform_edt(~piece) * CELL <= e["grow"] + 2 * CELL
            z0 = np.where(near | piece, np.maximum(z0, pushed), z0)
    # "carve": [rx, ry] makes an ellipse around "at" sea, down to "floor" (a channel wide enough for a bridge between two cliffs).
    for e in plan.get("shape_edits", []):
        if "carve" in e:
            yy, xx = np.indices(z0.shape, dtype=np.float32)
            X, Y = (xx - plan["border"]) * CELL, (yy - plan["border"]) * CELL
            inside = ((X - e["at"][0]) / e["carve"][0]) ** 2 + ((Y - e["at"][1]) / e["carve"][1]) ** 2 <= 1
            z0 = np.where(inside, np.minimum(z0, e.get("floor", 1.0)), z0)
    mock = json.load(open(sys.argv[3]))
    imp0 = np.array(Image.open(sys.argv[4]).convert("L")) > 127
    out = sys.argv[5]
    seed = int(sys.argv[6]) if len(sys.argv) > 6 else plan.get("seed", 1)
    os.makedirs(out, exist_ok=True)
    border, shift = plan["border"], plan["shift"]
    objs = [dict(o, x=o["x"] + shift, y=o["y"] + shift) for o in mock["objects"]["objects"]]
    water = plan["water"]["height"]
    levels = [l["z"] for l in plan["levels"] if isinstance(l["z"], (int, float))]
    # The mockup's passability (cells, top row north) on the vertex grid: what the user's mockup lets units cross.
    cells = np.zeros(z0.shape, bool)
    cells[:imp0.shape[0], :imp0.shape[1]] = imp0[::-1]
    mock_pass = ~cells & (z0 >= water)
    mlab, n = ndimage.label(passable_with_bridges(mock_pass, objs, border))
    sizes = ndimage.sum(np.ones(z0.shape), mlab, range(1, n + 1))
    comps = [i + 1 for i, sz in enumerate(sizes) if sz >= 300]
    extra = []
    # A piece cut off from what the mockup connects gets a ramp between it and the main piece, on every step between them; then the map is built again.
    for attempt in range(8):
        zb, blocked, report = build(plan, z0, mock_pass, objs, border, seed, extra)
        imp = impassable(zb, blocked)
        # Only the playable area outside the mountains counts: mountains now close off ground the mockup left passable.
        region = np.zeros(z0.shape, bool)
        region[border:-border, border:-border] = True
        region &= ~ndimage.binary_dilation(blocked, iterations=4)
        lab, lost = gaps(zb, imp, objs, border, water, mlab, comps, region)
        if not lost:
            break
        for (ay, ax), (by, bx) in lost:
            lo, hi = sorted([z0[ay, ax], z0[by, bx]])
            between = [k for k in range(1, len(levels)) if lo < (levels[k - 1] + levels[k]) / 2 < hi]
            # On one level, a nearby step cut a narrow strip: a ramp on the step nearest to the spot.
            for k in between or [None]:
                extra.append((k, ((ax + bx) / 2 - border) * CELL, ((ay + by) / 2 - border) * CELL, 10))
    report["cut_off"] = [{"at": [round((a[1] - border) * CELL), round((a[0] - border) * CELL)]} for a, b in lost]
    report["attempts"] = attempt + 1
    Image.fromarray(np.round(zb / TOP * 65535).astype(np.uint16)[::-1]).save(os.path.join(out, "heights.png"))
    np.save(os.path.join(out, "heights_world.npy"), zb)
    Image.fromarray((imp[:-1, :-1][::-1] * 255).astype(np.uint8)).save(os.path.join(out, "impassable.png"))
    # Preview: shaded relief, water, impassable, ramps, slopes, kept objects.
    z = zb
    gy, gx = np.gradient(z, CELL)
    sh = np.clip((-gx * 0.7 + gy * 0.7 + 0.6) / np.sqrt(1 + gx * gx + gy * gy), 0, 1)
    t = np.clip((z - water) / (TOP - water), 0, 1)
    rgb = np.stack([0.35 + 0.55 * t, 0.5 + 0.4 * t, 0.25 + 0.35 * t], -1) * (0.5 + 0.5 * sh[..., None])
    under = z < water
    rgb[under] = np.array([0.05, 0.2, 0.5]) + 0.25 * (z[under] / water)[:, None]
    rgb[imp & ~under] = rgb[imp & ~under] * 0.5 + np.array([0.5, 0.05, 0.05])
    k = 3
    H = z.shape[0]
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1]).resize((z.shape[1] * k, H * k), Image.NEAREST)
    d = ImageDraw.Draw(im)
    P = lambda x, y: ((x / CELL + border) * k, (H - 1 - (y / CELL + border)) * k)
    for r in report["ramps"]:
        x, y = P(*r["at"]); rr = r["radius"] * k
        d.ellipse([x - rr, y - rr, x + rr, y + rr], outline={"road": (255, 255, 0), "mockup": (0, 255, 120), "reach": (255, 0, 255), "chain": (255, 140, 0)}[r["why"]], width=2)
    for r in report["slopes"]:
        x, y = P(*r["at"])
        d.rectangle([x - 12, y - 12, x + 12, y + 12], outline=(0, 200, 255), width=3)
    for o in objs:
        if o["kind"] == "waypoint" or (o["kind"] == "object" and o["template"].startswith(("Tech", "Supply"))):
            x, y = P(o["x"], o["y"])
            d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(255, 255, 255) if o["kind"] == "waypoint" else (255, 220, 0) if o["template"].startswith("Tech") else (0, 220, 255))
    pm = plan["playable_max"]
    d.rectangle([P(0, pm)[0], P(0, pm)[1], P(pm, 0)[0], P(pm, 0)[1]], outline=(255, 255, 255))
    im.save(os.path.join(out, "preview.png"))
    json.dump(report, open(os.path.join(out, "report.json"), "w"), indent=1)
    print(json.dumps({"steps": report["steps"], "cut_off": report["cut_off"], "attempts": report["attempts"],
                      "ramps_by_cause": {w: sum(1 for r in report["ramps"] if r["why"] == w) for w in ("road", "mockup", "reach")}, "slopes": len(report["slopes"])}))


if __name__ == "__main__":
    main()
