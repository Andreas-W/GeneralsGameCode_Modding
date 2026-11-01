# Tries the outpost tools on a real map through the second WorldBuilder (start_wb.py). Nothing is saved unless save=<name>.
# Usage: outpost_test.py <map> <theme> [count] [key=value ...]
import json, os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from wb_bridge import WorldBuilderBridge
import outpost_tool
path, theme = sys.argv[1], sys.argv[2]
rest = sys.argv[3:]
count = int(rest.pop(0)) if rest and "=" not in rest[0] else 4
kw = dict(a.split("=", 1) for a in rest)
save = kw.pop("save", None)
for k in ("decorate_tech", "clear_nature", "dry_run"):
    if k in kw:
        kw[k] = kw[k] == "1"
if "seed" in kw:
    kw["seed"] = int(kw["seed"])
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
wb.call("map.open", path=path, discard_changes=True)
r = outpost_tool.populate(wb, count, theme, **kw)
print(json.dumps({k: v for k, v in r.items() if k != "placed"})[:600])
out = os.path.join(outpost_tool.WORK, "plans", "outpost_views")
os.makedirs(out, exist_ok=True)
for i, p in enumerate(r["placed"]):
    print(" ", p)
    if i < 5 and not kw.get("dry_run"):
        v = wb.call("view.screenshot", x=p["x"], y=p["y"], zoom=-520, pitch=2.5, angle_deg=0, max_width=800, format="jpg", quality=80)
        shutil.copy(v["path"], os.path.join(out, f"{theme}_{i}.jpg"))
if save:
    d = os.path.join(os.path.expanduser("~"), "Documents", "Command and Conquer Generals Zero Hour Data", "Maps", save)
    os.makedirs(d, exist_ok=True)
    print("saved", wb.call("map.save", path=os.path.join(d, save + ".map"), overwrite=True)["path"])
