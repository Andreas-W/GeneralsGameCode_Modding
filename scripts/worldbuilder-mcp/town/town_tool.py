# town_generate: builds a town of a style into the map open in WorldBuilder. Reads what is on the map, runs a generator on it, flattens
# the ground under the town and places everything. Used by server.py; talks to WorldBuilder through the bridge it is given.
import json, math, os, re, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get("TOWN_WORK") or os.path.join(HERE, "work")
ARABIC_BLOCKS = ("golden", "baghdad")
ARABIC_ORGANIC = ("generic", "scorcho", "serpent", "bridge", "cairo")

NATURE = re.compile(r"^(Tree|Bush|Rocks|Shrub|DesertShrub|ArborVitae)")

def _inside(x, y, poly):
    c = False
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            c = not c
    return c

def styles():
    return {"alpine": [], "urban": [], "arabic": list(ARABIC_ORGANIC + ARABIC_BLOCKS)}

def _command(style, preset, seed, site, out, jitter, theta, irregularity, theme, lots, outline, grid_scale=None, major=None, contain=False,
             street_style=None):
    """The generator command for a style; each generator takes its own options."""
    opts = [f"site={site}", f"out={out}"]
    if lots:
        opts.append(f"lots={lots}")
    if theme:
        opts.append(f"theme={theme}")
    grid = ([f"jitter={jitter}"] if jitter is not None else []) + ([f"theta={theta}"] if theta is not None else []) +         ([f"grid_scale={grid_scale}"] if grid_scale is not None else []) + ([f"major={major}"] if major else []) + (["contain=1"] if contain else []) +         ([f"street_style={street_style}"] if street_style else [])
    if style == "alpine":
        return ["town_plan.py", str(seed)] + ([] if outline else ["no-outline"]) + opts
    if style == "urban":
        return ["town_urban.py", "urban_generic", str(seed)] + grid + opts
    if style == "arabic":
        preset = preset or "generic"
        if preset in ARABIC_BLOCKS:
            return ["town_urban.py", f"arabic_{preset}", str(seed)] + grid + opts
        if preset in ARABIC_ORGANIC:
            return ["town_organic.py", "arabic", preset, str(seed)] + ([f"irregularity={irregularity}"] if irregularity is not None else []) + opts
        raise ValueError(f"unknown arabic preset {preset}; known: {', '.join(ARABIC_ORGANIC + ARABIC_BLOCKS)}")
    raise ValueError(f"unknown style {style}; known: alpine, urban, arabic")

def _run(cmd):
    r = subprocess.run([sys.executable, os.path.join(HERE, cmd[0])] + cmd[1:], capture_output=True, text=True, cwd=HERE, env=os.environ.copy())
    if r.returncode:
        raise RuntimeError(f"{cmd[0]} failed: {(r.stderr or r.stdout)[-800:]}")
    return r.stdout

def _road_widths(game_dir):
    out, cur = {}, None
    for line in open(os.path.join(game_dir, "Data", "INI", "Roads.ini"), encoding="latin-1"):
        p = line.split()
        if len(p) == 2 and p[0] == "Road":
            cur = p[1]
        elif len(p) >= 3 and p[0] == "RoadWidth":
            out[cur] = float(p[2])
    return out

def _read_heights(wb, nx, ny):
    import numpy as np
    rows = []
    step = max(1, 65536 // nx)
    for y0 in range(0, ny, step):
        rows.extend(wb.call("terrain.get_heights", x0=0, y0=y0, w=nx, h=min(step, ny - y0))["heights"])
    return np.array(rows, float)

def _read_mask(wb, kind, path):
    import numpy as np
    from PIL import Image
    wb.call("terrain.export_mask", kind=kind, path=path, include_border=True)
    return (np.array(Image.open(path).convert("L")) > 0)[::-1]

def _cells_along(mask, a, b, hw, B):
    """Marks the cells within hw of the segment a-b."""
    import numpy as np
    h, w = mask.shape
    x0, x1 = min(a[0], b[0]) - hw, max(a[0], b[0]) + hw
    y0, y1 = min(a[1], b[1]) - hw, max(a[1], b[1]) + hw
    i0, i1 = max(int(x0 / 10 + B), 0), min(int(x1 / 10 + B) + 1, w - 1)
    j0, j1 = max(int(y0 / 10 + B), 0), min(int(y1 / 10 + B) + 1, h - 1)
    if i1 < i0 or j1 < j0:
        return
    xx, yy = np.meshgrid((np.arange(i0, i1 + 1) - B) * 10.0 + 5, (np.arange(j0, j1 + 1) - B) * 10.0 + 5)
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = np.clip(((xx - a[0]) * dx + (yy - a[1]) * dy) / max(dx * dx + dy * dy, 1e-6), 0, 1)
    mask[j0:j1 + 1, i0:i1 + 1] |= np.hypot(xx - a[0] - t * dx, yy - a[1] - t * dy) <= hw

def _rect_cells(mask, x, y, a, b, deg, B):
    import numpy as np
    h, w = mask.shape
    R = math.hypot(a, b) + 10
    i0, i1 = max(int((x - R) / 10 + B), 0), min(int((x + R) / 10 + B) + 1, w - 1)
    j0, j1 = max(int((y - R) / 10 + B), 0), min(int((y + R) / 10 + B) + 1, h - 1)
    if i1 < i0 or j1 < j0:
        return
    xx, yy = np.meshgrid((np.arange(i0, i1 + 1) - B) * 10.0 + 5, (np.arange(j0, j1 + 1) - B) * 10.0 + 5)
    r = math.radians(deg)
    u = (xx - x) * math.cos(r) + (yy - y) * math.sin(r)
    v = -(xx - x) * math.sin(r) + (yy - y) * math.cos(r)
    mask[j0:j1 + 1, i0:i1 + 1] |= (abs(u) <= a) & (abs(v) <= b)

def _flatten(wb, heights, hard, plan, geo, widths, B):
    """Smooths the ground under streets and buildings, levels a pad under each building, and leaves water and cliffs alone."""
    import numpy as np
    import scipy.ndimage as nd
    ny, nx = heights.shape
    area = np.zeros((ny - 1, nx - 1), bool)
    for r in plan["roads"]:
        w = widths.get(r["type"], 30)
        for a, b in zip(r["points"][:-1], r["points"][1:]):
            _cells_along(area, a, b, w / 2 + 20, B)
    structures = [o for o in plan["objects"] if o.get("owner")]
    for o in structures:
        a, b = geo.get(o["template"], [20, 20])
        _rect_cells(area, o["x"], o["y"], a + 25, b + 25, o["angle_deg"], B)
    if not area.any():
        return {"changed_vertices": 0}
    # cells to vertices: a vertex is touched by the four cells around it
    def to_vertices(c):
        v = np.zeros((ny, nx), bool)
        v[:-1, :-1] |= c
        v[1:, :-1] |= c
        v[:-1, 1:] |= c
        v[1:, 1:] |= c
        return v
    keep = to_vertices(hard)
    weight = nd.gaussian_filter(to_vertices(area).astype(float), 1.5)
    weight = np.clip(weight * 1.6, 0, 1)
    weight[keep] = 0
    new = heights + weight * (nd.gaussian_filter(heights, 4) - heights)
    xx, yy = np.meshgrid((np.arange(nx) - B) * 10.0, (np.arange(ny) - B) * 10.0)
    for o in structures:
        a, b = geo.get(o["template"], [20, 20])
        r = math.radians(o["angle_deg"])
        u = (xx - o["x"]) * math.cos(r) + (yy - o["y"]) * math.sin(r)
        v = -(xx - o["x"]) * math.sin(r) + (yy - o["y"]) * math.cos(r)
        inner = (abs(u) <= a + 5) & (abs(v) <= b + 5)
        if not inner.any():
            continue
        level = float(new[inner].mean())
        # full inside the footprint, fading out over 25 units
        d = np.maximum(abs(u) - a - 5, abs(v) - b - 5)
        w = np.clip(1 - d / 25, 0, 1)
        w[keep] = 0
        new = new + w * (level - new)
    changed = np.abs(new - heights) >= 0.5
    if not changed.any():
        return {"changed_vertices": 0}
    jj, ii = np.nonzero(changed)
    j0, j1, i0, i1 = jj.min(), jj.max(), ii.min(), ii.max()
    rows = [[int(round(new[j, i])) if changed[j, i] else None for i in range(i0, i1 + 1)] for j in range(j0, j1 + 1)]
    wb.call("terrain.set_heights", x0=int(i0), y0=int(j0), heights=rows)
    return {"changed_vertices": int(changed.sum()), "largest_change_raw": round(float(np.abs(new - heights).max()), 1)}

CONNECTABLE = re.compile(r"Lane|Dirt|Gravel|Cobblestone")

def _existing_roads(listed):
    """Streets already on the map, as (type, ax, ay, bx, by); no sidewalks, parking lines, rails or bridges."""
    rp = [o for o in listed if o["kind"] == "road_point"]
    return [(a["template"], a["x"], a["y"], b["x"], b["y"]) for a, b in zip(rp[0::2], rp[1::2]) if CONNECTABLE.search(a["template"]) and "Bridge" not in a["template"]]

def _plan_connections(exits, segs, widths, reach):
    """For each street leaving town, the nearest road on the map and how to meet it. The narrower of the two types forms the connection:
    same type ends on a road point, which makes a real junction; different types end at the edge of the wider road with an end cap,
    and the wider road keeps its connection."""
    out = []
    for ex in exits:
        p = ex["point"]
        best = None
        for s in segs:
            ax, ay, bx, by = s[1:]
            dx, dy = bx - ax, by - ay
            t = max(0, min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / max(dx * dx + dy * dy, 1e-6)))
            q = (ax + dx * t, ay + dy * t)
            d = math.dist(p, q)
            if best is None or d < best[0]:
                best = (d, s, q)
        if best is None or best[0] > reach:
            continue
        d, s, q = best
        road_w = widths.get(s[0], 30)
        kind = s[0] if road_w < ex["width"] else ex["type"]
        start = p
        if kind != ex["type"]:
            # a narrower connector ends beside the town's street
            start = [p[0] + ex["dir"][0] * (ex["width"] / 2), p[1] + ex["dir"][1] * (ex["width"] / 2)]
        if kind == s[0]:
            # the place on the road opposite the start; _meet_road makes a road point there
            to = q
            join = "junction"
        else:
            L = math.dist(start, q) or 1
            to = (q[0] + (start[0] - q[0]) / L * road_w / 2, q[1] + (start[1] - q[1]) / L * road_w / 2)
            join = "beside"
        out.append({"type": kind, "from": [round(start[0], 1), round(start[1], 1)], "to": [round(to[0], 1), round(to[1], 1)], "join": join, "road": s[0], "length": round(math.dist(start, to))})
    return out

def _meet_road(wb, c):
    """A junction needs a road point: the nearest one if it is close, otherwise the road is split where the connection meets it."""
    rp = [o for o in wb.call("objects.list", template=c["road"], include_waypoints_and_roads=True, limit=20000)["objects"] if o["kind"] == "road_point"]
    p = c["to"]
    best = None
    for a, b in zip(rp[0::2], rp[1::2]):
        dx, dy = b["x"] - a["x"], b["y"] - a["y"]
        t = max(0, min(1, ((p[0] - a["x"]) * dx + (p[1] - a["y"]) * dy) / max(dx * dx + dy * dy, 1e-6)))
        q = (a["x"] + dx * t, a["y"] + dy * t)
        d = math.dist(p, q)
        if best is None or d < best[0]:
            best = (d, a, b, q)
    if best is None:
        return p
    _, a, b, q = best
    for n in (a, b):
        if math.dist((n["x"], n["y"]), q) < 90:
            return [n["x"], n["y"]]
    # the bridge does not report the corner style of a point: tight is what roads_route uses at sharp bends
    wb.call("objects.delete", ids=[a["id"], b["id"]])
    q = [round(q[0], 1), round(q[1], 1)]
    def pieces(u, v):
        # very long segments can render with gaps, so the parts keep road points every so often
        n = max(1, int(math.dist(u, v) // 300))
        return [[round(u[0] + (v[0] - u[0]) * k / n, 1), round(u[1] + (v[1] - u[1]) * k / n, 1)] for k in range(n)]
    A, B = [a["x"], a["y"]], [b["x"], b["y"]]
    wb.call("roads.add", type=c["road"], points=pieces(A, q) + pieces(q, B) + [B], corners="tight", end_caps=bool(a.get("end_cap") or b.get("end_cap")))
    return q

def _build_connections(wb, plans, widths):
    done, failed = [], []
    for c in plans:
        w = widths.get(c["type"], 30)
        try:
            if c["join"] == "junction":
                c["to"] = _meet_road(wb, c)
                c["length"] = round(math.dist(c["from"], c["to"]))
            if c["length"] < 60:
                wb.call("roads.add", type=c["type"], points=[c["from"], c["to"]], corners="tight", end_caps=True)
            else:
                wb.call("roads.route", **{"from": c["from"]}, to=c["to"], type=c["type"], corners="auto", flatten=True, flatten_width=w + 10, snap=1, end_caps=True, object_clearance=8)
            done.append(c)
        except Exception as exc:
            failed.append(f"{c['type']} from {c['from']} to {c['road']}: {exc}")
    return done, failed

def generate(wb, polygon, style="urban", preset=None, seed=1, jitter=None, theta=None, irregularity=None, theme=None, lots=None,
             outline=False, flatten=True, max_slope=12.0, reblend="area", clear_nature=False, connect=True, connect_range=800.0, dry_run=False,
             grid_scale=None, major=None, contain=False, street_style=None):
    """See town_generate in server.py."""
    import numpy as np
    game_dir = os.environ.get("GAME_DIR", "")
    if not game_dir or not os.path.isdir(game_dir):
        raise RuntimeError("GAME_DIR is not set: put the mod folder into .claude/settings.local.json (see CLAUDE.md)")
    if len(polygon) < 3:
        raise ValueError("polygon needs at least 3 points")
    if reblend not in ("area", "map", "none"):
        raise ValueError("reblend must be area, map or none")
    if not os.path.exists(os.path.join(WORK, "gamedata", "geo.json")):
        _run(["gamedata.py"])
    geo = json.load(open(os.path.join(WORK, "gamedata", "geo.json")))
    info = wb.call("map.info")
    hm, world = info["heightmap"], info["world"]
    B, nx, ny = hm["border"], hm["x_extent"], hm["y_extent"]
    px, py = world["playable_max_x"], world["playable_max_y"]
    if not all(0 <= p[0] <= px and 0 <= p[1] <= py for p in polygon):
        raise ValueError(f"polygon must lie inside the playable area, 0..{px} x 0..{py}")
    stamp = f"town_{int(time.time())}"
    folder = os.path.join(WORK, "plans", stamp)
    os.makedirs(folder, exist_ok=True)
    # --- what is in the way ---
    heights = _read_heights(wb, nx, ny)
    scale = world.get("height_scale", 0.625)
    h4 = np.stack([heights[:-1, :-1], heights[1:, :-1], heights[:-1, 1:], heights[1:, 1:]])
    slope = (h4.max(axis=0) - h4.min(axis=0)) * scale
    cliff = _read_mask(wb, "passability", os.path.join(folder, "_cliff.png"))
    water = _read_mask(wb, "water", os.path.join(folder, "_water.png"))
    hard = cliff | water
    blocked = hard | (slope > max_slope)
    terrain_cells = int(blocked.sum())
    widths = _road_widths(game_dir)
    listed = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
    existing = 0
    nature = []
    for o in listed:
        if o["kind"] == "object" and not o["template"].startswith("Amb"):
            # trees and rocks inside the area can make room; everything else stays and is built around
            if clear_nature and NATURE.match(o["template"]) and _inside(o["x"], o["y"], polygon):
                nature.append(o["id"])
                continue
            a, b = geo.get(o["template"], [6, 6])
            _rect_cells(blocked, o["x"], o["y"], a + 6, b + 6, o["angle_deg"], B)
            existing += 1
    rp = [o for o in listed if o["kind"] == "road_point"]
    for a, b in zip(rp[0::2], rp[1::2]):
        _cells_along(blocked, (a["x"], a["y"]), (b["x"], b["y"]), widths.get(a["template"], 30) / 2 + 8, B)
    np.save(os.path.join(folder, "blocked.npy"), blocked)
    site = os.path.join(folder, "site.json")
    json.dump({"polygon": [[float(p[0]), float(p[1])] for p in polygon], "border": B, "x_extent": nx, "y_extent": ny,
               "blocked": os.path.join(folder, "blocked.npy")}, open(site, "w"))
    # --- plan ---
    cmd = _command(style, preset, seed, site, stamp, jitter, theta, irregularity, theme, lots, outline, grid_scale, major, contain, street_style)
    try:
        out = _run(cmd)
    except RuntimeError as exc:
        # how much of the area was usable says most about why
        xs_, ys_ = np.meshgrid((np.arange(nx - 1) - B) * 10.0 + 5, (np.arange(ny - 1) - B) * 10.0 + 5)
        cells = [(j, i) for j, i in zip(*np.nonzero((xs_ >= min(p[0] for p in polygon)) & (xs_ <= max(p[0] for p in polygon)) & (ys_ >= min(p[1] for p in polygon)) & (ys_ <= max(p[1] for p in polygon)))) if _inside(xs_[j, i], ys_[j, i], polygon)]
        share = sum(bool(blocked[j, i]) for j, i in cells) / max(len(cells), 1)
        raise RuntimeError(f"{exc} ({share:.0%} of the area is water, cliffs, steep ground, objects or roads; clear_nature=true removes trees and rocks)")
    plan = json.load(open(os.path.join(folder, "plan.json")))
    notes = [l[:200] for l in out.splitlines() if l.startswith(("no room", "lot ", "buildings", "houses"))]
    kinds = {}
    for r in plan["roads"]:
        kinds[r["type"]] = kinds.get(r["type"], 0) + 1
    xs = [p[0] for r in plan["roads"] for p in r["points"]] + [o["x"] for o in plan["objects"]]
    ys = [p[1] for r in plan["roads"] for p in r["points"]] + [o["y"] for o in plan["objects"]]
    box = [min(xs), min(ys), max(xs), max(ys)]
    result = {"plan": folder, "objects": len(plan["objects"]), "road_runs": kinds, "textures": plan["masks"], "bounds": box, "notes": notes,
              "blocked_terrain_cells": terrain_cells, "existing_objects_avoided": existing, "nature_in_area": len(nature)}
    if os.path.exists(os.path.join(folder, "preview.png")):
        result["preview"] = os.path.join(folder, "preview.png")
    connections = _plan_connections(plan.get("exits", []), _existing_roads(listed), widths, connect_range) if connect else []
    if connect:
        result["connections"] = connections
    if dry_run:
        result["dry_run"] = True
        return result
    # --- build ---
    if nature:
        wb.call("objects.delete", ids=nature)
        result["nature_removed"] = len(nature)
    if flatten:
        result["flatten"] = _flatten(wb, heights, hard, plan, geo, widths, B)
    if "teamPlyrCivilian" not in json.dumps(wb.call("sides.list")):
        wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
    errors = []
    for t in plan["masks"]:
        try:
            wb.call("terrain.import_mask", path=os.path.join(folder, t + ".png"), target="texture", texture=t, fit="exact")
        except Exception as exc:
            errors.append(f"texture {t}: {exc}")
    placed = 0
    for o in plan["objects"]:
        try:
            wb.call("objects.place", **o)
            placed += 1
        except Exception as exc:
            errors.append(f"{o['template']}: {exc}")
    for r in plan["roads"]:
        wb.call("roads.add", **r)
    if connections:
        built, failed = _build_connections(wb, connections, widths)
        result["connections"] = built
        errors.extend(failed)
    if reblend == "map":
        report = wb.call("terrain.reblend")
    elif reblend == "area":
        report = wb.call("terrain.reblend", shape="rect", x0=box[0] - 60, y0=box[1] - 60, x1=box[2] + 60, y1=box[3] + 60)
    else:
        report = {}
    result["blend"] = {k: report[k] for k in ("cells_blended", "three_way_tiles", "hard_edges") if k in report}
    result["placed"] = placed
    if errors:
        result["errors"] = errors[:20]
    result["saved"] = False
    result["undo"] = "Too many steps for edit_undo: reopen the map without saving to take this town back."
    return result
