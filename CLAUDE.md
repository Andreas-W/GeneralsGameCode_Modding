# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this fork is

A **modding-focused fork** of TheSuperHackers/GeneralsGameCode, the community port of the *Command & Conquer: Generals* / *Zero Hour* engine from VS6/C++98 to VS2022/C++20.

**This fork only cares about Zero Hour (`GeneralsMD/`).** Treat the original Generals (`Generals/`) as out of scope unless explicitly asked. Upstream rules say "Zero Hour changes take precedence; mirror to Generals" — here, skip the mirror step. `Core/` holds the shared engine and must be considered for most changes.

**Game data INIs** (Object/Weapon/OCL/etc. that configure the modules this engine parses) live outside the repo at `F:\Operation-Firestorm-2.0\Data\INI`. Read/search there when a task involves how units, weapons, or modules are configured in-game.

## Build

This fork builds with the modern VS2022 toolchain (not VS6). The relevant preset is in `CMakeUserPresets.json`:

```bash
cmake --preset win32-modding-zh          # configure (Release + RTS_DEBUG_LOGGING + RTS_BUILD_OPTION_DEBUG + stacktrace + crashing)
cmake --build build/win32 --preset w32-modding-zh   # builds targets: z_generals, z_worldbuilder
```

- `win32-modding-zh` writes to `build/win32` and enables in-game logging/asserts/stacktraces for mod debugging.
- Stock presets also exist: `win32` (clean Release), `win32-debug`, `win32-vcpkg` (uses vcpkg toolchain for zlib/ffmpeg), `unix`, `mingw-w64-i686`, plus the `vc6*` family used upstream for **retail replay compatibility** (not needed for modding work).
- `compile_commands.json` is exported (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`) under the build dir.

### Verifying a change compiles

Use the **"BUILD ZH AND WB FOR MODDING"** preset (`w32-modding-zh`). To verify, build only what the change touches:
- Build `z_generals` (`generalszh.exe`) for normal engine/game changes.
- Build `z_worldbuilder` (`worldbuilder.exe`) only for WorldBuilder-specific changes.

```bash
cmake --build build/win32 --preset w32-modding-zh --target z_generals
```

Run cmake/ninja from a **VS dev shell** (`vcvarsall.bat amd64_x86`), not a plain PowerShell/Bash session, or the build fails to launch the compiler. If a configured `build/win32` errors with `CreateProcess failed: The system cannot find the file specified`, reconfigure (`cmake --preset win32-modding-zh`) from that dev shell to regenerate compiler paths.

### Targets / output names

Zero Hour targets are prefixed `z_` (Generals uses `g_`):
- `z_generals` → `generalszh.exe` (the game)
- `z_worldbuilder` → `worldbuilder.exe` (map editor)
- Other ZH tools: `z_imagepacker`, `z_mapcachebuilder`, `z_guiedit`, `z_w3dview`, etc.

### Key build options (`cmake/config-build.cmake`)

`RTS_BUILD_ZEROHOUR` (default ON), `RTS_BUILD_GENERALS` (turn OFF to skip original Generals), `RTS_BUILD_OPTION_DEBUG`, `RTS_BUILD_OPTION_PROFILE`, `RTS_DEBUG_LOGGING`, `RTS_DEBUG_CRASHING`, `RTS_DEBUG_STACKTRACE`, `RTS_BUILD_OPTION_FFMPEG`. Debug defines: `RTS_DEBUG WWDEBUG DEBUG`; Release: `RTS_RELEASE NDEBUG`.

## Architecture

Three source roots, each a CMake subtree:

- **`Core/`** — engine + libraries shared by both games. Don't duplicate here what already lives in Core.
  - `Core/GameEngine/` — base engine.
  - `Core/GameEngineDevice/` — platform/render backends (DirectX 8).
  - `Core/Libraries/Source/WWVegas/` — Westwood "WW" graphics/math/3D framework (WW3D2, WWMath, WWLib...). Foundational; touched indirectly by most rendering work.
  - `Core/Libraries/Source/Compression/`, `debug/`, `profile/`.
  - `Core/Tools/` — shared dev tools.
- **`GeneralsMD/Code/`** — Zero Hour specific code. **Primary work area.**
- **`Generals/Code/`** — original Generals. Ignore for this fork.

### Engine split (the big picture)

Inside `GameEngine/Source/`:
- **`GameLogic/`** — simulation, rules, game state. Deterministic; this is what must stay in sync across the network and across replays.
- **`GameClient/`** — rendering, UI, input, audio — everything player-facing and machine-local.
- **`Common/`** — shared interfaces/types between Logic and Client (`GameEngine/Include/Common/`).
- **`GameNetwork/`** — netcode.
- `GameEngineDevice/Source/{W3DDevice,Win32Device}/` — concrete implementations of Client abstractions on W3D (DirectX) and Win32.

The Logic/Client separation is load-bearing: gameplay-affecting logic must be deterministic (replays/multiplayer depend on it). Keep rendering/UI out of `GameLogic`. Entry point: `GeneralsMD/Code/Main/WinMain.cpp`.

## Code conventions

- **Match surrounding legacy style.** Original code is C++98-flavored and deliberately simple. Prefer not to introduce modern C++ features unless they make the change clearly more robust/maintainable. Manual memory management (`new`/`delete`) is the norm.
- **Scope discipline.** Edits touch only the lines serving the change. Never mix a large refactor with a logic change in the same commit.
- **No `TheSuperHackers` change-marker comments.** That convention belongs to the upstream `TheSuperHackers/GeneralsGameCode` repo this was forked from — do not add those markers here. Use plain comments that explain *why* a change was made, only where the reason isn't obvious from the code.
- **Commit / PR titles** use Conventional Commits: `bugfix:`, `fix:`, `feat:`, `build:`, `perf:`, `refactor:`, `tweak:`, `chore:`, `ci:`, `docs:`, `style:`, `test:`, `unify:`. Optional scope, e.g. `bugfix(system): fix uninitialized memory in Get_OS_Info`. Present tense ("Fixes", not "Fixed").
- When naming game units in docs/comments, spell out and prefix with faction (USA/China/GLA/Boss/Civilian), e.g. "USA Missile Defender" not "MD".

## Testing

No unit-test suite for gameplay. Correctness for engine changes is verified via **replay compatibility** (`GeneralsReplays/`):

```
generalszh.exe -jobs 4 -headless -replay subfolder/*.rep
```

Copy replays into `%USERPROFILE%/Documents/Command and Conquer Generals Zero Hour Data/Replays/<subfolder>` and maps into `.../Maps`. Exit code signals pass/fail. Note: upstream CI runs this against a **VC6 optimized, `RTS_BUILD_OPTION_DEBUG=OFF`** build for retail-byte compatibility — modding-preset builds are not retail-compatible and won't match retail replays.

Running the game requires an installed copy of retail Generals/Zero Hour (assets).

## Tooling / linting

- `.clang-tidy` at root; custom checks in `scripts/clang-tidy-plugin/`. Run via `scripts/run-clang-tidy.py` against `compile_commands.json`.
- `scripts/cpp/` holds maintenance helpers (include-case fixes, whitespace cleanup, refactor scripts).
- `scripts/docker-build.sh` / `docker-install.sh --detect` for Linux builds + install to a detected game dir.

## WorldBuilder MCP

An MCP server that drives a running WorldBuilder: it edits the open map, its scripts and teams, and the AI data around them. Use its tools (`mcp__worldbuilder__*`) for map and script work instead of editing `.map` / `.scb` files by hand. Full tool reference: `scripts/worldbuilder-mcp/README.md`; every tool also documents itself.

```
Claude  <-- stdio -->  scripts/worldbuilder-mcp/server.py  <-- TCP 127.0.0.1:47800 -->  WorldBuilderZH.exe -mcp
```

### Running it

- WorldBuilder must run **from the mod folder** (`F:\Operation-Firestorm-2.0`, it needs the game data): `WorldBuilderMCP.bat` there starts `WorldBuilderZH.exe -mcp -ignoreAsserts`. Without `-ignoreAsserts` an assert dialog blocks the bridge.
- The server is registered in the repo's `.mcp.json` and started through `uv`. New or changed tools appear only after Claude is **fully quit and restarted**; WorldBuilder must be restarted after rebuilding it.
- The bridge serves one client. If tools fail with "Cannot reach WorldBuilder", WorldBuilder is not running with `-mcp` (or hangs in a dialog).
- WorldBuilder and the game read INI data at startup: restart after changing game data.

### Rules that apply to every edit

- **World units**: 10 per terrain cell, (0,0) at the lower left of the playable area. Heights are raw 0..255 (times `height_map_scale`). Angles in degrees, counterclockwise, 0 = +x. Start with `map_info`.
- **Look names up, never guess**: `objects_list_templates`, `terrain_list_textures`, `roads_list_types`, `sides_list_factions`, `scripts_list_actions` / `scripts_list_conditions`, `scripts_list_values`, `teams_list_properties`.
- **Every edit is one undo step** (`edit_undo`); WorldBuilder keeps 15. Many script edits belong in one `scripts_batch` (one step, all or nothing).
- **Nothing is saved until `map_save`** (or `scripts_export` for a script file). Overwriting an existing file needs an explicit flag; ask before overwriting the user's files.
- Script edits fail while WorldBuilder's own script editor window is open.
- `view_screenshot` shows the result; use it to check terrain and object placement.

### Map editing

- **Terrain**: `terrain_generate` (presets), `terrain_brush`, `terrain_set_heights` / `terrain_import_heightmap`, `terrain_limit_slope`, `terrain_set_passability`; textures with `terrain_paint_texture`, `terrain_auto_texture`, `terrain_auto_blend`.
- **Whole-map operations**: `map_resize`, `map_transform`, `map_symmetrize` (mirror or rotate one part onto the rest), `map_replace`.
- **Objects**: `objects_place` / `objects_place_many` / `objects_scatter`, `objects_modify`, `objects_set_properties`. Owners are team names from `sides_list` (`team` is neutral).
- **Waypoints, areas, water, roads**: `waypoints_add` / `waypoints_link`, `polygons_add`, `roads_add`, `roads_route` (road that follows the terrain).
- **Skirmish maps**: `map_generate_starts` for `Player_<N>_Start` waypoints, then `ai_skirmish_setup` (perimeters, `Center<N>` / `Flank<N>` / `Backdoor<N>` paths) and `ai_skirmish_check`. Plain `Center` labels without a player number are ignored by the AI.
- **Ships**: a map is meant for ships when its `map.ini` sets `EnableShips` (`map_info`: `ships_enabled`; `map_set_info` writes it). The AI then needs waypoints `Shipyard<player>_<n>`, the area `NavalPerimeter<player>` and paths `Naval<player>` / `NavalFlank<player>`; `ai_skirmish_check` lists what is there.

### Script and team editing

- **Read**: `scripts_list` (start with `detail="groups"` on big sets), `scripts_get`, `scripts_find` (who uses a team, flag, object type ...), `teams_list`, `teams_get`. **Write**: `scripts_add`, `scripts_update`, `scripts_move`, `scripts_delete`, the group tools, `teams_set`, `teams_set_many`.
- A script is JSON: flags, `conditions` (a list of OR clauses, each a list of ANDed conditions), `actions`, `false_actions`. Parameters are given in the order of the action's parameter types.
- **Bulk work**: `scripts_copy`, `teams_copy`, `scripts_clone_player`, `scripts_rename` (renames with all references), `scripts_set_values`, `scripts_batch`, `scripts_dump` / `scripts_load` (readable JSON, good for diffs and backups).
- **Checks**: `scripts_verify` (what the editor would flag), `scripts_missing` (broken references by cause), `scripts_analyze` (dead logic), `scripts_compare`.
- **Understanding a set**: `scripts_build_order`, `scripts_trace`, `scripts_by_difficulty`, `teams_summary`, `scripts_report`.
- **Files**: `scripts_import` / `scripts_export` read and write `.scb`. The mod's AI is `Data\Scripts\SkirmishScripts.scb` (about 3400 scripts, 750 teams, one `Skirmish<Faction>` player per faction): import it into a scratch map to work on it, and treat the installed file as the user's, to be overwritten only on request.

### Writing and testing AI

The pieces build on each other; files live in `worldbuilder_factions\` next to the WorldBuilder executable.

1. **What a faction can do**: `faction_generate` (once, and after game data changes), then `faction_get` (`overview`, `tree`, `units`, `naval`, single types). Roles, slots and phases are guesses until the faction's `.overrides.yaml` has been reviewed; types marked `unavailable` cannot be built.
2. **What every AI shares**: `ai_framework_generate` / `ai_framework_get` (common scripts, flags, timers, team roster and hooks, object lists).
3. **What is outside the scripts**: `aidata_get` reads `AIData.ini`. `SKIRMISH_BUILD_BUILDING` only builds structures that have a place in the side's `SkirmishBuildList`; `SideInfo` holds gatherer counts, the base defense structure and the science skill sets. Edit with `buildlist_from_aidata` / `buildlist_export` and `aidata_set_side` (they write only when given a file).
4. **A new faction AI**: `ai_scaffold` derives it from a related faction's player (dry run first), using `faction_map`; then work through its `unmapped` list.
5. **Static check**: `faction_check` on the player finds teams and build orders the faction cannot fulfil.
6. **Play it**: `ai_match_run` lets the game play a headless AI-vs-AI skirmish and `ai_match_report` says what each side built, never built, which scripts and teams never ran and what went wrong. `scripts="open"` plays the scripts open in WorldBuilder and `ai_data=<file>` a changed `AIData.ini`, without installing either. The same seed repeats a match exactly, so run before and after a change.

### Working on the MCP itself

- Bridge commands: `GeneralsMD/Code/Tools/WorldBuilder/src/mcp/McpCmd*.cpp` (handlers run on the UI thread, take and return `McpJson`, fail with `mcpFail`, are registered with `mcpRegisterCommand`). Edits go through an `Undoable` so that undo works.
- Server tools: `scripts/worldbuilder-mcp/server.py`; logic that needs no bridge is in `faction.py`, `framework.py`, `scaffold.py`, `aidata.py`, `match.py`.
- AI matches are an engine feature (`GameLogic/AIMatch.h`): `generalszh.exe -aiMatch <map> -aiPlayers Faction:difficulty,... -aiMatchLog <file> [-headless]`.
- After a change: build `z_worldbuilder`, copy `WorldBuilderZH.exe` to the mod folder, restart it with `-mcp -ignoreAsserts`, run `python scripts/worldbuilder-mcp/smoke_test.py` (it replaces the open map with a test map) and extend that test for new commands.
