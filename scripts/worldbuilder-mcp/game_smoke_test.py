"""End-to-end check of the game's MCP bridge, without an MCP client.

Starts generalszh.exe headless in a sandbox (only the local player), paused at the start, and goes
through every command area: status, stepping, objects, players, orders, test changes, scripts.

    python game_smoke_test.py [--game-dir F:\\Operation-Firestorm-2.0] [--exe generalszh.exe]
                              [--map "armed neighbors"] [--faction FactionAmerica] [--windowed]

The bridge serves one client, so run it while no MCP client is connected to the game.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time

from wb_bridge import WorldBuilderBridge, WorldBuilderError

PORT = 47801


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"  ok  {message}")


def wait_for(bridge: WorldBuilderBridge, process: subprocess.Popen, timeout: float = 240) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"the game exited with code {process.returncode}")
        try:
            status = bridge.call("game.status")
            if status.get("start_error"):
                raise AssertionError(status["start_error"])
            if status["in_game"] and status["paused"]:
                return status
        except (WorldBuilderError, OSError):
            bridge.close()
        time.sleep(0.5)
    raise AssertionError("the game did not start in time")


def step(bridge: WorldBuilderBridge, frames: int) -> dict:
    target = bridge.call("game.step", frames=frames)["pause_at_frame"]
    deadline = time.time() + 120
    while time.time() < deadline:
        status = bridge.call("game.status")
        if status["frame"] >= target and status["paused"]:
            return status
        time.sleep(0.05)
    raise AssertionError(f"did not reach frame {target}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-dir", default=os.environ.get("GAME_DIR", r"F:\Operation-Firestorm-2.0"))
    parser.add_argument("--exe", default=os.environ.get("GAME_EXE", "generalszh.exe"))
    parser.add_argument("--map", default="armed neighbors")
    parser.add_argument("--faction", default="FactionAmerica")
    parser.add_argument("--unit", default="AmericaVehicleHumvee")
    parser.add_argument("--windowed", action="store_true")
    options = parser.parse_args()

    screen = ["-win", "-xres", "1024", "-yres", "768", "-noaudio"] if options.windowed else ["-headless"]
    args = [os.path.join(options.game_dir, options.exe), "-mcp", "-mcpGame", options.map, "-mcpPlayers", options.faction,
            "-mcpStartPaused", "-seed", "1", "-quickstart", "-ignoreAsserts"] + screen
    process = subprocess.Popen(args, cwd=options.game_dir)
    bridge = WorldBuilderBridge(label="the game", env_prefix="GAME_MCP", default_port=PORT)
    try:
        print("start")
        status = wait_for(bridge, process)
        check(status["sandbox"], "sandbox without opponents")
        check(status["paused"] and status["frame"] <= 2, f"paused at the start (frame {status['frame']})")
        names = {c["name"] for c in bridge.call("list_commands")["commands"]}
        check({"game.step", "objects.get", "orders.move", "objects.spawn", "scripts.flags"} <= names, "commands registered")

        print("time")
        frame = status["frame"]
        status = step(bridge, 30)
        check(status["frame"] == frame + 30 and status["paused"], "step runs 30 frames and pauses again")

        print("players")
        players = bridge.call("players.list")["players"]
        local = [p for p in players if p["local"]]
        check(len(local) == 1 and local[0]["faction"] == options.faction, f"local player plays {options.faction}")
        me = bridge.call("players.get")
        check(me["money"] >= 0 and "sciences" in me and "objects" in me, "players.get describes the local player")

        print("objects")
        mine = bridge.call("objects.list", player="local")
        check(mine["matched"] > 0, f"the local player starts with objects ({mine['matched']})")
        detail = bridge.call("objects.get", ids=[mine["objects"][0]["id"]])["objects"][0]
        check("status" in detail and "modules" in detail, f"objects.get describes {detail['template']}")
        templates = bridge.call("objects.templates", contains=options.unit)["templates"]
        check(any(t["name"] == options.unit for t in templates), f"template {options.unit} exists")

        print("test changes")
        spawned = bridge.call("objects.spawn", template=options.unit, x=500, y=500, count=2)["ids"]
        check(len(spawned) == 2, "two units spawned")
        unit = bridge.call("objects.get", ids=[spawned[0]], sections=["ai", "weapons"])["objects"][0]
        check(abs(unit["position"]["x"] - 500) < 40, "spawned near the position")
        bridge.call("objects.set", ids=[spawned[0]], health_percent=50)
        unit = bridge.call("objects.get", ids=[spawned[0]], sections=[])["objects"][0]
        check(abs(unit["health"] - unit["max_health"] / 2) < 1, "health set to half")
        before = bridge.call("players.get")["money"]
        bridge.call("players.set", add_money=1000)
        check(bridge.call("players.get")["money"] == before + 1000, "money added")

        print("orders")
        bridge.call("orders.move", ids=spawned, x=800, y=500)
        step(bridge, 90)
        moved = bridge.call("objects.get", ids=[spawned[0]], sections=["ai"])["objects"][0]
        check(moved["position"]["x"] > unit["position"]["x"] + 20, f"unit moved toward the target (x {moved['position']['x']:.0f})")
        bridge.call("orders.stop", ids=spawned)
        step(bridge, 2)

        print("scripts")
        flags = bridge.call("scripts.flags", set={"McpSmokeTestFlag": True}, contains="McpSmokeTest")["flags"]
        check(flags.get("McpSmokeTestFlag") is True, "script flag set")
        counters = bridge.call("scripts.counters", set={"McpSmokeTestCounter": 7}, contains="McpSmokeTest")["counters"]
        check(counters["McpSmokeTestCounter"]["value"] == 7, "script counter set")
        messages = bridge.call("game.messages")
        check("next_serial" in messages, f"game.messages answers ({len(messages['messages'])} messages)")

        if options.windowed:
            print("view")
            bridge.call("view.camera", id=spawned[0])
            step(bridge, 2)
            path = os.path.join(tempfile.gettempdir(), "game_mcp_smoke.jpg")
            bridge.call("view.screenshot", path=path)
            check(os.path.getsize(path) > 1000, f"screenshot written ({path})")

        print("cleanup")
        bridge.call("objects.destroy", ids=spawned, how="remove")
        step(bridge, 2)
        check(bridge.call("objects.list", template=options.unit, player="local")["matched"] == 0, "spawned units removed")
        bridge.call("game.quit")
        bridge.close()
        process.wait(timeout=60)
        check(process.returncode == 0, "the game closed")
        print("all good")
        return 0
    finally:
        bridge.close()
        if process.poll() is None:
            process.kill()


if __name__ == "__main__":
    sys.exit(main())
