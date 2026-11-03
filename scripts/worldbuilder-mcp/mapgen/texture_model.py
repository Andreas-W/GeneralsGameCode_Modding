# Texturing step 3 (DESIGN.md "Texturing: the plan"): a texture model per reference map, for the painter (step 4).
# - Roles from what borders what: an accent lies inside one host texture's patches (most of its border is the host), a transition
#   lies between two textures that rarely meet directly, cliff textures cover steep faces, edge detail lines them.
# - Zones: a main texture with its accents (cliff textures form one zone). Which zone goes where is a random forest on the cell
#   features (probabilities, since painters are not consistent), trained on the whole map; scored on one half predicting the other.
# - Patch statistics per texture (size, raggedness, elongation and direction, how much it mixes), transition widths, accent density.
# Usage: texture_model.py [names...]   Reads work/refs/<name>/ (ref_scan.py, cell_features.py); writes work/models/<name>.json,
#        <name>.pkl (the zone classifier) and <name>.png (textures, zones, zones predicted for the right half). Needs scikit-learn.
import json, os, pickle, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
REFS = os.path.join(HERE, "work", "refs")
OUT = os.path.join(HERE, "work", "models")
# Absolute height and distance to the map edge are left out: they do not carry over to another map (the edge also lets a mirrored
# layout's other half give the answer away).
FEATS = ["slope", "to_water", "depth", "rel_small", "rel_large", "convex", "to_cliff", "cliff_side", "flat_area", "height_rank",
         "tree_density", "rock_density", "to_road", "to_building", "to_tech", "to_prop"]
STEEP = 0.6
HOST = 0.6           # an accent's border: at least this share on its host
MINOR = 0.005        # textures below this share of the map are accents of their main neighbour
TRANS_EACH = 0.2     # a transition's border: at least this share on each side, together TRANS_BOTH
TRANS_BOTH = 0.75
CLIFF = 0.4          # share of a texture's cells on steep faces for a cliff texture (CLIFF_LOW when nearly all lie by a cliff)
CLIFF_LOW = 0.25
EDGE = 0.6           # share within EDGE_NEAR cells of a cliff for edge detail
EDGE_NEAR = 3
MIN_CELLS = 30       # smaller textures are kept as accents of their main neighbour, without statistics
# Families: zones that border each other, look alike and that location does not tell apart (on one half of the map predicting the
# other, a pair's cells go to the wrong one of the two at least MIX_ERROR of the time) are one zone, its parts mixed by noise.
MIX_ERROR = 0.3
MIX_BORDER = 0.1     # share of the smaller zone's border on the other
MIX_BRIGHT = 0.15    # brightness difference at most


def adjacency(tex, T):
    a = np.zeros((T, T))
    for p, q in ((tex[:, :-1], tex[:, 1:]), (tex[:-1], tex[1:])):
        np.add.at(a, (p.ravel(), q.ravel()), 1)
        np.add.at(a, (q.ravel(), p.ravel()), 1)
    return a


def border_shares(adj):
    off = adj - np.diag(np.diag(adj))
    return off / np.maximum(off.sum(1, keepdims=True), 1)


def roles(tex, f, names):
    T = len(names)
    count = np.bincount(tex.ravel(), minlength=T)
    adj = adjacency(tex, T)
    nb = border_shares(adj)
    steep = np.array([(f["slope"][tex == t] >= STEEP).mean() if count[t] else 0 for t in range(T)])
    near = np.array([(f["to_cliff"][tex == t] <= EDGE_NEAR).mean() if count[t] else 0 for t in range(T)])
    role, host = {}, {}
    for t in range(T):
        if count[t] and (steep[t] >= CLIFF or steep[t] >= CLIFF_LOW and near[t] >= 0.7):
            role[t] = "cliff"
    # Accents and transitions, smallest first, so that an accent's host is settled as a main texture or a further accent.
    for t in sorted(range(T), key=lambda t: count[t]):
        if not count[t] or t in role:
            continue
        order = [j for j in np.argsort(-nb[t]) if count[j]]
        a = order[0] if order else t
        if count[a] > count[t] and (nb[t, a] >= HOST or count[t] < MINOR * tex.size) or count[t] < MIN_CELLS:
            role[t], host[t] = "accent", a
            continue
        if len(order) > 1:
            b = order[1]
            # The two sides are larger and meet directly less than either meets the transition.
            direct = adj[a, b] if count[a] > count[t] and count[b] > count[t] else np.inf
            # Nothing lies between a cliff and its surroundings but edge detail.
            if role.get(a) != "cliff" and role.get(b) != "cliff" and near[t] < EDGE and nb[t, a] >= TRANS_EACH and nb[t, b] >= TRANS_EACH and nb[t, a] + nb[t, b] >= TRANS_BOTH and direct < min(adj[t, a], adj[t, b]):
                role[t], host[t] = "transition", (a, b)
                continue
        if near[t] >= EDGE:
            role[t] = "edge"
            continue
        role[t] = "main"

    def root(t, seen=()):
        if role.get(t) == "accent" and t not in seen:
            return root(host[t], seen + (t,))
        return t
    # Transitions join two main textures (or cliffs); an accent side counts as its host.
    for t, r in role.items():
        if r == "transition":
            host[t] = tuple(root(x) for x in host[t])
    return role, host, root, count, nb, steep, near


def zones_of(tex, names, role, host, root):
    """Zone per texture: the main (or edge) texture it belongs to; cliffs share one zone; transitions go to the side they border most."""
    T = len(names)
    cliff = [t for t in range(T) if role.get(t) == "cliff"]
    zone = {}
    for t in range(T):
        r = role.get(t)
        if r is None:
            continue
        if r == "cliff":
            zone[t] = "cliff"
        elif r == "transition":
            zone[t] = None
        else:
            # An accent of a transition goes with the transition's cells.
            z = root(t)
            zone[t] = "cliff" if role.get(z) == "cliff" else None if role.get(z) == "transition" else names[z]
    labels = sorted({z for z in zone.values() if z})
    zmap = np.full(tex.shape, -1, np.int16)
    for t, z in zone.items():
        if z:
            zmap[tex == t] = labels.index(z)
    # Transition cells take the nearest zone.
    if (zmap < 0).any():
        _, (iy, ix) = ndimage.distance_transform_edt(zmap < 0, return_indices=True)
        zmap = zmap[iy, ix]
    return labels, zmap


def patch_stats(m):
    """Connected patches of a mask: sizes, raggedness (perimeter over a circle's of the same area), elongation and direction."""
    lab, n = ndimage.label(m)
    if not n:
        return None
    areas = np.bincount(lab.ravel())[1:]
    edge = m & ~ndimage.binary_erosion(m, border_value=1)
    per = np.bincount(lab[edge], minlength=n + 1)[1:]
    big = np.nonzero(areas >= 20)[0] + 1
    elong, angles, weights = [], [], []
    for i, sl in zip(range(1, n + 1), ndimage.find_objects(lab)):
        if i not in big:
            continue
        yy, xx = np.nonzero(lab[sl] == i)
        c = np.cov(np.stack([xx, yy]).astype(float))
        w, v = np.linalg.eigh(c)
        elong.append(float(np.sqrt(w[1] / max(w[0], 1e-6))))
        angles.append(float(np.degrees(np.arctan2(v[1, 1], v[0, 1])) % 180))
        weights.append(int(areas[i - 1]))
    ragged = per / (2 * np.sqrt(np.pi * areas))
    # The size of the patch a typical cell lies in (area-weighted median): many specks hardly count, unlike in area_median.
    srt = np.sort(areas)
    cell_median = float(srt[np.searchsorted(np.cumsum(srt), srt.sum() / 2)])
    out = {"patches": int(n), "area_median": float(np.median(areas)), "area_cell_median": cell_median, "area_p90": float(np.percentile(areas, 90)),
           "area_max": int(areas.max()), "ragged": float(np.average(ragged, weights=areas))}
    if elong:
        out["elongation"] = round(float(np.average(elong, weights=weights)), 2)
        # Main direction of elongated patches (degrees, 0 = east, counterclockwise), and how consistent it is (0..1).
        a = np.radians(np.array(angles) * 2)
        wv = np.array(weights) * (np.array(elong) > 1.5)
        if (wv > 0).sum() >= 5:
            s, c = (wv * np.sin(a)).sum() / wv.sum(), (wv * np.cos(a)).sum() / wv.sum()
            out["direction"] = round(float(np.degrees(np.arctan2(s, c)) / 2 % 180), 0)
            out["direction_strength"] = round(float(np.hypot(s, c)), 2)
    return out


def transition_width(tex, t, a, b):
    """Typical thickness of a transition band: distance from one side to the other across it, in cells (median)."""
    da = ndimage.distance_transform_edt(tex != a)
    db = ndimage.distance_transform_edt(tex != b)
    m = tex == t
    if not m.any():
        return None
    w = (da + db)[m] - 1
    return float(np.median(w))


def half_split(X, y, T):
    """Train on the left half, predict the right half. Returns the prediction (right half), accuracy, most-common accuracy and the
    confusion matrix (rows: true zone)."""
    from sklearn.ensemble import RandomForestClassifier
    left = np.zeros(y.shape, bool)
    left[:, : y.shape[1] // 2] = True
    clf = RandomForestClassifier(n_estimators=40, min_samples_leaf=20, n_jobs=-1, random_state=1)
    clf.fit(X[left][::3], y[left][::3])
    pred = clf.predict(X[~left])
    true = y[~left]
    conf = np.zeros((T, T))
    np.add.at(conf, (true, pred), 1)
    return pred.reshape(y.shape[0], -1), float((pred == true).mean()), float((true == np.bincount(y[left], minlength=T).argmax()).mean()), conf


def families(zmap, labels, X, tx):
    """Merge zones into families (see MIX_ERROR). Returns the family of each zone (by index) and the confusion before."""
    T = len(labels)
    _, acc, _, conf = half_split(X, zmap, T)
    zadj = adjacency(zmap, T)
    nbz = border_shares(zadj)
    parent = list(range(T))

    def find(i):
        while parent[i] != i:
            i = parent[i]
        return i
    pairs = []
    for a in range(T):
        for b in range(a + 1, T):
            if "cliff" in (labels[a], labels[b]):
                continue
            na, nb_ = conf[a, a] + conf[a, b], conf[b, b] + conf[b, a]
            if not na or not nb_:
                continue
            err = (conf[a, b] / na + conf[b, a] / nb_) / 2
            bright = abs(tx.get(labels[a], {}).get("brightness", 0) - tx.get(labels[b], {}).get("brightness", 1))
            if err >= MIX_ERROR and max(nbz[a, b], nbz[b, a]) >= MIX_BORDER and bright <= MIX_BRIGHT:
                pairs.append((err, a, b))
    for _, a, b in sorted(pairs, reverse=True):
        parent[find(b)] = find(a)
    return [find(i) for i in range(T)], acc


def colour_image(tex, names, tx):
    pal = np.array([tx.get(n, {}).get("rgb", [255, 0, 255]) for n in names], np.uint8)
    return pal[tex][::-1]


def build(name, tx, score=True):
    from sklearn.ensemble import RandomForestClassifier
    info = json.load(open(os.path.join(REFS, name, "info.json")))
    b = info["border"]
    tex = np.load(os.path.join(REFS, name, "textures.npy"))[b:-b, b:-b]
    fz = np.load(os.path.join(REFS, name, "features.npz"))
    f = {k: fz[k][b:-b, b:-b] for k in fz.files}
    names = info["textures"]
    role, host, root, count, nb, steep, near = roles(tex, f, names)
    labels, zmap = zones_of(tex, names, role, host, root)
    N = tex.size
    zone_border = np.zeros(tex.shape, bool)
    zone_border[:, 1:] |= zmap[:, 1:] != zmap[:, :-1]
    zone_border[1:] |= zmap[1:] != zmap[:-1]
    far = ndimage.distance_transform_edt(~zone_border) if zone_border.any() else np.full(tex.shape, 999.0)
    strand = np.zeros(tex.shape, bool)
    textures = {}
    for t in sorted(role, key=lambda t: -count[t]):
        e = {"role": role[t], "share": round(float(count[t] / N), 4), "steep": round(float(steep[t]), 2), "near_cliff": round(float(near[t]), 2)}
        e.update({k: tx[names[t]][k] for k in ("brightness", "hue", "saturation", "tile_cells") if names[t] in tx})
        if role[t] == "accent":
            e["host"] = names[host[t]]
            z = root(t)
            e["zone"] = "cliff" if role.get(z) == "cliff" else names[z]
        if role[t] == "transition":
            e["between"] = [names[x] for x in host[t]]
            e["width"] = transition_width(tex, t, *host[t])
            # Share of its cells away from any zone border: strands through a zone rather than a band between zones.
            strand[tex == t] = far[tex == t] > max(3, (e["width"] or 0) / 2 + 1)
            e["strand_share"] = round(float(strand[tex == t].mean()), 3)
        e["borders"] = {names[j]: round(float(nb[t, j]), 2) for j in np.argsort(-nb[t])[:4] if nb[t, j] >= 0.05}
        if count[t] >= MIN_CELLS:
            e["patches"] = patch_stats(tex == t)
        textures[names[t]] = e
    X = np.stack([f[k] for k in FEATS], -1)
    # Families: the largest zone names the family; the merged zones are its variants, with their own patch statistics.
    fam, acc_before = families(zmap, labels, X, tx) if score and len(labels) > 1 else (list(range(len(labels))), None)
    big = {}
    for i, r in enumerate(fam):
        if (zmap == i).sum() > (zmap == big.get(r, i)).sum() or r not in big:
            big[r] = i
    flabels = sorted({labels[big[r]] for r in fam})
    fz = np.array([flabels.index(labels[big[r]]) for r in fam], np.int16)[zmap]
    zones = {}
    for i, z in enumerate(flabels):
        m = fz == i
        parts = [labels[j] for j in range(len(labels)) if labels[big[fam[j]]] == z]
        members = [n for n, e in textures.items() if e.get("zone", n if e["role"] in ("main", "edge") else None) in parts
                   or (z == "cliff" and e["role"] == "cliff")]
        inside = tex[m]
        comp = {names[t]: round(float((inside == t).mean()), 3) for t in np.unique(inside)}
        zones[z] = {"share": round(float(m.mean()), 4), "members": members,
                    "composition": dict(sorted(comp.items(), key=lambda x: -x[1])), "patches": patch_stats(m)}
        st = {names[t]: round(float((strand[m] & (inside == t)).mean()), 3) for t in np.unique(inside[strand[m]])}
        if st:
            zones[z]["strands"] = st
        if len(parts) > 1:
            zones[z]["variants"] = {p: {"share": round(float((zmap[m] == labels.index(p)).mean()), 3),
                                        "patches": patch_stats(zmap == labels.index(p))} for p in parts}
    labels, zmap = flabels, fz
    # Which zones meet, and how: through a transition texture or directly.
    zadj = adjacency(zmap, len(labels))
    meets = []
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            if zadj[i, j] >= 20:
                via = [n for n, e in textures.items() if e["role"] == "transition" and set(e["between"]) == {labels[i], labels[j]}]
                meets.append({"zones": [labels[i], labels[j]], "border_cells": int(zadj[i, j]), "via": via})
    y = zmap
    result = {"name": name, "textures": textures, "zones": zones, "meets": meets, "features": FEATS,
              "most_common_zone": float(np.bincount(y.ravel()).max() / N)}
    img = [colour_image(tex, names, tx)]
    zpal = np.array([tx.get(z, {}).get("rgb", [90, 80, 70]) if z != "cliff" else [70, 60, 55] for z in labels], np.uint8)
    img.append(zpal[zmap][::-1])
    if score:
        left = np.zeros(tex.shape, bool)
        left[:, : tex.shape[1] // 2] = True
        pred, result["half_map_zone_accuracy"], result["half_map_most_common"], _ = half_split(X, y, len(labels))
        result["half_map_zone_accuracy_before_families"] = acc_before
        p = zmap.copy()
        p[~left] = pred.ravel()
        img.append(zpal[p][::-1])
    clf = RandomForestClassifier(n_estimators=60, min_samples_leaf=20, n_jobs=-1, random_state=1)
    clf.fit(X[::2, ::2].reshape(-1, len(FEATS)), y[::2, ::2].ravel())
    result["importance"] = {k: round(float(v), 3) for k, v in sorted(zip(FEATS, clf.feature_importances_), key=lambda x: -x[1])}
    os.makedirs(OUT, exist_ok=True)
    # Texture by location, for where accents and transitions lie inside their zone (the painter adds it to its noise).
    tclf = RandomForestClassifier(n_estimators=40, min_samples_leaf=40, n_jobs=-1, random_state=1)
    tclf.fit(X[::2, ::2].reshape(-1, len(FEATS)), tex[::2, ::2].ravel())
    pickle.dump({"labels": labels, "features": FEATS, "classifier": clf, "texture_labels": names, "texture_classifier": tclf},
                open(os.path.join(OUT, name + ".pkl"), "wb"))
    json.dump(result, open(os.path.join(OUT, name + ".json"), "w"), indent=1)
    gap = np.full((tex.shape[0], 6, 3), 255, np.uint8)
    Image.fromarray(np.concatenate(sum([[im, gap] for im in img], [])[:-1], 1)).save(os.path.join(OUT, name + ".png"))
    return result


def texture_probabilities(name, f):
    """Per cell the probability of each of the reference's textures by location alone. Returns {texture: array}."""
    m = pickle.load(open(os.path.join(OUT, name + ".pkl"), "rb"))
    X = np.stack([f[k] for k in m["features"]], -1)
    c = m["texture_classifier"]
    p = c.predict_proba(X.reshape(-1, X.shape[-1])).reshape(X.shape[:2] + (-1,))
    return {m["texture_labels"][int(k)]: p[..., i] for i, k in enumerate(c.classes_)}


def zone_probabilities(name, f):
    """For the painter: per cell the probability of each of the reference's zones, from cell features (cell_features.features)
    of any map. Returns (labels, array rows x cols x zones) and the model's JSON."""
    m = pickle.load(open(os.path.join(OUT, name + ".pkl"), "rb"))
    X = np.stack([f[k] for k in m["features"]], -1)
    p = m["classifier"].predict_proba(X.reshape(-1, X.shape[-1])).reshape(X.shape[:2] + (-1,))
    labels = [m["labels"][int(c)] for c in m["classifier"].classes_]
    return labels, p, json.load(open(os.path.join(OUT, name + ".json")))


def main():
    tx = json.load(open(os.path.join(HERE, "work", "textures.json")))
    names = sys.argv[1:] or sorted(os.listdir(REFS))
    for n in names:
        r = build(n, tx)
        rc = {}
        for e in r["textures"].values():
            rc[e["role"]] = rc.get(e["role"], 0) + 1
        print(f"{n:26s} zones {len(r['zones']):2d}  zone accuracy {r.get('half_map_zone_accuracy', 0):.2f} "
              f"(most common {r.get('half_map_most_common', 0):.2f})  roles {rc}", flush=True)


if __name__ == "__main__":
    main()
