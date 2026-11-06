# GoldCoastGen, stage B land (the sea is northern_island_sea.py's): roles first, the user's own textures from the template where painted.
# - plateau: dry grass, GrassMediumType35 with patches of the olive GrassMediumType32 and the darker GrassMediumType34 (all three from
#   the user's hints, Desert_2 family), in noise patches some 300 across (GrassMediumType32 as the base showed stripes over the plateau);
# - the user's hints on gentle ground as painted: crop fields, grass field squares, the sand strips along the cliff top, the dark grass
#   along the river;
# - beach: low gentle land at the sea, SandMediumType13 (the user's beach);
# - cliffs: the coast's CliffLargeType11 (tall faces); the ridge and the hills CliffLargeType12 and CliffMediumType20 in patches; the
#   ridge's gentler tops dark grass with rock patches;
# - river: the bed SandType3wet, the banks within BANK of the water GrassLargeType2 (the user's river band);
# - dirt (DirtMediumType12) under the supply docks.
# Writes masks and masks.json (largest first) for import_masks.py.
# Usage: gold_coast_paint.py <plan.json> <work dir (mapgen_work)>
import json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
WATER, RIVER, B, CELL = plan["water"]["height"], plan["river"]["height"], plan["border"], 10.0
BANK = 50.0
z = np.load(os.path.join(work, "heights_world.npy")).astype(np.float64)
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
cs = np.maximum.reduce([np.abs(z[1:, 1:] - z[:-1, :-1]), np.abs(z[1:, :-1] - z[:-1, 1:]), np.abs(z[1:, :-1] - z[:-1, :-1]),
                        np.abs(z[:-1, 1:] - z[:-1, :-1])]) / CELL
shape = zc.shape
cy, cx = np.indices(shape)
X, Y = (cx - B + 0.5) * CELL, (cy - B + 0.5) * CELL
rng = np.random.default_rng(plan.get("seed", 1) + 9)


def noise(size):
    n = ndimage.gaussian_filter(rng.standard_normal(shape), size)
    return n / (n.std() + 1e-9)


river_v = np.load(os.path.join(work, "stageA", "river_band.npy"))
river = river_v[:-1, :-1] | river_v[1:, 1:]
sea = (zc < WATER) & ~river
land = ~sea & ~river
steep = land & (cs >= 0.5)
names, tex = [], np.full(shape, -1, np.int32)


def put(name, mask):
    global tex
    if name not in names:
        names.append(name)
    tex = np.where(mask, names.index(name), tex)


# Plateau and everything gentle: dry grass in patches.
n1, n2 = noise(12), noise(8)
put("GrassMediumType35", land)
put("GrassMediumType32", land & (n1 > 0.45))
put("GrassMediumType34", land & (n2 > 1.0))
# The user's hints on gentle ground.
hints = json.load(open(os.path.join(work, "template", "textures.json")))
for name in ("SandMediumType11", "SandLargeType4Grassy", "GrassLargeType2", "GrassMediumType35", "GrassMediumType32", "GrassMediumType34",
             "CropsType1"):
    m = np.array(Image.open(hints[name]).convert("L"))[::-1] > 127
    mm = np.zeros(shape, bool)
    mm[:m.shape[0], :m.shape[1]] = m
    put(name, mm & land & ~steep)
# Beach.
near_sea = ndimage.distance_transform_edt(~sea) <= 30
put("SandMediumType13", land & ~steep & (zc < 74) & near_sea)
# River bed and banks.
d_river = ndimage.distance_transform_edt(~river) * CELL
put("GrassLargeType2", land & ~steep & (d_river <= BANK * (1 + 0.3 * noise(4))))
put("SandType3wet", river)
# Cliffs and mountains.
coast = X < 1700
put("CliffLargeType11", steep & coast)
n3 = noise(6)
put("CliffLargeType12", steep & ~coast & (n3 >= 0))
put("CliffMediumType20", steep & ~coast & (n3 < 0))
# The ridge's gentler ground: dark grass with rock patches (CliffMediumType19 tiled into rows that looked like haystacks).
ridge_top = land & ~steep & (zc > 124) & (X > 2550)
put("GrassLargeType2", ridge_top)
put("CliffMediumType20", ridge_top & (noise(5) > 0.7))
# Dirt under the docks.
for o in json.load(open(os.path.join(work, "objects.json"))):
    if o["template"] == "SupplyDock":
        d = np.hypot(X - o["x"], Y - o["y"]) / (1 + 0.15 * noise(2.5))
        put("DirtMediumType12", land & ~steep & (d <= 85))

out = os.path.join(work, "stageB")
os.makedirs(out, exist_ok=True)
masks = []
for k, n in enumerate(names):
    m = tex == k
    if not m.any():
        continue
    p = os.path.join(out, f"{n}.png")
    Image.fromarray((m[::-1] * 255).astype(np.uint8)).save(p)
    masks.append({"texture": n, "path": p, "cells": int(m.sum())})
masks.sort(key=lambda m: -m["cells"])
json.dump({"masks": masks}, open(os.path.join(out, "masks.json"), "w"), indent=1)
print({m["texture"]: m["cells"] for m in masks})
