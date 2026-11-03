# Outposts: real small groups harvested from reference maps (kit_extract.py), stamped into the open map to decorate Tech buildings and
# supplies, fill empty spots and give lone garrisonable buildings some flavour. Used by server.py through the bridge it is given.
import json, math, os, re, time
import town_tool

HERE = town_tool.HERE
WORK = town_tool.WORK
ANCHOR = re.compile(r"^(Tech|SupplyDock|SupplyPile|SupplyWarehouse)")
NATURE = town_tool.NATURE
OWNER = "teamPlyrCivilian"

def library():
    return json.load(open(os.path.join(HERE, "packs", "outposts.json")))["kits"]

def _pick(kits, theme=None, purpose=None, contains=None, max_radius=None, shore=True):
    out = kits if shore else [k for k in kits if not k.get("shore")]
    if theme:
        out = [k for k in out if k["theme"] == theme]
    if purpose:
        out = [k for k in out if purpose in k["purpose"]]
    if contains:
        c = contains.lower()
        out = [k for k in out if any(c in o["t"].lower() for o in k["objects"])]
    if max_radius:
        out = [k for k in out if k["radius"] <= max_radius]
    return out

def outpost_list(theme=None, purpose=None, contains=None, max_radius=None):
    kits = _pick(library(), theme, purpose, contains, max_radius)
    rows = []
    for k in kits:
        c = {}
        for o in k["objects"]:
            n = re.sub(r"\d+$", "", o["t"])
            c[n] = c.get(n, 0) + 1
        rows.append({"id": k["id"], "theme": k["theme"], "purpose": k["purpose"], "shore": k.get("shore", False), "radius": k["radius"], "objects": dict(sorted(c.items(), key=lambda kv: -kv[1])[:6])})
    return {"count": len(rows), "themes": sorted({k["theme"] for k in library()}), "purposes": ["tech", "supply", "garrison", "decor"], "kits": rows}

class _Map:
    """What is in the way on the open map, kept up to date while kits go down."""
    def __init__(self, wb, max_slope):
        import numpy as np
        if not os.path.exists(os.path.join(WORK, "gamedata", "geo.json")):
            town_tool._run(["gamedata.py"])
        self.geo = json.load(open(os.path.join(WORK, "gamedata", "geo.json")))
        self.wb = wb
        info = wb.call("map.info")
        hm, world = info["heightmap"], info["world"]
        self.B, self.nx, self.ny = hm["border"], hm["x_extent"], hm["y_extent"]
        self.px, self.py = world["playable_max_x"], world["playable_max_y"]
        self.folder = os.path.join(WORK, "plans", f"outpost_{int(time.time())}")
        os.makedirs(self.folder, exist_ok=True)
        self.heights = town_tool._read_heights(wb, self.nx, self.ny)
        h = self.heights
        h4 = np.stack([h[:-1, :-1], h[1:, :-1], h[:-1, 1:], h[1:, 1:]])
        slope = (h4.max(axis=0) - h4.min(axis=0)) * world.get("height_scale", 0.625)
        cliff = town_tool._read_mask(wb, "passability", os.path.join(self.folder, "_cliff.png"))
        water = town_tool._read_mask(wb, "water", os.path.join(self.folder, "_water.png"))
        self.hard = cliff | water
        self.blocked = self.hard | (slope > max_slope)
        self.widths = town_tool._road_widths(os.environ["GAME_DIR"])
        self.textures = {t["name"] for t in wb.call("map.usage")["textures"]}
        self.listed = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
        self.nature_cells = np.zeros_like(self.blocked)
        # With clear_nature the trees in a kit's circle go before it is placed, so buildings may stand where they are (set by the caller).
        self.clear_nature = False
        for o in self.listed:
            if o["kind"] == "object" and not o["template"].startswith("Amb"):
                a, b = self.geo.get(o["template"], [6, 6])
                if NATURE.match(o["template"]):
                    town_tool._rect_cells(self.nature_cells, o["x"], o["y"], a + 2, b + 2, o["angle_deg"], self.B)
                else:
                    town_tool._rect_cells(self.blocked, o["x"], o["y"], a + 6, b + 6, o["angle_deg"], self.B)
        rp = [o for o in self.listed if o["kind"] == "road_point"]
        for a, b in zip(rp[0::2], rp[1::2]):
            town_tool._cells_along(self.blocked, (a["x"], a["y"]), (b["x"], b["y"]), self.widths.get(a["template"], 30) / 2 + 8, self.B)

    def rect_free(self, x, y, a, b, deg, nature_ok):
        import numpy as np
        m = np.zeros_like(self.blocked)
        town_tool._rect_cells(m, x, y, a, b, deg, self.B)
        if not m.any():
            return False
        bad = self.blocked | (self.nature_cells if not nature_ok else False)
        return not bad[m].any()

    def add_rect(self, x, y, a, b, deg):
        town_tool._rect_cells(self.blocked, x, y, a, b, deg, self.B)

def _layout(m, kit, x, y, angle, skip_anchor, include_ground, include_roads):
    """The kit at (x, y), turned by angle. None if a building does not fit; props that do not fit are left out."""
    r = math.radians(angle)
    cr, sr = math.cos(r), math.sin(r)
    objects, dropped = [], 0
    for o in sorted(kit["objects"], key=lambda o: -max(m.geo.get(o["t"], [4, 4]))):
        t = o["t"]
        # decorating adds no resources or Tech buildings: those change how a map plays
        if skip_anchor and (ANCHOR.match(t) or t.endswith("Bib")):
            continue
        wx, wy = x + o["x"] * cr - o["y"] * sr, y + o["x"] * sr + o["y"] * cr
        deg = (o["a"] + angle) % 360
        a, b = m.geo.get(t, [4, 4])
        big = max(a, b) >= 12
        if not (0 <= wx <= m.px and 0 <= wy <= m.py) or not m.rect_free(wx, wy, a + 2, b + 2, deg, nature_ok=not big or m.clear_nature):
            if big:
                return None
            dropped += 1
            continue
        entry = {"template": t, "x": round(wx), "y": round(wy), "angle_deg": round(deg, 1)}
        if o.get("owned"):
            entry["owner"] = OWNER
        objects.append(entry)
    ground = []
    if include_ground != "none":
        for tex, cells in kit["ground"].items():
            # "auto": only what the map uses already, so that a kit's sand does not land on a meadow as a patch
            if include_ground == "auto" and tex not in m.textures:
                continue
            for cx_, cy_ in cells:
                ground.append((x + cx_ * cr - cy_ * sr, y + cx_ * sr + cy_ * cr, tex))
    roads = []
    if include_roads:
        for t, ax, ay, bx, by in kit["roads"]:
            roads.append({"type": t, "corners": "tight", "end_caps": not t.startswith("Sidewalk"),
                          "points": [[round(x + ax * cr - ay * sr, 1), round(y + ax * sr + ay * cr, 1)], [round(x + bx * cr - by * sr, 1), round(y + bx * sr + by * cr, 1)]]})
    return {"kit": kit["id"], "x": round(x), "y": round(y), "angle": round(angle, 1), "objects": objects, "ground": ground, "roads": roads, "props_left_out": dropped}

def _commit(m, placed, flatten, clear_nature, reblend):
    """Puts the laid out kits on the map."""
    import numpy as np
    from PIL import Image
    wb = m.wb
    result = {}
    all_objects = [o for p in placed for o in p["objects"]]
    all_roads = [r for p in placed for r in p["roads"]]
    plan = {"objects": all_objects, "roads": all_roads}
    if clear_nature:
        ids = []
        for o in m.listed:
            if o["kind"] == "object" and NATURE.match(o["template"]) and any(math.dist((o["x"], o["y"]), (p["x"], p["y"])) < p["radius"] for p in placed):
                ids.append(o["id"])
        if ids:
            wb.call("objects.delete", ids=ids)
            result["nature_removed"] = len(ids)
    if flatten:
        result["flatten"] = town_tool._flatten(wb, m.heights, m.hard, plan, m.geo, m.widths, m.B)
    if any(o.get("owner") for o in all_objects) and OWNER not in json.dumps(wb.call("sides.list")):
        wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
    masks = {}
    for p in placed:
        for gx, gy, tex in p["ground"]:
            i, j = int(gx / 10 + m.B), int(gy / 10 + m.B)
            if 0 <= j < m.ny - 1 and 0 <= i < m.nx - 1 and not m.hard[j, i]:
                masks.setdefault(tex, np.zeros((m.ny - 1, m.nx - 1), bool))[j, i] = True
    errors = []
    for tex, mask in masks.items():
        path = os.path.join(m.folder, tex + ".png")
        Image.fromarray((mask[::-1] * 255).astype(np.uint8)).save(path)
        try:
            wb.call("terrain.import_mask", path=path, target="texture", texture=tex, fit="exact")
        except Exception as exc:
            errors.append(f"texture {tex}: {exc}")
    done = 0
    for o in all_objects:
        try:
            wb.call("objects.place", **o)
            done += 1
        except Exception as exc:
            errors.append(f"{o['template']}: {exc}")
    for r in all_roads:
        wb.call("roads.add", **r)
    if masks and reblend == "map":
        wb.call("terrain.reblend")
    elif masks and reblend == "area":
        for p in placed:
            R = p["radius"] + 40
            wb.call("terrain.reblend", shape="circle", x=p["x"], y=p["y"], radius=R)
    result["objects_placed"] = done
    if errors:
        result["errors"] = errors[:20]
    result["saved"] = False
    return result

def _summary(p):
    return {"kit": p["kit"], "x": p["x"], "y": p["y"], "angle": p["angle"], "objects": len(p["objects"]), "props_left_out": p["props_left_out"]}

def place(wb, x=None, y=None, around=None, kit=None, theme=None, purpose=None, contains=None, angle=None, seed=1, ground="auto", roads=True,
          flatten=True, clear_nature=False, max_slope=12.0, reblend="area", dry_run=False):
    import numpy as np
    rng = np.random.default_rng(seed)
    lib = library()
    m = _Map(wb, max_slope)
    m.clear_nature = clear_nature
    skip_anchor = False
    if around is not None:
        target = next((o for o in m.listed if o["id"] == around and o["kind"] == "object"), None)
        if target is None:
            raise ValueError(f"no object with id {around}: see objects_list")
        x, y = target["x"], target["y"]
        skip_anchor = True
        # it stands where it stands: its own footprint must not count against the kit
        anchor = target["template"]
    if x is None or y is None:
        raise ValueError("give x and y, or around=<object id>")
    if kit:
        cands = [k for k in lib if k["id"] == kit]
        if not cands:
            raise ValueError(f"unknown kit {kit}: see outpost_list")
    else:
        cands = _pick(lib, theme, purpose, contains)
        if around is not None:
            same = [k for k in cands if k["anchor"] == anchor]
            cands = same or [k for k in cands if k["anchor"]]
        if not cands:
            raise ValueError("no kit matches: see outpost_list")
    if around is not None:
        # free the anchor's own cells, it is meant to stay in the middle of the kit
        a, b = m.geo.get(anchor, [20, 20])
        c = np.zeros_like(m.blocked)
        town_tool._rect_cells(c, x, y, a + 6, b + 6, target["angle_deg"], m.B)
        m.blocked &= ~c
    for k in [cands[i] for i in rng.permutation(len(cands))]:
        tries = [angle] if angle is not None else ([target["angle_deg"]] if around is not None else []) + [float(v) for v in rng.uniform(0, 360, 8)]
        for ang in tries:
            p = _layout(m, k, x, y, ang, skip_anchor, ground, roads)
            if p is not None and p["objects"]:
                p["radius"] = k["radius"]
                out = {"placed": _summary(p)}
                if dry_run:
                    out["dry_run"] = True
                    return out
                out.update(_commit(m, [p], flatten, clear_nature, reblend))
                return out
    raise RuntimeError("no kit fits here: water, cliffs, steep ground, objects or roads are in the way of its buildings")

def populate(wb, count=4, theme=None, purpose=None, contains=None, area=None, decorate_tech=False, spacing=350, seed=1, ground="auto", roads=True,
             flatten=True, clear_nature=False, max_slope=12.0, max_radius=220, reblend="area", dry_run=False):
    import numpy as np
    rng = np.random.default_rng(seed)
    lib = library()
    m = _Map(wb, max_slope)
    m.clear_nature = clear_nature
    placed, used_at, notes = [], [], []
    def far(x, y):
        return all(math.dist((x, y), q) >= spacing for q in used_at)
    def inside(x, y):
        return town_tool._inside(x, y, area) if area else (0 <= x <= m.px and 0 <= y <= m.py)
    if decorate_tech:
        techs = [o for o in m.listed if o["kind"] == "object" and ANCHOR.match(o["template"]) and not o["template"].endswith("Bib") and inside(o["x"], o["y"])]
        seen = []
        for t in techs:
            # piles stand in groups: one kit for a group
            if any(math.dist((t["x"], t["y"]), q) < 150 for q in seen):
                continue
            seen.append((t["x"], t["y"]))
            cands = _pick(lib, theme, None, contains, shore=False)
            same = [k for k in cands if k["anchor"] == t["template"]]
            cands = same or [k for k in cands if k["anchor"]]
            a, b = m.geo.get(t["template"], [20, 20])
            c = np.zeros_like(m.blocked)
            town_tool._rect_cells(c, t["x"], t["y"], a + 6, b + 6, t["angle_deg"], m.B)
            saved = m.blocked.copy()
            m.blocked &= ~c
            done = False
            for k in [cands[i] for i in rng.permutation(len(cands))][:12]:
                p = _layout(m, k, t["x"], t["y"], t["angle_deg"], True, ground, roads)
                if p and p["objects"]:
                    p["radius"] = k["radius"]
                    p["around"] = t["template"]
                    placed.append(p)
                    used_at.append((t["x"], t["y"]))
                    done = True
                    break
            m.blocked = saved
            if done:
                for o in placed[-1]["objects"]:
                    a2, b2 = m.geo.get(o["template"], [4, 4])
                    m.add_rect(o["x"], o["y"], a2 + 8, b2 + 8, o["angle_deg"])
            else:
                notes.append(f"nothing fits around {t['template']} at {round(t['x'])},{round(t['y'])}")
    # empty spots get no resources or Tech buildings, and nothing that needs a shore
    cands = _pick(lib, theme, purpose, contains, max_radius, shore=False)
    cands = [k for k in cands if not k["anchor"] and not any(ANCHOR.match(o["t"]) for o in k["objects"])]
    tries = 0
    want = len(placed) + count
    while len(placed) < want and tries < 1500 and cands:
        tries += 1
        x, y = float(rng.uniform(60, m.px - 60)), float(rng.uniform(60, m.py - 60))
        if not inside(x, y) or not far(x, y):
            continue
        k = cands[int(rng.integers(len(cands)))]
        # a free spot: nothing inside the kit's circle
        c = np.zeros_like(m.blocked)
        town_tool._cells_along(c, (x, y), (x, y), k["radius"], m.B)
        if m.blocked[c].mean() > 0.05:
            continue
        p = _layout(m, k, x, y, float(rng.uniform(0, 360)), False, ground, roads)
        if p and p["objects"]:
            p["radius"] = k["radius"]
            placed.append(p)
            used_at.append((x, y))
            for o in p["objects"]:
                a2, b2 = m.geo.get(o["template"], [4, 4])
                m.add_rect(o["x"], o["y"], a2 + 8, b2 + 8, o["angle_deg"])
    if len(placed) < want:
        notes.append(f"found room for {len(placed)} of {want}")
    out = {"placed": [dict(_summary(p), **({"around": p["around"]} if "around" in p else {})) for p in placed], "notes": notes}
    if dry_run:
        out["dry_run"] = True
        return out
    if placed:
        out.update(_commit(m, placed, flatten, clear_nature, "map" if len(placed) > 6 and reblend == "area" else reblend))
    return out
