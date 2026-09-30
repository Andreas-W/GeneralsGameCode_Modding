"""MCP server that lets an AI assistant edit the map open in Zero Hour WorldBuilder.

Run WorldBuilderZH.exe with ``-mcp`` first, then point an MCP client at this script (stdio).
"""

from __future__ import annotations

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
def worldbuilder_command(cmd: str, args: dict[str, Any] | None = None) -> Any:
    """Runs a raw bridge command (see list_commands) for anything not covered by the other tools."""
    return wb.call(cmd, **(args or {}))


if __name__ == "__main__":
    mcp.run()
