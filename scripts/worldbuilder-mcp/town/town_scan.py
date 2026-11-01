# First look at a reference town: where it is, its buildings, streets and props, as a compact table and a plan picture.
import json, math, os, sys, re, collections
from PIL import Image, ImageDraw
import town_paths  # enters the work folder
src = sys.argv[1]
raw = open(src, encoding="utf-8", errors="replace").read()
data = json.loads(raw)
if isinstance(data, list):           # {type:text,text:...} wrapper
    data = json.loads(data[0]["text"])
elif "result" in data:
    data = json.loads(data["result"])
O = data["objects"]
json.dump(O, open("refs/alpine/aa_objects.json", "w"))
print("total", len(O), collections.Counter(o["kind"] for o in O))
BLD = re.compile(r"^(German|Stan|Chalet|FarmHouse|Barn|Mogadishu|Euro|Asia|Civilian(High)|Soviet)")
bld = [o for o in O if o["kind"] == "object" and BLD.match(o["template"]) and "Bib" not in o["template"]]
for o in sorted(bld, key=lambda o: (round(o["y"] / 200), o["x"])):
    print(f'{o["template"]:20s} {o["x"]:7.0f} {o["y"]:7.0f} {o["angle_deg"]:7.1f} {o["owner"]}')
xs, ys = [o["x"] for o in bld], [o["y"] for o in bld]
print("bbox", min(xs), min(ys), max(xs), max(ys))
