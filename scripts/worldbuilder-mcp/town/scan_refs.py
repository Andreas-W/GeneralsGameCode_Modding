# Scans the known reference maps (references.json) with scan_ref.py and city_scan.py. Usage: scan_refs.py [names]
import json, os, subprocess, sys
from town_paths import GAME_DIR, HERE, REF_MAPS  # enters the work folder

refs = json.load(open(os.path.join(HERE, "references.json")))
for name in sys.argv[1:] or refs:
    r = refs[name]
    base = GAME_DIR if r["root"] == "game" else REF_MAPS
    if not base:
        sys.exit(f"{name} needs TOWN_REF_MAPS: the folder holding Maps and maps_gens")
    path = os.path.join(base, r["path"])
    out = subprocess.run([sys.executable, os.path.join(HERE, "scan_ref.py"), name, path], capture_output=True, text=True)
    print((out.stdout + out.stderr[-400:])[:900], flush=True)
    if os.path.exists(f"refs/{name}/dump.json"):
        out = subprocess.run([sys.executable, os.path.join(HERE, "city_scan.py"), name, f"refs/{name}/dump.json"], capture_output=True, text=True)
        print((out.stdout + out.stderr[-300:])[:1600], flush=True)
