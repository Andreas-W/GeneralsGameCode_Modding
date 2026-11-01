# Harvests small groups of objects from scanned reference maps into a kit library: what stands together, the ground painted under it, short roads.
# Rules for groups this small are hard to generalize, so real groups are kept and stamped. Usage: kit_extract.py   (needs scan_refs.py and gamedata.py)
import glob, json, math, os, re, collections
import numpy as np
from PIL import Image, ImageDraw
from town_paths import PACKS  # enters the work folder

THEMES = {"cairo": "arabic", "golden": "arabic", "lights": "industrial", "beach": "tropical", "defcon": "military",
          "silent": "tropical", "warlords": "industrial", "winding": "tropical"}
geo = json.load(open("gamedata/geo.json"))
objs = json.load(open("gamedata/objs.json"))
NATURE = re.compile(r"^(Tree|Bush|Rocks|Shrub|DesertShrub|ArborVitae|Palm)")
SKIP = re.compile(r"^(Amb|CINE|Waypoint|America|China[A-Z]|GLA|Boss|Tank_|Nuke_|Lazr_|AirF_|SupW_|Demo_|Infa_|Slth_|Chem_|Mogadishu.*Civilian|.*Civilian0|HomelessGuy|TechBuildingRebuildHole)")
ANCHOR = re.compile(r"^(Tech|SupplyDock|SupplyPile|SupplyWarehouse)")
LINK = 40       # gap between footprints that still counts as together
LARGE = 520     # towns and rows of buildings are not kits
def radius(t):
    g = geo.get(t)
    return max(g) if g else 6
def is_building(t):
    o = objs.get(t, {})
    return o.get("EditorSorting") == "STRUCTURE" and radius(t) >= 12 and not t.endswith("Bib") and not ANCHOR.match(t)
# what a kit shows decides its theme, not the map it came from: Silent River has towns as well as huts
FAMILIES = [("arabic", r"^(Mogadishu|JunkDealer|MarketVender|Tent\d|Tent$|EgyptianPyramid)"), ("tropical", r"^(GrassHut|RiverHouse|Hut)"),
            ("urban", r"^(Stan|Asia|China|Soviet|CivilianHighrise|Euro|Convien|German|American)"), ("military", r"^(NuclearBunker|NukeBunker|Sandbag|SandBag|GuardTower|WatchTower)"),
            ("industrial", r"^(Quonset|CivilianBunker|Oil|StanOil|Gas|FuelTank|Shipping|Industrial|Water|Crane|VehicleHanger|Searchlight)")]
def theme_of(templates, default):
    c = collections.Counter()
    for t in templates:
        for name, pat in FAMILIES:
            if re.match(pat, t):
                c[name] += max(radius(t), 8)
                break
    return c.most_common(1)[0][0] if c else default
def ground_by_tex(cells):
    out = {}
    for x, y, t in cells:
        out.setdefault(t, []).append([x, y])
    return out
kits = []
for name, theme in THEMES.items():
    if not os.path.exists(f"refs/{name}/dump.json"):
        print("not scanned:", name)
        continue
    info = json.load(open(f"refs/{name}/info.json"))
    B = info["heightmap"]["border"]
    O = json.load(open(f"refs/{name}/dump.json"))["objects"]
    rp = [o for o in O if o["kind"] == "road_point"]
    segs = [(a["template"], a["x"], a["y"], b["x"], b["y"]) for a, b in zip(rp[0::2], rp[1::2])]
    items = [o for o in O if o["kind"] == "object" and not SKIP.match(o["template"]) and not NATURE.match(o["template"])]
    nature = [o for o in O if o["kind"] == "object" and NATURE.match(o["template"])]
    # single link clusters
    parent = list(range(len(items)))
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for i, a in enumerate(items):
        for j in range(i):
            b = items[j]
            if math.dist((a["x"], a["y"]), (b["x"], b["y"])) - radius(a["template"]) - radius(b["template"]) < LINK:
                parent[find(i)] = find(j)
    groups = collections.defaultdict(list)
    for i, o in enumerate(items):
        groups[find(i)].append(o)
    tex = {os.path.basename(f)[4:-4]: (np.array(Image.open(f)) > 0)[::-1] for f in glob.glob(f"refs/{name}/tex_*.png")}
    names = list(tex)
    H, W = next(iter(tex.values())).shape
    label = np.full((H, W), -1)
    for k, n in enumerate(names):
        label[tex[n]] = k
    XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(H) - B) * 10.0 + 5)
    for g in groups.values():
        bld = [o for o in g if is_building(o["template"])]
        anchors = [o for o in g if ANCHOR.match(o["template"])]
        if len(bld) >= 10 or not (bld or anchors or len(g) >= 4):
            continue
        xs, ys = [o["x"] for o in g], [o["y"] for o in g]
        if max(xs) - min(xs) > LARGE or max(ys) - min(ys) > LARGE:
            continue
        if len(g) < 2:
            # a lone building is only worth keeping with something around it, added below
            if not any(math.dist((g[0]["x"], g[0]["y"]), (n["x"], n["y"])) < radius(g[0]["template"]) + 40 for n in nature):
                continue
        a = anchors[0] if anchors else max(bld, key=lambda o: radius(o["template"])) if bld else g[0]
        cx, cy = a["x"], a["y"]
        members = list(g)
        for n in nature:
            if len(members) < len(g) + 12 and any(math.dist((n["x"], n["y"]), (o["x"], o["y"])) - radius(o["template"]) < 34 for o in g):
                members.append(n)
        R = max(math.dist((o["x"], o["y"]), (cx, cy)) + radius(o["template"]) for o in members) + 25
        # ground: what differs from the surroundings of the group
        d = np.hypot(XC - cx, YC - cy)
        ring = (d > R) & (d < R + 70)
        base = collections.Counter(label[ring].tolist()).most_common(1)[0][0] if ring.any() else -1
        cells = [[round(float(XC[j, i] - cx)), round(float(YC[j, i] - cy)), names[label[j, i]]] for j, i in zip(*np.nonzero((d <= R) & (label != base) & (label >= 0)))]
        # a group on a large painted area is not what paints it: keep at most what lies close to its objects
        cells = [c for c in cells if any(math.dist((c[0] + cx, c[1] + cy), (o["x"], o["y"])) < radius(o["template"]) + 45 for o in members)]
        # shore and water textures make no sense away from that water
        cells = [c for c in cells if not re.search(r"blue|wet", c[2], re.I)]
        roads = []
        for s in segs:
            # paths inside the group, not streets and tracks that pass by
            if not re.match(r"^(Dirt|Gravel|Cobblestone|Sidewalk)", s[0]) or "Road" in s[0] and s[0] != "DirtRoad3" and not s[0].startswith("GravelRoad"):
                continue
            if max(math.dist(s[1:3], (cx, cy)), math.dist(s[3:5], (cx, cy))) <= R + 10:
                roads.append([s[0], round(s[1] - cx), round(s[2] - cy), round(s[3] - cx), round(s[4] - cy)])
        tset = [o["template"] for o in members]
        garrison = [t for t in tset if objs.get(t, {}).get("garrison")]
        purpose = sorted(({"tech"} if any(t.startswith("Tech") for t in tset) else set())
                         | ({"supply"} if any(t.startswith("Supply") for t in tset) else set())
                         | ({"garrison"} if garrison else set()) or {"decor"})
        kits.append({"id": f"{name}_{len(kits):03d}", "map": name, "theme": theme_of(tset, theme), "shore": any(re.match(r"^(Boat|Dock)", t) for t in tset), "purpose": purpose, "anchor": a["template"] if anchors else None,
                     "radius": round(R), "objects": [{"t": o["template"], "x": round(o["x"] - cx), "y": round(o["y"] - cy), "a": round(o["angle_deg"], 1),
                                                      "owned": o["owner"] != "team"} for o in members],
                     "ground": ground_by_tex(cells), "roads": roads})
json.dump({"kits": kits}, open(f"{PACKS}/outposts.json", "w"), separators=(",", ":"))
print("kits", len(kits), collections.Counter(k["map"] for k in kits).most_common(), collections.Counter(p for k in kits for p in k["purpose"]).most_common())
print("file size", os.path.getsize(f"{PACKS}/outposts.json"))
# contact sheet
CELL, COLS = 230, 8
rows = math.ceil(len(kits) / COLS) or 1
sheet = Image.new("RGB", (CELL * COLS, CELL * rows), (40, 45, 40))
dr = ImageDraw.Draw(sheet)
for k, kit in enumerate(kits):
    ox, oy = (k % COLS) * CELL + CELL / 2, (k // COLS) * CELL + CELL / 2
    sc = min(1.0, (CELL / 2 - 8) / kit["radius"])
    P = lambda x, y: (ox + x * sc, oy - y * sc)
    for x, y in [c for cs in kit["ground"].values() for c in cs]:
        a = P(x - 5, y + 5)
        dr.rectangle((a[0], a[1], a[0] + 10 * sc, a[1] + 10 * sc), fill=(120, 105, 80))
    for r in kit["roads"]:
        dr.line([P(r[1], r[2]), P(r[3], r[4])], fill=(20, 20, 20), width=2)
    for o in kit["objects"]:
        t = o["t"]
        a_, b_ = geo.get(t, [4, 4])
        r_ = math.radians(o["a"])
        q = [P(o["x"] + u * math.cos(r_) - v * math.sin(r_), o["y"] + u * math.sin(r_) + v * math.cos(r_)) for u, v in ((-a_, -b_), (a_, -b_), (a_, b_), (-a_, b_))]
        col = (70, 150, 70) if NATURE.match(t) else (230, 200, 60) if ANCHOR.match(t) else (200, 80, 70) if is_building(t) else (120, 170, 230)
        dr.polygon(q, fill=col)
    dr.text((ox - CELL / 2 + 3, oy - CELL / 2 + 2), f"{kit['id']} {','.join(kit['purpose'])}", fill=(255, 255, 255))
    main = collections.Counter(re.sub(r"\d+$", "", o["t"]) for o in kit["objects"] if not NATURE.match(o["t"])).most_common(3)
    dr.text((ox - CELL / 2 + 3, oy + CELL / 2 - 14), " ".join(f"{n}x{c}" for n, c in main)[:40], fill=(230, 230, 230))
sheet.save("plans/kits.png")
print("sheet", sheet.size)
