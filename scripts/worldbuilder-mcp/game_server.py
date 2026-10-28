"""MCP server that lets an AI assistant play and inspect a running Zero Hour game.

The game (generalszh.exe built with RTS_BUILD_OPTION_GAME_MCP) must be started with ``-mcp``;
game_launch does that. Then point an MCP client at this script (stdio).
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import time
from typing import Any, Literal

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from wb_bridge import WorldBuilderBridge, WorldBuilderError

GAME_PORT = 47801
GAME_EXE = os.environ.get("GAME_EXE", "generalszh.exe")
GAME_DIR = os.environ.get("GAME_DIR", r"F:\Operation-Firestorm-2.0")
FRAMES_PER_SECOND = 30

INSTRUCTIONS = """\
Plays and inspects a running Command & Conquer Generals Zero Hour game, to test game features.

Starting:
- game_launch starts generalszh.exe from the mod folder on a skirmish map with the local player's
  faction and optional AI opponents ("FactionChina:hard"). Without opponents it is a sandbox in
  which nobody wins or loses. By default it starts paused at frame 1 so nothing has happened yet.
  headless=true runs only the game logic, without a window (fast; no screenshots or camera).
- A game started by hand with -mcp works too; game_status says what is running.

Time:
- The logic runs 30 frames per second. game_pause / game_resume, game_step(frames) runs that many
  frames and pauses again (it waits until they are done). game_set_speed changes how fast it runs
  (0 = as fast as possible).
- Orders take effect on the next logic frame: give them while paused, then game_step.
- game_save / game_load write and read save files like the in-game menu (not headless; load pauses afterwards).

Looking:
- objects_list (filters: player, template, kindof, status, area) gives ids, positions, health;
  objects_get gives everything about objects (status, weapons, AI state, upgrades, special powers,
  production, passengers, modules, bonuses). players_get: money, power, sciences, upgrades,
  battle plans, score.
- objects_templates finds template names. game_messages shows what scripts and the AI reported.
- view_screenshot / view_camera need the window (not headless, not minimized).

Changing:
- orders_* give the units of any player orders as that player would (recorded in the replay).
- objects_spawn / objects_destroy / objects_set / players_set change the game directly (test cheats).
- scripts_run, scripts_set_active, scripts_flags, scripts_counters work on the map's scripts.

Coordinates are world units (10 per terrain cell, (0,0) lower left of the playable area); angles
are degrees, counterclockwise, 0 = +x.
"""

mcp = MCPServer("game", instructions=INSTRUCTIONS)
_bridge = WorldBuilderBridge(label="the game", env_prefix="GAME_MCP", default_port=GAME_PORT,
                             start_hint="Start it with game_launch, or run generalszh.exe with -mcp.")
_process: subprocess.Popen | None = None


class _Bridge:
    """Forwards to the bridge, turning its errors into ToolError so their text reaches the client."""

    @staticmethod
    def call(cmd: str, **args: Any) -> Any:
        try:
            return _bridge.call(cmd, **args)
        except WorldBuilderError as exc:
            raise ToolError(str(exc)) from exc


game = _Bridge()


def _reachable() -> bool:
    try:
        _bridge.call("game.status")
        return True
    except (WorldBuilderError, OSError):
        _bridge.close()
        return False


def _wait_paused_at(target: int, timeout: float) -> dict:
    """Polls until the logic has reached target and paused again."""
    deadline = time.time() + timeout
    while True:
        status = game.call("game.status")
        if status["frame"] >= target and status["paused"]:
            return status
        if not status["in_game"]:
            raise ToolError(f"the game ended at frame {status['frame']}")
        if time.time() > deadline:
            status["note"] = f"still running after {timeout} s; it pauses at frame {target}"
            return status
        time.sleep(0.05)


# --------------------------------------------------------------------------------------------------
# Game
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def game_launch(map: str, faction: str = "FactionAmerica", opponents: list[str] | None = None, team: int | None = None,
                seed: int = 1, cash: int | None = None, headless: bool = False, start_paused: bool = True,
                audio: bool = False, width: int = 1280, height: int = 720, extra_args: list[str] | None = None,
                wait_seconds: float = 180) -> dict:
    """Starts the game on a skirmish map with the local player and optional AI opponents.
    - map: name of a multiplayer map or part of its path ("tournament desert").
    - faction: player template of the local player (FactionAmerica, FactionChina, FactionGLA,
      a general like FactionAmericaAirForceGeneral, or Observer to only watch); team: its team number.
    - opponents: AI players as "Faction:difficulty[:team]" (easy, normal, hard), e.g.
      ["FactionChina:hard"]. None: a sandbox without opponents in which nobody wins.
    - seed fixes start positions and randomness; cash is the starting money.
    - headless: no window, only the game logic (no screenshots, camera or sound).
    - start_paused: pause at the first frame, before anything has happened.
    The game runs from GAME_DIR (default F:\\Operation-Firestorm-2.0). Waits until the map has
    loaded and returns game_status. Fails if a game with the bridge is already running (game_quit)."""
    global _process
    if _reachable():
        raise ToolError("a game is already running with the bridge; game_quit it first")
    exe = os.path.join(GAME_DIR, GAME_EXE)
    if not os.path.exists(exe):
        raise ToolError(f"{exe} not found (set GAME_DIR / GAME_EXE)")
    local = faction if team is None else f"{faction}:{team}"
    players = ",".join([local] + list(opponents or []))
    args = ["-mcp", "-mcpGame", map, "-mcpPlayers", players, "-seed", str(seed), "-quickstart", "-ignoreAsserts"]
    args += ["-headless"] if headless else ["-win", "-xres", str(width), "-yres", str(height)]
    if not audio and not headless:
        args.append("-noaudio")
    if cash:
        args += ["-mcpCash", str(cash)]
    if start_paused:
        args.append("-mcpStartPaused")
    args += list(extra_args or [])
    _process = subprocess.Popen([exe] + args, cwd=GAME_DIR)

    deadline = time.time() + wait_seconds
    status: dict | None = None
    while time.time() < deadline:
        if _process.poll() is not None:
            raise ToolError(f"the game exited with code {_process.returncode} before the bridge answered "
                            f"(command line: {' '.join(args)})")
        if _reachable():
            status = game.call("game.status")
            if status.get("start_error"):
                raise ToolError(f"the game did not start: {status['start_error']}")
            if status["in_game"] and status["frame"] >= 1 and (status["paused"] or not start_paused):
                break
        time.sleep(0.5)
    if status is None:
        raise ToolError(f"the bridge did not answer within {wait_seconds} s")
    status["pid"] = _process.pid
    status["command_line"] = args
    return status


@mcp.tool()
def game_status() -> dict:
    """In game, map, frame (30 per second), seconds, paused, speed, headless, sandbox, local player."""
    return game.call("game.status")


@mcp.tool()
def game_pause() -> dict:
    """Pauses the game logic. The window keeps drawing; inspection and orders still work."""
    return game.call("game.pause")


@mcp.tool()
def game_resume() -> dict:
    """Lets the game logic run freely again."""
    return game.call("game.resume")


@mcp.tool()
def game_step(frames: int | None = None, seconds: float | None = None, wait: bool = True, timeout: float = 300) -> dict:
    """Runs the game for some logic frames (or seconds of game time, 30 frames each), then pauses.
    With wait it returns once they have run, with the status at that point."""
    if frames is None:
        frames = max(1, round((seconds or 1 / FRAMES_PER_SECOND) * FRAMES_PER_SECOND))
    result = game.call("game.step", frames=frames)
    if not wait:
        return result
    return _wait_paused_at(result["pause_at_frame"], timeout)


@mcp.tool()
def game_set_speed(fps: int, render_fps: int | None = None) -> dict:
    """Logic frames per second: 30 is normal speed, 60 double, 0 as fast as possible.
    render_fps (windowed only) lets the window render at its own rate (0 = uncapped) while the
    logic keeps fps, as in network games; needed to see UseRenderInterpolation at work."""
    if render_fps is None:
        return game.call("game.set_speed", fps=fps)
    return game.call("game.set_speed", fps=fps, render_fps=render_fps)


@mcp.tool()
def game_messages(since: int = 0, contains: str | None = None, limit: int = 200) -> dict:
    """What the script engine and the AI reported (script debug messages, warnings), newest last.
    Pass next_serial of the last call as since to get only new ones."""
    return game.call("game.messages", since=since, contains=contains, limit=limit)


@mcp.tool()
def game_save(filename: str | None = None, description: str | None = None) -> dict:
    """Saves the running game like the in-game save menu, into the save directory (not headless:
    a save also holds the client side). filename is a plain file name (.sav is added); without
    one a new numbered file is created. Returns the file name and path."""
    return game.call("game.save", filename=filename, description=description)


@mcp.tool()
def game_load(filename: str, paused: bool = True) -> dict:
    """Loads a save file from the save directory like the in-game load menu: the running game is
    replaced by the saved one, at the saved frame. Not headless. Pauses afterwards unless paused=false."""
    return game.call("game.load", filename=filename, paused=paused)


@mcp.tool()
def game_quit(wait_seconds: float = 20) -> dict:
    """Ends the game and closes it."""
    result = game.call("game.quit")
    _bridge.close()
    if _process is not None:
        try:
            _process.wait(timeout=wait_seconds)
            result["exit_code"] = _process.returncode
        except subprocess.TimeoutExpired:
            result["note"] = "the game is still closing"
    return result


@mcp.tool()
def game_command(cmd: str, args: dict | None = None) -> Any:
    """Runs any bridge command directly (list_commands lists them with their arguments)."""
    return game.call(cmd, **(args or {}))


# --------------------------------------------------------------------------------------------------
# Objects and players
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def objects_list(player: str | None = None, template: str | None = None, name: str | None = None,
                 kindof: str | None = None, status: str | None = None, x: float | None = None, y: float | None = None,
                 radius: float | None = None, x0: float | None = None, y0: float | None = None, x1: float | None = None,
                 y1: float | None = None, include_dead: bool = False, limit: int = 200) -> dict:
    """Objects in the game with id, template, owner, position, angle and health. Filters: player
    (internal name like "player0", display name, "local"), template (part of the name), name (script
    name), kindof (e.g. INFANTRY, STRUCTURE), status (e.g. UNDER_CONSTRUCTION), circle x,y,radius or
    rectangle x0,y0,x1,y1. matched counts all hits, objects holds at most limit."""
    return game.call("objects.list", player=player, template=template, name=name, kindof=kindof, status=status,
                     x=x, y=y, radius=radius, x0=x0, y0=y0, x1=x1, y1=y1, include_dead=include_dead or None, limit=limit)


ObjectSection = Literal["kindof", "model", "ai", "weapons", "upgrades", "bonuses", "special_powers", "production", "contain", "modules"]


@mcp.tool()
def objects_get(ids: list[int], sections: list[ObjectSection] | None = None) -> dict:
    """Everything about objects: owner, team, position, health, damage state, status and disabled
    flags, veterancy and experience, velocity, construction progress, and the sections kindof,
    model (model conditions), ai (state, goal), weapons (per slot: status, ammo, range, reload),
    upgrades, bonuses (weapon bonus conditions, weapon and armor set flags, armor damage scalar,
    vision and shroud clearing range, speed multiplier), special_powers (ready, percent),
    production queue, contain (passengers) and modules (behavior modules with their INI tags).
    sections limits the optional parts (default all)."""
    return game.call("objects.get", ids=ids, sections=sections)


@mcp.tool()
def objects_templates(contains: str | None = None, side: str | None = None, kindof: str | None = None,
                      limit: int = 300) -> dict:
    """Object templates (names for objects_spawn, orders_build, orders_produce) with side, cost and
    build time."""
    return game.call("objects.templates", contains=contains, side=side, kindof=kindof, limit=limit)


@mcp.tool()
def players_list() -> dict:
    """All players: internal name, display name, side, faction, human or computer, local, active, money."""
    return game.call("players.list")


@mcp.tool()
def players_get(player: str | None = None) -> dict:
    """One player (default: the local one): money, power, rank, skill and science points, sciences,
    player upgrades, battle_plans (active plan sources per plan and the accumulated armor and
    sight scalars), score, relationships to the others and its objects counted by template."""
    return game.call("players.get", player=player)


# --------------------------------------------------------------------------------------------------
# Orders
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def orders_move(ids: list[int], x: float, y: float, attack_move: bool = False) -> dict:
    """Moves units (all of one player) to a position; attack_move fights on the way."""
    return game.call("orders.move", ids=ids, x=x, y=y, attack_move=attack_move or None)


@mcp.tool()
def orders_attack(ids: list[int], target: int | None = None, x: float | None = None, y: float | None = None,
                  force: bool = False) -> dict:
    """Attacks an object (force: even an ally or neutral one), or fires at the ground at x, y."""
    return game.call("orders.attack", ids=ids, target=target, x=x, y=y, force=force or None)


@mcp.tool()
def orders_stop(ids: list[int]) -> dict:
    """Stops units."""
    return game.call("orders.stop", ids=ids)


@mcp.tool()
def orders_guard(ids: list[int], target: int | None = None, x: float | None = None, y: float | None = None,
                 mode: int = 0) -> dict:
    """Guards an object or a position (mode 0 normal, 1 without pursuit, 2 against aircraft only)."""
    return game.call("orders.guard", ids=ids, target=target, x=x, y=y, mode=mode)


@mcp.tool()
def orders_sell(ids: list[int]) -> dict:
    """Sells structures."""
    return game.call("orders.sell", ids=ids)


@mcp.tool()
def orders_build(dozer: int, template: str, x: float, y: float, angle: float = 0) -> dict:
    """Orders a dozer or worker to build a structure at a position (costs money as usual)."""
    return game.call("orders.build", dozer=dozer, template=template, x=x, y=y, angle=angle)


@mcp.tool()
def orders_produce(factory: int, template: str, count: int = 1) -> dict:
    """Queues units in a production building (costs money as usual)."""
    return game.call("orders.produce", factory=factory, template=template, count=count)


@mcp.tool()
def orders_upgrade(id: int, upgrade: str) -> dict:
    """Queues an upgrade in an object (a building researches it, or a unit gets it)."""
    return game.call("orders.upgrade", id=id, upgrade=upgrade)


@mcp.tool()
def orders_special_power(id: int, power: str | None = None, button: str | None = None, target: int | None = None,
                         x: float | None = None, y: float | None = None, angle: float | None = None,
                         options: int | None = None) -> dict:
    """Uses a special power of an object: at an object (target), at a position (x, y, angle) or
    without a target. Name the power (SpecialPower template, see objects_get special_powers) or the
    command button that fires it (button also supplies the UI's command options)."""
    return game.call("orders.special_power", id=id, power=power, button=button, target=target, x=x, y=y, angle=angle,
                     options=options)


@mcp.tool()
def orders_purchase_science(science: str, player: str | None = None) -> dict:
    """Buys a general's power (science) with science points, as the player would."""
    return game.call("orders.purchase_science", science=science, player=player)


# --------------------------------------------------------------------------------------------------
# Test changes
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def objects_spawn(template: str, x: float, y: float, player: str | None = None, team: str | None = None,
                  angle: float = 0, count: int = 1, spacing: float = 20, name: str | None = None) -> dict:
    """Creates objects (count of them in a grid) for a player (default: the local one) at a position.
    Not recorded in the replay."""
    return game.call("objects.spawn", template=template, x=x, y=y, player=player, team=team, angle=angle, count=count,
                     spacing=spacing, name=name)


@mcp.tool()
def objects_destroy(ids: list[int], how: Literal["kill", "remove"] = "kill") -> dict:
    """Kills objects (they die as usual: death effects, rubble, experience) or removes them silently."""
    return game.call("objects.destroy", ids=ids, how=how)


@mcp.tool()
def objects_set(ids: list[int], health: float | None = None, health_percent: float | None = None,
                max_health: float | None = None, x: float | None = None, y: float | None = None,
                angle: float | None = None, veterancy: int | str | None = None, upgrade: str | None = None,
                set_status: str | None = None, clear_status: str | None = None) -> dict:
    """Changes objects directly: health (0 kills), max health, position, angle, veterancy (0..3 or
    REGULAR, VETERAN, ELITE, HEROIC), gives an object upgrade, sets or clears a status flag."""
    return game.call("objects.set", ids=ids, health=health, health_percent=health_percent, max_health=max_health,
                     x=x, y=y, angle=angle, veterancy=veterancy, upgrade=upgrade, set_status=set_status,
                     clear_status=clear_status)


@mcp.tool()
def players_set(player: str | None = None, money: int | None = None, add_money: int | None = None,
                rank: int | None = None, add_skill_points: int | None = None, add_science_points: int | None = None,
                sciences: list[str] | None = None, all_sciences: bool = False, upgrades: list[str] | None = None,
                instant_build: bool | None = None, special_power_delays: bool | None = None) -> dict:
    """Changes a player (default: the local one): money, rank, points, grants sciences or player
    upgrades, instant building; special_power_delays=false makes special powers recharge at once
    (for everyone)."""
    return game.call("players.set", player=player, money=money, add_money=add_money, rank=rank,
                     add_skill_points=add_skill_points, add_science_points=add_science_points, sciences=sciences,
                     all_sciences=all_sciences or None, upgrades=upgrades, instant_build=instant_build,
                     special_power_delays=special_power_delays)


# --------------------------------------------------------------------------------------------------
# Scripts
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def scripts_run(name: str) -> dict:
    """Runs a map or AI script's actions now, whatever its conditions."""
    return game.call("scripts.run", name=name)


@mcp.tool()
def scripts_set_active(name: str, active: bool = True) -> dict:
    """Enables or disables a script or script group."""
    return game.call("scripts.set_active", name=name, active=active)


@mcp.tool()
def scripts_flags(contains: str | None = None, set: dict[str, bool] | None = None) -> dict:
    """Script flags and their values (set changes some first)."""
    return game.call("scripts.flags", contains=contains, set=set)


@mcp.tool()
def scripts_counters(contains: str | None = None, set: dict[str, int] | None = None) -> dict:
    """Script counters and timers (timers count logic frames; set changes some first)."""
    return game.call("scripts.counters", contains=contains, set=set)


# --------------------------------------------------------------------------------------------------
# View
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def view_camera(id: int | None = None, x: float | None = None, y: float | None = None, angle: float | None = None,
                pitch: float | None = None, zoom: float | None = None, reset: bool = False) -> dict:
    """Moves the camera to an object or a position, turns or zooms it, and returns where it is."""
    return game.call("view.camera", id=id, x=x, y=y, angle=angle, pitch=pitch, zoom=zoom, reset=reset or None)


@mcp.tool()
def view_screenshot(id: int | None = None, x: float | None = None, y: float | None = None, zoom: float | None = None,
                    path: str | None = None, format: Literal["jpg", "png"] = "jpg", quality: int = 85) -> Image:
    """Returns what the game window shows. Optionally moves the camera first (to an object or a
    position); a moved camera needs a drawn frame, so the picture is taken a moment later. The
    window must not be minimized; not available headless."""
    if id is not None or x is not None or y is not None or zoom is not None:
        game.call("view.camera", id=id, x=x, y=y, zoom=zoom)
        time.sleep(0.3)
    if path is None:
        path = os.path.join(tempfile.gettempdir(), f"game_mcp_{int(time.time() * 1000)}.{format}")
    result = game.call("view.screenshot", path=path, quality=quality)
    return Image(path=result["path"])


if __name__ == "__main__":
    mcp.run()
