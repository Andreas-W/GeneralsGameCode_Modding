---
title: "Draw modules"
description: "W3DModelDraw and W3DDecalDraw."
status: verified
verifiedAt: 7cdd826c5
sources:
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DDecalDraw.cpp
  - Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp
sidebar:
  order: 8
---

## W3DModelDraw

Added parameters (To the main Draw module block, not the individual ConditionStates):

* `IgnoreAnimationSpeedScaling = No` - ignore animation scaling (e.g. for PreAttackDelay) for a draw module. Useful for having Looping animations (e.g. rotor blades or flags) along with a pre_attack anim in another draw module.

* `IgnoreRotation = No` - This model will always stay aligned to the world (i.e. Z up), ignoring rotation via movement. Do not use this on modules with Firebones or Turrets.

* `OnlyVisibleToOwningPlayer = No` - This model will only be visible to the owning player. Do not use it on modules with Firebones or Turrets.

* `KeepRecoilAcrossStates = No` - see [Animation blending](/GeneralsGameCode_Modding/systems/animation-blending/#w3dmodeldraw-module-level).

* `Material = <Material name>` - Direct3D 12 material for the whole model, see [Materials](/GeneralsGameCode_Modding/rendering/directx12/#materials).

## W3DDecalDraw

**New in this fork.**

A draw module that renders a single ground decal for its object. Mainly used together with the [Decal FX nugget](/GeneralsGameCode_Modding/ini/fxlist-and-particles/#decal-entries) to create temporary decals (e.g. scorch marks, footprints) via FXLists.

```ini
Draw = W3DDecalDraw ModuleTag_Decal
  Texture = <texture name>
  Style = SHADOW_ALPHA_DECAL   ; SHADOW_ALPHA_DECAL or SHADOW_ADDITIVE_DECAL only - see note below
  Color = R:255 G:255 B:255 A:255
  Opacity = 100%
  SizeX = 1.0       ; world-space size in X (scaled by the drawable's scale)
  SizeY = 1.0       ; world-space size in Y (scaled by the drawable's scale)
  FadeInTime = 0    ; ms to fade in
  FadeOutTime = 0   ; ms to fade out before the object expires (use with LifetimeUpdate)
  RenderAboveWater = No   ; Yes or No - see below
End
```

Only `RenderAboveWater` has a set default; give every other field a value (the values above are an example, not defaults).

* `RenderAboveWater = No` - (If Yes, the decal is drawn above the water surface; if No (default), below it, like a shadow. The global [`RadiusDecalsAboveWater`](/GeneralsGameCode_Modding/ini/gamedata/#radius-decals-above-water) GameData flag does not apply to this module.)
* `FadeInTime` - (While fading in, the opacity rises from 0 to 100%, not to `Opacity`.)

Note: `Style = SHADOW_DECAL` is **not** supported here (it's a real object-shadow type, not a standalone FX decal, and will crash/get coerced to `SHADOW_ALPHA_DECAL`). Only `SHADOW_ALPHA_DECAL` and `SHADOW_ADDITIVE_DECAL` are valid.
