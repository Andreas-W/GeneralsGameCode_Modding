---
title: "MapData"
description: "Per-map settings."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/MapData.cpp
  - Core/GameEngine/Source/Common/INI/INIMapData.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/WorldHeightMap.cpp
sidebar:
  order: 26
---

A new top-level INI block, `MapData ... End`, for per-map settings. Unlike `GameData.ini` (global engine defaults), a `MapData` block belongs in the map's own `map.ini` (the file next to the `.map` file in the map's folder), so these settings apply only to that map. All values are reset to their defaults when a game ends. WorldBuilder reads only the `MapData` block (and a few rendering blocks) from a map's `map.ini`.

```ini
MapData
  HeightMapScale = 1.0
  EnableShips = No

  TerrainHeightAmbientLightStart = -1
  TerrainHeightAmbientLightHeight1 = -1
  TerrainHeightAmbientLightHeight2 = -1
  TerrainHeightAmbientLightColor1 = R:0 G:0 B:0
  TerrainHeightAmbientLightColor2 = R:0 G:0 B:0
  TerrainHeightAmbientLightAdditive = No
End
```

## Fields

* `HeightMapScale = 1.0` - (The map's raw terrain heights are multiplied by this factor when the map is loaded, so terrain can be higher (or have finer steps) than the vanilla 0..255 range allows. WorldBuilder uses it too and snaps edited heights to multiples of it. Default = 1.0)
* `EnableShips = No` - (Marks the map as a map for ships. Every player additionally gets the sciences in their PlayerTemplate's `IntrinsicSciencesShipMap`, so shipyards and naval content can require a science that land maps never grant. The skirmish AI script condition "ships enabled" (`SKIRMISH_SHIPS_ENABLED`) is true. Default = No)

## Water Depth Terrain Lighting

Tints terrain ambient lighting based on terrain height. The main purpose is to darken terrain the deeper it is below the water surface, to create an underwater depth effect.

* `TerrainHeightAmbientLightStart = -1` - (Terrain height (world units) at which the effect begins. Above this height no tint is applied. The effect is only active when this is greater than 0. Default = -1 = disabled)
* `TerrainHeightAmbientLightHeight1 = -1` - (Height at which `TerrainHeightAmbientLightColor1` is fully reached)
* `TerrainHeightAmbientLightHeight2 = -1` - (Height at which `TerrainHeightAmbientLightColor2` is fully reached)
* `TerrainHeightAmbientLightColor1 = R:0 G:0 B:0` - (Tint color reached at Height1. Components 0..255.)
* `TerrainHeightAmbientLightColor2 = R:0 G:0 B:0` - (Tint color reached at Height2. Components 0..255.)
* `TerrainHeightAmbientLightAdditive = No` - (If No, the terrain lighting is multiplied by the color: `R:255 G:255 B:255` leaves it unchanged, `R:0 G:0 B:0` makes it black. If Yes, the color is added to the lighting instead: `R:0 G:0 B:0` changes nothing.)

Notes:
* The tint blends from no effect at `...Start`, towards Color1 at `...Height1`, and towards Color2 at `...Height2`, allowing a two-step gradient (e.g. shallow water vs. deep water). Below Height2 Color2 is used.
* At least one of `...Height1` / `...Height2` must be set (0 or above). With only one of them above 0, a single step is used: from `...Start` to that height with that height's color.
* The whole feature is disabled by default (start height = -1).
* Under Direct3D 12 the effect is skipped where the water material handles the depth itself (`ReplaceHeightAmbientLight` in `WaterRendering`, see [Direct3D 12 water](/GeneralsGameCode_Modding/rendering/directx12/#which-water-gets-which-material-waterrendering)).
* Known issue: in the single-step case, terrain below that height gets an undefined color (an engine bug); set both heights to be safe.
* This used to be part of `GameData.ini` (marked work-in-progress); it has since been moved to the per-map `MapData` block and the earlier issues are resolved.
