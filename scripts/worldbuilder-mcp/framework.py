"""The framework a set of AI players shares.

AI script sets are written as one skeleton repeated per faction: the same groups, scripts, flags,
timers and team roster, with the faction's types filled in. This module finds that skeleton by
comparing the players of a scripts.dump with each other: which scripts every player has and
where their parameters differ, which flags and counters they pass state through, which scripts
teams hook into, which object lists, waypoint paths and areas they expect. A new faction AI that
keeps to it plugs into the rest.
"""

from __future__ import annotations

import json
import os
import re
from collections import Counter
from typing import Any

TAG = "<TAG>"
PLAYER = "<PLAYER>"

SYMBOL_KINDS = {"FLAG": "flag", "COUNTER": "counter"}
MAP_KINDS = ("TRIGGER_AREA", "WAYPOINT", "WAYPOINT_PATH", "SKIRMISH_WAYPOINT_PATH")
SCRIPT_REF_KINDS = ("SCRIPT", "SCRIPT_SUBROUTINE")
TEAM_HOOKS = ("teamProductionCondition", "teamOnCreateScript", "teamOnIdleScript", "teamOnDestroyedScript",
              "teamEnemySightedScript", "teamAllClearScript", "teamOnUnitDestroyedScript", "teamGenericScriptHook0",
              "teamGenericScriptHook1", "teamGenericScriptHook2", "teamGenericScriptHook3", "teamGenericScriptHook4",
              "teamGenericScriptHook5", "teamGenericScriptHook6", "teamGenericScriptHook7")
TEAM_SETTINGS = ("teamProductionPriority", "teamMaxInstances", "teamIsSingleton", "teamExecutesActionsOnCreate",
                 "teamAggressiveness", "teamIsAIRecruitable", "teamIsBaseDefense", "teamIsPerimeterDefense",
                 "teamAutoReinforce", "teamInitialIdleFrames", "teamDestroyedThreshold")


def paths(directory: str) -> dict[str, str]:
    base = os.path.join(os.path.abspath(directory), "ai_framework")
    return {"generated": base + ".generated.json", "notes": base + ".notes.md"}


def _player_tag(teams: list[dict[str, Any]]) -> str:
    """The word a player's own names start with (team 'SEC Attack 1' -> SEC)."""
    words = Counter(t["teamName"].split(" ")[0] for t in teams if " " in t["teamName"])
    if not words:
        return ""
    word, count = words.most_common(1)[0]
    return word if count * 2 >= len(teams) else ""


class _Names:
    """Takes what is particular to one player out of names, so that players can be compared."""

    def __init__(self, player: str, tag: str) -> None:
        self.player = player
        self.tag = tag
        self.tag_word = re.compile(r"(?<![A-Za-z0-9])" + re.escape(tag) + r"(?![A-Za-z0-9])") if tag else None

    def __call__(self, text: str) -> str:
        if text == self.player:
            return PLAYER
        if self.tag_word:
            text = self.tag_word.sub(TAG, text)
        return text


def _items(script: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    """The conditions and actions of a script with a label for each."""
    found = []
    for c, clause in enumerate(script.get("conditions", [])):
        for i, condition in enumerate(clause):
            found.append((f"condition {c + 1}.{i + 1}", condition))
    for i, action in enumerate(script.get("actions", [])):
        found.append((f"action {i + 1}", action))
    for i, action in enumerate(script.get("false_actions", [])):
        found.append((f"false action {i + 1}", action))
    return found


def _shape(script: dict[str, Any]) -> tuple:
    return (tuple(tuple(c["type"] for c in clause) for clause in script.get("conditions", [])),
            tuple(a["type"] for a in script.get("actions", [])),
            tuple(a["type"] for a in script.get("false_actions", [])),
            script.get("subroutine", False))


def _flags(script: dict[str, Any]) -> dict[str, Any]:
    flags = {k: script[k] for k in ("active", "one_shot", "subroutine") if k in script}
    flags["difficulty"] = "".join(letter for key, letter in (("easy", "E"), ("normal", "N"), ("hard", "H")) if script.get(key, True))
    if script.get("delay_seconds"):
        flags["delay_seconds"] = script["delay_seconds"]
    return flags


def _player_scripts(entry: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    """(group name, script) for every script of a player, in evaluation order."""
    found = [("", s) for s in entry.get("scripts", [])]
    for group in entry.get("groups", []):
        found.extend((group["name"], s) for s in group.get("scripts", []))
    return found


def _template(script: dict[str, Any], names: _Names, holes: dict[tuple[str, int], int]) -> dict[str, Any]:
    """A script with the player's own names taken out and the varying parameters as ${n}."""
    out: dict[str, Any] = {"name": names(script["name"])}
    out.update({k: script[k] for k in ("active", "one_shot", "subroutine", "easy", "normal", "hard", "delay_seconds") if k in script})

    def convert(label: str, item: dict[str, Any]) -> dict[str, Any]:
        params = []
        for index, param in enumerate(item.get("params", [])):
            value = param.get("value")
            if (label, index) in holes:
                value = "${%d}" % holes[(label, index)]
            elif isinstance(value, str):
                value = names(value)
            params.append({"type": param["type"], "value": value})
        return {"type": item["type"], "params": params}

    out["conditions"] = [[convert(f"condition {c + 1}.{i + 1}", cond) for i, cond in enumerate(clause)]
                         for c, clause in enumerate(script.get("conditions", []))]
    out["actions"] = [convert(f"action {i + 1}", a) for i, a in enumerate(script.get("actions", []))]
    if script.get("false_actions"):
        out["false_actions"] = [convert(f"false action {i + 1}", a) for i, a in enumerate(script["false_actions"])]
    return out


def extract(dump: dict[str, Any], players: list[str] | None = None) -> dict[str, Any]:
    """The framework shared by the players of a dump (all of them, or the named ones)."""
    entries = [e for e in dump["players"] if (players is None or e["player"] in players)
               and (e.get("scripts") or any(g.get("scripts") for g in e.get("groups", [])))]
    if len(entries) < 2:
        raise ValueError("a framework needs at least two players with scripts to compare")
    order = [e["player"] for e in entries]
    total = len(order)
    teams_of = {p: [t for t in dump.get("teams", []) if t.get("teamOwner") == p] for p in order}
    names = {p: _Names(p, _player_tag(teams_of[p])) for p in order}

    # ---- scripts by normalized name
    scripts: dict[str, dict[str, tuple[str, dict[str, Any]]]] = {}
    duplicates: dict[str, list[str]] = {}
    group_layout: dict[str, dict[str, int]] = {}
    group_order: list[str] = []
    for entry in entries:
        player = entry["player"]
        norm = names[player]
        for group in entry.get("groups", []):
            key = norm(group["name"])
            if key not in group_layout:
                group_layout[key] = {}
                group_order.append(key)
            group_layout[key][player] = len(group.get("scripts", []))
        for group, script in _player_scripts(entry):
            key = norm(script["name"])
            per_player = scripts.setdefault(key, {})
            if player in per_player:
                duplicates.setdefault(key, []).append(player)
            else:
                per_player[player] = (norm(group), script)

    classes: dict[str, dict[str, Any]] = {}
    for key, per_player in scripts.items():
        present = [p for p in order if p in per_player]
        info: dict[str, Any] = {"group": Counter(g for g, _ in per_player.values()).most_common(1)[0][0]}
        if len(present) < total:
            info["players"] = present
        if len(present) == 1:
            info["class"] = "specific"
            classes[key] = info
            continue
        shapes = Counter(_shape(per_player[p][1]) for p in present)
        if len(shapes) > 1:
            info["class"] = "divergent" if len(present) == total else "partial"
            if len(present) == total:
                common = shapes.most_common(1)[0][0]
                info["differs_in"] = [p for p in present if _shape(per_player[p][1]) != common]
            classes[key] = info
            continue

        # Same structure everywhere: find the parameters that differ.
        reference = per_player[present[0]][1]
        varies = []
        holes: dict[tuple[str, int], int] = {}
        per_item = {p: _items(per_player[p][1]) for p in present}
        for position, (label, item) in enumerate(per_item[present[0]]):
            for index, param in enumerate(item.get("params", [])):
                values = {}
                for p in present:
                    other = per_item[p][position][1].get("params", [])
                    value = other[index].get("value") if index < len(other) else None
                    values[p] = names[p](value) if isinstance(value, str) else value
                if len({json.dumps(v, sort_keys=True) for v in values.values()}) > 1:
                    holes[(label, index)] = len(varies)
                    varies.append({"at": f"{label}, parameter {index + 1}", "of": item["type"], "type": param["type"], "values": values})
        flag_sets = {p: _flags(per_player[p][1]) for p in present}
        if len({json.dumps(f, sort_keys=True) for f in flag_sets.values()}) > 1:
            info["flags_differ"] = {p: f for p, f in flag_sets.items()}
        if len(present) < total:
            info["class"] = "partial"
        else:
            info["class"] = "parameterized" if varies else "framework"
        info.update(_flags(reference))
        if varies:
            info["varies"] = varies
        info["template"] = _template(reference, names[present[0]], holes)
        classes[key] = info

    # ---- flags, counters and timers: who writes, who reads
    symbols: dict[tuple[str, str], dict[str, Any]] = {}
    script_refs: dict[str, dict[str, set[str]]] = {}
    map_names: dict[tuple[str, str], set[str]] = {}
    lists: dict[str, dict[str, list[str]]] = {}
    priority_sets: dict[str, set[str]] = {}
    for entry in entries:
        player = entry["player"]
        norm = names[player]
        for _, script in _player_scripts(entry):
            owner = norm(script["name"])
            for label, item in _items(script):
                reading = label.startswith("condition")
                params = item.get("params", [])
                if item["type"] == "OBJECTLIST_ADDOBJECTTYPE" and len(params) >= 2:
                    members = lists.setdefault(norm(str(params[0].get("value"))), {}).setdefault(player, [])
                    if params[1].get("value") not in members:
                        members.append(params[1].get("value"))
                for param in params:
                    value = param.get("value")
                    if not isinstance(value, str) or not value:
                        continue
                    kind = param["type"]
                    if kind in SYMBOL_KINDS:
                        symbol = symbols.setdefault((SYMBOL_KINDS[kind], norm(value)), {"players": set(), "written_in": set(), "read_in": set(), "timer": False})
                        symbol["players"].add(player)
                        symbol["read_in" if reading else "written_in"].add(owner)
                        if "TIMER" in item["type"]:
                            symbol["timer"] = True
                    elif kind in SCRIPT_REF_KINDS:
                        ref = script_refs.setdefault(norm(value), {"players": set(), "from": set(), "how": set()})
                        ref["players"].add(player)
                        ref["from"].add(owner)
                        ref["how"].add(item["type"])
                    elif kind in MAP_KINDS:
                        map_names.setdefault((kind, value), set()).add(player)
                    elif kind == "ATTACK_PRIORITY_SET":
                        priority_sets.setdefault(norm(value), set()).add(player)

    def scope(count: int) -> str:
        return "all" if count == total else ("one" if count == 1 else "some")

    symbol_list = []
    for (kind, name), data in sorted(symbols.items()):
        row: dict[str, Any] = {"name": name, "kind": "timer" if data["timer"] else kind, "players": scope(len(data["players"])),
                               "written_in": sorted(data["written_in"]), "read_in": sorted(data["read_in"])}
        if len(data["players"]) not in (1, total):
            row["player_names"] = [p for p in order if p in data["players"]]
        if not data["read_in"]:
            row["note"] = "written but never read"
        elif not data["written_in"]:
            row["note"] = "read but never written by a script"
        symbol_list.append(row)

    # ---- teams: the roster and the scripts they hook into
    roster: dict[str, dict[str, dict[str, Any]]] = {}
    hooks: dict[tuple[str, str], dict[str, int]] = {}
    for player in order:
        norm = names[player]
        for team in teams_of[player]:
            roster.setdefault(norm(team["teamName"]), {})[player] = team
            for hook in TEAM_HOOKS:
                if team.get(hook):
                    per = hooks.setdefault((hook, norm(team[hook])), {})
                    per[player] = per.get(player, 0) + 1
    team_list = []
    for name, per_player in sorted(roster.items()):
        present = [p for p in order if p in per_player]
        reference = per_player[present[0]]
        row = {"name": name, "players": scope(len(present))}
        if len(present) not in (1, total):
            row["player_names"] = present
        for key in TEAM_SETTINGS:
            values = {p: per_player[p].get(key) for p in present}
            if any(v is not None for v in values.values()):
                row[key] = reference.get(key) if len({json.dumps(v) for v in values.values()}) == 1 else values
        row["hooks"] = {h: names[present[0]](reference[h]) for h in TEAM_HOOKS if reference.get(h)}
        row["units"] = {p: [f"{t.get(f'teamUnitMinCount{i}', 0)}-{t.get(f'teamUnitMaxCount{i}', 0)} {t[f'teamUnitType{i}']}"
                            for i in range(1, 8) if t.get(f"teamUnitType{i}") and t.get(f"teamUnitMaxCount{i}", 0) > 0]
                        for p, t in per_player.items()}
        team_list.append(row)
    hook_list = [{"hook": hook, "script": script, "players": scope(len(per)), "teams": sum(per.values()),
                  "script_class": classes.get(script, {}).get("class", "missing")}
                 for (hook, script), per in sorted(hooks.items())]

    by_class = Counter(info["class"] for info in classes.values())
    return {
        "meta": {
            "note": "Generated by ai_framework_generate by comparing the players' scripts. Do not edit; notes go into ai_framework.notes.md.",
            "players": order,
            "tags": {p: names[p].tag for p in order},
            "placeholders": {TAG: "the word a player's own teams, groups and scripts start with", PLAYER: "the player's name"},
            "script_classes": dict(by_class),
            "duplicate_script_names": {k: v for k, v in sorted(duplicates.items())},
        },
        "groups": [{"name": g, "players": scope(len(group_layout[g])), "scripts": group_layout[g]} for g in group_order],
        "scripts": classes,
        "symbols": symbol_list,
        "script_calls": [{"script": name, "players": scope(len(ref["players"])), "how": sorted(ref["how"]), "from": sorted(ref["from"]),
                          "script_class": classes.get(name, {}).get("class", "missing")}
                         for name, ref in sorted(script_refs.items())],
        "team_hooks": hook_list,
        "teams": team_list,
        "object_lists": [{"name": name, "players": scope(len(per)), "members": per} for name, per in sorted(lists.items())],
        "map_names": [{"kind": kind, "name": name, "players": scope(len(who))} for (kind, name), who in sorted(map_names.items())],
        "attack_priority_sets": [{"name": name, "players": scope(len(who))} for name, who in sorted(priority_sets.items())],
    }


def notes_skeleton() -> str:
    return ("# AI framework notes\n\n"
            "What the generated comparison cannot know: why the skeleton is built the way it is. Free text;\n"
            "ai_framework_get shows it with the overview.\n\n"
            "## How an attack wave is started\n\n## How difficulty is switched\n\n## What every faction must provide\n\n"
            "## What a faction may add\n\n## Known quirks\n")


def generate(dump: dict[str, Any], directory: str, players: list[str] | None = None) -> dict[str, Any]:
    where = paths(directory)
    os.makedirs(os.path.dirname(where["generated"]), exist_ok=True)
    doc = extract(dump, players)
    with open(where["generated"], "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
    result: dict[str, Any] = {"generated": where["generated"], "bytes": os.path.getsize(where["generated"])}
    if not os.path.exists(where["notes"]):
        with open(where["notes"], "w", encoding="utf-8", newline="\n") as f:
            f.write(notes_skeleton())
        result["notes_created"] = where["notes"]
    result.update(view(doc, "overview"))
    return result


def load(directory: str) -> dict[str, Any]:
    where = paths(directory)
    if not os.path.exists(where["generated"]):
        raise FileNotFoundError(f"no AI framework in {os.path.dirname(where['generated'])}; run ai_framework_generate first")
    with open(where["generated"], encoding="utf-8") as f:
        doc = json.load(f)
    if os.path.exists(where["notes"]):
        with open(where["notes"], encoding="utf-8") as f:
            doc["notes"] = f.read()
    return doc


def _matches(text: str, pattern: str | None) -> bool:
    return pattern is None or pattern.lower() in text.lower()


def view(doc: dict[str, Any], what: str, filter: str | None = None, names: list[str] | None = None) -> dict[str, Any]:
    """One slice of the framework (see ai_framework_get)."""
    if what == "overview":
        result = {"players": doc["meta"]["players"], "tags": doc["meta"]["tags"], "placeholders": doc["meta"]["placeholders"],
                  "script_classes": doc["meta"]["script_classes"],
                  "groups": [{"name": g["name"], "players": g["players"], "scripts": max(g["scripts"].values())} for g in doc["groups"]],
                  "counts": {"symbols_shared": sum(1 for s in doc["symbols"] if s["players"] == "all"),
                             "symbols_total": len(doc["symbols"]),
                             "teams_shared": sum(1 for t in doc["teams"] if t["players"] == "all"), "teams_total": len(doc["teams"]),
                             "team_hook_scripts_shared": len({h["script"] for h in doc["team_hooks"] if h["players"] == "all"}),
                             "object_lists": len(doc["object_lists"]), "map_names": len(doc["map_names"])}}
        if doc["meta"]["duplicate_script_names"]:
            result["duplicate_script_names"] = len(doc["meta"]["duplicate_script_names"])
        if doc.get("notes"):
            result["notes"] = doc["notes"]
        return result
    if what == "scripts":
        if filter is None:
            # Too many to list: how many of each class every group holds.
            table: dict[str, dict[str, int]] = {}
            for info in doc["scripts"].values():
                row = table.setdefault(info["group"], {})
                row[info["class"]] = row.get(info["class"], 0) + 1
            return {"total": len(doc["scripts"]), "classes": doc["meta"]["script_classes"],
                    "by_group": [dict(group=g, **table[g]) for g in sorted(table)],
                    "hint": "filter by a class (framework, parameterized, partial, divergent, specific), a group or part of a name"}
        rows = []
        for name, info in doc["scripts"].items():
            if not (filter == info["class"] or _matches(name, filter) or _matches(info["group"], filter)):
                continue
            row = {"name": name, "group": info["group"], "class": info["class"]}
            if "varies" in info:
                row["varies"] = [f"{v['type']} in {v['of']}" for v in info["varies"]]
            for key in ("players", "differs_in"):
                if key in info:
                    row[key] = info[key]
            rows.append(row)
        return {"total": len(rows), "scripts": rows}
    if what == "script":
        if not names:
            raise ValueError("view \"script\" needs names")
        return {"scripts": [dict(doc["scripts"][n], name=n) for n in names if n in doc["scripts"]],
                "unknown": [n for n in names if n not in doc["scripts"]]}
    if what == "symbols":
        if filter is None:
            shared = [{"name": s["name"], "kind": s["kind"], "written_in": len(s["written_in"]), "read_in": len(s["read_in"]),
                       **({"note": s["note"]} if "note" in s else {})} for s in doc["symbols"] if s["players"] == "all"]
            odd = [{"name": s["name"], "kind": s["kind"], "players": s["players"], "note": s["note"]}
                   for s in doc["symbols"] if "note" in s and s["players"] != "all"]
            return {"shared_by_all": shared, "suspicious_elsewhere": odd, "total": len(doc["symbols"]),
                    "hint": "filter by part of a name, a kind (flag, counter, timer) or all/some/one for the scripts that write and read"}
        return {"symbols": [s for s in doc["symbols"] if filter in (s["kind"], s["players"]) or _matches(s["name"], filter)]}
    if what == "teams":
        if filter is None:
            return {"shared_by_all": [{k: t[k] for k in ("name", "teamProductionPriority", "teamMaxInstances", "hooks") if k in t}
                                      for t in doc["teams"] if t["players"] == "all"],
                    "total": len(doc["teams"]),
                    "hint": "filter by part of a name or all/some/one for every setting and the units per player"}
        return {"teams": [t for t in doc["teams"] if filter == t["players"] or _matches(t["name"], filter)]}
    if what in ("team_hooks", "script_calls"):
        if filter is None:
            return {what: [h for h in doc[what] if h["players"] == "all"], "total": len(doc[what]),
                    "hint": "these are shared by all players; filter by part of a script name or some/one for the rest"}
        return {what: [h for h in doc[what] if filter == h["players"] or _matches(h["script"], filter)]}
    if what == "object_lists":
        if filter is None:
            return {"object_lists": [{"name": e["name"], "players": e["players"],
                                      "members": max(len(m) for m in e["members"].values())} for e in doc["object_lists"]],
                    "hint": "filter by part of a name for the members per player"}
        return {"object_lists": [e for e in doc["object_lists"] if _matches(e["name"], filter)]}
    if what in ("map_names", "attack_priority_sets"):
        return {what: [e for e in doc[what] if _matches(e["name"], filter)]}
    if what == "groups":
        return {"groups": doc["groups"]}
    raise ValueError("view must be overview, groups, scripts, script, symbols, teams, team_hooks, script_calls, object_lists, "
                     "map_names or attack_priority_sets")
