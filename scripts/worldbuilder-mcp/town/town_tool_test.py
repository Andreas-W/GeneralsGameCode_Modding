# Tries town_tool.py on real maps through the second WorldBuilder (start_wb.py). Nothing is saved. Usage: town_tool_test.py <map> <x0,y0,x1,y1> <style> [preset] [key=value ...]
import json, os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from wb_bridge import WorldBuilderBridge
import town_tool

path, box, style = sys.argv[1], [float(v) for v in sys.argv[2].split(",")], sys.argv[3]
rest = sys.argv[4:]
preset = rest.pop(0) if rest and "=" not in rest[0] else None
kw = dict(a.split("=", 1) for a in rest)
save = kw.pop("save", None)
for k in ("seed",):
    if k in kw:
        kw[k] = int(kw[k])
for k in ("jitter", "theta", "irregularity", "max_slope"):
    if k in kw:
        kw[k] = float(kw[k])
for k in ("dry_run", "flatten", "outline", "clear_nature"):
    if k in kw:
        kw[k] = kw[k] == "1"
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
wb.call("map.open", path=path, discard_changes=True)
x0, y0, x1, y1 = box
r = town_tool.generate(wb, [[x0, y0], [x1, y0], [x1, y1], [x0, y1]], style=style, preset=preset, **kw)
print(json.dumps({k: v for k, v in r.items() if k != "notes"})[:900])
for n in r["notes"]:
    print(" ", n[:170])
if not kw.get("dry_run"):
    for k, (x, y, z) in enumerate((((x0 + x1) / 2, (y0 + y1) / 2, -2800), ((x0 + x1) / 2, (y0 + y1) / 2, -1000))):
        v = wb.call("view.screenshot", x=x, y=y, zoom=z, pitch=2.5, angle_deg=0, max_width=1000, format="jpg", quality=85)
        shutil.copy(v["path"], os.path.join(r["plan"], f"view{k}.jpg"))
    print("views in", r["plan"])
if save:
    d = os.path.join(os.path.expanduser("~"), "Documents", "Command and Conquer Generals Zero Hour Data", "Maps", save)
    os.makedirs(d, exist_ok=True)
    print("saved", wb.call("map.save", path=os.path.join(d, save + ".map"), overwrite=True)["path"])
