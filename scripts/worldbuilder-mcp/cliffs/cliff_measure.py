# Measures the shape of cliffs on maps read by cliff_scan.py: how tall, how wide, how the line runs, how rough, where they stand. Textures play no part.
# Usage: cliff_measure.py [names]   Needs numpy, scipy and pillow. Writes work/<name>/measure.json and relief.png, and work/relief.jpg with all maps.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from scipy.spatial import cKDTree

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "work")
CELL = 10.0
STEEP = 0.75   # slope from which WorldBuilder's cliff tool maps a cell
GENTLE = 0.25  # where a wall has ended when walking away from it
FLAT = 0.15


def load_mask(path, like):
    # One pixel per cell; which way up it is shows in how well it fits the steep cells.
    m = np.array(Image.open(path).convert("L")) > 127
    return m


def pct(a, q):
    return round(float(np.percentile(a, q)), 1) if len(a) else None


def walk(z, gx, gy, x, y, sign, limit=80):
    # From a wall cell along the slope until the ground is gentle. Returns the path of heights.
    h, w = z.shape
    path = [z[int(y), int(x)]]
    for _ in range(limit):
        ix, iy = int(round(x)), int(round(y))
        if not (0 <= ix < w and 0 <= iy < h):
            break
        n = math.hypot(gx[iy, ix], gy[iy, ix])
        if n < GENTLE:
            break
        x += sign * gx[iy, ix] / n
        y += sign * gy[iy, ix] / n
        ix, iy = int(round(x)), int(round(y))
        if not (0 <= ix < w and 0 <= iy < h):
            break
        path.append(z[iy, ix])
    return path


def measure(name):
    d = os.path.join(WORK, name)
    scan = json.load(open(os.path.join(d, "scan.json")))
    b = scan["info"]["heightmap"]["border"]
    raw = np.load(os.path.join(d, "heights.npy")) * 0.625
    z_all = raw
    z = z_all[b:-b, b:-b] if b else z_all
    h, w = z.shape
    gy, gx = np.gradient(z, CELL)
    slope = np.hypot(gx, gy)
    steep = slope >= STEEP
    imp = load_mask(os.path.join(d, "passability.png"), z_all)
    wat = load_mask(os.path.join(d, "water.png"), z_all)
    # Masks hold cells (one fewer than vertices) and cover the border too.
    def crop(m):
        cells = np.zeros(z_all.shape, dtype=bool)
        cells[:m.shape[0], :m.shape[1]] = m
        return cells[b:-b, b:-b] if b else cells
    a, f = crop(imp), crop(imp[::-1])
    flipped = (f & steep).sum() > (a & steep).sum()
    imp = f if flipped else a
    wat = crop(wat[::-1]) if flipped else crop(wat)
    res = {"size": [w * 10, h * 10], "z_p5": pct(z, 5), "z_p50": pct(z, 50), "z_p95": pct(z, 95), "z_max": pct(z, 100),
           "cliff_mapped": scan["cliff_report"]["cells_mapped"], "cliff_cells": scan["cliff_report"]["cliff_cells"], "mask_flipped": bool(flipped)}
    area = float(w * h)
    res["steep_pct"] = round(100 * steep.sum() / area, 1)
    res["impassable_pct"] = round(100 * imp.sum() / area, 1)
    res["water_pct"] = round(100 * wat.sum() / area, 1)

    # Walls: walk from sampled steep cells down and up.
    ys, xs = np.nonzero(steep)
    rng = np.random.default_rng(1)
    pick = rng.permutation(len(xs))[:4000]
    rise, run, top_pos, top_z, bot_z, roundtop, roundbot, maxslope = [], [], [], [], [], [], [], []
    for i in pick:
        x, y = float(xs[i]), float(ys[i])
        up = walk(z, gx, gy, x, y, 1)
        down = walk(z, gx, gy, x, y, -1)
        prof = down[::-1] + up[1:]
        r = prof[-1] - prof[0]
        if r <= 0 or len(prof) < 2:
            continue
        p = np.array(prof, dtype=np.float32)
        rise.append(r)
        run.append((len(prof) - 1) * CELL)
        top_pos.append((x, y))
        top_z.append(prof[-1])
        bot_z.append(prof[0])
        maxslope.append(float(np.max(np.diff(p))) / CELL)
        # Share of the run spent on the upper and lower quarter of the rise: 0.25 is an even ramp, more is a rounded edge.
        roundtop.append(np.mean(p >= p[0] + r * .75))
        roundbot.append(np.mean(p <= p[0] + r * .25))
    rise = np.array(rise); run = np.array(run)
    res["wall"] = {"rise_p25": pct(rise, 25), "rise_p50": pct(rise, 50), "rise_p90": pct(rise, 90), "rise_max": pct(rise, 100),
                   "run_p50": pct(run, 50), "run_p90": pct(run, 90), "slope_avg_p50": round(float(np.median(rise / np.maximum(run, 1))), 2),
                   "slope_max_p50": round(float(np.median(maxslope)), 2) if maxslope else None,
                   "round_top": round(float(np.median(roundtop)), 2) if roundtop else None, "round_bottom": round(float(np.median(roundbot)), 2) if roundbot else None}

    # The line of a wall: how much the direction it faces varies at small and large scale. 0 = straight, 1 = no common direction.
    ux, uy = gx / np.maximum(slope, 1e-3), gy / np.maximum(slope, 1e-3)
    s = steep.astype(np.float32)
    line = {}
    for r_cells, key in ((2, "jag_2"), (5, "jag_5"), (12, "bend_12"), (30, "bend_30")):
        size = 2 * r_cells + 1
        n = ndimage.uniform_filter(s, size)
        mx = ndimage.uniform_filter(s * ux, size)
        my = ndimage.uniform_filter(s * uy, size)
        order = np.hypot(mx, my) / np.maximum(n, 1e-6)
        line[key] = round(float(np.median(1 - order[steep])), 3) if steep.any() else None
    res["line"] = line

    # Along the wall: how the height of its top and foot change between neighbours within 100 units.
    if len(top_pos) > 10:
        pts = np.array(top_pos)
        tree = cKDTree(pts)
        tv, bv, rv = [], [], []
        tz, bz = np.array(top_z), np.array(bot_z)
        for i in range(len(pts)):
            n = tree.query_ball_point(pts[i], 10)
            if len(n) >= 4:
                tv.append(np.std(tz[n])); bv.append(np.std(bz[n])); rv.append(np.std(tz[n] - bz[n]))
        res["along"] = {"top_std": round(float(np.median(tv)), 1), "foot_std": round(float(np.median(bv)), 1), "rise_std": round(float(np.median(rv)), 1)} if tv else {}
        # The same over 300 units: slow changes of height that a short window does not see.
        tv, bv = [], []
        for i in range(len(pts)):
            n = tree.query_ball_point(pts[i], 30)
            if len(n) >= 8:
                tv.append(np.std(tz[n])); bv.append(np.std(bz[n]))
        if tv:
            res["along"]["top_std_300"] = round(float(np.median(tv)), 1)
            res["along"]["foot_std_300"] = round(float(np.median(bv)), 1)

    # Roughness: what is left of the height after smoothing, on walls, right above walls and on open ground.
    fine = z - ndimage.gaussian_filter(z, 1.2)
    mid = ndimage.gaussian_filter(z, 1.2) - ndimage.gaussian_filter(z, 5)
    near = ndimage.binary_dilation(steep, iterations=6) & ~steep & ~wat
    far = ~ndimage.binary_dilation(steep, iterations=15) & ~wat & ~imp
    def rms(a, m):
        return round(float(np.sqrt(np.mean(a[m] ** 2))), 2) if m.any() else None
    res["rough"] = {"wall_fine": rms(fine, steep), "wall_mid": rms(mid, steep), "beside_fine": rms(fine, near), "beside_mid": rms(mid, near), "open_fine": rms(fine, far), "open_mid": rms(mid, far)}

    # Levels: heights where flat playable ground gathers.
    flat = (slope < FLAT) & ~imp & ~wat
    levels = []
    if flat.any():
        hist, edges = np.histogram(z[flat], bins=np.arange(0, 170, 5))
        smooth = np.convolve(hist, [1, 2, 1], "same") / 4.0
        for i in range(len(hist)):
            if smooth[i] == smooth[max(0, i - 2):i + 3].max() and smooth[i] >= 0.04 * flat.sum():
                levels.append({"z": int(edges[i] + 2.5), "pct": round(100 * hist[max(0, i - 1):i + 2].sum() / flat.sum())})
    res["levels"] = levels
    res["flat_pct"] = round(100 * flat.sum() / area, 1)
    lab, n = ndimage.label((slope < 0.4) & ~imp & ~wat)
    sizes = ndimage.sum(np.ones_like(lab), lab, range(1, n + 1)) if n else []
    res["playable_regions"] = int(sum(1 for v in sizes if v >= 0.01 * area))

    # Ramps: passable slopes that join ground of clearly different heights.
    lab, n = ndimage.label((slope >= FLAT) & (slope < STEEP) & ~imp & ~wat)
    ramps = []
    for i, sl in enumerate(ndimage.find_objects(lab)):
        m = lab[sl] == i + 1
        if m.sum() < 25:
            continue
        zz = z[sl][m]
        if zz.max() - zz.min() >= 15:
            ramps.append((m.sum(), float(zz.max() - zz.min()), float(slope[sl][m].mean())))
    res["ramps"] = {"count": len(ramps), "rise_p50": pct([r[1] for r in ramps], 50), "slope_p50": round(float(np.median([r[2] for r in ramps])), 2) if ramps else None}

    # Where: runs of wall, the map edge, mountains and shores.
    lab, n = ndimage.label(ndimage.binary_dilation(steep), structure=np.ones((3, 3)))
    extents = []
    for i, sl in enumerate(ndimage.find_objects(lab)):
        c = (lab[sl] == i + 1).sum()
        if c >= 30:
            extents.append((c, math.hypot(sl[0].stop - sl[0].start, sl[1].stop - sl[1].start) * CELL))
    res["runs"] = {"count": len(extents), "longest": round(max([e[1] for e in extents], default=0)), "p50": pct([e[1] for e in extents], 50)}

    # Per run of wall, since cliffs of one map can differ: how far the face leaves a plane fitted to it, measured along the plane's normal.
    # A plane over 6 cells sees bumps on the face. Over 15 cells a curved surface is fitted, so that a wall that merely bends does not count.
    per_run = []
    tp = np.array(top_pos) if top_pos else np.zeros((0, 2))
    tz_all, bz_all = np.array(top_z), np.array(bot_z)
    tp_run = lab[tp[:, 1].astype(int), tp[:, 0].astype(int)] if len(tp) else np.zeros(0, dtype=int)
    for i, sl in enumerate(ndimage.find_objects(lab)):
        run_id = i + 1
        ry, rx = np.nonzero((lab[sl] == run_id) & steep[sl])
        if len(rx) < 30:
            continue
        rx = rx + sl[1].start
        ry = ry + sl[0].start
        tree_run = cKDTree(np.c_[rx, ry])
        fine, broad = [], []
        for c in range(0, len(rx), 4):
            for radius, out, curved in ((6, fine, False), (15, broad, True)):
                n = tree_run.query_ball_point([rx[c], ry[c]], radius)
                if len(n) < (12 if not curved else 30):
                    continue
                px, py = (rx[n] - rx[c]) * CELL, (ry[n] - ry[c]) * CELL
                terms = [px, py, np.ones(len(n))] + ([px * px, px * py, py * py] if curved else [])
                A = np.c_[terms].T if False else np.column_stack(terms)
                sol = np.linalg.lstsq(A, z[ry[n], rx[n]], rcond=None)[0]
                resid = z[ry[n], rx[n]] - A @ sol
                out.append(float(np.sqrt(np.mean(resid ** 2))) / math.sqrt(1 + sol[0] ** 2 + sol[1] ** 2))
        m = tp_run == run_id
        per_run.append({"cells": int(len(rx)), "x": float(rx.mean() * CELL), "y": float(ry.mean() * CELL),
                        "face_fine": round(float(np.median(fine)), 2) if fine else None, "face_broad": round(float(np.median(broad)), 2) if broad else None,
                        "top_std": round(float(np.std(tz_all[m])), 1) if m.sum() >= 4 else None, "rise_p50": round(float(np.median(tz_all[m] - bz_all[m])), 1) if m.sum() >= 4 else None})
    res["per_run"] = per_run
    def spread(key):
        v = [p[key] for p in per_run if p[key] is not None]
        return {"p10": pct(v, 10), "p50": pct(v, 50), "p90": pct(v, 90), "max": pct(v, 100)}
    res["run_spread"] = {k: spread(k) for k in ("face_fine", "face_broad", "top_std", "rise_p50")}

    # Hills on the ground above and below walls and on open ground: what a curved surface fitted to that ground alone leaves.
    # Leaving out the wall and its soft edges keeps the wall's own shape out of the number.
    dist, idx = ndimage.distance_transform_edt(~steep, return_indices=True)
    wall_z = z[idx[0], idx[1]]
    def hills(mask):
        ky, kx = np.nonzero(mask)
        if len(kx) < 30:
            return None
        tree_h = cKDTree(np.c_[kx, ky])
        out = []
        for c in range(0, len(kx), max(1, len(kx) // 1500)):
            n = tree_h.query_ball_point([kx[c], ky[c]], 7)
            if len(n) < 25:
                continue
            px, py = (kx[n] - kx[c]) * CELL, (ky[n] - ky[c]) * CELL
            A = np.column_stack([px, py, np.ones(len(n)), px * px, px * py, py * py])
            zz = z[ky[n], kx[n]]
            out.append(float(np.sqrt(np.mean((zz - A @ np.linalg.lstsq(A, zz, rcond=None)[0]) ** 2))))
        return round(float(np.median(out)), 2) if out else None
    band = (dist >= 5) & (dist <= 14) & ~wat & (slope < STEEP)
    res["hills"] = {"above": hills(band & (z > wall_z)), "below": hills(band & (z <= wall_z)), "open": hills((dist > 25) & ~wat & ~imp)}
    edge = np.minimum.reduce([np.arange(w)[None, :] * np.ones((h, 1)), (w - 1 - np.arange(w))[None, :] * np.ones((h, 1)),
                              np.arange(h)[:, None] * np.ones((1, w)), (h - 1 - np.arange(h))[:, None] * np.ones((1, w))])
    zone = edge < 0.12 * min(w, h)
    res["edge"] = {"impassable_near_edge_pct": round(100 * (imp & zone).sum() / max(1, zone.sum()), 1), "steep_share_near_edge": round(100 * (steep & zone).sum() / max(1, steep.sum()), 1)}
    lab, n = ndimage.label(imp)
    touch, inside = [], []
    for i, sl in enumerate(ndimage.find_objects(lab)):
        m = lab[sl] == i + 1
        if m.sum() < 40:
            continue
        rise_m = float(z[sl][m].max() - np.percentile(z[sl][m], 10))
        (touch if edge[sl][m].min() <= 1 else inside).append((int(m.sum()), rise_m))
    res["mountains"] = {"edge_masses": len(touch), "edge_area_pct": round(100 * sum(t[0] for t in touch) / area, 1), "inner_masses": len(inside), "inner_area_pct": round(100 * sum(t[0] for t in inside) / area, 1),
                        "inner_rise_p50": pct([t[1] for t in inside], 50)}
    if wat.any():
        res["shore_steep_pct"] = round(100 * (steep & ndimage.binary_dilation(wat, iterations=3)).sum() / max(1, steep.sum()), 1)
    else:
        res["shore_steep_pct"] = 0.0

    # Overall relief: how power falls with detail size. Higher = smoother.
    f2 = np.abs(np.fft.fftshift(np.fft.fft2(z - z.mean()))) ** 2
    yy, xx = np.indices(f2.shape)
    rad = np.hypot((xx - w // 2) / w, (yy - h // 2) / h)
    m = (rad > 0.01) & (rad < 0.4)
    res["spectrum_beta"] = round(float(-np.polyfit(np.log(rad[m]), np.log(f2[m] + 1e-6), 1)[0]), 2)

    json.dump(res, open(os.path.join(d, "measure.json"), "w"), indent=1)

    # Relief: shaded heights; red = impassable, yellow = steep but passable, blue = water.
    light = np.clip(0.55 + 0.9 * (gx * -0.6 + gy * 0.7), 0, 1)
    elev = (z - z.min()) / max(1, np.ptp(z))
    rgb = np.stack([0.25 + 0.6 * elev, 0.3 + 0.5 * elev, 0.2 + 0.45 * elev], axis=2) * (0.4 + 0.6 * light[..., None])
    rgb[steep & ~imp] = rgb[steep & ~imp] * 0.5 + np.array([.5, .45, 0])
    rgb[imp] = rgb[imp] * 0.6 + np.array([.4, 0, 0])
    rgb[wat] = rgb[wat] * 0.4 + np.array([0, .15, .5])
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1])
    im.save(os.path.join(d, "relief.png"))
    return res


names = sys.argv[1:] or sorted(n for n in os.listdir(WORK) if os.path.exists(os.path.join(WORK, n, "scan.json")) and not n.startswith("gen_"))
results = {}
for n in names:
    results[n] = measure(n)
# A partial run must not lose the other maps.
all_path = os.path.join(WORK, "measure.json")
merged = json.load(open(all_path)) if os.path.exists(all_path) else {}
merged.update(results)
json.dump(merged, open(all_path, "w"), indent=1)
hdr = "map       mapped steep imp flat water | rise25/50/90/max run50 sl_avg sl_max rtop rbot | jag2 jag5 bend12 bend30 | top/foot/rise_std | rough wall f/m beside f/m open f/m | runs long p50 | edgeImp edgeSteep edgeM innerM innerRise shore | ramps rise slope | levels | beta"
print(hdr)
for n, r in results.items():
    w, l, a, g, rr, m = r["wall"], r["line"], r.get("along", {}), r["rough"], r["runs"], r["mountains"]
    print(f"{n:9s} {r['cliff_mapped']:5d} {r['steep_pct']:4} {r['impassable_pct']:4} {r['flat_pct']:4} {r['water_pct']:4} | {w['rise_p25']}/{w['rise_p50']}/{w['rise_p90']}/{w['rise_max']} {w['run_p50']} {w['slope_avg_p50']} {w['slope_max_p50']} {w['round_top']} {w['round_bottom']} | "
          f"{l['jag_2']} {l['jag_5']} {l['bend_12']} {l['bend_30']} | {a.get('top_std')}/{a.get('foot_std')}/{a.get('rise_std')} | {g['wall_fine']}/{g['wall_mid']} {g['beside_fine']}/{g['beside_mid']} {g['open_fine']}/{g['open_mid']} | {rr['count']} {rr['longest']} {rr['p50']} | "
          f"{r['edge']['impassable_near_edge_pct']} {r['edge']['steep_share_near_edge']} {m['edge_masses']}({m['edge_area_pct']}) {m['inner_masses']}({m['inner_area_pct']}) {m['inner_rise_p50']} {r['shore_steep_pct']} | {r['ramps']['count']} {r['ramps']['rise_p50']} {r['ramps']['slope_p50']} | "
          + " ".join(f"{v['z']}:{v['pct']}" for v in r["levels"]) + f" | {r['spectrum_beta']}")
cols = 4
tile = 340
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tile, rows * tile), "white")
for i, n in enumerate(names):
    im = Image.open(os.path.join(WORK, n, "relief.png"))
    k = tile / max(im.size)
    im = im.resize((int(im.width * k), int(im.height * k)), Image.LANCZOS)
    ImageDraw.Draw(im).text((4, 3), n, fill="white")
    sheet.paste(im, ((i % cols) * tile, (i // cols) * tile))
sheet.save(os.path.join(WORK, "relief.jpg" if len(sys.argv) == 1 else "relief_part.jpg"), quality=88)
