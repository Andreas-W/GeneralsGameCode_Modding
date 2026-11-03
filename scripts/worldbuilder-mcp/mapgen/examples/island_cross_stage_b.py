# Stage B of IslandCrossGen: tropical jungle textures from two references. No single reference has both: the jungle maps (Eastern
# Everglades) have no sea and paint grass under the water, the island maps (Seaside Mutiny) paint bright sand and dirt far inland.
# So: land from the jungle model, the seabed from the island model, and beaches by rule with the jungle map's own sand, which is darker
# than the island map's and joins the dark greens through a dirt band instead of a hard bright edge.
# The map must be scanned first (paint_map.py ... --scan). Writes masks like paint_map.py: one per texture, largest first, masks.json.
# Usage: island_cross_stage_b.py <map name> <land model> <sea model> <out dir> [--seed N]
import json, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
import painter

# Beaches: dry, not steep, near the water and low. The sand ends at SAND_TOP above the water, the dirt band at DIRT_TOP (world units);
# the beaches climb from 64 to 100 over about 220 (most of the low ground lies in the shallows), so the sand is some 6 to 12 cells wide.
NEAR = 30
STEEP = 0.5
SAND_TOP = 18.0
DIRT_TOP = 26.0
VARY = 4.0
SAND, SAND_ACCENT, DIRT = "SandType2wavy", "SandType2", "DirtType1"


def main():
    args = sys.argv[1:]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    name, land_model, sea_model, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:4]
    d = os.path.join(HERE, "work", "maps", name)
    info = json.load(open(os.path.join(d, "info.json")))
    fz = np.load(os.path.join(d, "features.npz"))
    f = {k: fz[k] for k in fz.files}
    border = info["border"]
    water = max(w for w in info["water"] if w is not None)
    land_tex, land_names = painter.paint(f, land_model, seed, border)
    sea_tex, sea_names = painter.paint(f, sea_model, seed, border)
    names = list(dict.fromkeys(land_names + sea_names + [SAND, SAND_ACCENT, DIRT]))
    tex = np.array([names.index(n) for n in land_names])[land_tex]
    wet = f["wet"].astype(bool)
    # Under water only the sea model's seabed and cliff textures: its bright beach sand stood as specks in the sea.
    seabed = max(set(sea_tex[wet.astype(bool)].tolist()), key=sea_tex[wet.astype(bool)].tolist().count)
    keep = np.array([i == seabed or sea_names[i].startswith(("Cliff", "SandMediumType9")) for i in range(len(sea_names))])
    sea_tex = np.where(keep[sea_tex], sea_tex, seabed)
    tex[wet] = np.array([names.index(n) for n in sea_names])[sea_tex][wet]

    rng = np.random.default_rng(seed)
    shore = ~wet & (f["slope"] < STEEP) & (f["to_water"] <= NEAR)
    above = f["z"] - water
    vary = VARY * painter.field(tex.shape, 8, rng)
    dirt = shore & (above < DIRT_TOP + vary)
    sand = shore & (above < SAND_TOP + vary)
    tex[dirt] = names.index(DIRT)
    tex[sand] = names.index(SAND)
    # Darker sand in patches of the reference's accent size, inside the beach only.
    accent = sand & (painter.field(tex.shape, 5, rng) > 0.8)
    tex[accent] = names.index(SAND_ACCENT)
    tex = painter.despeckle(painter.soften_corners(painter.despeckle(tex)))

    used = sorted(set(np.unique(tex)))
    txinfo = json.load(open(os.path.join(HERE, "work", "textures.json")))
    bd = border
    st = painter.stats(tex[bd:-bd, bd:-bd], names, txinfo)
    os.makedirs(out, exist_ok=True)
    order = []
    for t in used:
        m = tex == t
        Image.fromarray((m[::-1] * 255).astype(np.uint8)).save(os.path.join(out, names[t] + ".png"))
        order.append({"texture": names[t], "cells": int(m.sum()), "path": os.path.join(out, names[t] + ".png")})
    order.sort(key=lambda o: -o["cells"])
    json.dump({"land_model": land_model, "sea_model": sea_model, "seed": seed, "masks": order, "three_way": st["three_way"],
               "brightness_step_p90": st["brightness_step_p90"]}, open(os.path.join(out, "masks.json"), "w"), indent=1)
    Image.fromarray(painter.colours(tex, names, txinfo)).save(os.path.join(out, "preview.png"))
    print(f"{len(order)} textures; three-way {st['three_way']}; brightness step p90 {st['brightness_step_p90']:.2f}")
    for o in order:
        print(f"  {o['texture']:26s} {o['cells']}")


if __name__ == "__main__":
    main()
