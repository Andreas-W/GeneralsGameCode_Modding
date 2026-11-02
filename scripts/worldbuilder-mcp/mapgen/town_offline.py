# Runs town_tool.generate (what the town_generate tool does) in the second WorldBuilder on a map file, so that generator options can be
# tried before the MCP server knows them, and many dry runs do not touch the map open in the user's WorldBuilder.
# Usage: town_offline.py <map> <json list of runs> [apply_save_path]   GAME_DIR must be set.
# Each run: {"name":..., "polygon":..., plus generate() keyword arguments}. Without apply_save_path every run is a dry run.
import json, os, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "town"))
from wb_bridge import WorldBuilderBridge
import town_tool
wb = WorldBuilderBridge(port=47810, timeout=600)
wb.call("map.open", path=sys.argv[1], discard_changes=True)
runs = json.loads(open(sys.argv[2]).read())
# Optional: delete road segments with both ends inside a rect, given as env DROP_ROADS="x0,y0,x1,y1".
if os.environ.get("DROP_ROADS"):
    x0, y0, x1, y1 = map(float, os.environ["DROP_ROADS"].split(","))
    rp = [o for o in wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"] if o["kind"] == "road_point"]
    ids = []
    inside = lambda o: x0 <= o["x"] <= x1 and y0 <= o["y"] <= y1
    for a, b in zip(rp[0::2], rp[1::2]):
        if inside(a) and inside(b):
            ids += [a["id"], b["id"]]
    if ids:
        wb.call("objects.delete", ids=ids)
    print("dropped road points", len(ids), flush=True)
apply = sys.argv[3] if len(sys.argv) > 3 else None
for r in runs:
    r = dict(r)
    name = r.pop("name")
    try:
        res = town_tool.generate(wb, dry_run=not apply, **r)
        keep = {k: res.get(k) for k in ("objects", "road_runs", "notes", "bounds", "preview", "connections_failed", "dry_run")}
        print(name, json.dumps(keep), flush=True)
    except Exception as e:
        print(name, "FAILED", str(e).splitlines()[0], flush=True)
if apply:
    wb.call("map.save", path=apply)
    print("saved", apply)
