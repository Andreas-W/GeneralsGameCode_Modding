---
title: "FXList and ParticleSystem"
description: "New FXList nugget fields and particle system changes."
status: unverified
sidebar:
  order: 28
---

## FXList
*This refers to FXList definitions in FXList.ini* 

### ParticleSystem entries
*This refers to ParticleSystem entries within an FXList definition in FXList.ini*

#### Added Alignment Parameters

Note: By default, FXList without "OrientToObject = Yes" will not have their offset aligned to the parent object anymore. This prevents the issue of large effects like Nuke mushroom clouds to be "drifting away".

* `OrientOffset = No` - Restores vanilla behavior by always orienting the Offset to the parent object (not recommended, this mainly exists for compatibility)
* `OrientXY = No` - Like OrientToObject, but keeps the Z axis aligned upwards. Useful for directional impact effects that have an upwards cloud component.

#### Height and Surface Restrictions

Added parameters to restrict when a ParticleSystem nugget is played, based on the height above terrain and the surface type underneath. Mainly intended for naval/water effects.

* `MinAllowedHeight = <height>` - (The effect is skipped if its Z offset above the terrain is below this value. Default = no limit)
* `MaxAllowedHeight = <height>` - (The effect is skipped if its Z offset above the terrain is above this value. Default = no limit)
* `AllowedSurface = ALL` - (Restrict the effect to a surface type. Default = ALL)
  - `ALL` - no restriction (default)
  - `LAND` - only play over land
  - `WATER` - only play over water (also skips bridges)

### Sound entries
*This refers to Sound entries within an FXList definition in FXList.ini*

#### Height and Surface Restrictions

The same restrictions as for ParticleSystem nuggets (see above) were added for Sound nuggets:

* `MinAllowedHeight = <height>` - (Skip the sound if its Z offset above terrain is below this value. Default = no limit)
* `MaxAllowedHeight = <height>` - (Skip the sound if its Z offset above terrain is above this value. Default = no limit)
* `AllowedSurface = ALL` - (`ALL` / `LAND` / `WATER`; restrict the sound to a surface type. Default = ALL)

### Decal entries
*This refers to a new `Decal` nugget within an FXList definition in FXList.ini*

Added a new FX nugget that spawns a ground decal at the FX location. The decal itself is an object template that uses the new [W3DDecalDraw](/GeneralsGameCode_Modding/modules/draw/#w3ddecaldraw) module.

* `DecalName = <ObjectTemplate>` - (Name of the object template to spawn for the decal. Default = `GenericDecal`. Can be listed **multiple times** - one is picked at random each time the nugget fires.)
* `Lifetime = <ms>` - (Lifetime of the decal in milliseconds)
* `Offset = X:0 Y:0 Z:0` - (Offset of the decal relative to the FX location)
* `Scale = 1.0` - (Scale applied to the decal. Accepts a [GameClientRandomVariable](/GeneralsGameCode_Modding/ini/fxlist-and-particles/) range, e.g. `Scale = Min:0.8 Max:1.2`, to randomize the size per spawn.)
* `Angle = 0.0` - (Fixed rotation angle of the decal)
* `RandomAngle = No` - (If Yes, use a random rotation angle instead of `Angle`)
* `OrientToObject = No` - (If Yes, orient the decal to the parent object's facing)
* `Probability = 1.0` - (Chance (0.0 - 1.0) for the decal to be created)

Example:
```ini
FXList FX_ScorchDecal
  Decal
    DecalName = ScorchDecalSmall
    DecalName = ScorchDecalSmall2   ; picked at random alongside the one above
    Lifetime = 20000
    Scale = Min:0.8 Max:1.2
    RandomAngle = Yes
    Probability = 0.5
  End
End
```

## ParticleSystem
* This refers to ParticleSystem definitions in ParticleSystem.ini*

TBD
