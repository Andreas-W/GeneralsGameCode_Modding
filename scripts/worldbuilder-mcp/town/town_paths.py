# Where the town scripts find and put things. Importing this enters the work folder, so that plans and scans never land in the repo.
#   GAME_DIR         the mod folder (required for game data)
#   TOWN_REF_MAPS    folder holding the reference maps, with Maps\ and maps_gens\ below it (only for scanning)
#   TOWN_WORK        work folder, default work\ next to these scripts (git-ignored)
#   TOWN_WB_PORT     port of the second WorldBuilder, default 47810
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACKS = os.path.join(HERE, "packs")
WORK = os.environ.get("TOWN_WORK") or os.path.join(HERE, "work")
WB_PORT = int(os.environ.get("TOWN_WB_PORT", 47810))
GAME_DIR = os.environ.get("GAME_DIR", "")
REF_MAPS = os.environ.get("TOWN_REF_MAPS", "")
ROADS_INI = os.path.join(GAME_DIR, "Data", "INI", "Roads.ini")
USER_MAPS = os.path.join(os.path.expanduser("~"), "Documents", "Command and Conquer Generals Zero Hour Data", "Maps")

if not GAME_DIR or not os.path.isdir(GAME_DIR):
    sys.exit("GAME_DIR is not set: put the mod folder into .claude/settings.local.json (see CLAUDE.md) or the environment.")
# wb_bridge.py lives one folder up
sys.path.insert(0, os.path.dirname(HERE))
for sub in ("plans", "refs", "refs/alpine", "gamedata"):
    os.makedirs(os.path.join(WORK, sub), exist_ok=True)
os.chdir(WORK)
if not os.path.exists("gamedata/geo.json") and os.path.basename(sys.argv[0]) != "gamedata.py":
    sys.exit("No game data yet: run gamedata.py first (and again after the INI files change).")
