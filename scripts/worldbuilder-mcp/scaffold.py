"""Deriving the AI of one faction from that of another.

Takes the scripts and teams of an existing AI player and rewrites them for another faction: the
shared framework is kept as it is, everything that names a type, upgrade, science, special power
or command button of the source faction is translated to its counterpart in the target faction.
The counterparts come from comparing the two faction descriptions (see faction.py); what has
none is reported instead of guessed.
"""

from __future__ import annotations

import copy
import re
from typing import Any

from framework import SCRIPT_REF_KINDS, TEAM_HOOKS, _player_tag

# General prefixes of type names: Tank_ChinaTankGattling, SecW_..., Z_SpecW_... -> ChinaTankGattling.
PREFIX = re.compile(r"^(?:[A-Za-z0-9]{1,5}_)+")
BUTTON_PARAMS = ("COMMANDBUTTON", "COMMANDBUTTON_ABILITY", "COMMANDBUTTON_ALL_ABILITIES")
BUTTON_KEYS = ("builds", "upgrades", "special_powers", "abilities")


def base_name(name: str) -> str:
    stripped = PREFIX.sub("", name)
    return stripped or name


def _by_base(names: list[str]) -> dict[str, str]:
    """Base name -> name, for the base names that are unambiguous."""
    seen: dict[str, list[str]] = {}
    for name in names:
        seen.setdefault(base_name(name).lower(), []).append(name)
    return {base: found[0] for base, found in seen.items() if len(found) == 1}


def _slot_pairs(src: dict[str, Any], dst: dict[str, Any]) -> dict[str, str]:
    pairs = {}
    for slot, value in src["slots"].items():
        other = dst["slots"].get(slot)
        if isinstance(value, str) and isinstance(other, str):
            pairs[value] = other
    return pairs


def map_names(src: dict[str, Any], dst: dict[str, Any], overrides: dict[str, str] | None = None) -> dict[str, Any]:
    """Counterparts in the target faction for the things of the source faction, with the reason for each."""
    overrides = dict(dst.get("counterparts", {}), **(overrides or {}))
    objects: dict[str, dict[str, str]] = {}
    unmapped: list[dict[str, Any]] = []
    dst_objects = dst["objects"]
    dst_base = _by_base(list(dst_objects))
    slots = _slot_pairs(src, dst)

    src_names = list(src["objects"]) + [o["name"] for o in src["other_objects"]]
    for name in src_names:
        obj = src["objects"].get(name)
        if name in overrides:
            objects[name] = {"to": overrides[name], "by": "override"}
        elif name in dst_objects:
            objects[name] = {"to": name, "by": "same"}
        elif base_name(name).lower() in dst_base:
            objects[name] = {"to": dst_base[base_name(name).lower()], "by": "name"}
        elif name in slots:
            objects[name] = {"to": slots[name], "by": "slot"}
        elif obj is not None:
            # Nearest type with the same job: role and category must agree.
            same_job = [o for o in dst_objects.values() if o["role"] == obj["role"] and o["category"] == obj["category"]
                        and bool(o.get("naval")) == bool(obj.get("naval")) and not o.get("unavailable") and o.get("ai_use") != "never"]
            if same_job:
                best = min(same_job, key=lambda o: (abs(o.get("tier", 9) - obj.get("tier", 9)), abs(o["cost"] - obj["cost"]), o["name"]))
                objects[name] = {"to": best["name"], "by": "role"}
            else:
                unmapped.append({"kind": "object", "name": name, "role": obj["role"], "category": obj["category"]})
        else:
            unmapped.append({"kind": "object", "name": name})

    def simple(kind: str, key: str) -> dict[str, dict[str, str]]:
        found = {}
        base = _by_base(list(dst[key]))
        for name in src[key]:
            if name in overrides:
                found[name] = {"to": overrides[name], "by": "override"}
            elif name in dst[key]:
                found[name] = {"to": name, "by": "same"}
            elif base_name(name).lower() in base:
                found[name] = {"to": base[base_name(name).lower()], "by": "name"}
            else:
                unmapped.append({"kind": kind, "name": name})
        return found

    upgrades = simple("upgrade", "upgrades")
    sciences = simple("science", "sciences")
    powers = simple("special_power", "special_powers")

    # Buttons: the same button, or the one on the counterpart type that does the counterpart thing.
    buttons: dict[str, dict[str, str]] = {}
    dst_buttons = set(dst.get("buttons", []))
    button_base = _by_base(sorted(dst_buttons))
    for name, obj in src["objects"].items():
        target = dst_objects.get(objects.get(name, {}).get("to", ""))
        for key in BUTTON_KEYS:
            for entry in obj.get(key, []):
                button = entry["button"]
                if button in buttons:
                    continue
                if button in overrides:
                    buttons[button] = {"to": overrides[button], "by": "override"}
                    continue
                if button in dst_buttons:
                    buttons[button] = {"to": button, "by": "same"}
                    continue
                match = None
                for other in (target or {}).get(key, []):
                    if key == "builds" and objects.get(entry["object"], {}).get("to") == other["object"]:
                        match = other
                    elif key == "upgrades" and upgrades.get(entry["upgrade"], {}).get("to") == other["upgrade"]:
                        match = other
                    elif key == "special_powers" and powers.get(entry.get("special_power", ""), {}).get("to") == other.get("special_power"):
                        match = other
                    elif key == "abilities" and entry.get("type") == other.get("type") and entry.get("options") == other.get("options"):
                        match = match or other
                if match:
                    buttons[button] = {"to": match["button"], "by": "counterpart"}
                elif base_name(button).lower() in button_base:
                    buttons[button] = {"to": button_base[base_name(button).lower()], "by": "name"}
                else:
                    unmapped.append({"kind": "button", "name": button, "on": name})

    def tally(found: dict[str, dict[str, str]]) -> dict[str, int]:
        counts: dict[str, int] = {}
        for entry in found.values():
            counts[entry["by"]] = counts.get(entry["by"], 0) + 1
        return counts

    return {"from": src["meta"]["faction"], "to": dst["meta"]["faction"],
            "objects": objects, "upgrades": upgrades, "sciences": sciences, "special_powers": powers, "buttons": buttons,
            "unmapped": unmapped,
            "summary": {"objects": tally(objects), "upgrades": tally(upgrades), "sciences": tally(sciences),
                        "special_powers": tally(powers), "buttons": tally(buttons), "unmapped": len(unmapped)}}


class _Translator:
    """Rewrites the values of one player's scripts and teams for another faction."""

    def __init__(self, mapping: dict[str, Any], src: dict[str, Any], renames: dict[str, str], old_player: str, new_player: str) -> None:
        self.by_kind = {"OBJECT_TYPE": mapping["objects"], "UPGRADE": mapping["upgrades"], "SCIENCE": mapping["sciences"],
                        "SPECIAL_POWER": mapping["special_powers"]}
        for kind in BUTTON_PARAMS:
            self.by_kind[kind] = mapping["buttons"]
        # What belongs to the source faction; anything else (enemy types, lists) is left alone.
        self.own = {"OBJECT_TYPE": set(src["objects"]) | {o["name"] for o in src["other_objects"]},
                    "UPGRADE": set(src["upgrades"]), "SCIENCE": set(src["sciences"]), "SPECIAL_POWER": set(src["special_powers"])}
        for kind in BUTTON_PARAMS:
            self.own[kind] = set(src.get("buttons", []))
        self.renames = renames
        self.old_player = old_player
        self.new_player = new_player
        self.unmapped: dict[tuple[str, str], list[str]] = {}
        self.seen: set[str] = set()
        self.translated = 0

    def value(self, kind: str, value: Any, where: str, translate: bool) -> Any:
        if not isinstance(value, str):
            return value
        if kind in SCRIPT_REF_KINDS or kind == "TEAM":
            return self.renames.get(value, value)
        if kind == "SIDE" and value == self.old_player:
            return self.new_player
        if not translate or kind not in self.by_kind or value not in self.own[kind]:
            return value
        self.seen.add(value)
        entry = self.by_kind[kind].get(value)
        if entry is None:
            places = self.unmapped.setdefault((kind, value), [])
            if where not in places:
                places.append(where)
            return value
        if entry["to"] != value:
            self.translated += 1
        return entry["to"]

    def unit(self, name: str, where: str) -> str:
        return self.value("OBJECT_TYPE", name, where, True)


def _retag(name: str, old_tag: str, new_tag: str) -> str:
    if not old_tag:
        return name
    return re.sub(r"(?<![A-Za-z0-9])" + re.escape(old_tag) + r"(?![A-Za-z0-9])", new_tag, name)


def scaffold(dump: dict[str, Any], framework: dict[str, Any], src: dict[str, Any], dst: dict[str, Any],
             like_player: str, new_player: str, tag: str, mapping: dict[str, Any],
             drop_unmapped: bool = False) -> dict[str, Any]:
    """A scripts.load document for new_player, derived from like_player, and a report on it."""
    entry = next((e for e in dump["players"] if e["player"] == like_player), None)
    if entry is None:
        raise ValueError(f"the map has no player '{like_player}'")
    teams = [t for t in dump.get("teams", []) if t.get("teamOwner") == like_player]
    old_tag = framework["meta"]["tags"].get(like_player) or _player_tag(teams)
    if not re.fullmatch(r"[A-Za-z0-9]{2,8}", tag):
        raise ValueError("tag must be 2 to 8 letters or digits; it starts the names of the new player's teams and groups")
    taken = {t["teamName"] for t in dump.get("teams", []) if t.get("teamOwner") != new_player}

    # Names: the tag is swapped; a team name without it gets the new tag in front, as team names are unique in a map.
    renames: dict[str, str] = {}
    for team in teams:
        new_name = _retag(team["teamName"], old_tag, tag)
        if new_name == team["teamName"] or new_name in taken:
            new_name = f"{tag} {team['teamName']}"
        renames[team["teamName"]] = new_name
    all_scripts = [(None, s) for s in entry.get("scripts", [])] + [(g, s) for g in entry.get("groups", []) for s in g.get("scripts", [])]
    for _, script in all_scripts:
        new_name = _retag(script["name"], old_tag, tag)
        if new_name != script["name"]:
            renames[script["name"]] = new_name

    translator = _Translator(mapping, src, renames, like_player, new_player)
    classes = framework["scripts"]
    counts = {"kept_as_framework": 0, "parameters_filled": 0, "translated": 0, "dropped": 0}
    dropped: list[str] = []

    def norm(name: str) -> str:
        return _retag(name, old_tag, "<TAG>")

    def convert(script: dict[str, Any]) -> dict[str, Any] | None:
        info = classes.get(norm(script["name"]), {})
        kind = info.get("class", "specific")
        # A framework script is the same for every faction, lists of enemy types included.
        translate_all = kind not in ("framework", "parameterized")
        holes = set()
        if kind == "parameterized":
            for vary in info.get("varies", []):
                holes.add(vary["at"])
        out = copy.deepcopy(script)
        out["name"] = renames.get(script["name"], script["name"])
        before = len(translator.unmapped)
        where = f"script {out['name']}"

        def items(label: str, item: dict[str, Any]) -> None:
            item.pop("text", None)
            for index, param in enumerate(item.get("params", [])):
                do = translate_all or f"{label}, parameter {index + 1}" in holes
                param["value"] = translator.value(param["type"], param.get("value"), where, do)

        for c, clause in enumerate(out.get("conditions", [])):
            for i, condition in enumerate(clause):
                items(f"condition {c + 1}.{i + 1}", condition)
        for i, action in enumerate(out.get("actions", [])):
            items(f"action {i + 1}", action)
        for i, action in enumerate(out.get("false_actions", [])):
            items(f"false action {i + 1}", action)
        if drop_unmapped and len(translator.unmapped) > before:
            counts["dropped"] += 1
            dropped.append(where)
            return None
        counts["kept_as_framework" if kind == "framework" else ("parameters_filled" if kind == "parameterized" else "translated")] += 1
        return out

    new_entry: dict[str, Any] = {"player": new_player,
                                 "properties": dict(entry.get("properties", {}), playerName=new_player, playerDisplayName=new_player,
                                                    playerFaction=dst["meta"]["faction"]),
                                 "scripts": [s for s in (convert(s) for s in entry.get("scripts", [])) if s],
                                 "groups": []}
    for group in entry.get("groups", []):
        new_group = {k: v for k, v in group.items() if k != "scripts"}
        new_group["name"] = _retag(group["name"], old_tag, tag)
        new_group["scripts"] = [s for s in (convert(s) for s in group.get("scripts", [])) if s]
        new_entry["groups"].append(new_group)

    new_teams = []
    for team in teams:
        out = dict(team)
        out["teamName"] = renames[team["teamName"]]
        out["teamOwner"] = new_player
        where = f"team {out['teamName']}"
        before = len(translator.unmapped)
        for slot in range(1, 8):
            key = f"teamUnitType{slot}"
            if out.get(key) and out[key] != "<none>" and out.get(f"teamUnitMaxCount{slot}", 0) > 0:
                out[key] = translator.unit(out[key], where)
        for hook in TEAM_HOOKS:
            if out.get(hook):
                out[hook] = renames.get(out[hook], out[hook])
        if drop_unmapped and len(translator.unmapped) > before:
            counts["dropped"] += 1
            dropped.append(where)
            continue
        new_teams.append(out)

    unmapped = [{"kind": kind, "name": name, "count": len(places), "where": places[:6]}
                for (kind, name), places in sorted(translator.unmapped.items())]
    by_role = sorted(f"{name} -> {m['to']}" for name, m in mapping["objects"].items() if m["by"] == "role" and name in translator.seen)
    report = {
        "player": new_player, "faction": dst["meta"]["faction"], "derived_from": like_player, "tag": tag, "source_tag": old_tag,
        "scripts": sum(len(g["scripts"]) for g in new_entry["groups"]) + len(new_entry["scripts"]),
        "teams": len(new_teams),
        "script_counts": counts,
        "values_translated": translator.translated,
        "unmapped": unmapped,
        "mapped_by_role_only": by_role,
    }
    if dropped:
        report["dropped"] = dropped[:40]
    document = {"format": dump.get("format", "worldbuilder-scripts-1"), "players": [new_entry], "teams": new_teams}
    return {"document": document, "report": report}
