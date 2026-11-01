# A map from scratch through the second WorldBuilder (start_wb.py): terrain, main roads, then towns that connect to them. Saved to the user maps.
# Usage: town_scratch_test.py <map name> <layout preset> <road type> <style> [preset] [seed]
import json, os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from wb_bridge import WorldBuilderBridge
import town_tool

name, layout, road, style = sys.argv[1:5]
preset = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] != "-" else None
seed = int(sys.argv[6]) if len(sys.argv) > 6 else 3
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
wb.call("map.new", width=240, height=240, border=30, initial_height=90, discard_changes=True)
presets = json.load(open(os.path.join(os.path.dirname(HERE), "presets", "layouts.json")))
wb.call("terrain.generate", layers=presets[layout]["layers"] if isinstance(presets[layout], dict) and "layers" in presets[layout] else presets[layout], seed=seed)
wb.call("terrain.limit_slope", max_step=10)
# a main road across the map, passing beside where the town goes
r = wb.call("roads.route", **{"from": [60, 300]}, to=[2330, 2100], via=[[1500, 450]], type=road, corners="auto", flatten=True, flatten_width=70, end_caps=True)
print("main road", {k: r.get(k) for k in ("length", "points", "route_points", "flattened")})
area = [[500, 900], [1500, 700], [1800, 1500], [1000, 2000], [400, 1600]]
t = town_tool.generate(wb, area, style=style, preset=preset, seed=seed)
print(json.dumps({k: v for k, v in t.items() if k in ("objects", "placed", "road_runs", "connections", "flatten", "errors", "blend", "blocked_terrain_cells")})[:1100])
for n in t["notes"]:
    print(" ", n[:150])
d = os.path.join(os.path.expanduser("~"), "Documents", "Command and Conquer Generals Zero Hour Data", "Maps", name)
os.makedirs(d, exist_ok=True)
wb.call("map.save", path=os.path.join(d, name + ".map"), overwrite=True)
views = [(1200, 1200, -3400)] + [(c["to"][0], c["to"][1], -520) for c in t.get("connections", [])[:2]]
for k, (x, y, z) in enumerate(views):
    v = wb.call("view.screenshot", x=x, y=y, zoom=z, pitch=2.5, angle_deg=0, max_width=1000, format="jpg", quality=85)
    shutil.copy(v["path"], os.path.join(t["plan"], f"view{k}.jpg"))
print("views in", t["plan"])
