"""MCP server that lets an AI assistant edit the map open in Zero Hour WorldBuilder.

Run WorldBuilderZH.exe with ``-mcp`` first, then point an MCP client at this script (stdio).
"""

from __future__ import annotations

import json
import os
import time
from typing import Any, Literal

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

import yaml

import aidata as ai_data_file
import faction as faction_data
import framework as framework_data
import match as match_data
import scaffold as scaffold_data
from wb_bridge import WorldBuilderBridge, WorldBuilderError

INSTRUCTIONS = """\
Edits the map that is open in Command & Conquer Generals Zero Hour WorldBuilder, live.

Coordinates:
- World units: 10 per terrain cell. (0,0) is the lower left corner of the playable area; the
  border extends to negative coordinates. Call map_info for the exact extents.
- Heights are raw values 0..255 on the heightmap vertices; world height = raw * 0.625.
  A map.ini next to the map can set HeightMapScale (map_info: height_map_scale): raw heights then
  go up to 255 * scale (height_raw_limit) and are always multiples of the scale, so edits snap to it.
  terrain_get_heights/terrain_set_heights use heightmap vertex indices (which include the border);
  every other command uses world units.
- Object angles are in degrees, counterclockwise, 0 = facing +x.
- Object z is the height above the terrain; 0 means on the ground.

Workflow tips:
- Start with map_info. Use objects_list_templates / terrain_list_textures / roads_list_types to find
  valid names instead of guessing.
- Object owners are team names from sides_list ("team" is neutral, "teamplayer0001" belongs to
  player0001). Add players with sides_add_player.
- Skirmish maps: add Player_<N>_Start waypoints, then run ai_skirmish_setup (and ai_skirmish_check).
  The AI needs numbered names: InnerPerimeter1, OuterPerimeter1 and paths labeled Center1, Flank1,
  Backdoor1 leading into base 1, and so on per player. Plain "Center" labels are ignored.
- Scripts (map scripts and AI scripts): scripts_list / scripts_get to read, scripts_add /
  scripts_update to write, scripts_list_actions / scripts_list_conditions to find the types,
  scripts_find to search what scripts contain (who uses a team, script, object type ...).
  Scripts refer to each other, to teams, waypoints and areas by name. Teams are reviewed with
  teams_list and edited with teams_get / teams_set. On AI script sets (thousands of scripts) start
  with scripts_list detail="groups" and teams_list per owner.
- Building or reworking a whole AI: look names up with scripts_list_values and
  teams_list_properties instead of guessing; derive a faction with scripts_clone_player; rename
  with scripts_rename (it updates references); put many edits into scripts_batch (one undo step,
  all or nothing); keep a scripts_dump file as a readable backup; finish with scripts_verify and
  scripts_analyze.
- Understanding a set: scripts_build_order (what is built when), scripts_trace (what one team or
  script sets in motion), scripts_by_difficulty, objects_info (unit stats), scripts_report (all of
  it as a Markdown file).
- What a faction can build and how strong its units are: faction_describe (tech tree,
  prerequisites, cost, health, armor, weapons, upgrades, sciences, special powers from the game
  data). Write it to a file with path, or ask for single sections or templates.
- Planning an AI for a faction: faction_generate once (and after game data changes), then
  faction_get: overview first, then tree, units, or single types. Roles, slots and phases there
  are guesses until someone has reviewed the faction's overrides file; `unavailable` types
  cannot be built and must not be put into teams or build orders. After writing or changing an
  AI, run faction_check on the player: it finds teams and build orders the faction cannot
  fulfil.
- Where the skirmish AI builds is not in the scripts: SKIRMISH_BUILD_BUILDING takes the place
  from the side's SkirmishBuildList in AIData.ini (aidata_get). A structure without an entry
  there is never built. buildlist_from_aidata loads that layout into a map player to look at and
  edit, buildlist_export writes it back as an INI block. aidata_set_side changes a side's
  gatherer counts, base defense and science skill sets in place, keeping the file's comments.
- Whether an AI really plays is found out by letting it: ai_match_run starts the game for a
  skirmish between AI players and logs it, ai_match_report says what each one built when, what
  it never built, which scripts and teams never ran and what the script engine complained
  about. The match uses the installed scripts unless told otherwise: scripts="open" plays
  with what is open in WorldBuilder, ai_data with a changed AIData.ini. With the same seed, a
  match before and after a change shows what the change did.
- AI sets repeat one skeleton per faction. ai_framework_generate finds it from the players of
  the open map; ai_framework_get shows which scripts, flags, timers, team names, hooks and
  object lists every faction is expected to have, and where the factions differ.
- A new faction AI: ai_scaffold derives it from the AI of a related faction (dry run first; it
  keeps the shared scripts, translates the rest through faction_map and lists what has no
  counterpart). Then work through its `unmapped` list and faction_check.
- Rebalancing: teams_summary and scripts_compare show what a faction has and how two differ;
  scripts_set_values and teams_set_many change many values at once (try dry_run first);
  scripts_missing groups broken references by cause and suggests the intended names. Run scripts_verify after a batch of changes. Script edits fail while
  WorldBuilder's own script editor window is open.
- Every edit is one WorldBuilder undo step (edit_undo reverts it). WorldBuilder keeps only 15.
- Use view_screenshot to look at the result.
"""

mcp = MCPServer("worldbuilder", instructions=INSTRUCTIONS)
_bridge = WorldBuilderBridge()


class _Bridge:
    """Forwards to the bridge, turning its errors into ToolError so their text reaches the client
    (the SDK hides the message of any other exception)."""

    @staticmethod
    def call(cmd: str, **args: Any) -> Any:
        try:
            return _bridge.call(cmd, **args)
        except WorldBuilderError as exc:
            raise ToolError(str(exc)) from exc


wb = _Bridge()

Shape = Literal["circle", "rect"]


def _shape_args(shape: Shape | None, x: float | None, y: float | None, radius: float | None,
                x0: float | None, y0: float | None, x1: float | None, y1: float | None) -> dict[str, Any]:
    return {"shape": shape, "x": x, "y": y, "radius": radius, "x0": x0, "y0": y0, "x1": x1, "y1": y1}


# --------------------------------------------------------------------------------------------------
# Map
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def map_info() -> dict:
    """Map file, size, border, world extents, height range, object counts, name, weather, time of day."""
    return wb.call("map.info")


@mcp.tool()
def map_new(width: int = 128, height: int = 128, initial_height: int = 16, border: int = 30,
            discard_changes: bool = False) -> dict:
    """Creates a new empty map. width/height are playable cells (excluding the border on each side).
    Fails if the open map has unsaved changes unless discard_changes is true."""
    return wb.call("map.new", width=width, height=height, initial_height=initial_height, border=border,
                   discard_changes=discard_changes)


@mcp.tool()
def map_open(path: str, discard_changes: bool = False) -> dict:
    """Opens a .map file (absolute path)."""
    return wb.call("map.open", path=path, discard_changes=discard_changes)


@mcp.tool()
def map_save(path: str | None = None) -> dict:
    """Saves the map. Without a path it saves to the current file; untitled maps need a full .map path.
    WorldBuilder expects maps at <user data>/Maps/<name>/<name>.map to show up in the game."""
    return wb.call("map.save", path=path)


@mcp.tool()
def map_set_info(name: str | None = None,
                 time_of_day: Literal["MORNING", "AFTERNOON", "EVENING", "NIGHT"] | None = None,
                 weather: Literal["NORMAL", "SNOWY"] | None = None) -> dict:
    """Changes the map name, time of day or weather."""
    return wb.call("map.set_info", name=name, time_of_day=time_of_day, weather=weather)


@mcp.tool()
def edit_undo(count: int = 1) -> dict:
    """Undoes the last `count` edits (max 15)."""
    return wb.call("edit.undo", count=count)


@mcp.tool()
def edit_redo(count: int = 1) -> dict:
    """Redoes `count` undone edits."""
    return wb.call("edit.redo", count=count)


# --------------------------------------------------------------------------------------------------
# Usage and bulk replace
# --------------------------------------------------------------------------------------------------

def _read_mapping_file(path: str) -> dict[str, str]:
    """Reads a Genesis-style replace list: one "from to" pair per line."""
    mapping: dict[str, str] = {}
    with open(os.path.abspath(path), encoding="utf-8") as f:
        for line in f:
            parts = line.split(None, 1)
            if len(parts) == 2 and not parts[0].startswith("#"):
                mapping[parts[0]] = parts[1].strip()
    return mapping


@mcp.tool()
def map_usage() -> dict:
    """Lists what the map uses: textures with cell counts, object templates with counts (known=false
    marks templates this game/mod does not have, e.g. in a map made for another mod) and road types.
    Use it to build the mappings for map_replace."""
    return wb.call("map.usage")


@mcp.tool()
def map_replace(textures: dict[str, str] | None = None, objects: dict[str, str] | None = None,
                roads: dict[str, str] | None = None, textures_file: str | None = None,
                objects_file: str | None = None, roads_file: str | None = None, dry_run: bool = False) -> dict:
    """Replaces textures, object templates and road/bridge types everywhere in the map, each given as
    a {from: to} mapping (names as in map_usage; matching ignores case). Textures keep their blends;
    objects keep position, rotation, owner and properties; roads keep their corner flags. The
    *_file arguments read a text file with one "from to" pair per line (Genesis replace lists).
    dry_run only reports how many cells/objects would change. A name cannot be both a source and a
    target in one call (run chained or swapped replacements separately). One undo step."""
    def merged(mapping: dict[str, str] | None, path: str | None) -> dict[str, str] | None:
        if path is None:
            return mapping
        return {**_read_mapping_file(path), **(mapping or {})}

    return wb.call("map.replace", textures=merged(textures, textures_file),
                   objects=merged(objects, objects_file), roads=merged(roads, roads_file), dry_run=dry_run)


# --------------------------------------------------------------------------------------------------
# Resize
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def map_resize(width: int | None = None, height: int | None = None,
               anchor: Literal["center", "left", "right", "bottom", "top", "bottom_left", "bottom_right",
                               "top_left", "top_right"] | None = None,
               add_left: int | None = None, add_right: int | None = None, add_bottom: int | None = None,
               add_top: int | None = None, border: int | None = None, fill_height: int | None = None,
               fill_texture: str | None = None,
               remove_outside: Literal["none", "objects", "waypoints", "all"] = "none") -> dict:
    """Grows or crops the map. Terrain, textures, passability, objects, waypoints, roads, areas, water
    and build lists keep their place on the terrain, so their world coordinates shift by the returned
    world_offset when cells are added or removed on the left or bottom.
    Two ways to give the new size (cells, as in map_new):
    - width/height with an anchor: the anchored side or corner stays put (default center: the change
      is split between both sides).
    - add_left/add_right/add_bottom/add_top: cells to add per side; negative values crop.
    border changes the border width. New cells continue the old edge, which smears edge detail into
    stripes: fill_height (raw) and fill_texture give them a fixed height and texture instead.
    Rework a grown strip afterwards with terrain_generate / terrain_auto_texture limited to a rect.
    remove_outside deletes objects and/or waypoints that end up outside the new map (a road segment
    only when both ends are outside); by default they are kept and counted in objects_outside.
    Areas are never deleted (areas_outside counts those entirely off the map). An area that covers
    the whole map, like Default Water, is refit to the new map. Script actions with literal
    coordinates are not adjusted. One undo step."""
    return wb.call("map.resize", width=width, height=height, anchor=anchor, add_left=add_left,
                   add_right=add_right, add_bottom=add_bottom, add_top=add_top, border=border,
                   fill_height=fill_height, fill_texture=fill_texture, remove_outside=remove_outside)


# --------------------------------------------------------------------------------------------------
# Symmetry
# --------------------------------------------------------------------------------------------------

SymmetryParts = dict[str, bool]


@mcp.tool()
def map_transform(op: Literal["mirror_x", "mirror_y", "rotate_90", "rotate_180", "rotate_270",
                              "mirror_diag", "mirror_antidiag"],
                  include: SymmetryParts | None = None) -> dict:
    """Mirrors or rotates the whole map in place: heights, textures (blends are re-oriented),
    passability, objects, waypoints and areas. mirror_x flips east/west, mirror_y north/south,
    rotations are counterclockwise; rotations by 90/270 and the diagonal mirrors need a square map.
    include: {heights, textures, passability, objects, waypoints, areas} booleans, all true by default.
    One undo step. Cliff texture mapping (UV adjustment) is reset on the moved cells."""
    return wb.call("map.transform", op=op, include=include)


@mcp.tool()
def map_symmetrize(mode: Literal["mirror_x", "mirror_y", "rotate_180", "mirror_diag", "mirror_antidiag",
                                 "rotate_quarters"],
                   source: Literal["west", "east", "south", "north", "sw", "se", "ne", "nw"],
                   rename: str | list[list[str]] = "auto", owner_map: dict[str, str] | None = None,
                   include: SymmetryParts | None = None, axis_tolerance: float = 5,
                   skip_default_water: bool = True) -> dict:
    """Builds a symmetric map: copies the `source` part onto the rest, replacing whatever was there.
    - mirror_x (source west|east), mirror_y (south|north): mirror symmetry for 2 players.
    - rotate_180 (source south|north|west|east): point symmetry for 2 players (e.g. bases in opposite
      corners).
    - mirror_diag (across y=x, source se|nw) / mirror_antidiag (across y=-x, source sw|ne).
    - rotate_quarters (source sw|se|ne|nw, square map): 4-player rotational symmetry, copies go
      counterclockwise.
    Copies terrain, textures, passability, objects (road pairs stay intact), waypoints with their
    links, and areas. Objects/areas within axis_tolerance (world units) of the mirror axis or center
    are kept once instead of copied. rename="auto" renumbers players in names and owners for each copy
    (Player_1_Start -> Player_2_Start, P1_ -> P2_, InnerPerimeter1 -> InnerPerimeter2,
    teamplayer0001 -> teamplayer0002); "none" keeps names; or pass [["from","to"], ...] replacements.
    owner_map {old_team: new_team} overrides owners. "Default Water" is left alone by default.
    Object ids from objects_list are invalidated. One undo step."""
    return wb.call("map.symmetrize", mode=mode, source=source, rename=rename, owner_map=owner_map,
                   include=include, axis_tolerance=axis_tolerance, skip_default_water=skip_default_water)


# --------------------------------------------------------------------------------------------------
# View
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def view_set_camera(x: float | None = None, y: float | None = None, angle_deg: float | None = None,
                    pitch: float | None = None, zoom: float | None = None, reset: bool = False) -> dict:
    """Moves the 3D camera. x/y: world point shown in the middle of the screen (kept when omitted);
    the result reports the current one. angle_deg: camera yaw.
    pitch: must be > 0; 1.0 is the default tilt, larger values look more straight down (about 2.5 is
    nearly top-down), smaller values flatten toward the horizon. zoom: mouse wheel offset, 0 is default,
    positive zooms in, negative zooms out (about -3500 shows a whole 240x240 map).
    reset restores the default camera first; the other arguments are applied after it."""
    return wb.call("view.set_camera", x=x, y=y, angle_deg=angle_deg, pitch=pitch, zoom=zoom, reset=reset or None)


@mcp.tool()
def view_screenshot(x: float | None = None, y: float | None = None, angle_deg: float | None = None,
                    pitch: float | None = None, zoom: float | None = None, reset: bool = False,
                    max_width: int = 1280, format: Literal["jpg", "png"] = "jpg", quality: int = 85) -> Image:
    """Renders the WorldBuilder 3D view and returns it as an image. Takes the same optional camera
    arguments as view_set_camera. The frame is downscaled to at most max_width pixels (0 = full size).
    The WorldBuilder window must be visible (not minimized)."""
    result = wb.call("view.screenshot", x=x, y=y, angle_deg=angle_deg, pitch=pitch, zoom=zoom,
                     reset=reset or None, max_width=max_width, format=format, quality=quality)
    return Image(path=result["path"])


# --------------------------------------------------------------------------------------------------
# Terrain
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def terrain_get_heights(x0: int = 0, y0: int = 0, w: int | None = None, h: int | None = None,
                        step: int = 1) -> dict:
    """Reads raw vertex heights (0..height_raw_limit, see map_info) as rows (row 0 = y0). Indices are heightmap vertex indices
    including the border; vertex (border, border) is world (0,0). Use step>1 to downsample large areas.
    At most 65536 values per call."""
    return wb.call("terrain.get_heights", x0=x0, y0=y0, w=w, h=h, step=step)


@mcp.tool()
def terrain_set_heights(x0: int, y0: int, heights: list[list[int | None]]) -> dict:
    """Writes raw vertex heights (0..height_raw_limit, see map_info). heights[r][c] goes to vertex (x0+c, y0+r); null leaves a
    vertex unchanged. One undo step."""
    return wb.call("terrain.set_heights", x0=x0, y0=y0, heights=heights)


@mcp.tool()
def terrain_brush(op: Literal["raise", "lower", "set", "flatten", "smooth", "noise"],
                  shape: Shape | None = None,
                  x: float | None = None, y: float | None = None, radius: float | None = None,
                  x0: float | None = None, y0: float | None = None, x1: float | None = None, y1: float | None = None,
                  feather: float = 0, amount: float | None = None, height: float | None = None,
                  strength: float = 1.0, iterations: int = 1, seed: int | None = None) -> dict:
    """Shapes terrain in a world-space area: a circle (x, y, radius) or a rect (x0, y0, x1, y1).
    feather: world-unit falloff band outside the shape.
    - raise/lower: change height by `amount` raw units (16 raw = 10 world units).
    - set: move heights to `height` (raw).
    - flatten: like set; without `height` it uses the height at the center.
    - smooth: average neighbours; raise `iterations` for stronger smoothing.
    - noise: random +-`amount` raw variation (deterministic per `seed`).
    strength (0..1) scales the effect."""
    return wb.call("terrain.brush", op=op, feather=feather, amount=amount, height=height, strength=strength,
                   iterations=iterations, seed=seed, **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_list_textures(filter: str | None = None) -> dict:
    """Terrain texture classes (name, ui_name, whether already used / can still fit in this map)."""
    return wb.call("terrain.list_textures", filter=filter)


@mcp.tool()
def terrain_paint_texture(texture: str, shape: Shape | None = None,
                          x: float | None = None, y: float | None = None, radius: float | None = None,
                          x0: float | None = None, y0: float | None = None, x1: float | None = None,
                          y1: float | None = None) -> dict:
    """Paints a terrain texture (name from terrain_list_textures) over a circle or rect in world units.
    Follow up with terrain_auto_blend to soften hard edges."""
    return wb.call("terrain.paint_texture", texture=texture, **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_flood_fill(texture: str, x: float, y: float, replace_all: bool = False) -> dict:
    """Flood fills the connected texture region under world point (x, y) with `texture`.
    replace_all instead replaces the texture under the point everywhere on the map; use it when the map
    has no room for another texture (it swaps one texture for the other)."""
    return wb.call("terrain.flood_fill", texture=texture, x=x, y=y, replace_all=replace_all)


@mcp.tool()
def terrain_auto_blend(x: float, y: float, edge_texture: str | None = None) -> dict:
    """Blends the edges of the texture region under world point (x, y) into its neighbours, optionally
    using a blend-edge texture."""
    return wb.call("terrain.auto_blend", x=x, y=y, edge_texture=edge_texture)


@mcp.tool()
def terrain_set_passability(impassable: bool = True, shape: Shape | None = None,
                            x: float | None = None, y: float | None = None, radius: float | None = None,
                            x0: float | None = None, y0: float | None = None, x1: float | None = None,
                            y1: float | None = None) -> dict:
    """Marks cells impassable (or passable again) over a circle or rect in world units."""
    return wb.call("terrain.set_passability", impassable=impassable,
                   **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_sample(x: float, y: float) -> dict:
    """Height, texture and passability at a world point."""
    return wb.call("terrain.sample", x=x, y=y)


# --------------------------------------------------------------------------------------------------
# Procedural terrain
# --------------------------------------------------------------------------------------------------

_PRESET_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "presets")


def _load_presets(name: str) -> dict[str, Any]:
    with open(os.path.join(_PRESET_DIR, name), encoding="utf-8") as f:
        data = json.load(f)
    return {k: v for k, v in data.items() if not k.startswith("_")}


def _preset(kind: str, name: str) -> dict[str, Any]:
    presets = _load_presets(kind)
    if name not in presets:
        raise ToolError(f"unknown preset '{name}'; available: {', '.join(sorted(presets))}")
    return presets[name]


@mcp.tool()
def terrain_list_presets() -> dict:
    """Terrain layouts (for terrain_generate) and sceneries (for terrain_auto_texture), with their settings."""
    return {"layouts": _load_presets("layouts.json"), "sceneries": _load_presets("sceneries.json")}


@mcp.tool()
def terrain_generate(preset: str | None = "default", layers: list[dict[str, Any]] | None = None, seed: int = 1,
                     mode: Literal["set", "add"] = "set", base_height: float | None = None,
                     shape: Shape | None = None, x: float | None = None, y: float | None = None,
                     radius: float | None = None, x0: float | None = None, y0: float | None = None,
                     x1: float | None = None, y1: float | None = None, feather: float = 0,
                     protect: list[dict[str, Any]] | None = None) -> dict:
    """Generates terrain heights from layered Perlin noise (Genesis TerrainGenerator).
    Each layer adds clamp(noise, 0..1) * height raw units; noise has frequency (per heightmap vertex),
    octaves, persistence (roughness), amplitude, and optional ridged (sharp crests) or signed (-1..1).
    preset loads a layout from terrain_list_presets (default, rolling, mountains, flat_bumpy); passing
    layers overrides it. mode=set starts from base_height, mode=add adds onto the current terrain.
    Limit to a circle/rect with feather, and keep spots unchanged with
    protect=[{"x":..,"y":..,"radius":..,"feather":..}] (e.g. base locations; flatten them afterwards).
    Different seeds give different terrain. One undo step."""
    if layers is None:
        if preset is None:
            raise ToolError("pass a preset or layers")
        layout = _preset("layouts.json", preset)
        layers = layout["layers"]
        if base_height is None:
            base_height = layout.get("base_height")
    return wb.call("terrain.generate", layers=layers, seed=seed, mode=mode, base_height=base_height,
                   feather=feather, protect=protect, **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_limit_slope(max_step: float = 15, iterations: int = 20, shape: Shape | None = None,
                        x: float | None = None, y: float | None = None, radius: float | None = None,
                        x0: float | None = None, y0: float | None = None, x1: float | None = None,
                        y1: float | None = None) -> dict:
    """Pulls too-steep neighbouring vertices together until no height step exceeds max_step raw units
    (Genesis TerrainSmoother). Impassable cliff flags follow the resulting slopes automatically.
    Run it after terrain_generate to turn noise spikes into walkable slopes. One undo step."""
    return wb.call("terrain.limit_slope", max_step=max_step, iterations=iterations,
                   **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_auto_texture(scenery: str | None = "highlands", base: dict[str, Any] | None = None,
                         cliff: dict[str, Any] | None = None, water: dict[str, Any] | None = None,
                         cliff_slope: float | None = None, water_below: float | None = None,
                         seed: int = 1, blend: bool = True, edge_texture: str | None = None,
                         shape: Shape | None = None, x: float | None = None, y: float | None = None,
                         radius: float | None = None, x0: float | None = None, y0: float | None = None,
                         x1: float | None = None, y1: float | None = None) -> dict:
    """Textures the terrain automatically (Genesis TextureGenerator). Each cell is classified as
    cliff (height difference across the cell >= cliff_slope raw, default 12, or impassable),
    water (below water_below raw height, or under a water polygon when water_below is omitted),
    or ground. Each class has a base texture plus overlays scattered by noise:
    {"texture": .., "overlays": [{"texture": .., "frequency": 0.07, "threshold": 0.5}]}.
    scenery loads a preset (highlands, fall, snow, woodland, rocky_island, sand_cliffs, desert);
    base/cliff/water override its parts. Overlays, cliffs and water are then blended into their
    surroundings (blend=false skips that). Fails up front if the map has no room for a texture.
    One undo step."""
    preset = _preset("sceneries.json", scenery) if scenery else {}
    base = base or preset.get("base")
    cliff = cliff or preset.get("cliff")
    water = water or preset.get("water")
    if base is None:
        raise ToolError("pass a scenery or a base texture")
    if cliff is not None and cliff_slope is not None:
        cliff = {**cliff, "slope": cliff_slope}
    if water is not None and water_below is not None:
        water = {**water, "below": water_below}
    return wb.call("terrain.auto_texture", base=base, cliff=cliff, water=water, seed=seed, blend=blend,
                   edge_texture=edge_texture, **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_blend_all(textures: list[str] | None = None, edge_texture: str | None = None,
                      shape: Shape | None = None, x: float | None = None, y: float | None = None,
                      radius: float | None = None, x0: float | None = None, y0: float | None = None,
                      x1: float | None = None, y1: float | None = None) -> dict:
    """Auto-blends the edges of every region of the given textures (default: all textures except the
    most common one, taken as the ground) into their surroundings. One undo step."""
    return wb.call("terrain.blend_all", textures=textures, edge_texture=edge_texture,
                   **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def terrain_remove_blends(shape: Shape | None = None, x: float | None = None, y: float | None = None,
                          radius: float | None = None, x0: float | None = None, y0: float | None = None,
                          x1: float | None = None, y1: float | None = None) -> dict:
    """Removes texture blends (in an area or the whole map), leaving hard texture edges. One undo step."""
    return wb.call("terrain.remove_blends", **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


# --------------------------------------------------------------------------------------------------
# Terrain images
# --------------------------------------------------------------------------------------------------

def _abspath(path: str | None) -> str | None:
    # WorldBuilder runs with the mod folder as its working directory, so resolve relative paths here.
    return os.path.abspath(path) if path else None


ImageFit = Literal["stretch", "exact", "center"]
Channel = Literal["luma", "r", "g", "b", "a"]


@mcp.tool()
def terrain_export_heightmap(path: str | None = None, normalize: bool = False, include_border: bool = True,
                             view: bool = False) -> Any:
    """Writes the heightmap as an 8-bit grayscale PNG: one pixel per heightmap vertex, pixel value =
    raw height 0..255 (divided by the map's HeightMapScale, like in the map file), top row = north edge. normalize stretches the used height range to 0..255 for
    easier viewing (not for round trips). include_border=false exports only the playable area.
    view=true also returns the image so you can look at the relief. Default path is a temp file."""
    result = wb.call("terrain.export_heightmap", path=_abspath(path), normalize=normalize,
                     include_border=include_border)
    if view:
        return [result, Image(path=result["path"])]
    return result


@mcp.tool()
def terrain_import_heightmap(path: str, mode: Literal["set", "add"] = "set", range: list[float] | None = None,
                             channel: Channel = "luma", fit: ImageFit = "stretch", include_border: bool = True,
                             smooth: int = 0) -> dict:
    """Loads terrain heights from a PNG/BMP/TGA image (8 or 16 bit; top row = north). Pixel 0..max is
    mapped onto range=[low, high] raw heights (default [0, height_raw_limit] for set, [-64, 64] for add). mode=add
    adds the mapped value to the current heights. fit: stretch = resample to the map (bilinear),
    exact = image must match the vertex grid size, center = place 1:1 in the middle.
    include_border=false targets only the playable area. smooth = smoothing passes afterwards.
    Tip: generate heightmaps locally (e.g. Python + numpy/PIL) and import them here. One undo step."""
    return wb.call("terrain.import_heightmap", path=_abspath(path), mode=mode, range=range, channel=channel,
                   fit=fit, include_border=include_border, smooth=smooth)


@mcp.tool()
def terrain_export_mask(kind: Literal["passability", "texture", "water"], texture: str | None = None,
                        path: str | None = None, include_border: bool = True, view: bool = False) -> Any:
    """Writes a black/white PNG with one pixel per terrain cell (top row = north): impassable cells,
    cells whose base texture is `texture`, or cells under water. view=true also returns the image."""
    result = wb.call("terrain.export_mask", kind=kind, texture=texture, path=_abspath(path),
                     include_border=include_border)
    if view:
        return [result, Image(path=result["path"])]
    return result


@mcp.tool()
def terrain_import_mask(path: str, target: Literal["passability", "texture"], texture: str | None = None,
                        threshold: int = 128, invert: bool = False, impassable: bool = True,
                        channel: Channel = "luma", fit: ImageFit = "stretch", include_border: bool = True) -> dict:
    """Applies a mask image to terrain cells: every cell whose pixel is >= threshold (0..255, or below it
    with invert) is painted with `texture` (target=texture) or set impassable/passable (target=passability,
    impassable flag). Cells outside the mask are left unchanged. Handy for stamping shapes such as text,
    rivers or plateaus that you draw as an image locally. fit=exact expects one pixel per cell
    (map_info heightmap extent minus one). One undo step."""
    return wb.call("terrain.import_mask", path=_abspath(path), target=target, texture=texture,
                   threshold=threshold, invert=invert, impassable=impassable, channel=channel, fit=fit,
                   include_border=include_border)


# --------------------------------------------------------------------------------------------------
# Objects and sides
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def objects_list_templates(filter: str | None = None, editor_sorting: str | None = None, side: str | None = None,
                           limit: int = 200, offset: int = 0) -> dict:
    """Searches placeable object templates. filter: substring of the template name.
    editor_sorting: STRUCTURE, INFANTRY, VEHICLE, SHRUBBERY, MISC_MAN_MADE, MISC_NATURAL, DEBRIS, SYSTEM,
    AUDIO, TEST, FOR_REVIEW. side: e.g. America, China, GLA, Civilian."""
    return wb.call("objects.list_templates", filter=filter, editor_sorting=editor_sorting, side=side,
                   limit=limit, offset=offset)


@mcp.tool()
def objects_list(template: str | None = None, owner: str | None = None,
                 x0: float | None = None, y0: float | None = None, x1: float | None = None, y1: float | None = None,
                 include_waypoints_and_roads: bool = False, limit: int = 500) -> dict:
    """Lists objects in the map with their ids (ids are stable for this WorldBuilder session).
    Filter by template substring, exact owner team, or a world rect (x0, y0, x1, y1)."""
    return wb.call("objects.list", template=template, owner=owner, x0=x0, y0=y0, x1=x1, y1=y1,
                   include_waypoints_and_roads=include_waypoints_and_roads, limit=limit)


@mcp.tool()
def objects_get(id: int) -> dict:
    """One object including its full property dictionary."""
    return wb.call("objects.get", id=id)


@mcp.tool()
def objects_place(template: str, x: float, y: float, z: float = 0, angle_deg: float | None = None,
                  owner: str | None = None, name: str | None = None,
                  properties: dict[str, Any] | None = None) -> dict:
    """Places an object. owner: team name, player name or "neutral" (default). name: script name
    (objectName). properties: extra keys such as objectInitialHealth (int %), objectVeterancy (0-3),
    objectAggressiveness, objectIndestructible, objectUnsellable, objectEnabled, objectPowered.
    angle_deg defaults to the template's placement angle."""
    return wb.call("objects.place", template=template, x=x, y=y, z=z, angle_deg=angle_deg, owner=owner,
                   name=name, properties=properties)


@mcp.tool()
def objects_place_many(objects: list[dict[str, Any]]) -> dict:
    """Places several objects; each entry takes the same fields as objects_place. Each placement is
    its own undo step, so keep batches small if you may need to undo."""
    placed, errors = [], []
    for i, spec in enumerate(objects):
        try:
            placed.append(wb.call("objects.place", **spec))
        except Exception as exc:  # report per-object errors and keep going
            errors.append({"index": i, "error": str(exc)})
    return {"placed": placed, "errors": errors}


@mcp.tool()
def objects_modify(ids: list[int], x: float | None = None, y: float | None = None,
                   dx: float | None = None, dy: float | None = None, z: float | None = None,
                   angle_deg: float | None = None, rotate_deg: float | None = None,
                   template: str | None = None) -> dict:
    """Moves (absolute x/y for a single object, or relative dx/dy), sets z above ground, rotates
    (absolute angle_deg or relative rotate_deg) or swaps the template of objects. One undo step."""
    return wb.call("objects.modify", ids=ids, x=x, y=y, dx=dx, dy=dy, z=z, angle_deg=angle_deg,
                   rotate_deg=rotate_deg, template=template)


@mcp.tool()
def objects_set_properties(ids: list[int], properties: dict[str, Any] | None = None,
                           owner: str | None = None, name: str | None = None) -> dict:
    """Sets object properties (see objects_get for keys). A null value removes a key. owner/name are
    shortcuts for originalOwner/objectName."""
    return wb.call("objects.set_properties", ids=ids, properties=properties, owner=owner, name=name)


@mcp.tool()
def objects_delete(ids: list[int]) -> dict:
    """Deletes objects (also waypoints or road points by id)."""
    return wb.call("objects.delete", ids=ids)


@mcp.tool()
def objects_scatter(templates: list[str | dict[str, Any]], count: int | None = None, density: float = 40,
                    seed: int = 1, shape: Shape | None = None, x: float | None = None, y: float | None = None,
                    radius: float | None = None, x0: float | None = None, y0: float | None = None,
                    x1: float | None = None, y1: float | None = None, feather: float = 0,
                    min_spacing: float = 30, road_clearance: float = 40, object_clearance: float = 40,
                    start_clearance: float = 300, avoid_cliffs: bool = True, avoid_water: bool = True,
                    max_slope: float | None = None, only_textures: list[str] | None = None,
                    avoid_areas: list[str] | None = None, inside_areas: list[str] | None = None,
                    cluster: dict[str, float] | None = None) -> dict:
    """Scatters decoration (trees, bushes, rocks, props) at random positions with random rotation.
    templates: names or {"template": .., "weight": ..} (see objects_list_templates, e.g.
    editor_sorting SHRUBBERY or MISC_NATURAL). Number: count, or density = objects per 1000x1000
    world units over the area. Area: the whole playable map, or a circle/rect (feather thins the edge).
    Placement keeps min_spacing between scattered objects, road_clearance from roads,
    object_clearance beyond the footprint of existing structures/units, start_clearance from
    Player_<N>_Start waypoints, and skips cliffs and water. Optional filters: max_slope (raw height
    difference across the cell), only_textures (e.g. grass only), avoid_areas / inside_areas (area
    names). cluster={"frequency": 0.03, "threshold": 0.5} groups objects into groves using noise
    (lower frequency = larger groves, higher threshold = fewer). Same seed gives the same result.
    All objects are neutral. One undo step; follow with map_symmetrize if the map must stay symmetric."""
    return wb.call("objects.scatter", templates=templates, count=count, density=density, seed=seed,
                   feather=feather, min_spacing=min_spacing, road_clearance=road_clearance,
                   object_clearance=object_clearance, start_clearance=start_clearance,
                   avoid_cliffs=avoid_cliffs, avoid_water=avoid_water, max_slope=max_slope,
                   only_textures=only_textures, avoid_areas=avoid_areas, inside_areas=inside_areas,
                   cluster=cluster, **_shape_args(shape, x, y, radius, x0, y0, x1, y1))


@mcp.tool()
def sides_list() -> dict:
    """Players (with their property dicts) and teams. Object owners are team names."""
    return wb.call("sides.list")


@mcp.tool()
def sides_list_factions() -> dict:
    """Player templates (factions) for sides_add_player, e.g. FactionAmerica, FactionChina, FactionGLA."""
    return wb.call("sides.list_factions")


@mcp.tool()
def objects_info(templates: list[str] | None = None, filter: str | None = None, limit: int = 20) -> dict:
    """Stats of object types (units, structures): build cost, owning side, display name, KindOf
    flags, buildable status, energy, vision range, command set and `built_at`, the structures
    that produce it. Pass the names as templates (up to 60; unknown ones come back in `unknown`),
    or a name substring as filter. Use it to judge team compositions and build orders."""
    return wb.call("objects.info", templates=templates, filter=filter, limit=limit)


@mcp.tool()
def faction_describe(faction: str, path: str | None = None, scope: Literal["side", "tree"] = "side",
                     sections: list[str] | None = None, templates: list[str] | None = None) -> dict:
    """A faction as the game data defines it, read from the loaded templates (inheritance, reskins
    and module overrides resolved). faction is a faction name from sides_list_factions
    (FactionChina) or its side (China). No map is needed. Sections of the result:
    - faction: starting building and units, start money, intrinsic sciences, science command
      sets, cost and build time multipliers of single types.
    - objects: per unit and structure name, side, in_tech_tree, kind_of, cost, build_time
      (seconds), energy, max_simultaneous, vision, health, locomotor (speed per second,
      surfaces), contain, stealth, detector_range, supply_boxes, modules, weapon_sets and
      armor_sets by condition, prerequisites, built_by, and from its command sets: builds,
      upgrades, special_powers, abilities (button names as script actions take them).
      `prerequisites` are all required: one type out of every group in `objects`, and every
      science in `sciences`. `stealth` is set for innate stealth only.
    - weapons: damage, damage_radius, damage_type, range, shot_delay and clip_reload (seconds),
      clip_size, anti (what it may target), projectile, and dps: sustained damage per second on
      one target before armor (wrong for weapons that deal their damage through spawned objects).
    - armors: damage_taken per damage type as a factor (types at 1.0 left out).
    - upgrades: type, cost, build_time, researched_at.
    - sciences: point_cost, prerequisites, purchase_rank, what they unlock.
    - special_powers: type, reload_time (seconds), required_science, used_by.
    scope "side" adds the unit and structure types of the side that nothing in the tech tree
    builds (in_tech_tree false; often AI-only variants); "tree" leaves them out. templates
    narrows objects, weapons and armors to the named types. The whole description is large
    (hundreds of KB): give path to write it to a JSON file and get only the counts back, or ask
    for single sections / templates."""
    doc = wb.call("faction.describe", faction=faction, scope=scope, sections=sections, templates=templates)
    if path is None:
        return doc
    full = os.path.abspath(path)
    with open(full, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
    result = {"path": full, "bytes": os.path.getsize(full), "counts": doc["counts"]}
    if "unknown_templates" in doc:
        result["unknown_templates"] = doc["unknown_templates"]
    return result


FACTION_FOLDER = "worldbuilder_factions"
_exe_dir: str | None = None


def _faction_dir(directory: str | None) -> str:
    """The folder of the faction files: next to the WorldBuilder executable, which runs in the
    folder of the game data it describes. A relative directory is taken from there as well."""
    global _exe_dir
    override = directory or os.environ.get("WB_MCP_FACTION_DIR")
    if override and os.path.isabs(override):
        return override
    if _exe_dir is None:
        _exe_dir = wb.call("app.info")["exe_dir"]
    return os.path.join(_exe_dir, override or FACTION_FOLDER)


def _read_ai_data() -> dict[str, Any]:
    """AIData.ini as the game reads it: the default file, then the one that overrides it."""
    data: dict[str, Any] = {"settings": {}, "sides": {}, "build_lists": {}, "files": []}
    for path in ai_data_file.FILES:
        found = wb.call("game.read_file", path=path)
        if found.get("exists"):
            ai_data_file.merge(data, ai_data_file.parse(found["text"]))
            data["files"].append(path)
    return data


def _side_of(player: str, side: str | None) -> str:
    if side:
        return side
    wanted = _player_faction(player)
    for entry in wb.call("sides.list_factions")["factions"]:
        if entry["name"] == wanted:
            return entry["side"]
    raise ToolError(f"faction '{wanted}' of player '{player}' is unknown; pass side")


@mcp.tool()
def aidata_get(side: str | None = None, entries: bool = False) -> dict:
    """AIData.ini, the skirmish AI's settings outside of the scripts, read the way the game
    reads it. Without side: the global settings (build and team intervals, wealthy and poor
    thresholds, guard ranges ...) and per side the number of build list entries. With side
    (China, ChinaTankGeneral ...; the side of a faction, see sides_list_factions):
    - gatherers: how many supply gatherers the AI keeps per difficulty;
    - base_defense: the structure SKIRMISH_BUILD_BASE_DEFENSE_* actions place;
    - skill_sets: up to five priority lists for spending General's points. A skirmish AI
      takes one at random unless a script chooses with PLAYER_SELECT_SKILLSET, then buys the
      first sciences of the list it is able to buy whenever it has points. Without any, the AI
      buys no sciences;
    - build_list: the SkirmishBuildList, the base layout: per structure how many places it
      has. A build script (SKIRMISH_BUILD_BUILDING) only says what to build; the place comes
      from here, relative to the command center, and a structure without an entry is never
      built. entries=true lists every entry with position, angle and rebuilds."""
    data = _read_ai_data()
    if not data["files"]:
        raise ToolError("the game data has no AIData.ini")
    if side is None:
        return {"files": data["files"], "settings": data["settings"],
                "sides": {name: {"build_list_entries": len(data["build_lists"].get(name, [])), **info["gatherers"]}
                          for name, info in data["sides"].items()},
                "build_lists_without_side_info": [n for n in data["build_lists"] if n not in data["sides"]]}
    result = ai_data_file.for_side(data, side)
    if len(result) == 1:
        raise ToolError(f"AIData.ini has nothing for side '{side}'; sides: {', '.join(sorted(set(data['sides']) | set(data['build_lists'])))}")
    if entries:
        result["entries"] = next((v for n, v in data["build_lists"].items() if n.lower() == side.lower()), [])
    return result


def _faction_of_side(side: str) -> str | None:
    for entry in wb.call("sides.list_factions")["factions"]:
        if entry["side"].lower() == side.lower():
            return entry["name"]
    return None


@mcp.tool()
def aidata_set_side(side: str, gatherers: dict[str, int] | None = None, base_defense: str | None = None,
                    skill_sets: dict[str, list[str]] | None = None, copy_from: str | None = None,
                    replace_in: str | None = None, path: str | None = None, allow_unknown: bool = False,
                    directory: str | None = None) -> dict:
    """Changes the SideInfo block of a side in AIData.ini: what the skirmish AI is given outside
    of its scripts (see aidata_get). Only what is passed changes:
    - gatherers: {"easy": n, "normal": n, "hard": n}, gatherers kept per supply center;
    - base_defense: the structure the "build base defense" script actions place;
    - skill_sets: {"1": [sciences in buying order], ...} for sets 1 to 5; a list replaces the
      whole set, [] empties it. At most 35 sciences per set.
    The file is edited line by line: comments, spacing and every line that does not change stay
    as they are; a removed science takes its line with it, a new one goes next to its
    neighbour in the list. A side without a block gets one after the last SideInfo; copy_from
    names a side whose block (with its comments) it starts from.
    Checked against the faction description (faction_generate): the base defense must be a
    structure of the faction, every science one it can purchase. allow_unknown skips that.
    Without replace_in nothing is written: the result holds the new block and a diff against
    the game's AIData.ini. path writes the whole edited text to a new file. replace_in names
    the AIData.ini to change (the old file is kept as <name>.bak). The game and WorldBuilder
    read AIData.ini at startup."""
    sets: dict[int, list[str]] = {}
    for key, wanted in (skill_sets or {}).items():
        if not str(key).isdigit() or not 1 <= int(key) <= ai_data_file.MAX_SKILL_SETS:
            raise ToolError(f"skill sets are numbered 1 to {ai_data_file.MAX_SKILL_SETS}")
        if len(wanted) > ai_data_file.MAX_SKILL_SET_SCIENCES:
            raise ToolError(f"skill set {key} has {len(wanted)} sciences; the game keeps {ai_data_file.MAX_SKILL_SET_SCIENCES}")
        if len(set(wanted)) != len(wanted):
            raise ToolError(f"skill set {key} lists a science twice")
        sets[int(key)] = list(wanted)

    if not allow_unknown and (base_defense is not None or sets):
        faction = _faction_of_side(side)
        try:
            doc = faction_data.load(faction, _faction_dir(directory)) if faction else None
        except (OSError, ValueError, yaml.YAMLError):
            doc = None
        if doc is None:
            raise ToolError(f"no faction description for side '{side}' to check against; run faction_generate or pass allow_unknown")
        problems = []
        if base_defense is not None:
            obj = doc["objects"].get(base_defense)
            if obj is None or obj["category"] != "structure":
                problems.append(f"{base_defense} is not a structure of {doc['meta']['faction']}")
            elif obj.get("unavailable"):
                problems.append(f"{base_defense} cannot be built: {obj['unavailable']}")
        for number, wanted in sets.items():
            for science in wanted:
                info = doc["sciences"].get(science)
                if info is None:
                    problems.append(f"skill set {number}: {science} is not a science of {doc['meta']['faction']}")
                elif not info.get("point_cost"):
                    problems.append(f"skill set {number}: {science} cannot be purchased")
        if problems:
            raise ToolError("; ".join(problems[:12]) + (f"; and {len(problems) - 12} more" if len(problems) > 12 else ""))

    try:
        if replace_in:
            target = os.path.abspath(replace_in)
            with open(target, encoding="latin-1", newline="") as f:
                old = f.read()
        else:
            target = None
            old = wb.call("game.read_file", path=ai_data_file.FILES[0]).get("text")
            if old is None:
                raise ToolError("the game data has no " + ai_data_file.FILES[0])
        edited = ai_data_file.edit_side_info(old, side, gatherers, base_defense, sets, copy_from)
        newline = "\r\n" if "\r\n" in old else "\n"
        import difflib
        diff = [line for line in difflib.unified_diff(old.split(newline), edited["text"].split(newline), lineterm="", n=1)][2:]
        result: dict[str, Any] = {"side": side, "created": edited["created"], "changed_lines": sum(1 for d in diff if d[:1] in "+-"),
                                  "diff": diff[:200], "block": edited["block"]}
        if len(diff) > 200:
            result["diff_truncated"] = len(diff)
        if path:
            full = os.path.abspath(path)
            with open(full, "w", encoding="latin-1", newline="") as f:
                f.write(edited["text"])
            result["path"] = full
        if target:
            if edited["text"] != old:
                with open(target + ".bak", "w", encoding="latin-1", newline="") as f:
                    f.write(old)
                with open(target, "w", encoding="latin-1", newline="") as f:
                    f.write(edited["text"])
                result.update(changed=target, backup=target + ".bak")
            else:
                result["changed"] = None
            del result["block"]
        return result
    except (OSError, ValueError) as e:
        raise ToolError(str(e)) from e


@mcp.tool()
def buildlist_from_aidata(player: str, side: str | None = None, command_center_at: list[float] | None = None) -> dict:
    """Puts the SkirmishBuildList of a side from AIData.ini into the build list of a player of
    the open map, REPLACING that list (one undo step), so that the base layout can be seen and
    edited with WorldBuilder's Build List tool or the buildlist_* tools. side defaults to the
    side of the player's faction. The layout keeps the coordinates it was made with (around
    500, 500), so the map should be about 1000 units wide; command_center_at=[x, y] moves it
    so that the command center lies there. Write it back with buildlist_export."""
    side = _side_of(player, side)
    data = _read_ai_data()
    found = next((v for n, v in data["build_lists"].items() if n.lower() == side.lower()), None)
    if found is None:
        raise ToolError(f"AIData.ini has no SkirmishBuildList for side '{side}'")
    dx = dy = 0.0
    if command_center_at is not None:
        centers = wb.call("objects.info", templates=[e["template"] for e in found][:60])["templates"]
        names = {t["template"] for t in centers if "COMMANDCENTER" in t["kind_of"]}
        center = next((e for e in found if e["template"] in names), found[0])
        dx, dy = command_center_at[0] - center["x"], command_center_at[1] - center["y"]
    placed = [{k: v for k, v in dict(e, x=e["x"] + dx, y=e["y"] + dy).items() if k != "automatically_build"} for e in found]
    result = wb.call("buildlist.set", player=player, entries=placed)
    result["side"] = side
    return result


@mcp.tool()
def buildlist_export(player: str, side: str | None = None, path: str | None = None, replace_in: str | None = None) -> dict:
    """The build list of a player of the open map as a SkirmishBuildList block for AIData.ini,
    as WorldBuilder's Build List export writes it. side is the name the block gets (default:
    the side of the player's faction). Positions are written as they are in the map; the game
    takes them relative to the command center entry, which the list must contain. Returns the
    text; path also writes it to a file. replace_in names an AIData.ini to change: the side's
    block is replaced there (or added inside the AIData block) and the old file kept as
    <name>.bak. The game and WorldBuilder read AIData.ini at startup."""
    side = _side_of(player, side)
    lists = wb.call("buildlist.list", player=player)["players"]
    entries = lists[0]["build_list"] if lists else []
    if not entries:
        raise ToolError(f"player '{player}' has no build list entries")
    text = ai_data_file.format_build_list(side, entries)
    result: dict[str, Any] = {"side": side, "entries": len(entries), "text": text}
    try:
        if path:
            full = os.path.abspath(path)
            with open(full, "w", encoding="latin-1", newline="\r\n") as f:
                f.write(text)
            result["path"] = full
        if replace_in:
            target = os.path.abspath(replace_in)
            with open(target, encoding="latin-1", newline="") as f:
                old = f.read()
            new, replaced = ai_data_file.replace_build_list(old, side, text)
            with open(target + ".bak", "w", encoding="latin-1", newline="") as f:
                f.write(old)
            with open(target, "w", encoding="latin-1", newline="") as f:
                f.write(new)
            result.update(changed=target, backup=target + ".bak", block="replaced" if replaced else "added")
            del result["text"]
    except OSError as e:
        raise ToolError(str(e)) from e
    return result


@mcp.tool()
def faction_generate(faction: str, directory: str | None = None) -> dict:
    """Writes the description of a faction that AI scripts and teams are planned from, into the
    `worldbuilder_factions` folder next to the WorldBuilder executable, i.e. in the game or mod
    folder (directory or WB_MCP_FACTION_DIR name another one; relative to the executable):
    - <Faction>.generated.json: the facts of faction_describe, cleaned and extended. Test,
      cinematic and unselectable types are dropped; every type gets `tier` (structures to build
      before it), `build_path` with `path_cost`, `unlocks`, `combat` (range, targets, damage per
      second against infantry, vehicle, aircraft and structure armor, and per 1000 credits),
      `role_guess`, `phase_guess`, and `unavailable` when its prerequisites cannot be met by
      the faction. Rewritten on every run; run it again after the game data changed.
    - <Faction>.overrides.yaml: the tuning, by hand: `slots` (abstract roles such as barracks,
      main_battle, anti_air -> types), per unit role, phase, ai_use, weight, counters, micro and
      notes, plus `exclude` / `include` lists for the next run. Written once from the guesses,
      then never touched; later runs report types that are new or gone.
    - <Faction>.doctrine.md: free text on how the faction should play. A skeleton, written once.
    Read the result with faction_get. WorldBuilder reads the game data at startup: restart it
    after INI changes."""
    raw = wb.call("faction.describe", faction=faction, scope="side")
    try:
        return faction_data.generate(raw, raw["faction"]["name"], _faction_dir(directory),
                                     ai_data_file.for_side(_read_ai_data(), raw["faction"]["side"]))
    except (ValueError, OSError, yaml.YAMLError) as e:
        raise ToolError(str(e)) from e


@mcp.tool()
def faction_get(faction: str, view: Literal["overview", "tree", "units", "unit", "upgrades", "sciences",
                                             "special_powers", "other_objects", "ai_data"] = "overview",
                names: list[str] | None = None, directory: str | None = None) -> dict:
    """One view of a faction description made by faction_generate, with the overrides merged in
    (faction is the name used there, e.g. FactionChina). Start with the overview.
    - overview: starting set, slots, every type with role, phase, tier and cost, and the doctrine.
    - tree: per type what builds it, prerequisites, what it unlocks and builds, build path, cost.
    - units: the combat table: health, speed, range, targets, damage per second by target kind,
      stealth, detection, cargo, and the tuning notes. Structures with weapons under `defenses`.
    - unit: everything about the types in `names`, with their weapons and armors.
    - upgrades, sciences, special_powers: cost, where researched, what they need and unlock.
    - other_objects: types of the side that nothing builds (spawned, AI-only, leftovers).
    - ai_data: what AIData.ini gives the side: places per structure in its base layout
      (SkirmishBuildList), gatherers, base defense, science skill sets.
    Damage per second is against the faction's commonest armor of each kind (`reference_armor`)."""
    try:
        return faction_data.view(faction_data.load(faction, _faction_dir(directory)), view, names)
    except (ValueError, OSError, yaml.YAMLError) as e:
        raise ToolError(str(e)) from e


@mcp.tool()
def faction_check(player: str, faction: str | None = None, include_teams: bool = False, max_where: int = 8,
                  directory: str | None = None) -> dict:
    """Checks the scripts and teams of a player in the open map against what its faction can
    do, using the description made by faction_generate. faction defaults to the player's
    faction. Equal findings are folded into one entry with `count` and the first `max_where`
    places. Errors are things that cannot work:
    - teams with types the faction does not have, cannot build (`unavailable`), structures,
      types nothing builds, more units than the build limit allows, minimum above maximum;
    - teams and build scripts that need a structure no build script builds;
    - build scripts for types that are unavailable or no structures; upgrades whose research
      structure is never built;
    - build scripts for structures that have no place in the side's SkirmishBuildList in
      AIData.ini (the action then does nothing), or a list without a command center.
    Warnings want a look: types, upgrades, sciences, special powers and command buttons of
    other factions, types marked ai_use: never, ships mixed with land units. Infos are
    judgement calls: combat teams without anti-air, large speed differences, a power deficit,
    core structures never built. `unused` lists combat units in no team, structures never
    built and upgrades never mentioned: what the AI leaves on the table.
    include_teams adds a row per team: cost range, tier, roles, structures needed, anti-air.
    This checks what can be built, not whether scripts refer to things that exist
    (scripts_verify, scripts_missing) or whether the AI plays well."""
    if faction is None:
        for side in wb.call("sides.list")["players"]:
            if side.get("playerName") == player:
                faction = side.get("playerFaction")
        if not faction:
            raise ToolError(f"player '{player}' has no faction set; pass faction")
    dump = wb.call("scripts.dump", player=player, include_teams=True)
    order = wb.call("scripts.build_order", player=player)
    try:
        doc = faction_data.load(faction, _faction_dir(directory))
        return faction_data.check(doc, player, dump, order, include_teams, max_where)
    except (ValueError, OSError, yaml.YAMLError) as e:
        raise ToolError(str(e)) from e


@mcp.tool()
def ai_framework_generate(players: list[str] | None = None, directory: str | None = None) -> dict:
    """Finds the skeleton the AI players of the open map share, by comparing their scripts and
    teams with each other, and writes it to ai_framework.generated.json in the faction folder
    (see faction_generate). players defaults to those whose name starts with "Skirmish", or
    all players with scripts. Names are compared with what is particular to a player taken
    out: its name becomes <PLAYER>, the word its own teams and groups start with <TAG>.
    It records: the group layout; every script with a class (framework: identical in all
    players; parameterized: same structure, some parameters differ, kept as a template with
    ${n} holes and the value per player; partial: only some players; divergent: same name,
    different structure; specific: one player); flags, counters and timers with the scripts
    that write and read them; the scripts teams hook into; the team roster with units per
    player; object lists with members per player; the waypoint paths and areas the scripts
    expect of a map; attack priority sets. ai_framework.notes.md is written once as a skeleton
    for what only a person can say. Read the result with ai_framework_get; run again after the
    scripts changed."""
    dump = wb.call("scripts.dump", include_teams=True)
    if players is None:
        with_scripts = [e["player"] for e in dump["players"] if e.get("scripts") or any(g.get("scripts") for g in e.get("groups", []))]
        skirmish = [p for p in with_scripts if p.startswith("Skirmish")]
        players = skirmish if len(skirmish) >= 2 else with_scripts
    try:
        return framework_data.generate(dump, _faction_dir(directory), players)
    except (ValueError, OSError) as e:
        raise ToolError(str(e)) from e


@mcp.tool()
def ai_framework_get(view: Literal["overview", "groups", "scripts", "script", "symbols", "teams", "team_hooks",
                                   "script_calls", "object_lists", "map_names", "attack_priority_sets"] = "overview",
                     filter: str | None = None, names: list[str] | None = None, directory: str | None = None) -> dict:
    """One view of the AI framework found by ai_framework_generate. Without filter the larger
    views are summaries; with one they are complete for what matches.
    - overview: players and their tags, group layout, counts per script class, the notes.
    - groups: every group with its script count per player.
    - scripts: counts per group and class; filter by a class, a group or part of a name.
    - script: the scripts in `names` in full: flags, template, and per varying parameter the
      value each player uses (these are the places a new faction fills in).
    - symbols: flags, counters and timers shared by all players, and ones written but never
      read or read but never written; filter for the scripts that write and read.
    - teams: the roster all players share (name, priority, hooks); filter for all settings and
      the units per player.
    - team_hooks: scripts that teams run (production condition, on create, generic hooks ...).
    - script_calls: scripts that other scripts call, enable or disable.
    - object_lists: the named type lists and how many types each holds; filter for members.
    - map_names: waypoint paths and areas the scripts expect in every map.
    A new faction AI should keep the framework and parameterized scripts, the shared symbols,
    the shared team names and hooks, and fill the object lists with its own types."""
    try:
        return framework_data.view(framework_data.load(_faction_dir(directory)), view, filter, names)
    except (ValueError, OSError) as e:
        raise ToolError(str(e)) from e


def _player_faction(player: str) -> str:
    for side in wb.call("sides.list")["players"]:
        if side.get("playerName") == player and side.get("playerFaction"):
            return side["playerFaction"]
    raise ToolError(f"the map has no player '{player}' with a faction")


@mcp.tool()
def faction_map(from_faction: str, to_faction: str, overrides: dict[str, str] | None = None, full: bool = False,
                directory: str | None = None) -> dict:
    """Counterparts in one faction for the types, upgrades, sciences, special powers and command
    buttons of another, from their descriptions (faction_generate). Each is found by, in order:
    an override (the argument, or `counterparts` in the target's overrides file); the same
    name; the same name without the general's prefix (Tank_ChinaBarracks -> Nuke_ChinaBarracks);
    the same single slot (barracks -> barracks); for types, the nearest type with the same role
    and category by tier and cost; for buttons, the button on the counterpart type that builds,
    researches or fires the counterpart thing. Returns the count per method, the role matches
    (the weakest: review them) and what has no counterpart; full adds every pair."""
    try:
        folder = _faction_dir(directory)
        mapping = scaffold_data.map_names(faction_data.load(from_faction, folder), faction_data.load(to_faction, folder), overrides)
    except (ValueError, OSError, yaml.YAMLError) as e:
        raise ToolError(str(e)) from e
    if full:
        return mapping
    return {"from": mapping["from"], "to": mapping["to"], "summary": mapping["summary"],
            "by_role": {name: m["to"] for name, m in mapping["objects"].items() if m["by"] == "role"},
            "by_slot": {name: m["to"] for name, m in mapping["objects"].items() if m["by"] == "slot"},
            "unmapped": mapping["unmapped"]}


@mcp.tool()
def ai_scaffold(player: str, faction: str, like_player: str, tag: str, overrides: dict[str, str] | None = None,
                drop_unmapped: bool = False, dry_run: bool = True, path: str | None = None,
                directory: str | None = None) -> dict:
    """Derives the AI of `player` (faction `faction`) from the scripts and teams of
    `like_player`, an AI player of the open map, preferably of a related faction. Needs the
    descriptions of both factions (faction_generate) and the framework (ai_framework_generate).
    - Scripts all players share are copied unchanged, their lists of enemy types included.
    - Scripts that differ between players only in some parameters get those parameters
      translated; all other scripts get every type, upgrade, science, special power and command
      button of the source faction translated (see faction_map for how; overrides adds pairs).
    - Teams are copied with their units translated. `tag` (2-8 letters) replaces the source
      player's tag in team, group and script names; team names without it get it in front.
    What has no counterpart is left as it is and listed in `unmapped` with where it is used;
    drop_unmapped leaves those scripts and teams out instead. `mapped_by_role_only` are the
    weakest translations. dry_run (the default) changes nothing; path writes the result as a
    scripts_dump file to review or edit and load with scripts_load. With dry_run=false the
    player is created if missing and ALL its scripts and teams are replaced (one undo step);
    the result then includes faction_check. `side_info` says whether AIData.ini has a SideInfo
    and a base layout for the new side, and holds ready arguments for aidata_set_side with the
    source side's gatherers, base defense and science skill sets translated; without a
    SideInfo the AI buys no sciences. The outcome is a starting point: work through unmapped
    and the check, then fill in what makes the faction its own."""
    try:
        folder = _faction_dir(directory)
        src = faction_data.load(_player_faction(like_player), folder)
        dst = faction_data.load(faction, folder)
        skeleton = framework_data.load(folder)
        dump = wb.call("scripts.dump", include_teams=True)
        mapping = scaffold_data.map_names(src, dst, overrides)
        made = scaffold_data.scaffold(dump, skeleton, src, dst, like_player, player, tag, mapping, drop_unmapped)
    except (ValueError, OSError, yaml.YAMLError) as e:
        raise ToolError(str(e)) from e
    report = made["report"]
    report["mapping"] = mapping["summary"]
    # What the new side needs in AIData.ini, derived from the source side the same way.
    ai_data = _read_ai_data()
    source_side, new_side = src["faction"]["side"], dst["faction"]["side"]
    source_info = ai_data_file.for_side(ai_data, source_side)
    new_info = ai_data_file.for_side(ai_data, new_side)
    side_report: dict[str, Any] = {"side": new_side, "has_side_info": "skill_sets" in new_info,
                                   "has_build_list": "build_list" in new_info}
    if "skill_sets" in source_info:
        no_counterpart = []

        def science(name: str) -> str | None:
            found = mapping["sciences"].get(name, {}).get("to") or (name if name in dst["sciences"] else None)
            if found is None and name not in no_counterpart:
                no_counterpart.append(name)
            return found

        proposal: dict[str, Any] = {"side": new_side, "gatherers": source_info["gatherers"],
                                    "skill_sets": {str(i + 1): list(dict.fromkeys(s for s in map(science, names) if s))
                                                   for i, names in enumerate(source_info["skill_sets"])}}
        defense = mapping["objects"].get(source_info.get("base_defense", ""), {}).get("to")
        if defense:
            proposal["base_defense"] = defense
        if "skill_sets" not in new_info:
            proposal["copy_from"] = source_side
        side_report["aidata_set_side"] = proposal
        side_report["sciences_without_counterpart"] = no_counterpart
    report["side_info"] = side_report
    if path:
        full = os.path.abspath(path)
        with open(full, "w", encoding="utf-8", newline="\n") as f:
            json.dump(made["document"], f, indent=1, ensure_ascii=False)
        report["path"] = full
    if dry_run:
        report["dry_run"] = True
        return report
    report["load"] = wb.call("scripts.load", data=made["document"], create_players=True)
    checked = faction_data.check(dst, player, wb.call("scripts.dump", player=player, include_teams=True),
                                 wb.call("scripts.build_order", player=player))
    report["check"] = {"issue_counts": checked["issue_counts"],
                       "errors": [i for i in checked["issues"] if i["severity"] == "error"]}
    return report


def _game_dir() -> str:
    _faction_dir(None)
    return _exe_dir or ""


@mcp.tool()
def ai_match_run(map: str, players: list[str], minutes: float = 15, seed: int = 1, interval_seconds: float = 10,
                 starting_cash: int | None = None, wait_seconds: float = 240, log: str | None = None,
                 scripts: str | Literal["open"] | None = None, ai_data: str | None = None, headless: bool = True) -> dict:
    """Lets the game play a skirmish between AI players by itself and write down what happens.
    It starts generalszh.exe from WorldBuilder's folder without a window: only the game logic
    runs, with the local player as observer, as fast as the machine allows; the game closes
    when one side is left or after `minutes` of game time. headless=false shows the match in
    a window instead (slower, and it takes the screen), to watch what the AI does.
    - map: name of a skirmish map or part of its path ("armed neighbors").
    - players: two or more of "Faction:difficulty[:team]", e.g. ["FactionChina:hard",
      "FactionGLA:hard"]; difficulty easy, normal or hard; players with the same team number
      are allies. Start positions are random, fixed by seed.
    - seed: the same seed gives the same match as long as scripts and game data are the same.
    By default the game uses the installed AI (Data/Scripts/SkirmishScripts.scb and
    AIData.ini). To try changes without installing them:
    - scripts="open" plays with the scripts and teams open in WorldBuilder right now (they
      are exported to a file next to the log; the open map must hold the whole AI set, i.e.
      the Skirmish... players), or scripts=<path of a .scb file> plays with that file;
    - ai_data=<path> reads that AIData.ini on top of the game's: its settings, SideInfo
      blocks and build lists replace those of the same name (see aidata_set_side and
      buildlist_export with path).
    The report names the files a match used.
    A match runs several times faster than real time. The call waits up to wait_seconds; if
    the match is still going it returns status "running" and ai_match_report can be asked
    later. The log (JSON lines) goes to worldbuilder_factions/matches unless log names a file."""
    game_dir = _game_dir()
    if log is None:
        stamp = time.strftime("%Y%m%d_%H%M%S")
        name = "".join(c if c.isalnum() else "_" for c in map)[:30]
        log = os.path.join(_faction_dir(None), "matches", f"{stamp}_{name}_seed{seed}.jsonl")
    log = os.path.abspath(log)
    if scripts == "open":
        owners = [p["playerName"] for p in wb.call("sides.list")["players"] if p.get("playerName", "").startswith("Skirmish")]
        if not owners:
            raise ToolError("the open map has no Skirmish... players: import the AI script set first, or pass a .scb path")
        scripts = os.path.splitext(log)[0] + ".scb"
        os.makedirs(os.path.dirname(scripts), exist_ok=True)
        wb.call("scripts.export", path=scripts, include_players=True, overwrite=True, backup=False)
    try:
        result = match_data.run(game_dir, map, players, log, minutes, seed, interval_seconds, starting_cash, wait_seconds,
                                scripts=os.path.abspath(scripts) if scripts else None,
                                ai_data=os.path.abspath(ai_data) if ai_data else None, headless=headless)
    except OSError as e:
        raise ToolError(str(e)) from e
    if scripts:
        result["scripts"] = os.path.abspath(scripts)
    if os.path.exists(result["log"]):
        brief = match_data.report(match_data.read(result["log"]))
        result.update({k: brief[k] for k in ("status", "reason", "winners", "game_minutes") if k in brief})
        if brief.get("status") == "error":
            result["error"] = brief["reason"]
    elif result["status"] == "finished":
        result["status"] = "error"
        result["error"] = "the game ended without writing a log (did it start?)"
    return result


@mcp.tool()
def ai_match_report(log: str | None = None, player: str | None = None, compare_scripts: bool = True, limit: int = 25) -> dict:
    """What happened in an AI match (ai_match_run), from its log; log defaults to the newest
    one. Per player (or the one named by match name, faction, or script player such as
    SkirmishChina):
    - score, money at the end, rank; economy (money range, earned, spent, when power ran short);
    - structures and units in the order they first appeared, with seconds of game time and
      how many were built and lost;
    - never_built: structures and combat units of the faction that never appeared (needs the
      faction description);
    - longest_pause: the longest time in which nothing was built;
    - teams the AI started, with counts and first time; sciences and upgrades in order;
    - never_ran (compare_scripts, needs the same AI set open in WorldBuilder): scripts that
      could have run on this difficulty and never did, and teams that were never started.
    Each player's `trouble` lists what its scripts and its AI complained about, most frequent
    first ("Dozer unable to build - money or technology missing", "could not find building ...
    in the building template list", scripts not defined ...); `trouble` at the top holds what
    was reported outside of any player. Times are game seconds. A match that hit the time
    limit has no winner; compare scores instead."""
    try:
        if log is None:
            folder = os.path.join(_faction_dir(None), "matches")
            logs = sorted((os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(".jsonl")), key=os.path.getmtime) \
                if os.path.isdir(folder) else []
            if not logs:
                raise ToolError("no match logs yet; run ai_match_run")
            log = logs[-1]
        records = match_data.read(os.path.abspath(log))
        factions: dict[str, Any] = {}
        for record in records:
            if record["t"] == "start":
                for entry in record["players"]:
                    try:
                        factions[entry["faction"]] = faction_data.load(entry["faction"], _faction_dir(None))
                    except (OSError, ValueError, yaml.YAMLError):
                        pass
        dump = None
        if compare_scripts:
            try:
                dump = wb.call("scripts.dump", include_teams=True)
            except ToolError:
                dump = None
        result = match_data.report(records, dump, factions, player, limit)
        result["log"] = os.path.abspath(log)
        return result
    except OSError as e:
        raise ToolError(str(e)) from e


@mcp.tool()
def buildlist_list(player: str | None = None) -> dict:
    """The players' build lists (WorldBuilder's Build List tool): structures a player's AI
    builds at fixed spots, or starts with, in order. Used by campaign and scripted-map AI;
    skirmish AI sets usually build through scripts instead (see scripts_build_order)."""
    return wb.call("buildlist.list", player=player)


@mcp.tool()
def buildlist_add(player: str, template: str, x: float, y: float, z: float = 0, angle_deg: float = 0,
                  name: str | None = None, script: str | None = None, initially_built: bool | None = None,
                  rebuilds: int | Literal["unlimited"] | None = None, health: int | None = None,
                  whiner: bool | None = None, unsellable: bool | None = None, repairable: bool | None = None,
                  index: int | None = None) -> dict:
    """Adds a structure to a player's build list at world position x, y (index = position in the
    list, default the end; the AI builds in list order). name lets scripts refer to the building,
    script runs when it is built, initially_built makes it exist at game start, rebuilds is how
    often the AI rebuilds it when destroyed, health its starting health in percent."""
    return wb.call("buildlist.add", player=player, template=template, x=x, y=y, z=z, angle_deg=angle_deg, name=name,
                   script=script, initially_built=initially_built, rebuilds=rebuilds, health=health, whiner=whiner,
                   unsellable=unsellable, repairable=repairable, index=index)


@mcp.tool()
def buildlist_update(player: str, index: int, new_index: int | None = None, template: str | None = None,
                     x: float | None = None, y: float | None = None, z: float | None = None,
                     angle_deg: float | None = None, name: str | None = None, script: str | None = None,
                     initially_built: bool | None = None, rebuilds: int | Literal["unlimited"] | None = None,
                     health: int | None = None, whiner: bool | None = None, unsellable: bool | None = None,
                     repairable: bool | None = None) -> dict:
    """Changes the given fields of a build list entry (index from buildlist_list); new_index moves
    it to another position in the list."""
    return wb.call("buildlist.update", player=player, index=index, new_index=new_index, template=template, x=x, y=y,
                   z=z, angle_deg=angle_deg, name=name, script=script, initially_built=initially_built,
                   rebuilds=rebuilds, health=health, whiner=whiner, unsellable=unsellable, repairable=repairable)


@mcp.tool()
def buildlist_delete(player: str, index: int) -> dict:
    """Deletes a build list entry."""
    return wb.call("buildlist.delete", player=player, index=index)


@mcp.tool()
def sides_set(name: str, new_name: str | None = None, properties: dict[str, Any] | None = None) -> dict:
    """Changes a player. properties are the player dict keys sides_list shows (playerFaction,
    playerDisplayName, playerIsHuman, playerAllies, playerEnemies, ...). new_name renames the
    player and what refers to the name: its teams' owner, its default team "team<name>", other
    players' allies/enemies and player/team parameters in scripts. Renaming fails while map
    objects belong to its teams."""
    return wb.call("sides.set", name=name, new_name=new_name, properties=properties)


@mcp.tool()
def sides_delete(name: str, force: bool = False) -> dict:
    """Deletes a player. With scripts or teams it needs force=true and deletes them too. Fails
    while map objects belong to its teams."""
    return wb.call("sides.delete", name=name, force=force)


@mcp.tool()
def sides_add_player(faction: str, name: str | None = None, display_name: str | None = None,
                     is_human: bool = True, allies: str | None = None, enemies: str | None = None) -> dict:
    """Adds a player (and its default team "team<name>"). allies/enemies are space separated player names."""
    return wb.call("sides.add_player", faction=faction, name=name, display_name=display_name, is_human=is_human,
                   allies=allies, enemies=enemies)


# --------------------------------------------------------------------------------------------------
# Waypoints, polygons, roads
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def waypoints_list() -> dict:
    """All waypoints (with object ids, waypoint ids, names, path labels) and the links between them."""
    return wb.call("waypoints.list")


@mcp.tool()
def waypoints_add(x: float, y: float, name: str | None = None, path_labels: list[str] | None = None,
                  link_from: str | int | None = None) -> dict:
    """Adds a waypoint. Special names include Player_1_Start .. Player_8_Start for skirmish start
    positions. link_from: name or waypoint id of an existing waypoint to link to the new one."""
    return wb.call("waypoints.add", x=x, y=y, name=name, path_labels=path_labels, link_from=link_from)


@mcp.tool()
def waypoints_link(from_waypoint: str | int, to_waypoint: str | int) -> dict:
    """Links two waypoints (names or waypoint ids) into a path. Not undoable."""
    return wb.call("waypoints.link", **{"from": from_waypoint, "to": to_waypoint})


@mcp.tool()
def waypoints_unlink(from_waypoint: str | int, to_waypoint: str | int) -> dict:
    """Removes a waypoint link. Not undoable."""
    return wb.call("waypoints.unlink", **{"from": from_waypoint, "to": to_waypoint})


@mcp.tool()
def polygons_list(include_points: bool = False) -> dict:
    """Trigger areas and water areas."""
    return wb.call("polygons.list", include_points=include_points)


@mcp.tool()
def polygons_add(points: list[list[float]], kind: Literal["area", "water", "river"] = "area",
                 name: str | None = None, water_height: float | None = None, river_start: int | None = None) -> dict:
    """Adds a polygon from [x, y] world points. area: script trigger area. water: a water surface at
    water_height (world units; compare with terrain world heights). river: flowing water, starting at
    point index river_start."""
    return wb.call("polygons.add", points=points, kind=kind, name=name, water_height=water_height,
                   river_start=river_start)


@mcp.tool()
def polygons_delete(id: int) -> dict:
    """Deletes a polygon by id."""
    return wb.call("polygons.delete", id=id)


@mcp.tool()
def roads_list_types() -> dict:
    """Road types and bridge types. Landmark bridges are placed with objects_place instead."""
    return wb.call("roads.list_types")


@mcp.tool()
def roads_add(type: str, points: list[list[float]],
              corners: Literal["curved", "angled", "tight"] = "curved") -> dict:
    """Adds a road along [x, y] world points (one segment per consecutive pair), or a bridge with
    exactly 2 points."""
    return wb.call("roads.add", type=type, points=points, corners=corners)


@mcp.tool()
def roads_route(type: str, from_point: list[float], to_point: list[float], via: list[list[float]] | None = None,
                corners: Literal["auto", "curved", "angled", "tight"] = "auto", flatten: bool = True,
                flatten_width: float = 50, flatten_feather: float = 40, slope_weight: float = 0.5,
                max_grade: float = 10, avoid_cliffs: bool = True, avoid_water: bool = True,
                avoid_areas: list[str] | None = None, object_clearance: float = 20, straighten: float = 1.25,
                snap: float = 25) -> dict:
    """Routes a road from from_point to to_point ([x, y] world units), optionally through via points,
    finding its own way over the terrain: it goes around cliffs, water, structures (plus
    object_clearance) and avoid_areas, prefers gentle slopes (slope_weight) and never climbs steps
    steeper than max_grade raw height units per cell (0 = no limit). The path is then straightened (straighten
    = how much costlier a straight shortcut may be, 0 keeps every grid step) and built like roads_add.
    corners="auto" uses tight corners only at sharp bends. flatten levels the ground under the road
    to a smoothed profile (flatten_width plus a flatten_feather falloff on each side). Ends within
    `snap` of an existing road point join that road. Fails with a hint when no route exists; water
    needs a bridge (roads_add with a bridge type). Road, terrain and all are one undo step."""
    return wb.call("roads.route", type=type, to=to_point, via=via, corners=corners, flatten=flatten,
                   flatten_width=flatten_width, flatten_feather=flatten_feather, slope_weight=slope_weight,
                   max_grade=max_grade, avoid_cliffs=avoid_cliffs, avoid_water=avoid_water,
                   avoid_areas=avoid_areas, object_clearance=object_clearance, straighten=straighten,
                   snap=snap, **{"from": from_point})


# --------------------------------------------------------------------------------------------------
# Skirmish AI
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def map_generate_starts(players: int, base_radius: float = 300, edge_margin: float | None = None,
                        angle_deg: float | None = None, distance: float = 1.0, flatten: bool = True,
                        flatten_feather: float = 150, replace: bool = True) -> dict:
    """Places the start positions for `players` (2..8) as Player_<N>_Start waypoints, spaced at even
    angles around the map center (Genesis StartingPositionGenerator), so the layout is rotationally
    symmetric. Each start lies on a rectangle inset from the playable edge by edge_margin (default
    base_radius + 50); distance < 1 pulls them toward the center. angle_deg is the direction of
    player 1 from the center (0 = east, counterclockwise); by default it points at the south-west
    corner, which puts 2 players on the diagonal and 4 players into the corners. flatten levels each
    base (base_radius, plus flatten_feather falloff) to its average height, lifted above water if
    needed. replace=true removes existing start waypoints first. Fails if the starts would be closer
    than two base radii. Follow with ai_skirmish_setup for the AI data. One undo step; object ids are
    invalidated."""
    return wb.call("map.generate_starts", players=players, base_radius=base_radius, edge_margin=edge_margin,
                   angle_deg=angle_deg, distance=distance, flatten=flatten, flatten_feather=flatten_feather,
                   replace=replace)


@mcp.tool()
def ai_skirmish_setup(inner_radius: float = 350, outer_radius: float = 600, flank_angle: float = 70,
                      backdoor_angle: float = 70, path_points: int = 5, combat_zone: bool = True,
                      replace: bool = True) -> dict:
    """Creates everything the skirmish AI looks up by name, for every Player_<N>_Start waypoint:
    - areas InnerPerimeter<N> / OuterPerimeter<N> (circles of the given radii around the start),
    - three approach paths INTO base N, labeled Center<N>, Flank<N> and Backdoor<N> (the number is
      the defending player; attackers join at the nearest waypoint and follow the links into the
      base). Flank/backdoor swing out by flank_angle/backdoor_angle degrees to either side.
      Waypoints are kept inside the map and moved off cliffs and water.
    - a CombatZone area between the bases (used by the mod's scripts).
    replace=true (default) first removes existing perimeters, CombatZone and approach-path waypoints
    (including unnumbered Center/Flank/Backdoor labels, which the AI ignores), so it can be re-run
    after moving starts. Place the start waypoints first. One undo step; object ids are invalidated."""
    return wb.call("ai.skirmish_setup", inner_radius=inner_radius, outer_radius=outer_radius,
                   flank_angle=flank_angle, backdoor_angle=backdoor_angle, path_points=path_points,
                   combat_zone=combat_zone, replace=replace)


@mcp.tool()
def ai_skirmish_check() -> dict:
    """Checks whether the map has what the skirmish AI needs: per start position the perimeter
    areas and the number of waypoints on its Center/Flank/Backdoor approach paths, plus a list of
    problems (missing areas or paths, path labels without a player number)."""
    return wb.call("ai.skirmish_check")


# --------------------------------------------------------------------------------------------------
# Scripts and teams
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def scripts_list(player: str | None = None, group: str | None = None, filter: str | None = None,
                 detail: Literal["auto", "groups", "names", "full"] = "auto") -> dict:
    """Scripts and script groups (folders) per player. Every script belongs to a player ("" is the
    neutral player), either at the top level or in a group; they run in the listed order.
    player and group ("" = the scripts outside of groups) narrow it; filter is a substring of the
    script name. detail: "groups" = groups with script counts only, "names" = script names,
    "full" = name, flags and comment per script. "auto" picks full for up to 120 matching scripts,
    names up to 400 and groups beyond (AI script sets hold thousands), and says so in `note`.
    Start with detail="groups" on a big set, then list one group."""
    return wb.call("scripts.list", player=player, group=group, filter=filter, detail=detail)


@mcp.tool()
def scripts_find(text: str | None = None, value: str | None = None, type: str | None = None,
                 param_type: str | None = None, player: str | None = None, limit: int = 50, offset: int = 0,
                 max_hits_per_script: int = 10) -> dict:
    """Searches inside scripts and returns the matching conditions and actions per script (with
    `where`, e.g. actions[2], and the editor's wording). All given filters must hold for the same
    condition or action:
    - value: exact parameter value (case-insensitive), e.g. a team, script, waypoint, area, object
      type, flag or counter name. Add param_type (TEAM, SCRIPT, OBJECT_TYPE, ...) to pin the kind.
    - text: substring of the editor's wording, e.g. "begins hunting" or part of a name.
    - type: condition or action type, e.g. TEAM_HUNT. param_type alone lists every use of that kind.
    With only text/value it also matches script names and comments, and returns `teams`: team
    properties holding that text (teams name scripts and unit types too). Use it before renaming
    or deleting something to see who refers to it. total_scripts counts all matches; page with
    limit/offset. Each script lists at most max_hits_per_script hits (hit_count has the real
    number); teams are capped at limit (total_team_hits)."""
    return wb.call("scripts.find", text=text, value=value, type=type, param_type=param_type, player=player,
                   limit=limit, offset=offset, max_hits_per_script=max_hits_per_script)


@mcp.tool()
def scripts_get(name: str, player: str | None = None, group: str | None = None,
                script_index: int | None = None) -> dict:
    """One script in full, in the form scripts_add/scripts_update take, plus `text`, the script as
    the editor words it. player, group ("" = not in a group) and script_index (position in its
    list, as scripts_list orders them) are only needed when several scripts share the name; the
    error then lists the candidates. The same selectors work on update, delete and move."""
    return wb.call("scripts.get", name=name, player=player, group=group, script_index=script_index)


@mcp.tool()
def scripts_add(script: dict[str, Any], player: str | None = None, group: str | None = None,
                index: int | None = None, allow_duplicate_name: bool = False) -> dict:
    """Adds a script to a player's top level (player; "" = neutral) or to a script group (group).
    index is the position in that list (default: at the end). script:
      {"name": "...", "comment": "", "active": true, "one_shot": true, "subroutine": false,
       "easy": true, "normal": true, "hard": true, "delay_seconds": 0,
       "conditions": [[{"type": "CONDITION_TRUE"}]],
       "actions": [{"type": "SET_FLAG", "params": ["MyFlag", true]}],
       "false_actions": []}
    Only name is required; the defaults are the ones shown. conditions is a list of OR clauses,
    each a list of conditions that must all hold (a flat list is one clause). Types and their
    parameters come from scripts_list_conditions / scripts_list_actions. params are given in
    order, one value per parameter type:
    - names (TEAM, SCRIPT, WAYPOINT, TRIGGER_AREA, SIDE, UNIT, OBJECT_TYPE, FLAG, COUNTER, ...): string.
      Special names: "<This Team>", "<This Player>", "<Local Player>", "<This Player's Enemy>".
    - INT, REAL: number. BOOLEAN: true/false. ANGLE: degrees. PERCENT: 0..100.
    - COORD3D: [x, y, z]. COLOR: "#RRGGBB" or "#AARRGGBB".
    - COMPARISON: LESS_THAN, LESS_EQUAL, EQUAL, GREATER_EQUAL, GREATER, NOT_EQUAL (or <, <=, ==, ...).
    - RELATION: ENEMY, NEUTRAL, FRIEND. AI_MOOD: SLEEP, PASSIVE, NORMAL, ALERT, AGGRESSIVE.
    - KIND_OF_PARAM, BUILDABLE, SURFACES_ALLOWED, SHAKE_INTENSITY, RADAR_EVENT_TYPE: the name.
    Returns `warnings`: references the editor would flag (missing team, script, waypoint, ...).
    The script is added anyway. A name must be new for that player unless allow_duplicate_name.
    One undo step."""
    return wb.call("scripts.add", script=script, player=player, group=group, index=index,
                   allow_duplicate_name=allow_duplicate_name)


@mcp.tool()
def scripts_update(name: str, script: dict[str, Any], player: str | None = None, group: str | None = None,
                   script_index: int | None = None, allow_duplicate_name: bool = False) -> dict:
    """Changes a script. `script` holds only the keys to change (same keys as scripts_add, e.g.
    {"active": false} or {"name": "NewName"}); conditions, actions and false_actions are each
    replaced as a whole list, so start from scripts_get to change one entry. Renaming does not
    update references in other scripts or teams; scripts_verify finds them. Returns `warnings`."""
    return wb.call("scripts.update", name=name, script=script, player=player, group=group,
                   script_index=script_index, allow_duplicate_name=allow_duplicate_name)


@mcp.tool()
def scripts_batch(commands: list[dict[str, Any]], results: Literal["summary", "full"] = "summary") -> dict:
    """Runs many script and team edits as ONE undo step, all or nothing: if a command fails,
    nothing is changed and the error names its index. Use it when building or reworking a set
    (WorldBuilder keeps only 15 undo steps). commands: [{"cmd": "scripts.add", "args": {...}}, ...]
    with the bridge names and arguments of the scripts_* / teams_* tools (dots instead of the
    first underscore: scripts.add, scripts.update, scripts.add_group, scripts.rename,
    scripts.clone_player with from/to, teams.set, teams.delete, sides.set, sides.delete).
    Returns the warnings of all commands, each with its `command` index; results="full" also
    returns every command's result."""
    return wb.call("scripts.batch", commands=commands, results=results)


def _strip_text(node: Any) -> None:
    """Removes the editor wording from a scripts.dump document."""
    if isinstance(node, dict):
        if "type" in node and "params" in node:
            node.pop("text", None)
        for value in node.values():
            _strip_text(value)
    elif isinstance(node, list):
        for value in node:
            _strip_text(value)


@mcp.tool()
def scripts_dump(path: str, player: str | None = None, include_teams: bool = True, text: bool = True) -> dict:
    """Writes all scripts, groups and teams (or one player's) to a readable JSON file: players
    with their properties, scripts in full (as scripts_get shows them) and team property dicts.
    Good for review, diffs, version control, backups and bulk edits; scripts_load reads it back.
    text=false leaves out the editor's wording of each condition and action (smaller file)."""
    doc = wb.call("scripts.dump", player=player, include_teams=include_teams)
    if not text:
        _strip_text(doc)
    full = os.path.abspath(path)
    with open(full, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
    return {"path": full, "players": [p["player"] for p in doc["players"]], "scripts": doc["script_count"],
            "teams": len(doc.get("teams", [])), "bytes": os.path.getsize(full)}


@mcp.tool()
def scripts_load(path: str, create_players: bool = True) -> dict:
    """Reads a scripts_dump JSON file and REPLACES, for every player in it, all of that player's
    scripts and groups, and (if the file has `teams`) all of its teams, with the file's contents.
    Players not in the file are untouched; players the map lacks are created from the file's
    properties. The whole file is checked like scripts_add / teams_set and nothing is changed if
    any part is invalid; the error names the player and script. One undo step. Run
    scripts_verify afterwards."""
    with open(os.path.abspath(path), encoding="utf-8") as f:
        doc = json.load(f)
    return wb.call("scripts.load", data=doc, create_players=create_players)


@mcp.tool()
def scripts_analyze(player: str | None = None, limit: int = 40) -> dict:
    """Looks for dead logic that scripts_verify cannot see, per category with a count and up to
    `limit` items: subroutines nothing calls, inactive scripts nothing enables, scripts without
    actions, flags and counters/timers that are read but never set or set but never read, empty
    groups, and teams that have no production condition and are named by no script. Findings are
    hints, not errors: a few are intended (e.g. scripts kept switched off)."""
    return wb.call("scripts.analyze", player=player, limit=limit)


@mcp.tool()
def scripts_trace(name: str, player: str | None = None, kind: Literal["auto", "script"] = "auto", depth: int = 4,
                  max_scripts: int = 60) -> dict:
    """Follows what a team or a script sets in motion, as a tree in the editor's wording. For a
    team: every script it names (production condition, on create, on destroyed, hooks ...). For
    each script: its conditions (`if`, one entry per OR clause), actions (`then` / `else`) and
    `leads_to`: scripts its actions run or enable, and scripts of the same player whose conditions
    wait for a flag, counter or timer it sets (`via` says which). A script already shown appears
    as shown_above; missing ones as missing. For a script, `started_by` lists the scripts and
    teams naming it. name is a team name or, with player (or kind="script"), a script name. AI
    behavior is spread over many small scripts; this shows one behavior in one place."""
    return wb.call("scripts.trace", name=name, player=player, kind=kind, depth=depth, max_scripts=max_scripts)


@mcp.tool()
def scripts_build_order(player: str, difficulty: Literal["E", "N", "H"] | None = None,
                        include_inactive: bool = True) -> dict:
    """A player's build order: every script that builds a structure or an upgrade (the
    SKIRMISH_BUILD_* and AI_PLAYER_BUILD_* actions), in the order the scripts are evaluated, with
    group, conditions (`if`, one entry per OR clause), what it builds, and whether it is inactive,
    repeats or only runs on some difficulties (difficulty "NH" = normal and hard). `totals` counts
    build actions per structure. difficulty keeps only steps that run on E, N or H."""
    return wb.call("scripts.build_order", player=player, difficulty=difficulty, include_inactive=include_inactive)


@mcp.tool()
def scripts_by_difficulty(player: str, limit: int = 40) -> dict:
    """A player's scripts and teams by difficulty. script_switches: how many scripts run on which
    difficulties according to their easy/normal/hard switches, with the names of those that do
    not run on all three. scripts_named / teams_named: scripts and teams whose name carries the
    word E, N or H ("SCM E Attack Team 1", "CT Def H"), the convention these AI sets use to tie
    things to a difficulty through their production conditions."""
    return wb.call("scripts.by_difficulty", player=player, limit=limit)


def _load_script_templates() -> list[dict[str, Any]]:
    with open(os.path.join(_PRESET_DIR, "script_templates.json"), encoding="utf-8") as f:
        return json.load(f)["templates"]


def _fill_template(node: Any, values: dict[str, Any]) -> Any:
    """Replaces ${name} placeholders; a string that is only a placeholder takes the value's type."""
    if isinstance(node, dict):
        return {key: _fill_template(value, values) for key, value in node.items()}
    if isinstance(node, list):
        return [_fill_template(value, values) for value in node]
    if isinstance(node, str):
        if node.startswith("${") and node.endswith("}") and node[2:-1] in values:
            return values[node[2:-1]]
        for key, value in values.items():
            node = node.replace("${" + key + "}", str(value))
    return node


@mcp.tool()
def scripts_list_templates() -> dict:
    """Script templates for scripts_apply_template: ready-made combinations of scripts and teams
    with parameters, such as a build-order step, an attack team with its condition and launch
    scripts, or a repeating timer. They are defined in presets/script_templates.json, where more
    can be added."""
    return {"templates": [{"name": t["name"], "description": t["description"], "params": t["params"]}
                          for t in _load_script_templates()]}


@mcp.tool()
def scripts_apply_template(template: str, params: dict[str, Any], dry_run: bool = False) -> dict:
    """Creates the scripts and teams of a template (see scripts_list_templates) with the given
    parameter values, as one undo step and all or nothing. Parameters without a default are
    required. dry_run returns the commands it would run (they are scripts_batch commands, so they
    can be adjusted and run with that instead). Returns the warnings of the created scripts and
    teams, e.g. unit types or scripts that do not exist."""
    found = [t for t in _load_script_templates() if t["name"] == template]
    if not found:
        raise ToolError(f"unknown template '{template}' (see scripts_list_templates)")
    values = {name: spec["default"] for name, spec in found[0]["params"].items() if "default" in spec}
    unknown = [name for name in params if name not in found[0]["params"]]
    if unknown:
        raise ToolError(f"template '{template}' has no parameter(s) {unknown}; it takes {list(found[0]['params'])}")
    values.update(params)
    missing = [name for name in found[0]["params"] if name not in values]
    if missing:
        raise ToolError(f"template '{template}' needs parameter(s) {missing}")
    commands = _fill_template(found[0]["commands"], values)
    if dry_run:
        return {"dry_run": True, "commands": commands}
    return wb.call("scripts.batch", commands=commands)


@mcp.tool()
def scripts_report(player: str, path: str) -> dict:
    """Writes a Markdown report of one player's AI to a file: script groups with counts, the
    build order, every team with units, priority and scripts, the army in numbers, scripts and
    teams per difficulty, dead logic and broken references. For reading, reviewing with others
    or keeping next to a scripts_dump as documentation of a state."""
    groups = wb.call("scripts.list", player=player, detail="groups")["players"][0]
    order = wb.call("scripts.build_order", player=player)
    teams = wb.call("teams.list", owner=player, limit=1000)["teams"]
    summary = wb.call("teams.summary", owner=player, limit=100)["owners"]
    difficulty = wb.call("scripts.by_difficulty", player=player, limit=15)
    analysis = wb.call("scripts.analyze", player=player, limit=15)
    missing = wb.call("scripts.missing", player=player, limit=60)

    def cell(text: Any) -> str:
        return str(text).replace("|", "\\|").replace("\r", " ").replace("\n", " ")

    out = [f"# AI report: {player or '(neutral)'}", ""]
    total = groups.get("script_count", 0) + sum(g["script_count"] for g in groups["groups"])
    out += [f"{total} scripts in {len(groups['groups'])} groups, {len(teams)} teams.", ""]

    out += ["## Script groups", "", "| Group | Scripts | Subroutines | Inactive |", "|---|---|---|---|"]
    if groups.get("script_count"):
        out.append(f"| (not in a group) | {groups['script_count']} | {groups.get('subroutines', 0)} | {groups.get('inactive', 0)} |")
    for g in groups["groups"]:
        flags = "" if g.get("active", True) else " (group off)"
        out.append(f"| {cell(g['name'])}{flags} | {g['script_count']} | {g.get('subroutines', 0)} | {g.get('inactive', 0)} |")

    out += ["", "## Build order", "", "| # | Script | Builds | When | Notes |", "|---|---|---|---|---|"]
    for step in order["steps"]:
        notes = [n for n in ("inactive" if step.get("active") is False else "", "repeats" if step.get("repeats") else "",
                             f"difficulty {step['difficulty']}" if step.get("difficulty") else "") if n]
        builds = ", ".join(b.get("what", b["action"]) for b in step["builds"])
        out.append(f"| {step['step']} | {cell(step['script'])} | {cell(builds)} | {cell(' OR '.join(step['if']))} | {', '.join(notes)} |")

    out += ["", "## Teams", "", "| Team | Units | Priority | Max | Built when | Scripts |", "|---|---|---|---|---|---|"]
    for t in teams:
        scripts = dict(t["scripts"])
        condition = scripts.pop("ProductionCondition", "")
        hooks = ", ".join(f"{k.replace('GenericScriptHook', 'hook ').replace('Script', '')}: {v}" for k, v in scripts.items())
        out.append(f"| {cell(t['name'])} | {cell(', '.join(t['units']))} | {t.get('priority', '')} | {t.get('max_instances', '')} "
                   f"| {cell(condition)} | {cell(hooks)} |")

    for s in summary:
        out += ["", "## Army in numbers", ""]
        if "team_size" in s:
            out.append(f"Team size {s['team_size']['min']} to {s['team_size']['max']} units (average {s['team_size']['average']:.1f}); "
                       f"full team cost {s['team_cost']['min']} to {s['team_cost']['max']} (average {s['team_cost']['average']:.0f}).")
            out.append("")
        out += ["| Unit | Teams | Min total | Max total | Cost |", "|---|---|---|---|---|"]
        for u in s["units"]:
            out.append(f"| {cell(u['type'])} | {u['teams']} | {u['min_total']} | {u['max_total']} | {u.get('cost', 'unknown type')} |")
        out += ["", "Production priorities: " + ", ".join(f"{p['priority']} ({p['teams']} teams)" for p in s["priorities"]) + "."]

    out += ["", "## Difficulty", ""]
    for s in difficulty["script_switches"]:
        names = f": {', '.join(s['names'])}" if s.get("names") else ""
        out.append(f"- Scripts running on {s['runs_on']}: {s['scripts']}{cell(names)}")
    for tag in difficulty["teams_named"]:
        out.append(f"- Teams named {tag['tag']}: {tag['teams']}")
    out.append(f"- Teams without a difficulty word: {difficulty['teams_without_tag']}")

    out += ["", "## Possible dead logic", ""]
    for key, value in analysis.items():
        if value["count"]:
            names = ", ".join(i["name"] for i in value["items"])
            more = " ..." if value["count"] > len(value["items"]) else ""
            out.append(f"- {key.replace('_', ' ')}: {value['count']} ({cell(names)}{more})")

    out += ["", "## Broken references", ""]
    if not missing["missing"]:
        out.append("None.")
    else:
        out += ["| Uses | Kind | Missing | Used by | Closest existing |", "|---|---|---|---|---|"]
        for m in missing["missing"]:
            out.append(f"| {m['uses']} | {m['kind']} | {cell(m['missing'])} | {cell('; '.join(m['used_by']))} "
                       f"| {cell(', '.join(m.get('suggestions', [])))} |")

    full = os.path.abspath(path)
    with open(full, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    return {"path": full, "lines": len(out), "scripts": total, "teams": len(teams), "build_steps": len(order["steps"]),
            "broken_references": missing["total_uses"]}


@mcp.tool()
def scripts_compare(player: str, with_player: str | None = None, with_file: str | None = None,
                    replace: dict[str, str] | None = None, include_teams: bool = True, limit: int = 30) -> dict:
    """Compares the scripts and teams of `player` (side a) with another player of the map
    (with_player) or with a scripts_dump file (with_file; with_player then names the player inside
    the file, default the same name), e.g. to see what your edits changed before exporting.
    Returns per side what only it has, and for scripts both have, the conditions/actions/flags
    that differ, in the editor's wording (lines only in a / only in b). Teams: which properties
    differ as [a, b]. Factions name things with their own prefix; `replace` rewrites names of side
    b before comparing so they line up, e.g. comparing SkirmishAmerica with the SuperWeapon
    general: {"HTC ": "SCM ", "SupW_": ""}. Lists are capped at limit; the counts are complete."""
    data = None
    if with_file:
        with open(os.path.abspath(with_file), encoding="utf-8") as f:
            data = json.load(f)
    return wb.call("scripts.compare", player=player, with_player=with_player, with_data=data, replace=replace,
                   include_teams=include_teams, limit=limit)


@mcp.tool()
def scripts_set_values(param_type: str, value: Any, op: Literal["set", "add", "multiply"] = "set",
                       player: str | None = None, group: str | None = None, script_filter: str | None = None,
                       type: str | None = None, current: str | None = None, text: str | None = None,
                       where: Literal["any", "conditions", "actions"] = "any", min: float | None = None,
                       max: float | None = None, all_scripts: bool = False, dry_run: bool = False) -> dict:
    """Changes one kind of parameter in many scripts at once. Picks parameters of param_type
    (INT, REAL, OBJECT_TYPE, TEAM, ...) in the conditions/actions that match all given filters:
    player, group, script_filter (substring of the script name), type (condition/action type, e.g.
    PLAYER_HAS_CREDITS), current (the parameter's exact current value), text (substring of the
    editor wording), where. op: "set" gives them `value`; "add" / "multiply" change numbers by
    `value` (INT is rounded; min / max clamp the result). Examples: raise every credit threshold
    of one faction's build scripts by 20%: param_type="INT", type="PLAYER_HAS_CREDITS",
    group="SCM Base Main", op="multiply", value=1.2. Swap a unit everywhere: param_type=
    "OBJECT_TYPE", current="OldName", value="NewName" (scripts_rename also covers teams).
    At least one filter is required unless all_scripts. Use dry_run first: it returns the counts
    and up to 15 before/after samples without changing anything. One undo step."""
    return wb.call("scripts.set_values", param_type=param_type, value=value, op=op, player=player, group=group,
                   script_filter=script_filter, type=type, current=current, text=text, min=min, max=max,
                   all_scripts=all_scripts, dry_run=dry_run, **{"in": where})


@mcp.tool()
def scripts_missing(player: str | None = None, limit: int = 50, suggest: bool = True) -> dict:
    """Everything scripts and teams refer to that does not exist, grouped by the missing name and
    sorted by how often it is used: kind (OBJECT_TYPE, SCRIPT, TEAM, WAYPOINT, UPGRADE, ...), the
    players and a few places using it, and `suggestions`, the closest existing names. One typo or
    one removed unit usually causes dozens of scripts_verify / teams_verify warnings; this shows
    the causes. Fix a group with scripts_rename(kind=rename_kind, from_name=missing, to_name=...)
    which updates scripts and teams. exists_for_another_player marks scripts that were not copied
    along with the teams or scripts using them."""
    return wb.call("scripts.missing", player=player, limit=limit, suggest=suggest)


@mcp.tool()
def scripts_copy(name: str, new_name: str, player: str | None = None, group: str | None = None,
                 script_index: int | None = None, to_player: str | None = None, to_group: str | None = None,
                 index: int | None = None) -> dict:
    """Duplicates a script under a new name. By default the copy goes to the end of the same list;
    to_player / to_group ("" = top level) / index place it elsewhere."""
    return wb.call("scripts.copy", name=name, new_name=new_name, player=player, group=group,
                   script_index=script_index, to_player=to_player, to_group=to_group, index=index)


@mcp.tool()
def scripts_clone_player(from_player: str, to_player: str, replace: dict[str, str] | None = None,
                         existing: Literal["fail", "replace", "append"] = "fail", include_teams: bool = True,
                         dry_run: bool = False) -> dict:
    """Copies every script, group and team of one player to another player, the way a new
    faction's AI is derived from an existing one. `replace` is an ordered {"old text": "new text"}
    list of substring replacements applied to script, group and team names, to every name inside
    script parameters (teams, scripts, object types, flags, upgrades ...) and to the teams' string
    properties, e.g. {"SCM ": "XYZ ", "AmericaTankCrusader": "XyzTankLion"}. Team names are unique
    per map, so the replacements must change every team name (it fails otherwise and names some).
    Player parameters naming the source player are switched to the target. existing: what to do
    if the target already has scripts (replace also removes its teams). Returns counts, how often
    each replacement hit, and how many warnings scripts_verify / teams_verify now give for the
    target: those are the references still to adapt. dry_run reports without changing anything.
    One undo step."""
    return wb.call("scripts.clone_player", **{"from": from_player}, to=to_player, replace=replace, existing=existing,
                   include_teams=include_teams, dry_run=dry_run)


@mcp.tool()
def scripts_rename(kind: str, from_name: str, to_name: str, player: str | None = None) -> dict:
    """Renames something together with every reference to it. kind:
    - "script" or "group": the script/group and all script parameters and team properties
      (production condition, on-create, hooks ...) naming it. Without player it is done for every
      player that has one, each within its own scripts and teams.
    - "team": the team and all team parameters in scripts. Fails if map objects belong to it.
    - a parameter type holding a name, e.g. FLAG, COUNTER, OBJECT_TYPE, UPGRADE, WAYPOINT,
      TRIGGER_AREA, SCIENCE: every parameter of that type with that exact value; for OBJECT_TYPE
      and WAYPOINT also the teams' unit types and waypoints.
    - "script_reference" / "team_reference": only the references, for names that do not exist
      (see scripts_missing); nothing has to be named from_name.
    Returns how many references changed. One undo step."""
    return wb.call("scripts.rename", kind=kind, **{"from": from_name}, to=to_name, player=player)


@mcp.tool()
def scripts_delete(name: str, player: str | None = None, group: str | None = None,
                   script_index: int | None = None) -> dict:
    """Deletes a script."""
    return wb.call("scripts.delete", name=name, player=player, group=group, script_index=script_index)


@mcp.tool()
def scripts_move(name: str, player: str | None = None, group: str | None = None,
                 script_index: int | None = None, to_player: str | None = None, to_group: str | None = None,
                 index: int | None = None) -> dict:
    """Moves a script. With only index it changes position in its list (scripts run top to bottom).
    to_group moves it into that group, to_group "" out of its group to the player's top level,
    to_player to another player. index defaults to the end."""
    return wb.call("scripts.move", name=name, player=player, group=group, script_index=script_index,
                   to_player=to_player, to_group=to_group, index=index)


@mcp.tool()
def scripts_add_group(player: str, name: str, active: bool = True, subroutine: bool = False,
                      index: int | None = None) -> dict:
    """Adds a script group (folder) to a player. An inactive group disables all its scripts."""
    return wb.call("scripts.add_group", player=player, name=name, active=active, subroutine=subroutine,
                   index=index)


@mcp.tool()
def scripts_update_group(name: str, player: str | None = None, new_name: str | None = None,
                         active: bool | None = None, subroutine: bool | None = None,
                         index: int | None = None, to_player: str | None = None) -> dict:
    """Renames a script group, changes its flags, its position (index) or its player (to_player)."""
    return wb.call("scripts.update_group", name=name, player=player, new_name=new_name, active=active,
                   subroutine=subroutine, index=index, to_player=to_player)


@mcp.tool()
def scripts_delete_group(name: str, player: str | None = None, delete_scripts: bool = False) -> dict:
    """Deletes a script group. A group that still holds scripts needs delete_scripts=true."""
    return wb.call("scripts.delete_group", name=name, player=player, delete_scripts=delete_scripts)


@mcp.tool()
def scripts_list_actions(filter: str | None = None, limit: int = 50, offset: int = 0) -> dict:
    """Script action types: `type` (the name to use), `ui_name` (the editor's menu path), `params`
    (parameter types in order), `sentence` (how the editor words it) and `help`. There are several
    hundred, so search with filter (matches type, ui_name and help), e.g. "TEAM_ATTACK" or "timer"."""
    return wb.call("scripts.list_actions", filter=filter, limit=limit, offset=offset)


@mcp.tool()
def scripts_list_conditions(filter: str | None = None, limit: int = 50, offset: int = 0) -> dict:
    """Script condition types, like scripts_list_actions."""
    return wb.call("scripts.list_conditions", filter=filter, limit=limit, offset=offset)


@mcp.tool()
def scripts_list_values(param_type: str, filter: str | None = None, limit: int = 100, offset: int = 0) -> dict:
    """The values a parameter type accepts, as the script editor's dropdown offers them: e.g.
    UPGRADE, SCIENCE, SPECIAL_POWER, COMMAND_BUTTON, COMMANDBUTTON_ALL_ABILITIES, OBJECT_TYPE,
    OBJECT_TYPE_LIST, KIND_OF_PARAM, OBJECT_STATUS, FACTION_NAME, SOUND, MUSIC, DIALOG, and the
    names this map defines (TEAM, SIDE, SCRIPT, SCRIPT_SUBROUTINE, FLAG, COUNTER, WAYPOINT,
    TRIGGER_AREA, ATTACK_PRIORITY_SET). filter is a substring; page with limit/offset. Types
    without a list (INT, REAL, COORD3D, ...) return `free_form`, how to write the value. Look
    names up here instead of guessing them."""
    return wb.call("scripts.list_values", param_type=param_type, filter=filter, limit=limit, offset=offset)


@mcp.tool()
def scripts_verify(player: str | None = None, name: str | None = None, summary: bool = False) -> dict:
    """Runs the script editor's checks on all scripts (or one player's, or one script): parameters
    that name a script, team, player, waypoint, area, unit, object type, sound etc. that does not
    exist. Also lists script names a player uses more than once (the same name under different
    players is normal, e.g. in AI script sets), and `team_warnings` from teams_verify (not when
    checking a single script). Takes a few seconds on maps with thousands of scripts.
    summary=true returns each distinct warning once with a count and the players it occurs for
    (most frequent first) instead of one entry per place; use it on big sets, then scripts_missing
    for the causes."""
    return wb.call("scripts.verify", player=player, name=name, summary=summary)


@mcp.tool()
def scripts_export(path: str, player: str | None = None, group: str | None = None, name: str | None = None,
                   include_players: bool | None = None, include_waypoints: bool = False, include_areas: bool = False,
                   include_units: bool = False, overwrite: bool = False, backup: bool = True) -> dict:
    """Writes a script file (.scb) like the script editor's Export. Without a selection: all
    scripts of all players, with the players and all teams. This is how an AI script set such as
    SkirmishScripts.scb is produced. With player, group or name: only that player's scripts,
    that group or that script, as one list that scripts_import puts into a player of choice, with
    the teams those scripts refer to. Map data goes along on request, for moving map scripts to
    another map: include_waypoints (all waypoints with their links; for a selection those its
    scripts name, also by path label), include_areas (trigger areas, same rule; never water) and
    include_units (the named units scripts refer to and the units of the teams they name). AI
    script sets need none of these. An existing file is only replaced with overwrite=true, and
    then a copy named <path>.bak-<date>-<time> is kept (backup=false skips it). Ask before
    overwriting the mod's live SkirmishScripts.scb."""
    return wb.call("scripts.export", path=_abspath(path), player=player, group=group, name=name,
                   include_players=include_players, include_waypoints=include_waypoints,
                   include_areas=include_areas, include_units=include_units, overwrite=overwrite, backup=backup)


@mcp.tool()
def scripts_import(path: str, player: str | None = None, map_data: bool = False) -> dict:
    """Reads a script file (.scb) like the script editor's Import. Scripts go to the player of the
    same name and are put in front of that player's existing scripts; players and teams the map
    does not have are added (when the file carries them), existing ones are left alone. A file
    with a single script list (a selection export) goes to `player`. map_data=true also adds the
    waypoints (with links), trigger areas and units the file carries; ones the map already has
    (same waypoint or area name, same object at the same spot) are kept as they are and reported
    in `notes`. To edit an AI script set, import it into an empty map, edit, then scripts_export.
    One undo step; with map data, object ids are invalidated."""
    return wb.call("scripts.import", path=_abspath(path), player=player, map_data=map_data)


@mcp.tool()
def teams_list(owner: str | None = None, filter: str | None = None, unit: str | None = None,
               script: str | None = None, include_default_teams: bool = False, limit: int = 100,
               offset: int = 0) -> dict:
    """Teams in short form, for reviewing many at once: `units` such as "3-6 AmericaTankCrusader"
    (min-max per unit type), production `priority`, `max_instances` and `scripts`, every script
    the team names by role (ProductionCondition, OnCreateScript, OnDestroyedScript,
    GenericScriptHook0.., ...). owner = player name; filter = substring of the team name; unit =
    substring of a unit type in the team; script = exact name of a script the team refers to.
    Players' default teams are left out unless include_default_teams. teams_get has every property."""
    return wb.call("teams.list", owner=owner, filter=filter, unit=unit, script=script,
                   include_default_teams=include_default_teams, limit=limit, offset=offset)


@mcp.tool()
def teams_list_properties() -> dict:
    """The team properties the game reads: key, type (bool / int / real / string), what a string
    names (script, object_type, waypoint, player) and what it does. Numbered keys are shown as
    teamUnitType<N> with their range."""
    return wb.call("teams.list_properties")


@mcp.tool()
def teams_get(name: str) -> dict:
    """One team (the team template dialog): `units` as [{type, min, max}] and every property:
    owner, production condition and priority, scripts run on create/idle/destroyed, generic script
    hooks, behavior, reinforcement settings."""
    return wb.call("teams.get", name=name)


@mcp.tool()
def teams_set(name: str, owner: str | None = None, units: list[dict[str, Any]] | None = None,
              properties: dict[str, Any] | None = None, allow_unknown_properties: bool = False) -> dict:
    """Creates a team (owner = player name, required then) or changes an existing one.
    units: [{"type": "AmericaTankCrusader", "min": 3, "max": 6}, ...] replaces all seven unit
    slots (min defaults to max, max to 1). properties: team keys from teams_list_properties, e.g.
    {"teamProductionCondition": "...", "teamProductionPriority": 1000, "teamMaxInstances": 1,
    "teamOnCreateScript": "...", "teamGenericScriptHook0": "..."}; null removes a key. Values are
    stored with the type the game reads (a wrong type would be ignored by the game), so a
    mismatch or an unknown key is an error. Returns `warnings`: unit types, scripts or waypoints
    the team names that do not exist."""
    return wb.call("teams.set", name=name, owner=owner, units=units, properties=properties,
                   allow_unknown_properties=allow_unknown_properties)


@mcp.tool()
def teams_copy(name: str, new_name: str, owner: str | None = None) -> dict:
    """Duplicates a team under a new name, optionally for another owner."""
    return wb.call("teams.copy", name=name, new_name=new_name, owner=owner)


@mcp.tool()
def teams_set_many(owner: str | None = None, filter: str | None = None, unit: str | None = None,
                   script: str | None = None, properties: dict[str, Any] | None = None,
                   multiply: dict[str, float] | None = None, add: dict[str, float] | None = None,
                   all_teams: bool = False, dry_run: bool = False) -> dict:
    """Changes every team matching the teams_list filters (owner, filter = name substring, unit,
    script). properties sets values like teams_set (typed and checked). multiply / add change
    numeric properties: {"teamProductionPriority": 0.8} or, with a trailing * for all numbered
    keys of a prefix, {"teamUnitMaxCount*": 1.5} (whole numbers are rounded). Examples: lower the
    priority of all "E Attack Team" teams of one faction; make every team of a faction 50% larger.
    A filter is required unless all_teams. dry_run returns the counts and up to 15 samples of
    {property: [old, new]} without changing anything. One undo step."""
    return wb.call("teams.set_many", owner=owner, filter=filter, unit=unit, script=script, properties=properties,
                   multiply=multiply, add=add, all_teams=all_teams, dry_run=dry_run)


@mcp.tool()
def teams_summary(owner: str | None = None, filter: str | None = None, unit: str | None = None,
                  script: str | None = None, limit: int = 40) -> dict:
    """A faction's army in numbers, per owner (all owners without one): how many teams use each
    unit type, with the summed min/max counts and the unit's build cost; team sizes and full-team
    build costs (min / max / average); how many teams sit at each production priority; which
    production condition scripts gate how many teams. The filters are those of teams_list, e.g.
    filter="Attack Team" for the attack forces only. Compare two factions by reading both."""
    return wb.call("teams.summary", owner=owner, filter=filter, unit=unit, script=script, limit=limit)


@mcp.tool()
def teams_verify(owner: str | None = None, name: str | None = None) -> dict:
    """Checks teams (all, one owner's, or one team): properties stored with a type the game does
    not read, unknown properties, unit types that do not exist, min above max, and scripts or
    waypoints they name that do not exist. Scripts are looked up under the team's own player."""
    return wb.call("teams.verify", owner=owner, name=name)


@mcp.tool()
def teams_delete(name: str, force: bool = False) -> dict:
    """Deletes a team. Default teams of players cannot be deleted; a team that still owns map
    objects needs force=true."""
    return wb.call("teams.delete", name=name, force=force)


@mcp.tool()
def worldbuilder_command(cmd: str, args: dict[str, Any] | None = None) -> Any:
    """Runs a raw bridge command (see list_commands) for anything not covered by the other tools."""
    return wb.call(cmd, **(args or {}))


if __name__ == "__main__":
    mcp.run()
