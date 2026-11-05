# Imports the masks of paint_map.py or paint_coast.py into a map through the second WorldBuilder (town/start_wb.py), largest first, then
# reblends, maps the cliffs and saves. Reopen the map in the main WorldBuilder afterwards (map_open with discard_changes).
# Usage: import_masks.py <map path> <masks dir>
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

path, folder = sys.argv[1], sys.argv[2]
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=path, discard_changes=True)
for m in json.load(open(os.path.join(folder, "masks.json")))["masks"]:
    wb.call("terrain.import_mask", path=m["path"], target="texture", texture=m["texture"], fit="exact")
blend = wb.call("terrain.reblend")
cliffs = wb.call("terrain.cliff_map", repair="fit")
wb.call("map.save", path=path)
print({k: blend.get(k) for k in ("three_way_tiles", "hard_edges", "blend_table_used")},
      {k: cliffs.get(k) for k in ("cliff_cells", "cliff_cells_unmapped", "regions")}, "saved", path)
