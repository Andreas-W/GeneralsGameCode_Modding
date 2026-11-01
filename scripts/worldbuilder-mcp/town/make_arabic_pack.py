# Arabic style pack from the six measured maps: building mix counted over all of them, one preset per map family, and a generalized default.
import json, os, re, collections
from town_paths import PACKS  # enters the work folder
geo = json.load(open("gamedata/geo.json"))
FAM = re.compile(r"^(Mogadishu(House|Shop|Garage|Dome|ArchedHouse|SmallApartment|Highrise)|JunkDealer$)")
mix = collections.Counter()
for n in ("cairo", "golden", "serpent", "scorcho", "bridge", "baghdad"):
    for o in json.load(open(f"refs/{n}/dump.json"))["objects"]:
        if o["kind"] == "object" and FAM.match(o["template"]) and "Bib" not in o["template"] and o["template"] in geo:
            mix[o["template"]] += 1
print("measured mix", mix.most_common())
themes = json.load(open(f"{PACKS}/building_themes.json"))
themes["arabic"] = dict(mix)
json.dump(themes, open(f"{PACKS}/building_themes.json", "w"), indent=1)
PALMS = {"TreePalm1short": 5, "TreePalm1": 3, "TreePalm2": 5, "TreePalm2short": 5, "TreePalm03": 3, "TreePalm04": 1}
MARKET = {"ClothesLine": 0.9, "BigWoodenBarrels": 0.4, "Barrel": 0.6, "MarketVender01": 0.3, "MarketVender03": 0.3, "CanvasSunShader": 0.3, "Tent": 0.5, "AsianFruitStand02": 0.3, "MogadishuWell": 0.2, "CarScrap": 0.4}
PRESETS = {
    # regular towns: block generator
    "golden": {"generator": "blocks", "source": "Golden Oasis", "major": "TwoLaneOld", "minor": "TwoLaneOld2", "major_width": 60, "minor_width": 60, "sidewalk": None,
               "grid": [320, 250], "theta": 40, "floor": "RocksType5", "floor_share": 0.5, "islands": ["GrassMediumType7", "GrassMediumType34"], "surround": "SandMediumType5c",
               "trees": {**PALMS, "TreeWillow02": 2}, "trees_rate": 13, "props": MARKET},
    "baghdad": {"generator": "blocks", "source": "USA01", "major": "TwoLaneOld2", "minor": "TwoLaneOld", "major_width": 60, "minor_width": 60, "sidewalk": {"type": "Sidewalk3", "offset_ratio": 0.33, "outer_keep": 0.8, "lot_gap": 16},
                "grid": [300, 230], "theta": 40, "floor": "RocksType5", "floor_share": 0.55, "islands": ["SandMediumType12Grassy", "GrassMediumType34"], "surround": "SandMediumType12Grassy",
                "trees": {"TreeDogwood1": 2, "TreePalm1": 2, "TreeDogwood2": 2, "TreePalm2": 2, "TreeDogwood8": 2}, "trees_rate": 8, "props": MARKET},
    # irregular towns: organic streets
    "serpent": {"generator": "organic", "source": "Sand Serpent", "major": "DirtRoad5", "minor": "Sidewalk3", "major_width": 52, "minor_width": 22, "irregularity": 0.8, "street_density": 70,
                "floor": {"SandMediumType13grassy3": 0.65, "SandMediumType13": 0.35}, "patch": "SandMediumType13grassy", "surround": "SandMediumType13b",
                "trees": {"TreeCypress01": 2, "TreeCypress02": 1, "Bush11": 2, "TreeWillow01": 1}, "trees_rate": 8, "props": {"ParkBench": 1.0, "PlanterRound": 0.6, "Dumpster": 0.5, "MarketVender02": 0.5, "BigWoodenBarrels": 0.5, "SidewalkCafeSet": 0.4, "CanvasSunShader": 0.3}},
    "scorcho": {"generator": "organic", "source": "El Scorcho", "major": "DirtRoad", "minor": "DirtRoad3", "major_width": 52, "minor_width": 33, "irregularity": 0.65, "street_density": 45,
                "floor": {"SandMediumType11": 0.85, "RocksType5": 0.15}, "patch": "GrassLargeType2", "surround": "SandType3",
                "trees": {"TreePalm2short": 7, "TreeDogwood2": 6, "TreePalm2": 6, "TreePalm03": 3, "Bush01": 3, "Bush03": 1}, "trees_rate": 22, "props": {**MARKET, "CarCompact": 0.2}},
    "bridge": {"generator": "organic", "source": "Bridge Busters", "major": "TwoLaneDarkDotted", "minor": "Sidewalk3", "major_width": 35, "minor_width": 22, "irregularity": 0.8, "street_density": 70,
               "floor": {"RocksType5": 1.0}, "patch": "GrassMediumType31", "surround": "GrassMediumType30",
               "trees": {"TreeDogwood1": 10, "Bush08": 3, "TreeDogwood4": 1, "Bush04": 1, "TreeDogwood7": 1}, "trees_rate": 20, "lamp": ["AsianStreetLight", 4.0], "props": {"BikeRack": 0.7, "FireHydrantRed": 0.7, "ParkBench": 0.6, "TrashCanA": 0.4, "MailBox": 0.3, "Dumpster": 0.2}},
    "cairo": {"generator": "organic", "source": "Cairo Commandos", "major": "DirtRoad", "minor": "DirtRoadTracks2", "major_width": 52, "minor_width": 52, "irregularity": 0.95, "street_density": 40,
              "floor": {"SandLargeType3Rocky3": 0.55, "SandLargeType3Rocky&Grassy": 0.45}, "patch": "SandType4", "surround": "SandMediumType3",
              "trees": {"TreePalm1short": 9, "TreePalm1": 5, "TreeMaple04": 1}, "trees_rate": 15, "props": {"ClothesLine": 1.0, "Tire02": 1.0, "CarScrap": 0.5, "MarketVender01": 0.5, "MarketVender03": 0.5, "CanvasSunShader": 0.5, "Tent": 1.0}},
    # what they share: Mogadishu buildings close to winding streets, paved around the houses, sand beyond, palms
    "generic": {"generator": "organic", "source": "all six", "major": "TwoLaneOld", "minor": "DirtRoad3", "major_width": 60, "minor_width": 33, "irregularity": 0.6, "street_density": 45,
                "floor": {"RocksType5": 0.7, "SandMediumType11": 0.3}, "patch": "GrassMediumType34", "surround": "SandMediumType11",
                "trees": PALMS, "trees_rate": 14, "props": MARKET},
}
pack = {"name": "arabic", "sources": ["Cairo Commandos", "Golden Oasis", "Sand Serpent", "El Scorcho", "Bridge Busters", "USA01"],
        "measured": {"buildings_per_100x100": {"cairo": 0.51, "golden": 0.35, "serpent": 0.65, "scorcho": 0.48, "bridge": 0.50, "baghdad": 0.46},
                     "street_gap_median": {"cairo": -11, "golden": 11, "serpent": 2, "scorcho": 6, "bridge": 0, "baghdad": 4},
                     "off_axis_deg": {"cairo": 6, "golden": 2, "serpent": 2, "scorcho": 3, "bridge": 1, "baghdad": 2},
                     "bends_per_1000": {"cairo": 5.2, "golden": 1.3, "serpent": 6.5, "scorcho": 7.4, "bridge": 7.3, "baghdad": 2.8},
                     "dead_ends_per_1000": {"cairo": 5.7, "golden": 1.9, "serpent": 3.8, "scorcho": 2.9, "bridge": 2.0, "baghdad": 1.2},
                     "junctions_per_1000": {"cairo": 0.5, "golden": 1.7, "serpent": 1.9, "scorcho": 1.1, "bridge": 2.4, "baghdad": 0.2}},
        "buildings_per_1000_street": 5.5, "gap_to_street": [0, 12], "presets": PRESETS}
json.dump(pack, open(f"{PACKS}/arabic.json", "w"), indent=1)
# what the block generator reads, one pack per regular preset
for name in ("golden", "baghdad"):
    p = PRESETS[name]
    u = {"name": "arabic_" + name, "measured": {"street_per_100x100": {}, "aligned_to_street": {"along": 0.62, "square": 0.32}},
         "streets": {"major": p["major"], "minor": p["minor"], "major_width": p["major_width"], "minor_width": p["minor_width"], "theta_deg": p["theta"], "grid": p["grid"], "jitter": [14, 12]},
         "sidewalk": p["sidewalk"] or {"type": None, "offset_ratio": 0.5, "outer_keep": 0},
         # Baghdad's sidewalks lie partly on the wide street, so buildings stand further from the sidewalk line
         "buildings": {"per_1000_street": 6.5, "mix": dict(mix), "gap_to_sidewalk": [16, 26] if p["sidewalk"] else [5, 14]},
         "ground": {"floor": p["floor"], "floor_share": p["floor_share"], "islands": p["islands"], "parking": "AsphaltType3", "surround": p["surround"], "lot_dirt": "DirtMediumType12"},
         "parking": {"type": "ParkingLines", "share_of_street": 0.0, "row_length": 107, "row_spacing": 44},
         "props_per_1000_street": dict(p["props"]), "stoplights_per_major_junction": 0, "cars_per_1000_street": 1.0, "cars": ["CarScrap", "CarCompact", "CarBug2", "CarSUV01"],
         "trees_per_1000_street": p["trees_rate"], "trees": p["trees"]}
    json.dump(u, open(f"{PACKS}/arabic_{name}.json", "w"), indent=1)
print("presets", list(PRESETS))
