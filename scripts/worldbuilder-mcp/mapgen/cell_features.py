# Texturing step 2 (DESIGN.md "Texturing: the plan"): features of every terrain cell, the same for reference maps and generated terrain,
# so that a texture model learned on references can be applied to new maps. Named locations (beach, hill top, plain) are combinations.
# Usage: cell_features.py [names...]   Reads work/refs/<name>/ (ref_scan.py), writes features.npz there. Without names: every scanned map.
# As a module: features(z, wet, impassable, objects, border, water_height) for any map.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
CELL = 10.0
STEEP = 0.6          # a cliff face, where texturing turns to cliff textures (as in textures_from_plan.py)
FLAT = 0.15
CLIFF_NEAR = 8       # cells within which a cell is at the top or the foot of a cliff
TREE = ("Tree", "Bush", "Shrub", "ArborVitae")

# Objects by kind: names for nature, the game data's EditorSorting / KindOf for the rest (town/work/gamedata/objs.json, town/gamedata.py).
_OBJS = None


def object_kind(template):
    global _OBJS
    if _OBJS is None:
        p = os.path.join(os.path.dirname(HERE), "town", "work", "gamedata", "objs.json")
        _OBJS = json.load(open(p)) if os.path.exists(p) else {}
    if template.startswith("Amb_"):
        return None
    if template.startswith(TREE) or "Palm" in template:
        return "tree"
    o = _OBJS.get(template, {})
    kinds, sort = o.get("kindof", []), o.get("EditorSorting", "")
    if "TECH_BUILDING" in kinds or "SUPPLY_SOURCE" in kinds or "SUPPLY_SOURCE_ON_PREVIEW" in kinds:
        return "tech"
    if sort == "STRUCTURE":
        return "building"
    if sort == "MISC_NATURAL" or template.startswith(("Rock", "Boulder")):
        return "rock"
    if sort == "MISC_MAN_MADE":
        return "prop"
    return None


def features(z, wet, impassable, objects, border, water_height=None):
    """z: vertex heights (world units). wet, impassable: cells (row 0 south). objects: dicts with template, x, y, kind.
    Returns a dict of cell arrays (float32), row 0 south, border included."""
    zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
    shape = zc.shape
    gy, gx = np.gradient(z, CELL)
    sv = np.hypot(gx, gy)
    slope = np.maximum.reduce([sv[:-1, :-1], sv[1:, :-1], sv[:-1, 1:], sv[1:, 1:]])
    f = {"z": zc, "slope": slope, "wet": wet.astype(np.float32), "impassable": impassable.astype(np.float32)}
    f["depth"] = np.where(wet, (water_height if water_height is not None else zc.max()) - zc, 0)
    # In cells, 0 on water (to_water) or on land (to_land); 999 when the map has none.
    f["to_water"] = ndimage.distance_transform_edt(~wet) if wet.any() else np.full(shape, 999.0)
    f["to_land"] = ndimage.distance_transform_edt(wet) if (~wet).any() else np.full(shape, 999.0)
    # Height against the surroundings at two scales: hill tops and ridges positive, hollows and valleys negative.
    f["rel_small"] = zc - ndimage.gaussian_filter(zc, 4)
    f["rel_large"] = zc - ndimage.gaussian_filter(zc, 15)
    # Convexity: minus the Laplacian of the smoothed surface, per cell squared; ridges and crests positive.
    f["convex"] = -ndimage.laplace(ndimage.gaussian_filter(zc, 2))
    # Cliffs: closed steep faces; distance to them and on which side (above the face or at its foot).
    steep = (slope >= STEEP) & ~wet
    steep = ndimage.binary_closing(steep, iterations=1)
    d, (iy, ix) = ndimage.distance_transform_edt(~steep, return_indices=True)
    f["to_cliff"] = d
    # Only near a cliff: far from one, the nearest face says nothing about a cell.
    f["cliff_side"] = np.where(steep | (d > CLIFF_NEAR), 0, np.sign(zc - ndimage.gaussian_filter(zc, 1)[iy, ix]))
    # Flat ground: the size of the connected flat area a cell belongs to (log10 of cells), 0 off flat ground.
    flat = (slope < FLAT) & ~wet
    lab, n = ndimage.label(flat)
    sizes = np.bincount(lab.ravel())
    f["flat_area"] = np.where(flat, np.log10(np.maximum(sizes[lab], 1)), 0)
    # Height within the map's land, 0..1 (rank), so that maps of different heights compare.
    land = ~wet
    ranks = np.zeros(shape, np.float32)
    if land.any():
        # Equal heights share a rank (the middle of their run), or flat ground would be striped by position.
        v = np.sort(zc[land])
        mid = (np.searchsorted(v, zc[land], "left") + np.searchsorted(v, zc[land], "right") - 1) / 2
        ranks[land] = mid / max(1, land.sum() - 1)
    f["height_rank"] = ranks
    jj, ii = np.indices(shape)
    f["to_edge"] = np.minimum.reduce([ii - border, jj - border, shape[1] - 1 - border - ii, shape[0] - 1 - border - jj]).astype(np.float32)
    # Context from objects: tree and rock density (per cell, smoothed over about 4 cells), distance to roads, buildings, Tech.
    dens = {k: np.zeros(shape, np.float32) for k in ("tree", "rock")}
    marks = {k: np.zeros(shape, bool) for k in ("building", "tech", "prop")}
    to_cell = lambda o: (int(o["y"] / CELL + border), int(o["x"] / CELL + border))
    for o in objects:
        if o.get("kind", "object") != "object":
            continue
        k = object_kind(o["template"])
        j, i = to_cell(o)
        if not (0 <= j < shape[0] and 0 <= i < shape[1]) or k is None:
            continue
        if k in dens:
            dens[k][j, i] += 1
        else:
            marks[k][j, i] = True
    for k in dens:
        f[k + "_density"] = ndimage.gaussian_filter(dens[k], 4) * 2 * math.pi * 16
    for k in marks:
        f["to_" + k] = ndimage.distance_transform_edt(~marks[k]) if marks[k].any() else np.full(shape, 999.0, np.float32)
    road = Image.new("1", (shape[1], shape[0]))
    dr = ImageDraw.Draw(road)
    rp = [o for o in objects if o.get("kind") in ("road_point", "bridge_point")]
    for a, b in zip(rp[0::2], rp[1::2]):
        dr.line([(a["x"] / CELL + border, a["y"] / CELL + border), (b["x"] / CELL + border, b["y"] / CELL + border)], fill=1, width=3)
    road = np.array(road, bool)
    f["to_road"] = ndimage.distance_transform_edt(~road) if road.any() else np.full(shape, 999.0, np.float32)
    return {k: np.asarray(v, np.float32) for k, v in f.items()}


def load_ref(d):
    info = json.load(open(os.path.join(d, "info.json")))
    z = np.load(os.path.join(d, "heights.npy"))
    shape = (z.shape[0] - 1, z.shape[1] - 1)
    def mask(name):
        m = np.array(Image.open(os.path.join(d, name)).convert("L"))[::-1] > 127
        out = np.zeros(shape, bool)
        out[:min(shape[0], m.shape[0]), :min(shape[1], m.shape[1])] = m[:shape[0], :shape[1]]
        return out
    return info, z, mask("water.png"), mask("passability.png"), json.load(open(os.path.join(d, "objects.json")))


def main():
    root = os.path.join(HERE, "work", "refs")
    names = sys.argv[1:] or sorted(os.listdir(root))
    for name in names:
        d = os.path.join(root, name)
        info, z, wet, imp, objs = load_ref(d)
        water = max([w for w in info.get("water") or [] if w is not None] or [0])
        f = features(z, wet, imp, objs, info["border"], water)
        np.savez_compressed(os.path.join(d, "features.npz"), **f)
        print(name, {k: round(float(np.percentile(v, 50)), 2) for k, v in f.items() if k in ("slope", "to_water", "flat_area", "tree_density")}, flush=True)


if __name__ == "__main__":
    main()
