# NorthernIslandGen (and WinterIsleGen), stage D roads: GravelRoadSnow from each base to the nearest open street end of the centre town, DirtRoadSnow from
# each base to its expansion; trees and bushes on them deleted, cliff mapping redone (fit). Second WorldBuilder.
# Usage: northern_island_roads.py <plan.json>
import json, math, os, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

plan = json.load(open(sys.argv[1]))
starts = {k["name"]: tuple(k["at"]) for k in plan["keep"] if k["kind"] == "waypoint"}
# Each base's expansion is the oil derrick nearest its start (NorthernIslandGen: (640, 820) and (3125, 3035); WinterIsleGen too).
work = os.path.join(os.path.dirname(plan["map"]), "mapgen_work")
derricks = [(o["x"], o["y"]) for o in json.load(open(os.path.join(work, "objects.json"))) if o["template"] == "TechOilDerrick"]
EXPANSIONS = {n: min(derricks, key=lambda d: math.dist(d, s)) for n, s in starts.items()}
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=plan["map"], discard_changes=True)
objs = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
pts = [(round(o["x"]), round(o["y"])) for o in objs if o["kind"] == "road_point" and o["template"] == "GravelRoadSnow"]
# Street ends: positions used by one road point only. With plan "roads": {"join": "any"} every street point counts, so each road can
# meet the town at its own junction: a town with two open ends and five players sent three roads to one end (WinterIsleGen).
JOIN_ANY = plan.get("roads", {}).get("join") == "any"
ends = list(set(pts)) if JOIN_ANY else [p for p in set(pts) if pts.count(p) == 1]
roads = []
used = []
for name, s in sorted(starts.items()):
    # The nearest street point that no other base joined (within 200) and that a route reaches: a point among the houses may be boxed in.
    r = None
    for end in sorted(ends, key=lambda p: math.dist(p, s)):
        if any(math.dist(end, u) < 200 for u in used):
            continue
        toward = (end[0] - s[0], end[1] - s[1]); L = math.hypot(*toward)
        begin = (s[0] + toward[0] / L * 180, s[1] + toward[1] / L * 180)
        try:
            r = wb.call("roads.route", type="GravelRoadSnow", **{"from": list(begin)}, to=list(end), corners="curved", end_caps=True,
                        object_clearance=30)
            break
        except Exception as e:
            print(name, "no route to", end, str(e)[:60])
    used.append(end)
    roads.append(r["points"])
    print(name, "to town end", end, "length", round(r["length"]), "points", len(r["points"]))
    ex = EXPANSIONS[name]
    r = wb.call("roads.route", type="DirtRoadSnow", **{"from": list(begin)}, to=[ex[0] + (s[0] - ex[0]) * 0.12, ex[1] + (s[1] - ex[1]) * 0.12],
                corners="curved", end_caps=True, object_clearance=30)
    roads.append(r["points"])
    print(name, "to expansion, length", round(r["length"]))


def seg_dist(p, a, b):
    L = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
    t = 0 if L == 0 else max(0, min(1, ((p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])) / L))
    return math.hypot(p[0] - a[0] - t * (b[0] - a[0]), p[1] - a[1] - t * (b[1] - a[1]))


plants = [o for o in wb.call("objects.list", limit=20000)["objects"] if o["template"].startswith(("Tree", "Bush"))]
hit = [o["id"] for o in plants if min(seg_dist((o["x"], o["y"]), a, b) for r in roads for a, b in zip(r, r[1:])) < 38]
if hit:
    wb.call("objects.delete", ids=hit)
print("plants on roads deleted", len(hit))
c = wb.call("terrain.cliff_map", repair="fit")
print("cliffs outside", c["outside_v"])
wb.call("map.save", path=plan["map"], overwrite=True)
json.dump(roads, open(os.path.join(work, "roads.json"), "w"))
print("saved")
