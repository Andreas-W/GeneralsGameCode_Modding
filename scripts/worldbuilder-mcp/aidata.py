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
    lines = text.split(newline)
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
            return newline.join(lines + [""] + new_lines + [""]), False
        return newline.join(lines[:closing] + new_lines + [""] + lines[closing:]), False
    # The comment line the export writes above the block belongs to it.
    if start > 0 and lines[start - 1].strip().lower().startswith(";skirmish ai build list"):
        start -= 1
    return newline.join(lines[:start] + new_lines + lines[end + 1:]), True


#-------------------------------------------------------------------------------------------------
# Editing a SideInfo block in place
#-------------------------------------------------------------------------------------------------

GATHERER_KEYS = {"easy": "ResourceGatherersEasy", "normal": "ResourceGatherersNormal", "hard": "ResourceGatherersHard"}
MAX_SKILL_SETS = 5
MAX_SKILL_SET_SCIENCES = 35     # MAX_AI_UPGRADES of the engine: the slots of the science purchase screens
SCIENCE_LINE = re.compile(r"^(\s*)Science(\s*)=(\s*)(\S+)(.*)$", re.IGNORECASE)


def _code(line: str) -> str:
    return line.split(";", 1)[0].strip()


def _find_block(lines: list[str], name: str, arg: str | None, first: int, last: int) -> tuple[int, int] | None:
    """Line numbers of the header and the End of a block inside lines[first:last], or None."""
    index = first
    while index < last:
        parts = _code(lines[index]).split()
        if parts and "=" not in _code(lines[index]) and parts[0].lower() == name.lower() and (
                arg is None or (len(parts) > 1 and parts[1].lower() == arg.lower())):
            depth = 1
            for end in range(index + 1, last):
                code = _code(lines[end])
                if not code or "=" in code:
                    continue
                depth += -1 if code.lower() == "end" else 1
                if depth == 0:
                    return index, end
            return None
        index += 1
    return None


def _indent(line: str) -> str:
    return line[:len(line) - len(line.lstrip())]


def _set_field(lines: list[str], start: int, end: int, key: str, value: Any) -> int:
    """Sets a field of the block lines[start:end], keeping the line's layout and comment. Returns the lines added."""
    pattern = re.compile(r"^(\s*" + re.escape(key) + r"\s*=\s*)(\S+)(.*)$", re.IGNORECASE)
    last_field = start
    depth = 0
    for index in range(start + 1, end):
        code = _code(lines[index])
        if depth == 0:
            match = pattern.match(lines[index])
            if match and not lines[index].lstrip().startswith(";"):
                # Keep a comment where it was when the value changes its length.
                tail = match.group(3)
                pad = len(match.group(2)) - len(str(value))
                if tail.strip() and pad > 0:
                    tail = " " * pad + tail
                lines[index] = f"{match.group(1)}{value}{tail}"
                return 0
            if "=" in code:
                last_field = index
        if code and "=" not in code:
            depth += -1 if code.lower() == "end" else 1
    indent = _indent(lines[last_field]) if last_field > start else _indent(lines[start]) + "  "
    lines.insert(last_field + 1, f"{indent}{key} = {value}")
    return 1


def _set_sciences(lines: list[str], start: int, end: int, wanted: list[str]) -> None:
    """Brings the Science lines of the block lines[start:end] to the wanted list with the fewest line changes."""
    import difflib

    positions = [i for i in range(start + 1, end) if SCIENCE_LINE.match(lines[i]) and not lines[i].lstrip().startswith(";")]
    current = [SCIENCE_LINE.match(lines[i]).group(4) for i in positions]
    indent = _indent(lines[positions[0]]) if positions else _indent(lines[start]) + "  "
    remove: list[int] = []
    insert: list[tuple[int, list[str]]] = []      # (after this line, new lines)
    for op, a1, a2, b1, b2 in difflib.SequenceMatcher(None, current, wanted, autojunk=False).get_opcodes():
        if op == "equal":
            continue
        reuse = min(a2 - a1, b2 - b1) if op == "replace" else 0
        for k in range(reuse):
            match = SCIENCE_LINE.match(lines[positions[a1 + k]])
            # The comment on the line described the science that stood there.
            lines[positions[a1 + k]] = f"{match.group(1)}Science{match.group(2)}={match.group(3)}{wanted[b1 + k]}"
        remove.extend(positions[a1 + reuse:a2])
        added = [f"{indent}Science = {name}" for name in wanted[b1 + reuse:b2]]
        if added:
            if a1 + reuse > 0:
                anchor = positions[a1 + reuse - 1]
            else:
                # In front of the first science, below the comments that head the list.
                anchor = positions[0] - 1 if positions else start
            insert.append((anchor, added))
    for anchor, added in sorted(insert, reverse=True):
        lines[anchor + 1:anchor + 1] = added
        remove = [r + len(added) if r > anchor else r for r in remove]
    for index in sorted(remove, reverse=True):
        del lines[index]


def edit_side_info(text: str, side: str, gatherers: dict[str, int] | None = None, base_defense: str | None = None,
                   skill_sets: dict[int, list[str]] | None = None, copy_from: str | None = None) -> dict[str, Any]:
    """Changes the SideInfo of a side in an AIData.ini text line by line, so that comments and
    layout stay. A side without a block gets a new one, copied from copy_from if given.
    Returns {text, block, created}."""
    newline = "\r\n" if "\r\n" in text else "\n"
    # Split on the file's own line ending, so that every untouched line comes back as it was.
    lines = text.split(newline)
    found = _find_block(lines, "SideInfo", side, 0, len(lines))
    created = False
    if found is None:
        created = True
        if copy_from:
            source = _find_block(lines, "SideInfo", copy_from, 0, len(lines))
            if source is None:
                raise ValueError(f"AIData.ini has no SideInfo {copy_from} to copy from")
            block = lines[source[0]:source[1] + 1]
            block[0] = re.sub(r"(?i)(SideInfo\s+)\S+", lambda m: m.group(1) + side, block[0], count=1)
        else:
            block = ["  SideInfo " + side, "    ResourceGatherersEasy = 2", "    ResourceGatherersNormal = 2",
                     "    ResourceGatherersHard = 2", "  End"]
        # After the last SideInfo; without any, in front of the End that closes AIData.
        after = None
        position = 0
        while True:
            other = _find_block(lines, "SideInfo", None, position, len(lines))
            if other is None:
                break
            after, position = other[1], other[1] + 1
        if after is None:
            closing = next((i for i in range(len(lines) - 1, -1, -1) if _code(lines[i]).lower() == "end"), len(lines))
            after = closing - 1
        lines[after + 1:after + 1] = [""] + block
        found = _find_block(lines, "SideInfo", side, 0, len(lines))

    start, end = found
    for level, value in (gatherers or {}).items():
        if level not in GATHERER_KEYS:
            raise ValueError("gatherers takes easy, normal and hard")
        end += _set_field(lines, start, end, GATHERER_KEYS[level], int(value))
    if base_defense is not None:
        end += _set_field(lines, start, end, "BaseDefenseStructure1", base_defense)
    for number, wanted in sorted((skill_sets or {}).items()):
        number = int(number)
        if not 1 <= number <= MAX_SKILL_SETS:
            raise ValueError(f"skill sets are numbered 1 to {MAX_SKILL_SETS}")
        start, end = _find_block(lines, "SideInfo", side, 0, len(lines))
        inner = _find_block(lines, f"SkillSet{number}", None, start + 1, end)
        if inner is None:
            indent = _indent(lines[start]) + "  "
            lines[end:end] = [f"{indent}SkillSet{number}", f"{indent}End"]
            inner = (end, end + 1)
        _set_sciences(lines, inner[0], inner[1], list(wanted))
    start, end = _find_block(lines, "SideInfo", side, 0, len(lines))
    return {"text": newline.join(lines), "block": "\n".join(lines[start:end + 1]) + "\n", "created": created}
