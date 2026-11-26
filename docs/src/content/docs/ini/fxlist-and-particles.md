---
title: "FXList and ParticleSystem"
description: "New FXList nugget fields and particle system changes."
status: verified
verifiedAt: 7cdd826c5
sources:
  - Core/GameEngine/Source/GameClient/FXList.cpp
sidebar:
  order: 28
---

## FXList
*This refers to FXList definitions in FXList.ini* 

### ParticleSystem entries
*This refers to ParticleSystem entries within an FXList definition in FXList.ini*

#### Added Alignment Parameters

Note: By default, FXList without "OrientToObject = Yes" will not have their offset aligned to the parent object anymore. This prevents the issue of large effects like Nuke mushroom clouds to be "drifting away".

* `OrientOffset = No` - (Restores vanilla behavior by always orienting the Offset to the parent object (not recommended, this mainly exists for compatibility).)
* `OrientXY = No` - (Like OrientToObject, but only the parent's facing (rotation around Z) is used for the offset and the particle system, so the Z axis stays aligned upwards. Useful for directional impact effects that have an upwards cloud component. Do not combine it with `OrientToObject = Yes`.)

#### Height and Surface Restrictions

Added parameters to restrict when a ParticleSystem nugget is played, based on the height above terrain and the surface type underneath. Mainly intended for naval/water effects.

* `MinAllowedHeight = <height>` - (The effect is skipped if the FX position is less than this high above the surface below it. Default = no limit)
* `MaxAllowedHeight = <height>` - (The effect is skipped if the FX position is more than this high above the surface below it. Default = no limit)
* `AllowedSurface = ALL` - (Restrict the effect to a surface type. Default = ALL)
  - `ALL` - no restriction (default)
  - `LAND` - only play over land (terrain or bridges)
  - `WATER` - only play over water (not on bridges)
* `UseCachedSurfaceInfo = No` - (If Yes, a nugget with an `Offset` or `Radius` reuses the surface check done once for the whole FX call (shared with the other nuggets of the FXList) instead of checking again for every particle system it creates. Default = No)

Notes:
* "The surface below" is the water surface where there is water, otherwise the terrain or a bridge. Water is only looked for when `AllowedSurface` is not `ALL`, or when GameData [`HeightAboveTerrainIncludesWater`](/GeneralsGameCode_Modding/ini/gamedata/#height-above-water) is Yes; otherwise the heights are measured from the terrain (or bridge).
* With `CreateAtGroundHeight = Yes` and a water check, the particle system is created at the water surface instead of on the seabed.
* The check always uses the FX position, not the position moved by `Offset` / `Radius`.

### Sound entries
*This refers to Sound entries within an FXList definition in FXList.ini*

#### Height and Surface Restrictions

The same restrictions as for ParticleSystem nuggets (see above) were added for Sound nuggets:

* `MinAllowedHeight = <height>` - (Skip the sound if the FX position is less than this high above the surface below it. Default = no limit)
* `MaxAllowedHeight = <height>` - (Skip the sound if the FX position is more than this high above the surface below it. Default = no limit)
* `AllowedSurface = ALL` - (`ALL` / `LAND` / `WATER`; restrict the sound to a surface type. Default = ALL)

For Sound nuggets, water is only looked for when `AllowedSurface` is not `ALL`.

### Decal entries
*This refers to a new `Decal` nugget within an FXList definition in FXList.ini*

Added a new FX nugget that spawns a ground decal at the FX location. The decal itself is an object template that uses the new [W3DDecalDraw](/GeneralsGameCode_Modding/modules/draw/#w3ddecaldraw) module.

* `DecalName = <ObjectTemplate>` - (Name of the object template to spawn for the decal. Default = `GenericDecal`. Can be listed **multiple times** - one is picked at random each time the nugget fires.)
* `Lifetime = 0` - (Lifetime of the decal in milliseconds. Set it: the decal is removed when it runs out, so with 0 it disappears at once.)
* `Offset = X:0 Y:0 Z:0` - (Offset of the decal relative to the FX location. Rotated with the parent object when `OrientToObject = Yes`.)
* `Scale = 1.0 1.0` - (Scale factor applied to the decal's size, given as a random range `<low> <high>` (optionally followed by a distribution such as `UNIFORM`, the default). Both values are required; `Scale = 0.8 1.2` randomizes the size per spawn. Default = 1.0 1.0)
* `Angle = 0.0` - (Parsed, but currently has no effect: a fixed angle is not applied.)
* `RandomAngle = No` - (If Yes, the decal gets a random rotation.)
* `OrientToObject = No` - (If Yes, the decal takes over the parent object's orientation, and the `Offset` is rotated with it.)
* `Probability = 1.0` - (Chance (0.0 - 1.0) for the decal to be created)

Example:
```ini
FXList FX_ScorchDecal
  Decal
    DecalName = ScorchDecalSmall
    DecalName = ScorchDecalSmall2   ; picked at random alongside the one above
    Lifetime = 20000
    Scale = 0.8 1.2
    RandomAngle = Yes
    Probability = 0.5
  End
End
```

### IncludeFX entries
*This refers to a new `IncludeFX` nugget within an FXList definition in FXList.ini*

Plays another FXList as if its entries were written at this place. Effects that many FXLists share (the splash, ripple and sound of a water impact, say) can be defined once as an ordinary FXList and included everywhere, instead of repeating their entries in every FXList.

* `FX = <FXList>` - (The FXList to play. It gets the same position, orientation, objects and radius as the FXList that includes it, so `FXListAtBonePos`, `AttachToObject` and `UseCallersRadius` in it work as usual.)
* `MinAllowedHeight = <height>` - (Skip the included FXList if the FX position is less than this high above the surface below it. Default = no limit)
* `MaxAllowedHeight = <height>` - (Skip the included FXList if the FX position is more than this high above the surface below it. Default = no limit)
* `AllowedSurface = ALL` - (`ALL` / `LAND` / `WATER`; play the included FXList only over this surface. Default = ALL)

Example: one weapon FXList that plays a water template on water and a land template on land.
```ini
FXList FX_WaterImpactMedium          ; a template is an ordinary FXList
  WaterRipple
    StartRadius = 5
    EndRadius = 80
    Duration = 2000
  End
  ParticleSystem
    Name = TorpedoWaterDropSplash
  End
End

FXList WeaponFX_ShellDetonation
  IncludeFX
    FX = FX_WaterImpactMedium
    AllowedSurface = WATER
  End
  IncludeFX
    FX = FX_ShellImpactLand
    AllowedSurface = LAND
  End
  ViewShake                          ; its own entries as usual
    Type = SUBTLE
  End
End
```

Notes:
* The included FXList is looked up by name each time it plays, so it may be defined anywhere in FXList.ini, before or after the FXLists that include it, and changes to it (also in the FX Editor) reach all of them. An unknown name plays nothing and is written to the debug log once.
* The height and surface restrictions are checked once for the whole included FXList, the same way as for Sound entries. Entries inside it can have their own restrictions too.
* FXLists that include each other (A includes B, B includes A) stop after 8 levels and write a line to the debug log; nothing hangs.

## ParticleSystem
*This refers to ParticleSystem definitions in ParticleSystem.ini*

Not documented yet.
