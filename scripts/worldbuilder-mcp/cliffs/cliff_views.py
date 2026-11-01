# Views of generated maps at inner walls, looking uphill, one picture per recipe. Needs numpy and pillow and the second WorldBuilder. Writes work/gen_views.jpg.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

# Rough spots in world units; the nearest wall cell is taken.
SPOTS = {"twilight": [(780, 700), (2150, 2300)], "desert": [(600, 1200), (1500, 1800)], "overland": [(420, 900), (1000, 1500)], "manic": [(1500, 1900), (900, 1500)], "flash": [(1170, 700), (1800, 800)]}
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=280)
names = sys.argv[1:] or list(SPOTS)
tile = (640, 238)
sheet = Image.new("RGB", (tile[0] * 2, tile[1] * len(names)), "white")
for r, n in enumerate(names):
    d = os.path.join(HERE, "work", "gen_" + n)
    wb.call("map.open", path=os.path.join(d, f"gen_{n}.map"), discard_changes=True)
    b = json.load(open(os.path.join(d, "scan.json")))["info"]["heightmap"]["border"]
    z = np.load(os.path.join(d, "heights.npy")) * 0.625
    gy, gx = np.gradient(z, 10.0)
    ys, xs = np.nonzero(np.hypot(gx, gy) >= 0.75)
    for c, (sx, sy) in enumerate(SPOTS[n]):
        i = np.argmin(np.hypot((xs - b) * 10.0 - sx, (ys - b) * 10.0 - sy))
        ix, iy = xs[i], ys[i]
        a = math.degrees(math.atan2(gy[iy - 8:iy + 9, ix - 8:ix + 9].sum(), gx[iy - 8:iy + 9, ix - 8:ix + 9].sum()))
        wx, wy = (ix - b) * 10.0, (iy - b) * 10.0
        dist = 260
        v = wb.call("view.screenshot", x=wx - math.cos(math.radians(a)) * dist * .55, y=wy - math.sin(math.radians(a)) * dist * .55, zoom=-dist * 1.5, pitch=1.0, angle_deg=a, max_width=900, format="jpg", quality=85)
        im = Image.open(v["path"]).convert("RGB").resize(tile, Image.LANCZOS)
        ImageDraw.Draw(im).text((5, 3), f"gen {n} {c}", fill="yellow")
        sheet.paste(im, (c * tile[0], r * tile[1]))
sheet.save(os.path.join(HERE, "work", "gen_views.jpg"), quality=86)
print("ok")
