# How reference cities connect different street types, and what their parking rows look like.
import json, math, os, sys, collections
import numpy as np
from town_paths import ROADS_INI  # enters the work folder
ROADS = {}
for line in open(ROADS_INI, encoding="latin-1"):
    p = line.split()
    if len(p) == 2 and p[0] == "Road":
        cur = p[1]
    if len(p) == 3 and p[0] in ("RoadWidth", "RoadWidthInTexture"):
        ROADS.setdefault(cur, {})[p[1 - 1]] = float(p[2])
for t in ("FourLane", "FourLaneDark", "TwoLane", "TwoLaneDark", "Sidewalk", "Sidewalk2", "Sidewalk3", "Sidewalk4", "ParkingLines", "Cobblestone"):
    print(t, ROADS.get(t))
isstreet = lambda t: "Lane" in t or t == "Cobblestone"
for name in sys.argv[1:]:
    O = json.load(open(f"refs/{name}/objects.json"))
    rp = [o for o in O if o["kind"] == "road_point"]
    pairs = list(zip(rp[0::2], rp[1::2]))
    pts = collections.Counter()
    for a, b in pairs:
        pts[(a["template"], round(a["x"]), round(a["y"]))] += 1
        pts[(b["template"], round(b["x"]), round(b["y"]))] += 1
    segs = [(a["template"], a["x"], a["y"], b["x"], b["y"]) for a, b in pairs]
    print("==", name)
    # open ends of streets: what lies there
    res = collections.Counter()
    caps = collections.Counter()
    seglen = collections.defaultdict(list)
    for a, b in pairs:
        if isstreet(a["template"]):
            seglen[a["template"]].append(math.dist((a["x"], a["y"]), (b["x"], b["y"])))
        for p in (a, b):
            if not isstreet(p["template"]) or pts[(p["template"], round(p["x"]), round(p["y"]))] > 1:
                continue
            best = 1e9
            kind = None
            for s in segs:
                if not isstreet(s[0]) or s[0] == p["template"]:
                    continue
                dx, dy = s[3] - s[1], s[4] - s[2]
                q = dx * dx + dy * dy or 1
                t = max(0, min(1, ((p["x"] - s[1]) * dx + (p["y"] - s[2]) * dy) / q))
                d = math.hypot(p["x"] - s[1] - t * dx, p["y"] - s[2] - t * dy)
                if d < best:
                    best, kind = d, s[0]
            w = ROADS.get(kind, {}).get("RoadWidth", 35)
            tag = f"{p['template']} end on {kind}" if best < w * 0.55 else f"{p['template']} free end"
            res[tag] += 1
            caps[(tag, bool(p.get("end_cap")))] += 1
    print(" open street ends", res.most_common(8))
    print(" with end cap", sorted(caps.items(), key=lambda kv: -kv[1])[:8])
    print(" segment length median", {k: round(float(np.median(v))) for k, v in seglen.items()})
    walk_caps = collections.Counter(bool(p.get("end_cap")) for p in rp if p["template"].startswith("Sidewalk"))
    print(" sidewalk points with end cap", dict(walk_caps))
    park = [s for s in segs if s[0] == "ParkingLines"]
    if park:
        print(" parking rows", len(park), "length median", round(float(np.median([math.dist(s[1:3], s[3:5]) for s in park]))))
