# Stage D of OasisLakeGen: the centre island's small town with Tech and supplies. The Tech and supplies stand symmetrically, the same
# distance from every player: oil derricks toward the four cliff sides, supply docks toward the four diagonal beaches (where landings
# come ashore), a reinforcement pad in the middle. The town around them is a village of kits: on an island some 700 across with nine
# Tech objects, block towns (urban or Golden Oasis streets, grid 0.45 to 0.7, jittered or not) fitted 0 to 4 buildings (--try).
# Kits around each Tech object and dock, dirt roads (Golden Oasis's DirtRoad) from each dock to the plaza, Arabic kits on the open ground,
# then Arabic houses (the town packs' arabic theme, smaller ones) in two rings in the gaps between derricks and docks, facing the plaza.
# Usage: oasis_lake_stage_d.py <map path> <work dir> [--save] [--try]   --try dry-runs layout variants and prints them; --save builds.
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "town")]
from wb_bridge import WorldBuilderBridge
import town_tool, outpost_tool

path, work = sys.argv[1], sys.argv[2]
save, trying = "--save" in sys.argv, "--try" in sys.argv
B, CELL, C = 30, 10.0, (2500.0, 2500.0)
OWNER = "teamPlyrCivilian"
TECH = ([("TechOilDerrick", 220, a, a + 90) for a in (0, 90, 180, 270)] + [("SupplyDock", 250, a, a + 180) for a in (45, 135, 225, 315)]
        + [("TechReinforcementPad", 0, 0, 0)])
TOWN = dict(style="urban", theme="arabic", street_style="arabic_golden", grid_scale=0.6, jitter=1.5, contain=True, clear_nature=True, seed=1)

wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=path, discard_changes=True)
z = np.load(os.path.join(work, "stageA", "heights_world.npy"))
gy, gx = np.gradient(z, CELL)
flat = (np.abs(z - z[int(C[1] / CELL + B), int(C[0] / CELL + B)]) < 5) & (np.hypot(gx, gy) < 0.15)
at = lambda x, y: flat[int(round(y / CELL + B)), int(round(x / CELL + B))]

# A map made from nothing has no civilian player to own Tech buildings and supplies.
if not any(t.get("name") == OWNER for t in wb.call("sides.list").get("teams", [])):
    wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
for t, r, a, facing in TECH:
    x, y = C[0] + r * math.cos(math.radians(a)), C[1] + r * math.sin(math.radians(a))
    wb.call("objects.place", template=t, x=round(x, 1), y=round(y, 1), angle_deg=facing % 360, owner=OWNER)
print("placed", len(TECH), "Tech and supplies", flush=True)

island = []
for a in range(0, 360, 10):
    ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
    r = 0
    while r < 600 and at(C[0] + r * ca, C[1] + r * sa):
        r += 5
    island.append([round(C[0] + (r - 25) * ca), round(C[1] + (r - 25) * sa)])


def run(kw, dry):
    try:
        res = town_tool.generate(wb, polygon=island, dry_run=dry, **kw)
        note = next((n for n in res.get("notes", []) if n.startswith("buildings")), "buildings 0")
        return int(note.split()[1]), res
    except Exception as e:
        return -1, {"error": str(e).splitlines()[0][:100]}


if trying:
    for g in (0.5, 0.6, 0.7):
        for j in (0, 1.5):
            for s in (1, 2):
                n, res = run(dict(TOWN, grid_scale=g, jitter=j, seed=s), True)
                print("grid", g, "jitter", j, "seed", s, "buildings", n, res.get("road_runs") or res.get("error"), flush=True)
    for g in (0.45, 0.55):
        n, res = run(dict(TOWN, street_style=None, grid_scale=g), True)
        print("generic streets grid", g, "buildings", n, flush=True)
    sys.exit(0)
if not save:
    sys.exit(0)
for t, r, a, facing in TECH:
    if t == "SupplyDock":
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        try:
            wb.call("roads.route", type="DirtRoad", to=[round(C[0] + 70 * ca), round(C[1] + 70 * sa)], corners="auto", flatten=True,
                    flatten_width=30, flatten_feather=20, max_grade=10, avoid_cliffs=True, avoid_water=True, object_clearance=10,
                    straighten=1.25, snap=30, end_caps=True, **{"from": [round(C[0] + 190 * ca), round(C[1] + 190 * sa)]})
            print("road from the dock at", a, flush=True)
        except Exception as e:
            print("road", a, "FAILED", str(e).splitlines()[0][:90], flush=True)
dec = outpost_tool.populate(wb, count=0, decorate_tech=True, area=island, clear_nature=True, seed=1)
print("kits around Tech:", [(p["kit"], p.get("around")) for p in dec.get("placed", [])], flush=True)
for theme, count, seed in (("arabic", 6, 2), ("arabic", 3, 3)):
    try:
        res = outpost_tool.populate(wb, count=count, theme=theme, area=island, spacing=140, max_radius=150, clear_nature=True, seed=seed)
        print("kits", theme, [p["kit"] for p in res.get("placed", [])], flush=True)
    except Exception as e:
        print("kits", theme, "FAILED", str(e)[:90], flush=True)

# Houses: candidates in the gaps between each derrick (on an axis) and dock (on a diagonal), on two rings; each must stand on flat ground,
# clear of every object and road. Large houses (half size over 32) are left out: they did not fit between the docks.
geo = json.load(open(os.path.join(HERE, "town", "work", "gamedata", "geo.json")))
themes = json.load(open(os.path.join(HERE, "town", "packs", "building_themes.json")))["arabic"]
houses = {n: w for n, w in themes.items() if max(geo.get(n, [99, 99])) <= 32}
rng = np.random.default_rng(1)
listed = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
taken = [(o["x"], o["y"], max(geo.get(o["template"], [6, 6]))) for o in listed if o["kind"] == "object"]
rp = [o for o in listed if o["kind"] == "road_point"]
segs = [((a_["x"], a_["y"]), (b_["x"], b_["y"])) for a_, b_ in zip(rp[0::2], rp[1::2])]
def seg_dist(x, y, a_, b_):
    ux, uy = b_[0] - a_[0], b_[1] - a_[1]
    t = max(0.0, min(1.0, ((x - a_[0]) * ux + (y - a_[1]) * uy) / max(ux * ux + uy * uy, 1e-6)))
    return math.hypot(x - a_[0] - t * ux, y - a_[1] - t * uy)
placed = 0
names, w = list(houses), np.array(list(houses.values()), float)
for r, offsets in ((165, (0,)), (250, (-9, 9)), (330, (-11, 0, 11))):
    for k in range(8):
        for off in offsets:
            a = 22.5 + 45 * k + off
            x, y = C[0] + r * math.cos(math.radians(a)), C[1] + r * math.sin(math.radians(a))
            n = names[rng.choice(len(names), p=w / w.sum())]
            half = max(geo[n])
            if not all(at(x + dx, y + dy) for dx in (-half, half) for dy in (-half, half)):
                continue
            if any(math.hypot(x - ox, y - oy) < half + oh + 12 for ox, oy, oh in taken):
                continue
            if any(seg_dist(x, y, a_, b_) < half + 22 for a_, b_ in segs):
                continue
            wb.call("objects.place", template=n, x=round(x, 1), y=round(y, 1), angle_deg=round((a + 180) % 360, 1), owner=OWNER)
            taken.append((x, y, half))
            placed += 1
print("houses", placed, flush=True)
wb.call("terrain.reblend")
wb.call("map.save", path=path)
print("saved", path, wb.call("map.info")["counts"])
