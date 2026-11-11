"""End-to-end check of the FX Editor's MCP bridge, without an MCP client.

Starts FXEditorZH.exe from the mod folder, waits for the preview game, then lists, plays, edits,
undoes, validates and captures particle systems, FXLists and a sound.

    python fx_smoke_test.py [--game-dir <mod folder, default $GAME_DIR>] [--exe FXEditorZH.exe]
                            [--dx12] [--map <multiplayer map>] [--save]

--save also tests saving: it adds a copy of a particle system to its INI file, checks the file and
then puts the file back as it was (and removes the .fxeditor.bak the test made).
The bridge serves one client, so run it while no MCP client is connected to the editor.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from wb_bridge import WorldBuilderBridge, WorldBuilderError

PORT = 47802
REPO_MAP = Path(__file__).resolve().parents[2] / "GeneralsMD" / "Code" / "Tools" / "FXEditor" / "Data" / "Maps" / "FXEditorPreview"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"  ok  {message}")


def wait_ready(bridge: WorldBuilderBridge, process: subprocess.Popen, timeout: float = 240) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"the editor exited with code {process.returncode}")
        try:
            status = bridge.call("fx.status")
            if status.get("start_error"):
                raise AssertionError(status["start_error"])
            if status["ready"] and status["ini_loaded"]:
                return status
        except (WorldBuilderError, OSError):
            bridge.close()
        time.sleep(0.5)
    raise AssertionError("the editor was not ready in time")


def wait_map(bridge: WorldBuilderBridge, process: subprocess.Popen, key: str, timeout: float = 120) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"the editor exited with code {process.returncode} while loading {key}")
        time.sleep(0.3)
        status = bridge.call("fx.status")
        if status.get("map_key") == key and not status.get("map_loading"):
            return status
    raise AssertionError(f"{key} was not ready in time")


def step(bridge: WorldBuilderBridge, frames: int) -> dict:
    start = bridge.call("fx.time", step=frames)["frame"]
    deadline = time.time() + 60
    while time.time() < deadline:
        status = bridge.call("fx.status")
        if status["frame"] >= start + frames and status["paused"]:
            return status
        time.sleep(0.03)
    raise AssertionError(f"did not reach frame {start + frames}")


def install_map() -> None:
    target = Path.home() / "Documents" / "Command and Conquer Generals Zero Hour Data" / "Maps" / "FXEditorPreview"
    if not REPO_MAP.exists():
        return
    changed = [f for f in REPO_MAP.iterdir()
               if not (target / f.name).exists() or (target / f.name).read_bytes() != f.read_bytes()]
    if not changed:
        return
    target.mkdir(parents=True, exist_ok=True)
    for f in changed:
        (target / f.name).write_bytes(f.read_bytes())
    print(f"installed the preview map into {target}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-dir", default=os.environ.get("GAME_DIR", ""))
    parser.add_argument("--exe", default="FXEditorZH.exe")
    parser.add_argument("--dx12", action="store_true")
    parser.add_argument("--map")
    parser.add_argument("--save", action="store_true")
    args = parser.parse_args()
    if not args.game_dir:
        print("set GAME_DIR or pass --game-dir")
        return 2

    if not args.map:
        install_map()
    cmd = [os.path.join(args.game_dir, args.exe)]
    if args.dx12:
        cmd.append("-dx12")
    if args.map:
        cmd += ["-map", args.map]
    process = subprocess.Popen(cmd, cwd=args.game_dir)
    bridge = WorldBuilderBridge(label="the FX Editor", env_prefix="FX_MCP", default_port=PORT)
    try:
        print("status")
        status = wait_ready(bridge, process)
        check(status["renderer"] == ("dx12" if args.dx12 else "dx8"), f"renderer {status['renderer']}")
        check(status["blocks"] > 0, f"{status['blocks']} INI blocks")

        print("lists")
        particles = bridge.call("fx.list", kind="particle", limit=5000)
        editable = [i["name"] for i in particles["items"] if i.get("has_text")]
        check(len(editable) > 0, f"{particles['total']} particle systems, {len(editable)} with INI text")
        fxlists = bridge.call("fx.list", kind="fxlist", limit=5000)
        check(fxlists["total"] > 0, f"{fxlists['total']} FXLists")
        sounds = bridge.call("fx.list", kind="sound", limit=5000)
        check(sounds["total"] > 0, f"{sounds['total']} sounds")
        schema = bridge.call("fx.schema", kind="particle")
        check(any(f["name"] == "Lifetime" for f in schema["fields"]), "particle schema has Lifetime")
        nuggets = bridge.call("fx.schema", kind="fxlist")["nuggets"]
        check("ParticleSystem" in nuggets and "Sound" in nuggets, "FXList schema has nugget types")

        print("play a particle system")
        # One that emits steadily (no one-shot, lives a while) makes the counts reliable.
        name = None
        for candidate in editable:
            fields = bridge.call("fx.get", kind="particle", name=candidate, text=False)["fields"]
            if fields.get("SystemLifetime", "0").strip() == "0" and fields.get("IsOneShot", "No").lower() != "yes" \
                    and fields.get("Type", "PARTICLE").upper() == "PARTICLE":
                name = candidate
                break
        check(name is not None, f"found a steady system: {name}")
        bridge.call("fx.time", paused=True)
        bridge.call("fx.play", kind="particle", name=name, repeat=False, move=False, height=10)
        step(bridge, 30)
        stats = bridge.call("fx.stats")
        check(stats["preview_systems"] >= 1, f"{stats['preview_systems']} preview systems, {stats['preview_particles']} particles")

        print("follow the camera")
        center = bridge.call("fx.center", look=True)["center"]
        bridge.call("view.camera", x=center["x"] + 80, y=center["y"] + 40)
        step(bridge, 2)
        still = bridge.call("fx.center", look=False)["center"]
        check(abs(still["x"] - center["x"]) < 2 and abs(still["y"] - center["y"]) < 2,
              "what runs stays when the camera moves")
        bridge.call("fx.stop", action="restart")
        moved = bridge.call("fx.center", look=False)["center"]
        check(abs(moved["x"] - center["x"] - 80) < 2 and abs(moved["y"] - center["y"] - 40) < 2,
              f"a restart starts at the camera ({center['x']:.0f},{center['y']:.0f} -> {moved['x']:.0f},{moved['y']:.0f})")
        step(bridge, 15)
        check(bridge.call("fx.stats", systems=False)["preview_particles"] > 0, "emitting at the new point")
        bridge.call("fx.center", x=center["x"], y=center["y"], look=True)

        print("edit, undo")
        before = bridge.call("fx.get", kind="particle", name=name)
        edited = bridge.call("fx.set_fields", kind="particle", name=name, fields={"BurstCount": "40 40", "Lifetime": "60 60"})
        check(edited["dirty"] and edited["fields"]["BurstCount"] == "40 40", "BurstCount set")
        step(bridge, 30)
        stats2 = bridge.call("fx.stats", systems=False)
        check(stats2["preview_particles"] > stats["preview_particles"],
              f"more particles after the edit ({stats['preview_particles']} -> {stats2['preview_particles']})")
        try:
            bridge.call("fx.set_fields", kind="particle", name=name, fields={"Shader": "NOT_A_SHADER"})
            check(False, "a bad value is refused")
        except WorldBuilderError as exc:
            check(True, f"a bad value is refused: {str(exc)[:80]}")
        reverted = bridge.call("fx.undo", kind="particle", name=name, action="revert")
        check(not reverted["dirty"], "reverted to the text on disk")
        check(bridge.call("fx.get", kind="particle", name=name)["text"] == before["text"], "text is as before")

        print("validate, capture")
        check("problems" in bridge.call("fx.validate", kind="particle", name=name), "validate answers")
        shot = os.path.join(tempfile.gettempdir(), "fx_smoke.png")
        if os.path.exists(shot):
            os.remove(shot)
        bridge.call("fx.capture", path=shot)
        check(os.path.getsize(shot) > 1000, f"capture written ({os.path.getsize(shot)} bytes)")
        bridge.call("fx.capture", path=shot, with_interface=True)
        check(os.path.getsize(shot) > 1000, "capture with the interface written")

        print("FXList with nuggets")
        fx_name = next(i["name"] for i in fxlists["items"] if i.get("has_text"))
        fx_block = bridge.call("fx.get", kind="fxlist", name=fx_name)
        count = len(fx_block["nuggets"])
        added = bridge.call("fx.nugget", kind="fxlist", name=fx_name, action="add", type="LightPulse",
                            fields={"Color": "R:255 G:128 B:0", "Radius": 30, "IncreaseTime": "0", "DecreaseTime": "500"})
        check(len(added["nuggets"]) == count + 1 and added["nuggets"][-1]["type"] == "LightPulse", "nugget added")
        moved = bridge.call("fx.nugget", kind="fxlist", name=fx_name, action="move", index=count, offset=-1) if count > 0 else added
        check(len(moved["nuggets"]) == count + 1, "nugget moved")
        bridge.call("fx.play", kind="fxlist", name=fx_name)
        step(bridge, 10)
        selected = bridge.call("fx.select", kind="particle", name=name)
        check(selected.get("playing", {}).get("name") == name, "selecting while playing plays the selection")
        bridge.call("fx.select", kind="fxlist", name=fx_name)
        bridge.call("fx.undo", kind="fxlist", name=fx_name, action="revert")
        check(len(bridge.call("fx.get", kind="fxlist", name=fx_name)["nuggets"]) == count, "FXList reverted")

        print("sound")
        bridge.call("fx.time", paused=False)
        sound = next((s["name"] for s in sounds["items"] if "explosion" in s["name"].lower()), sounds["items"][0]["name"])
        bridge.call("fx.play", kind="sound", name=sound, repeat=False)
        time.sleep(0.3)
        check(bridge.call("fx.stats", systems=False)["sound_playing"], f"sound {sound} plays")

        print("create")
        copy_name = f"FXEditorSmokeTest{os.getpid()}"
        created = bridge.call("fx.create", kind="particle", name=copy_name, copy_from=name)
        check(created["dirty"] and created["name"] == copy_name, "copy created")
        bridge.call("fx.play", kind="particle", name=copy_name)

        print("render blocks")
        render = bridge.call("fx.render_status")
        check(len(render["blocks"]) > 0 or not args.dx12, f"{len(render['blocks'])} render blocks")
        if args.dx12 and render.get("postprocess", {}).get("running"):
            check(render["postprocess"]["error"] == "", f"post-processing runs: {' '.join(render['postprocess']['passes'])}")
            grade = next((b for b in render["blocks"] if b["type"] == "PostProcessPass" and b["name"] in render["postprocess"]["passes"]), None)
            before_shot = os.path.join(tempfile.gettempdir(), "fx_smoke_pp1.png")
            after_shot = os.path.join(tempfile.gettempdir(), "fx_smoke_pp2.png")
            bridge.call("fx.capture", path=before_shot)
            changed = bridge.call("fx.render_set", type="PostProcessPass", name=grade["name"], params={"FXEditorSmokeTest": [1, 2]})
            check(changed["params"].get("FXEditorSmokeTest") == "1 2" and changed["dirty"], f"param added to {grade['name']}")
            try:
                bridge.call("fx.render_set", type="PostProcessPass", name=grade["name"], fields={"Scale": "notanumber"})
                check(False, "a bad value is refused")
            except WorldBuilderError as exc:
                check(True, f"a bad value is refused: {str(exc)[:70]}")
            bridge.call("fx.render_undo", type="PostProcessPass", name=grade["name"], action="revert")
            check(not bridge.call("fx.render_get", type="PostProcessPass", name=grade["name"], text=False)["dirty"], "reverted")
            # Turning post-processing off changes the picture.
            bridge.call("fx.render_status", postprocess=False)
            step(bridge, 2)
            bridge.call("fx.capture", path=after_shot)
            bridge.call("fx.render_status", postprocess=True)
            check(open(before_shot, "rb").read() != open(after_shot, "rb").read(), "post-processing off changes the picture")
            check(bridge.call("fx.render_status")["postprocess"]["running"], "post-processing runs again")

            print("pass chain")
            chain = bridge.call("fx.render_chain")
            rows = chain["rows"]
            names = [r["name"] for r in rows]
            check(len(rows) > 0 and chain["order_error"] == "", f"{len(rows)} passes, custom order {chain['custom_order']}")
            # A pass another pass reads: switched off, the chain still runs and the pass keeps its place.
            read = next((r for r in rows if r["enabled"] and any(r["name"] in o["reads"] for o in rows if o["enabled"])), None)
            if read:
                place = names.index(read["name"])
                chain = bridge.call("fx.render_chain", enable={read["name"]: False})
                step(bridge, 2)
                status = bridge.call("fx.render_status")["postprocess"]
                check(status["running"] and status["error"] == "" and read["name"] not in status["passes"],
                      f"{read['name']} off, the chain runs: {status['error'] or 'ok'}")
                check([r["name"] for r in chain["rows"]].index(read["name"]) == place, f"{read['name']} keeps its place when off")
                # What only fed the switched off pass's branch (the blurs of the occlusion) does not run.
                unused = [r["name"] for r in bridge.call("fx.render_chain")["rows"] if r["enabled"] and r.get("runs") is False]
                check(all(n not in status["passes"] for n in unused), f"unused while {read['name']} is off: {' '.join(unused) or 'none'}")
                chain = bridge.call("fx.render_chain", enable={read["name"]: True})
                row = chain["rows"][place]
                check(row["name"] == read["name"] and row["enabled"], f"{read['name']} on again at its place")
            # Moving a pass before a pass it reads is refused.
            reader = next((r for r in rows if r["reads"]), None)
            if reader:
                try:
                    bridge.call("fx.render_chain", move={"pass": reader["reads"][0], "after": reader["name"]})
                    check(False, "a move before a pass it reads is refused")
                except WorldBuilderError as exc:
                    check(True, f"a move before a pass it reads is refused: {str(exc)[:70]}")
            # A valid swap of two neighbours makes a custom order; reset brings the default back.
            rows = bridge.call("fx.render_chain")["rows"]
            pair = next((i for i in range(len(rows) - 1) if rows[i]["name"] not in rows[i + 1]["reads"]
                         and rows[i]["default_index"] >= 0 and rows[i + 1]["default_index"] >= 0), None)
            if pair is not None:
                chain = bridge.call("fx.render_chain", move={"pass": rows[pair + 1]["name"], "before": rows[pair]["name"]})
                check(chain["custom_order"] and chain["moved_count"] == 1, f"swapped {rows[pair]['name']} / {rows[pair + 1]['name']}: custom order")
                chain = bridge.call("fx.render_chain", reset=True)
                check(not chain["custom_order"], "reset to the default order")
            # Auto inputs: every pass gets the inputs of its place.
            rewired = [r["name"] for r in bridge.call("fx.render_chain")["rows"] if "auto_inputs" in r]
            chain = bridge.call("fx.render_chain", connect=True)
            check(chain["connect_count"] == 0, f"connect inputs: {' '.join(rewired) or 'nothing to change'}")
            rows = chain["rows"]
            image = next((i for i in range(len(rows) - 1) if all(r["role"] == "the image chain" and r["shader"] != "PP_ColorGrade"
                          and r["default_index"] >= 0 for r in (rows[i], rows[i + 1]))), None)
            if image is not None:
                a, b = rows[image], rows[image + 1]
                try:
                    bridge.call("fx.render_chain", move={"pass": b["name"], "before": a["name"]})
                    check(False, f"without auto inputs {b['name']} cannot run before {a['name']}")
                except WorldBuilderError:
                    check(True, f"without auto inputs {b['name']} cannot run before {a['name']}")
                chain = bridge.call("fx.render_chain", move={"pass": b["name"], "before": a["name"]}, auto_inputs=True)
                moved = {r["name"]: r for r in chain["rows"]}
                check(moved[b["name"]]["inputs"][0] == a["inputs"][0] and moved[a["name"]]["inputs"][0] == b["name"]
                      and chain["connect_count"] == 0 and chain["custom_order"],
                      f"auto inputs: {b['name']} before {a['name']}, the image chain follows")
                chain = bridge.call("fx.render_chain", reset=True)
                moved = {r["name"]: r for r in chain["rows"]}
                check(not chain["custom_order"] and moved[b["name"]]["inputs"][0] == a["name"], "reset rewires the image chain")
            grade = next((i for i, r in enumerate(rows) if r["shader"] == "PP_ColorGrade"), None)
            branch = next((r for r in rows if r["role"] in ("the bloom branch", "the occlusion branch")), None)
            if grade is not None and branch:
                try:
                    bridge.call("fx.render_chain", move={"pass": branch["name"], "after": rows[grade]["name"]}, auto_inputs=True)
                    check(False, f"{branch['name']} cannot move behind {rows[grade]['name']}")
                except WorldBuilderError as exc:
                    check(True, f"{branch['name']} cannot move behind {rows[grade]['name']}: {str(exc)[:70]}")
            for r in bridge.call("fx.render_chain")["rows"]:
                if r["shader"] and bridge.call("fx.render_get", type="PostProcessPass", name=r["name"], text=False)["dirty"]:
                    bridge.call("fx.render_undo", type="PostProcessPass", name=r["name"], action="revert")
            bridge.call("fx.render_undo", type="PostProcess", action="revert")
            check(not bridge.call("fx.render_get", type="PostProcess", text=False)["dirty"], "chain reverted")
        if args.dx12 and any(b["type"] == "ShadowMap" for b in render["blocks"]):
            shadow = bridge.call("fx.render_set", type="ShadowMap", fields={"Darkness": 0.5})
            check(shadow["fields"].get("Darkness") == "0.5", "ShadowMap Darkness set")
            bridge.call("fx.render_undo", type="ShadowMap", action="revert")
        material = next((b for b in render["blocks"] if b["type"] == "Material"), None)
        if args.dx12 and material:
            mat = bridge.call("fx.render_set", type="Material", name=material["name"], params={"FXEditorSmokeTest": 0.5})
            check(mat["params"].get("FXEditorSmokeTest") == "0.5", f"material {material['name']} param set")
            bridge.call("fx.render_undo", type="Material", name=material["name"], action="revert")
            check(bridge.call("fx.render_status")["unsaved"] == 0, "nothing left to save")

        print("maps")
        maps = bridge.call("fx.maps")
        check(len(maps["factions"]) > 0, f"{len(maps['maps'])} maps, factions {', '.join(maps['factions'][:4])} ...")
        skirmish_map = next((m for m in maps["maps"] if m["start_positions"] >= 2 and m["official"]), None)
        bridge.call("fx.play", kind="particle", name=name, repeat=False)
        if skirmish_map:
            faction = maps["factions"][-1]
            loading = bridge.call("fx.load_map", map=skirmish_map["key"], faction=faction)
            status = wait_map(bridge, process, loading["map_key"])
            check(status["faction"] == faction and loading["mode"] == "skirmish", f"loaded {skirmish_map['name']} as {faction}")
            step(bridge, 2)  # destroyed objects go at the end of a frame
            local = bridge.call("objects.list", player="local")["objects"]
            check(len(local) == 0, "the starting units are gone")
            check(status.get("playing", {}).get("name") == name, "what played plays again")
        loading = bridge.call("fx.load_map", map="FXEditorPreview")
        status = wait_map(bridge, process, loading["map_key"])
        check(loading["mode"] == "single_player" and status["faction"] == "", "back on the preview map")
        try:
            bridge.call("fx.load_map", map="NoSuchMapAnywhere")
            check(False, "an unknown map is refused")
        except WorldBuilderError:
            check(True, "an unknown map is refused")

        if args.save:
            print("save")
            source = Path(args.game_dir) / bridge.call("fx.get", kind="particle", name=name, text=False)["file"]
            original = source.read_bytes()
            backup = Path(str(source) + ".fxeditor.bak")
            had_backup = backup.exists()
            try:
                written = bridge.call("fx.save", overwrite=True)["written"]
                check(len(written) >= 1, f"saved {written}")
                text = source.read_bytes()
                check(copy_name.encode() in text and text.startswith(original[:200]), "the copy is in the file, the rest unchanged")
                check(text.count(b"\n") >= original.count(b"\n"), "no lines lost")
            finally:
                source.write_bytes(original)
                if not had_backup and backup.exists():
                    backup.unlink()
                print("  put the file back")

        print("all fine")
        return 0
    finally:
        try:
            bridge.call("game.quit")
        except (WorldBuilderError, OSError):
            pass
        bridge.close()
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    sys.exit(main())
