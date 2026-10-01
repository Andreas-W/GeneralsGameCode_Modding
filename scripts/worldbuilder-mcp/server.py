"""MCP server that lets an AI assistant edit the map open in Zero Hour WorldBuilder.

Run WorldBuilderZH.exe with ``-mcp`` first, then point an MCP client at this script (stdio).
"""

from __future__ import annotations

import json
import os
from typing import Any, Literal

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from wb_bridge import WorldBuilderBridge, WorldBuilderError

INSTRUCTIONS = """\
Edits the map that is open in Command & Conquer Generals Zero Hour WorldBuilder, live.

Coordinates:
- World units: 10 per terrain cell. (0,0) is the lower left corner of the playable area; the
  border extends to negative coordinates. Call map_info for the exact extents.
- Heights are raw values 0..255 on the heightmap vertices; world height = raw * 0.625.
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
    """Reads raw vertex heights (0..255) as rows (row 0 = y0). Indices are heightmap vertex indices
    including the border; vertex (border, border) is world (0,0). Use step>1 to downsample large areas.
    At most 65536 values per call."""
    return wb.call("terrain.get_heights", x0=x0, y0=y0, w=w, h=h, step=step)


@mcp.tool()
def terrain_set_heights(x0: int, y0: int, heights: list[list[int | None]]) -> dict:
    """Writes raw vertex heights (0..255). heights[r][c] goes to vertex (x0+c, y0+r); null leaves a
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
    raw height 0..255, top row = north edge. normalize stretches the used height range to 0..255 for
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
    mapped onto range=[low, high] raw heights (default [0, 255] for set, [-64, 64] for add). mode=add
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


@mcp.tool()
def worldbuilder_command(cmd: str, args: dict[str, Any] | None = None) -> Any:
    """Runs a raw bridge command (see list_commands) for anything not covered by the other tools."""
    return wb.call(cmd, **(args or {}))


if __name__ == "__main__":
    mcp.run()
