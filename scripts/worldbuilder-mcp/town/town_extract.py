# Style pack from a reference town: street measurements, how buildings meet streets, ground, props, and frontage prefabs
# (a building with its bib, nearby props and ground, stored in a street-aligned frame so it can be placed along any street).
import json, math, os, sys, collections, re
import numpy as np
from PIL import Image
from town_paths import PACKS  # enters the work folder
NAME = "alpine_assault"
AREA = (980, 930, 1400, 1560)   # the town proper, without the petrol station and the rail yard
B = 70
ROADW = {"TwoLane": float(sys.argv[1]) if len(sys.argv) > 1 else 40, "Sidewalk": 12}
O = json.load(open("refs/alpine/aa_objects.json"))
segs = json.load(open("refs/alpine/aa_segs.json"))
geo = json.load(open("gamedata/geo.json"))
x0, y0, x1, y1 = AREA
inside = lambda x, y, m=0: x0 - m <= x <= x1 + m and y0 - m <= y <= y1 + m
BLD = re.compile(r"^(German|Stan)")
blds = [o for o in O if o["kind"] == "object" and BLD.match(o["template"]) and "Bib" not in o["template"] and inside(o["x"], o["y"])]
bibs = [o for o in O if o["kind"] == "object" and "Bib" in o["template"] and inside(o["x"], o["y"], 40)]
props = [o for o in O if o["kind"] == "object" and inside(o["x"], o["y"], 30) and not BLD.match(o["template"]) and not o["template"].startswith(("Amb", "Tree", "Rocks"))]
trees = [o for o in O if o["kind"] == "object" and inside(o["x"], o["y"], 30) and o["template"].startswith("Tree")]
streets = [s for s in segs if s[0] == "TwoLane" and (inside(s[1], s[2], 120) or inside(s[3], s[4], 120))]
walks = [s for s in segs if s[0] == "Sidewalk" and (inside(s[1], s[2], 60) or inside(s[3], s[4], 60))]
hw = ROADW["TwoLane"] / 2
def nearest(x, y, sg):
    """Distance to the nearest segment, with the closest point and unit tangent."""
    best = (1e9, 0, 0, 1, 0)
    for _, ax, ay, bx, by in sg:
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy) or 1
        t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (L * L)))
        px, py = ax + dx * t, ay + dy * t
        d = math.hypot(x - px, y - py)
        if d < best[0]:
            best = (d, px, py, dx / L, dy / L)
    return best
def front(o):
    r = math.radians(o["angle_deg"])
    return math.sin(r), -math.cos(r)
# --- streets ---
ends = collections.Counter()
for _, ax, ay, bx, by in streets:
    ends[(round(ax / 8), round(ay / 8))] += 1
    ends[(round(bx / 8), round(by / 8))] += 1
junc = [(k[0] * 8, k[1] * 8) for k, v in ends.items() if v >= 3 and inside(k[0] * 8, k[1] * 8, 60)]
lens = [math.dist(s[1:3], s[3:5]) for s in streets]
ang = collections.Counter()
for s in streets:
    L = math.dist(s[1:3], s[3:5])
    a = math.degrees(math.atan2(s[4] - s[2], s[3] - s[1])) % 90
    ang[round(a / 5) * 5] += L
jd = []
for i, p in enumerate(junc):
    ds = sorted(math.dist(p, q) for j, q in enumerate(junc) if i != j)
    if ds:
        jd.append(ds[0])
print("streets", len(streets), "segment length median", round(float(np.median(lens))), "junctions", len(junc), "nearest junction median", round(float(np.median(jd))) if jd else None)
print("street direction histogram (mod 90)", dict(ang.most_common(5)))
# --- buildings against the streets ---
rows = []
for o in blds:
    a, b = geo.get(o["template"], [20, 20])
    d, px, py, tx, ty = nearest(o["x"], o["y"], streets)
    nx, ny = (o["x"] - px) / (d or 1), (o["y"] - py) / (d or 1)
    fx, fy = front(o)
    facing = -(fx * nx + fy * ny)                # 1: the front looks straight at the street
    r = math.radians(o["angle_deg"])
    align = abs(math.cos(r) * tx + math.sin(r) * ty)   # 1: the long side runs along the street
    setback = d - hw - (abs(math.sin(r) * tx - math.cos(r) * ty) * b + abs(math.cos(r) * tx + math.sin(r) * ty) * 0) if False else d - hw - b
    rows.append((o["template"], round(d), round(setback), round(facing, 2), round(align, 2)))
print("building, road distance, setback, facing, along street")
for r in rows:
    print(" ", *r)
nf = [r[3] for r in rows]
print("median setback", np.median([r[2] for r in rows]), "share facing street", round(np.mean([f > 0.5 for f in nf]), 2), "share aligned", round(np.mean([r[4] > 0.8 for r in rows]), 2))
dd = sorted(min(math.dist((a["x"], a["y"]), (b["x"], b["y"])) for b in blds if b is not a) for a in blds)
print("nearest building median", np.median(dd))
mix = collections.Counter(o["template"] for o in blds)
# --- ground ---
tex = {}
for n in ("CobblestoneType1", "AsphaltType1", "RocksType3", "GrassMediumType10", "GrassType1"):
    tex[n] = (np.array(Image.open(f"refs/alpine/aa_{n}.png")) > 0)[::-1]
gh, gw = tex["CobblestoneType1"].shape
print("ground share by distance from a building edge (town cells)")
rows_g = collections.defaultdict(collections.Counter)
for j in range(gh):
    for i in range(gw):
        x, y = (i - B) * 10 + 5, (j - B) * 10 + 5
        if not inside(x, y):
            continue
        # distance to the nearest building rectangle
        db = 1e9
        for o in blds:
            a, b = geo.get(o["template"], [20, 20])
            r = math.radians(o["angle_deg"])
            u, v = (x - o["x"]) * math.cos(r) + (y - o["y"]) * math.sin(r), -(x - o["x"]) * math.sin(r) + (y - o["y"]) * math.cos(r)
            db = min(db, max(abs(u) - a, abs(v) - b))
        band = min(int(max(db, 0) // 20) * 20, 80)
        name = next((n for n in tex if tex[n][j, i]), "other")
        rows_g[band][name] += 1
for k in sorted(rows_g):
    tot = sum(rows_g[k].values())
    print(" ", k, {n: round(c / tot, 2) for n, c in rows_g[k].most_common(4)})
ground_share = {str(k): {n: round(c / sum(v.values()), 3) for n, c in v.items()} for k, v in rows_g.items()}
# --- props ---
print("prop, count, median road edge distance, median building distance, median junction distance, side of the street front")
pr = {}
by = collections.defaultdict(list)
for o in props:
    by[o["template"]].append(o)
for t, lst in sorted(by.items(), key=lambda kv: -len(kv[1])):
    de = [max(nearest(o["x"], o["y"], streets)[0] - hw, 0) for o in lst]
    db = [min(math.dist((o["x"], o["y"]), (b["x"], b["y"])) for b in blds) for o in lst]
    dj = [min((math.dist((o["x"], o["y"]), j) for j in junc), default=999) for o in lst]
    pr[t] = {"count": len(lst), "road_edge": round(float(np.median(de))), "building": round(float(np.median(db))), "junction": round(float(np.median(dj)))}
    print(" ", t, len(lst), pr[t]["road_edge"], pr[t]["building"], pr[t]["junction"])
street_len = sum(math.dist(s[1:3], s[3:5]) for s in streets if inside(s[1], s[2]) and inside(s[3], s[4]))
print("street length in the town", round(street_len), "lamps per 100 units of street", round(pr.get("StreetLamp", {"count": 0})["count"] / street_len * 100, 2))
# lamp spacing
lamps = by.get("StreetLamp", [])
if len(lamps) > 1:
    print("lamp nearest neighbour median", round(float(np.median([min(math.dist((a["x"], a["y"]), (b["x"], b["y"])) for b in lamps if b is not a) for a in lamps]))))
print("trees in town", len(trees), collections.Counter(t["template"] for t in trees), "median road edge", round(float(np.median([max(nearest(o["x"], o["y"], streets)[0] - hw, 0) for o in trees]))) if trees else None)
print("sidewalk segments", len(walks), "length", round(sum(math.dist(s[1:3], s[3:5]) for s in walks)), "offset from street centre", [round(nearest((s[1] + s[3]) / 2, (s[2] + s[4]) / 2, streets)[0]) for s in walks])
# --- frontage prefabs in a street-aligned frame: x along the street, y away from it ---
cobble = tex["CobblestoneType1"] | tex["RocksType3"]
prefabs = []
for o in blds:
    a, b = geo.get(o["template"], [20, 20])
    d, px, py, tx, ty = nearest(o["x"], o["y"], streets)
    ny, nx = o["y"] - py, o["x"] - px
    L = math.hypot(nx, ny) or 1
    nx, ny = nx / L, ny / L
    if tx * ny - ty * nx < 0:     # keep +y pointing away from the street
        tx, ty = -tx, -ty
    def local(x, y):
        return (round((x - px) * tx + (y - py) * ty, 1), round((x - px) * nx + (y - py) * ny, 1))
    tang = math.degrees(math.atan2(ty, tx))
    items = [{"t": o["template"], "p": local(o["x"], o["y"]), "a": round(o["angle_deg"] - tang, 1), "role": "building"}]
    R = math.hypot(a, b) + 45
    for q in bibs:
        if math.dist((q["x"], q["y"]), (o["x"], o["y"])) < R:
            items.append({"t": q["template"], "p": local(q["x"], q["y"]), "a": round(q["angle_deg"] - tang, 1), "role": "bib"})
    for q in props:
        if q["template"] != "StreetLamp" and math.dist((q["x"], q["y"]), (o["x"], o["y"])) < R:
            items.append({"t": q["template"], "p": local(q["x"], q["y"]), "a": round(q["angle_deg"] - tang, 1), "role": "prop"})
    cells = []
    for j in range(int((o["y"] - R) / 10 + B), int((o["y"] + R) / 10 + B) + 1):
        for i in range(int((o["x"] - R) / 10 + B), int((o["x"] + R) / 10 + B) + 1):
            if 0 <= j < gh and 0 <= i < gw and cobble[j, i]:
                cells.append(local((i - B) * 10 + 5, (j - B) * 10 + 5))
    us = [p["p"][0] for p in items]
    prefabs.append({"name": o["template"], "items": items, "ground": cells, "width": round(2 * max(a, b) + 10), "setback": round(d - hw - b, 1), "facing": round(-(front(o)[0] * nx + front(o)[1] * ny), 2)})
print("prefabs", len(prefabs), "with props", sum(any(i["role"] == "prop" for i in p["items"]) for p in prefabs), "with ground", sum(bool(p["ground"]) for p in prefabs))
pack = {"name": NAME, "source": "Alpine Assault", "area": AREA,
        "streets": {"type": "TwoLane", "width": ROADW["TwoLane"], "segment_median": round(float(np.median(lens))), "junction_spacing": round(float(np.median(jd))) if jd else None, "grid_angle_hist": dict(ang)},
        "buildings": {"mix": mix, "setback_median": float(np.median([r[2] for r in rows])), "nearest_median": float(np.median(dd))},
        "ground": ground_share, "props": pr, "lamp_rate_per_100": round(pr.get("StreetLamp", {"count": 0})["count"] / street_len * 100, 3), "prefabs": prefabs,
        "trees": dict(collections.Counter(t["template"] for t in trees)),
        "tree_rate_per_100_street": round(len(trees) / street_len * 100, 2),
        "sidewalks": {"type": "Sidewalk", "length_share_of_street": round(sum(math.dist(s[1:3], s[3:5]) for s in walks) / street_len, 3)}}
json.dump(pack, open(f"{PACKS}/{NAME}.json", "w"), default=int)
print("pack written", os.path.getsize(f"{PACKS}/{NAME}.json"))
