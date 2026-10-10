"""Faction descriptions for AI authoring.

Turns the raw output of the bridge command faction.describe into a description an AI can plan
with: noise filtered out, tech tiers, build paths, combat numbers, guessed roles. The facts go to
<Faction>.generated.json and are rewritten on every run; the tuning lives in
<Faction>.overrides.yaml, which is written once as a skeleton and from then on only read.
"""

from __future__ import annotations

import json
import os
import re
from collections import Counter
from typing import Any

import yaml

# Names of types that exist for cinematics, tests or as leftovers; never part of what a faction plays with.
NOISE_NAME = re.compile(r"^(CINE_|TEST|Test_|GC_|SHELL_)|Cinematic|Parade|HighDef|(OLD|Old)$|Test[A-Z]")

CATEGORIES = ("infantry", "vehicle", "aircraft", "structure")

# Structure roles by KindOf, first match wins.
STRUCTURE_ROLES = (
    ("COMMANDCENTER", "command_center"),
    ("FS_SUPERWEAPON", "superweapon"),
    ("FS_POWER", "power"),
    ("FS_SUPPLY_CENTER", "supply_center"),
    ("FS_SUPPLY_DROPZONE", "income"),
    ("FS_BLACK_MARKET", "income"),
    ("FS_BARRACKS", "barracks"),
    ("FS_WARFACTORY", "factory"),
    ("FS_AIRFIELD", "airfield"),
    ("FS_BASE_DEFENSE", "base_defense"),
    ("FS_STRATEGY_CENTER", "tech"),
    ("FS_ADVANCED_TECH", "tech"),
    ("FS_TECHNOLOGY", "tech"),
)

# Slots that hold one type, and slots that hold a list.
SINGLE_SLOTS = ("command_center", "builder", "supply_gatherer", "supply_center", "power", "barracks", "factory",
                "airfield", "capture_unit", "hero")
LIST_SLOTS = ("tech", "base_defense", "superweapon", "income", "main_battle", "anti_air", "anti_infantry",
              "anti_tank", "siege", "air_strike", "transport", "naval")

SIEGE_RANGE = 300.0

OVERLAY_UNIT_KEYS = ("role", "phase", "ai_use", "weight", "counters", "countered_by", "micro", "team_hint", "notes")


def paths(faction: str, directory: str) -> dict[str, str]:
    base = os.path.join(os.path.abspath(directory), faction)
    return {"generated": base + ".generated.json", "overlay": base + ".overrides.yaml", "doctrine": base + ".doctrine.md"}


def load_overlay(path: str) -> dict[str, Any]:
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    if not isinstance(data, dict):
        raise ValueError(f"{path}: the top level must be a mapping")
    return data


def category_of(obj: dict[str, Any]) -> str | None:
    kinds = obj["kind_of"]
    if "STRUCTURE" in kinds:
        return "structure"
    if "AIRCRAFT" in kinds:
        return "aircraft"
    if "INFANTRY" in kinds:
        return "infantry"
    if "VEHICLE" in kinds:
        return "vehicle"
    return None


def _noise_reason(obj: dict[str, Any], forced_in: set[str], forced_out: set[str]) -> str | None:
    name = obj["name"]
    if name in forced_out:
        return "excluded in overrides"
    if name in forced_in:
        return None
    if NOISE_NAME.search(name) or obj.get("side") == "Test":
        return "test or cinematic type"
    if not obj["in_tech_tree"] and "SELECTABLE" not in obj["kind_of"]:
        return "not selectable"
    return None


def _default_set(sets: list[dict[str, Any]]) -> dict[str, Any] | None:
    for entry in sets:
        if not entry["conditions"]:
            return entry
    return sets[0] if sets else None


def _strip(obj: dict[str, Any], gone: set[str], known: set[str]) -> None:
    """Removes dropped types from the links of an object."""
    prereqs = obj.get("prerequisites")
    if prereqs and "objects" in prereqs:
        groups = [[n for n in group if n not in gone] for group in prereqs["objects"]]
        # A requirement no type of the faction meets cannot be met by building.
        foreign = sorted({n for g in groups if g and not any(m in known for m in g) for n in g})
        if foreign:
            obj["unavailable"] = "needs types the faction cannot build: " + ", ".join(foreign)
        prereqs["objects"] = [g for g in groups if g]
        if not prereqs["objects"]:
            del prereqs["objects"]
        if not prereqs:
            del obj["prerequisites"]
    if "built_by" in obj:
        obj["built_by"] = [n for n in obj["built_by"] if n not in gone]
        if not obj["built_by"]:
            del obj["built_by"]
    if "builds" in obj:
        obj["builds"] = [b for b in obj["builds"] if b["object"] not in gone]
        if not obj["builds"]:
            del obj["builds"]


def _tiers(objects: dict[str, dict[str, Any]], starts: list[str]) -> dict[str, int]:
    """Number of structures that have to be built before a type can be made."""
    tier = {name: 0 for name in starts if name in objects}
    for _ in range(len(objects) + 1):
        changed = False
        for name, obj in objects.items():
            if name in starts or obj.get("unavailable"):
                continue
            needs: list[list[str]] = []
            if obj.get("built_by"):
                needs.append(obj["built_by"])
            needs.extend(obj.get("prerequisites", {}).get("objects", []))
            if not needs:
                continue
            depth = 0
            known = True
            for group in needs:
                # What the player starts with costs no build step.
                options = [tier[n] + (1 if category_of(objects[n]) == "structure" and n not in starts else 0)
                           for n in group if n in tier]
                if not options:
                    known = False
                    break
                depth = max(depth, min(options))
            if known and tier.get(name) != depth and (name not in tier or depth < tier[name]):
                tier[name] = depth
                changed = True
        if not changed:
            break
    return tier


def _build_path(name: str, objects: dict[str, dict[str, Any]], tier: dict[str, int], starts: list[str]) -> list[str]:
    """The structures to build before a type, taking the lowest option of every choice."""
    needed: list[str] = []
    seen: set[str] = set()

    def visit(current: str) -> None:
        obj = objects[current]
        groups: list[list[str]] = []
        if obj.get("built_by"):
            groups.append(obj["built_by"])
        groups.extend(obj.get("prerequisites", {}).get("objects", []))
        for group in groups:
            options = [n for n in group if n in tier and n in objects]
            if not options:
                continue
            choice = min(options, key=lambda n: (tier[n], objects[n]["cost"]))
            if choice in seen:
                continue
            seen.add(choice)
            visit(choice)
            if category_of(objects[choice]) == "structure" and choice not in starts:
                needed.append(choice)

    seen.add(name)
    visit(name)
    return needed


def _combat(obj: dict[str, Any], weapons: dict[str, dict[str, Any]], armors: dict[str, dict[str, Any]],
            reference: dict[str, str]) -> dict[str, Any] | None:
    chosen = _default_set(obj.get("weapon_sets", []))
    if chosen is None:
        return None
    used = [weapons[n] for n in dict.fromkeys(chosen["weapons"].values()) if n in weapons]
    used = [w for w in used if w.get("dps", 0) > 0.01]
    if not used:
        return None
    ground = [w for w in used if "GROUND" in w["anti"]]
    air = [w for w in used if "AIRBORNE_VEHICLE" in w["anti"]]
    if not ground and not air:
        return None
    result: dict[str, Any] = {"targets": []}
    if ground:
        result["targets"].append("ground")
        result["range"] = max(w["range"] for w in ground)
    if air:
        result["targets"].append("air")
        result["range_air"] = max(w["range"] for w in air)
    dps_vs: dict[str, float] = {}
    for cat in CATEGORIES:
        pool = air if cat == "aircraft" else ground
        factors = armors.get(reference.get(cat, ""), {}).get("damage_taken", {})
        best = max((w["dps"] * factors.get(w["damage_type"], 1.0) for w in pool), default=0.0)
        if best > 0:
            dps_vs[cat] = round(best, 1)
    result["dps_vs"] = dps_vs
    if obj["cost"] > 0:
        result["dps_per_1000"] = {cat: round(v * 1000.0 / obj["cost"], 1) for cat, v in dps_vs.items()}
    return result


def _guess_role(obj: dict[str, Any], naval: set[str]) -> str:
    kinds = obj["kind_of"]
    combat = obj.get("combat")
    if category_of(obj) == "structure":
        built = [b["object"] for b in obj.get("builds", [])]
        if built and sum(n in naval for n in built) * 2 > len(built):
            return "shipyard"
        for kind, role in STRUCTURE_ROLES:
            if kind in kinds:
                return role
        return "production" if obj.get("builds") else "structure"
    if "DOZER" in kinds:
        return "builder"
    if "HARVESTER" in kinds or obj.get("supply_boxes"):
        return "supply_gatherer"
    if "HERO" in kinds:
        return "hero"
    if combat is None:
        return "transport" if obj.get("contain", {}).get("slots") else "support"
    dps = combat["dps_vs"]
    vs_infantry, vs_vehicle = dps.get("infantry", 0), dps.get("vehicle", 0)
    if combat["targets"] == ["air"] or dps.get("aircraft", 0) > 3 * max(vs_infantry, vs_vehicle, dps.get("structure", 0)):
        return "anti_air"
    if category_of(obj) == "aircraft":
        return "air_strike"
    if combat["range"] >= SIEGE_RANGE:
        return "siege"
    if category_of(obj) == "infantry":
        return "anti_tank" if vs_vehicle > vs_infantry else "anti_infantry"
    return "main_battle" if vs_vehicle >= vs_infantry else "anti_infantry"


def _guess_slots(objects: dict[str, dict[str, Any]]) -> dict[str, Any]:
    slots: dict[str, Any] = {}
    hero_capture = None
    ordered = sorted(objects.values(), key=lambda o: (o.get("tier", 99), o["cost"], o["name"]))
    for obj in ordered:
        role = obj["role_guess"]
        if obj.get("unavailable"):
            continue
        if obj.get("naval") or role == "shipyard":
            slots.setdefault("naval", []).append(obj["name"])
            continue
        if role in SINGLE_SLOTS:
            slots.setdefault(role, obj["name"])
        elif role in LIST_SLOTS:
            slots.setdefault(role, []).append(obj["name"])
        if "HARVESTER" in obj["kind_of"] or obj.get("supply_boxes"):
            slots.setdefault("supply_gatherer", obj["name"])
        if "air" in (obj.get("combat") or {}).get("targets", []) and role != "anti_air" and category_of(obj) != "structure":
            slots.setdefault("anti_air", []).append(obj["name"])
        buttons = [b["button"] for b in obj.get("abilities", []) + obj.get("special_powers", [])]
        if category_of(obj) == "infantry" and any("Capture" in b for b in buttons):
            if "HERO" in obj["kind_of"]:
                hero_capture = hero_capture or obj["name"]
            else:
                slots.setdefault("capture_unit", obj["name"])
    if hero_capture:
        slots.setdefault("capture_unit", hero_capture)
    return slots


def derive(raw: dict[str, Any], overlay: dict[str, Any], ai_data: dict[str, Any] | None = None) -> dict[str, Any]:
    """The description of a faction from the raw faction.describe output."""
    forced_in = set(overlay.get("include") or [])
    forced_out = set(overlay.get("exclude") or [])
    weapons = {w["name"]: w for w in raw["weapons"]}
    armors = {a["name"]: a for a in raw["armors"]}

    all_buttons = sorted({b["button"] for o in raw["objects"]
                          for key in ("abilities", "special_powers", "upgrades", "builds") for b in o.get(key, [])})
    objects: dict[str, dict[str, Any]] = {}
    other: list[dict[str, Any]] = []
    dropped: dict[str, str] = {}
    for obj in raw["objects"]:
        reason = _noise_reason(obj, forced_in, forced_out)
        if reason:
            dropped[obj["name"]] = reason
        elif obj["in_tech_tree"] or obj["name"] in forced_in:
            objects[obj["name"]] = obj
        else:
            other.append(obj)
    gone = set(dropped) | {o["name"] for o in other}
    for obj in objects.values():
        _strip(obj, gone, set(objects))

    # A type the tree does not build that is named like one it does is a variant of it (AI-only copies).
    names = sorted(objects, key=len, reverse=True)
    others: list[dict[str, Any]] = []
    for obj in other:
        entry = {"name": obj["name"], "side": obj["side"], "cost": obj["cost"], "category": category_of(obj)}
        base = next((n for n in names if obj["name"].startswith(n + "_")), None)
        if base:
            entry["variant_of"] = base
            objects[base].setdefault("variants", []).append(obj["name"])
        others.append(entry)

    faction = raw["faction"]
    faction["starting_units"] = [n for n in faction["starting_units"] if n in objects]
    # Something that is unavailable keeps everything only it builds unavailable too.
    for _ in range(len(objects)):
        spread = False
        for obj in objects.values():
            builders = obj.get("built_by", [])
            if builders and "unavailable" not in obj and all("unavailable" in objects[b] for b in builders if b in objects):
                obj["unavailable"] = "only built by unavailable types"
                spread = True
        if not spread:
            break
    # Starting units that a structure also builds get their tier from that structure.
    starts = [n for n in [faction["starting_building"]] + faction["starting_units"]
              if n in objects and (n == faction["starting_building"] or not objects[n].get("built_by"))]
    tier = _tiers(objects, starts)

    # Armor a weapon is most likely to meet, per kind of target: the commonest default armor.
    reference: dict[str, str] = {}
    for cat in CATEGORIES:
        counts = Counter()
        for obj in objects.values():
            chosen = _default_set(obj.get("armor_sets", []))
            if chosen and category_of(obj) == cat:
                counts[chosen["armor"]] += 1
        if counts:
            reference[cat] = counts.most_common(1)[0][0]

    unlocks: dict[str, list[str]] = {}
    for obj in objects.values():
        for group in obj.get("prerequisites", {}).get("objects", []):
            for needed in group:
                unlocks.setdefault(needed, []).append(obj["name"])

    top = max(tier.values(), default=0)
    for obj in objects.values():
        surfaces = obj.get("locomotor", {}).get("surfaces", [])
        if "WATER" in surfaces and "GROUND" not in surfaces and "AIR" not in surfaces:
            obj["naval"] = True
    naval = {n for n, o in objects.items() if o.get("naval")}
    for name, obj in objects.items():
        obj["category"] = category_of(obj)
        if name in tier:
            obj["tier"] = tier[name]
            path = _build_path(name, objects, tier, starts)
            if path:
                obj["build_path"] = path
                obj["path_cost"] = sum(objects[n]["cost"] for n in path)
                obj["path_time"] = round(sum(objects[n]["build_time"] for n in path), 1)
            obj["phase_guess"] = "early" if tier[name] * 3 <= top else ("mid" if tier[name] * 3 <= top * 2 else "late")
        if name in unlocks:
            obj["unlocks"] = unlocks[name]
        combat = _combat(obj, weapons, armors, reference)
        if combat:
            obj["combat"] = combat
        obj["role_guess"] = _guess_role(obj, naval)

    used_weapons = {n for o in objects.values() for s in o.get("weapon_sets", []) for n in s["weapons"].values()}
    used_armors = {s["armor"] for o in objects.values() for s in o.get("armor_sets", [])}
    return {
        "meta": {
            "faction": faction["name"],
            "note": "Generated by faction_generate from the game data. Do not edit; tune in the overrides file.",
            "reference_armor": reference,
            "max_tier": top,
            "dropped": dropped,
        },
        "faction": faction,
        "slots_guess": _guess_slots(objects),
        "objects": objects,
        "other_objects": others,
        "weapons": {n: w for n, w in weapons.items() if n in used_weapons},
        "armors": {n: a for n, a in armors.items() if n in used_armors},
        "upgrades": {u["name"]: u for u in raw["upgrades"]},
        "sciences": {s["name"]: s for s in raw["sciences"]},
        "special_powers": {p["name"]: p for p in raw["special_powers"]},
        "buttons": all_buttons,
        "ai_data": ai_data or {},
    }


def _yaml_value(value: Any) -> str:
    if isinstance(value, list):
        return "[" + ", ".join(str(v) for v in value) + "]"
    return str(value)


def overlay_skeleton(doc: dict[str, Any]) -> str:
    """First version of the overrides file: every guess spelled out for review."""
    name = doc["meta"]["faction"]
    lines = [
        f"# AI tuning for {name}. Edited by hand; faction_generate writes this file once and never again.",
        f"# The facts are in {name}.generated.json. Everything below was guessed from them: review it.",
        "#",
        "# slots   abstract roles -> the types that fill them; script templates refer to these.",
        "# units   per type, any of: role, phase (early|mid|late), ai_use (normal|limited|never; default",
        "#         normal), weight, counters, countered_by, micro, team_hint, notes.",
        "# exclude / include   types to drop from, or keep in, the description on the next generate.",
        "# counterparts   {name in another faction: name here} for types, upgrades, sciences, special",
        "#         powers and buttons, used when this faction's AI is derived from another one (ai_scaffold).",
        "",
        f"faction: {name}",
        "exclude: []",
        "include: []",
        "counterparts: {}",
        "",
        "slots:",
    ]
    for slot in SINGLE_SLOTS + LIST_SLOTS:
        if slot in doc["slots_guess"]:
            lines.append(f"  {slot}: {_yaml_value(doc['slots_guess'][slot])}")
    lines += ["", "units:"]
    ordered = sorted(doc["objects"].values(), key=lambda o: (o["category"] == "structure", o.get("tier", 99), o["cost"]))
    for obj in ordered:
        fields = [f"role: {obj['role_guess']}"]
        if "phase_guess" in obj:
            fields.append(f"phase: {obj['phase_guess']}")
        if obj.get("unavailable"):
            fields.append("ai_use: never")
        lines.append(f"  {obj['name']}: {{{', '.join(fields)}}}")
    return "\n".join(lines) + "\n"


def doctrine_skeleton(doc: dict[str, Any]) -> str:
    name = doc["meta"]["faction"]
    return (f"# {name} doctrine\n\n"
            "How this faction should be played by the AI. Free text; faction_get shows it with the overview.\n\n"
            "## Identity\n\n## Opening\n\n## Economy\n\n## Army composition by phase\n\n## Defense\n\n"
            "## General powers and upgrades (priority)\n\n## Per difficulty\n\n## Do not\n")


def generate(raw: dict[str, Any], faction: str, directory: str, ai_data: dict[str, Any] | None = None) -> dict[str, Any]:
    """Writes the generated description and, the first time, the overrides and doctrine skeletons."""
    where = paths(faction, directory)
    os.makedirs(os.path.dirname(where["generated"]), exist_ok=True)
    overlay = load_overlay(where["overlay"])
    doc = derive(raw, overlay, ai_data)
    with open(where["generated"], "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)

    result: dict[str, Any] = {
        "generated": where["generated"],
        "bytes": os.path.getsize(where["generated"]),
        "objects": len(doc["objects"]),
        "other_objects": len(doc["other_objects"]),
        "dropped": len(doc["meta"]["dropped"]),
        "max_tier": doc["meta"]["max_tier"],
        "slots_guess": doc["slots_guess"],
        "build_list_entries": doc["ai_data"].get("build_list_entries", 0),
    }
    if overlay:
        known = set(overlay.get("units") or {})
        result["overrides"] = where["overlay"]
        result["new_in_game_data"] = sorted(set(doc["objects"]) - known)
        result["no_longer_in_game_data"] = sorted(known - set(doc["objects"]))
        slot_names = {n for v in (overlay.get("slots") or {}).values() for n in (v if isinstance(v, list) else [v])}
        result["unknown_in_slots"] = sorted(n for n in slot_names if n not in doc["objects"])
    else:
        with open(where["overlay"], "w", encoding="utf-8", newline="\n") as f:
            f.write(overlay_skeleton(doc))
        result["overrides_created"] = where["overlay"]
    if not os.path.exists(where["doctrine"]):
        with open(where["doctrine"], "w", encoding="utf-8", newline="\n") as f:
            f.write(doctrine_skeleton(doc))
        result["doctrine_created"] = where["doctrine"]
    untiered = sorted(n for n, o in doc["objects"].items() if "tier" not in o)
    if untiered:
        result["without_tier"] = untiered
    return result


def load(faction: str, directory: str) -> dict[str, Any]:
    """The generated description with the overrides merged in."""
    where = paths(faction, directory)
    if not os.path.exists(where["generated"]):
        raise FileNotFoundError(f"no description of {faction} in {os.path.dirname(where['generated'])}; run faction_generate first")
    with open(where["generated"], encoding="utf-8") as f:
        doc = json.load(f)
    overlay = load_overlay(where["overlay"])
    units = overlay.get("units") or {}
    for name, obj in doc["objects"].items():
        tuning = units.get(name) or {}
        obj["role"] = tuning.get("role", obj["role_guess"])
        obj["phase"] = tuning.get("phase", obj.get("phase_guess"))
        for key in OVERLAY_UNIT_KEYS[2:]:
            if key in tuning:
                obj[key] = tuning[key]
        # YAML reads a bare yes or no as a boolean.
        if isinstance(obj.get("ai_use"), bool):
            obj["ai_use"] = "normal" if obj["ai_use"] else "never"
    doc["slots"] = dict(doc["slots_guess"], **(overlay.get("slots") or {}))
    doc["counterparts"] = overlay.get("counterparts") or {}
    if os.path.exists(where["doctrine"]):
        with open(where["doctrine"], encoding="utf-8") as f:
            doc["doctrine"] = f.read()
    return doc


def _tree_row(obj: dict[str, Any]) -> dict[str, Any]:
    row = {k: obj[k] for k in ("name", "category", "role", "tier", "cost", "build_time", "built_by", "prerequisites",
                               "unlocks", "build_path", "path_cost", "max_simultaneous", "energy", "unavailable",
                               "naval") if k in obj}
    if obj.get("builds"):
        row["builds"] = [b["object"] for b in obj["builds"]]
    if obj.get("upgrades"):
        row["upgrades"] = [u["upgrade"] for u in obj["upgrades"]]
    return row


def _unit_row(obj: dict[str, Any]) -> dict[str, Any]:
    row = {k: obj[k] for k in ("name", "category", "role", "phase", "ai_use", "weight", "tier", "cost", "health") if k in obj}
    if "locomotor" in obj:
        row["speed"] = obj["locomotor"]["speed"]
    combat = obj.get("combat")
    if combat:
        row.update({k: combat[k] for k in ("targets", "range", "range_air", "dps_vs") if k in combat})
    for key in ("stealth", "detector_range", "counters", "countered_by", "micro", "team_hint", "notes", "variants",
                "unavailable", "naval"):
        if key in obj:
            row[key] = obj[key]
    if obj.get("contain", {}).get("slots"):
        row["carries"] = obj["contain"]["slots"]
    return row


def view(doc: dict[str, Any], what: str, names: list[str] | None = None) -> dict[str, Any]:
    """One slice of a merged description (see faction_get)."""
    objects = doc["objects"]
    ordered = sorted(objects.values(), key=lambda o: (o.get("tier", 99), o["cost"], o["name"]))
    if what == "overview":
        result = {"faction": doc["faction"], "slots": doc["slots"], "max_tier": doc["meta"]["max_tier"],
                  "reference_armor": doc["meta"]["reference_armor"],
                  "objects": [{k: o[k] for k in ("name", "role", "phase", "tier", "cost", "ai_use", "unavailable")
                               if o.get(k) is not None} for o in ordered],
                  "counts": {k: len(doc[k]) for k in ("objects", "other_objects", "weapons", "upgrades", "sciences", "special_powers")}}
        if doc.get("doctrine"):
            result["doctrine"] = doc["doctrine"]
        return result
    if what == "tree":
        return {"objects": [_tree_row(o) for o in ordered]}
    if what == "units":
        return {"reference_armor": doc["meta"]["reference_armor"],
                "units": [_unit_row(o) for o in ordered if o["category"] != "structure"],
                "defenses": [_unit_row(o) for o in ordered if o["category"] == "structure" and o.get("combat")]}
    if what == "unit":
        if not names:
            raise ValueError("view \"unit\" needs names")
        found, unknown = [], []
        for name in names:
            obj = objects.get(name)
            if obj is None:
                unknown.append(name)
                continue
            full = dict(obj)
            full["weapons"] = {n: doc["weapons"][n] for s in obj.get("weapon_sets", []) for n in s["weapons"].values() if n in doc["weapons"]}
            full["armors"] = {s["armor"]: doc["armors"][s["armor"]] for s in obj.get("armor_sets", []) if s["armor"] in doc["armors"]}
            found.append(full)
        return {"objects": found, "unknown": unknown}
    if what == "ai_data":
        return {"ai_data": doc.get("ai_data", {}),
                "note": "build_list: how many places the side's SkirmishBuildList has per structure; a structure without one is never built"}
    if what in ("upgrades", "sciences", "special_powers", "other_objects"):
        values = doc[what]
        return {what: list(values.values()) if isinstance(values, dict) else values}
    raise ValueError("view must be overview, tree, units, unit, upgrades, sciences, special_powers, other_objects or ai_data")


#-------------------------------------------------------------------------------------------------
# Checking a player's scripts and teams against the description
#-------------------------------------------------------------------------------------------------

BUILD_UPGRADE_ACTIONS = ("AI_PLAYER_BUILD_UPGRADE",)
# Build actions that take the place of the structure from the side's SkirmishBuildList.
BUILD_LIST_ACTIONS = ("SKIRMISH_BUILD_BUILDING",)
COMMAND_BUTTON_PARAMS = ("COMMANDBUTTON", "COMMANDBUTTON_ABILITY", "COMMANDBUTTON_ALL_ABILITIES")
COMBAT_ROLES = ("main_battle", "siege", "anti_infantry", "anti_tank")
SEVERITIES = ("error", "warning", "info")


class _Issues:
    """Findings, with equal ones folded into one entry that lists where they occur."""

    def __init__(self, max_where: int) -> None:
        self.max_where = max_where
        self.entries: dict[tuple[str, str, str], list[str]] = {}

    def add(self, severity: str, kind: str, message: str, where: str) -> None:
        places = self.entries.setdefault((severity, kind, message), [])
        if where not in places:
            places.append(where)

    def result(self) -> tuple[dict[str, int], list[dict[str, Any]]]:
        counts = {s: 0 for s in SEVERITIES}
        listed = []
        ordered = sorted(self.entries.items(), key=lambda e: (SEVERITIES.index(e[0][0]), e[0][1], e[0][2]))
        for (severity, kind, message), places in ordered:
            counts[severity] += 1
            listed.append({"severity": severity, "kind": kind, "message": message, "count": len(places),
                           "where": places[:self.max_where]})
        return counts, listed


def _walk_params(node: Any, script: str, found: list[tuple[str, str, str, str]]) -> None:
    """Collects (script, action or condition type, parameter type, value) for every named value."""
    if isinstance(node, dict):
        if "conditions" in node or "actions" in node:
            script = node.get("name", script)
        if "type" in node and isinstance(node.get("params"), list):
            for param in node["params"]:
                if isinstance(param, dict) and isinstance(param.get("value"), str):
                    found.append((script, node["type"], param["type"], param["value"]))
        for value in node.values():
            _walk_params(value, script, found)
    elif isinstance(node, list):
        for value in node:
            _walk_params(value, script, found)


def _team_units(team: dict[str, Any]) -> list[tuple[str, int, int]]:
    units = []
    for slot in range(1, 8):
        name = team.get(f"teamUnitType{slot}", "")
        low, high = team.get(f"teamUnitMinCount{slot}", 0), team.get(f"teamUnitMaxCount{slot}", 0)
        if name and name != "<none>" and (low > 0 or high > 0):
            units.append((name, low, high))
    return units


def _describe_unknown(doc: dict[str, Any], name: str) -> tuple[str, str]:
    """Severity and wording for a type the faction description does not hold."""
    for other in doc["other_objects"]:
        if other["name"] == name:
            hint = f" (variant of {other['variant_of']})" if "variant_of" in other else ""
            return "warning", f"{name} is a type of the side that nothing in the tech tree builds{hint}"
    if name in doc["meta"]["dropped"]:
        return "warning", f"{name} was left out of the description: {doc['meta']['dropped'][name]}"
    return "error", f"{name} is not a type of {doc['meta']['faction']}"


def check(doc: dict[str, Any], player: str, dump: dict[str, Any], build_order: dict[str, Any],
          include_teams: bool = False, max_where: int = 8) -> dict[str, Any]:
    """Compares the scripts and teams of a player with what its faction can do."""
    objects = doc["objects"]
    issues = _Issues(max_where)
    start = doc["faction"]["starting_building"]

    # ---- what the build scripts build
    built: dict[str, str] = {}
    researched: dict[str, str] = {}
    from_list: dict[str, str] = {}
    for step in build_order.get("steps", []):
        for build in step["builds"]:
            what = build.get("what")
            if not what:
                continue
            if build["action"] in BUILD_UPGRADE_ACTIONS:
                researched.setdefault(what, step["script"])
            else:
                built.setdefault(what, step["script"])
                if build["action"] in BUILD_LIST_ACTIONS:
                    from_list.setdefault(what, step["script"])
    has_build_scripts = bool(built)
    standing = set(built) | {start}

    for what, script in built.items():
        obj = objects.get(what)
        where = f"script {script}"
        if obj is None:
            # Building another faction's structure can be meant (captured builders), so it is no error.
            issues.add("warning", "build_foreign_type", _describe_unknown(doc, what)[1], where)
            continue
        if obj.get("unavailable"):
            issues.add("error", "build_unavailable", f"{what} cannot be built: {obj['unavailable']}", where)
        if obj["category"] != "structure":
            issues.add("error", "build_not_structure", f"{what} is not a structure", where)
        for needed in obj.get("build_path", []):
            if needed not in standing:
                issues.add("error", "build_prerequisite_never_built", f"{what} needs {needed}, which no build script builds", where)
        if obj.get("ai_use") == "never":
            issues.add("warning", "build_ai_use_never", f"{what} is marked ai_use: never", where)

    for what, script in researched.items():
        upgrade = doc["upgrades"].get(what)
        where = f"script {script}"
        if upgrade is None:
            issues.add("warning", "upgrade_foreign", f"{what} is not an upgrade of {doc['meta']['faction']}", where)
        elif has_build_scripts and upgrade.get("researched_at") and not any(n in standing for n in upgrade["researched_at"]):
            issues.add("error", "upgrade_no_structure",
                       f"{what} is researched at {', '.join(upgrade['researched_at'][:3])}, which no build script builds", where)

    if has_build_scripts:
        for slot in ("power", "supply_center", "barracks", "factory"):
            name = doc["slots"].get(slot)
            if isinstance(name, str) and name in objects and name not in standing:
                issues.add("info", "slot_never_built", f"the {slot} ({name}) is never built", "build order")
        energy = sum(objects[n].get("energy", 0) for n in standing if n in objects)
        if energy < 0 and doc["slots"].get("power"):
            issues.add("info", "power_deficit", f"building each structure once leaves power at {energy}", "build order")

    # ---- the base layout and side settings of AIData.ini
    ai_data = doc.get("ai_data") or {}
    places = ai_data.get("build_list")
    side = ai_data.get("side", doc["faction"].get("side", ""))
    unplaced: list[str] = []
    if ai_data:
        if places is None:
            if from_list:
                issues.add("error", "build_list_missing", f"AIData.ini has no SkirmishBuildList for side {side}: build scripts have nowhere to build",
                           "AIData.ini")
        else:
            for what, script in from_list.items():
                if what not in places:
                    issues.add("error", "build_no_place", f"{what} has no place in SkirmishBuildList {side}, so the build action does nothing",
                               f"script {script}")
            if not any("COMMANDCENTER" in objects.get(n, {}).get("kind_of", []) for n in places):
                issues.add("error", "build_list_no_command_center",
                           f"SkirmishBuildList {side} has no command center; the layout cannot be moved to the start position", "AIData.ini")
            for name in places:
                obj = objects.get(name)
                if obj is None:
                    issues.add("warning", "build_list_foreign_type", f"SkirmishBuildList {side}: " + _describe_unknown(doc, name)[1], "AIData.ini")
                elif obj.get("unavailable"):
                    issues.add("warning", "build_list_unavailable", f"SkirmishBuildList {side}: {name} cannot be built: {obj['unavailable']}", "AIData.ini")
            unplaced = sorted(n for n in places if n not in built) if has_build_scripts else []
        defense = ai_data.get("base_defense")
        if defense and defense not in objects:
            issues.add("warning", "side_base_defense_foreign", f"SideInfo {side}: base defense " + _describe_unknown(doc, defense)[1], "AIData.ini")
        if "skill_sets" not in ai_data:
            issues.add("warning", "side_info_missing", f"AIData.ini has no SideInfo for side {side} (gatherers, base defense, skill sets)", "AIData.ini")
        for number, sciences in enumerate(ai_data.get("skill_sets", []), 1):
            for science in sciences:
                if science not in doc["sciences"]:
                    issues.add("warning", "skill_set_foreign_science", f"{science} is not a science of the faction", f"SideInfo {side}, SkillSet{number}")

    # ---- teams
    used_types: set[str] = set()
    team_rows = []
    teams = [t for t in dump.get("teams", []) if t.get("teamOwner") == player]
    for team in teams:
        where = f"team {team['teamName']}"
        units = _team_units(team)
        row: dict[str, Any] = {"name": team["teamName"], "cost_min": 0, "cost_max": 0, "tier": 0, "roles": [], "needs": []}
        hits_air = False
        speeds = []
        naval = set()
        for name, low, high in units:
            used_types.add(name)
            if low > high:
                issues.add("error", "team_min_over_max", f"{name}: minimum {low} is above maximum {high}", where)
            obj = objects.get(name)
            if obj is None:
                severity, message = _describe_unknown(doc, name)
                issues.add(severity, "team_unknown_type", message, where)
                continue
            if obj.get("unavailable"):
                issues.add("error", "team_unavailable", f"{name} cannot be built: {obj['unavailable']}", where)
                continue
            if obj["category"] == "structure":
                issues.add("error", "team_structure", f"{name} is a structure", where)
                continue
            if "tier" not in obj or (not obj.get("built_by") and name not in doc["faction"]["starting_units"]):
                issues.add("error", "team_nothing_builds", f"nothing builds {name}", where)
                continue
            if obj.get("ai_use") == "never":
                issues.add("warning", "team_ai_use_never", f"{name} is marked ai_use: never", where)
            limit = obj.get("max_simultaneous")
            if limit and low > limit:
                issues.add("error", "team_over_build_limit", f"needs {low} {name} but only {limit} may exist", where)
            for needed in obj.get("build_path", []):
                if needed not in row["needs"]:
                    row["needs"].append(needed)
                if has_build_scripts and needed not in standing:
                    issues.add("error", "team_structure_never_built", f"{name} needs {needed}, which no build script builds", where)
            row["cost_min"] += obj["cost"] * low
            row["cost_max"] += obj["cost"] * high
            row["tier"] = max(row["tier"], obj["tier"])
            if obj["role"] not in row["roles"]:
                row["roles"].append(obj["role"])
            hits_air = hits_air or "air" in (obj.get("combat") or {}).get("targets", [])
            naval.add(bool(obj.get("naval")))
            if obj["category"] != "aircraft" and obj.get("locomotor", {}).get("speed", 0) > 0:
                speeds.append(obj["locomotor"]["speed"])
        row["anti_air"] = hits_air
        if len(naval) > 1:
            issues.add("warning", "team_land_and_sea", "mixes ships with land or air units", where)
        if sum(high for _, _, high in units) >= 4 and not hits_air and any(r in COMBAT_ROLES for r in row["roles"]):
            issues.add("info", "team_no_anti_air", "combat team of 4 or more units with nothing that can hit aircraft", where)
        if len(speeds) > 1 and max(speeds) > 2 * min(speeds):
            issues.add("info", "team_speed_spread", "fastest unit is more than twice as fast as the slowest", where)
        if not units:
            issues.add("info", "team_empty", "team without units", where)
        team_rows.append(row)

    # ---- named values in scripts
    refs: list[tuple[str, str, str, str]] = []
    for entry in dump.get("players", []):
        if entry.get("player") == player:
            _walk_params(entry, "", refs)
    buttons = set(doc.get("buttons", []))
    for script, _, kind, value in refs:
        where = f"script {script}"
        if kind == "UPGRADE" and value not in doc["upgrades"]:
            issues.add("info", "ref_upgrade_foreign", f"{value} is not an upgrade of the faction", where)
        elif kind == "SPECIAL_POWER" and value not in doc["special_powers"]:
            issues.add("warning", "ref_special_power_foreign", f"{value} is not a special power of the faction", where)
        elif kind == "SCIENCE" and value not in doc["sciences"]:
            issues.add("warning", "ref_science_foreign", f"{value} is not a science of the faction", where)
        elif kind in COMMAND_BUTTON_PARAMS and value not in buttons:
            issues.add("warning", "ref_button_foreign", f"no type of the faction has the button {value}", where)

    # ---- what the faction has but the AI never uses
    def usable(obj: dict[str, Any]) -> bool:
        return "tier" in obj and not obj.get("unavailable") and obj.get("ai_use") != "never"

    unused_units = sorted(n for n, o in objects.items()
                          if o["category"] != "structure" and o.get("combat") and usable(o) and n not in used_types)
    never_built = sorted(n for n, o in objects.items()
                         if o["category"] == "structure" and usable(o) and n not in standing) if has_build_scripts else []
    mentioned = {value for _, _, kind, value in refs if kind == "UPGRADE"}
    unused_upgrades = sorted(n for n in doc["upgrades"] if n not in researched and n not in mentioned)

    counts, listed = issues.result()
    result: dict[str, Any] = {
        "player": player,
        "faction": doc["meta"]["faction"],
        "checked": {"teams": len(teams), "build_scripts": len(build_order.get("steps", [])),
                    "structures_built": len(built), "upgrades_researched": len(researched)},
        "issue_counts": counts,
        "issues": listed,
        "unused": {"combat_units_in_no_team": unused_units, "structures_never_built": never_built,
                   "upgrades_never_mentioned": unused_upgrades, "build_list_places_never_used": unplaced},
    }
    if not has_build_scripts:
        result["note"] = "No script of the player builds structures, so nothing was checked against a build order."
    if include_teams:
        result["teams"] = team_rows
    return result
