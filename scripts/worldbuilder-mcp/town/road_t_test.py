# Same-type T junctions on a blank map, to see which way of adding them renders cleanly. Pictures in work/plans/road_t.
import os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

road = sys.argv[1] if len(sys.argv) > 1 else "FourLane"
out = os.path.join(HERE, "work", "plans", "road_t")
os.makedirs(out, exist_ok=True)
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=120)
wb.call("map.new", width=240, height=240, border=30, initial_height=90, discard_changes=True)
# (main corners, main caps, side corners, side caps, side length)
VARIANTS = [("tight", False, "tight", False, 200), ("tight", True, "tight", True, 200), ("angled", False, "angled", False, 200), ("curved", False, "curved", False, 200),
            ("tight", False, "tight", True, 200), ("angled", False, "tight", False, 200), ("tight", False, "tight", False, 60)]
for k, (mc, mcap, sc, scap, L) in enumerate(VARIANTS):
    y = 300 + k * 290
    wb.call("roads.add", type=road, points=[[300, y], [800, y], [1500, y]], corners=mc, end_caps=mcap)
    wb.call("roads.add", type=road, points=[[800, y], [800, y + L]], corners=sc, end_caps=scap)
    v = wb.call("view.screenshot", x=880, y=y + 40, zoom=-420, pitch=2.5, angle_deg=0, max_width=800, format="jpg", quality=80)
    shutil.copy(v["path"], os.path.join(out, f"{road}_{k}.jpg"))
    print(k, mc, mcap, sc, scap, L)
