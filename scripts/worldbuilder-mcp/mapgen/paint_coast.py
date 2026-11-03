# Texturing a coast from two references, when no single one has both the land and the sea the description asks for: inland references
# (jungle, Arabic city) have no sea and paint land textures under the water, coastal ones paint their beach sand far inland.
# Land from the land model, the seabed from the sea model (only its seabed and cliff textures: its beach sand stood as specks in the
# sea), beaches by rule: dry, gentle ground near the water, sand up to --sand-top above the water, then an optional band (--band) up
# to --band-top, so that bright sand need not meet dark ground with a hard edge. Most of a beach's low ground lies in the shallows, so
# the tops are some way above the water.
# The map must be scanned first (paint_map.py ... --scan). Writes masks like paint_map.py: one per texture, largest first, masks.json.
# Usage: paint_coast.py <map name> <land model> <sea model> <out dir> --sand Main[,Accent] [--band Tex] [--sand-top 18] [--band-top 26] [--seed N]
#   [--fold d4]  (folds the painting for an exactly symmetric map, see symmetry.py)
import json, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import painter

NEAR = 30         # cells from the water
STEEP = 0.5
VARY = 4.0        # world units of noise on the band tops
SEA_SHARE = 0.1   # sea model textures kept under water besides its seabed and cliffs: at least this share of the wet cells
SHORE_CLIFF = 4   # cells from land within which cliff textures stay under water


def main():
    args = sys.argv[1:]
    opt = lambda k, d=None: args[args.index(k) + 1] if k in args else d
    seed = int(opt("--seed", 1))
    name, land_model, sea_model, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:4]
    sand = opt("--sand").split(",")
    band = opt("--band")
    sand_top, band_top = float(opt("--sand-top", 18)), float(opt("--band-top", 26))
    d = os.path.join(HERE, "work", "maps", name)
    info = json.load(open(os.path.join(d, "info.json")))
    fz = np.load(os.path.join(d, "features.npz"))
    f = {k: fz[k] for k in fz.files}
    border = info["border"]
    water = max(w for w in info["water"] if w is not None)
    land_tex, land_names = painter.paint(f, land_model, seed, border)
    sea_tex, sea_names = painter.paint(f, sea_model, seed, border)
    names = list(dict.fromkeys(land_names + sea_names + sand + ([band] if band else [])))
    tex = np.array([names.index(n) for n in land_names])[land_tex]
    wet = f["wet"].astype(bool)
    counts = np.bincount(sea_tex[wet], minlength=len(sea_names))
    seabed = int(counts.argmax())
    keep = np.array([i == seabed or sea_names[i].startswith("Cliff") or counts[i] >= SEA_SHARE * wet.sum() for i in range(len(sea_names))])
    sea_tex = np.where(keep[sea_tex], sea_tex, seabed)
    # Cliff textures only on the faces of the shore: a steep step in the seabed far out showed as grey streaks through the water.
    cliff = np.array([n.startswith("Cliff") for n in sea_names])
    sea_tex = np.where(cliff[sea_tex] & (f["to_land"] > SHORE_CLIFF), seabed, sea_tex)
    tex[wet] = np.array([names.index(n) for n in sea_names])[sea_tex][wet]

    rng = np.random.default_rng(seed)
    shore = ~wet & (f["slope"] < STEEP) & (f["to_water"] <= NEAR)
    above = f["z"] - water
    vary = VARY * painter.field(tex.shape, 8, rng)
    beach = shore & (above < sand_top + vary)
    if band:
        tex[shore & (above < band_top + vary)] = names.index(band)
    tex[beach] = names.index(sand[0])
    if len(sand) > 1:
        # The accent in patches of a typical accent's size, inside the beach only.
        tex[beach & (painter.field(tex.shape, 5, rng) > 0.8)] = names.index(sand[1])
    tex = painter.despeckle(painter.soften_corners(painter.despeckle(tex)))
    if opt("--fold"):
        # A symmetric map gets symmetric ground: one part painted, folded onto the rest (symmetry.py).
        import symmetry
        tex = symmetry.fold_grid(tex, opt("--fold"))
    txinfo = json.load(open(os.path.join(HERE, "work", "textures.json")))
    st = painter.stats(tex[border:-border, border:-border], names, txinfo)
    os.makedirs(out, exist_ok=True)
    order = []
    for t in sorted(set(np.unique(tex))):
        m = tex == t
        Image.fromarray((m[::-1] * 255).astype(np.uint8)).save(os.path.join(out, names[t] + ".png"))
        order.append({"texture": names[t], "cells": int(m.sum()), "path": os.path.join(out, names[t] + ".png")})
    order.sort(key=lambda o: -o["cells"])
    json.dump({"land_model": land_model, "sea_model": sea_model, "sand": sand, "band": band, "seed": seed, "masks": order,
               "three_way": st["three_way"], "brightness_step_p90": st["brightness_step_p90"]}, open(os.path.join(out, "masks.json"), "w"), indent=1)
    Image.fromarray(painter.colours(tex, names, txinfo)).save(os.path.join(out, "preview.png"))
    print(f"{len(order)} textures; three-way {st['three_way']}; brightness step p90 {st['brightness_step_p90']:.2f}")
    for o in order:
        print(f"  {o['texture']:26s} {o['cells']}")


if __name__ == "__main__":
    main()
