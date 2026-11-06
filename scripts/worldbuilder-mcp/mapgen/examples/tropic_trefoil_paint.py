# TropicTrefoilGen, stage B land: the user's own textures from the template, used as hints (tropic_trefoil_stage_a.py turned player 1's
# painted wedge onto the other two: mock/hints.npy). The template was painted in strokes with gaps (the default AsphaltType1 between them)
# and on the template's terrain, so:
# - every land cell without a hint takes the nearest hint; a 3x3 majority softens single-cell specks;
# - steep cells and mountains (stage A's cliffs, the rims and neck peaks) take the nearest cliff hint (CliffMediumType2 / 3); cliff hints that ended on gentle
#   ground take the nearest other hint;
# - low gentle land at the water takes the user's beach sand (SandMediumType6Light); the sea is left to northern_island_sea.py;
# - the ground follows the objects, as the user painted it under player 1's: a cobblestone core with a dirt fringe under Tech buildings,
#   dirt under docks, supply piles and starts, rocks under guard towers. The turned cobblestone and dirt hints that have no object on
#   them any more (the objects moved) take the surrounding ground.
# Writes one mask per texture (cells, top row north) and masks.json, largest first, for import_masks.py.
# Usage: tropic_trefoil_paint.py <plan.json> <work dir (mapgen_work)>
import json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
WATER = plan["water"]["height"]
BEACH = "SandMediumType6Light"
hint = np.load(os.path.join(work, "mock", "hints.npy")).astype(np.int32)
names = json.load(open(os.path.join(work, "mock", "hints.json")))["names"]
z = np.load(os.path.join(work, "heights_world.npy")).astype(np.float64)
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
cs = np.maximum.reduce([np.abs(z[1:, 1:] - z[:-1, :-1]), np.abs(z[1:, :-1] - z[:-1, 1:]), np.abs(z[1:, :-1] - z[:-1, :-1]),
                        np.abs(z[:-1, 1:] - z[:-1, :-1])]) / 10.0
land = zc >= WATER - 1
cliff_ids = [k for k, n in enumerate(names) if n.startswith("Cliff")]
is_cliff_hint = np.isin(hint, cliff_ids)


def nearest(src_mask):
    # For every cell, the hint of the nearest cell in src_mask.
    _, (iy, ix) = ndimage.distance_transform_edt(~src_mask, return_indices=True)
    return hint[iy, ix]


# Mountains (the rims and neck peaks, above MOUNT) are cliff textures all over, as the user painted player 1's rim: their gentler tops
# took the nearest ground hint and looked like grass craters and flat grey domes.
MOUNT = 122.0
steep = land & ((cs >= 0.55) | (zc > MOUNT))
B = plan["border"]
CELL = 10.0
cy_, cx_ = np.indices(hint.shape)
Xc, Yc = (cx_ - B + 0.5) * CELL, (cy_ - B + 0.5) * CELL
objs = json.load(open(os.path.join(work, "objects.json")))
starts = [(o["x"], o["y"]) for o in json.load(open(os.path.join(work, "mock", "objects_turned.json"))) if o["kind"] == "waypoint"]
# (texture, core radius, fringe texture, fringe width) by object
UNDER = {"TechOilDerrick": ("CobblestoneType1", 45, "DirtMediumType11", 30), "TechOilRefinery": ("CobblestoneType1", 80, "DirtMediumType11", 35),
         "TechConstructionYard": ("CobblestoneType1", 95, "DirtMediumType11", 35), "SupplyDock": ("DirtMediumType11", 85, None, 0),
         "SupplyPileSmall": ("DirtMediumType11", 45, None, 0), "GuardTower": ("RocksType4", 30, None, 0)}
spots = [(o["x"], o["y"]) + UNDER[o["template"]] for o in objs if o["template"] in UNDER] + [(x, y, "DirtMediumType11", 90, None, 0) for x, y in starts]
rng = np.random.default_rng(plan.get("seed", 1) + 5)
wob = ndimage.gaussian_filter(rng.standard_normal(hint.shape), 2.5)
wob = wob / (wob.std() + 1e-9)
near_obj = np.zeros(hint.shape, bool)
for x, y, *_ in spots:
    near_obj |= np.hypot(Xc - x, Yc - y) < 200
object_ids = [names.index(n) for n in ("CobblestoneType1", "DirtMediumType11")]
stray = np.isin(hint, object_ids) & ~near_obj
hint = np.where(stray, -1, hint)
ground_hint = nearest((hint >= 0) & ~is_cliff_hint)
cliff_hint = nearest(is_cliff_hint)
tex = np.where(steep, cliff_hint, np.where((hint >= 0) & ~is_cliff_hint, hint, ground_hint))
# 3x3 majority on the gentle land only.
gentle = land & ~steep
votes = np.stack([ndimage.uniform_filter((tex == k).astype(np.float32), 3) for k in range(len(names))])
tex = np.where(gentle & ~np.isin(votes.argmax(0), cliff_ids), votes.argmax(0), tex)
# Beach: gentle land below WATER + 10 within 6 cells of the water.
near_water = ndimage.distance_transform_edt(land) <= 6
beach = land & ~steep & (zc < WATER + 10) & near_water & (ndimage.maximum_filter(cs, 3) < 0.3)
beach = ndimage.binary_opening(beach, iterations=1)
tex = np.where(beach, names.index(BEACH), tex)
# Under the objects: fringe first, then the core over it; irregular edges (radius wobbles by 15 %).
for x, y, core, rad, fringe, fw in spots:
    dist = np.hypot(Xc - x, Yc - y) / (1 + 0.15 * wob)
    if fringe:
        tex = np.where(land & ~steep & (dist <= rad + fw), names.index(fringe), tex)
    tex = np.where(land & ~steep & (dist <= rad), names.index(core), tex)
tex = np.where(land, tex, -1)

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
json.dump({"masks": masks, "from": "template hints"}, open(os.path.join(out, "masks.json"), "w"), indent=1)
print({m["texture"]: m["cells"] for m in masks}, "steep", int(steep.sum()), "beach", int(beach.sum()))
