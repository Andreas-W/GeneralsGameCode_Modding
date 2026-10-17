"""AI matches: running the game's -aiMatch mode and reading the log it writes.

The game plays a skirmish between AI players by itself and writes one JSON record per line: the
players, a snapshot every few seconds (money, power, objects by type, teams), events (a team is
started, a science or upgrade arrives, a script runs for the first time, the script engine
reports something) and a summary. This module turns that into what one wants to know about an
AI: what it built when, what it never did, where it stalled.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from typing import Any

GAME_EXE = "generalszh.exe"
TROUBLE = re.compile(r"\*\*\*|error|unable|could not|not found|not defined|invalid|warning|fail|missing", re.IGNORECASE)
RANK_SCIENCE = re.compile(r"^SCIENCE_Rank\d+$")


def command_line(map_name: str, players: list[str], log: str, minutes: float, seed: int, interval: float,
                 cash: int | None, width: int, height: int, scripts: str | None = None, ai_data: str | None = None,
                 headless: bool = False) -> list[str]:
    screen = ["-headless"] if headless else ["-win", "-xres", str(width), "-yres", str(height)]
    args = screen + ["-noaudio", "-quickstart", "-ignoreAsserts",
            "-aiMatch", map_name, "-aiPlayers", ",".join(players), "-aiMatchLog", log,
            "-aiMatchMinutes", str(minutes), "-aiMatchInterval", str(interval), "-seed", str(seed)]
    if cash:
        args += ["-aiMatchCash", str(cash)]
    if scripts:
        args += ["-aiMatchScripts", scripts]
    if ai_data:
        args += ["-aiMatchAIData", ai_data]
    return args


def run(game_dir: str, map_name: str, players: list[str], log: str, minutes: float = 15, seed: int = 1,
        interval: float = 10, cash: int | None = None, wait_seconds: float = 240, width: int = 1024,
        height: int = 768, scripts: str | None = None, ai_data: str | None = None, headless: bool = True) -> dict[str, Any]:
    """Starts a match and waits up to wait_seconds for it to end."""
    exe = os.path.join(game_dir, GAME_EXE)
    if not os.path.exists(exe):
        raise FileNotFoundError(f"{exe} not found")
    os.makedirs(os.path.dirname(log), exist_ok=True)
    if os.path.exists(log):
        os.remove(log)
    for label, path in (("script file", scripts), ("AIData file", ai_data)):
        if path and not os.path.exists(path):
            raise FileNotFoundError(f"{label} {path} not found")
    process = subprocess.Popen([exe] + command_line(map_name, players, log, minutes, seed, interval, cash, width, height, scripts, ai_data,
                                                    headless), cwd=game_dir)
    started = time.time()
    result: dict[str, Any] = {"log": log, "pid": process.pid}
    try:
        process.wait(timeout=wait_seconds)
        result["status"] = "finished"
        result["exit_code"] = process.returncode
    except subprocess.TimeoutExpired:
        result["status"] = "running"
    result["wall_seconds"] = round(time.time() - started, 1)
    return result


def read(log: str) -> list[dict[str, Any]]:
    records = []
    with open(log, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                # The game is still writing the last line.
                break
    return records


def _seconds(frame: int, fps: int) -> float:
    return round(frame / fps, 1)


def _player_keys(snaps: list[dict[str, Any]], name: str) -> tuple[str, str]:
    """(script player, name suffix) of a match player: the game copies the scripts of
    'SkirmishChina' to the player and puts its index behind every script and team name."""
    for snap in snaps:
        for entry in snap["players"]:
            if entry["name"] != name:
                continue
            for team in entry["teams"]:
                found = re.fullmatch(r"team(Skirmish.*?)(\d+)", team)
                if found:
                    return found.group(1), found.group(2)
    return "", ""


def _strip(name: str, suffix: str) -> str:
    return name[:-len(suffix)] if suffix and name.endswith(suffix) else name


def _trouble(messages: list[dict[str, Any]], suffix: str, fps: int, limit: int) -> list[dict[str, Any]]:
    """The messages that speak of a problem, the most frequent first, with the player's name suffix taken out."""
    found = sorted((m for m in messages if TROUBLE.search(m["text"])), key=lambda m: -m["count"])
    tidy = re.compile(r"(?<=\S)" + re.escape(suffix) + r"(?=\s|'|$)") if suffix else None
    return [{"text": tidy.sub("", m["text"]) if tidy else m["text"], "count": m["count"], "first_at": _seconds(m["first"], fps)}
            for m in found[:limit]]


def report(records: list[dict[str, Any]], dump: dict[str, Any] | None = None, factions: dict[str, dict[str, Any]] | None = None,
           only_player: str | None = None, limit: int = 25) -> dict[str, Any]:
    """What happened in a match, per player. dump is a scripts.dump of the AI set the match used
    (to name what never ran); factions maps faction names to their descriptions."""
    by_type: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        by_type.setdefault(record["t"], []).append(record)
    if "error" in by_type:
        return {"status": "error", "reason": by_type["error"][0]["reason"]}
    if "setup" not in by_type:
        return {"status": "no match in this log"}
    setup = by_type["setup"][0]
    fps = setup.get("frames_per_second", 30)
    snaps = by_type.get("snap", [])
    end = by_type.get("end", [None])[0]
    result: dict[str, Any] = {"status": "finished" if end else ("running" if "start" in by_type else "starting"),
                              "map": setup["map"], "seed": setup["seed"]}
    for key in ("scripts", "ai_data"):
        if setup.get(key):
            result[key] = setup[key]
    if not snaps or "start" not in by_type:
        return result
    last_frame = end["frame"] if end else snaps[-1]["frame"]
    result["game_minutes"] = round(last_frame / fps / 60, 1)
    if end:
        result["reason"] = end["reason"]
        result["winners"] = end["winners"]

    fired_by_player = {r["player"]: r["scripts"] for r in by_type.get("scripts", [])}
    # What the script engine and the AI reported; while the match runs only the first of each is known.
    messages = by_type["messages"][0]["messages"] if "messages" in by_type else []
    if not messages:
        seen: dict[tuple[str, str], dict[str, Any]] = {}
        for event in by_type.get("message", []):
            seen.setdefault((event.get("player", ""), event["text"]),
                            {"player": event.get("player", ""), "text": event["text"], "count": 1, "first": event["frame"]})
        messages = list(seen.values())
    players = []
    for start in by_type["start"][0]["players"]:
        name = start["name"]
        script_player, suffix = _player_keys(snaps, name)
        if only_player and only_player not in (name, script_player, start["faction"]):
            continue
        series = [next(p for p in snap["players"] if p["name"] == name) for snap in snaps]
        frames = [snap["frame"] for snap in snaps]
        final = series[-1]
        doc = (factions or {}).get(start["faction"])
        objects = doc["objects"] if doc else {}
        info: dict[str, Any] = {"name": name, "faction": start["faction"], "scripts_of": script_player, "difficulty": start["difficulty"],
                                "alive": final["alive"], "score": final["score"], "money_end": final["money"], "rank_end": final["rank"]}

        # First time every type stood on the map.
        first_seen: dict[str, int] = {}
        for frame, entry in zip(frames, series):
            for kind in entry["objects"]:
                first_seen.setdefault(kind, frame)
        ordered = sorted(first_seen.items(), key=lambda kv: kv[1])
        built = next((p["built"] for p in end["players"] if p["name"] == name), {}) if end else {}
        lost = next((p["lost"] for p in end["players"] if p["name"] == name), {}) if end else {}

        def is_structure(kind: str) -> bool:
            return objects[kind]["category"] == "structure" if kind in objects else kind in built and "Infantry" not in kind

        info["structures"] = [{"type": k, "first_at": _seconds(f, fps), "built": built.get(k, 0), "lost": lost.get(k, 0)}
                              for k, f in ordered if (objects.get(k, {}).get("category") == "structure") or (not objects and is_structure(k))]
        info["units"] = [{"type": k, "first_at": _seconds(f, fps), "built": built.get(k, 0), "lost": lost.get(k, 0)}
                         for k, f in ordered if k in objects and objects[k]["category"] != "structure"]
        foreign = [k for k, _ in ordered if objects and k not in objects]
        if foreign:
            info["other_types_owned"] = foreign[:limit]

        if doc:
            usable = {n: o for n, o in objects.items() if "tier" in o and not o.get("unavailable") and o.get("ai_use") != "never"}
            info["never_built"] = {
                "structures": sorted(n for n, o in usable.items() if o["category"] == "structure" and n not in first_seen and n not in built),
                "units": sorted(n for n, o in usable.items() if o["category"] != "structure" and o.get("combat")
                                and n not in first_seen and n not in built)}

        # Economy.
        low_power = next((f for f, e in zip(frames, series) if e["power"][0] < e["power"][1]), None)
        info["economy"] = {"money_min": min(e["money"] for e in series), "money_max": max(e["money"] for e in series),
                           "money_earned": final["score"]["money_earned"], "money_spent": final["score"]["money_spent"]}
        if low_power is not None:
            info["economy"]["low_power_from"] = _seconds(low_power, fps)
        # Longest time without a new unit or structure.
        longest = (0, 0)
        since = frames[0]
        made = series[0]["score"]["units_built"] + series[0]["score"]["buildings_built"]
        for frame, entry in zip(frames, series):
            now = entry["score"]["units_built"] + entry["score"]["buildings_built"]
            if now != made:
                made, since = now, frame
            elif frame - since > longest[1] - longest[0]:
                longest = (since, frame)
        if longest[1] - longest[0] >= 60 * fps:
            info["longest_pause"] = {"from": _seconds(longest[0], fps), "to": _seconds(longest[1], fps),
                                     "note": "nothing was built in this time"}

        # Teams, sciences, upgrades.
        team_counts: dict[str, int] = {}
        team_first: dict[str, int] = {}
        for event in by_type.get("team", []):
            if event["player"] == name:
                team = _strip(event["team"], suffix)
                team_counts[team] = team_counts.get(team, 0) + 1
                team_first.setdefault(team, event["frame"])
        info["teams"] = [{"team": t, "started": c, "first_at": _seconds(team_first[t], fps)}
                         for t, c in sorted(team_counts.items(), key=lambda kv: team_first[kv[0]])]
        info["sciences"] = [{"science": e["science"], "at": _seconds(e["frame"], fps)} for e in by_type.get("science", [])
                            if e["player"] == name and not RANK_SCIENCE.match(e["science"])]
        info["upgrades"] = [{"upgrade": e["upgrade"], "at": _seconds(e["frame"], fps)} for e in by_type.get("upgrade", []) if e["player"] == name]

        fired = {_strip(n, suffix): v for n, v in fired_by_player.get(name, {}).items()}
        info["scripts_run"] = len(fired) if end else sum(1 for e in by_type.get("script", []) if e["player"] == name)

        # Against the script set: what was there to run and never did.
        entry = next((e for e in (dump or {}).get("players", []) if e["player"] == script_player), None) if script_player else None
        if entry and end:
            level = {"easy": "easy", "normal": "normal", "hard": "hard"}[start["difficulty"]]
            never_ran = []
            for group in [{"name": "", "active": True, "scripts": entry.get("scripts", [])}] + entry.get("groups", []):
                for script in group.get("scripts", []):
                    if (script.get("active", True) and group.get("active", True) and script.get(level, True)
                            and not script.get("subroutine") and script.get("actions") and script["name"] not in fired):
                        never_ran.append({"script": script["name"], "group": group["name"]})
            teams_defined = [t["teamName"] for t in dump.get("teams", []) if t.get("teamOwner") == script_player]
            never_started = sorted(t for t in teams_defined if t not in team_counts and not t.startswith("team"))
            info["never_ran"] = {"scripts": len(never_ran), "scripts_first": never_ran[:limit],
                                 "teams": len(never_started), "teams_of": len(teams_defined), "teams_first": never_started[:limit]}
            if len(info["teams"]) > limit:
                info["teams"] = info["teams"][:limit] + [{"more": len(team_counts) - limit}]
        info["trouble"] = _trouble([m for m in messages if m.get("player") == name], suffix, fps, limit)
        players.append(info)
    result["players"] = players

    # What nobody can be blamed for: reported outside of any player's scripts or AI.
    names = {start["name"] for start in by_type["start"][0]["players"]}
    result["trouble"] = _trouble([m for m in messages if m.get("player", "") not in names], "", fps, limit)
    result["messages_total"] = len(messages)
    return result
