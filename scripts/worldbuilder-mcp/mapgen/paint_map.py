# Texturing step 5: paint a map with a reference's model (painter.py) and write one mask per texture for terrain_import_mask
# (fit=exact, include_border=true), largest first; then terrain_reblend and terrain_cliff_map.
# The map is scanned first like a reference, into work/maps/<name>/ (--scan <path.map>, through the second WorldBuilder,
# town/start_wb.py; again after the map changed). Cells that now carry a kept texture (town ground and road surfaces by default)
# stay as they are.
# Usage: paint_map.py <map name> <model> <out dir> [--scan <path.map>] [--seed N] [--keep Tex1,Tex2]
import json, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import painter

KEEP = ("ConcreteType", "AsphaltType")   # prefixes


def scan(path):
    import shutil, threading
    import ref_scan, cell_features
    sys.path.insert(0, os.path.dirname(HERE))
    from wb_bridge import WorldBuilderBridge
    ref_scan.WORK = os.path.join(HERE, "work", "maps")
    name = os.path.splitext(os.path.basename(path))[0]
    d = os.path.join(ref_scan.WORK, name)
    shutil.rmtree(d, ignore_errors=True)
    th = threading.Thread(target=ref_scan.dismiss)
    th.start()
    try:
        wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=300)
        print(ref_scan.scan(wb, os.path.dirname(os.path.dirname(path)), name))
    finally:
        ref_scan.stop.set()
        th.join()
    info, z, wet, imp, objs = cell_features.load_ref(d)
    water = max([w for w in info.get("water") or [] if w is not None] or [0])
    np.savez_compressed(os.path.join(d, "features.npz"), **cell_features.features(z, wet, imp, objs, info["border"], water))


def main():
    args = [a for a in sys.argv[1:]]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    keep_names = args[args.index("--keep") + 1].split(",") if "--keep" in args else None
    name, model, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:3]
    if "--scan" in args:
        scan(args[args.index("--scan") + 1])
    d = os.path.join(HERE, "work", "maps", name)
    info = json.load(open(os.path.join(d, "info.json")))
    fz = np.load(os.path.join(d, "features.npz"))
    tex, names = painter.paint({k: fz[k] for k in fz.files}, model, seed, info["border"])
    cur = np.load(os.path.join(d, "textures.npy"))
    kept = [n for n in info["textures"] if (n in keep_names if keep_names is not None else n.startswith(KEEP))]
    keep = np.isin(cur, [info["textures"].index(n) for n in kept])
    os.makedirs(out, exist_ok=True)
    order = []
    for t, n in enumerate(names):
        m = (tex == t) & ~keep
        if m.any():
            # Row 0 of the image is north.
            Image.fromarray((m[::-1] * 255).astype(np.uint8)).save(os.path.join(out, n + ".png"))
            order.append({"texture": n, "cells": int(m.sum()), "path": os.path.join(out, n + ".png")})
    order.sort(key=lambda o: -o["cells"])
    json.dump({"model": model, "seed": seed, "kept": kept, "kept_cells": int(keep.sum()), "masks": order},
              open(os.path.join(out, "masks.json"), "w"), indent=1)
    print(f"kept {kept} ({int(keep.sum())} cells); {len(order)} masks in {out}")


if __name__ == "__main__":
    main()
