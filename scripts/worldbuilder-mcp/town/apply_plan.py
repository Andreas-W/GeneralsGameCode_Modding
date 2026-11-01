# Builds a town plan on a new scratch map through a second WorldBuilder (start_wb.py), saves it to the user maps and takes pictures.
# Usage: apply_plan.py <plan folder, e.g. plans/urban_plan> <map name> <view x,y,zoom> ...
import json, os, shutil, sys, time
from town_paths import WB_PORT, USER_MAPS  # enters the work folder
from wb_bridge import WorldBuilderBridge
folder, name = os.path.abspath(sys.argv[1]), sys.argv[2]
views = [tuple(float(v) for v in a.split(",")) for a in sys.argv[3:]]
plan = json.load(open(os.path.join(folder, "plan.json")))
wb = WorldBuilderBridge(port=WB_PORT)
wb.call("map.new", width=240, height=240, border=30, initial_height=90, discard_changes=True)
wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
def mask(tex, fn):
    return wb.call("terrain.import_mask", path=os.path.join(folder, fn), target="texture", texture=tex, fit="exact")
mask(plan["surround"], "base.png")
for t in plan["masks"]:
    print(t, mask(t, t + ".png").get("cells_set", 0))
errors = 0
for o in plan["objects"]:
    try:
        wb.call("objects.place", **o)
    except Exception as exc:
        errors += 1
        print("object failed", o["template"], exc)
print("objects", len(plan["objects"]), "errors", errors)
for r in plan["roads"]:
    wb.call("roads.add", **r)
print("road runs", len(plan["roads"]), {t: sum(1 for r in plan["roads"] if r["type"] == t) for t in {r["type"] for r in plan["roads"]}})
print("reblend", {k: v for k, v in wb.call("terrain.reblend").items() if k in ("cells_blended", "three_way_tiles", "hard_edges", "blend_table_used")})
d = os.path.join(USER_MAPS, name)
os.makedirs(d, exist_ok=True)
print("saved", wb.call("map.save", path=os.path.join(d, name + ".map"), overwrite=True)["path"])
for k, (x, y, z) in enumerate(views):
    r = wb.call("view.screenshot", x=x, y=y, zoom=z, pitch=2.5, angle_deg=0, max_width=1100, format="jpg", quality=85)
    shutil.copy(r["path"], os.path.join(folder, f"view{k}.jpg"))
