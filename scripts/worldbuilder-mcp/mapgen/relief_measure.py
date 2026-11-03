# Relief on the reference maps: the soft height changes on passable ground, apart from cliffs and levels. Per map in work/refs/<name>/:
# - bands: how much the ground varies at three scales (standard deviation of band-passed heights, world units): fine (below some 80),
#   medium (80 to 250), large (250 to 600), on gentle ground away from cliffs and water;
# - slopes of gentle ground (median, 90th percentile);
# - soft features: hills and pits (ground some units above or below its surroundings at 400 units, passable throughout): count per
#   km^2, typical area and height; soft plateaus: hills with a flat top;
# - flatness: height spread and steepest slope within 300 of each start, Tech, supply and civilian building, against random spots of the
#   same map (MapCache.ini of MapsZH.big gives starts, Tech and supplies).
# Writes work/relief.json and prints a summary. Usage: relief_measure.py [names] [--maps]   (--maps: generated maps in work/maps/)
import json, math, os, re, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
CELL = 10.0
STEEP = 0.4
CLIFF_AWAY = 8          # cells from steep ground before relief counts
FEATURE_H = 3.0         # world units above or below the surroundings to count as a hill or pit
FEATURE_MIN = 40        # cells
NEAR = 300.0            # world units around a start, Tech, supply or building
BUILDINGS = ("Tree", "Bush", "Shrub", "Rock", "Boulder", "Vine", "Reed", "Tech", "Supply", "*", "Desert", "Fence", "Wall")


def map_cache():
    import bigfs
    fs = bigfs.BigFS()
    entry = fs.index[r"maps\mapcache.ini"]
    archive, off, size = entry[0] if isinstance(entry, list) else entry
    with open(archive, "rb") as f:
        f.seek(off)
        text = f.read(size).decode("latin-1")
    out, cur = {}, None
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"MapCache\s+(.+)", s)
        if m:
            path = re.sub(r"_([0-9A-F]{2})", lambda g: chr(int(g.group(1), 16)), m.group(1).strip())
            cur = out.setdefault(path.split("\\")[1].lower(), {"starts": [], "tech": [], "supply": []})
            continue
        if cur is None:
            continue
        m = re.match(r"(Player_\d+_Start|techPosition|supplyPosition)\s*=\s*X:([-\d.]+)\s+Y:([-\d.]+)", s)
        if m:
            key = "starts" if m.group(1).startswith("Player") else "tech" if m.group(1).startswith("tech") else "supply"
            cur[key].append((float(m.group(2)), float(m.group(3))))
    return out


def measure(name, cache, base="refs"):
    d = os.path.join(HERE, "work", base, name)
    info = json.load(open(os.path.join(d, "info.json")))
    z = np.load(os.path.join(d, "heights.npy")).astype(np.float64)
    B = info["border"]
    # Cells, from vertices: mean of four corners.
    zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
    wet = np.array(Image.open(os.path.join(d, "water.png")).convert("L"))[::-1] > 127
    wet = wet[:zc.shape[0], :zc.shape[1]]
    gy, gx = np.gradient(zc, CELL)
    slope = np.hypot(gx, gy)
    steep = slope >= STEEP
    play = np.zeros(zc.shape, bool)
    play[B:-B, B:-B] = True
    gentle = play & ~wet & ~steep & (ndimage.distance_transform_edt(~(steep | wet)) > CLIFF_AWAY)
    if gentle.sum() < 500:
        return None
    # Band-passed heights; smoothing only over gentle ground (normalised convolution), so cliffs and water do not leak in.
    w = gentle.astype(np.float64)
    def smooth(s):
        return ndimage.gaussian_filter(zc * w, s) / np.maximum(ndimage.gaussian_filter(w, s), 1e-6)
    g3, g8, g25, g40 = smooth(3), smooth(8), smooth(25), smooth(40)
    inner = gentle & (ndimage.distance_transform_edt(gentle) > 4)
    bands = {"fine": float(np.std((zc - g8)[inner])), "medium": float(np.std((g8 - g25)[inner])), "large": float(np.std((g25 - smooth(60))[inner]))}
    # Inside the levels only: 300 or more from steep ground and water, where the slopes between levels and to beaches do not count.
    deep = gentle & (ndimage.distance_transform_edt(~(steep | wet)) > 30)
    g60 = smooth(60)
    interior = {"share": float(deep.sum() / max(1, gentle.sum())),
                "fine": float(np.std((zc - g8)[deep])) if deep.sum() > 200 else None,
                "medium": float(np.std((g8 - g25)[deep])) if deep.sum() > 200 else None,
                "large": float(np.std((g25 - g60)[deep])) if deep.sum() > 200 else None,
                "range_p5_p95": float(np.percentile((zc - g60)[deep], 95) - np.percentile((zc - g60)[deep], 5)) if deep.sum() > 200 else None,
                "slope_p90": float(np.percentile(slope[deep], 90)) if deep.sum() > 200 else None}
    # Soft features: relative height at 400 units, regions above / below, passable all over.
    rel = g3 - g40
    feats = {}
    km2 = gentle.sum() * CELL * CELL / 1e6
    for kind, mask in (("hills", rel > FEATURE_H), ("pits", rel < -FEATURE_H)):
        lab, n = ndimage.label(mask & gentle)
        sizes = ndimage.sum(np.ones_like(zc), lab, range(1, n + 1))
        keep = [i + 1 for i, s in enumerate(sizes) if s >= FEATURE_MIN]
        heights = [float(np.abs(rel[lab == i]).max()) for i in keep]
        areas = [float(sizes[i - 1]) for i in keep]
        flat_top = [i for i in keep if np.mean(slope[lab == i] < 0.04) > 0.5]
        feats[kind] = {"per_km2": len(keep) / km2, "area_median": float(np.median(areas)) if areas else 0.0,
                       "height_median": float(np.median(heights)) if heights else 0.0, "flat_topped": len(flat_top)}
    # Flatness near starts, Tech, supplies and civilian buildings, against random gentle spots.
    yy, xx = np.indices(zc.shape)
    def spot(x, y):
        m = (np.hypot((xx - B + 0.5) * CELL - x, (yy - B + 0.5) * CELL - y) < NEAR) & play & ~wet
        if m.sum() < 50:
            return None
        return float(np.std(zc[m])), float(np.percentile(slope[m], 90))
    c = cache.get(name.lower(), {"starts": [], "tech": [], "supply": []})
    objs = json.load(open(os.path.join(d, "objects.json")))
    build = [(o["x"], o["y"]) for o in objs if o["kind"] == "object" and "civilian" in o.get("owner", "").lower() and not o["template"].startswith(BUILDINGS)]
    rng = np.random.default_rng(1)
    cand = np.argwhere(gentle)
    rand = [((ix - B + 0.5) * CELL, (iy - B + 0.5) * CELL) for iy, ix in cand[rng.choice(len(cand), min(60, len(cand)), replace=False)]]
    near = {}
    for k, pts in (("starts", c["starts"]), ("tech", c["tech"]), ("supply", c["supply"]), ("buildings", build[::3]), ("random", rand)):
        vals = [v for v in (spot(x, y) for x, y in pts) if v]
        near[k] = {"n": len(vals), "spread": float(np.median([v[0] for v in vals])) if vals else None,
                   "slope_p90": float(np.median([v[1] for v in vals])) if vals else None}
    # How far the flat ground reaches around starts: the height spread on rings of growing radius.
    rings = {}
    for r0, r1 in ((0, 300), (300, 450), (450, 600), (600, 800), (800, 1000)):
        vals = []
        for x, y in c["starts"]:
            dd = np.hypot((xx - B + 0.5) * CELL - x, (yy - B + 0.5) * CELL - y)
            m = (dd >= r0) & (dd < r1) & gentle
            if m.sum() > 30:
                vals.append(float(np.std(zc[m])))
        rings[f"{r0}-{r1}"] = float(np.median(vals)) if vals else None
    g = slope[gentle]
    return {"bands": bands, "slope_gentle_median": float(np.median(g)), "slope_gentle_p90": float(np.percentile(g, 90)), "features": feats,
            "near": near, "start_rings": rings, "interior": interior, "gentle_share": float(gentle.sum() / play.sum())}


def main():
    base = "maps" if "--maps" in sys.argv else "refs"
    names = [a for a in sys.argv[1:] if not a.startswith("--")] or sorted(n for n in os.listdir(os.path.join(HERE, "work", base)) if os.path.exists(os.path.join(HERE, "work", base, n, "heights.npy")))
    cache = map_cache()
    out = {}
    for n in names:
        try:
            r = measure(n, cache, base)
        except Exception as e:
            print(n, "FAILED", e)
            continue
        if r:
            out[n] = r
    json.dump(out, open(os.path.join(HERE, "work", "relief.json" if base == "refs" else "relief_maps.json"), "w"), indent=1)
    if base == "maps":
        for n, r in out.items():
            print(n, "interior", {k: (round(v, 2) if v is not None else None) for k, v in r["interior"].items()}, "hills", round(r["features"]["hills"]["per_km2"], 2), "pits", round(r["features"]["pits"]["per_km2"], 2), "slope p90", round(r["slope_gentle_p90"], 3))
        return
    col = lambda f: np.array([f(r) for r in out.values()])
    print(f"{len(out)} maps")
    for k in ("fine", "medium", "large"):
        v = col(lambda r: r["bands"][k])
        print(f"band {k:6s}: median {np.median(v):5.2f}  p25 {np.percentile(v, 25):5.2f}  p75 {np.percentile(v, 75):5.2f}")
    print("gentle slope median %.3f, p90 %.3f" % (np.median(col(lambda r: r["slope_gentle_median"])), np.median(col(lambda r: r["slope_gentle_p90"]))))
    for k in ("hills", "pits"):
        print(f"{k}: per km2 median {np.median(col(lambda r: r['features'][k]['per_km2'])):.2f}, area median {np.median(col(lambda r: r['features'][k]['area_median'])):.0f} cells, "
              f"height median {np.median(col(lambda r: r['features'][k]['height_median'])):.1f}, flat-topped share {np.sum(col(lambda r: r['features'][k]['flat_topped'])) / max(1, np.sum(col(lambda r: r['features'][k]['per_km2'] * 0 + 1))):.1f} per map")
    for k in ("starts", "tech", "supply", "buildings", "random"):
        s = [r["near"][k]["spread"] for r in out.values() if r["near"][k]["spread"] is not None]
        sl = [r["near"][k]["slope_p90"] for r in out.values() if r["near"][k]["slope_p90"] is not None]
        print(f"near {k:9s}: height spread median {np.median(s):5.2f}, slope p90 median {np.median(sl):.3f}  ({len(s)} maps)")
    for k in ("0-300", "300-450", "450-600", "600-800", "800-1000"):
        v = [r["start_rings"][k] for r in out.values() if r["start_rings"].get(k) is not None]
        print(f"around starts {k:9s}: height spread median {np.median(v):5.2f}  p75 {np.percentile(v, 75):5.2f}")
    for k in ("share", "fine", "medium", "large", "range_p5_p95", "slope_p90"):
        v = [r["interior"][k] for r in out.values() if r["interior"].get(k) is not None]
        print(f"interior {k:12s}: median {np.median(v):6.3f}  p25 {np.percentile(v, 25):6.3f}  p75 {np.percentile(v, 75):6.3f}")
    hilly = sorted(out, key=lambda n: -(out[n]["bands"]["medium"] + out[n]["bands"]["large"]))
    print("hilliest:", [(n, round(out[n]["bands"]["medium"] + out[n]["bands"]["large"], 1)) for n in hilly[:8]])
    print("flattest:", [(n, round(out[n]["bands"]["medium"] + out[n]["bands"]["large"], 1)) for n in hilly[-6:]])


if __name__ == "__main__":
    main()
