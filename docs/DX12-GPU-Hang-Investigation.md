# DX12 GPU hang investigation (paused)

Status of the investigation into the Direct3D 12 renderer (D3D8on12, `-dx12`) freezing the game on the
`feat/dx12-water` branch. Paused on 2026-11-12, unresolved. This file records what is known so that it can be
picked up again.

## Symptom

- While playing in DX12, the picture freezes; the game logic keeps running (the MCP bridge still answers,
  `render_fps` is reported as normal). Windows shows a "display driver stopped responding" notification.
- Windows System log: `nvlddmkm` event 153 twice, then `Display` event 4101 (TDR, timeout detection and recovery).
- `d3d8on12.log`: `device removed: 0x887A0006` (`DXGI_ERROR_DEVICE_HUNG`): the GPU took too long, as opposed to
  a page fault or a removed device. DRED reported no page fault in any of the logged freezes.
- System: NVIDIA GeForce RTX 3080 Ti, driver 32.0.15.8142 (581.42, dated 2025-09-22), Windows 10 Pro 19045.
  Game settings during most freezes: 1920x1080 windowed, 4x MSAA, HDR scene (`back buffer drawn in 16 bit float`),
  shadow map 2048x2048.

## Timeline

All driver timeouts (event 4101) on this machine are from 2026-11-12, the day after the water work started
(first water commit `cc376d2bb`, 2026-11-11 20:43). There are none before. The user had played little in DX12
before that, so this alone does not prove the branch is at fault.

| Time | Build | How | Log |
|------|-------|-----|-----|
| 07:42, 07:56, 09:39 | early water builds | normal play, menu skirmish | none kept |
| 12:58, 13:12, 13:31 | water builds | Isle of War Ships, restarts | freeze_1312 (no breadcrumbs yet) |
| 14:33, 14:56, 14:58 | `427ff9d7d` + DRED | user, restarts | freeze_2: op 5 of 349; freeze_3: op 41 of 314 |
| 15:16 | `886548f06` | user, restarts | freeze_4: draw 0 of 258, a shadow map caster |
| 16:52 | `886548f06`, pre-water INI files | user, `/testmode`, restarts | freeze_5: draw 19 of 685, a scene draw |
| 17:12 | `886548f06`, current INI files | automated: save loaded ~21 times through the game MCP | not logged (no DRED) |

The logs were in the session scratchpad (`d3d8on12_freeze_*.log`) and are not in the repository.

## What the breadcrumbs show

With `D3D8ON12_DRED=1` the renderer logs, at device removal, the DRED auto-breadcrumbs and the names of the draws
around where the GPU stopped (see "Tools" below). In every logged freeze:

- Two command lists are unfinished: the one the GPU was in, and the next frame's, with 0 operations done.
- The stop is always early in its command list: operation 5, 41, 5 and 42 of 300 to 1400.
- The draw there is ordinary and cheap: shadow map casters of 6 to 30 indices (fvf 0x112), or a scene mesh of
  2850 indices (fvf 0x152, no culling). All use the generated vertex and pixel shaders, no material.
- The draw differs from freeze to freeze. It is not one particular draw.

Breadcrumb completion is written at the end of the pipeline, so the draw named is the oldest one not finished.
The work actually stuck may be a little later. Even so, the stop is always within the first few dozen operations
of a list.

## Experiments

| Test | Result |
|------|--------|
| Shadow maps off (`PostProcess.ini`), many restarts | no freeze |
| Pre-water build (`generalszh.exe.bak`, built from `main`) with the pre-water INI files, restarts | no freeze in 20+ restarts with the user's `/testmode` method |
| Current build `886548f06` with the pre-water INI files (old `MAT_Water`: no `MAT_Ocean`, no vertex function, no refraction copy) | froze (16:52) |
| Automated: a fresh game started on Isle of War Ships each run (10 runs, `restart_loop.sh`) | no freeze |
| Automated: one game, save loaded again and again through the game MCP (tool calls, a few seconds apart) | froze after about 21 loads (17:12) |
| Automated: same with a script on the bridge, 1, 3 and 6 s between loads, 230 loads (~18 min) | no freeze |
| Debug layer (`D3D8ON12_DEBUG=1`) | no errors before the hang |
| GPU-based validation (`D3D8ON12_GBV=1`) | found one real error (below); after the fix 0 errors, but GBV runs are so slow that they hit the timeout themselves, so they prove nothing about the hang |

Conclusion so far: the freeze is rare and random, roughly once per half hour of play on this machine, and restarts
may not be what triggers it. At that rate, 20 clean restarts of the pre-water build are weak evidence, and so is
"shadows off". **It is not proven that the branch causes the freeze**, although the timeline points that way.

If the branch is at fault, the part left after the INI test is what runs even with the old water material: the
renderer core changes and the water field.

## Fixed along the way

- `886548f06`: GPU-based validation reported a vertex shader reading t8 (root parameter 19) while the texture was
  in `PIXEL_SHADER_RESOURCE` state, which is undefined behaviour. Extra material textures are now moved to
  `PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE` before such draws (`Device::Make_Readable_By_All_Stages`).
  The freeze continued after this fix.
- `5eb3d2dc3`: DRED output was empty on Windows 10 because `ID3D12DeviceRemovedExtendedData2` does not exist
  there; it now falls back to versions 1 and 0.
- `a8d9b6eec`: breadcrumb context strings are not delivered on this system, so each draw's description is kept
  on the CPU and matched to the breadcrumbs by counting markers.

## Changes on the branch that every draw goes through

These were reviewed line by line against `main`, and no bug was found that would explain a hang:

- `cc376d2bb`: `row_major float4x4 View` at the end of the vertex constants b0 (`FFVertexConstants::view`). The
  HLSL layout matches the C++ struct; the light loop of the generated vertex shader is still bounded by
  `LightCount.x` of at most 8.
- `add59c960`: `VertexShaderKey::vertexHook` (also changes the key's padding), the `MATERIALDATA` output of
  generated vertex shaders for material draws, preprocessing of every material source at build time.
- `57d515eae`: root signature: b1 and b2 visible to all stages (were pixel only), 4 more descriptor tables for
  t8 to t11 (visibility all), 4 static samplers s8 to s11 (one a comparison sampler). Root signature version 1.0,
  26 DWORDs. Every table is set before every draw (null SRV for non-material draws, and in
  `PostProcessor::Bind_Root`).
- Extra textures bound for material draws: the water field (RGBA16F, `D3DPOOL_MANAGED`, created and filled with
  `LockRect` at map load and when water or terrain change) and the shroud texture (R5G6B5, managed, rewritten every
  frame). Both are set only while `renderWater` runs and cleared afterwards; their pointers are not reference
  counted.

Also checked and not at fault: upload memory and descriptor reuse are fenced (`Release_Gpu_Texture`, texture pool
keeps the resource state), the shadow map barriers track state, `Copy_Scene` leaves the copy readable, no shader
has a loop whose length comes from data (the `MAT_Ocean` loops are fixed at 24 steps or unrolled), no cross-queue
waits.

## Open leads

1. Measure the real freeze rate with long unattended runs (an hour or more each): the pre-water build, the
   current build, and the current build with the renderer core changes switched off. A test switch could restore
   the `main` root signature (no t8 to t11, b1/b2 pixel only) and skip the extra textures, and another skip the
   water field.
2. Update the NVIDIA driver (581.42 is over a year old). A driver bug triggered by the new root signature layout
   is possible.
3. Find out why the scripted save-load loop never reproduced while the MCP-driven one did. Differences: the game
   had run about 3.5 minutes before the loads; the loads came in irregular bursts; possibly window focus.
4. "could not create a 64x64 texture of format 26" (A4R4G4B4, `DXGI_FORMAT_B4G4R4A4_UNORM` not supported by
   `CreateCommittedResource` here) appears 4 times in every log. Not investigated; probably unrelated, but
   unexplained.

## Tools

Environment variables of the renderer (set them before starting `generalszh.exe -dx12`):

| Variable | Effect |
|----------|--------|
| `D3D8ON12_DRED=1` | auto-breadcrumbs, page fault reporting, one marker per draw with its description; logged at device removal |
| `D3D8ON12_DEBUG=1` | Direct3D 12 debug layer; its messages go to `d3d8on12.log` |
| `D3D8ON12_GBV=1` | GPU-based validation (very slow, can cause timeouts on its own) |
| `D3D8ON12_LOG=1` | write `d3d8on12.log` |

`d3d8on12.log` is written next to the exe and overwritten at each start: copy it before restarting after a freeze.

Helpers left in the mod folder during the investigation (not in the repository):

- `generalszh_dx12_dred.bat`: current exe with `D3D8ON12_DRED=1` and `D3D8ON12_DEBUG=1`, windowed 1920x1080, `-mcp`.
- `generalszh_dx12_prewater.bat`: the pre-water exe (`generalszh.exe.bak`, copied to `generalszh_prewater.exe`) with
  the pre-water INI files (`Data\INI\*.ini.bak-dx12water`); puts the current `Material.ini` and `Water.ini` back when
  the game exits (copies in `*.ini.dx12water-current`).
- `generalszh_dx12_oldwater.bat`: the current exe with the pre-water INI files, same restore.
- `generalszh_dx12water.exe`: copy of build `886548f06`.

Checking for driver timeouts since a point in time (PowerShell):

```powershell
Get-WinEvent -FilterHashtable @{LogName='System'; Id=4101,153; StartTime=(Get-Date).AddMinutes(-30)} | Select TimeCreated, Id
```

The scripted save-load loop (Python, run from the repository root). The game MCP server must not be connected to
the game at the same time, because the bridge serves one client:

```python
import os, subprocess, sys, time
sys.path.insert(0, r"scripts\worldbuilder-mcp")
from wb_bridge import WorldBuilderBridge

GAME_DIR = os.environ["GAME_DIR"]
exe, loads, wait = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
start = time.time()

def timeouts():
    since = int(time.time() - start) + 5
    cmd = (f"(Get-WinEvent -FilterHashtable @{{LogName='System'; Id=4101; StartTime=(Get-Date).AddSeconds(-{since})}}"
           " -ErrorAction SilentlyContinue | Measure-Object).Count")
    return int(subprocess.run(["powershell", "-NoProfile", "-Command", cmd], capture_output=True, text=True).stdout.strip() or 0)

proc = subprocess.Popen([os.path.join(GAME_DIR, exe), "-mcp", "-mcpGame", "IsleOfWar", "-mcpPlayers", "FactionChina",
                         "-quickstart", "-ignoreAsserts", "-win", "-xres", "1920", "-yres", "1080", "-noaudio", "-dx12"],
                        cwd=GAME_DIR)
bridge = WorldBuilderBridge(label="the game", env_prefix="GAME_MCP", default_port=47801, timeout=120.0)
while True:
    try:
        if bridge.call("game.status").get("in_game"):
            break
    except Exception:
        time.sleep(1)
bridge.call("game.save", filename="tdr_loop")
for i in range(1, loads + 1):
    bridge.call("game.load", filename="tdr_loop.sav", paused=False)
    time.sleep(wait)
    if i % 5 == 0 and timeouts():
        print(f"driver timeout by load {i}")
        break
proc.kill()
```
