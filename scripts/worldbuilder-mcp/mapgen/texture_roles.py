# Measures which textures a reference map uses for each role of stage B (textures_from_plan.py), and the share each covers there:
# deep_water, shallows, shore, main, high, slope, mountain, cliff. Works through the second WorldBuilder (town/start_wb.py).
# Usage: texture_roles.py <reference .map> [step cells]   Prints the roles as JSON, ready for the plan's styles.terrain_textures.roles.
import collections, json, os, sys, threading
import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "cliffs"))
from wb_bridge import WorldBuilderBridge
import cliff_scan

# Textures below this share of a role are noise from blends and single cells.
MIN_SHARE = 0.03


def main():
    path = sys.argv[1]
    step = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
    th = threading.Thread(target=cliff_scan.dismiss)
    th.start()
    try:
        info = wb.call("map.open", path=path, discard_changes=True)
        hm = info["heightmap"]
        b = hm["border"]
        z = cliff_scan.heights(wb, hm["x_extent"], hm["y_extent"]) * 0.625 * hm.get("height_map_scale", 1)
        water = max([p.get("water_height", -1) for p in wb.call("polygons.list").get("polygons", []) if p.get("kind") == "water"] or [-1])
        gy, gx = np.gradient(z, 10.0)
        slope = np.hypot(gx, gy)
        wet = z < water
        from_land = ndimage.distance_transform_edt(wet)
        from_water = ndimage.distance_transform_edt(~wet)
        imp = np.zeros(z.shape, bool)
        mask_path = os.path.join(os.environ.get("TEMP", "."), "texture_roles_pass.png")
        wb.call("terrain.export_mask", kind="passability", path=mask_path, include_border=True)
        from PIL import Image
        m = np.array(Image.open(mask_path).convert("L"))[::-1] > 127
        imp[:m.shape[0], :m.shape[1]] = m
        land = ~wet & (slope < 0.25)
        high_cut = np.percentile(z[b:-b, b:-b][land[b:-b, b:-b]], 70) if land.any() else 0
        roles = collections.defaultdict(collections.Counter)
        for iy in range(b, z.shape[0] - b, step):
            for ix in range(b, z.shape[1] - b, step):
                t = wb.call("terrain.sample", x=(ix - b) * 10.0, y=(iy - b) * 10.0)["texture"]
                if wet[iy, ix]:
                    role = "shallows" if from_land[iy, ix] < 6 else "deep_water"
                elif slope[iy, ix] >= 0.6:
                    role = "cliff"
                elif imp[iy, ix]:
                    role = "mountain"
                elif slope[iy, ix] >= 0.25:
                    role = "slope"
                elif water > 0 and from_water[iy, ix] < 6:
                    role = "shore"
                elif z[iy, ix] >= high_cut:
                    role = "high"
                else:
                    role = "main"
                roles[role][t] += 1
        out = {}
        for role, cnt in roles.items():
            n = sum(cnt.values())
            out[role] = [[t, round(c / n, 2)] for t, c in cnt.most_common() if c / n >= MIN_SHARE]
            out[role + "_cells"] = n
        print(json.dumps({"map": path, "water": water, "high_from": round(float(high_cut), 1), "time": info.get("time_of_day"), "roles": out}, indent=1))
    finally:
        cliff_scan.stop.set()
        th.join()


if __name__ == "__main__":
    main()
