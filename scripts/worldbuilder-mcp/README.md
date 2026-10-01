# WorldBuilder MCP server

Lets an MCP client (Claude Code, Claude Desktop, ...) edit the map that is open in Zero Hour
WorldBuilder: terrain heights and textures, objects, players, waypoints, water and trigger areas,
roads, map settings, save/load and 3D screenshots.

```
MCP client  <-- stdio -->  server.py  <-- TCP 127.0.0.1:47800, JSON lines -->  WorldBuilderZH.exe -mcp
```

The C++ side lives in `GeneralsMD/Code/Tools/WorldBuilder/src/mcp/`. WorldBuilder runs every command
on its UI thread and records each edit as a normal undo step.

## Setup

1. Build `z_worldbuilder` and start it with the bridge enabled:

   ```
   WorldBuilderZH.exe -mcp              # port 47800
   WorldBuilderZH.exe -mcpport:47801    # custom port
   ```

   Setting the `WB_MCP_PORT` environment variable also enables it. The bridge only listens on
   localhost.

   Add `-ignoreAsserts` to log debug-build assertions instead of showing blocking dialogs. A
   dialog opened during a command stalls the bridge until the command times out.

2. Install [uv](https://docs.astral.sh/uv/) (`winget install astral-sh.uv`). The dependencies are
   declared in `pyproject.toml`. uv creates `scripts/worldbuilder-mcp/.venv` and keeps it in sync
   on first run, and `uv.lock` pins the exact versions.

   ```
   uv sync --directory scripts/worldbuilder-mcp
   ```

3. Register the server. The repo root `.mcp.json` already does this for Claude Code
   (`uv run --directory scripts/worldbuilder-mcp server.py`). Other clients can use the same
   command over stdio. Set `WB_MCP_PORT` in the server's environment if you use a custom port.

## Checking the bridge without MCP

```
uv run --directory scripts/worldbuilder-mcp smoke_test.py
```

The bridge serves one client at a time, so run this before an MCP client has connected to
WorldBuilder (or with the MCP server stopped). Otherwise it waits for the connection.

This creates a new map and discards unsaved changes in the open one. It then exercises every
command area, saves the map to a temp folder, reopens it and verifies it.

## Protocol

Each request is one line: `{"id": 1, "cmd": "map.info", "args": {}}`. The reply is
`{"id": 1, "ok": true, "result": {...}}` or `{"id": 1, "ok": false, "error": "..."}`.
`list_commands` lists every command with a short argument summary.

WorldBuilder rejects commands while a modal dialog is open or a mouse drag is in progress.

## Notes

- **World coordinates.** One terrain cell is 10 world units. (0,0) is the lower-left corner of the
  playable area.
- **Heights.** Raw heights are 0..255; world height is raw × 0.625.
- **Undo.** WorldBuilder keeps 15 undo steps. Waypoint links are not undoable (same as in the UI).
- **Screenshots.** They render the 3D view, so the WorldBuilder window must not be minimized.
- **Symmetry.** `map_transform` mirrors or rotates the whole map. `map_symmetrize` copies one half or quarter onto the rest.
  - Both carry heights, textures with correctly oriented blends, passability, objects, waypoints with their links, and areas.
  - Player numbers in names and owners are renumbered for each copy.
  - Cliff texture mapping is reset on copied cells.
  - The mirroring approach follows the Genesis map tools (The CWC Team, Apache-2.0).
- **Usage and bulk replace.** `map_usage` lists what a map uses; `map_replace` swaps names everywhere in one undo step.
  - `map_usage` lists textures, object templates and road types, and flags templates the game does not know.
  - `map_replace` takes `{from: to}` mappings for textures (blends are kept), object templates and road types.
  - It can also read Genesis-style "from to" list files, and has a dry run.
- **Road routing.** `roads_route` finds its own way from A to B, optionally through via points.
  - It goes around cliffs, water, structures and named areas, prefers gentle slopes and refuses steep steps.
  - Unneeded bends are removed, but a shortcut is never allowed to be noticeably steeper than the routed path.
  - By default it levels the ground under the road to a smoothed profile.
  - Road and terrain change are one undo step.
- **Scattering.** `objects_scatter` places decoration such as trees and rocks at random.
  - It takes weighted templates and a count or density, and can group objects into noise-driven groves.
  - It keeps clear of roads, building footprints, start positions, cliffs and water.
  - Optional filters limit it by texture, slope or named area.
- **Start positions.** `map_generate_starts` places `Player_<N>_Start` waypoints for 2 to 8 players, evenly spaced around the map center.
  - The first player sits in the lower-left corner unless an angle is given; `distance` pulls all starts toward the center.
  - Each base area is flattened with a feathered edge, and lifted above water if needed.
  - Existing start waypoints are replaced. It fails if the bases would overlap.
- **Skirmish AI.** `ai_skirmish_setup` creates what the AI looks up by name for every `Player_<N>_Start`. `ai_skirmish_check` reports what is missing.
  - Areas: `InnerPerimeter<N>` and `OuterPerimeter<N>`, plus a `CombatZone`.
  - Paths: approach paths into each base, labelled `Center<N>`, `Flank<N>` and `Backdoor<N>`.
- **Procedural terrain.** A typical sequence is `terrain_generate`, then `terrain_limit_slope`, then `terrain_auto_texture`.
  - `terrain_generate` builds heights from layered, seeded Perlin noise. It can protect spots such as base locations.
  - `terrain_limit_slope` turns spikes into walkable slopes.
  - `terrain_auto_texture` textures cells as ground, cliff or water, scatters noise overlays and blends the edges.
  - `terrain_blend_all` and `terrain_remove_blends` are bulk blending helpers.
  - Layouts and sceneries are JSON presets in `presets/` (`terrain_list_presets`), taken from Genesis. See `NOTICE`.
- **Terrain images.** Heightmap and mask PNGs have north at the top.
  - Heightmaps have one pixel per heightmap vertex, and the pixel value is the raw height. 16-bit images are accepted on import.
  - Masks have one pixel per cell.
  - Relative paths are resolved by the Python server, not by WorldBuilder.
