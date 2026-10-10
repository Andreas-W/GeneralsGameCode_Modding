"""AIData.ini: the base layouts and side settings of the skirmish AI.

A build script only says that a structure is to be built. Where it goes comes from the
SkirmishBuildList of the player's side in AIData.ini: a layout made on a map with WorldBuilder's
Build List tool, which the game moves to the player's start position. A structure without an
entry there is never built. The SideInfo blocks hold what else the AI is given per side: how many
gatherers to keep, its base defense structure and the orders in which it buys sciences.
"""

from __future__ import annotations

import re
from typing import Any

FILES = ("Data\\INI\\Default\\AIData.ini", "Data\\INI\\AIData.ini")
LOCATION = re.compile(r"X:\s*(-?[0-9.]+)\s+Y:\s*(-?[0-9.]+)", re.IGNORECASE)


def _blocks(text: str) -> dict[str, Any]:
    """The nested blocks of an INI text. A block is {name, arg, fields: {key: [values]}, blocks: [...]}."""
    root: dict[str, Any] = {"name": "", "arg": "", "fields": {}, "blocks": []}
    stack = [root]
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            stack[-1]["fields"].setdefault(key.strip(), []).append(value.strip())
        elif line.lower() == "end":
            if len(stack) > 1:
                stack.pop()
        else:
            parts = line.split(None, 1)
            block = {"name": parts[0], "arg": parts[1].strip() if len(parts) > 1 else "", "fields": {}, "blocks": []}
            stack[-1]["blocks"].append(block)
            stack.append(block)
    return root


def _walk(block: dict[str, Any], name: str) -> list[dict[str, Any]]:
    found = [b for b in block["blocks"] if b["name"].lower() == name.lower()]
    for child in block["blocks"]:
        if child["name"].lower() != name.lower():
            found.extend(_walk(child, name))
    return found


def _first(block: dict[str, Any], key: str, default: Any = None) -> Any:
    for name, values in block["fields"].items():
        if name.lower() == key.lower():
            return values[0]
    return default


def _number(text: Any) -> Any:
    try:
        value = float(text)
    except (TypeError, ValueError):
        return text
    return int(value) if value == int(value) else value


def _yes(text: Any) -> bool:
    return str(text).strip().lower() in ("yes", "true", "1")


def parse(text: str) -> dict[str, Any]:
    """Settings, side infos and build lists of one AIData.ini text."""
    root = _blocks(text)
    result: dict[str, Any] = {"settings": {}, "sides": {}, "build_lists": {}}
    for block in _walk(root, "AIData"):
        for key, values in block["fields"].items():
            result["settings"][key] = _number(values[0]) if values[0].lower() not in ("yes", "no") else _yes(values[0])
    for block in _walk(root, "SideInfo"):
        side: dict[str, Any] = {"gatherers": {level: _number(_first(block, "ResourceGatherers" + level.capitalize(), 0))
                                              for level in ("easy", "normal", "hard")}}
        defense = _first(block, "BaseDefenseStructure1")
        if defense:
            side["base_defense"] = defense
        sets = sorted((b for b in block["blocks"] if b["name"].lower().startswith("skillset")), key=lambda b: b["name"])
        side["skill_sets"] = [[v for key, values in b["fields"].items() if key.lower() == "science" for v in values] for b in sets]
        result["sides"][block["arg"]] = side
    for block in _walk(root, "SkirmishBuildList"):
        entries = []
        for structure in block["blocks"]:
            if structure["name"].lower() != "structure":
                continue
            entry: dict[str, Any] = {"template": structure["arg"]}
            where = LOCATION.search(_first(structure, "Location", ""))
            entry["x"], entry["y"] = (float(where.group(1)), float(where.group(2))) if where else (0.0, 0.0)
            entry["angle_deg"] = float(_number(_first(structure, "Angle", 0)))
            rebuilds = _number(_first(structure, "Rebuilds", 0))
            entry["rebuilds"] = "unlimited" if rebuilds == -1 else rebuilds
            entry["initially_built"] = _yes(_first(structure, "InitiallyBuilt", "No"))
            if _yes(_first(structure, "AutomaticallyBuild", "No")):
                entry["automatically_build"] = True
            name = _first(structure, "Name")
            if name:
                entry["name"] = name
            entries.append(entry)
        result["build_lists"][block["arg"]] = entries
    return result


def merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    """Later files replace settings, side infos and build lists of the same name."""
    for key in ("settings", "sides", "build_lists"):
        base[key].update(override[key])
    return base


def for_side(data: dict[str, Any], side: str) -> dict[str, Any]:
    """What AIData.ini gives one side, as stored with its faction description."""
    def find(table: dict[str, Any]) -> Any:
        return next((value for name, value in table.items() if name.lower() == side.lower()), None)

    result: dict[str, Any] = {"side": side}
    info = find(data["sides"])
    if info is not None:
        result.update(info)
    entries = find(data["build_lists"])
    if entries is not None:
        counts: dict[str, int] = {}
        for entry in entries:
            counts[entry["template"]] = counts.get(entry["template"], 0) + 1
        result["build_list"] = counts
        result["build_list_entries"] = len(entries)
    return result


def format_build_list(side: str, entries: list[dict[str, Any]]) -> str:
    """A SkirmishBuildList block as WorldBuilder's Build List export writes it."""
    lines = [";Skirmish AI Build List", f"SkirmishBuildList {side}"]
    for entry in entries:
        rebuilds = entry.get("rebuilds", 0)
        lines.append(f"  Structure {entry['template']}")
        if entry.get("name"):
            lines.append(f"    Name = {entry['name']}")
        lines.append(f"    Location = X:{entry['x']:.2f} Y:{entry['y']:.2f}")
        lines.append(f"    Rebuilds = {-1 if rebuilds == 'unlimited' else int(rebuilds)}")
        lines.append(f"    Angle = {entry.get('angle_deg', 0):.2f}")
        lines.append(f"    InitiallyBuilt = {'Yes' if entry.get('initially_built') else 'No'}")
        lines.append(f"    AutomaticallyBuild = {'Yes' if entry.get('automatically_build') else 'No'}")
        lines.append(f"  END ;Structure {entry['template']}")
    lines.append(f"END ;SkirmishBuildList {side}")
    return "\n".join(lines) + "\n"


def replace_build_list(text: str, side: str, block: str) -> tuple[str, bool]:
    """The INI text with the build list of a side replaced, or added at the end. Returns (text, replaced)."""
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    start = end = None
    depth = 0
    for index, raw in enumerate(lines):
        line = raw.split(";", 1)[0].strip()
        if start is None:
            parts = line.split()
            if len(parts) == 2 and parts[0].lower() == "skirmishbuildlist" and parts[1].lower() == side.lower():
                start = index
                depth = 1
            continue
        if not line or "=" in line:
            continue
        if line.lower() == "end":
            depth -= 1
            if depth == 0:
                end = index
                break
        else:
            depth += 1
    new_lines = block.rstrip("\n").split("\n")
    if start is None or end is None:
        # Build lists are part of the AIData block: a new one goes in front of the End that closes it.
        closing = next((i for i in range(len(lines) - 1, -1, -1) if lines[i].split(";", 1)[0].strip().lower() == "end"), None)
        if closing is None:
            return newline.join(lines + [""] + new_lines) + newline, False
        return newline.join(lines[:closing] + new_lines + [""] + lines[closing:]) + newline, False
    # The comment line the export writes above the block belongs to it.
    if start > 0 and lines[start - 1].strip().lower().startswith(";skirmish ai build list"):
        start -= 1
    return newline.join(lines[:start] + new_lines + lines[end + 1:]) + newline, True
