# Generates the sample towns and builds them on the second WorldBuilder (start_wb.py), with close-ups of their special lots.
# Usage: build_samples.py [map names]; the maps go to the user maps folder. Same seeds, same towns.
import json, os, subprocess, sys
from town_paths import HERE  # enters the work folder

U, O, A = "town_urban.py", "town_organic.py", "town_plan.py"
RUNS = [("urban_generic", "TownUrban", [U, "urban_generic", "5", "out=urban_generic"]),
        ("urban_grid", "TownUrbanGrid", [U, "urban_generic", "5", "jitter=0", "theta=0", "out=urban_grid"]),
        ("urban_highrise", "TownHighrise", [U, "urban_generic", "5", "theme=highrise", "lots=dock:1,site:2,hospital:1,repair:1,derrick:1", "out=urban_highrise"]),
        ("urban_asian", "TownAsianGrid", [U, "urban_generic", "9", "jitter=0", "theta=0", "theme=asian", "lots=dock:1,site:1,pad:1,radar:1", "out=urban_asian"]),
        ("urban_american", "TownAmerican", [U, "urban_generic", "12", "theme=american", "lots=site:1,derrick:1,refinery:1", "out=urban_american"]),
        ("urban_eastern", "TownEastern", [U, "urban_generic", "3", "jitter=0.5", "theta=20", "theme=eastern", "lots=dock:1,site:1,hospital:1", "out=urban_eastern"]),
        ("plan", "TownAlpineLots", [A, "33", "no-outline", "lots=site:1,dock:1,derrick:1"]),
        ("arab_generic", "ArabicGeneric", [O, "arabic", "generic", "4", "lots=site:1,dock:1,derrick:1", "out=arab_generic"]),
        ("arab_scorcho", "ArabicScorcho", [O, "arabic", "scorcho", "7", "out=arab_scorcho"]),
        ("arab_serpent", "ArabicSerpent", [O, "arabic", "serpent", "5", "out=arab_serpent"]),
        ("arab_bridge", "ArabicBridge", [O, "arabic", "bridge", "9", "out=arab_bridge"]),
        ("arab_cairo", "ArabicCairo", [O, "arabic", "cairo", "3", "out=arab_cairo"]),
        ("arab_golden", "ArabicGolden", [U, "arabic_golden", "5", "lots=dock:1,site:1", "out=arab_golden"]),
        ("arab_baghdad", "ArabicBaghdad", [U, "arabic_baghdad", "9", "lots=hospital:1,site:1", "out=arab_baghdad"])]
if os.environ.get("TOWN_PLAN_ONLY"):
    print("plans only")
for folder, name, cmd in RUNS:
    if sys.argv[1:] and name not in sys.argv[1:]:
        continue
    r = subprocess.run([sys.executable, os.path.join(HERE, cmd[0])] + cmd[1:], capture_output=True, text=True)
    print("==", name, [l[:150] for l in r.stdout.splitlines() if l.startswith(("per 1000", "no room", "buildings", "houses"))], r.stderr[-300:], flush=True)
    if os.environ.get("TOWN_PLAN_ONLY"):
        continue
    plan = json.load(open(f"plans/{folder}/plan.json"))
    views = ["1200,1200,-3000"]
    for t in ("SupplyPileSmall", "SupplyDock", "TechHospital", "TechReinforcementPad", "TechOilRefinery"):
        o = next((o for o in plan["objects"] if o["template"] == t), None)
        if o and len(views) < 3:
            views.append(f"{o['x']},{o['y']},-520")
    for _ in range(4):
        a = subprocess.run([sys.executable, os.path.join(HERE, "apply_plan.py"), f"plans/{folder}", name] + views, capture_output=True, text=True)
        if "busy" not in a.stderr:
            break
    print("  ", [l[:130] for l in a.stdout.splitlines() if l.startswith(("objects", "reblend", "object failed"))][:6], a.stderr[-300:], flush=True)
