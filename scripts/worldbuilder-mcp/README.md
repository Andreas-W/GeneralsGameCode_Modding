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
