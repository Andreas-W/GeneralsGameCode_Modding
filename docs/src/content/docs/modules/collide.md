---
title: "Collide modules"
description: "Crate collision, sticky bombs and AdvancedCollide."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Collide/CrateCollide/CrateCollide.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Collide/CrateCollide/StickyBombCrateCollide.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/StickyBombCrateCollide.h
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/StickyBombUpdate.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/StickyBombUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Collide/AdvancedCollide.cpp
sidebar:
  order: 5
---

## CrateCollide

Added new parameters for all CrateCollide modules:

* `AllowNeutralPlayer = No` - (Crate can be picked up by objects of the neutral player.)
* `AllowPickAboveTerrain = No` - (Crate can be picked up while it is above the ground. Without it, only `BuildingPickup` structures can pick up a crate in the air.)

## StickyBombCrateCollide

**New in this fork.**

CrateCollide module to apply sticky bombs to objects. Can be used with pickup crates or projectiles.

Example:
```ini
Behavior = StickyBombCrateCollide ModuleTag_419
   NeedsTarget = No ; Only the intended target object (AI goal object, or projectile target) triggers it, not anything that just collides
   StickyBombObject = <Object with StickyBombUpdate>  ; The object to create
   AllowMultiCollide = No ; If No, the module triggers only once (a failed chance roll also uses it up)
   ShowInfiltrationEvent = No  ; If enabled, shows a radar event for the target player
   ChanceToTriggerPercent = 100%  ; Chance to create the StickyBombObject.
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

* `Animation2DBase = <entry from Animation2D.ini>` - (2D anim for the background bomb visuals, shown on remote and timed bombs. Default: the retail `BombRemote` icon.)
* `Animation2DTimed = <entry from Animation2D.ini>` - (2D anim for the foreground/timer visuals; used when the bomb object has a `LifetimeUpdate`. Default: the retail `BombTimed` icon.)
* `ShowTimer = Yes` - (Set to No to treat timed bombs like remote bombs: only the base animation is shown.)

Note: Setting `Animation2DBase` or `Animation2DTimed` to `None` will hide them.

## AdvancedCollide

**New in this fork.**
A collide module with more versatile parameters

* `CollideWeapon = <weapon entry>` - (Weapon to fire on collide, at the object we collide with)
* `OCL = <OCL entry>` - (OCL to create on collide)
* `FX = <FXList entry>` - (FX to play on collide)
* `FireOnce = No` - (Meant to trigger only once instead of on every collision. **Currently has no effect**: the module never records that it fired, so it triggers on every collision.)
* `CollideWithGround = No` - (trigger when hitting the ground)
* `CollideWithObjects = Yes` - (trigger when hitting objects)
* `RequiredStatus = <status list>` - (this object needs all of these statuses)
* `ForbiddenStatus = <status list>` - (this object must have none of these statuses)
* `TargetRequiredStatus = <status list>` - (the object we collide with needs all of these statuses)
* `TargetForbiddenStatus = <status list>` - (the object we collide with must have none of these statuses)
* `RequiredKindOf = <KindOf list>` - (required KindOf for the object we collide with)
* `ForbiddenKindOf = <KindOf list>` - (forbidden KindOf for the object we collide with)
* `ChanceToTriggerPercent = 100%` - (Chance to trigger effects on collide)
* `RollOnceForTrigger = No` - (Roll for trigger chance every time we collide, or only the first time (and then keep the result))

Note: This module allows to trigger effects when an object collides with the ground, e.g. when a projectile misses its target. The target checks (status, KindOf) only apply to object collisions; there is no check for allies or enemies.
