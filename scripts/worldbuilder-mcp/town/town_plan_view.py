# Plan picture of a town area: textures, roads by type, buildings with their fronts, props.
import json, math, os, re, glob, sys
import numpy as np
from PIL import Image, ImageDraw
import town_paths  # enters the work folder
X0, Y0, X1, Y1 = 880, 780, 1720, 1680
SC = 1.05
B = 70
O = json.load(open("refs/alpine/aa_objects.json"))
geo = json.load(open("gamedata/geo.json"))
PX = lambda x, y: ((x - X0) * SC, (Y1 - y) * SC)
W, H = int((X1 - X0) * SC), int((Y1 - Y0) * SC)
im = Image.new("RGB", (W, H), (70, 100, 60))
px = im.load()
for name, col in (("GrassType1", (100, 130, 70)), ("GrassMediumType10", (120, 110, 60)), ("RocksType3", (130, 125, 115)), ("CobblestoneType1", (175, 150, 125)), ("AsphaltType1", (60, 60, 65))):
    m = (np.array(Image.open(f"refs/alpine/aa_{name}.png")) > 0)[::-1]
    for j, i in zip(*np.nonzero(m)):
        x, y = (i - B) * 10, (j - B) * 10
        if X0 <= x < X1 and Y0 <= y < Y1:
            a, b = PX(x, y + 10)
            ImageDraw.Draw(im).rectangle((a, b, a + 10 * SC, b + 10 * SC), fill=col)
dr = ImageDraw.Draw(im)
RC = {"TwoLane": (30, 30, 30), "Sidewalk": (240, 240, 240), "DirtRoad": (150, 90, 30), "DirtRoad2": (190, 120, 40), "GravelRoad": (150, 150, 150), "ParkingLines": (255, 255, 0), "TrainTrack": (120, 0, 120)}
RW = {"TwoLane": 40, "Sidewalk": 10, "DirtRoad": 30, "DirtRoad2": 30, "GravelRoad": 20, "ParkingLines": 16, "TrainTrack": 10}
rp = [o for o in O if o["kind"] == "road_point"]
segs = []
for a, b in zip(rp[0::2], rp[1::2]):
    segs.append((a["template"], a["x"], a["y"], b["x"], b["y"]))
json.dump(segs, open("refs/alpine/aa_segs.json", "w"))
for t, ax, ay, bx, by in segs:
    if X0 - 100 < ax < X1 + 100 and Y0 - 100 < ay < Y1 + 100:
        dr.line([PX(ax, ay), PX(bx, by)], fill=RC.get(t, (255, 0, 0)), width=max(2, int(RW.get(t, 12) * SC)))
for o in O:
    if o["kind"] != "object" or not (X0 < o["x"] < X1 and Y0 < o["y"] < Y1) or o["template"].startswith("Amb"):
        continue
    t, x, y = o["template"], o["x"], o["y"]
    r = math.radians(o["angle_deg"])
    a, b = geo.get(t, [5, 5])
    if max(a, b) >= 12 and not t.startswith(("Tree", "Hedge")):
        pts = [(x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r)) for u, v in ((-a, -b), (a, -b), (a, b), (-a, b))]
        fill = (200, 80, 70) if t.startswith(("German", "Stan")) else (90, 130, 200)
        if "Bib" in t:
            dr.polygon([PX(*p) for p in pts], outline=(255, 200, 200))
        else:
            dr.polygon([PX(*p) for p in pts], fill=fill)
            fx, fy = x + (b + 12) * math.sin(r), y - (b + 12) * math.cos(r)
            dr.line([PX(x, y), PX(fx, fy)], fill=(255, 255, 255), width=2)
            dr.text(PX(x - 12, y + 6), t.replace("German", "")[:8], fill=(0, 0, 0))
    else:
        c = PX(x, y)
        col = {"StreetLamp": (255, 255, 0), "ParkBench": (0, 255, 255), "BrickWall01": (255, 0, 255), "BrickWall04": (255, 0, 255), "Hedge1": (0, 90, 0)}.get(t, (255, 140, 0))
        if t.startswith("Tree"):
            dr.ellipse((c[0] - 4, c[1] - 4, c[0] + 4, c[1] + 4), outline=(0, 60, 0))
        else:
            dr.ellipse((c[0] - 3, c[1] - 3, c[0] + 3, c[1] + 3), fill=col)
            if t not in ("StreetLamp",):
                dr.text((c[0] + 4, c[1] - 5), t[:9], fill=(255, 255, 255))
im.save("refs/alpine/aa_plan.png")
import collections
near = [s for s in segs if X0 < s[1] < X1 and Y0 < s[2] < Y1]
print(collections.Counter(s[0] for s in near))
