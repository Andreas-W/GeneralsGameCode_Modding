# DX12 water: status and how to continue

Hand-over notes for the Direct3D 12 water work on the branch `feat/dx12-water`, written so that it can be picked
up on another machine. As of 2026-11-13. The reference for every parameter and INI field is
[DirectX12.md](DirectX12.md) (sections Water and `WaterRendering`); the open GPU hang is in
[DX12-GPU-Hang-Investigation.md](DX12-GPU-Hang-Investigation.md).

## Goal

New water visuals for the DX12 renderer (`-dx12`, D3D8on12), inspired by Red Alert 3. The DX8 path stays as it is.

- Stage 1 (done): moving wave geometry, normals and specular, refraction, depth-based underwater light,
  soft and hard shores with waves that replace the `.wak` tracks, Ocean / Lake / River presets, all configurable
  in INI and overridable in `map.ini`.
- Stage 2 (not started): ship ripples as a client module, a highlight for submarines under water, a water
  displacement FX nugget, more preset polish. Outline below.

## Where things are

| What | Where |
|------|-------|
| Material vertex functions (`MaterialVertex`), extra textures t8..t11, the `View` matrix in b0, half float format | `Core/Libraries/Source/D3D8on12/`: `ShaderGen.cpp`, `Materials.cpp`, `Draw.cpp`, `Gpu.cpp` (root signature), `Pipeline.h`, `Formats.cpp`, `d3d8on12_ext.h` |
| DRED / breadcrumb logging for device removal | `Device.cpp` (`Log_Device_Removed`, `Log_Breadcrumbs`), `Interface.cpp` (env variables) |
| `Material` blocks in `map.ini`, time-of-day `Param*` lines, `WaterRendering` block | `Core/GameEngine/Source/GameClient/PostProcess.cpp`, `Include/GameClient/PostProcess.h`, `Common/INI/INI.cpp` |
| Built-in shaders `MAT_Water`, `MAT_Ocean` (raw string literals `s_waterShader`, `s_oceanShader`), water material binding | `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DMaterials.cpp` |
| Water field texture (terrain height, distance to shore, shore direction; RGBA16F in t8) and shroud binding (t9) | `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterField.cpp` |
| Water drawing: world-aligned grid for moving water, subdivided rivers, per-area material, `.wak` skip | `.../GameClient/Water/W3DWater.cpp` |
| Skipping the `TerrainHeightAmbientLight` bake | `.../GameClient/BaseHeightMap.cpp` |
| Offline shader check | `scripts/d3d8on12/check_material_shaders.py` |

A material can override the built-in shader with `$GAME_DIR\Data\Shaders\Material\MAT_Ocean.hlsl`.

## Branch history

All commits are on `feat/dx12-water`, branched from `main`:

1. Renderer: `cc376d2bb` shared vertex constants + view matrix, `add59c960` material vertex functions,
   `57d515eae` extra textures t8..t11 and RGBA16F.
2. Engine: `9a3cc765b` map.ini materials and time-of-day parameters, `f2c0e01e0` `WaterRendering`,
   `05b364f30` water field, `aaeb4ba9f` per-area materials, `e75aaee60` `MAT_Ocean`, `94f1df7cd` `.wak` and height
   ambient handled by the new water.
3. Fixes after testing: `7da803653` grid gaps, `b7a0e8f4d` refraction doubling, `6b607e946` grid lines in the
   glints and refraction strength.
4. Foam: `9c6112dba` `FoamScale` / `FoamSpeed`, `9f5d5d1a4` crest foam controls, `424e254a4` shore foam separate
   from crest foam (`ShoreFoamWidth`, `ShoreFoamMinimum`, `ShoreFoamWindward`, `CrestFoamScale`, `CrestFoamSpeed`,
   `ExtraMap` as crest pattern, foam visible up to the waterline), `58d3e382b` `ShoreFoamMinimumTexture`,
   `ShoreWaveFoamDepth`.
5. GPU hang diagnostics: `427ff9d7d`, `5eb3d2dc3`, `a8d9b6eec`, `886548f06`, and the write-up `263a7b78d`.

## Mod-side setup (not in this repository)

The presets live in the mod folder (`$GAME_DIR`). On a new machine, copy them over or recreate them from here.
State of the author's mod on 2026-11-13:

`$GAME_DIR\Data\INI\Water.ini`, appended:

```ini
WaterRendering
  OceanMaterial = Ocean           ; standing water reaching the edge of the playable area
  LakeMaterial = Lake             ; any other standing water
  RiverMaterial = River           ; rivers
  ; PolygonMaterial = Lake <water area name>   ; one water area by its name, before the kinds above
  GridSpacing = 10                ; world units between the vertices of moving water
  RiverSubdivisions = 4           ; river quads are cut into 4 x 4 pieces
  Refraction = Yes                ; see through the water (copies the scene before the water)
  DrawWaterTracks = No            ; also the maps' .wak waves (MAT_Ocean makes its own surf)
  ShoreFieldResolution = 2        ; texels per terrain cell of the water field (shore distance)
  ShoreFieldRange = 100           ; largest distance to the shore it holds, world units
  ReplaceHeightAmbientLight = Yes ; MAT_Ocean darkens what is under water instead of MapData TerrainHeightAmbientLight
End
```

`$GAME_DIR\Data\INI\Material.ini`, the water materials (the particle materials `SoftSmoke` / `GlowFire` above
them are unrelated):

```ini
Material Lake
  Use = Water
  Shader = MAT_Ocean
  NormalMap = None
  SpecularMap = None
  EmissiveMap = None
  Param = WaveAmplitude 0.4
  Param = WaveLength 60
  Param = WaveChoppiness 0.3
  Param = NormalStrength 0.25
  Param = ShoreWaveHeight -1
  Param = CrestFoam -1
  Param = FoamDistance 1.5
  Param = FoamStrength 0.6
  Param = ShoreFoamWidth 6
  Param = ShoreFoamMinimum 0.3
  Param = Absorption 0.05 0.022 0.016
  Param = ScatterColor 0.05 0.24 0.22
  ParamEvening = ScatterColor 0.05 0.17 0.17
  ParamNight = ScatterColor 0.01 0.04 0.05
  ParamNight = GlintStrength 1
  ParamNight = CausticStrength -1
End

Material River
  Use = Water
  Shader = MAT_Ocean
  NormalMap = None
  SpecularMap = None
  EmissiveMap = None
  Param = WaveAmplitude 0.25
  Param = WaveLength 40
  Param = NormalStrength 0.45
  Param = ShoreWaveHeight -1
  Param = CrestFoam -1
  Param = FoamDistance 1.5
  Param = FoamStrength 0.7
  Param = ShoreHardness 0.5
  Param = ShoreFoamWidth 4
  Param = ShoreFoamMinimum 0.3
  Param = Absorption 0.08 0.05 0.04
  Param = ScatterColor 0.10 0.16 0.10
  Param = CausticStrength 0.3
  ParamNight = ScatterColor 0.02 0.03 0.02
  ParamNight = GlintStrength 1
  ParamNight = CausticStrength -1
End

Material Ocean
  Use = Water
  Shader = MAT_Ocean
  NormalMap = fxwatertile02f_n.tga
  SpecularMap = TWWaterFoam2.tga   ; shore foam pattern (red channel)
  ExtraMap = WaterFoam.tga         ; crest foam pattern (red channel)
  EmissiveMap = None
  Param = WaveAmplitude 3.6
  Param = WaveLength 150
  Param = WaveChoppiness 0.8
  Param = Absorption 0.03 0.01 0.005
  Param = ScatterColor 0.15 0.64 0.8
  ; Shore foam
  Param = FoamDistance 10
  Param = FoamStrength 1.0
  Param = FoamScale 80
  Param = FoamSpeed 2.5
  Param = ShoreFoamWidth 5
  Param = ShoreFoamMinimum 0.8
  Param = ShoreFoamWindward 0.25
  Param = ShoreFoamMinimumTexture 0.66
  Param = ShoreWaveFoamDepth 12
  ; Crest foam
  Param = CrestFoamStart -1.18
  Param = CrestFoamRange 3.06
  Param = CrestFoamSoftness 1.75
  Param = CrestFoam 0.7
  Param = CrestFoamScale 160
  Param = CrestFoamSpeed 2.5
  ParamEvening = ScatterColor 0.06 0.22 0.28
  ParamNight = ScatterColor 0.01 0.05 0.09
  ParamNight = GlintStrength 1.5
  ParamNight = CausticStrength -1
  Param = ShoreDistance 5
  Param = ShoreWaveHeight 0.75
  Param = RefractionStrength 0.5
  Param = UnderwaterAmbient 0.66
  Param = CausticStrength 0.66
  Param = TextureTint 0.50
  Param = DeepOpacity 0.6
  Param = FoamColor 1.2 1.3 1.3
End
```

Textures the Ocean preset uses, all in `$GAME_DIR\Art\Textures` (mod art, not in this repository):
`fxwatertile02f_n.tga` (ripples), `TWWaterFoam2.dds` (shore foam), `WaterFoam.dds` / `.tga` (crest foam; the
author's own). The material looks for `.dds` first, then `.tga`, whatever extension the INI names.

## Building and testing

1. Build from a VS developer shell (`vcvarsall.bat amd64_x86`) with `set VSLANG=1033` (German compiler messages
   break ninja's header dependency scan):
   `cmake --build build/win32 --preset w32-modding-zh --target z_generals`.
   Check the time stamp of `build/win32/GeneralsMD/Release/generalszh.exe` afterwards: a build that silently did not
   run once left an old exe in place.
2. Shader changes: `python scripts/d3d8on12/check_material_shaders.py s_oceanShader` first. It compiles both stages
   with fxc in seconds and warns when a raw string literal piece nears the MSVC limit (`s_oceanShader` is two
   pieces, the second about 13 KB of 16 KB; split again with `)" R"(` when needed).
3. Test without touching the installed exe: copy the build into `$GAME_DIR` under another name and start it with
   the game MCP bridge, e.g.
   `generalszh_test.exe -mcp -mcpGame "Isle Of War Ships" -mcpPlayers FactionChina -quickstart -ignoreAsserts -win -xres 1920 -yres 1080 -noaudio -dx12`
   from `$GAME_DIR`, then use the `mcp__game__*` tools (they connect to any running game with `-mcp`).
   `game_launch(..., extra_args=["-dx12"])` works too, but always starts `generalszh.exe`.
4. Tune live: `render_materials(material="Ocean", param="ShoreFoamMinimum", values=[0.8])` changes a parameter
   without restarting; `render_materials(reload=true)` rereads `Material.ini` and the shader files. Nothing is
   saved; copy good values into `Material.ini`.
5. Look: a beach on Isle Of War Ships is at `view_screenshot(x=1950, y=4620, zoom=0.7)`; that map is in evening
   light, so foam looks darker than at noon.
6. Ask before replacing the user's `generalszh.exe`; keep a backup (`generalszh.exe.bak` is the build from before
   the water work).

## Things to know

- `Param` values of 0 mean the shader's default; −1 turns off the features marked so in DirectX12.md.
- With `Refraction = Yes` (the default) `MAT_Ocean` ignores `DeepColor`, `DepthScale` and `DeepOpacity`;
  `ScatterColor` and `Absorption` do their job.
- `FoamStrength`, `FoamScale`, `FoamSpeed`, `FoamDistance -1` concern the shore foam only; the crest foam has
  `CrestFoam`, `CrestFoamScale`, `CrestFoamSpeed` and its own pattern in `ExtraMap`. Rivers cannot use `ExtraMap`
  (the engine puts the river edge alpha there), so their crests use `SpecularMap`.
- The shore foam band is the larger of the depth band (`FoamDistance`) and the distance band (`ShoreFoamWidth`,
  from the water field). The minimum line (`ShoreFoamMinimum`, patterned by `ShoreFoamMinimumTexture`) covers that
  band; the foam on the shore waves reaches `ShoreWaveFoamDepth`, as far out as `ShoreWaveRange`.
- Foam is no longer faded out by `ShoreDistance`, only over the last 0.3 units of depth, so it marks the waterline
  on soft edges.
- Adding a sixth material texture slot would mean changing the D3D8on12 root signature; avoid it while the GPU hang
  is open.

## Open items

1. **GPU hang (TDR) in DX12**, rare and random, about once per half hour of play on the author's machine (RTX 3080 Ti,
   driver 581.42). Not proven to come from this branch. Paused; everything known is in
   [DX12-GPU-Hang-Investigation.md](DX12-GPU-Hang-Investigation.md). Next steps listed there: long unattended A/B runs,
   a newer NVIDIA driver.
2. "could not create a 64x64 texture of format 26" (A4R4G4B4) in every `d3d8on12.log`; not investigated.
3. The user found `RefractionStrength 1` a bit strong; the Ocean preset uses 0.5.
4. Stage 2 (not started):
   - Ship ripples: revive `ShipEffectsClientUpdate` (a dead stub in
     `Core/GameEngine/Source/GameClient/Drawable/Update/`), register it in `ModuleFactory.cpp`, add it to CMake.
     On DX8 it starts particle systems; on DX12 it sends hull and wake impulses to a GPU ripple simulation
     (heightfield in t11, `WaterDisturbance` in `Material.hlsli` is already declared) that displaces the water.
   - Submarine highlight: an outline or tint for submarines under water (stencil, or a material parameter).
   - FX nugget: `WaterDisturbance` in `FXList.cpp`, plus the FX Editor schema.
   - Preset polish: tuned default Ocean / Lake / River, normal and foam textures.
5. Test helpers left in the author's mod folder (not needed elsewhere): `generalszh_dx12_dred.bat`,
   `generalszh_dx12_prewater.bat`, `generalszh_dx12_oldwater.bat`, `generalszh_dx12water.exe`,
   `generalszh_prewater.exe`, `generalszh_foam.exe`, and INI backups `*.bak-dx12water`, `*.dx12water-current`,
   `*.bak-shorefoam*`.
