# Rebuilds OasisLakeGen from its plan after a change to the terrain: stage A, relief, the map's heights and passability (through the
# second WorldBuilder), the starts onto the plan's radius, plants off the mainland, textures painted again, plants placed again.
# The island town (stage D) stays: relief keeps the island flat and its objects are not touched.
# Usage: oasis_lake_rebuild.py [map folder]   (default H:/Work/Generals/MapWork/OasisLakeGen)
import json, math, os, subprocess, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
G = sys.argv[1] if len(sys.argv) > 1 else "H:/Work/Generals/MapWork/OasisLakeGen"
PLAN, WORK, MAP = os.path.join(G, "mapgen_plan.json"), os.path.join(G, "mapgen_work"), os.path.join(G, "OasisLakeGen.map")


def wb_up():
    # The second WorldBuilder closed twice between the map step and the scan; start it again when its port is closed.
    import socket
    try:
        socket.create_connection(("127.0.0.1", int(os.environ.get("TOWN_WB_PORT", 47810))), timeout=2).close()
    except OSError:
        subprocess.run([sys.executable, os.path.join(os.path.dirname(HERE), "town", "start_wb.py")], check=True)


def py(script, *args):
    wb_up()
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True)

py("examples/oasis_lake_stage_a.py", PLAN, WORK)
mock = json.load(open(os.path.join(WORK, "mock", "mock.json")))["objects"]["objects"]
keep = [{"kind": "waypoint", "template": "*Waypoints/Waypoint", "x": o["x"], "y": o["y"]} for o in mock if o["kind"] == "waypoint"]
json.dump(keep, open(os.path.join(WORK, "relief_keep_objects.json"), "w"))
py("relief.py", PLAN, os.path.join(WORK, "stageA", "heights_world.npy"), os.path.join(WORK, "relief_keep_objects.json"), os.path.join(WORK, "relief"))

sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge
wb_up()
plan = json.load(open(PLAN))
L = plan["layout"]
C = L["playable_cells"] * 5.0
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=MAP, discard_changes=True)
wb.call("terrain.import_heightmap", path=os.path.join(WORK, "relief", "heights.png"), fit="exact", range=[0, 255])
mask = os.path.join(WORK, "relief", "impassable.png")
wb.call("terrain.import_mask", path=mask, target="passability", fit="exact", invert=True, impassable=False)
wb.call("terrain.import_mask", path=mask, target="passability", fit="exact")
objs = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
plants = [o["id"] for o in objs if o["kind"] == "object" and o["template"].startswith(("Tree", "Bush", "Vine"))
          and math.hypot(o["x"] - C, o["y"] - C) > L["island_radius"] + 150]
if plants:
    wb.call("objects.delete", ids=plants)
for o in objs:
    if o["kind"] == "waypoint" and "Start" in (o.get("waypoint_name") or ""):
        a = math.atan2(o["y"] - C, o["x"] - C)
        wb.call("objects.modify", ids=[o["id"]], x=round(C + L["start_radius"] * math.cos(a), 1), y=round(C + L["start_radius"] * math.sin(a), 1))
wb.call("map.save", path=MAP)
print("terrain imported, starts at", L["start_radius"], flush=True)
# The bridge serves one client: with this connection still open, the next script's connection took WorldBuilder down.
wb.close()

masks = os.path.join(WORK, "stageB", "coast_relief")
py("paint_map.py", "OasisLakeGen", "Golden Oasis", os.path.join(WORK, "stageB", "golden"), "--scan", MAP, "--keep", "NONE")
py("paint_coast.py", "OasisLakeGen", "Golden Oasis", "Golden Oasis", masks, "--sand", "SandType3wet", "--band", "SandType3wavy",
   "--sand-top", "8", "--band-top", "18")
py("import_masks.py", MAP, masks)
py("examples/oasis_lake_stage_c.py", "OasisLakeGen", MAP, masks, PLAN, os.path.join(WORK, "stageC"), "--place")
