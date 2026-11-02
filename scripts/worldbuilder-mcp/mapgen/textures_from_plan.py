# Stage B of map generation (DESIGN.md): ground and underwater textures for the terrain of stage A, from one reference map's textures.
# The plan gives, per role (deep water, shallows, shore, main level, high ground, slopes, cliffs, mountains), the reference's textures
# with the share each covers there, as measured on it. Each role's area is split among them by smooth noise so that the shares hold
# and the patches are large and irregular.
# Writes one mask per layer (cells, top row north) and textures.json listing them in painting order; each is meant for
# terrain_import_mask(target="texture", fit="exact"), later ones over earlier ones. Then terrain.reblend and terrain.cliff_map.
# Usage: textures_from_plan.py <plan.json> <heights_world.npy> <impassable.png (cells)> <out dir> [seed]
import json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "cliffs"))
from cliff_gen import CELL, noise


def main():
    plan = json.load(open(sys.argv[1]))
    zv = np.load(sys.argv[2])
    imp = np.array(Image.open(sys.argv[3]).convert("L"))[::-1] > 127
    out = sys.argv[4]
    seed = int(sys.argv[5]) if len(sys.argv) > 5 else plan.get("seed", 1)
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(seed)
    style = plan["styles"]["terrain_textures"]
    roles = style["roles"]
    water = plan["water"]["height"]
    levels = [l["z"] for l in plan["levels"] if isinstance(l["z"], (int, float))]
    # Cells: the mean of their four corners; slope: the steepest corner.
    z = (zv[:-1, :-1] + zv[1:, :-1] + zv[:-1, 1:] + zv[1:, 1:]) / 4
    shape = z.shape
    gy, gx = np.gradient(zv, CELL)
    sv = np.hypot(gx, gy)
    slope = np.maximum.reduce([sv[:-1, :-1], sv[1:, :-1], sv[:-1, 1:], sv[1:, 1:]])
    wet = z < water
    from_land = ndimage.distance_transform_edt(wet)
    from_water = ndimage.distance_transform_edt(~wet)
    # Fuzziness: role edges and patches wander at large and small scales, and single cells scatter across them, so that no texture
    # area reads as a smooth blob.
    fuzz = style.get("fuzz", {"patch_scales": [[22, 1.0], [8, 0.4]], "scatter": 0.0, "edge": 2.5, "level_edge": 0.0})
    edge = noise(shape, rng, [(12, 1.0), (5, 0.4), (2, 0.4)]) * fuzz["edge"]
    z_edge = z + noise(shape, rng, [(10, 1.0), (3, 0.5)]) * fuzz["level_edge"]

    # Role areas, later ones over earlier ones.
    steep = (slope >= 0.6) & ~wet
    steep = ndimage.binary_fill_holes(ndimage.binary_closing(steep, iterations=2)) & ndimage.binary_dilation(steep, iterations=1)
    steep = ndimage.binary_opening(steep, iterations=1)
    areas = [
        ("deep_water", wet),
        ("shallows", wet & (from_land < style["shallow_cells"] + edge)),
        ("main", ~wet & (z_edge < (levels[-2] + levels[-1]) / 2)),
        ("high", ~wet & (z_edge >= (levels[-2] + levels[-1]) / 2)),
        ("shore", ~wet & (from_water < style["shore_cells"] + edge) & (z < levels[0] + 10)),
        ("slope", ~wet & (slope >= 0.25) & ~steep),
        ("mountain", imp & ~wet & (z > plan["mountain_above"]) & (slope < 0.6)),
        ("cliff", steep),
    ]
    layers = []
    for role, area in areas:
        if not area.any():
            continue
        # The first texture covers the whole area; each next one takes its share where its own noise is highest.
        choices = roles[role]
        total = sum(sh for _, sh in choices)
        for i, (texture, share) in enumerate(choices):
            if i == 0:
                mask = area
            else:
                n = noise(shape, rng, [tuple(sc) for sc in fuzz["patch_scales"]]) + rng.standard_normal(shape) * fuzz["scatter"]
                cut = np.quantile(n[area], 1 - share / total)
                mask = area & (n > cut)
            path = os.path.join(out, f"{len(layers):02d}_{role}_{texture}.png")
            Image.fromarray((mask[::-1] * 255).astype(np.uint8)).save(path)
            layers.append({"mask": path, "texture": texture, "role": role, "cells": int(mask.sum())})
    json.dump(layers, open(os.path.join(out, "textures.json"), "w"), indent=1)
    print(json.dumps([{"role": l["role"], "texture": l["texture"], "cells": l["cells"]} for l in layers]))


if __name__ == "__main__":
    main()
