"""End-to-end check of the WorldBuilder command bridge, without MCP.

Start WorldBuilderZH.exe -mcp, then run:  python smoke_test.py [--keep]
It creates a new map (discarding unsaved changes in the open one), edits it, saves it to a temp
folder, reopens it and checks the result.
"""

from __future__ import annotations

import os
import sys
import tempfile

from wb_bridge import WorldBuilderBridge, WorldBuilderError


def main() -> int:
    wb = WorldBuilderBridge()
    commands = {c["name"] for c in wb.call("list_commands")["commands"]}
    print(f"{len(commands)} bridge commands")

    info = wb.call("map.new", width=96, height=96, initial_height=20, border=10, discard_changes=True)
    print("new map:", info["heightmap"], info["world"])

    r = wb.call("terrain.brush", op="raise", x=300, y=300, radius=80, feather=60, amount=40)
    assert r["changed"], r
    sample = wb.call("terrain.sample", x=300, y=300)
    assert sample["height_raw"] == 60, sample
    wb.call("terrain.brush", op="smooth", x=300, y=300, radius=150, iterations=3)

    wb.call("view.set_camera", x=300, y=300, reset=True)
    before = wb.call("terrain.sample", x=300, y=300)["height_raw"]
    bench = wb.call("perf.stroke_bench", tool="height", x0=250, y0=300, x1=350, y1=300, steps=10, width=6, feather=2, amount=120, undo=False)
    assert bench["steps"] == 10, bench
    assert wb.call("terrain.sample", x=300, y=300)["height_raw"] != before, "stroke did not change the terrain"
    wb.call("edit.undo")
    assert wb.call("terrain.sample", x=300, y=300)["height_raw"] == before, "undo did not restore the terrain"
    # On flat ground: while dragging, the view tracks the mouse on a fixed plane, not on the terrain.
    wb.call("view.set_camera", x=700, y=700, reset=True)
    flat = wb.call("terrain.sample", x=700, y=700)["height_raw"]
    wb.call("perf.stroke_bench", tool="height", x0=650, y0=700, x1=750, y1=700, steps=10, width=6, feather=2, amount=120, undo=False, via_messages=True)
    assert wb.call("terrain.sample", x=700, y=700)["height_raw"] != flat, "stroke by window messages did not change the terrain"
    wb.call("edit.undo")
    assert wb.call("terrain.sample", x=700, y=700)["height_raw"] == flat, "undo did not restore the terrain"

    textures = wb.call("terrain.list_textures")["textures"]
    assert textures, "no textures loaded"
    wb.call("terrain.paint_texture", texture=textures[0]["name"], x=600, y=600, radius=100)

    player = wb.call("sides.add_player", faction="FactionAmerica")
    team = player["default_team"]
    sides = wb.call("sides.list")
    assert any(t["name"] == team for t in sides["teams"]), sides

    templates = wb.call("objects.list_templates", filter="AmericaBarracks", limit=5)["templates"]
    assert templates, "AmericaBarracks template not found"
    barracks = wb.call("objects.place", template=templates[0]["name"], x=300, y=300, angle_deg=45, owner=team)
    trees = wb.call("objects.list_templates", editor_sorting="SHRUBBERY", limit=1)["templates"]
    if trees:
        wb.call("objects.place", template=trees[0]["name"], x=500, y=200)
    wb.call("objects.modify", ids=[barracks["id"]], dx=20, rotate_deg=45)
    moved = wb.call("objects.get", id=barracks["id"])
    assert abs(moved["x"] - 320) < 0.01 and abs(moved["angle_deg"] - 90) < 0.1, moved
    wb.call("objects.set_properties", ids=[barracks["id"]], name="MyBarracks", properties={"objectInitialHealth": 50})

    wb.call("waypoints.add", x=100, y=100, name="Player_1_Start")
    wb.call("waypoints.add", x=200, y=100, name="PathA")
    wb.call("waypoints.add", x=300, y=150, name="PathB", link_from="PathA")
    wp = wb.call("waypoints.list")
    assert len(wp["waypoints"]) == 3 and len(wp["links"]) == 1, wp

    wb.call("polygons.add", kind="water", points=[[700, 100], [900, 100], [900, 300], [700, 300]], water_height=20)
    wb.call("polygons.add", kind="area", name="BaseArea", points=[[250, 250], [350, 250], [350, 350], [250, 350]])

    roads = wb.call("roads.list_types")["roads"]
    if roads:
        wb.call("roads.add", type=roads[0], points=[[50, 500], [300, 500], [400, 700]])

    # Start positions: two players land on the diagonal, replacing the existing start; then undo.
    wp_before = len(wb.call("waypoints.list")["waypoints"])
    gs = wb.call("map.generate_starts", players=2, base_radius=150)
    assert len(gs["starts"]) == 2 and gs["starts_replaced"] == 1, gs
    s1, s2 = gs["starts"]
    assert abs(s1["x"] + s2["x"] - 950) < 1 and abs(s1["y"] + s2["y"] - 950) < 1, gs
    assert len(wb.call("waypoints.list")["waypoints"]) == wp_before + 1
    wb.call("edit.undo")
    assert len(wb.call("waypoints.list")["waypoints"]) == wp_before

    # Skirmish AI data: needs two starts, so add a second one first.
    wb.call("waypoints.add", x=800, y=800, name="Player_2_Start")
    assert not wb.call("ai.skirmish_check")["ready"]
    setup = wb.call("ai.skirmish_setup", inner_radius=120, outer_radius=220)
    assert setup["waypoints_created"] == 30, setup
    check = wb.call("ai.skirmish_check")
    assert check["ready"], check
    wb.call("edit.undo", count=2)

    # Scatter decoration: must respect the road and stay deterministic for a seed.
    if trees:
        objects_before = wb.call("map.info")["counts"]["objects"]
        sc = wb.call("objects.scatter", templates=[trees[0]["name"]], count=25, seed=3, start_clearance=60)
        assert 0 < sc["placed"] <= 25, sc
        assert wb.call("map.info")["counts"]["objects"] == objects_before + sc["placed"]
        # The scattered objects are added as one chain; each must still get its own unique ID.
        ids = [o["unique_id"] for o in wb.call("objects.list", limit=1000)["objects"]]
        assert len(ids) == len(set(ids)), sorted(i for i in set(ids) if ids.count(i) > 1)
        wb.call("edit.undo")
        assert wb.call("map.info")["counts"]["objects"] == objects_before

    # Routed road: must build segments, optionally flatten, and undo as one step.
    if roads:
        road_points = wb.call("map.info")["counts"]["road_points"]
        rt = wb.call("roads.route", type=roads[0], to=[880, 620], **{"from": [60, 600]})
        assert rt["segments"] >= 1 and rt["length"] > 800, rt
        assert wb.call("map.info")["counts"]["road_points"] == road_points + 2 * rt["segments"]
        wb.call("edit.undo")
        assert wb.call("map.info")["counts"]["road_points"] == road_points

    # Usage and bulk replace: swap a texture and an object template, then undo.
    usage = wb.call("map.usage")
    old_template = templates[0]["name"]
    assert any(o["template"] == old_template and o["known"] for o in usage["objects"]), usage
    structures = wb.call("objects.list_templates", editor_sorting="STRUCTURE", limit=5)["templates"]
    new_template = next(t["name"] for t in structures if t["name"] != old_template)
    old_tex, new_tex = textures[0]["name"], textures[2]["name"]
    dry = wb.call("map.replace", textures={old_tex: new_tex}, dry_run=True)
    assert dry["textures"][0]["cells"] > 0 and not dry["changed"], dry
    rep = wb.call("map.replace", textures={old_tex: new_tex}, objects={old_template: new_template})
    assert rep["changed"] and rep["objects"][0]["count"] == 1, rep
    after = wb.call("map.usage")
    assert not any(t["name"] == old_tex for t in after["textures"]), after["textures"]
    assert any(o["template"] == new_template for o in after["objects"]), after["objects"]
    wb.call("edit.undo")
    restored = wb.call("map.usage")
    assert any(t["name"] == old_tex for t in restored["textures"])
    assert any(o["template"] == old_template for o in restored["objects"])

    # Resize: grow on three sides, everything keeps its place on the terrain; then undo.
    info_before = wb.call("map.info")
    rs = wb.call("map.resize", add_left=20, add_bottom=10, add_right=4, fill_height=30,
                 fill_texture=textures[1]["name"])
    assert rs["changed"] and (rs["width"], rs["height"]) == (120, 106), rs
    assert rs["world_offset"] == {"x": 200, "y": 100} and rs["areas_refit"] == 1, rs
    strip = wb.call("terrain.sample", x=50, y=500)
    assert strip["height_raw"] == 30 and strip["texture"] == textures[1]["name"], strip
    grown = wb.call("map.info")
    assert grown["boundaries"][0] == {"width_cells": 120, "height_cells": 106}, grown
    assert wb.call("terrain.sample", x=500, y=400)["height_raw"] == 60
    moved = next(o for o in wb.call("objects.list")["objects"] if o.get("name") == "MyBarracks")
    assert abs(moved["x"] - 520) < 0.01 and abs(moved["y"] - 400) < 0.01, moved
    wb.call("edit.undo")
    assert wb.call("map.info") == info_before

    # Crop to the top right corner: what falls off the map is removed on request and comes back on undo.
    crop = wb.call("map.resize", width=40, height=40, anchor="top_right", remove_outside="all")
    assert crop["removed_waypoints"] == 3 and crop["removed_objects"] >= 1 and crop["areas_outside"] >= 2, crop
    assert not wb.call("waypoints.list")["links"]
    wb.call("edit.undo")
    assert wb.call("map.info") == info_before
    assert len(wb.call("waypoints.list")["links"]) == 1

    # Symmetry: mirror the west half onto the east half, then undo.
    before_wp = len(wb.call("waypoints.list")["waypoints"])
    sym = wb.call("map.symmetrize", mode="mirror_x", source="west")
    names = [w["waypoint_name"] for w in wb.call("waypoints.list")["waypoints"]]
    assert "Player_2_Start" in names, (sym, names)
    wb.call("map.transform", op="rotate_180")
    wb.call("edit.undo", count=2)
    assert len(wb.call("waypoints.list")["waypoints"]) == before_wp

    # Procedural terrain: generate, limit slopes, auto texture, then undo exactly what changed.
    steps = 0
    gen = wb.call("terrain.generate", seed=5, base_height=30, layers=[{"height": 60, "frequency": 0.03, "octaves": 3}])
    assert gen["changed"], gen
    steps += 1
    if wb.call("terrain.limit_slope", max_step=6)["changed"]:
        steps += 1
    tex = wb.call("terrain.auto_texture", seed=5, base={"texture": textures[0]["name"]},
                  cliff={"texture": textures[1]["name"], "slope": 5})
    assert tex["cells_ground"] > 0, tex
    steps += 1
    wb.call("terrain.blend_all")
    wb.call("terrain.remove_blends")
    steps += 2
    wb.call("edit.undo", count=steps)
    assert wb.call("terrain.sample", x=300, y=300)["height_raw"] == 60, "undo did not restore the terrain"

    # Heightmap image round trip must be lossless.
    out_png = os.path.join(tempfile.mkdtemp(prefix="wb_mcp_"), "height.png")
    wb.call("terrain.export_heightmap", path=out_png)
    wb.call("terrain.import_heightmap", path=out_png, fit="exact")
    again_png = out_png.replace("height.png", "height2.png")
    wb.call("terrain.export_heightmap", path=again_png)
    assert open(out_png, "rb").read() == open(again_png, "rb").read(), "heightmap round trip changed heights"
    wb.call("edit.undo")
    water = wb.call("terrain.export_mask", kind="water")
    assert water["cells_set"] > 0, water

    # Scripts: add, read back, update, groups, move, verify, team, export/import round trip.
    owner = player["player"]
    assert any(a["type"] == "SET_FLAG" for a in wb.call("scripts.list_actions", filter="SET_FLAG")["actions"])
    assert wb.call("scripts.list_conditions", filter="CONDITION_TRUE")["total_matches"] >= 1
    added = wb.call("scripts.add", player=owner, script={
        "name": "McpIntro", "comment": "smoke test",
        "actions": [{"type": "SET_FLAG", "params": ["McpFlag", True]}]})
    assert added["warnings"] == [], added
    got = wb.call("scripts.get", name="McpIntro")
    assert got["player"] == owner and got["script"]["conditions"][0][0]["type"] == "CONDITION_TRUE", got
    assert got["script"]["actions"][0]["params"] == [{"type": "FLAG", "value": "McpFlag"},
                                                     {"type": "BOOLEAN", "value": True}], got
    # What scripts.get reports is accepted back unchanged.
    wb.call("scripts.update", name="McpIntro", script={"actions": got["script"]["actions"], "one_shot": False})
    again = wb.call("scripts.get", name="McpIntro")["script"]
    assert again["actions"] == got["script"]["actions"] and again["one_shot"] is False, again
    made = wb.call("teams.set", name="McpStrike", owner=owner, units=[{"type": templates[0]["name"], "min": 0, "max": 2}],
                   properties={"teamProductionPriority": 500, "teamDestroyedThreshold": 1,
                               "teamOnCreateScript": "McpNoSuchScript"})
    assert made["properties"]["teamUnitMaxCount1"] == 2 and made["units"][0]["max"] == 2, made
    assert [w["property"] for w in made["warnings"]] == ["teamOnCreateScript"], made
    wb.call("teams.set", name="McpStrike", properties={"teamOnCreateScript": None})
    for bad in ({"teamProductionPriority": "high"}, {"teamProductionPriority": 1.5}, {"teamIsBaseDefense": 1},
                {"teamUnitType8": "x"}, {"teamNoSuchKey": 1}):
        try:
            wb.call("teams.set", name="McpStrike", properties=bad)
            raise AssertionError(f"team property {bad} must be rejected")
        except WorldBuilderError:
            pass
    assert wb.call("teams.verify", name="McpStrike")["warnings"] == []
    assert any(p["key"] == "teamGenericScriptHook<N>" and p["last"] == 15 for p in wb.call("teams.list_properties")["properties"])
    assert wb.call("scripts.list_values", param_type="COMPARISON")["values"][0] == "LESS_THAN"
    assert wb.call("scripts.list_values", param_type="UPGRADE")["total_matches"] > 10
    assert "free_form" in wb.call("scripts.list_values", param_type="COORD3D")
    assert "McpStrike" in wb.call("scripts.list_values", param_type="TEAM", filter="mcp")["values"]
    wb.call("scripts.add_group", player=owner, name="McpGroup")
    follow = wb.call("scripts.add", group="McpGroup", script={
        "name": "McpFollowUp",
        "conditions": [{"type": "FLAG", "params": ["McpFlag", True]}],
        "actions": [{"type": "TEAM_HUNT", "params": ["McpStrike"]}, {"type": "ENABLE_SCRIPT", "params": ["McpMissing"]}]})
    assert len(follow["warnings"]) == 1 and follow["warnings"][0]["where"] == "actions[1]", follow
    try:
        wb.call("scripts.add", player=owner, script={"name": "McpBad", "actions": [{"type": "SET_FLAG", "params": ["x"]}]})
        raise AssertionError("a wrong parameter count must be rejected")
    except WorldBuilderError:
        pass
    wb.call("scripts.move", name="McpIntro", to_group="McpGroup", index=0)
    listed = wb.call("scripts.list", player=owner)["players"][0]
    assert [s["name"] for s in listed["groups"][0]["scripts"]] == ["McpIntro", "McpFollowUp"], listed
    compact = wb.call("scripts.list", player=owner, detail="groups")["players"][0]["groups"][0]
    assert compact["script_count"] == 2 and "scripts" not in compact, compact
    assert wb.call("scripts.list", player=owner, group="McpGroup", detail="names")["players"][0]["groups"][0]["scripts"] == ["McpIntro", "McpFollowUp"]
    short = wb.call("teams.list", owner=owner, unit="barracks")["teams"]
    assert len(short) == 1 and short[0]["name"] == "McpStrike" and short[0]["units"][0].startswith("0-2 "), short
    assert len(wb.call("scripts.verify")["warnings"]) == 1
    uses = wb.call("scripts.find", value="McpStrike", param_type="TEAM")
    assert uses["total_scripts"] == 1 and uses["scripts"][0]["hits"][0]["where"] == "actions[0]", uses
    flagged = wb.call("scripts.find", text="mcpflag")
    assert flagged["total_scripts"] == 2 and flagged["total_hits"] == 2, flagged
    assert wb.call("scripts.find", value=templates[0]["name"])["teams"][0]["key"] == "teamUnitType1"
    # Copy, rename with references, clone to another player; then undo all of it.
    wb.call("scripts.copy", name="McpIntro", new_name="McpIntro2")
    wb.call("teams.copy", name="McpStrike", new_name="McpStrike2")
    ren = wb.call("scripts.rename", kind="FLAG", **{"from": "McpFlag"}, to="McpFlagB")
    assert ren["references_in_scripts"] == 3, ren
    ren = wb.call("scripts.rename", kind="team", **{"from": "McpStrike"}, to="McpStrikeB")
    assert ren["references_in_scripts"] == 1, ren
    wb.call("teams.set", name="McpStrikeB", properties={"teamOnCreateScript": "McpIntro"})
    ren = wb.call("scripts.rename", kind="script", **{"from": "McpIntro"}, to="McpStart")
    assert ren["renamed"] == 1 and ren["references_in_teams"] == 1, ren
    twin = wb.call("sides.add_player", faction="FactionChina")["player"]
    try:
        wb.call("scripts.clone_player", **{"from": owner}, to=twin)
        raise AssertionError("cloning without renaming the teams must fail")
    except WorldBuilderError:
        pass
    clone = wb.call("scripts.clone_player", **{"from": owner}, to=twin, replace={"Mcp": "Twin"})
    assert clone["scripts"] == 3 and clone["groups"] == 1 and clone["teams"] == 2, clone
    cloned = wb.call("scripts.get", name="TwinFollowUp", player=twin)["script"]
    assert cloned["actions"][0]["params"][0]["value"] == "TwinStrikeB", cloned
    assert wb.call("teams.get", name="TwinStrikeB")["properties"]["teamOnCreateScript"] == "TwinStart"
    wb.call("edit.undo", count=8)
    assert wb.call("scripts.get", name="McpIntro")["group"] == "McpGroup"
    # Batch: one undo step, and nothing at all if a command fails.
    count = wb.call("scripts.list")["script_count"]
    add = lambda n: {"cmd": "scripts.add", "args": {"player": owner, "script": {"name": n}}}
    try:
        wb.call("scripts.batch", commands=[add("McpB1"), add("McpB1")])
        raise AssertionError("a failing batch must be rejected")
    except WorldBuilderError as exc:
        assert "commands[1]" in str(exc), exc
    assert wb.call("scripts.list")["script_count"] == count
    batch = wb.call("scripts.batch", commands=[add("McpB1"), add("McpB2"),
                                                 {"cmd": "teams.set", "args": {"name": "McpBTeam", "owner": owner}}])
    assert batch["commands_run"] == 3 and wb.call("scripts.list")["script_count"] == count + 2
    wb.call("edit.undo")
    assert wb.call("scripts.list")["script_count"] == count
    assert not wb.call("teams.list", filter="McpBTeam")["teams"]

    # Dump and load are inverse.
    doc = wb.call("scripts.dump")
    wb.call("scripts.delete", name="McpFollowUp")
    wb.call("teams.delete", name="McpStrike")
    loaded = wb.call("scripts.load", data=doc)
    assert loaded["scripts"] == 2 and loaded["teams"] == 1, loaded
    assert wb.call("scripts.dump") == doc, "dump/load changed the scripts"
    wb.call("edit.undo", count=3)

    # Compare, bulk edits, team summary, missing references.
    same = wb.call("scripts.compare", player=owner, with_data=doc)
    assert same["scripts"]["identical"] == 2 and same["scripts"]["different"]["count"] == 0, same
    assert same["teams"]["identical"] == 1 and same["teams"]["different"]["count"] == 0, same
    wb.call("scripts.add", group="McpGroup", script={"name": "McpWait", "actions": [{"type": "SET_TIMER", "params": ["McpTimer", 100]}]})
    dry = wb.call("scripts.set_values", param_type="INT", type="SET_TIMER", op="multiply", value=1.5, dry_run=True)
    assert dry["parameters_changed"] == 1 and "150" in dry["samples"][0]["after"], dry
    assert wb.call("scripts.get", name="McpWait")["script"]["actions"][0]["params"][1]["value"] == 100
    wb.call("scripts.set_values", param_type="INT", type="SET_TIMER", op="multiply", value=1.5)
    wb.call("scripts.set_values", param_type="BOOLEAN", type="SET_FLAG", value=False, player=owner)
    diff = wb.call("scripts.compare", player=owner, with_data=doc)
    assert diff["scripts"]["only_in_a"]["items"] == ["McpGroup / McpWait"] and diff["scripts"]["different"]["count"] == 1, diff
    assert "FALSE" in diff["scripts"]["different"]["items"][0]["a"][0], diff
    many = wb.call("teams.set_many", owner=owner, multiply={"teamUnitMaxCount*": 2}, add={"teamProductionPriority": 100})
    assert many["teams_changed"] == 1 and many["samples"][0]["changes"]["teamUnitMaxCount1"] == [2, 4], many
    summary = wb.call("teams.summary", owner=owner)["owners"][0]
    assert summary["teams"] == 1 and summary["units"][0]["max_total"] == 4 and summary["team_size"]["max"] == 4, summary
    assert summary["priorities"] == [{"priority": "600", "teams": 1}], summary
    wb.call("teams.set", name="McpStrike", units=[{"type": templates[0]["name"] + "_Typo", "max": 1}])
    miss = wb.call("scripts.missing")
    by_name = {m["missing"]: m for m in miss["missing"]}
    assert templates[0]["name"] in by_name[templates[0]["name"] + "_Typo"]["suggestions"], miss
    assert by_name["McpMissing"]["rename_kind"] == "script_reference", miss
    fixed = wb.call("scripts.rename", kind="script_reference", **{"from": "McpMissing"}, to="McpIntro")
    assert fixed["references_in_scripts"] == 1, fixed
    wb.call("edit.undo", count=6)
    assert wb.call("scripts.compare", player=owner, with_data=doc)["scripts"]["different"]["count"] == 0

    # Views: trace, build order, difficulty, unit stats, build lists, verify summary.
    trace = wb.call("scripts.trace", name="McpIntro")
    assert trace["trace"]["leads_to"][0]["script"] == "McpFollowUp" and "McpFlag" in trace["trace"]["leads_to"][0]["via"], trace
    team_trace = wb.call("scripts.trace", name="McpStrike")
    assert team_trace["team"] == "McpStrike" and team_trace["scripts"] == [], team_trace
    wb.call("scripts.add", player=owner, script={"name": "McpBuild", "easy": False, "actions": [
        {"type": "SKIRMISH_BUILD_BUILDING", "params": [templates[0]["name"]]}]})
    order = wb.call("scripts.build_order", player=owner)
    assert order["steps"][0]["script"] == "McpBuild" and order["steps"][0]["difficulty"] == "NH", order
    assert order["totals"] == [{"what": templates[0]["name"], "build_actions": 1}], order
    assert wb.call("scripts.build_order", player=owner, difficulty="E")["steps"] == []
    switches = wb.call("scripts.by_difficulty", player=owner)["script_switches"]
    assert any(s["runs_on"] == "NH" and s["names"] == ["McpBuild"] for s in switches), switches
    stats = wb.call("objects.info", templates=[templates[0]["name"], "NoSuchThing"])
    assert stats["templates"][0]["cost"] > 0 and "STRUCTURE" in stats["templates"][0]["kind_of"], stats
    assert stats["unknown"] == ["NoSuchThing"]
    faction_name = "FactionAmerica"
    described = wb.call("faction.describe", faction=faction_name, scope="tree")
    assert described["faction"]["name"] == faction_name, described["faction"]
    assert described["counts"]["objects"] == len(described["objects"]) == described["counts"]["in_tech_tree"], described["counts"]
    if described["objects"]:
        first = described["objects"][0]
        assert first["in_tech_tree"] and "kind_of" in first and "modules" in first, first
        narrowed = wb.call("faction.describe", faction=faction_name, templates=[first["name"]], sections=["objects"])
        assert [o["name"] for o in narrowed["objects"]] == [first["name"]] and "weapons" not in narrowed, narrowed
    import faction as faction_data
    faction_dir = tempfile.mkdtemp(prefix="wb_mcp_faction_")
    made = faction_data.generate(wb.call("faction.describe", faction=faction_name), faction_name, faction_dir)
    assert made["objects"] > 0 and os.path.exists(made["overrides_created"]), made
    again = faction_data.generate(wb.call("faction.describe", faction=faction_name), faction_name, faction_dir)
    assert again["new_in_game_data"] == [] and again["no_longer_in_game_data"] == [] and again["unknown_in_slots"] == [], again
    merged = faction_data.load(faction_name, faction_dir)
    overview = faction_data.view(merged, "overview")
    assert overview["slots"].get("command_center") == described["faction"]["starting_building"], overview["slots"]
    assert all("role" in o for o in overview["objects"]), overview["objects"][:3]
    one = faction_data.view(merged, "unit", [overview["objects"][0]["name"], "NoSuchThing"])
    assert len(one["objects"]) == 1 and one["unknown"] == ["NoSuchThing"], one
    structure = next(o["name"] for o in merged["objects"].values() if o["category"] == "structure" and o.get("build_path"))
    soldier = next(o["name"] for o in merged["objects"].values()
                   if o["category"] == "infantry" and o.get("build_path") and o.get("built_by"))
    fake_dump = {"players": [], "teams": [
        {"teamName": "Good", "teamOwner": "P", "teamUnitType1": soldier, "teamUnitMinCount1": 1, "teamUnitMaxCount1": 2},
        {"teamName": "Bad", "teamOwner": "P", "teamUnitType1": "NoSuchThing", "teamUnitMinCount1": 3, "teamUnitMaxCount1": 1,
         "teamUnitType2": structure, "teamUnitMinCount2": 1, "teamUnitMaxCount2": 1}]}
    fake_order = {"steps": [{"script": "B", "builds": [{"action": "SKIRMISH_BUILD_BUILDING", "what": structure}]}]}
    checked = faction_data.check(merged, "P", fake_dump, fake_order, include_teams=True)
    kinds = {i["kind"] for i in checked["issues"]}
    assert {"team_unknown_type", "team_min_over_max", "team_structure", "build_prerequisite_never_built"} <= kinds, kinds
    assert checked["checked"]["teams"] == 2 and checked["teams"][0]["cost_max"] > 0, checked
    yard = next((o["name"] for o in merged["objects"].values() if "SHIPYARD" in o["kind_of"]), None)
    ship = next((o["name"] for o in merged["objects"].values() if o.get("naval") and o.get("tier") is not None), None)
    if yard and ship:
        on_land = next(o["name"] for o in merged["objects"].values()
                       if o["category"] == "structure" and "SHIPYARD" not in o["kind_of"])
        guard = {"name": "ShipsOk", "conditions": [[{"type": "SKIRMISH_SHIPS_ENABLED", "params": []}]], "actions": []}
        plain = {"name": "Always", "conditions": [[{"type": "CONDITION_TRUE", "params": []}]], "actions": []}
        naval_dump = {"players": [{"player": "P", "scripts": [guard, plain, dict(guard, name="YardGuarded"), dict(plain, name="YardPlain")],
                                   "groups": []}],
                      "teams": [{"teamName": "Fleet", "teamOwner": "P", "teamUnitType1": ship, "teamUnitMinCount1": 1, "teamUnitMaxCount1": 1,
                                 "teamProductionCondition": "ShipsOk"},
                                {"teamName": "Lost", "teamOwner": "P", "teamUnitType1": ship, "teamUnitMinCount1": 1, "teamUnitMaxCount1": 1,
                                 "teamProductionCondition": "Always"}]}
        naval_order = {"steps": [{"script": "YardGuarded", "builds": [{"action": "SKIRMISH_BUILD_SHIPYARD", "what": yard}]},
                                 {"script": "YardPlain", "builds": [{"action": "SKIRMISH_BUILD_BUILDING", "what": yard}]},
                                 {"script": "YardPlain", "builds": [{"action": "SKIRMISH_BUILD_SHIPYARD", "what": on_land}]}]}
        at_sea = faction_data.check(merged, "P", naval_dump, naval_order)
        found_at_sea = {(i["kind"], tuple(i["where"])) for i in at_sea["issues"]}
        assert ("team_ships_unguarded", ("team Lost",)) in found_at_sea, found_at_sea
        assert ("build_shipyard_wrong_action", ("script YardPlain",)) in found_at_sea, found_at_sea
        assert ("build_shipyard_wrong_type", ("script YardPlain",)) in found_at_sea, found_at_sea
        assert not any(kind == "build_shipyard_unguarded" and "script YardGuarded" in where for kind, where in found_at_sea), found_at_sea
    import framework as framework_data

    def fake_player(name, tag, unit, extra):
        common = {"name": "Go", "conditions": [[{"type": "FLAG", "params": [{"type": "FLAG", "value": "Ready"}]}]],
                  "actions": [{"type": "SET_FLAG", "params": [{"type": "FLAG", "value": "Done"}, {"type": "BOOLEAN", "value": True}]}]}
        build = {"name": "Build", "conditions": [[]],
                 "actions": [{"type": "SKIRMISH_BUILD_BUILDING", "params": [{"type": "OBJECT_TYPE", "value": unit}]}]}
        own = {"name": tag + " Own", "conditions": [[]], "actions": [{"type": extra, "params": []}]}
        return {"player": name, "scripts": [], "groups": [{"name": tag + " Scripts", "scripts": [common, build, own]}]}

    fake_set = {"players": [fake_player("A", "AAA", "TypeA", "NO_OP"), fake_player("B", "BBB", "TypeB", "DEBUG_STOP")],
                "teams": [{"teamName": "AAA Squad", "teamOwner": "A", "teamOnCreateScript": "Go"},
                          {"teamName": "BBB Squad", "teamOwner": "B", "teamOnCreateScript": "Go"}]}
    skeleton = framework_data.extract(fake_set)
    assert skeleton["meta"]["tags"] == {"A": "AAA", "B": "BBB"}, skeleton["meta"]
    assert skeleton["scripts"]["Go"]["class"] == "framework" and skeleton["scripts"]["<TAG> Own"]["class"] == "divergent", skeleton["scripts"]
    varying = skeleton["scripts"]["Build"]
    assert varying["class"] == "parameterized" and varying["varies"][0]["values"] == {"A": "TypeA", "B": "TypeB"}, varying
    assert varying["template"]["actions"][0]["params"][0]["value"] == "${0}", varying["template"]
    notes = {s["name"]: s.get("note") for s in skeleton["symbols"]}
    assert notes == {"Done": "written but never read", "Ready": "read but never written by a script"}, notes
    assert skeleton["teams"][0]["name"] == "<TAG> Squad" and skeleton["team_hooks"][0]["script_class"] == "framework", skeleton["teams"]
    import aidata as ai_data_file
    ini = ("AIData\n  TeamSeconds = 15.0 ; comment\n  SideInfo One\n    ResourceGatherersEasy = 2\n"
           "    BaseDefenseStructure1 = Tower\n    SkillSet1\n      Science = S1\n      Science = S2\n    End\n  End\n"
           "SkirmishBuildList One\n  Structure Hall\n    Location = X:500.00 Y:520.50\n    Rebuilds = -1\n    Angle = 90.00\n"
           "    InitiallyBuilt = No\n    AutomaticallyBuild = No\n  END ;Structure Hall\nEND ;SkirmishBuildList One\nEnd\n")
    parsed = ai_data_file.parse(ini)
    assert parsed["settings"] == {"TeamSeconds": 15} and parsed["sides"]["One"]["skill_sets"] == [["S1", "S2"]], parsed
    assert parsed["build_lists"]["One"] == [{"template": "Hall", "x": 500.0, "y": 520.5, "angle_deg": 90.0, "rebuilds": "unlimited",
                                             "initially_built": False}], parsed["build_lists"]
    block = ai_data_file.format_build_list("One", parsed["build_lists"]["One"] * 2)
    changed, was_there = ai_data_file.replace_build_list(ini, "one", block)
    assert was_there and len(ai_data_file.parse(changed)["build_lists"]["One"]) == 2 and changed.rstrip().endswith("End"), changed
    added, was_there = ai_data_file.replace_build_list(ini, "Two", ai_data_file.format_build_list("Two", parsed["build_lists"]["One"]))
    assert not was_there and sorted(ai_data_file.parse(added)["build_lists"]) == ["One", "Two"] and added.rstrip().endswith("End"), added
    assert ai_data_file.for_side(parsed, "ONE")["build_list"] == {"Hall": 1}
    noted = ini.replace("      Science = S1\n", "      ;first\n      Science = S1 ; keep\n")
    assert ai_data_file.edit_side_info(noted, "One", skill_sets={1: ["S1", "S2"]}, gatherers={"easy": 2})["text"] == noted
    edit = ai_data_file.edit_side_info(noted, "one", gatherers={"hard": 3}, base_defense="Fort", skill_sets={1: ["S0", "S1", "S3"], 2: ["T"]})
    after_edit = ai_data_file.parse(edit["text"])["sides"]["One"]
    assert after_edit == {"gatherers": {"easy": 2, "normal": 0, "hard": 3}, "base_defense": "Fort",
                          "skill_sets": [["S0", "S1", "S3"], ["T"]]}, after_edit
    assert "      ;first\n      Science = S0\n      Science = S1 ; keep\n      Science = S3\n" in edit["text"], edit["text"]
    assert ai_data_file.parse(edit["text"])["build_lists"] == parsed["build_lists"] and not edit["created"]
    fresh = ai_data_file.edit_side_info(noted, "Two", copy_from="One", base_defense="Wall")
    made_sides = ai_data_file.parse(fresh["text"])["sides"]
    assert fresh["created"] and made_sides["Two"]["base_defense"] == "Wall" and made_sides["One"] == parsed["sides"]["One"], made_sides
    assert ";first" in fresh["block"] and fresh["text"].rstrip().endswith("End")
    game_file = wb.call("game.read_file", path="Data\\INI\\Default\\AIData.ini")
    assert game_file["exists"] and "AIData" in game_file["text"], game_file.get("exists")
    assert not wb.call("game.read_file", path="Data\\INI\\NoSuchFile.ini")["exists"]
    sea = wb.call("ai.skirmish_check")
    assert sea["ships_enabled"] is False and sea["naval_ready"] is False and sea["naval_problems"] == [], sea
    assert wb.call("map.info")["ships_enabled"] is False
    try:
        wb.call("game.read_file", path="C:\\Windows\\win.ini")
        raise AssertionError("game.read_file must refuse paths outside of Data")
    except WorldBuilderError:
        pass
    wb.call("buildlist.set", player=owner, entries=[{"template": templates[0]["name"], "x": 300, "y": 300, "rebuilds": "unlimited"},
                                                    {"template": templates[0]["name"], "x": 350, "y": 300, "angle_deg": 45}])
    replaced_list = wb.call("buildlist.list", player=owner)["players"][0]["build_list"]
    assert [(e["x"], e["rebuilds"]) for e in replaced_list] == [(300, "unlimited"), (350, 0)], replaced_list
    wb.call("edit.undo")
    import match as match_data
    args = match_data.command_line("some map", ["FactionChina:hard", "FactionGLA:easy:1"], "log.jsonl", 5, 7, 10, 20000, 800, 600)
    assert args[args.index("-aiPlayers") + 1] == "FactionChina:hard,FactionGLA:easy:1" and "-aiMatchCash" in args, args
    assert "-aiMatchScripts" not in args and "-aiMatchAIData" not in args and args[:5] == ["-win", "-xres", "800", "-yres", "600"], args
    quiet = match_data.command_line("m", ["A:hard", "B:hard"], "l", 5, 7, 10, None, 800, 600, headless=True)
    assert quiet[0] == "-headless" and "-win" not in quiet, quiet
    more = match_data.command_line("m", ["A:hard", "B:hard"], "l", 5, 7, 10, None, 800, 600, scripts="s.scb", ai_data="a.ini")
    assert more[-4:] == ["-aiMatchScripts", "s.scb", "-aiMatchAIData", "a.ini"], more
    snap = {"alive": True, "money": 500, "power": [5, 3], "rank": 1, "science_points": 0,
            "score": {"units_built": 1, "units_lost": 0, "units_destroyed": 0, "buildings_built": 1, "buildings_lost": 0,
                      "buildings_destroyed": 0, "money_earned": 0, "money_spent": 900}}
    played = [
        {"t": "setup", "map": "m", "seed": 7, "frames_per_second": 30},
        {"t": "start", "frame": 2, "players": [{"name": "player1", "side": "S", "faction": "F", "difficulty": "hard", "team": -1, "start_position": 0}]},
        {"t": "team", "frame": 90, "player": "player1", "team": "Squad0"},
        {"t": "snap", "frame": 300, "players": [dict(snap, name="player1", objects={"Hall": 1}, teams={"teamSkirmishOne0": 1, "Squad0": 1})]},
        {"t": "snap", "frame": 600, "players": [dict(snap, name="player1", objects={"Hall": 1, "Tank": 2}, teams={"teamSkirmishOne0": 1})]},
        {"t": "end", "frame": 600, "reason": "time_limit", "winners": [],
         "players": [{"name": "player1", "alive": True, "score": snap["score"], "built": {"Hall": 1, "Tank": 2}, "lost": {"Tank": 1}}]},
        {"t": "scripts", "player": "player1", "scripts": {"Go0": [3, 10, 500]}},
        {"t": "messages", "messages": [{"player": "player1", "text": "Error - could not find building for Squad0", "count": 4, "first": 30, "last": 90},
                                        {"player": "", "text": "***Unexpected null player:***", "count": 9, "first": 3, "last": 90},
                                        {"player": "player1", "text": "all fine", "count": 1, "first": 5, "last": 5}]}]
    one_script = {"name": "Go", "actions": [{"type": "NO_OP", "params": []}]}
    played_dump = {"players": [{"player": "SkirmishOne", "scripts": [one_script, dict(one_script, name="Idle"), dict(one_script, name="Soft", hard=False)],
                                "groups": []}],
                   "teams": [{"teamName": "Squad", "teamOwner": "SkirmishOne"}, {"teamName": "Reserve", "teamOwner": "SkirmishOne"}]}
    told = match_data.report(played, played_dump)
    seen_player = told["players"][0]
    assert told["status"] == "finished" and told["game_minutes"] == 0.3 and seen_player["scripts_of"] == "SkirmishOne", told
    assert seen_player["teams"] == [{"team": "Squad", "started": 1, "first_at": 3.0}], seen_player["teams"]
    assert seen_player["never_ran"]["scripts_first"] == [{"script": "Idle", "group": ""}] and seen_player["never_ran"]["teams_first"] == ["Reserve"], seen_player["never_ran"]
    assert seen_player["trouble"] == [{"text": "Error - could not find building for Squad", "count": 4, "first_at": 1.0}], seen_player["trouble"]
    assert told["trouble"] == [{"text": "***Unexpected null player:***", "count": 9, "first_at": 0.1}], told["trouble"]
    import scaffold as scaffold_data
    assert scaffold_data.base_name("Z_SpecW_ChinaTankGattling") == "ChinaTankGattling"

    def fake_faction(name, prefix):
        objects = {prefix + "Barracks": {"role": "barracks", "category": "structure", "cost": 500, "tier": 0},
                   prefix + "Tank": {"role": "main_battle", "category": "vehicle", "cost": 900, "tier": 2}}
        return {"meta": {"faction": name}, "objects": objects, "other_objects": [], "slots": {"barracks": prefix + "Barracks"},
                "upgrades": {}, "sciences": {}, "special_powers": {}, "buttons": []}

    one, two = fake_faction("F1", "A_"), fake_faction("F2", "B_")
    one["objects"]["A_Odd"] = {"role": "siege", "category": "vehicle", "cost": 1, "tier": 1}
    pairs = scaffold_data.map_names(one, two)
    assert pairs["objects"]["A_Tank"] == {"to": "B_Tank", "by": "name"} and pairs["unmapped"][0]["name"] == "A_Odd", pairs
    source = {"players": [{"player": "A", "properties": {"playerName": "A", "playerFaction": "F1"}, "scripts": [], "groups": [
        {"name": "AAA Scripts", "scripts": [
            {"name": "Lists", "conditions": [[]], "actions": [{"type": "NO_OP", "params": [{"type": "OBJECT_TYPE", "value": "A_Tank"}]}]},
            {"name": "AAA Make", "conditions": [[]], "actions": [
                {"type": "SKIRMISH_BUILD_BUILDING", "params": [{"type": "OBJECT_TYPE", "value": "A_Barracks"}]},
                {"type": "ENABLE_SCRIPT", "params": [{"type": "SCRIPT", "value": "AAA Make"}]},
                {"type": "NO_OP", "params": [{"type": "OBJECT_TYPE", "value": "A_Odd"}]}]}]}]}],
        "teams": [{"teamName": "AAA Squad", "teamOwner": "A", "teamUnitType1": "A_Tank", "teamUnitMinCount1": 1,
                   "teamUnitMaxCount1": 2, "teamOnCreateScript": "AAA Make"}]}
    shared = {"meta": {"tags": {"A": "AAA"}}, "scripts": {"Lists": {"class": "framework"}}}
    derived = scaffold_data.scaffold(source, shared, one, two, "A", "N", "NNN", pairs)
    new_scripts = derived["document"]["players"][0]["groups"][0]["scripts"]
    assert new_scripts[0]["actions"][0]["params"][0]["value"] == "A_Tank", new_scripts[0]
    assert [a["params"][0]["value"] for a in new_scripts[1]["actions"]] == ["B_Barracks", "NNN Make", "A_Odd"], new_scripts[1]
    new_team = derived["document"]["teams"][0]
    assert (new_team["teamName"], new_team["teamOwner"], new_team["teamUnitType1"], new_team["teamOnCreateScript"]) == \
        ("NNN Squad", "N", "B_Tank", "NNN Make"), new_team
    assert [u["name"] for u in derived["report"]["unmapped"]] == ["A_Odd"], derived["report"]
    wb.call("buildlist.add", player=owner, template=templates[0]["name"], x=400, y=400, angle_deg=90, rebuilds="unlimited")
    wb.call("buildlist.add", player=owner, template=templates[0]["name"], x=500, y=400, name="Second")
    wb.call("buildlist.update", player=owner, index=1, new_index=0, health=50)
    entries = wb.call("buildlist.list", player=owner)["players"][0]["build_list"]
    assert [e.get("name") for e in entries] == ["Second", None] and entries[0]["health"] == 50, entries
    assert entries[1]["rebuilds"] == "unlimited" and abs(entries[1]["angle_deg"] - 90) < 0.01, entries
    wb.call("buildlist.delete", player=owner, index=0)
    assert len(wb.call("buildlist.list", player=owner)["players"][0]["build_list"]) == 1
    brief = wb.call("scripts.verify", summary=True)
    assert brief["warning_count"] == 1 and brief["warnings"][0]["count"] == 1, brief
    wb.call("edit.undo", count=5)
    assert wb.call("buildlist.list")["players"] == []

    ana = wb.call("scripts.analyze")
    assert ana["flags_read_never_set"]["count"] == 0 and ana["unused_subroutines"]["count"] == 0, ana

    # Players: rename with references, delete.
    extra = wb.call("sides.add_player", faction="FactionGLA")["player"]
    wb.call("scripts.add", player=extra, script={"name": "McpSideTest", "actions": [{"type": "PLAYER_HUNT", "params": [extra]}]})
    renamed = wb.call("sides.set", name=extra, new_name="McpRenamed", properties={"playerIsHuman": False})
    assert renamed["references_in_scripts"] == 1 and renamed["properties"]["playerIsHuman"] is False, renamed
    assert any(t["name"] == "teamMcpRenamed" for t in wb.call("sides.list")["teams"])
    try:
        wb.call("sides.delete", name="McpRenamed")
        raise AssertionError("deleting a player with scripts needs force")
    except WorldBuilderError:
        pass
    wb.call("sides.delete", name="McpRenamed", force=True)
    assert all(p["playerName"] != "McpRenamed" for p in wb.call("sides.list")["players"])

    scb = os.path.join(tempfile.mkdtemp(prefix="wb_mcp_"), "scripts.scb")
    assert wb.call("scripts.export", path=scb)["scripts"] == 2
    try:
        wb.call("scripts.export", path=scb)
        raise AssertionError("exporting over an existing file needs overwrite")
    except WorldBuilderError:
        pass
    assert os.path.exists(wb.call("scripts.export", path=scb, overwrite=True)["backup"])
    wb.call("scripts.delete_group", name="McpGroup", delete_scripts=True)
    wb.call("teams.delete", name="McpStrike")
    assert wb.call("scripts.list")["script_count"] == 0
    imported = wb.call("scripts.import", path=scb)
    assert imported["scripts_imported"] == 2 and imported["teams_added"] == 1, imported
    assert wb.call("scripts.get", name="McpFollowUp")["group"] == "McpGroup"
    print("scripts: ok")

    shot = wb.call("view.screenshot", x=400, y=400)
    print("screenshot:", shot["path"], shot["width"], "x", shot["height"])

    # Undo/redo round trip on the last edit.
    before = wb.call("map.info")["counts"]
    wb.call("edit.undo")
    wb.call("edit.redo")
    assert wb.call("map.info")["counts"] == before

    out_dir = tempfile.mkdtemp(prefix="wb_mcp_")
    path = os.path.join(out_dir, "mcp_smoke.map")
    wb.call("map.save", path=path)
    counts = wb.call("map.info")["counts"]
    wb.call("map.open", path=path)
    reopened = wb.call("map.info")["counts"]
    assert reopened == counts, (counts, reopened)
    names = [o.get("name") for o in wb.call("objects.list")["objects"]]
    assert "MyBarracks" in names, names
    print("saved and reopened:", path, reopened)

    # HeightMapScale: a map.ini next to the map doubles all heights and the height limit.
    plain = wb.call("terrain.sample", x=300, y=300)["height_raw"]
    assert wb.call("map.info")["heightmap"]["height_raw_limit"] == 255
    map_ini = os.path.join(out_dir, "map.ini")
    with open(map_ini, "w") as f:
        f.write("MapData\n  HeightMapScale = 2.0\nEnd\n")
    wb.call("map.open", path=path)
    grid = wb.call("map.info")["heightmap"]
    assert grid["height_map_scale"] == 2 and grid["height_raw_limit"] == 510, grid
    assert wb.call("terrain.sample", x=300, y=300)["height_raw"] == 2 * plain
    # Heights above 255 are kept, odd heights snap to the scale, and nothing exceeds the limit.
    wb.call("terrain.brush", op="set", x=600, y=300, radius=40, height=401)
    assert wb.call("terrain.sample", x=600, y=300)["height_raw"] == 402
    wb.call("terrain.brush", op="raise", x=600, y=300, radius=40, amount=500)
    assert wb.call("terrain.sample", x=600, y=300)["height_raw"] == 510
    wb.call("terrain.brush", op="lower", x=600, y=300, radius=40, amount=1)
    assert wb.call("terrain.sample", x=600, y=300)["height_raw"] == 508
    scaled_png = os.path.join(out_dir, "scaled.png")
    wb.call("terrain.export_heightmap", path=scaled_png)
    wb.call("terrain.import_heightmap", path=scaled_png, fit="exact")
    assert wb.call("terrain.sample", x=600, y=300)["height_raw"] == 508
    wb.call("map.save")
    wb.call("map.open", path=path)
    assert wb.call("terrain.sample", x=600, y=300)["height_raw"] == 508
    assert wb.call("terrain.sample", x=300, y=300)["height_raw"] == 2 * plain
    os.remove(map_ini)
    wb.call("map.open", path=path)
    assert wb.call("terrain.sample", x=600, y=300)["height_raw"] == 254
    print("height map scale: ok")
    print("OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WorldBuilderError as exc:
        print("FAILED:", exc)
        sys.exit(1)
