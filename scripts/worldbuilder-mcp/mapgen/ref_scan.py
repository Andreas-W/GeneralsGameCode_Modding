# Texturing step 1 (DESIGN.md "Texturing: the plan"): per-cell data of reference maps, through the second WorldBuilder (town/start_wb.py).
# Per map in work/refs/<name>/: heights.npy (world units, vertices), textures.npy (base texture per cell as an index into info.json's
# "textures"; -1 none), water.png and passability.png (cells, top row north), objects.json (trees, roads, bridges, buildings), info.json.
# Usage: ref_scan.py <maps root> [names...]   Without names: work/mp_maps.json (the multiplayer maps of MapCache.ini). Skips done maps.
import ctypes, json, os, sys, threading, time
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "cliffs"))
from wb_bridge import WorldBuilderBridge
import cliff_scan

WORK = os.path.join(HERE, "work", "refs")
stop = threading.Event()
seen_dialogs = set()


def dismiss():
    # Maps made for other game data ask about missing units and textures. This only reads, so each such dialog is cancelled (or
    # confirmed when it has no Cancel); its title is logged once.
    u = ctypes.windll.user32
    EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    u.FindWindowExW.restype = ctypes.c_void_p
    u.FindWindowExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p]
    u.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p]
    u.GetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
    u.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
    u.IsWindowVisible.argtypes = [ctypes.c_void_p]

    def cb(h, _):
        cls = ctypes.create_unicode_buffer(64)
        u.GetClassNameW(h, cls, 64)
        if cls.value != "#32770" or not u.IsWindowVisible(h):
            return True
        title = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, title, 256)
        t = title.value
        if "Replace" in t or "Missing" in t or "Texture" in t:
            if t not in seen_dialogs:
                seen_dialogs.add(t)
                print("  dialog:", t, flush=True)
            for label in ("Cancel", "&Cancel", "OK", "&OK"):
                b = u.FindWindowExW(h, None, "Button", label)
                if b:
                    u.PostMessageW(b, 0x00F5, None, None)
                    break
        return True

    proc = EnumProc(cb)
    while not stop.is_set():
        u.EnumWindows(proc, 0)
        time.sleep(0.2)


KEEP = ("Tree", "Bush", "Shrub", "Rock", "Boulder")


def scan(wb, root, name):
    out = os.path.join(WORK, name)
    if os.path.exists(os.path.join(out, "info.json")):
        return "done before"
    os.makedirs(out, exist_ok=True)
    # A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
    wb.call("map.new", width=32, height=32, discard_changes=True)
    info = wb.call("map.open", path=os.path.join(root, name, name + ".map"), discard_changes=True)
    hm = info["heightmap"]
    b = hm["border"]
    z = cliff_scan.heights(wb, hm["x_extent"], hm["y_extent"]) * 0.625
    np.save(os.path.join(out, "heights.npy"), z.astype(np.float32))
    used = [t["name"] for t in wb.call("terrain.list_textures")["textures"] if t.get("used_in_map")]
    tex = np.full((z.shape[0] - 1, z.shape[1] - 1), -1, np.int16)
    for i, t in enumerate(used):
        p = os.path.join(out, "_tex.png")
        wb.call("terrain.export_mask", kind="texture", texture=t, path=p, include_border=True)
        m = np.array(Image.open(p).convert("L"))[::-1] > 127
        tex[:m.shape[0], :m.shape[1]][m] = i
    os.remove(os.path.join(out, "_tex.png"))
    np.save(os.path.join(out, "textures.npy"), tex)
    for kind in ("water", "passability"):
        wb.call("terrain.export_mask", kind=kind, path=os.path.join(out, kind + ".png"), include_border=True)
    water = [p.get("water_height") for p in wb.call("polygons.list").get("polygons", []) if p.get("kind") == "water"]
    objs = wb.call("objects.list", include_waypoints_and_roads=True, limit=50000)["objects"]
    keep = [{k: o[k] for k in ("template", "x", "y", "angle_deg", "owner", "kind") if k in o} for o in objs
            if o["kind"] in ("road_point", "bridge_point", "waypoint") or o["template"].startswith(KEEP) or "civilian" in o.get("owner", "").lower()]
    json.dump(keep, open(os.path.join(out, "objects.json"), "w"), separators=(",", ":"))
    json.dump({"name": name, "border": b, "height_map_scale": hm.get("height_map_scale", 1), "world": info["world"], "textures": used,
               "water": water, "time_of_day": info.get("time_of_day"), "weather": info.get("weather"),
               "cells_without_texture": int((tex[b:-b, b:-b] < 0).sum())}, open(os.path.join(out, "info.json"), "w"), indent=1)
    return f"{len(used)} textures, {len(keep)} objects, {int((tex[b:-b, b:-b] < 0).sum())} cells without a texture"


def multiplayer_maps(root):
    # The official multiplayer maps: isMultiplayer in Maps\MapCache.ini of MapsZH.big (the archived one, not the mod's loose cache),
    # as folder names under root.
    import re, bigfs
    fs = bigfs.BigFS()
    for archive, off, size in [fs.index[r"maps\mapcache.ini"]]:
        with open(archive, "rb") as f:
            f.seek(off)
            text = f.read(size).decode("latin-1")
    names, cur = [], None
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"MapCache\s+(.+)", s)
        if m:
            cur = {"path": re.sub(r"_([0-9A-F]{2})", lambda g: chr(int(g.group(1), 16)), m.group(1).strip())}
        elif s.upper() == "END" and cur:
            if cur.get("isMultiplayer", "").lower() == "yes":
                folder = cur["path"].split("\\")[1]
                hit = [d for d in os.listdir(root) if d.lower() == folder.lower()]
                names.append(hit[0] if hit else folder)
            cur = None
        elif cur is not None and "=" in s:
            k, v = s.split("=", 1)
            cur[k.strip()] = v.strip()
    return names


def main():
    root = sys.argv[1]
    listed = os.path.join(HERE, "work", "mp_maps.json")
    if not sys.argv[2:] and not os.path.exists(listed):
        os.makedirs(os.path.dirname(listed), exist_ok=True)
        json.dump(multiplayer_maps(root), open(listed, "w"), indent=0)
    names = sys.argv[2:] or json.load(open(listed))
    os.makedirs(WORK, exist_ok=True)
    th = threading.Thread(target=dismiss)
    th.start()
    try:
        wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=300)
        for name in names:
            t0 = time.time()
            try:
                print(name, scan(wb, root, name), f"{time.time() - t0:.0f}s", flush=True)
            except Exception as e:
                print(name, "FAILED", str(e).splitlines()[0], flush=True)
    finally:
        stop.set()
        th.join()


if __name__ == "__main__":
    main()
