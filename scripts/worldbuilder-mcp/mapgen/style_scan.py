# Quick style scan of reference maps through the second WorldBuilder: main textures on flat / steep ground, trees, civilian buildings, water.
# Usage: style_scan.py <out json> <map names...>   Maps are <TOWN_REF_MAPS>\Maps\<name>\<name>.map; results are added to out json.
import collections, json, os, re, sys, threading
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "cliffs"))
from wb_bridge import WorldBuilderBridge
import cliff_scan

out = sys.argv[1]
res = json.load(open(out)) if os.path.exists(out) else {}
wb = WorldBuilderBridge(port=47810, timeout=280)
th = threading.Thread(target=cliff_scan.dismiss)
th.start()
try:
    for name in sys.argv[2:]:
        try:
            info = wb.call("map.open", path=os.path.join(os.environ["TOWN_REF_MAPS"], "Maps", name, name + ".map"), discard_changes=True)
            hm = info["heightmap"]; b = hm["border"]
            rows = []
            while len(rows) < hm["y_extent"]:
                h = min(hm["y_extent"] - len(rows), max(1, 60000 // hm["x_extent"]))
                rows += wb.call("terrain.get_heights", x0=0, y0=len(rows), w=hm["x_extent"], h=h)["heights"]
            z = np.array(rows, np.float32) * 0.625 * hm.get("height_map_scale", 1)
            gy, gx = np.gradient(z, 10.0); sl = np.hypot(gx, gy)
            water = [p.get("water_height") for p in wb.call("polygons.list").get("polygons", []) if p.get("kind") == "water"]
            flat, steep = collections.Counter(), collections.Counter()
            step = max(4, (hm["x_extent"] - 2 * b) // 45)
            for iy in range(b, hm["y_extent"] - b, step):
                for ix in range(b, hm["x_extent"] - b, step):
                    if 0.2 < sl[iy, ix] < 0.75:
                        continue
                    t = wb.call("terrain.sample", x=(ix - b) * 10.0, y=(iy - b) * 10.0)["texture"]
                    (steep if sl[iy, ix] >= 0.75 else flat)[t] += 1
            objs = wb.call("objects.list", limit=20000).get("objects", [])
            trees, civ = collections.Counter(), collections.Counter()
            for o in objs:
                t = o["template"]
                if t.startswith("Tree") or "Palm" in t or "Bamboo" in t or "Bush" in t:
                    trees[t] += 1
                elif o.get("owner", "").lower().startswith("teamplyrcivilian") or o.get("owner") == "team":
                    fam = re.match(r"[A-Z][a-z]+", t)
                    civ[fam.group(0) if fam else t] += 1
            n = sum(flat.values())
            res[name] = {"size": [info["world"]["playable_max_x"], info["world"]["playable_max_y"]], "water": water,
                         "time": info.get("time_of_day"), "weather": info.get("weather"),
                         "flat": [(k, round(v / n, 2)) for k, v in flat.most_common(6)], "steep": steep.most_common(4),
                         "trees": trees.most_common(8), "civ_families": civ.most_common(10), "steep_pct": round(float((sl[b:-b, b:-b] >= 0.75).mean() * 100), 1)}
            print(name, res[name]["flat"][:3], res[name]["trees"][:3], res[name]["civ_families"][:5], flush=True)
        except Exception as e:
            print(name, "FAILED", e, flush=True)
        json.dump(res, open(out, "w"), indent=1)
finally:
    cliff_scan.stop.set()
    th.join()
