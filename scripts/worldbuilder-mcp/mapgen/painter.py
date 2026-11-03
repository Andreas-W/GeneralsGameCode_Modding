# Texturing step 4 (DESIGN.md "Texturing: the plan"): paint a terrain with a reference's texture model (texture_model.py).
# 1. Zones from the model's probabilities by location, smoothed and with warped noise on their borders.
# 2. Family variants inside their zone, by warped noise at the reference's patch size and in its shares.
# 3. Transitions as bands of the reference's width (varied by noise) where their two sides meet; what the zone's composition still
#    wants of them runs as strands through the zone (ridged noise, spaced by the size of the host's patches in the reference).
# 4. Accents inside their host, by warped noise at the reference's patch size, in the zone's measured composition.
#    Accents lean towards where the reference has them by location (texture_model.texture_probabilities).
# 5. Cliff textures by slope: the steepest texture on the steepest cells, mixed by noise.
# 6. No specks: patches below MIN_PATCH cells take the texture around them. Fewer three-texture corners (hard edges in the game):
#    at each, the cell with the least of its own texture around it takes the texture most common around it.
# Only the playable area is painted; the border carries the main texture of the zone at the nearest edge cell (plain, as on most
# reference maps).
# Usage: painter.py <model> [terrain] [--seed N]   terrain: a scanned map (work/refs/ or work/maps/<terrain>/features.npz); default the
#        model's own map. Writes work/paint/<model>__<terrain>.png (reference textures | painted, in mean colours) and prints the
#        painted numbers against the reference's. As a module: paint(f, model, seed) for any map's cell features.
import json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import texture_model

STEEP = 0.6
MIN_PATCH = 9            # cells: 3 x 3
MIN_COMPOSITION = 0.005  # accents below this share of their zone are left out (each texture counts against the map's limit)
ZONE_SMOOTH = 1.5        # cells, on the zone probabilities
ZONE_NOISE = 6.0         # cells, scale of the noise on zone borders
ZONE_NOISE_AMOUNT = 0.6  # in log probability
TRANSITION_VARY = 0.4    # band width varies by this fraction
EDGE_RUN = 41            # cells along the playable edge over which the border texture stays the same
LOCATION = 1.0           # weight of the location preference against the noise (both have unit spread)


def field(shape, scale, rng, warp=0.5):
    """Smooth noise, zero mean and unit spread, with features of about `scale` cells and none smaller than scale / 4; warped
    (sampled at positions moved by another smooth noise) for flowing, interlocking shapes."""
    n = np.zeros(shape)
    for k, w in ((1, 1.0), (2, 0.5), (4, 0.25)):
        g = ndimage.gaussian_filter(rng.standard_normal(shape), max(scale / k, 0.7), mode="wrap")
        n += w * g / max(g.std(), 1e-9)
    if warp:
        jj, ii = np.indices(shape).astype(float)
        dy = ndimage.gaussian_filter(rng.standard_normal(shape), scale, mode="wrap")
        dx = ndimage.gaussian_filter(rng.standard_normal(shape), scale, mode="wrap")
        d = warp * scale / max(np.hypot(dx, dy).mean(), 1e-9)
        n = ndimage.map_coordinates(n, [jj + d * dy, ii + d * dx], order=1, mode="wrap")
    return (n - n.mean()) / max(n.std(), 1e-9)


def cell_median(mask):
    lab, k = ndimage.label(mask)
    if not k:
        return 0.0
    a = np.sort(np.bincount(lab.ravel())[1:])
    return float(a[np.searchsorted(np.cumsum(a), a.sum() / 2)])


def preference(p, region):
    """Location preference from a texture probability: log odds, standardized over the region (0 where there is no information)."""
    if p is None:
        return 0
    lo = np.log(np.clip(p, 1e-3, 1 - 1e-3) / (1 - np.clip(p, 1e-3, 1 - 1e-3)))
    v = lo[region]
    return (lo - v.mean()) / v.std() if v.size and v.std() > 1e-6 else 0


def pick(region, count, area, rng, tries=3, bias=0, veins=False):
    """`count` cells of `region` in patches whose typical size (area-weighted median) is near `area` cells: the top of a noise
    field plus `bias`, its scale corrected a few times against the measured patch size. veins: strands along the noise's zero
    line instead, `area` then sets their spacing."""
    idx = np.nonzero(region)
    count = int(min(count, len(idx[0])))
    if count <= 0:
        return np.zeros(region.shape, bool)
    scale = max(np.sqrt(max(area, 4)) / 2.5, 1.0)
    best = None
    for _ in range(tries):
        n = field(region.shape, scale, rng)
        if veins:
            n = -np.abs(n) * 3
        n = n + LOCATION * bias
        v = n[idx]
        cut = np.partition(v, len(v) - count)[len(v) - count]
        m = region & (n >= cut)
        got = cell_median(m)
        if best is None or abs(np.log(max(got, 1) / area)) < abs(np.log(max(best[1], 1) / area)):
            best = (m, got)
        if veins or not got or 0.8 < got / area < 1.25:
            break
        scale *= np.clip((area / got) ** 0.5, 0.5, 2.0)
    return best[0]


def despeckle(tex, min_patch=MIN_PATCH, rounds=3):
    """Patches and holes below min_patch cells take the most common texture around them."""
    tex = tex.copy()
    for _ in range(rounds):
        changed = False
        for t in np.unique(tex):
            lab, k = ndimage.label(tex == t)
            if not k:
                continue
            sizes = np.bincount(lab.ravel())
            for i in np.nonzero(sizes[1:] < min_patch)[0] + 1:
                m = lab == i
                ring = ndimage.binary_dilation(m) & ~m
                around = tex[ring]
                around = around[around != t]
                if around.size:
                    tex[m] = np.bincount(around).argmax()
                    changed = True
        if not changed:
            break
    return tex


def three_way(tex):
    """Corner vertices where three or more textures meet (cells: each cell's lower left corner index)."""
    q = np.stack([tex[:-1, :-1], tex[1:, :-1], tex[:-1, 1:], tex[1:, 1:]])
    n = 1 + (q[1] != q[0]) + ((q[2] != q[0]) & (q[2] != q[1])) + ((q[3] != q[0]) & (q[3] != q[1]) & (q[3] != q[2]))
    return n >= 3


def soften_corners(tex, rounds=4):
    tex = tex.copy()
    for _ in range(rounds):
        v = three_way(tex)
        if not v.any():
            break
        cells = np.zeros(tex.shape, bool)
        for dj, di in ((0, 0), (1, 0), (0, 1), (1, 1)):
            cells[dj:dj + v.shape[0], di:di + v.shape[1]] |= v
        ts = np.unique(tex)
        counts = np.stack([ndimage.uniform_filter((tex == t).astype(float), 3, mode="nearest") for t in ts])
        own = np.take_along_axis(counts, np.searchsorted(ts, tex)[None], 0)[0]
        mode = ts[counts.argmax(0)]
        # Per corner only the weakest cell gives way: those with the least of their own texture around them.
        weak = cells & (own <= 4 / 9 + 1e-6) & (mode != tex)
        tex[weak] = mode[weak]
    return tex


def paint(f, model, seed=1, border=0):
    """f: cell features (cell_features.features) of the terrain, border included; border: cells outside the playable area on each
    side. Returns (texture index per cell, texture names)."""
    if border:
        inner = {k: v[border:-border, border:-border] for k, v in f.items()}
        tex, names, plain = _paint(inner, model, seed)
        # Long plain stretches: each edge row takes the most common main texture over EDGE_RUN cells along it.
        plain = plain.copy()
        for sl in ((0, slice(None)), (-1, slice(None)), (slice(None), 0), (slice(None), -1)):
            row = plain[sl]
            counts = np.stack([ndimage.uniform_filter1d((row == t).astype(float), EDGE_RUN, mode="nearest") for t in np.unique(row)])
            plain[sl] = np.unique(row)[counts.argmax(0)]
        out = np.pad(plain, border, mode="edge")
        out[border:-border, border:-border] = tex
        return out, names
    return _paint(f, model, seed)[:2]


def _paint(f, model, seed):
    rng = np.random.default_rng(seed)
    labels, P, m = texture_model.zone_probabilities(model, f)
    TP = texture_model.texture_probabilities(model, f)
    shape = P.shape[:2]
    tx = m["textures"]
    names = []

    def ti(n):
        if n not in names:
            names.append(n)
        return names.index(n)
    # 1. Zones.
    logp = np.log(np.stack([ndimage.gaussian_filter(P[..., k], ZONE_SMOOTH) for k in range(len(labels))], -1) + 1e-4)
    for k in range(len(labels)):
        logp[..., k] += ZONE_NOISE_AMOUNT * field(shape, ZONE_NOISE, rng)
    if "cliff" in labels:
        c = labels.index("cliff")
        logp[..., c] = np.where(f["slope"] >= STEEP, 50, np.where(f["to_cliff"] > 4, -50, logp[..., c]))
    zone = despeckle(np.argmax(logp, -1).astype(np.int16))
    mains = {z: max((m["zones"][z].get("variants") or {z: {"share": 1}}).items(), key=lambda x: x[1]["share"])[0] if z != "cliff"
             else max((n for n in m["zones"][z]["composition"] if tx.get(n, {}).get("role") == "cliff"), key=lambda n: m["zones"][z]["composition"][n])
             for z in labels}
    tex = np.full(shape, -1, np.int16)
    # 2. Families: the largest variant first, the others picked from its cells.
    for k, z in enumerate(labels):
        zm = zone == k
        e = m["zones"][z]
        if z == "cliff":
            continue
        variants = sorted((e.get("variants") or {z: {"share": 1.0, "patches": None}}).items(), key=lambda x: -x[1]["share"])
        tex[zm] = ti(variants[0][0])
        total = sum(v["share"] for _, v in variants)
        for n, v in variants[1:]:
            area = (v["patches"] or {}).get("area_cell_median", 400)
            sel = pick(zm & (tex == ti(variants[0][0])), v["share"] / total * zm.sum(), area, rng)
            tex[sel] = ti(n)
    # 3. Transitions where both sides are painted.
    for n, e in tx.items():
        if e["role"] != "transition" or not all(s in names for s in e["between"]):
            continue
        a, b = (ti(s) for s in e["between"])
        if not ((tex == a).any() and (tex == b).any()):
            continue
        half = (e.get("width") or 6) / 2 * (1 + TRANSITION_VARY * np.clip(field(shape, 8, rng), -2, 2))
        da = ndimage.distance_transform_edt(tex != a)
        db = ndimage.distance_transform_edt(tex != b)
        band = ((tex == a) & (db <= half)) | ((tex == b) & (da <= half))
        tex[band] = ti(n)
    # The rest of each zone's transitions as strands through it, where the zone's main texture lies.
    for k, z in enumerate(labels):
        zm = zone == k
        if z == "cliff" or z not in names:
            continue
        for n, share in (m["zones"][z].get("strands") or {}).items():
            if share < MIN_COMPOSITION:
                continue
            want = share * zm.sum()
            if want <= 0:
                continue
            host = zm & (tex == ti(z))
            spacing = min((tx.get(z, {}).get("patches") or {}).get("area_cell_median", 2500), 3000)
            sel = pick(host, want, spacing, rng, veins=True)
            tex[sel] = ti(n)
    # 4. Accents, in their zone's composition, inside their host (hosts first: an accent of an accent follows its host).
    def depth(n, d=0):
        h = tx.get(n, {}).get("host")
        return depth(h, d + 1) if h and tx.get(h, {}).get("role") == "accent" and d < 5 else d
    for k, z in enumerate(labels):
        if z == "cliff":
            continue
        zm = zone == k
        comp = m["zones"][z]["composition"]
        for n in sorted((n for n in comp if tx.get(n, {}).get("role") == "accent" and comp[n] >= MIN_COMPOSITION), key=depth):
            h = tx[n]["host"]
            if h not in names:
                continue
            area = (tx[n].get("patches") or {}).get("area_cell_median", 100)
            region = zm & (tex == ti(h))
            sel = pick(region, comp[n] * zm.sum(), area, rng, bias=preference(TP.get(n), region))
            tex[sel] = ti(n)
    # 5. Cliffs: textures ordered by how steep their cells are in the reference, on cells ranked by slope plus noise.
    if "cliff" in labels:
        cm = zone == labels.index("cliff")
        comp = m["zones"]["cliff"]["composition"]
        members = sorted((n for n in comp if tx.get(n, {}).get("role") in ("cliff", "accent", "edge") and comp[n] >= MIN_COMPOSITION),
                         key=lambda n: -tx[n]["steep"])
        if cm.any() and members:
            r = f["slope"][cm].argsort().argsort() / max(cm.sum() - 1, 1)
            score = r + 0.25 * field(shape, 6, rng)[cm]
            shares = np.array([comp[n] for n in members])
            edges = np.quantile(score, 1 - np.cumsum(shares / shares.sum())[:-1])
            pos = np.searchsorted(-edges, -score)   # steepest first
            out = np.array([ti(n) for n in members], np.int16)[np.clip(pos, 0, len(members) - 1)]
            tex[cm] = out
    tex[tex < 0] = ti(labels[0]) if labels[0] != "cliff" else 0
    plain = np.array([ti(mains[z]) for z in labels], np.int16)[zone]
    return despeckle(soften_corners(despeckle(tex))), names, plain


def stats(tex, names, txinfo):
    """Numbers to compare a painting with its reference: shares, typical patch size per texture, three-texture corners (hard
    edges in the game) and brightness steps across texture borders."""
    out = {"textures": {}}
    for t in np.unique(tex):
        m = tex == t
        out["textures"][names[t]] = {"share": float(m.mean()), "patch": cell_median(m)}
    out["three_way"] = int(three_way(tex).sum())
    br = np.array([txinfo.get(n, {}).get("brightness", 0.5) for n in names])
    steps = []
    for a, b in ((tex[:, :-1], tex[:, 1:]), (tex[:-1], tex[1:])):
        d = a != b
        steps.append(np.abs(br[a[d]] - br[b[d]]))
    s = np.concatenate(steps)
    out["border_cells"] = int(s.size)
    out["brightness_step_p90"] = float(np.percentile(s, 90)) if s.size else 0.0
    return out


def colours(tex, names, txinfo):
    pal = np.array([txinfo.get(n, {}).get("rgb", [255, 0, 255]) for n in names], np.uint8)
    return pal[tex][::-1]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 1
    if "--seed" in sys.argv:
        args.remove(str(seed))
    model, terrain = args[0], (args[1] if len(args) > 1 else args[0])
    txinfo = json.load(open(os.path.join(HERE, "work", "textures.json")))
    # A scanned reference, or another map scanned the same way into work/maps (ref_scan.scan with its WORK set there).
    d = os.path.join(HERE, "work", "refs", terrain)
    if not os.path.isdir(d):
        d = os.path.join(HERE, "work", "maps", terrain)
    fz = np.load(os.path.join(d, "features.npz"))
    f = {k: fz[k] for k in fz.files}
    info = json.load(open(os.path.join(d, "info.json")))
    bd = info["border"]
    tex, names = paint(f, model, seed, bd)
    ref = np.load(os.path.join(d, "textures.npy"))
    rnames = info["textures"]
    # Numbers on the playable area only.
    a, b = stats(ref[bd:-bd, bd:-bd], rnames, txinfo), stats(tex[bd:-bd, bd:-bd], names, txinfo)
    print(f"{'texture':26s} {'reference':>16s} {'painted':>16s}   (share, typical patch)")
    for n in sorted(set(a["textures"]) | set(b["textures"]), key=lambda n: -max(a["textures"].get(n, {}).get("share", 0), b["textures"].get(n, {}).get("share", 0))):
        ra, rb = a["textures"].get(n), b["textures"].get(n)
        fmt = lambda r: f"{r['share']:6.1%} {r['patch']:8.0f}" if r else " " * 15
        if max((ra or {}).get("share", 0), (rb or {}).get("share", 0)) >= 0.003:
            print(f"{n:26s} {fmt(ra):>16s} {fmt(rb):>16s}")
    for k in ("three_way", "border_cells", "brightness_step_p90"):
        print(f"{k:26s} {a[k]:16.2f} {b[k]:16.2f}")
    print("textures used", len(a["textures"]), len(b["textures"]))
    out = os.path.join(HERE, "work", "paint")
    os.makedirs(out, exist_ok=True)
    gap = np.full((tex.shape[0], 6, 3), 255, np.uint8)
    img = np.concatenate([colours(ref, rnames, txinfo), gap, colours(tex, names, txinfo)], 1)
    Image.fromarray(img).save(os.path.join(out, f"{model}__{terrain}.png"))


if __name__ == "__main__":
    main()
