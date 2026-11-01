# Civilian structures of the mod by family, and how reference maps place Tech buildings, supply docks and piles.
import glob, json, math, os, re, sys, collections
import numpy as np
from town_paths import GAME_DIR, ROADS_INI  # enters the work folder
INI = GAME_DIR + r"\Data\INI\Object"
objs = {}
cur = None
for fn in glob.glob(INI + r"\**\*.ini", recursive=True):
    for line in open(fn, encoding="latin-1", errors="replace"):
        s = line.split(";")[0].rstrip()
        m = re.match(r"^Object\s+(\S+)", s)
        if m:
            cur = objs.setdefault(m.group(1), {"file": os.path.basename(fn)})
            continue
        if cur is None:
            continue
        m = re.match(r"\s*(EditorSorting|Side|Geometry|GeometryMajorRadius|GeometryMinorRadius|GeometryHeight)\s*=\s*(\S+)", s)
        if m:
            cur.setdefault(m.group(1), m.group(2))
        if re.match(r"\s*KindOf\s*=", s):
            cur["kindof"] = s.split("=", 1)[1].split()
json.dump(objs, open("gamedata/objs.json", "w"))
fam = collections.defaultdict(list)
for n, o in objs.items():
    if o.get("EditorSorting") != "STRUCTURE" or "STRUCTURE" not in o.get("kindof", []):
        continue
    if o.get("Side", "Civilian") not in ("Civilian", "Neutral", ""):
        continue
    # INI values can carry stray letters ("40v.0"): take the leading number
    num = lambda k: float((re.match(r"\d+(?:\.\d+)?", str(o.get(k, 0))) or [0])[0])
    a, b, h = num("GeometryMajorRadius"), num("GeometryMinorRadius"), num("GeometryHeight")
    if max(a, b) < 14:
        continue
    m = re.match(r"[A-Z][a-z]+", n)
    fam[m.group(0) if m else n].append((n, int(a), int(b), int(h)))
for k, v in sorted(fam.items(), key=lambda kv: -len(kv[1])):
    print(f"{k} ({len(v)}):", " ".join(f"{n}:{a}x{b}h{h}" for n, a, b, h in sorted(v)[:45]))
print()
isstreet = lambda t: "Lane" in t or t in ("Cobblestone", "GravelRoad") or "Dirt" in t
ROADS = {}
for line in open(ROADS_INI, encoding="latin-1"):
    p = line.split()
    if len(p) == 2 and p[0] == "Road":
        c = p[1]
    if len(p) == 3 and p[0] == "RoadWidth":
        ROADS[c] = float(p[2])
SPECIAL = re.compile(r"^(Tech|SupplyDock|SupplyPile|CivilianTEROilDepot)")
rows = collections.defaultdict(list)
near = collections.defaultdict(collections.Counter)
for name, path in (("ha", "refs/ha/objects.json"), ("bt", "refs/bt/objects.json"), ("tc", "refs/tc/objects.json"), ("vv", "refs/vv/objects.json"), ("aa", "refs/alpine/aa_objects.json")):
    O = json.load(open(path))
    rp = [o for o in O if o["kind"] == "road_point"]
    segs = [(a["template"], a["x"], a["y"], b["x"], b["y"]) for a, b in zip(rp[0::2], rp[1::2]) if isstreet(a["template"])]
    S = np.array([s[1:] for s in segs], float)
    W = np.array([ROADS.get(s[0], 30) / 2 for s in segs])
    A = [o for o in O if o["kind"] == "object" and not o["template"].startswith(("Amb", "Tree", "Rock"))]
    for o in A:
        if not SPECIAL.match(o["template"]):
            continue
        dx, dy = S[:, 2] - S[:, 0], S[:, 3] - S[:, 1]
        t = np.clip(((o["x"] - S[:, 0]) * dx + (o["y"] - S[:, 1]) * dy) / np.maximum(dx * dx + dy * dy, 1), 0, 1)
        d = np.hypot(o["x"] - S[:, 0] - t * dx, o["y"] - S[:, 1] - t * dy) - W
        g = geo = None
        nb = [q for q in A if q is not o and math.dist((q["x"], q["y"]), (o["x"], o["y"])) < 90]
        blds = [q for q in O if q["kind"] == "object" and q is not o and math.dist((q["x"], q["y"]), (o["x"], o["y"])) < 200 and re.match(r"^(Stan|Mogadishu|Asia|China|German|Civilian|Euro|Soviet|Convien|Junk)", q["template"])]
        rows[o["template"]].append((name, float(d.min()), len(blds), o["owner"]))
        for q in nb:
            near[o["template"]][q["template"]] += 1
print("template, count by map, street edge median, buildings within 200 median, owners")
for t, v in sorted(rows.items(), key=lambda kv: -len(kv[1])):
    print(f"  {t:24s} {dict(collections.Counter(r[0] for r in v))} edge {np.median([r[1] for r in v]):.0f} (10-90%: {np.percentile([r[1] for r in v], 10):.0f}..{np.percentile([r[1] for r in v], 90):.0f}) bld200 {np.median([r[2] for r in v]):.0f} owners {collections.Counter(r[3] for r in v).most_common(2)}")
    print("     near:", near[t].most_common(10))
