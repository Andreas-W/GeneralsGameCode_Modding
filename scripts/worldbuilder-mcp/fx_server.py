"""MCP server for the FX Editor (FXEditorZH.exe): previews and edits ParticleSystem and FXList INI
with the game's own renderer and audio.

The editor runs the game engine on a flat preview map (a single player game) and serves the game MCP
bridge on port 47802, with its own fx.* commands. fx_launch starts it from the mod folder (GAME_DIR).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any, Literal

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from wb_bridge import WorldBuilderBridge, WorldBuilderError

FX_PORT = 47802
FX_EXE = os.environ.get("FX_EXE", "FXEditorZH.exe")
GAME_DIR = os.environ.get("GAME_DIR", "")
PREVIEW_MAP = "FXEditorPreview"
REPO_MAP_DIR = Path(__file__).resolve().parents[2] / "GeneralsMD" / "Code" / "Tools" / "FXEditor" / "Data" / "Maps" / PREVIEW_MAP
FRAMES_PER_SECOND = 30

Kind = Literal["particle", "fxlist", "sound"]
EditKind = Literal["particle", "fxlist"]

INSTRUCTIONS = """\
Previews and edits particle systems and FXLists of Command & Conquer Generals Zero Hour with the
game's own rendering (Direct3D 8, or 12 with dx12=true) and audio, in the FX Editor.

Starting: fx_launch starts FXEditorZH.exe from the mod folder; it loads a flat preview map as a
single player game and returns fx_status. fx_quit ends it. One editor at a time.

Finding things: fx_list(kind, contains) lists names the game knows (kind particle, fxlist, sound).
Never guess names. fx_get shows a block: its INI text, file, fields (particle systems) or nuggets
with their fields (FXLists). fx_schema lists the possible fields with types and allowed values.

Playing: fx_play plays a particle system, FXList or sound at the preview point (optionally repeated,
moving in a circle, higher up, or on a spawned object with attach_to). fx_time pauses, steps logic
frames (30 per second) and slows down or speeds up. fx_stats counts particles; fx_capture returns a
picture, fx_capture_sequence a contact sheet of frames over time. fx_center moves the preview point
and the camera.

Editing: fx_set_fields changes fields (null removes one), fx_nugget adds, removes, moves or duplicates
FXList nuggets, fx_set_text replaces the whole block, fx_create copies a block under a new name.
Every edit is parsed by the game at once and what plays restarts; a parse error is returned and the
game keeps the last good text. fx_undo undoes, redoes or reverts. fx_validate finds unknown fields and
names the game would not find (textures, systems, sounds, objects).

Saving: nothing is written until fx_save(overwrite=true). It puts only the changed blocks back into
the mod's INI files and keeps <file>.fxeditor.bak of each file. Ask the user before saving.
"""

mcp = MCPServer("fx", instructions=INSTRUCTIONS)
_bridge = WorldBuilderBridge(label="the FX Editor", env_prefix="FX_MCP", default_port=FX_PORT,
                             start_hint="Start it with fx_launch, or run FXEditorZH.exe from the mod folder.")
_process: subprocess.Popen | None = None


class _Bridge:
    @staticmethod
    def call(cmd: str, **args: Any) -> Any:
        try:
            return _bridge.call(cmd, **args)
        except WorldBuilderError as exc:
            raise ToolError(str(exc)) from exc


fx = _Bridge()


def _reachable() -> bool:
    try:
        _bridge.call("fx.status")
        return True
    except (WorldBuilderError, OSError):
        _bridge.close()
        return False


def _user_maps_dir() -> Path:
    return Path.home() / "Documents" / "Command and Conquer Generals Zero Hour Data" / "Maps"


def _install_preview_map() -> str | None:
    """Copies the preview map from the repository into the user's Maps folder when it is missing or
    differs from the repository's."""
    target = _user_maps_dir() / PREVIEW_MAP
    if not REPO_MAP_DIR.exists():
        return None
    changed = [f for f in REPO_MAP_DIR.iterdir()
               if not (target / f.name).exists() or (target / f.name).read_bytes() != f.read_bytes()]
    if not changed:
        return None
    target.mkdir(parents=True, exist_ok=True)
    for f in changed:
        shutil.copy2(f, target / f.name)
    return str(target)


def _wait_frame(target: int, timeout: float = 30) -> dict:
    deadline = time.time() + timeout
    while True:
        status = fx.call("fx.status")
        if status["frame"] >= target and status["paused"]:
            return status
        if time.time() > deadline:
            raise ToolError(f"the editor did not reach frame {target} within {timeout} s")
        time.sleep(0.03)


# --------------------------------------------------------------------------------------------------
# Editor
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def fx_launch(dx12: bool = False, map: str | None = None, width: int = 1600, height: int = 900, audio: bool = True,
              extra_args: list[str] | None = None, wait_seconds: float = 180) -> dict:
    """Starts the FX Editor from the mod folder (GAME_DIR) and waits until the preview game runs.
    - dx12: render with Direct3D 12 instead of 8 (needs a build with RTS_BUILD_OPTION_DX12).
    - map: another map as backdrop, a name from a Maps folder or a .map path (default: the flat
      preview map, installed into the user's Maps folder from the repository when missing or old).
      The local player is the map's human player.
    Returns fx_status. Fails if an editor with the bridge already runs (fx_quit it)."""
    global _process
    if _reachable():
        raise ToolError("an FX Editor is already running with the bridge; fx_quit it first")
    if not GAME_DIR:
        raise ToolError('GAME_DIR is not set; add {"env": {"GAME_DIR": "<mod folder>"}} to .claude/settings.local.json and restart Claude')
    exe = os.path.join(GAME_DIR, FX_EXE)
    if not os.path.exists(exe):
        raise ToolError(f"{exe} not found: build z_fxeditor and copy FXEditorZH.exe into the mod folder (or set FX_EXE)")
    installed = _install_preview_map() if map is None else None
    args = ["-xres", str(width), "-yres", str(height)]
    if dx12:
        args.append("-dx12")
    if map:
        args += ["-map", map]
    if not audio:
        args.append("-noaudio")
    args += list(extra_args or [])
    _process = subprocess.Popen([exe] + args, cwd=GAME_DIR)

    deadline = time.time() + wait_seconds
    status: dict | None = None
    while time.time() < deadline:
        if _process.poll() is not None:
            raise ToolError(f"the editor exited with code {_process.returncode} before the bridge answered "
                            f"(command line: {' '.join(args)})")
        if _reachable():
            status = fx.call("fx.status")
            if status.get("start_error"):
                raise ToolError(f"the preview game did not start: {status['start_error']}")
            if status["ready"] and status["ini_loaded"]:
                break
        time.sleep(0.5)
    if status is None or not status.get("ready"):
        raise ToolError(f"the editor was not ready within {wait_seconds} s")
    status["pid"] = _process.pid
    status["command_line"] = args
    if installed:
        status["installed_preview_map"] = installed
    return status


@mcp.tool()
def fx_maps(contains: str | None = None) -> dict:
    """Maps the editor can load (map cache key, name, start positions, official) and the playable
    factions. contains filters by part of the key or name."""
    return fx.call("fx.maps", contains=contains)


@mcp.tool()
def fx_load_map(map: str, faction: str = "FactionAmerica", wait_seconds: float = 120) -> dict:
    """Loads another map into the running editor and waits until the preview is ready again.
    - map: a key from fx_maps, a map name or part of its path; "FXEditorPreview" is the empty
      default map.
    - faction: the local player's faction on multiplayer maps (2+ start positions), which run as a sandbox
      skirmish (starting units removed). Other maps run as a single player game
      with the map's own player and ignore it.
    What played is played again on the new map; edits are kept. Returns fx_status."""
    result = fx.call("fx.load_map", map=map, faction=faction)
    deadline = time.time() + wait_seconds
    while time.time() < deadline:
        time.sleep(0.3)
        status = fx.call("fx.status")
        if status.get("map_key") == result["map_key"] and not status.get("map_loading"):
            return status
    raise ToolError(f"{result['map_key']} was not ready within {wait_seconds} s")


@mcp.tool()
def fx_status() -> dict:
    """Preview game ready, map, renderer (dx8/dx12), frame, paused, speed, what plays, preview point,
    and the blocks with unsaved changes."""
    return fx.call("fx.status")


@mcp.tool()
def fx_quit() -> dict:
    """Closes the editor. Unsaved changes are lost (check fx_status first)."""
    global _process
    result = fx.call("game.quit")
    _bridge.close()
    if _process is not None:
        try:
            _process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            pass
        _process = None
    return result


@mcp.tool()
def fx_log(since: int = 0) -> dict:
    """Messages of the editor (parse errors, saves) after serial `since`; pass next_serial next time."""
    return fx.call("fx.log", since=since)


# --------------------------------------------------------------------------------------------------
# Looking things up
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def fx_list(kind: Kind = "particle", contains: str | None = None, limit: int = 200, offset: int = 0) -> dict:
    """Names of particle systems, FXLists or sounds the game knows, filtered by a substring.
    has_text: the editor found its INI text and can edit it; dirty: changed and not saved."""
    return fx.call("fx.list", kind=kind, contains=contains, limit=limit, offset=offset)


@mcp.tool()
def fx_get(kind: EditKind, name: str, text: bool = True) -> dict:
    """One block: INI text, file, fields (particle) or nuggets with their fields (fxlist), dirty, error."""
    return fx.call("fx.get", kind=kind, name=name, text=text)


@mcp.tool()
def fx_schema(kind: EditKind = "particle", nugget_type: str | None = None) -> dict:
    """The fields a particle system or each FXList nugget type can have: type, allowed values, help.
    Value formats: random "min max", coord "X:1 Y:2 Z:3", color "R:255 G:128 B:0", alpha keyframe
    "low high frame", color keyframe "R:255 G:255 B:255 frame", bools Yes/No."""
    return fx.call("fx.schema", kind=kind, nugget_type=nugget_type)


@mcp.tool()
def fx_validate(kind: EditKind, name: str) -> dict:
    """Unknown fields and names the game would not find (textures, particle systems, FXLists, sounds,
    objects) in a block."""
    return fx.call("fx.validate", kind=kind, name=name)


# --------------------------------------------------------------------------------------------------
# Editing
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def fx_set_fields(kind: EditKind, name: str, fields: dict[str, Any], nugget: int | None = None) -> dict:
    """Sets fields of a particle system, or of one nugget (index from fx_get) of an FXList; a null
    value removes the field. One undo step; the game parses it at once and what plays restarts.
    Values are written as given (strings in INI format; numbers and bools are converted)."""
    return fx.call("fx.set_fields", kind=kind, name=name, fields=fields, nugget=nugget)


@mcp.tool()
def fx_set_text(kind: EditKind, name: str, text: str, keep_on_error: bool = False) -> dict:
    """Replaces the whole INI text of a block (from its header line to End)."""
    return fx.call("fx.set_text", kind=kind, name=name, text=text, keep_on_error=keep_on_error)


@mcp.tool()
def fx_nugget(name: str, action: Literal["add", "remove", "move", "duplicate"], index: int | None = None,
              type: str | None = None, fields: dict[str, Any] | None = None, offset: int | None = None) -> dict:
    """Edits the nuggets of an FXList: add (type from fx_schema, optional fields, inserted before
    index or at the end), remove, move (by offset), duplicate."""
    return fx.call("fx.nugget", kind="fxlist", name=name, action=action, index=index, type=type, fields=fields, offset=offset)


@mcp.tool()
def fx_create(kind: EditKind, name: str, copy_from: str | None = None) -> dict:
    """A new particle system or FXList, as a copy of another one or minimal. It goes into the file of
    the copied block (or the main file of its kind) on save."""
    return fx.call("fx.create", kind=kind, name=name, copy_from=copy_from)


@mcp.tool()
def fx_undo(kind: EditKind, name: str, action: Literal["undo", "redo", "revert"] = "undo") -> dict:
    """Undo or redo an edit of a block, or revert it to the text on disk."""
    return fx.call("fx.undo", kind=kind, name=name, action=action)


@mcp.tool()
def fx_save(overwrite: bool = False, kind: EditKind | None = None, name: str | None = None) -> dict:
    """Writes the changed blocks into the mod's INI files (all, or the file of one block). Needs
    overwrite=true; ask the user first. The first save of a file keeps <file>.fxeditor.bak."""
    return fx.call("fx.save", overwrite=overwrite, kind=kind, name=name)


@mcp.tool()
def fx_reload() -> dict:
    """Reads the INI text from disk again (after it was changed outside the editor)."""
    return fx.call("fx.reload")


# --------------------------------------------------------------------------------------------------
# Playing and looking
# --------------------------------------------------------------------------------------------------

@mcp.tool()
def fx_play(name: str, kind: Kind = "particle", repeat: bool | None = None, repeat_seconds: float | None = None,
            height: float | None = None, move: bool | None = None, move_radius: float | None = None,
            move_speed: float | None = None, angle: float | None = None, attach_to: str | None = None,
            x: float | None = None, y: float | None = None, follow_camera: bool | None = None) -> dict:
    """Plays a particle system, FXList or sound at the preview point, replacing what played before.
    - repeat / repeat_seconds: play again every so many seconds (systems that live forever start once).
    - height: above the ground; angle: direction in degrees.
    - move, move_radius, move_speed: the emitter circles the preview point (trails, drift).
    - attach_to: thing template to spawn and play on (object FX, attached systems); "" for none.
    - x, y: moves the preview point first.
    - follow_camera: each start (play, restart, repeat) happens where the camera looks, if the camera
      moved since the preview point was set; what already runs stays (default on).
    Settings not given stay as they were."""
    return fx.call("fx.play", kind=kind, name=name, repeat=repeat, repeat_seconds=repeat_seconds, height=height,
                   move=move, move_radius=move_radius, move_speed=move_speed, angle=angle, attach_to=attach_to, x=x, y=y,
                   follow_camera=follow_camera)


# --------------------------------------------------------------------------------------------------
# Direct3D 12: post-processing, materials, shadow map (PostProcess and Material INI)
# --------------------------------------------------------------------------------------------------

RenderType = Literal["PostProcessPass", "PostProcess", "PostProcessChain", "ShadowMap", "Material"]


@mcp.tool()
def fx_render_status(postprocess: bool | None = None, materials: bool | None = None) -> dict:
    """Direct3D 12 renderer (editor started with dx12=true): whether post-processing and materials are
    on and running, the passes that run, the materials built (with how many textures got each), errors,
    and the render blocks of the PostProcess / Material INI. postprocess / materials turn them on or
    off for the comparison (not saved)."""
    return fx.call("fx.render_status", postprocess=postprocess, materials=materials)


@mcp.tool()
def fx_render_get(type: RenderType, name: str | None = None, text: bool = True) -> dict:
    """A render block: its fields, its params (Param lines by name) and its text. name is not needed
    for PostProcess and ShadowMap."""
    return fx.call("fx.render_get", type=type, name=name, text=text)


@mcp.tool()
def fx_render_set(type: RenderType, name: str | None = None, fields: dict | None = None, params: dict | None = None,
                  text: str | None = None) -> dict:
    """Changes a render block and applies it at once (the renderer builds the chain or materials again).
    - fields: e.g. {"Inputs": "Scene Depth", "HDR": true, "Scale": 2}; null removes one.
    - params: Param lines by name, e.g. {"Saturation": 0.2, "Color": [0.5, 0.6, 0.7]}; null removes one.
    - text: the whole block instead.
    Fails with the game's parse error and keeps the last good text. Nothing is saved (fx_render_save)."""
    return fx.call("fx.render_set", type=type, name=name, fields=fields, params=params, text=text)


@mcp.tool()
def fx_render_create(type: Literal["PostProcessPass", "PostProcessChain", "Material"], name: str,
                     copy_from: str | None = None) -> dict:
    """A new pass, chain or material (a copy of copy_from, or a minimal one); goes into the mod's
    PostProcess.ini / Material.ini on save. A pass runs only once it is in Passes of PostProcess."""
    return fx.call("fx.render_create", type=type, name=name, copy_from=copy_from)


@mcp.tool()
def fx_render_undo(type: RenderType, name: str | None = None, action: Literal["undo", "redo", "revert"] = "undo") -> dict:
    """Undo, redo, or back to the text on disk, of a render block."""
    return fx.call("fx.render_undo", type=type, name=name, action=action)


@mcp.tool()
def fx_render_save(overwrite: bool = False) -> dict:
    """Writes the changed render blocks into the mod's PostProcess / Material INI files (keeps
    <file>.fxeditor.bak). Needs overwrite=true; ask the user first."""
    return fx.call("fx.render_save", overwrite=overwrite)


@mcp.tool()
def fx_render_reload() -> dict:
    """Reads the HLSL shader files and the render INI files again (after editing a shader); changes
    not saved stay on top."""
    return fx.call("fx.render_reload")


@mcp.tool()
def fx_select(name: str, kind: EditKind = "particle") -> dict:
    """Selects a particle system or FXList as a click in the editor's library does: the inspector
    shows it, and while an effect plays, the selected one plays instead (with the same settings)."""
    return fx.call("fx.select", kind=kind, name=name)


@mcp.tool()
def fx_stop(action: Literal["stop", "restart", "clear"] = "stop") -> dict:
    """stop: stop playing and remove its particles; restart: start it again; clear: remove all particles."""
    return fx.call("fx.stop", action=action)


@mcp.tool()
def fx_time(paused: bool | None = None, step: int | None = None, speed: float | None = None, wait: bool = True) -> dict:
    """Pauses or resumes the game logic, runs `step` logic frames (30 per second) and pauses again,
    or sets the speed (1 normal, 0.25 slow motion; the view stays smooth). With wait, a step returns
    when the frames have run."""
    result = fx.call("fx.time", paused=paused, step=step, speed=speed)
    if step and wait:
        status = _wait_frame(result["frame"] + step, timeout=max(30.0, step / FRAMES_PER_SECOND * 4 / max(speed or 1.0, 0.05)))
        result["frame"] = status["frame"]
    return result


@mcp.tool()
def fx_stats(systems: bool = True) -> dict:
    """Particle counts (total, maximum, of the preview), live particle systems with template,
    particle count and position, and whether the preview sound plays."""
    return fx.call("fx.stats", systems=systems)


@mcp.tool()
def fx_center(x: float | None = None, y: float | None = None, look: bool = True, angle: float | None = None,
              pitch: float | None = None, zoom: float | None = None) -> dict:
    """Moves the preview point and the camera (angle and pitch in degrees, zoom as the game's)."""
    return fx.call("fx.center", x=x, y=y, look=look, angle=angle, pitch=pitch, zoom=zoom)


@mcp.tool()
def fx_capture(with_interface: bool = False, path: str | None = None, format: Literal["jpg", "png"] = "jpg") -> Image:
    """A picture of the game view (without the editor interface unless asked)."""
    if path is None:
        path = os.path.join(tempfile.gettempdir(), f"fx_mcp_{int(time.time() * 1000)}.{format}")
    result = fx.call("fx.capture", path=path, with_interface=with_interface)
    return Image(path=result["path"])


@mcp.tool()
def fx_capture_sequence(frames: int = 8, interval: int = 5, restart: bool = True, columns: int = 4,
                        width: int = 480, path: str | None = None) -> Image:
    """A contact sheet of the effect over time: pauses, (re)starts what plays, then takes `frames`
    pictures `interval` logic frames apart (30 per second) and lays them out in a grid, each with its
    frame number. Leaves the game paused."""
    from PIL import Image as PILImage, ImageDraw

    fx.call("fx.time", paused=True)
    if restart:
        fx.call("fx.stop", action="restart")
    start = fx.call("fx.status")["frame"]
    shots: list[tuple[int, str]] = []
    tmp = tempfile.mkdtemp(prefix="fx_seq_")
    for i in range(frames):
        if i > 0:
            fx.call("fx.time", step=interval)
            _wait_frame(start + i * interval)
        shot = os.path.join(tmp, f"{i:03d}.png")
        result = fx.call("fx.capture", path=shot)
        shots.append((result["frame"] - start, shot))

    images = []
    for rel, shot in shots:
        img = PILImage.open(shot).convert("RGB")
        img = img.resize((width, int(img.height * width / img.width)))
        ImageDraw.Draw(img).text((6, 4), f"+{rel} f ({rel / FRAMES_PER_SECOND:.2f} s)", fill=(255, 255, 0))
        images.append(img)
    rows = (len(images) + columns - 1) // columns
    h = images[0].height
    sheet = PILImage.new("RGB", (columns * width, rows * h), (0, 0, 0))
    for i, img in enumerate(images):
        sheet.paste(img, ((i % columns) * width, (i // columns) * h))
    if path is None:
        path = os.path.join(tempfile.gettempdir(), f"fx_sheet_{int(time.time() * 1000)}.jpg")
    sheet.save(path, quality=88)
    shutil.rmtree(tmp, ignore_errors=True)
    return Image(path=path)


@mcp.tool()
def fx_command(cmd: str, args: dict[str, Any] | None = None) -> Any:
    """Runs any bridge command of the editor; list_commands lists them (all game.*, view.*, objects.*
    commands of the game bridge work here too)."""
    return fx.call(cmd, **(args or {}))


if __name__ == "__main__":
    mcp.run()
