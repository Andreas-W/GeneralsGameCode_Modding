# SnakeIslandsGen, stage B land (the sea and beaches are northern_island_sea.py's, plan beach_texture): textures by role from the
# tropical palette the user picked on the TropicTrefoil template.
# - ground: GrassMediumType26 with patches of GrassMediumType31 (noise some 250 across) and a few SandMediumType13grassy4 clearings;
# - toward the beaches: a band of SandMediumType7GrassyLight with a wandering inner edge, then the beach sand (SandMediumType6Light);
# - cliffs: CliffMediumType2 and CliffMediumType3 in patches; the plateau rim (gentle ground within 40 of a cliff top) RocksType4 and
#   SandMediumType13 in patches, so the cliff edge is not a hard line between grass and rock;
# - the ramps: SandMediumType13 tracks; dirt (DirtMediumType11) under the starts; cobblestone on the drawbridge quays.
# Writes masks and masks.json (largest first) for import_masks.py.
# Usage: snake_islands_paint.py <plan.json> <work dir (mapgen_work)>
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
WATER, B, CELL = plan["water"]["height"], plan["border"], 10.0
LOW, HIGH = plan["levels"][0]["z"], plan["levels"][1]["z"]
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


sea = zc < WATER
land = ~sea
steep = land & (cs >= 0.5)
names, tex = [], np.full(shape, -1, np.int32)


def put(name, mask):
    global tex
    if name not in names:
        names.append(name)
    tex = np.where(mask, names.index(name), tex)


gentle = land & ~steep
put("GrassMediumType26", land)
put("GrassMediumType31", gentle & (noise(12) > 0.35))
put("SandMediumType13grassy4", gentle & (noise(9) > 1.5))
# Toward the beaches: grassy sand, its inner edge wandering.
d_sea = ndimage.distance_transform_edt(~sea) * CELL
lowland = zc < (LOW + HIGH) / 2
# (210 +- 70 turned most of the narrow lowlands sandy; the beaches of the second version are some 110 wide themselves)
rocky = np.load(os.path.join(work, "mock", "rocky.npy")) if os.path.exists(os.path.join(work, "mock", "rocky.npy")) else np.zeros(shape, bool)
put("SandMediumType7GrassyLight", gentle & lowland & (d_sea < 190 + 50 * noise(5)))
put("SandMediumType6Light", gentle & lowland & (zc < WATER + 5.5 + 1.2 * noise(3)) & (d_sea < 260) & ~rocky)
# Rocky stretches of the low shore: the face takes cliff textures below; on top, rock near the edge in patches with sand between
# (all rock read as a flat dark strip).
n_rk = noise(3)
d_edge = ndimage.distance_transform_edt(~(cs >= 0.5)) * CELL
put("SandMediumType13", land & rocky & ~steep)
put("RocksType4", land & rocky & ~steep & (d_edge < 45 + 25 * n_rk) & (noise(2) > -0.4))
# Plateau rim and cliffs.
d_steep = ndimage.distance_transform_edt(~steep) * CELL
rim = gentle & ~lowland & (d_steep < 40 + 25 * noise(4))
n_rim = noise(5)
put("RocksType4", rim & (n_rim > 0.2))
put("SandMediumType13", rim & (n_rim <= 0.2) & (n_rim > -0.6))
n_cl = noise(6)
put("CliffMediumType2", steep & (n_cl >= 0))
put("CliffMediumType3", steep & (n_cl < 0))
# Ramps (stage A's report), starts, quays.
report = json.load(open(os.path.join(work, "stageA", "report.json")))
for r in report["ramps"]:
    d = np.hypot(X - r["at"][0], Y - r["at"][1]) / (1 + 0.2 * noise(2))
    put("SandMediumType13", land & (cs < 0.4) & (d < 90))
layout = json.load(open(os.path.join(work, "mock", "layout.json")))
for sx, sy in layout["starts"].values():
    d = np.hypot(X - sx, Y - sy) / (1 + 0.2 * noise(2.5))
    put("DirtMediumType11", gentle & (d <= 95))
for b in layout["drawbridges"]:
    a = math.radians(b["angle_deg"])
    t = np.abs((X - b["x"]) * math.cos(a) + (Y - b["y"]) * math.sin(a))
    s = np.abs(-(X - b["x"]) * math.sin(a) + (Y - b["y"]) * math.cos(a))
    put("CobblestoneType1", land & (t > 100) & (t < 200 + 15 * noise(2)) & (s < 50 + 10 * noise(2)))

# Water cells take the texture of the nearest land cell: the sea script leaves the cells next to land (up to some 8 with its noise) to
# this painter, and unpainted they kept the new map's AsphaltType1, which showed through the thin water of the gentle beaches.
_, (ly, lx) = ndimage.distance_transform_edt(~land, return_indices=True)
tex = np.where(sea, tex[ly, lx], tex)

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
