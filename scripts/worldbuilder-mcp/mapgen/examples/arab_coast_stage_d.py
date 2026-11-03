# Stage D of ArabCoastGen, in the second WorldBuilder: the city along the highway, the island town, two villages, desert details.
# 1. City: the template marks it astride the north-south highway, and the bridge road leaves it eastward. Roads cut a town site into
#    pieces and the generator builds only the largest, and the pieces beside the highway are too narrow for it (2 to 21 buildings each).
#    So the highway's stretch through the city and the bridge road west of the bridge give way to one town over the whole area, its grid
#    aligned with the highway; contained, since its main street ran on beside the highway, and joined to the stubs here. Streets, ground
#    and props as in Golden Oasis (street_style): old asphalt, no sidewalks, rocky floor (the generic urban streets and sidewalks looked
#    western; Baghdad's narrow sidewalks left 15 buildings, Golden Oasis's style 38).
# 2. Island: a small block town joined to the bridge road (the organic layouts find no street on sites this small), restyled like the
#    city when the old roads leave its blocks too small.
# 3. Villages at the template's two small areas, dirt tracks from the highway past them and the derricks to the supply docks (as on
#    Cairo Commandos: DirtRoadTracks2), kits around the remaining Tech, a few more on open ground, rock groups.
# Usage: arab_coast_stage_d.py <map path> <work dir> [--save]   Without --save nothing is kept (towns are dry runs).
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "town")]
from wb_bridge import WorldBuilderBridge
import town_tool, outpost_tool

path, work = sys.argv[1], sys.argv[2]
save = "--save" in sys.argv
B, CELL, MAIN = 30, 10.0, 87.0
TRACK = "DirtRoadTracks2"
STREETS = "arabic_golden"
ROCKS = {"Boulders": 3, "RockClusterLarge01": 2, "RockClusterMedium01": 3, "RockClusterSmall01": 3, "Rocks1Brown": 2, "Rocks2Brown": 2, "Rocks3Brown": 2}
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=path, discard_changes=True)
rng = np.random.default_rng(1)
z = np.load(os.path.join(work, "stageA", "heights_world.npy"))
gy, gx = np.gradient(z, CELL)
flat = (np.abs(z - MAIN) < 4) & (np.hypot(gx, gy) < 0.15)
at = lambda a, x, y: a[int(round(y / CELL + B)), int(round(x / CELL + B))]
listed = lambda: wb.call("objects.list", include_waypoints_and_roads=True, limit=40000)["objects"]
roads = lambda: [o for o in listed() if o["kind"] == "road_point"]


def edge(y, x0, step, lim):
    x = x0
    while (x < lim if step > 0 else x > lim) and at(flat, x, y):
        x += step
    return x


def report(name, res):
    print(name, json.dumps({k: res.get(k) for k in ("objects", "road_runs", "placed", "objects_placed")}),
          [n[:80] for n in res.get("notes", []) if "lot" in n or "buildings" in n], flush=True)


# 1. City.
drop = []
for a, b in zip(roads()[0::2], roads()[1::2]):
    ys, xs = sorted([a["y"], b["y"]]), sorted([a["x"], b["x"]])
    if (a["template"] == "TwoLaneOld2" and ys[0] > 1100 and ys[1] < 2200) or (a["template"] == "TwoLaneOld3" and xs[1] < 1400):
        drop += [a["id"], b["id"]]
wb.call("objects.delete", ids=drop)
ys = list(range(1200, 2101, 60))
city = [[edge(y, 880, 10, 1500) - 40, y] for y in ys] + [[edge(y, 880, -10, 250) + 40, y] for y in ys[::-1]]
# Street nodes jittered (2.5 times the pack's jitter, seed 2): crooked streets and uneven blocks, as the user asked; 33 buildings
# against 38 on a straight grid (3.5 left blocks empty).
report("city", town_tool.generate(wb, polygon=city, style="urban", theme="arabic", street_style=STREETS, grid_scale=0.7, jitter=2.5, theta=0,
                                  contain=True, lots="site:1", clear_nature=True, seed=2, dry_run=not save))
if save:
    # The stubs: the highway's ends north and south of the city (not the city's own street ends there). Each joins the nearest city street.
    rp = roads()
    city_pts = [o for o in rp if town_tool._inside(o["x"], o["y"], city) and not o["template"].startswith("Sidewalk")]
    stubs = [o for o in rp if o["template"] == "TwoLaneOld2" and (1100 < o["y"] < 1250 or 2050 < o["y"] < 2200) and o not in city_pts]
    for s in stubs:
        c = min(city_pts, key=lambda o: math.hypot(o["x"] - s["x"], o["y"] - s["y"]))
        wb.call("roads.add", type=s["template"], points=[[s["x"], s["y"]], [c["x"], c["y"]]])
        print("stub", s["template"], (round(s["x"]), round(s["y"])), "->", (round(c["x"]), round(c["y"])), round(math.hypot(c["x"] - s["x"], c["y"] - s["y"])), flush=True)
    # The bridge's two lanes end at x 1367 with nothing left west of them: both join the city street nearest the bridge.
    c = min(city_pts, key=lambda o: math.hypot(o["x"] - 1367, o["y"] - 1620))
    for ly in (1598.9, 1639.6):
        wb.call("roads.add", type="TwoLaneOld3", points=[[c["x"], c["y"]], [(c["x"] + 1367) / 2, (c["y"] + ly) / 2], [1366.8, ly]])
    print("bridge lanes joined at", (round(c["x"]), round(c["y"])), flush=True)

# 2. Island.
island = []
for a in range(0, 360, 15):
    ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
    r = 0
    while r < 500 and at(flat, 1950 + r * ca, 1660 + r * sa):
        r += 5
    island.append([round(1950 + (r - 35) * ca), round(1660 + (r - 35) * sa)])
def buildings(res):
    note = next((n for n in res.get("notes", []) if n.startswith("buildings ")), "buildings 0")
    return int(note.split()[1])


# The old roads are wider: on the island their blocks left streets without a single building. Then the generic streets, as before.
island_kw = dict(polygon=island, style="urban", theme="arabic", grid_scale=0.4, jitter=1.5, major="TwoLaneOld3", connect=True, clear_nature=True, seed=1)
try:
    styled = buildings(town_tool.generate(wb, street_style=STREETS, dry_run=True, **island_kw)) >= 5
except Exception as e:
    styled = False
print("island in street style", STREETS, styled, flush=True)
report("island", town_tool.generate(wb, street_style=STREETS if styled else None, dry_run=not save, **island_kw))
if save and not styled:
    # The generic layout fits twice the buildings; restyled afterwards like the city: old asphalt, rocky floor, no sidewalks or traffic
    # lights, Arabic props. Only the island uses these types here.
    wb.call("map.replace", textures={"ConcreteType4": "RocksType5"}, roads={"TwoLane": "TwoLaneOld2"},
            objects={"PhoneBooth": "ClothesLine", "BusStop": "CanvasSunShader", "StreetLamp03": "BigWoodenBarrels", "MailBox": "Barrel",
                     "Dumpster": "CarScrap", "TrashDumpster": "MarketVender01", "CarLuxury01": "CarScrap", "CarLuxury03": "CarCompact"})
    wb.call("objects.delete", ids=[o["id"] for o in listed() if o["template"] in ("Sidewalk", "StopLight")])
if not save:
    sys.exit(0)

# 3. Tracks from the highway past the villages and derricks to the docks, then the villages, kits and rocks.
tracks = [("south west", [930, 330], [[680, 140]], [360, 300]), ("south east", [940, 330], [[1150, 260]], [1400, 160]),
          ("north west", [890, 2860], [[700, 3090]], [430, 2960]), ("north east", [905, 2920], [[1120, 2950]], [1450, 3020])]
for name, a, via, b in tracks:
    try:
        r = wb.call("roads.route", type=TRACK, to=b, via=via, corners="auto", flatten=True, flatten_width=30, flatten_feather=30,
                    slope_weight=0.5, max_grade=10, avoid_cliffs=True, avoid_water=True, object_clearance=20, straighten=1.25, snap=40,
                    end_caps=True, **{"from": a})
        print("track", name, round(r.get("length", 0)), flush=True)
    except Exception as e:
        print("track", name, "FAILED", str(e).splitlines()[0][:100], flush=True)
# Villages: Arabic kits at fixed spots of the template's small areas (filling the areas found no spot beside the tracks).
for name, x, y, seed in (("village south", 640, 110, 1), ("village south", 780, 160, 2), ("village south", 560, 80, 3),
                         ("village north", 760, 3110, 1), ("village north", 600, 3080, 2)):
    try:
        report(name, outpost_tool.place(wb, x=x, y=y, theme="arabic", clear_nature=True, seed=seed))
    except Exception as e:
        print(name, "FAILED", str(e)[:100], flush=True)
report("decorate tech", outpost_tool.populate(wb, count=0, decorate_tech=True, clear_nature=True, seed=3))
for theme, n in (("arabic", 3), ("military", 2), ("industrial", 1)):
    try:
        report("open ground " + theme, outpost_tool.populate(wb, count=n, theme=theme, spacing=450, clear_nature=True, seed=4))
    except Exception as e:
        print("open ground", theme, "FAILED", str(e)[:100], flush=True)

# Rocks: groups of 2 to 5 on open flat ground, away from objects, roads and the starts.
objs = listed()
taken = [(o["x"], o["y"], 260 if o["kind"] == "waypoint" else 45) for o in objs]
names, w = list(ROCKS), np.array(list(ROCKS.values()), float)
placed, groups = 0, 0
for _ in range(4000):
    if groups >= 22:
        break
    x, y = rng.uniform(100, 2690), rng.uniform(100, 3090)
    if not at(flat, x, y) or any(math.hypot(x - ox, y - oy) < r for ox, oy, r in taken):
        continue
    groups += 1
    for _ in range(int(rng.integers(2, 6))):
        px, py = x + rng.normal(0, 25), y + rng.normal(0, 25)
        if at(flat, px, py):
            wb.call("objects.place", template=names[rng.choice(len(names), p=w / w.sum())], x=round(px, 1), y=round(py, 1), angle_deg=round(float(rng.uniform(0, 360)), 1))
            placed += 1
    taken.append((x, y, 300))
print("rocks", placed, "in", groups, "groups", flush=True)
wb.call("terrain.reblend")
wb.call("map.save", path=path)
print("saved", path, wb.call("map.info")["counts"])
