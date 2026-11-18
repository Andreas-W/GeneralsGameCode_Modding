# DX12 water: status and how to continue

Hand-over notes for the Direct3D 12 water work on the branch `feat/dx12-water`, written so that it can be picked
up on another machine. As of 2026-11-18. The reference for every parameter and INI field is
[DirectX12.md](DirectX12.md) (sections Water and `WaterRendering`); the open GPU hang is in
[DX12-GPU-Hang-Investigation.md](DX12-GPU-Hang-Investigation.md).

## Goal

New water visuals for the DX12 renderer (`-dx12`, D3D8on12), inspired by Red Alert 3. The DX8 path stays as it is.

- Stage 1 (done): moving wave geometry, normals and specular, refraction, depth-based underwater light,
  soft and hard shores with waves that replace the `.wak` tracks, Ocean / Lake / River presets, all configurable
  in INI and overridable in `map.ini`.
- Stage 2 (done in code, not yet used by the mod's data): contact foam where objects cut the water, a
  disturbance texture for ripples, the `WaterRipple` FX nugget, the `WaterInteractionClientUpdate` module for
  ships and amphibious units with a submarine highlight (also DX8), and the `WaterRippleEffects` option.
  Preset polish is left.

## Where things are

| What | Where |
|------|-------|
| Material vertex functions (`MaterialVertex`), extra textures t8..t12, the `View` matrix in b0, half float format | `Core/Libraries/Source/D3D8on12/`: `ShaderGen.cpp`, `Materials.cpp`, `Draw.cpp`, `Gpu.cpp` (root signature), `Pipeline.h`, `Formats.cpp`, `d3d8on12_ext.h` |
| DRED / breadcrumb logging for device removal | `Device.cpp` (`Log_Device_Removed`, `Log_Breadcrumbs`), `Interface.cpp` (env variables) |
| `Material` blocks in `map.ini`, time-of-day `Param*` lines, `WaterRendering` block | `Core/GameEngine/Source/GameClient/PostProcess.cpp`, `Include/GameClient/PostProcess.h`, `Common/INI/INI.cpp` |
| Built-in shaders `MAT_Water`, `MAT_Ocean` (raw string literals `s_waterShader`, `s_oceanShader`), water material binding | `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DMaterials.cpp` |
| Water field texture (terrain height, distance to shore, shore direction; RGBA16F in t8) and shroud binding (t9) | `Core/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterField.cpp` |
| Water drawing: world-aligned grid for moving water, subdivided rivers, per-area material, `.wak` skip | `.../GameClient/Water/W3DWater.cpp` |
| Skipping the `TerrainHeightAmbientLight` bake | `.../GameClient/BaseHeightMap.cpp` |
| Water disturbance (ripples; RGBA16F render target around the camera in t11, stamps and fade at 30 Hz) | interface `Core/GameEngine/Include/GameClient/WaterDisturbance.h`, DX12 side `.../GameClient/Water/W3DWaterDisturbance.cpp`, updated from `W3DDisplay::draw` |
| `WaterRipple` FX nugget | `Core/GameEngine/Source/GameClient/FXList.cpp`, FX Editor schema `GeneralsMD/Code/Tools/FXEditor/FieldSchema.cpp` |
| `WaterInteractionClientUpdate` (hull, wake, particle systems, submarine highlight) | `Core/GameEngine/{Include/GameClient/Module,Source/GameClient/Drawable/Update}/WaterInteractionClientUpdate.*` |
| Submarine outline after the water | `RTS3DScene::flushUnderwaterHighlights` in `GeneralsMD/.../W3DScene.cpp`, strength on `Drawable::setUnderwaterHighlight` |
| `WaterRippleEffects` option | `OptionPreferences::getWaterRippleEffects`, `GlobalData::m_waterRippleEffects` |
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
5. Glints: `ViewGlint`, `ViewGlintGloss`, `ViewGlintHeight`, a weaker glint light that turns with the camera,
   because the default camera faces north and the sun glints are never seen. W3D view space looks along −z
   (`sign(Projection._34)` gives the forward direction).
6. Deep water contrast: `WaveShading`, `CrestScatter`, `CrestScatterColor` shade the water's own colour by wave
   slope and height, because from the steep camera the reflection hardly changes and deep water looked flat.
7. GPU hang diagnostics: `427ff9d7d`, `5eb3d2dc3`, `a8d9b6eec`, `886548f06`, and the write-up `263a7b78d`.
8. Alpha blended parts under refracting water (`Flush_Submerged`, Tech Oil Rig legs): `d225e77ec`.
9. Presets for maps not made for ships: `MAT_Ocean` is opaque unless `Param = Refraction 1`; the scene copy and
   `Flush_Submerged` only run when a material the map uses refracts. `Material` blocks take `Base = <material>`
   (also `Base` = their own name in a `map.ini`, to change only some parameters). Naval maps pick the see-through
   materials in their `map.ini` (`WaterRendering OceanMaterial = OceanShips`).
10. Less repetitive waves: `NoiseHeight`, `NoiseScale`, `NoiseSpeed`, `NoiseDetail` add fractal noise on top of
   the four Gerstner waves. The noise is a shared texture `NoiseTexture` (t12, the 5th extra material texture), four
   octaves of tileable gradient noise with its slope, made by `W3DMaterials::createNoise` at start (RGBA16F
   256 x 256, mipmaps). `MAT_Ocean` samples it in three layers of different size, direction and drift; vertices
   read coarse mip levels, pixels the level of their size on screen. fxc: +65 pixel instruction slots (3 texture
   reads), +58 vertex; the same in shader maths (simplex, 4 octaves) was +238. A first attempt, `WaveVariation`
   (value noise scaling and bending the Gerstner waves), was dropped: it made their regular pattern stronger.
   Tried on Isle Of War Ships at night only (NoiseHeight 3..12): crests and glints become irregular, crest foam
   thins out (the wave height it reads now includes NoiseHeight). Not checked in daylight; GPU time not measured
   (`render_timing` returned no frames).
11. Opaque water by distance to the shore: `ShoreOpacityWidth` makes opaque `MAT_Ocean` fully clear at the
   shoreline (alpha 0) and deep (`DeepColor`, `DeepOpacity`) that far out, whatever the depth, like the DX8 water
   (alpha = depth / `TransparentWaterDepth` from the waterline, so its edge always was clear; our depth fade starts
   from the WaterSet alpha, 255 in the mod). It reads the shore distance of the water field; 0 keeps the depth
   fade. Seen in the FX Editor on Isle Of War Ships (OceanShips with Refraction 0, width 120); not tuned in the mod yet.
12. Stage 2, water interaction: `9d1868e19` contact foam (`ContactFoam*`), `6e0466a90` water disturbance
   (`Disturbance*` in `WaterRendering` and `MAT_Ocean`, game MCP `render.water_ripple`), `644446763` `WaterRipple`
   FX nugget, `15d29aa43` `WaterInteractionClientUpdate` (replaces the dead `ShipEffectsClientUpdate` stub; also
   fixes `Drawable::findClientUpdateModule`, which looped forever with two client update modules), `bb4c38114`
   flicker fix (the disturbance left `D3DRS_ZENABLE` off on frames the shadow map pass skipped), `842316828`
   submarine highlight, `5c67a3c99` `WaterRippleEffects` option. Tested on a copy of Isle Of War Ships with the
   module added in its `map.ini` (USA Destroyer, China Gattling Boat, China Nuclear Submarine): hull foam and wakes,
   rings, the outline in DX8 and DX12, hidden behind a ship in front, gone when surfaced, none for an enemy sub
   with `ALLIES`, nothing with the option off.

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
  ; Glints from every camera direction
  Param = ViewGlint 0.35
  Param = ViewGlintGloss 800
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

Since 2026-11-14 (item 9) that Ocean is split: `Ocean` keeps the shared lines (maps, wave length and choppiness,
foam, glints, `ScatterColor` / `CrestScatter` / `WaveShading`, `TextureTint`, `FoamColor`) with `WaveAmplitude 1.2`
and `ShoreDistance 1.5` and is opaque; `OceanShips` is `Base = Ocean` plus `Refraction 1`, `WaveAmplitude 3.6`,
`ShoreDistance 5`, `Absorption`, `RefractionStrength`, `UnderwaterAmbient`, `CausticStrength` (night −1). A large
`ShoreDistance` on opaque water makes all water shallower than it see-through (Flash Effect's bays are only a few
units deep). The mod's 11 maps with `EnableShips`
(`$GAME_DIR\Maps\*`) got in their `map.ini`:

```ini
WaterRendering
  OceanMaterial = OceanShips
  LakeMaterial = OceanShips
End
```

Backups: `Material.ini.bak-presets`, `map.ini.bak-presets`. `GameData.ini` sets `RadiusDecalsAboveWater = Yes`.

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
5. Reveal the map first, or water under fog of war looks much darker than it is:
   `game_command("scripts.action", {"name": "MAP_REVEAL_ALL_PERM", "parameters": ["player0"]})`
   (`MAP_REVEAL_ALL` reveals only once; the area falls back to fog).
   Look: a beach on Isle Of War Ships is at `view_screenshot(x=1950, y=4620, zoom=0.7)`; that map is in evening
   light, so foam looks darker than at noon.
6. Ask before replacing the user's `generalszh.exe`; keep a backup (`generalszh.exe.bak` is the build from before
   the water work).

## Things to know

- `Param` values of 0 mean the shader's default; −1 turns off the features marked so in DirectX12.md.
- With `Param = Refraction 1` (and `WaterRendering Refraction = Yes`, the default) `MAT_Ocean` ignores `DeepColor`,
  `DepthScale` and `DeepOpacity`; `ScatterColor` and `Absorption` do their job. Without it (the default) the reverse.
  `render_materials(material="Ocean", param="Refraction", values=[1])` switches it live.
- The terrain bake of `TerrainHeightAmbientLight` is decided when the map loads, from whether its water refracts;
  switching `Refraction` live does not redo it.
- Radius cursors on see-through water need `RadiusDecalsAboveWater = Yes` in `GameData.ini`.
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
4. Stage 2 follow-ups:
   - Mod data: add `WaterInteractionClientUpdate` to the ships, amphibious units and submarines (with
     `SubmergedHighlight`), `WaterRipple` to the FXLists of shells and explosions over water; maybe replace the
     draw modules' wake particle systems with the module's `BowWaveParticleSystem` / `WakeParticleSystem`.
   - An options menu checkbox for `WaterRippleEffects`.
   - Once, in a fight, a surfacing submarine was briefly invisible (not reproduced; the outline switches off when
     its top comes within a unit of the surface).
   - Preset polish: tuned default Ocean / Lake / River, normal and foam textures.
5. Test helpers left in the author's mod folder (not needed elsewhere): `generalszh_dx12_dred.bat`,
   `generalszh_dx12_prewater.bat`, `generalszh_dx12_oldwater.bat`, `generalszh_dx12water.exe`,
   `generalszh_prewater.exe`, `generalszh_foam.exe`, and INI backups `*.bak-dx12water`, `*.dx12water-current`,
   `*.bak-shorefoam*`.
