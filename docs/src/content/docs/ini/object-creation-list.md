---
title: "ObjectCreationList.ini"
description: "New OCL nuggets and fields."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/ObjectCreationList.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/DeliverPayloadAIUpdate.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/DeliverPayloadAIUpdate.h
sidebar:
  order: 27
---

## XP and Weapon Bonus Propagation
Added new parameters to CreateObject entries

* `InheritsWeaponBonus = No` - (Created objects copy the weapon bonus conditions their caller has at the moment of creation. This allows delayed weapon explosions to properly use the parent object's upgrades and veterancy.)
* `ExperienceSinkForCaller = No` - (Any experience gained by the created object is given to its caller. If the caller itself passes its experience on (e.g. a projectile to its launcher), the experience goes to that final owner. This allows objects created by a weapon's detonation OCL, like FireFields, to gain XP for the shooter.)


## Water Impact (CreateDebris)

Added parameters to `CreateDebris` nuggets so that created debris reacts when it hits the water surface (used for naval wrecks/debris). They are not available in `CreateObject`.

* `WaterImpactSound = <AudioEvent>` - (Sound played when the debris hits the water. Default = none)
* `WaterImpactFX = <FXList>` - (FXList played when the debris hits the water. Default = none)

`DebrisObject = <ObjectTemplate>` (the object template used for the debris, `GenericDebris` when not set) is a vanilla field of `CreateDebris`.

## New Disposition Flags

* `ALIGN_Z_UP` - (Used in combination with `LIKE_EXISTING`: the created object takes over only the caller's facing, not its full tilt/roll, so its Z axis stays pointing up.)

## DeliverPayload

Added parameters to the `DeliverPayload` nugget:

* `TargetOffset = X:0.0 Y:0.0` - (2D offset applied to the payload's target/drop point, relative to the delivery direction: X along the flight direction, Y to the left of it. Combined with the formation offsets when `FormationSize` is above 1. Default = X:0 Y:0)
* `FireWeaponSlots = PRIMARY` - (With `FireWeapon = Yes`: which weapon slot(s) the delivering object fires at the drop point, one shot per listed slot. Accepts a list of weapon slots, e.g. `PRIMARY SECONDARY` (`PRIMARY`, `SECONDARY`, `TERTIARY`, `WEAPON_FOUR` ...). Default = PRIMARY. Known issue: the list is added to the default, so `PRIMARY` is always included; `FireWeaponSlots = SECONDARY` fires both PRIMARY and SECONDARY.)
* `StrafingWeaponTargetsWater = No` - (If Yes, a strafing delivery (`StrafingWeaponSlot`, e.g. the A-10 strafe general's power) aims at the water surface where there is water, instead of the terrain under it, so shots detonate on water. Default = No)
