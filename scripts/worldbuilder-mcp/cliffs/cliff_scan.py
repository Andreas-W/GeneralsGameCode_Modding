# Reads reference maps through the second WorldBuilder (town/start_wb.py): heights, passability, water, cliff mapping, and views of the map and its walls.
# Usage: cliff_scan.py [names]   Needs numpy and scipy. Results in work/<name>; cliff_measure.py works from them without WorldBuilder.
import ctypes, json, math, os, shutil, sys, threading, time
import numpy as np
from scipy import ndimage
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

REF_MAPS = os.environ.get("TOWN_REF_MAPS", "")
PORT = int(os.environ.get("TOWN_WB_PORT", 47810))
stop = threading.Event()


def dismiss():
    # maps made for other game data ask about each missing unit; this only reads the map, so cancel those
    u = ctypes.windll.user32
    u.FindWindowW.restype = ctypes.c_void_p
    u.FindWindowExW.restype = ctypes.c_void_p
    u.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    u.FindWindowExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p]
    u.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p]
    while not stop.is_set():
        w = u.FindWindowW(None, "Replace Missing Unit")
        if w:
            b = u.FindWindowExW(w, None, "Button", "Cancel")
            if b:
                u.PostMessageW(b, 0x00F5, None, None)
        time.sleep(0.2)


def heights(wb, width, height):
    rows = []
    while len(rows) < height:
        h = min(height - len(rows), max(1, 60000 // width))
        rows += wb.call("terrain.get_heights", x0=0, y0=len(rows), w=width, h=h)["heights"]
    return np.array(rows, dtype=np.float32)


def scan(wb, name, ref):
    info = wb.call("map.open", path=os.path.join(REF_MAPS, ref["path"]), discard_changes=True)
    scan_open(wb, name, info, ref["path"])


def scan_open(wb, name, info, source):
    d = os.path.join(HERE, "work", name)
    os.makedirs(d, exist_ok=True)
    hm = info["heightmap"]
    z = heights(wb, hm["x_extent"], hm["y_extent"])
    np.save(os.path.join(d, "heights.npy"), z)
    for kind in ("passability", "water"):
        wb.call("terrain.export_mask", kind=kind, path=os.path.join(d, kind + ".png"), include_border=True)
    report = wb.call("terrain.cliff_report")
    water = [p for p in wb.call("polygons.list").get("polygons", []) if p.get("water") or p.get("kind") == "water"]
    json.dump({"info": info, "cliff_report": report, "water": water, "paths": source}, open(os.path.join(d, "scan.json"), "w"), indent=1)

    border = hm["border"]
    max_x, max_y = info["world"]["playable_max_x"], info["world"]["playable_max_y"]
    v = wb.call("view.screenshot", x=max_x / 2, y=max_y / 2, zoom=-max(max_x, max_y) * 1.35, pitch=2.5, angle_deg=90, max_width=900, format="jpg", quality=82)
    shutil.copy(v["path"], os.path.join(d, "overview.jpg"))
    # The densest two wall areas, seen looking uphill so that the wall faces the camera.
    world = z * 0.625
    gy, gx = np.gradient(world, 10.0)
    steep = np.hypot(gx, gy) > 0.8
    density = ndimage.uniform_filter(steep.astype(np.float32), 25)
    for k in range(2):
        y, x = np.unravel_index(np.argmax(density), density.shape)
        if density[y, x] <= 0:
            break
        m = np.zeros_like(density, dtype=bool)
        m[max(0, y - 12):y + 13, max(0, x - 12):x + 13] = True
        angle = math.degrees(math.atan2(gy[m & steep].sum(), gx[m & steep].sum()))
        wx, wy = (x - border) * 10.0, (y - border) * 10.0
        dist = 240
        v = wb.call("view.screenshot", x=wx - math.cos(math.radians(angle)) * dist * .6, y=wy - math.sin(math.radians(angle)) * dist * .6, zoom=-dist * 1.7, pitch=1.0, angle_deg=angle, max_width=900, format="jpg", quality=82)
        shutil.copy(v["path"], os.path.join(d, f"wall{k}.jpg"))
        density[max(0, y - 45):y + 46, max(0, x - 45):x + 46] = 0
    print(name, "size", max_x, max_y, "scale", hm.get("height_map_scale"), {k: report[k] for k in ("cliff_cells", "cells_mapped", "outside_v", "mutants")}, flush=True)


if __name__ == "__main__":
    refs = json.load(open(os.path.join(HERE, "references.json")))
    if not REF_MAPS:
        sys.exit("TOWN_REF_MAPS is not set: the folder holding Maps and maps_gens")
    th = threading.Thread(target=dismiss)
    th.start()
    try:
        wb = WorldBuilderBridge(port=PORT, timeout=280)
        for name in sys.argv[1:] or refs:
            try:
                scan(wb, name, refs[name])
            except Exception as e:
                print(name, "FAILED", e, flush=True)
    finally:
        stop.set()
        th.join()
