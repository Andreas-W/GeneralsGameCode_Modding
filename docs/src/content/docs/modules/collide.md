---
title: "Collide modules"
description: "Crate collision, sticky bombs and AdvancedCollide."
status: unverified
sidebar:
  order: 5
---

## CrateCollide

Added new parameters for all CrateCollide modules:

* `AllowNeutralPlayer = No` - Crate can be picked up by the neutral player
* `AllowPickAboveTerrain = No` - Crate can be picked up when it is above ground.

## StickyBombCrateCollide

CrateCollide module to apply sticky bombs to objects. Can be used with pickup crates or projectiles.

Example:
```ini
Behavior = StickyBombCrateCollide ModuleTag_419
   NeedsTarget = No ; Needs an intended target object (e.g. from a projectile) cannot be picked up by regular collision
   StickyBombObject = <Object with StickyBombUpdate>  ; The object to create
   AllowMultiCollide = No ; If used as a crate, can the effect be applied to multiple objects at once?
   ShowInfiltrationEvent = No  ; If enabled, shows a radar event for the target player
   ChanceToTriggerPercent = 100%; Chance to create the StickyBombObject.
End
```

Example to use on a Missile or DumbProjectile to apply a sticksBomb to the target:
```ini
Behavior = StickyBombCrateCollide ModuleTag_419
  RequiredKindOf = STRUCTURE
  ForbiddenKindOf = VEHICLE INFANTRY
  BuildingPickup = Yes
  ExecuteFX = FX_SniperDroneTargetTracerPingFX
  AllowNeutralPlayer = Yes
  AllowPickAboveTerrain = Yes
  NeedsTarget = Yes
  StickyBombObject = Lazr_SniperDroneTargetTracerStickyBomb
End

; Needs separate modules for Building/Unit collisions
Behavior = StickyBombCrateCollide ModuleTag_420
  ;RequiredKindOf = VEHICLE INFANTRY
  ForbiddenKindOf = STRUCTURE
  BuildingPickup = No
  ExecuteFX = FX_SniperDroneTargetTracerPingFX
  AllowNeutralPlayer = Yes
  AllowPickAboveTerrain = Yes
  NeedsTarget = Yes
  StickyBombObject = Lazr_SniperDroneTargetTracerStickyBomb
End
```

## StickyBombUpdate

Added new parameters to customize the 2D anim visuals:

* `Animation2DBase = <entry from Animation2D.ini>` - 2D anim for background bomb visuals (default = BombRemote)
* `Animation2DTimed = <entry from Animation2D.ini>` 2D anim for foreground/timer visuals; This is used when the bomb object has a LifeTimeUpdate (default = BombTimed) 
* `ShowTimer = Yes`  - Flag to disable Animation2DTimed for timed bombs - Only use base animation.

Note: Setting Animation2DBase or Timed to "None" will hide them.

## AdvancedCollide

**New in this fork.**
A collide module with more versatile parameters

* `CollideWeapon = <weapon entry>` - Weapon to fire on collide
* `OCL = <OCL entry>` - OCL to create on collide
* `FX = <FXList entry>` - FX to create on collide
* `FireOnce = No` - trigger once or for every collision
* `CollideWithGround = No` - trigger collision when hitting the ground
* `CollideWithObjects = Yes` - trigger collision when hitting objects.
* `RequiredStatus = <status entry>` - required status for this object
* `ForbiddenStatus = <status entry>` - forbidden status for this object
* `TargetRequiredStatus = <status entry>` - required status for the object we collide with
* `TargetForbiddenStatus = <status entry>` - forbidden status for the object we collide with
* `RequiredKindOf = <KindOf entry>` - required KindOf for the object we collide with
* `ForbiddenKindOf = <KindOf entry>` - forbidden KindOf for the object we collide with
* `ChanceToTriggerPercent = 100%` - Chance to trigger effects on collide
* `RollOnceForTrigger = No` - Roll for trigger chance every time we collide, or only the first time (and then keep the result)

Note: This module allows to trigger effects when objects collides with the ground, e.g. when a projectile misses it's target.
