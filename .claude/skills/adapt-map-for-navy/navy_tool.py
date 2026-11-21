"""Helpers for adapting a Zero Hour map for ships (see SKILL.md next to this file).

Standard library only. Subcommands:

  heights  Build the naval heightmap from terrain_get_heights dumps: lifts the land, carves a
           gradual seabed, writes heights16.png (16-bit, for terrain_import_heightmap fit=exact
           range=[0, 255*scale]) and sea.json (distances from shore, used by the other commands).
  mask     Water texture mask (8-bit, one pixel per cell) for terrain_import_mask: 0 = leave alone,
           64/128/192/255 = band 1..4 (shallow..deep), with noisy band borders.
  yards    Suggests shipyard waypoint positions per player start.
  check    Prints the distance from shore (cells) of world points; -1 = land.
  wak      Shifts the wave positions in a .wak file by the resize world offset.
  dx8water Writes the DX8 water transparency of ship maps (WaterTransparency block) into map.ini
           files: given ones, or every map.ini with EnableShips = Yes under a Maps folder.
"""
import argparse, json, math, os, random, re, shutil, struct, sys, zlib

HS = 0.625   # world units per raw height step


def load_dumps(paths):
    """terrain_get_heights results (one JSON per chunk of rows, in row order) -> rows."""
    rows = []
    for p in paths:
        d = json.load(open(p))
        if d.get("step", 1) != 1:
            sys.exit("%s: dump needs step=1" % p)
        rows += d["heights"]
    return rows


def png(path, rows, bitdepth):
    w, h = len(rows[0]), len(rows)
    raw = bytearray()
    for r in rows:
        raw.append(0)
        if bitdepth == 16:
            for v in r:
                raw += struct.pack(">H", v)
        else:
            raw += bytes(r)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bitdepth, 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def distance_field(H, land_raw):
    """Sea = vertices below land_raw connected to the map edge. Returns (sea, chamfer distance in cells)."""
    NY, NX = len(H), len(H[0])
    sea = [[False] * NX for _ in range(NY)]
    stack = [(x, y) for x in range(NX) for y in (0, NY - 1)] + [(x, y) for y in range(NY) for x in (0, NX - 1)]
    while stack:
        x, y = stack.pop()
        if sea[y][x] or H[y][x] >= land_raw:
            continue
        sea[y][x] = True
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < NX and 0 <= ny < NY and not sea[ny][nx]:
                stack.append((nx, ny))
    INF, R2 = 1e9, math.sqrt(2)
    D = [[(INF if sea[y][x] else 0.0) for x in range(NX)] for y in range(NY)]
    for y in range(NY):
        row = D[y]
        for x in range(NX):
            v = row[x]
            if v == 0: continue
            if x > 0: v = min(v, row[x - 1] + 1)
            if y > 0:
                up = D[y - 1]
                v = min(v, up[x] + 1)
                if x > 0: v = min(v, up[x - 1] + R2)
                if x < NX - 1: v = min(v, up[x + 1] + R2)
            row[x] = v
    for y in range(NY - 1, -1, -1):
        row = D[y]
        for x in range(NX - 1, -1, -1):
            v = row[x]
            if v == 0: continue
            if x < NX - 1: v = min(v, row[x + 1] + 1)
            if y < NY - 1:
                dn = D[y + 1]
                v = min(v, dn[x] + 1)
                if x < NX - 1: v = min(v, dn[x + 1] + R2)
                if x > 0: v = min(v, dn[x - 1] + R2)
            row[x] = v
    return sea, D


def cmd_heights(a):
    H = load_dumps(a.dumps)
    NY, NX = len(H), len(H[0])
    # Lift so the deepest seabed stays at or above `floor` world units; keep it on the scale grid.
    lift = max(0.0, (a.depth + a.floor - a.water) / HS)
    lift = int(math.ceil(lift / 2.0)) * 2
    W = a.water + lift * HS
    top = max(max(r) for r in H) + lift
    scale = max(1, int(math.ceil(top / 255.0)))
    limit = 255 * scale
    land_raw = int(math.floor(a.water / HS)) + 1
    sea, D = distance_field(H, land_raw)

    def snap(v):
        return max(0, min(limit, int(round(v / scale)) * scale))
    NEW = [[0] * NX for _ in range(NY)]
    for y in range(NY):
        for x in range(NX):
            if sea[y][x]:
                t = min(D[y][x] / a.ramp, 1.0)
                depth = max(a.depth * math.sin(t * math.pi / 2), a.water - H[y][x] * HS)
                NEW[y][x] = snap((W - depth) / HS)
            else:
                NEW[y][x] = snap(H[y][x] + lift)
    k = 65535.0 / limit
    png(os.path.join(a.out, "heights16.png"), [[int(round(v * k)) for v in NEW[y]] for y in range(NY - 1, -1, -1)], 16)
    json.dump({"border": a.border, "water": W, "D": [[round(v, 2) if v < 1e8 else -1 for v in r] for r in D]},
              open(os.path.join(a.out, "sea.json"), "w"))
    print(json.dumps({"lift_raw": lift, "lift_world": lift * HS, "new_water_height": W, "height_map_scale": scale,
                      "import_range": [0, limit], "raw_min": min(map(min, NEW)), "raw_max": max(map(max, NEW)),
                      "sea_vertices": sum(map(sum, sea)),
                      "lighting": {"Start": round(W - 2), "Height1": round(W - 17), "Height2": round(W - 37)}}))


def load_sea(out):
    return json.load(open(os.path.join(out, "sea.json")))


def cmd_mask(a):
    s = load_sea(a.out)
    D = s["D"]
    NY, NX = len(D), len(D[0])
    bands = [float(v) for v in a.bands.split(",")]
    rng = random.Random(a.seed)
    octaves = []
    for spec in a.noise.split(","):
        size, amp = (float(v) for v in spec.split(":"))
        g = [[rng.uniform(-1, 1) for _ in range(int(NX / size) + 3)] for _ in range(int(NY / size) + 3)]
        octaves.append((size, amp, g))

    def noise(x, y):
        v = 0.0
        for size, amp, g in octaves:
            fx, fy = x / size, y / size
            ix, iy = int(fx), int(fy)
            tx, ty = fx - ix, fy - iy
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            top = g[iy][ix] * (1 - tx) + g[iy][ix + 1] * tx
            bot = g[iy + 1][ix] * (1 - tx) + g[iy + 1][ix + 1] * tx
            v += amp * (top * (1 - ty) + bot * ty)
        return v
    mask, count = [], {}
    for cy in range(NY - 2, -1, -1):
        r = []
        for cx in range(NX - 1):
            d = min(D[cy][cx], D[cy][cx + 1], D[cy + 1][cx], D[cy + 1][cx + 1])
            val = 0
            if d >= a.skip:
                val = 64
                dn = d + noise(cx, cy)
                for lim, v in zip(bands, (128, 192, 255)):
                    if dn >= lim: val = v
            r.append(val)
            count[val] = count.get(val, 0) + 1
        mask.append(r)
    png(os.path.join(a.out, "watermask.png"), mask, 8)
    print(json.dumps({"cells_by_value": count}))


def cmd_yards(a):
    s = load_sea(a.out)
    D, b = s["D"], s["border"]
    NY, NX = len(D), len(D[0])
    H = load_dumps(a.dumps)   # original (unlifted) heights, to judge how steep the shore is
    starts = {}
    for spec in a.starts:
        n, x, y = spec.split(",")
        starts[int(n)] = (float(x), float(y))
    land_max = int(a.soft / HS)

    def soft(x, y):
        for j in range(max(0, y - 6), min(NY, y + 7)):
            for i in range(max(0, x - 6), min(NX, x + 7)):
                if D[j][i] == 0 and H[j][i] - a.water / HS > land_max:
                    return False
        return True
    cands = [((x - b) * 10, (y - b) * 10) for y in range(NY) for x in range(NX)
             if a.min_d <= D[y][x] <= a.max_d and soft(x, y)]
    out = {}
    for p, (sx, sy) in starts.items():
        picked = []
        for dist, cx, cy in sorted((math.hypot(cx - sx, cy - sy), cx, cy) for cx, cy in cands):
            if dist > a.radius: break
            if any(math.hypot(cx - ox, cy - oy) < dist for q, (ox, oy) in starts.items() if q != p): continue
            if all(math.hypot(cx - px, cy - py) >= a.spacing for px, py, _ in picked):
                picked.append((cx, cy, round(dist)))
            if len(picked) == a.count: break
        out[p] = picked
    print(json.dumps(out))


def cmd_check(a):
    s = load_sea(a.out)
    D, b = s["D"], s["border"]
    for spec in a.points:
        x, y = (float(v) for v in spec.split(","))
        i, j = int(round(x / 10)) + b, int(round(y / 10)) + b
        ok = 0 <= j < len(D) and 0 <= i < len(D[0])
        print("%s: %s" % (spec, D[j][i] if ok else "outside"))


def cmd_wak(a):
    # Layout (W3DWaterTracks.cpp saveTracks): per wave Vector2 start, Vector2 end (4 floats), Int type;
    # then Int count at the end. Positions are world units, like object positions.
    data = open(a.src, "rb").read()
    count = struct.unpack_from("<i", data, len(data) - 4)[0]
    if len(data) != count * 20 + 4:
        sys.exit("%s: unexpected size %d for %d waves" % (a.src, len(data), count))
    out = bytearray()
    for i in range(count):
        sx, sy, ex, ey, t = struct.unpack_from("<ffffi", data, i * 20)
        out += struct.pack("<ffffi", sx + a.dx, sy + a.dy, ex + a.dx, ey + a.dy, t)
    out += struct.pack("<i", count)
    open(a.dst or a.src, "wb").write(bytes(out))
    print(json.dumps({"waves": count, "dx": a.dx, "dy": a.dy, "written": a.dst or a.src}))


def ini_code(line):
    """A map.ini line without its comment and surrounding blanks."""
    return line.split(";", 1)[0].strip()


def ships_enabled(lines):
    return any(re.match(r"(?i)EnableShips\s*=\s*Yes\b", ini_code(l)) for l in lines)


def set_water_transparency(text, fields):
    """Sets fields (ordered (key, value) pairs) inside the top level WaterTransparency block of map.ini
    text, keeping other lines, inline comments and line endings; appends the block when there is none.
    Lines that already have the value stay as they are."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)
    start = next((i for i, l in enumerate(lines) if re.fullmatch(r"(?i)WaterTransparency", ini_code(l))), None)
    if start is None:
        while lines and not lines[-1].strip():
            lines.pop()
        if lines and not lines[-1].endswith(("\n", "\r")):
            lines[-1] += nl
        block = ["", "; DX8 Water Settings", "WaterTransparency"]
        block += ["  %s = %s" % kv for kv in fields]
        block += ["End"]
        return "".join(lines) + "".join(l + nl for l in block)
    end = next((i for i in range(start + 1, len(lines)) if re.fullmatch(r"(?i)End", ini_code(lines[i]))), None)
    if end is None:
        raise ValueError("WaterTransparency block without End")
    for key, value in fields:
        pattern = re.compile(r"(?i)%s\s*=\s*(.*)" % re.escape(key))
        found = False
        for i in range(start + 1, end):
            m = pattern.fullmatch(ini_code(lines[i]))
            if not m:
                continue
            found = True
            if m.group(1) == value:
                continue
            body = lines[i].rstrip("\r\n")
            ending = lines[i][len(body):]
            indent = re.match(r"\s*", body).group(0) or "  "
            comment = body[len(body.split(";", 1)[0]):]
            lines[i] = "%s%s = %s%s%s" % (indent, key, value, ("   " + comment) if comment else "", ending)
        if not found:
            ending = re.search(r"\r?\n?$", lines[start]).group(0) or nl
            lines.insert(end, "  %s = %s%s" % (key, value, ending))
            end += 1
    return "".join(lines)


def cmd_dx8water(a):
    files = list(a.files)
    if a.maps:
        for root, _dirs, names in os.walk(a.maps):
            for name in names:
                if name.lower() == "map.ini":
                    path = os.path.join(root, name)
                    with open(path, encoding="latin-1", newline="") as f:
                        if ships_enabled(f.read().splitlines()):
                            files.append(path)
    if not files:
        sys.exit("no map.ini given or found (pass files, or --maps <Maps folder>)")
    fields = [("TransparentWaterMinOpacity", a.min_opacity), ("TransparentWaterDepth", a.depth)]
    report = []
    for path in sorted(set(files)):
        with open(path, encoding="latin-1", newline="") as f:
            text = f.read()
        new = set_water_transparency(text, fields)
        state = "unchanged" if new == text else ("would change" if a.dry_run else "changed")
        if new != text and not a.dry_run:
            backup = path + ".bak-dx8water"
            if not os.path.exists(backup):
                shutil.copy2(path, backup)
            with open(path, "w", encoding="latin-1", newline="") as f:
                f.write(new)
        report.append({"file": path, "state": state})
    print(json.dumps(report, indent=1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("heights")
    p.add_argument("--out", required=True, help="work folder for heights16.png / sea.json")
    p.add_argument("--border", type=int, required=True, help="border cells (map_info heightmap.border)")
    p.add_argument("--water", type=float, required=True, help="current water height in world units")
    p.add_argument("--depth", type=float, default=60, help="water depth offshore, world units")
    p.add_argument("--floor", type=float, default=2, help="lowest seabed, world units")
    p.add_argument("--ramp", type=float, default=30, help="cells from shore to full depth")
    p.add_argument("dumps", nargs="+", help="terrain_get_heights results, bottom rows first")
    p.set_defaults(fn=cmd_heights)

    p = sub.add_parser("mask")
    p.add_argument("--out", required=True)
    p.add_argument("--skip", type=float, default=2, help="cells next to the shore left unpainted")
    p.add_argument("--bands", default="14,28,45", help="distances (cells) where bands 2, 3, 4 start")
    p.add_argument("--noise", default="28:6,10:2.5", help="size:amplitude (cells) per noise octave")
    p.add_argument("--seed", type=int, default=1234)
    p.set_defaults(fn=cmd_mask)

    p = sub.add_parser("yards")
    p.add_argument("--out", required=True)
    p.add_argument("--water", type=float, required=True, help="ORIGINAL water height (matches the dumps)")
    p.add_argument("--starts", nargs="+", required=True, help="N,x,y per Player_N_Start (after resize)")
    p.add_argument("--count", type=int, default=3)
    p.add_argument("--min-d", type=float, default=3.0, help="min cells from shore")
    p.add_argument("--max-d", type=float, default=4.0, help="max cells from shore")
    p.add_argument("--soft", type=float, default=15, help="max land height above water within 6 cells, world")
    p.add_argument("--radius", type=float, default=1300)
    p.add_argument("--spacing", type=float, default=300)
    p.add_argument("--dumps", nargs="+", required=True, help="the same dumps as for heights")
    p.set_defaults(fn=cmd_yards)

    p = sub.add_parser("check")
    p.add_argument("--out", required=True)
    p.add_argument("points", nargs="+", help="x,y world points")
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser("wak")
    p.add_argument("src")
    p.add_argument("--dx", type=float, required=True, help="map_resize world_offset.x")
    p.add_argument("--dy", type=float, required=True, help="map_resize world_offset.y")
    p.add_argument("--dst", help="output file (default: overwrite src)")
    p.set_defaults(fn=cmd_wak)

    p = sub.add_parser("dx8water")
    p.add_argument("files", nargs="*", help="map.ini files to write")
    p.add_argument("--maps", help="Maps folder: also every map.ini below it with EnableShips = Yes")
    p.add_argument("--min-opacity", default="0.5", help="TransparentWaterMinOpacity")
    p.add_argument("--depth", default="15.0", help="TransparentWaterDepth")
    p.add_argument("--dry-run", action="store_true", help="only report what would change")
    p.set_defaults(fn=cmd_dx8water)

    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
