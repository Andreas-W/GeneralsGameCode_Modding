# Reads the mod's object INIs into work/gamedata: footprint radii (geo.json) and sorting, side, KindOf, geometry (objs.json).
import glob, json, os, re
from town_paths import GAME_DIR  # enters the work folder

INI = os.path.join(GAME_DIR, "Data", "INI", "Object")
geo = {}
objs = {}
# not "Object = ..." lines inside modules
pat = re.compile(r"^\s*Object\s+([^=\s]\S*)")
for fn in glob.glob(INI + r"\**\*.ini", recursive=True):
    cur = None
    obj = None
    for line in open(fn, encoding="latin-1", errors="replace"):
        s = line.split(";")[0]
        m = pat.match(s)
        if m:
            cur = m.group(1)
            obj = objs.setdefault(cur, {"file": os.path.basename(fn)})
            continue
        if not cur:
            continue
        m = re.match(r"\s*Geometry(Major|Minor)Radius\s*=\s*([\d.]+)", s)
        if m:
            e = geo.setdefault(cur, [0, 0])
            # INI values can carry stray letters ("40v.0"): take the leading number
            e[0 if m.group(1) == "Major" else 1] = float(re.match(r"\d+(?:\.\d+)?", m.group(2)).group(0))
        m = re.match(r"\s*(EditorSorting|Side|Geometry|GeometryMajorRadius|GeometryMinorRadius|GeometryHeight)\s*=\s*(\S+)", s)
        if m:
            obj.setdefault(m.group(1), m.group(2))
        if re.match(r"\s*Behavior\s*=\s*GarrisonContain", s):
            obj["garrison"] = True
        if re.match(r"\s*KindOf\s*=", s):
            obj["kindof"] = s.split("=", 1)[1].split()
for v in geo.values():
    if v[1] == 0:
        v[1] = v[0]
json.dump(geo, open("gamedata/geo.json", "w"))
json.dump(objs, open("gamedata/objs.json", "w"))
print("objects", len(objs), "with footprint", len(geo))
