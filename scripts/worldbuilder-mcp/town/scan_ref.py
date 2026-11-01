# Reads a reference map through the second WorldBuilder (start_wb.py): objects, roads, textures.
# Usage: scan_ref.py <name> <map path>   (scan_refs.py does this for the known references)
import ctypes, json, os, sys, threading, time, re
from town_paths import WB_PORT  # enters the work folder
from wb_bridge import WorldBuilderBridge
name, path = sys.argv[1], sys.argv[2]
d = os.path.abspath(os.path.join("refs", name))
os.makedirs(d, exist_ok=True)
stop = threading.Event()
def dismiss():
    # maps made for other game data ask about each missing unit; this only reads the map, so cancel those
    u = ctypes.windll.user32
    u.FindWindowW.restype = ctypes.c_void_p
    u.FindWindowExW.restype = ctypes.c_void_p
    u.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    u.FindWindowExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p]
    u.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p]
    n = 0
    while not stop.is_set():
        w = u.FindWindowW(None, "Replace Missing Unit")
        if w:
            b = u.FindWindowExW(w, None, "Button", "Cancel")
            if b:
                u.PostMessageW(b, 0x00F5, None, None)
                n += 1
        time.sleep(0.2)
    print("missing unit dialogs cancelled", n)
th = threading.Thread(target=dismiss)
th.start()
try:
    wb = WorldBuilderBridge(port=WB_PORT, timeout=280)
    info = wb.call("map.open", path=path, discard_changes=True)
    usage = wb.call("map.usage")
    objs = wb.call("objects.list", include_waypoints_and_roads=True, limit=12000)
    json.dump(objs, open(os.path.join(d, "dump.json"), "w"))
    json.dump(usage, open(os.path.join(d, "usage.json"), "w"))
    json.dump(info, open(os.path.join(d, "info.json"), "w"))
    tex = [t for t in usage["textures"] if not t["name"].startswith("Cliff")]
    for t in tex:
        wb.call("terrain.export_mask", kind="texture", texture=t["name"], path=os.path.join(d, f"tex_{t['name']}.png"))
    print(name, "border", info["heightmap"]["border"], "size", info["world"]["playable_max_x"], info["world"]["playable_max_y"], "time", info["time_of_day"], "objects", info["counts"]["objects"], "road points", info["counts"]["road_points"])
    print(" roads", [(r["type"], r["points"]) for r in usage["roads"]])
    print(" textures", [(t["name"], t["cells"]) for t in sorted(tex, key=lambda t: -t["cells"])][:14])
finally:
    stop.set()
    th.join()
