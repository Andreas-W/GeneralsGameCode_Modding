# Ground of a reference city: texture share by distance from the street edge and from buildings. refs/<name>/tex_*.png
import glob, json, math, os, re, sys, collections
import numpy as np
from PIL import Image
from town_paths import ROADS_INI  # enters the work folder
name, B = sys.argv[1], int(sys.argv[2])
boxes = [tuple(float(v) for v in a.split(",")) for a in sys.argv[3:]]
tex = {os.path.basename(f)[4:-4]: (np.array(Image.open(f)) > 0)[::-1] for f in glob.glob(f"refs/{name}/tex_*.png")}
H, W = next(iter(tex.values())).shape
XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(H) - B) * 10.0 + 5)
names = list(tex)
label = np.full((H, W), -1)
for k, n in enumerate(names):
    label[tex[n]] = k
ROADS = {}
for line in open(ROADS_INI, encoding="latin-1"):
    p = line.split()
    if len(p) == 2 and p[0] == "Road":
        cur = p[1]
    if len(p) == 3 and p[0] == "RoadWidth":
        ROADS[cur] = float(p[2])
segs = json.load(open(f"refs/{name}/segs.json"))
inbox = np.zeros((H, W), bool)
for b in boxes:
    inbox |= (XC >= b[0]) & (XC <= b[2]) & (YC >= b[1]) & (YC <= b[3])
e = np.full((H, W), 1e9)
for s in segs:
    if "Lane" in s[0] or s[0] == "Cobblestone":
        ax, ay, bx, by = s[1:]
        dx, dy = bx - ax, by - ay
        t = np.clip(((XC - ax) * dx + (YC - ay) * dy) / ((dx * dx + dy * dy) or 1), 0, 1)
        e = np.minimum(e, np.hypot(XC - ax - t * dx, YC - ay - t * dy) - ROADS[s[0]] / 2)
print("==", name, "city cells", int(inbox.sum()))
for lo, hi in ((-99, 0), (0, 10), (10, 20), (20, 40), (40, 80), (80, 999)):
    m = inbox & (e >= lo) & (e < hi)
    c = collections.Counter(label[m].tolist())
    tot = sum(c.values()) or 1
    print(f"  edge {lo:4d}..{hi:4d} cells {tot:5d}", {(names[k] if k >= 0 else "other"): round(v / tot, 2) for k, v in c.most_common(5)})
