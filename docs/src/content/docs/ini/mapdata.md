---
title: "MapData"
description: "Per-map settings."
status: unverified
sidebar:
  order: 26
---

A new top-level INI block, `MapData ... End`, for per-map settings. Unlike `GameData.ini` (global engine defaults), a `MapData` block is meant to live in a map's own `.ini` file (alongside the map's `Map.ini`/script data), so these settings apply only to that map.

```ini
MapData
  HeightMapScale = 1.0
  EnableShips = Yes

  TerrainHeightAmbientLightStart = -1
  TerrainHeightAmbientLightHeight1 = -1
  TerrainHeightAmbientLightHeight2 = -1
  TerrainHeightAmbientLightColor1 = R:0 G:0 B:0
  TerrainHeightAmbientLightColor2 = R:0 G:0 B:0
  TerrainHeightAmbientLightAdditive = No
End
```

## Fields

* `HeightMapScale = 1.0` - (Vertical scale factor applied to the map's heightmap.)
* `EnableShips = Yes` - (Whether naval/ship units are permitted to be used on this map.)

## Water Depth Terrain Lighting

Tints terrain ambient lighting based on terrain height. The main purpose is to darken terrain the deeper it is below the water surface, to create an underwater depth effect.

* `TerrainHeightAmbientLightStart = -1` - (Terrain height at which the effect begins. Above this height no tint is applied. Default = -1 = disabled)
* `TerrainHeightAmbientLightHeight1 = -1` - (Height at which `TerrainHeightAmbientLightColor1` is fully reached)
* `TerrainHeightAmbientLightHeight2 = -1` - (Height at which `TerrainHeightAmbientLightColor2` is fully reached)
* `TerrainHeightAmbientLightColor1 = R:0 G:0 B:0` - (Tint color reached at Height1)
* `TerrainHeightAmbientLightColor2 = R:0 G:0 B:0` - (Tint color reached at Height2)
* `TerrainHeightAmbientLightAdditive = No` - (If Yes, the colors are added to the ambient light instead of blended/multiplied)

Notes:
* The tint blends from no effect at `...Start`, towards Color1 at `...Height1`, and towards Color2 at `...Height2`, allowing a two-step gradient (e.g. shallow water vs. deep water).
* The whole feature is disabled by default (start height = -1).
* This used to be part of `GameData.ini` (marked work-in-progress); it has since been moved to the per-map `MapData` block and the earlier issues are resolved.
