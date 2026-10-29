# WorldBuilder MCP server

Lets an MCP client (Claude Code, Claude Desktop, ...) edit the map that is open in Zero Hour
WorldBuilder: terrain heights and textures, objects, players, waypoints, water and trigger areas,
roads, scripts and teams, map settings, save/load and 3D screenshots.

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
  - A `map.ini` next to the map can set `HeightMapScale` for taller terrain. Raw heights then go up to 255 × scale.
  - The map file still stores bytes, so heights are always multiples of the scale. Edits snap to them.
  - Heightmap PNGs hold the stored bytes (raw ÷ scale).
- **Undo.** WorldBuilder keeps 15 undo steps. Waypoint links are not undoable (same as in the UI).
- **Screenshots.** They render the 3D view, so the WorldBuilder window must not be minimized.
- **Symmetry.** `map_transform` mirrors or rotates the whole map. `map_symmetrize` copies one half or quarter onto the rest.
  - Both carry heights, textures with correctly oriented blends, passability, objects, waypoints with their links, and areas.
  - Player numbers in names and owners are renumbered for each copy.
  - Cliff texture mapping is reset on copied cells.
  - The mirroring approach follows the Genesis map tools (The CWC Team, Apache-2.0).
- **Resize.** `map_resize` grows or crops the map, by a new size with an anchor or by cells per side.
  - Terrain, textures, passability, objects, waypoints, roads, areas, water and build lists keep their place on the terrain.
  - New cells continue the old edge, or get a fixed height and texture.
  - Objects and waypoints that fall off the map can be removed. Areas that cover the whole map, like Default Water, are refit.
  - Unlike WorldBuilder's own File > Resize it also moves areas and updates the map boundary.
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
- **Scripts and teams.** The `scripts_*` tools do what the script editor (Edit > Scripts) does, without its window.
  - `scripts_list` and `scripts_get` read; `scripts_add`, `scripts_update`, `scripts_delete` and `scripts_move` write. Groups have their own tools.
  - A script is JSON: flags, `conditions` (a list of OR clauses, each a list of ANDed conditions), `actions` and `false_actions`.
  - `scripts_list_actions` and `scripts_list_conditions` list the types with their parameter types. Parameters are given in order.
  - `scripts_list_values` lists what a parameter accepts (upgrades, sciences, special powers, command buttons, object types, ...); `teams_list_properties` lists the team properties with their types.
  - `scripts_copy`, `teams_copy` and `scripts_clone_player` duplicate; the last one derives a whole faction's scripts and teams from another, with name replacements.
  - `scripts_rename` renames a script, group, team, flag, counter or any named value together with its references in scripts and teams.
  - `scripts_build_order`, `scripts_trace` and `scripts_by_difficulty` read a set from one angle: what is built when, what a team or script sets in motion, what belongs to which difficulty.
  - `objects_info` gives unit and structure stats (cost, KindOf, where it is built); `buildlist_*` edits the players' build lists.
  - `faction_generate` writes a planning description of a faction to `worldbuilder_factions/` next to the WorldBuilder executable, so it stays with the game data it describes (`WB_MCP_FACTION_DIR` or `directory` name another folder, relative to the executable): `<Faction>.generated.json` with the cleaned facts plus tiers, build paths, combat numbers and guessed roles; `<Faction>.overrides.yaml` for hand tuning (slots, roles, phases, notes), written once and then left alone; `<Faction>.doctrine.md` for prose. `faction_get` reads it back in views (overview, tree, units, naval, single types, upgrades, sciences, special powers). Ships and shipyards have roles and slots of their own, and every unit's damage is also given against ship and submarine armor.
  - `ai_framework_generate` compares the AI players of the open map and writes the skeleton they share to `ai_framework.generated.json`: scripts that are identical everywhere, scripts that differ only in some parameters (kept as templates), shared flags, counters and timers with their writers and readers, team roster and hooks, object lists, and the waypoint paths and areas expected of a map. `ai_framework_get` reads it in views.
  - `faction_map` finds the counterparts of one faction's types, upgrades, sciences, special powers and buttons in another; `ai_scaffold` uses it to derive a new player's AI from an existing one, keeping the shared framework scripts and translating the rest. It is a dry run unless told otherwise and lists what has no counterpart.
  - `aidata_get` reads `AIData.ini`: global AI settings and per side the gatherer counts, base defense structure, science skill sets and the `SkirmishBuildList`, the base layout that decides where build scripts place structures. `buildlist_from_aidata` loads a side's layout into a map player's build list for viewing and editing; `buildlist_export` writes a player's build list as a `SkirmishBuildList` block, optionally into an `AIData.ini` (with a backup). `aidata_set_side` changes a side's `SideInfo` (gatherers, base defense, skill sets) line by line, so comments and untouched lines stay; it shows a diff and writes only when given the file to change.
  - `ai_match_run` lets the game play a skirmish between AI players on its own (`generalszh.exe -aiMatch -headless`: no window, only the game logic, as fast as the machine allows; a window on request) and writes a log; It can play with the scripts open in WorldBuilder or another `.scb` file, and with a changed `AIData.ini`, without installing either. `ai_match_report` reads it: per player the build timeline, what was never built, teams started, sciences and upgrades bought, scripts and teams that never ran, and what the script engine reported.
  - Ships: `map_info` shows and `map_set_info` sets `EnableShips` of the map's `map.ini`; `ai_skirmish_check` reports the shipyard waypoints, naval areas and naval paths per player; `faction_check` knows `SKIRMISH_BUILD_SHIPYARD` and wants naval scripts and teams to ask `SKIRMISH_SHIPS_ENABLED`.
  - `faction_check` checks a player's teams and build scripts against that description: types the faction cannot build, structures a team needs but no script builds, build limits, foreign upgrades, sciences and buttons, and what the AI never uses.
  - `faction_describe` gives a faction's tech tree and unit stats from the game data: what builds what, prerequisites, cost, build time, health, armor, weapons with damage per second, movement, upgrades, sciences and special powers. With `path` it writes a JSON file.
  - `scripts_list_templates` and `scripts_apply_template` create ready-made script and team combinations from `presets/script_templates.json`.
  - `scripts_report` writes a Markdown report of one player's AI.
  - `scripts_compare` shows how two players' scripts and teams differ, or how the map differs from a dump file.
  - `scripts_set_values` and `teams_set_many` change one kind of value in many scripts or teams (set, add, multiply), with a dry run.
  - `teams_summary` gives a faction's army in numbers: unit usage with costs, team sizes, priorities.
  - `scripts_missing` groups broken references by the missing name and suggests the closest existing ones.
  - `scripts_batch` runs many edits as one undo step, all or nothing.
  - `scripts_dump` and `scripts_load` write and read everything as readable JSON, for review, diffs and bulk edits.
  - `scripts_analyze` looks for dead logic: unused subroutines, flags and counters used one-sided, teams never built.
  - `sides_set` and `sides_delete` change, rename and remove players.
  - `scripts_find` searches inside scripts and team properties: by parameter value, wording, type or parameter type.
  - `scripts_verify` runs the editor's checks for references to things that do not exist. Edits report the same warnings.
  - `scripts_export` and `scripts_import` write and read `.scb` files with scripts, players and teams, and on request the waypoints, areas and units they need. An existing file is only replaced on request, and a dated backup is kept.
  - AI script sets such as `SkirmishScripts.scb` are edited by importing them into a map, changing them and exporting again.
  - `teams_list` shows teams in short form (units with counts, priority, the scripts they name); `teams_get`, `teams_set` and `teams_delete` edit team templates as property dicts.
  - On large sets `scripts_list` reduces its detail by itself; `detail`, `player` and `group` control it.
  - Like the editor, each edit replaces the whole sides list as one undo step. Edits are refused while the script editor window is open, because its OK would overwrite them.
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

# Game MCP server

`game_server.py` drives the running game the same way: Claude starts a skirmish as the local
player, alone in a sandbox or against AI players, pauses and steps it, looks at every object and
player, gives orders, changes things for a test and takes screenshots.

```
MCP client  <-- stdio -->  game_server.py  <-- TCP 127.0.0.1:47801, JSON lines -->  generalszh.exe -mcp
```

The C++ side lives in `GeneralsMD/Code/GameEngine/Source/Common/Mcp/`. It is compiled only with the
CMake option `RTS_BUILD_OPTION_GAME_MCP` (default ON) and listens only when the game is started with
`-mcp`. Commands run on the game's main thread between two logic frames.

## Starting the game

`game_launch` starts `generalszh.exe` from `GAME_DIR` (default `F:\Operation-Firestorm-2.0`; set
`GAME_EXE` for another executable name) and waits until the map has loaded. By hand:

```
generalszh.exe -mcp -mcpGame "armed neighbors" -mcpPlayers FactionAmerica,FactionChina:hard -mcpStartPaused -win -quickstart -ignoreAsserts
```

| Option | Meaning |
|---|---|
| `-mcp` / `-mcpport:N` | Enable the bridge on port 47801 / N. |
| `-mcpGame <map>` | Start a skirmish on that map (map name or part of its path). |
| `-mcpPlayers <list>` | `Faction[:team]` of the local player (or `Observer`), then AI players as `Faction:difficulty[:team]`. Only the local player: a sandbox in which nobody wins or loses. |
| `-mcpCash <n>` | Starting money. |
| `-mcpStartPaused` | Pause as soon as the game has started. |
| `-seed <n>` | Random seed (start positions, everything random). |
| `-headless` | Only the game logic: no window, no screenshots, as fast as allowed by `game_set_speed`. |

`-mcp` also works with `-aiMatch` (to look into an AI match) or a game started from the menu.

## Tools

- **Time:** `game_status`, `game_pause`, `game_resume`, `game_step` (frames or seconds, then pause; waits until done), `game_set_speed` (30 = normal, 0 = as fast as possible; `render_fps` renders at its own rate while the logic keeps `fps`, 0 = uncapped; without it, `RenderFramesPerSecondLimit` in GameData.ini already renders single player games at its own rate, shown as `render_fps_limit` in `game_status`), `game_save` / `game_load` (save files in the save directory, like the in-game menu; windowed games only; load pauses), `game_quit`.
- **Looking:** `objects_list` (filter by player, template, kindof, status, area), `objects_get` (status, disabled, kindof, veterancy, AI state, weapons, upgrades, bonuses such as weapon bonus conditions and armor/sight/speed scalars, special powers, production, passengers, behavior modules, model conditions), `objects_templates`, `players_list`, `players_get` (also battle plan counts), `game_messages` (what the script engine and the AI reported).
- **Orders** (as the owning player would click them; recorded in the replay; carried out on the next logic frame): `orders_move`, `orders_attack`, `orders_stop`, `orders_guard`, `orders_sell`, `orders_build`, `orders_produce`, `orders_upgrade`, `orders_special_power`, `orders_purchase_science`.
- **Test changes** (direct, not in the replay): `objects_spawn`, `objects_destroy`, `objects_set` (health, position, veterancy, upgrade, status), `players_set` (money, rank, points, sciences, upgrades, instant build, special power delays).
- **Scripts:** `scripts_run`, `scripts_set_active`, `scripts_flags`, `scripts_counters`.
- **View** (window only, not minimized): `view_camera`, `view_screenshot`, `view_display_mode` (resolution or windowed mode, not saved; resets the graphics device).
- **Post-processing** (Direct3D 12 renderer, `game_launch(..., extra_args=["-dx12"])`): `render_postprocess` turns the chain on or off, reloads `Data\INI\PostProcess.ini` and the shaders, or tunes a parameter of a running pass live. The chain is a list of `PostProcessPass` blocks (HLSL pixel shaders from `Data\Shaders\PostProcess\<Shader>.hlsl` or the built-in `PP_Copy`, `PP_BloomExtract`, `PP_Blur`, `PP_ColorGrade`, `PP_EdgeAA`, `PP_SharpenVignette`) and a `PostProcess` block with `Passes = ...`; the format is described in `Core/GameEngine/Include/GameClient/PostProcess.h`. The player option is `PostProcessing = yes/no` in Options.ini (gadget `OptionsMenu.wnd:CheckPostProcessing`).
- `game_command` runs any bridge command; `list_commands` lists them.

## Checking the bridge without MCP

```
python game_smoke_test.py --exe generalszh.exe [--windowed]
```

It starts the game headless in a sandbox and goes through every command area, then closes it.
