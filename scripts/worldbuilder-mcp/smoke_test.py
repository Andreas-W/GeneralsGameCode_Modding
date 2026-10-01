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
    print("OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WorldBuilderError as exc:
        print("FAILED:", exc)
        sys.exit(1)
