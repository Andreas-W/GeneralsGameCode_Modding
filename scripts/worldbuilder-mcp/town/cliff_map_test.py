# Cliff texture mapping on a map from scratch through the second WorldBuilder (start_wb.py): the same terrain unmapped, mapped, and mapped with repair.
# Round plateaus give curved walls, a straight ridge a long flat one, stacked terraces a tall region. Pictures and reports in work/plans/cliff_map.
# Usage: cliff_map_test.py <texture> [view scale]
import json, math, os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

texture = sys.argv[1]
out = os.path.join(HERE, "work", "plans", "cliff_map")
os.makedirs(out, exist_ok=True)
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
wb.call("map.new", width=200, height=200, border=30, initial_height=20, discard_changes=True)
info = wb.call("map.info")
W, H = info["heightmap"]["x_extent"], info["heightmap"]["y_extent"]
border = info["heightmap"]["border"]
wall = 3  # cells


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def plateau(x, y, cx, cy, r, h):
    # A round plateau whose edge wanders, so the wall faces every direction.
    a = math.atan2(y - cy, x - cx)
    edge = r + 3 * math.sin(a * 3 + cx) + 1.5 * math.sin(a * 7 + cy)
    return h * smooth((edge - math.hypot(x - cx, y - cy)) / wall)


rows = []
for vy in range(H):
    row = []
    for vx in range(W):
        x, y = vx - border, vy - border
        z = 20.0
        # raw heights: 60 raw = 37.5 world, 240 raw = 150 world
        z += plateau(x, y, 35, 35, 18, 60)
        z += plateau(x, y, 85, 35, 18, 120)
        z += plateau(x, y, 140, 35, 18, 215)
        # terraces: one region that climbs far in steps
        for r, h in ((26, 70), (17, 70), (8, 70)):
            z += plateau(x, y, 40, 110, r, h)
        # a straight ridge
        z += 90 * smooth((y - 90) / wall) * smooth((180 - y) / wall) * smooth((x - 90) / wall) * smooth((180 - x) / wall)
        row.append(int(min(255, z)))
    rows.append(row)
wb.call("terrain.set_heights", x0=0, y0=0, heights=rows)
wb.call("terrain.flood_fill", texture=texture, x=1000, y=1000)
report = {}
views = {"low": (350, 170), "mid": (850, 170), "high": (1400, 170), "terraces": (400, 800), "ridge": (1350, 900)}
modes = ["none", "fold", "slide", "fit"]


def shots(step):
    for name, (x, y) in views.items():
        v = wb.call("view.screenshot", x=x, y=y, zoom=-380, pitch=1.0, angle_deg=90, max_width=800, format="jpg", quality=82)
        shutil.copy(v["path"], os.path.join(out, f"{texture}_{step}_{name}.jpg"))


shots("unmapped")
for m in modes:
    report[m] = wb.call("terrain.cliff_map", repair=m)
    shots(m)
    wb.call("edit.undo")
for k, v in report.items():
    print(k, json.dumps({a: b for a, b in v.items() if a in ("repair", "cells_mapped", "outside_u", "outside_v", "mutants", "worst_v_px")}))
json.dump(report, open(os.path.join(out, f"{texture}_report.json"), "w"), indent=1)
print("pictures in", out)
