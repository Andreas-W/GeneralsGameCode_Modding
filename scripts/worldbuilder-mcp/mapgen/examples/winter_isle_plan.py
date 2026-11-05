# WinterIsleGen, the plan: five players on one compact northern island (the style of NorthernIslandGen), asymmetric but roughly balanced.
# Compact: a round main body with five player lobes merged into it, no capes or peninsulas without a purpose. The players stand at uneven
# angles (gaps 68 to 75 degrees) and slightly uneven distances from the centre. What lies between two neighbours differs for each pair
# (a mountain massif, a fjord, an upland, a mountain spine, a bay with an upland), but every pair shares one oil derrick about as far from
# both, and every player has the same own things: a beach harbour, a supply dock and an expansion on its lobe.
# Usage: winter_isle_plan.py <map path>   Writes mapgen_plan.json and mapgen_work/plan_layout.json next to the map.
import json, math, os, sys

MAP = sys.argv[1]
DIR = os.path.dirname(MAP)
SIZE, B = 500, 30
C = (2500.0, 2500.0)
# (angle, distance of the lobe centre from the island's centre)
PLAYERS = [(15, 1500), (85, 1480), (158, 1520), (228, 1490), (300, 1510)]


def at(angle, r):
    return [round(C[0] + r * math.cos(math.radians(angle))), round(C[1] + r * math.sin(math.radians(angle)))]


def mid(a, b):
    d = (b - a) % 360
    return (a + d / 2) % 360


blobs = [{"name": "main body", "at": list(C), "radius": 1250, "wobble": 0.12}]
beaches, keep = [], []
for n, (a, r) in enumerate(PLAYERS, 1):
    blobs.append({"name": f"lobe of player {n}", "at": at(a, r), "radius": 560, "wobble": 0.12})
    beaches.append({"name": f"harbour of player {n}", "at": at(a + 6, r + 560), "radius": 400, "depth": 70, "climb": 220})
    keep.append({"kind": "waypoint", "template": "*Waypoints/Waypoint", "name": f"Player_{n}_Start", "at": at(a, r - 30), "may_move": 50})

# Between neighbours: (pair, angle in the middle)
mids = [mid(PLAYERS[i][0], PLAYERS[(i + 1) % 5][0]) for i in range(5)]   # 50, 121.5, 193, 264, 337.5
# Headlands under the two mountain groups: the bays between those lobes would otherwise swallow them.
blobs += [{"name": "headland 1-2 (massif)", "at": at(mids[0], 1450), "radius": 400, "wobble": 0.25},
          {"name": "headland 4-5 (spine)", "at": at(mids[3], 1500), "radius": 340, "wobble": 0.25}]
plateaus = [
    # 1-2: a mountain massif on the coast between them
    {"name": "massif 1-2", "at": at(mids[0], 1580), "radius": 250, "wobble": 0.3, "level": 3},
    {"name": "massif 1-2, inner", "at": at(mids[0] - 3, 1330), "radius": 190, "wobble": 0.3, "level": 3},
    # 3-4: an upland holding their shared derrick, ramps toward both and the centre
    {"name": "upland 3-4", "at": at(mids[2], 1050), "radius": 300, "wobble": 0.15, "level": 2,
     "ramps": [mids[2] + 180, mids[2] + 75, mids[2] - 75]},
    # 4-5: a mountain spine from the coast inward
    {"name": "spine 4-5, outer", "at": at(mids[3], 1660), "radius": 190, "wobble": 0.3, "level": 3},
    {"name": "spine 4-5, middle", "at": at(mids[3] + 3, 1450), "radius": 190, "wobble": 0.3, "level": 3},
    {"name": "spine 4-5, inner", "at": at(mids[3] - 2, 1260), "radius": 140, "wobble": 0.3, "level": 3},
    # 5-1: an upland by the bay, the shared derrick on it
    {"name": "upland 5-1", "at": at(mids[4], 1000), "radius": 280, "wobble": 0.15, "level": 2,
     "ramps": [mids[4] + 180, mids[4] + 80, mids[4] - 80]},
    # 2-3: a knoll at the head of the fjord holding their shared derrick
    {"name": "knoll 2-3", "at": at(mids[1], 720), "radius": 250, "wobble": 0.15, "level": 2, "ramps": [mids[1] + 180, mids[1] + 80, mids[1] - 80]},
]
# 2-3: a bent fjord from the coast toward the centre
fa = mids[1]
shape_edits = [{"carve": [rx, ry], "at": at(fa + da, r), "floor": 1, "why": "fjord 2-3, bent"}
               for r, da, rx, ry in ((1700, 0, 140, 140), (1600, 1.5, 132, 132), (1500, 3, 124, 124), (1400, 4, 115, 115), (1300, 4, 105, 105),
                                     (1200, 3, 92, 92), (1110, 1.5, 75, 75))]
# The carve ellipses are axis-aligned ([rx, ry]); the fjord runs at about 120 degrees, so it is a dense chain of round pieces, each
# overlapping the next by over half (pieces 180 apart came out as separate round ponds), bent by stepping sideways.
# Shared oil derricks: (pair, distance from the centre on the middle angle); on the uplands and the knoll where there is one.
SHARED = [(0, 950), (1, 700), (2, 1050), (3, 950), (4, 1000)]
# 5-1: a bay with a beach
beaches.append({"name": "bay 5-1", "at": at(mids[4], 1750), "radius": 380, "depth": 60, "climb": 200})

plan = {
    "map": MAP, "mockup": None,
    "description": "From scratch: 5 player naval map in the style of NorthernIslandGen (one realistic irregular northern / winter island, mountains, "
                   "forests, a town in the centre), asymmetric but roughly balanced, more compact: no empty peninsulas.",
    "seed": 7,
    "symmetry": {"kind": "none", "from": "asymmetric; balance from equal lobes, harbours, docks, expansions and shared derricks at matched distances"},
    "water": {"height": 60, "floor": 0, "ships": True, "from": "as NorthernIslandGen"},
    "levels": [{"z": 64, "role": "beaches: harbours and the bay"}, {"z": 90, "role": "main level"}, {"z": 120, "role": "uplands"}],
    "mountain_above": 140,
    "mountains": {"rise": 45, "relief": 32, "flank": 150, "bend": 70, "from": "NorthernIslandGen"},
    "beach_width": 140, "shore_reach": 250, "border": B, "shift": 0,
    "playable_max": SIZE * 10 - 10, "playable_cells": [SIZE - 1, SIZE - 1], "height_scale": 1.2,
    "layout": {"vertices": SIZE + 2 * B, "symmetry": {"count": 1}, "neck": 200, "coast_noise": 80, "hills": 6, "shelf": 240,
               "main_level": 1, "ramp_jitter_deg": 6, "blobs": blobs, "plateaus": plateaus, "beaches": beaches, "mountain_level": 175},
    "shape_edits": shape_edits,
    "keep": keep,
    "styles": {"reference": "Bitter Winter, as NorthernIslandGen", "cliff_params": {"face": 4.2, "face_spread": 1.8, "bend": 40, "from": "NorthernIslandGen"}},
    "shared_derricks": [{"pair": f"{i + 1}-{(i + 1) % 5 + 1}", "at": at(mids[i], r)} for i, r in SHARED],
    "woods": {"count": 8, "town": [C[0], C[1], 600], "min_apart": 700, "radius_max": 200, "spacing": 21, "floor": "SnowType7 under the trees only"},
    "roads": {"main": "GravelRoadSnow from each base to the nearest town street point (join any)", "join": "any", "expansion": "DirtRoadSnow to each expansion"},
    "players": [{"n": n, "angle": a, "lobe_distance": r} for n, (a, r) in enumerate(PLAYERS, 1)],
    "between": {"1-2": "mountain massif", "2-3": "fjord (and a knoll inland)", "3-4": "upland with the shared derrick",
                "4-5": "mountain spine", "5-1": "bay and an upland with the shared derrick"},
    "stages": ["A layout and terrain shape", "A2 Tech, supplies, relief", "B textures and seabed", "C plants and woods", "D town and roads",
               "F checks", "E AI and naval markers: later (skipped on request)"],
}
os.makedirs(os.path.join(DIR, "mapgen_work"), exist_ok=True)
json.dump(plan, open(os.path.join(DIR, "mapgen_plan.json"), "w"), indent=1)
lay = dict(plan, levels=plan["levels"] + [{"z": 175, "role": "mountain marker for layout_blobs only"}])
json.dump(lay, open(os.path.join(DIR, "mapgen_work", "plan_layout.json"), "w"), indent=1)
print("mids", [round(m, 1) for m in mids], "starts", [k["at"] for k in keep])
