---
name: adapt-map-for-navy
description: Adapt a Zero Hour map for ships with the WorldBuilder MCP. Grows the map with open sea on all sides, lifts the land and carves a gradual seabed at least 50 deep (raising HeightMapScale in map.ini when needed), paints a noisy SandMediumType9blue gradient from the shore outward, adds the naval AI markers (Shipyard<N>_<n> waypoints, NavalPerimeter<N> areas, Naval<N> / NavalFlank<N> paths), writes EnableShips and the underwater lighting to map.ini, and shifts the waves in a matching .wak file. Use when asked to "prepare a map for navy / ships / water", "make this map naval" or "adapt a map for navy".
argument-hint: "[map path, default: the map open in WorldBuilder]"
---

# Adapt a map for navy

Works on the map open in WorldBuilder (`mcp__worldbuilder__*`; see CLAUDE.md "WorldBuilder MCP" for how to start it). If the tools are missing or fail with "Cannot reach WorldBuilder", stop and tell the user. If the MCP server itself failed to connect, check that `uv` is on PATH.

Helper: `navy_tool.py` next to this file (standard library only; run it with `python -I`). Keep its outputs in the session scratchpad (below: `$WORK`). The numbers below are the defaults; follow the user's if they give others.

## 0. Look first, back up

1. `map_info`: path, `heightmap` (extents, `border`, `height_map_scale`), world extents, `ships_enabled`. The map must have been saved to a file.
2. `polygons_list(include_points=true)`: the current water area and its `water_height` (`$WATER`). No water at all: ask the user where the shoreline should be.
3. `waypoints_list`: the `Player_<N>_Start` waypoints, and the existing path labels.
4. Look at the map folder: `<map>.map`, `map.ini`, `<map>.wak`.
5. **Back up** what will change: copy `<map>.map` to `<map>.map.bak`, and `map.ini` / `<map>.wak` to `.bak` if present. Don't overwrite an existing `.bak` (`cp -n`). To start over: copy the backups back (delete `map.ini` if there was none), then `map_open(path, discard_changes=true)`.

## 1. Grow the map

`map_resize(add_left=60, add_right=60, add_bottom=60, add_top=60, fill_height=0)`, at least 50 cells per side. Note `world_offset` (`$DX`, `$DY`): every object, waypoint and area moved by it. `scripts_find(param_type="COORD3D")` lists scripts with literal coordinates; those are **not** moved, so tell the user.

## 2. Heights

1. Dump all heights of the grown map **before saving it**: `terrain_get_heights(y0=…, h=100)` from row 0 up (at most 65536 values per call, so 100 rows of a ≤655-vertex-wide map). Each result is too big for the context and is saved to a tool-results file; collect those paths in row order. (`terrain_export_heightmap` failed in testing, so don't rely on it.)
2. `python -I navy_tool.py heights --out $WORK --border <border> --water $WATER <dumps…>`
   - Lifts all land by the same amount, so the land keeps its shape relative to itself. The new water surface is `$WATER + lift`.
   - Sea = vertices below the old water level that connect to the map edge. Inland ponds are lifted like land and stay ponds.
   - Seabed: `depth * sin(min(d/ramp, 1) * 90°)` below the surface, where `d` is the distance in cells from the nearest land vertex. Defaults: depth 60, ramp 30 cells, lowest seabed 2 world units. The shore stays gentle and the sea is ≥ 50 deep.
   - It prints `height_map_scale` (smallest whole scale that fits the new range), `import_range`, `new_water_height` and suggested `lighting`. Heights snap to multiples of the scale: up to ±0.6 world units of rounding on land.
3. `map_save` (the scale is still the old one here).
4. Write `map.ini` next to the map. If one exists, keep its other lines and set these fields inside its `MapData` block:
   ```
   MapData
     HeightMapScale = <height_map_scale>.0
     EnableShips = Yes
     TerrainHeightAmbientLightColor1 = R:196 G:225 B:255
     TerrainHeightAmbientLightColor2 = R:128 G:128 B:196
     TerrainHeightAmbientLightStart = <water - 2>
     TerrainHeightAmbientLightHeight1 = <water - 17>
     TerrainHeightAmbientLightHeight2 = <water - 37>
   End
   ```
   The light heights are world z of terrain vertices (`BaseHeightMap.cpp`): the tint starts below Start, reaches Color1 at Height1 and Color2 at Height2. Keep them relative to the new water surface. The user's reference was 40 / 25 / 5 for a water level near 42.
5. `map_open(<same path>)`: WorldBuilder reads `HeightMapScale` only when it opens a map. `map_info` must now show the new `height_map_scale`; the heights look scaled up until the import.
6. `terrain_import_heightmap(path=$WORK/heights16.png, fit="exact", range=<import_range>)`. Spot-check a few vertices with `terrain_get_heights` (land = old + lift, deep sea = the floor).

## 3. Water surface

Delete the old water polygon (`polygons_delete`) and add one over the whole map, border included: `polygons_add(kind="water", water_height=<new water>, points=[[min_x,min_y],[max_x,min_y],[max_x,max_y],[min_x,max_y]])` with the world extents from `map_info`.

## 4. Water textures

1. `python -I navy_tool.py mask --out $WORK` writes `watermask.png`, one pixel per cell:
   - 0: land and the sea cells within 2 cells of land (`--skip 2`). These keep their texture, so the shoreline looks as before.
   - 64 / 128 / 192 / 255: bands from shore outward, starting at 2, 14, 28 and 45 cells (`--bands 14,28,45`).
   - The band borders wander with smooth noise (`--noise 28:6,10:2.5` = feature size:amplitude in cells, `--seed`), so the gradient isn't even.
2. Paint the bands in this order, each over the last, all with `fit="exact"`. Use 224 for the last threshold: 255 matched no pixels in testing.

   | Threshold | Texture |
   |---|---|
   | 64 | `SandMediumType9blue4` (white, shallow) |
   | 128 | `SandMediumType9blue3` |
   | 192 | `SandMediumType9blue2` |
   | 224 | `SandMediumType9blue` (dark, deep) |

   Use `terrain_import_mask(path=…, target="texture", texture=…, threshold=…)` for each. Check the names with `terrain_list_textures(filter="SandMediumType9")`.
3. `terrain_blend_all(textures=[the four blues])`. This also blends blue4 into the shore texture, softening the outermost untouched tiles. Without blue4 in the list, its outer edge shows as a stepped line under the water.
4. `view_screenshot` near a shore to check.

## 5. Naval AI markers

Names the AI looks up (see `ai_skirmish_check`); `<N>` is the player number of `Player_<N>_Start`:

- **Shipyards** `Shipyard<N>_1`, `_2`, … numbered from 1 without gaps; at least 2, up to 4. They sit off a soft shoreline, 3–4 cells into the water (1–2 cells was too close to the shore).
  `python -I navy_tool.py yards --out $WORK --water $WATER --starts N,x,y … --dumps <same dumps>` suggests 3 per player (`--count`): sea vertices 3–4 cells from land (`--min-d` / `--max-d`), no land higher than 15 above the water within 6 cells (no cliff), nearest to that player's start and closer to it than to any other start, within 1300 units, at least 300 apart. Use start positions **after** the resize. Add them with `waypoints_add(name=…, x, y)`.
- **NavalPerimeter<N>**: an area over the water in front of the base, e.g. an octagon of radius ~800 around the centroid of that player's shipyards. It may include some shore. Keep neighbours from overlapping. Use `polygons_add(name=…, points=…)`.
- **Naval<N>** and **NavalFlank<N>**: paths of ~4 waypoints leading **into** player N's waters, linked from the open sea toward the shipyards, like the land `Center<N>` paths:
  - `Naval<N>` comes along the main sea lane from the enemy's side.
  - `NavalFlank<N>` swings around the other side of the land mass.

  Keep every point in deep water: `python -I navy_tool.py check --out $WORK x,y …` prints the distance from shore in cells; use 18 or more, and -1 means land. Add them with `waypoints_add(name="Naval1_1", x, y, path_labels=["Naval1"])`, then each next point with `link_from` the previous name. Waypoints at the same spot are fine but hard to click in WorldBuilder.
- Land paths: `ai_skirmish_check` also reports land approach paths. Labels like `CenterPath1` / `BackdoorPath1` are ignored by the AI (it wants `Center1` / `Backdoor1`). Ask the user before renaming. To rename, `objects_set_properties(ids, properties={"waypointPathLabel1": "Center1"})`; take the ids from a fresh `waypoints_list`, because object ids change after every `map_open`.

## 6. .wak file (water waves)

If `<map>.wak` exists next to the map, its waves are still at the old coordinates. Shift them by the resize offset:

`python -I navy_tool.py wak "<map>.wak" --dx $DX --dy $DY`

The file holds, per wave, a start and end point (2 × float32 world x/y) and an int32 wave type, with an int32 count at the end (`W3DWaterTracks.cpp` `saveTracks` / `loadTracks`). The tool checks the size against the count before writing. WorldBuilder never writes `.wak`; only the game's wave editing does. So the shift can happen at any time, but only once. A shift and its reverse differ only by float32 rounding.

## 7. Finish

1. `ai_skirmish_check`: `naval_ready: true`, `ships_enabled: true`, and `naval_problems` empty. Report remaining land `problems` to the user.
2. `map_save`, then a final `view_screenshot`.
3. Report:
   - the size change and world offset;
   - the lift, new water height, scale and depth;
   - the bands;
   - the markers per player;
   - the `map.ini` fields;
   - the `.wak` shift;
   - where the backups are.

   Say that nothing was tested in the game yet.
