# Square corners of fence types, placed as the Fence tool does, to see where each model sits relative to its position.
import os, shutil, sys
from town_paths import WB_PORT  # enters the work folder
from wb_bridge import WorldBuilderBridge
here = os.getcwd()
wb = WorldBuilderBridge(port=WB_PORT)
wb.call("map.new", width=240, height=240, border=30, initial_height=90, discard_changes=True)
TYPES = {"ChainLinkFence01": 27.35, "PicketFence01": 32.75, "BrickWall01": 30, "WoodWall01": 29}
for k, (t, w) in enumerate(TYPES.items()):
    x0, y0 = 500 + k * 260, 1000
    n = 5
    # along +x from the corner, then along +y from the corner, then back along -x and -y: all four directions
    for i in range(n):
        wb.call("objects.place", template=t, x=x0 + i * w, y=y0, angle_deg=0)
        wb.call("objects.place", template=t, x=x0, y=y0 + i * w, angle_deg=90)
        wb.call("objects.place", template=t, x=x0 + n * w - i * w, y=y0 + n * w, angle_deg=180)
        wb.call("objects.place", template=t, x=x0 + n * w, y=y0 + n * w - i * w, angle_deg=270)
    wb.call("waypoints.add", name=f"C{k}", x=x0, y=y0)
    r = wb.call("view.screenshot", x=x0 + n * w / 2, y=y0 + n * w / 2, zoom=-420, pitch=2.5, angle_deg=0, max_width=1000, format="jpg")
    shutil.copy(r["path"], os.path.join(here, f"fence_{t}.jpg"))
