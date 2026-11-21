---
title: "ObjectCreationList.ini"
description: "New OCL nuggets and fields."
status: unverified
sidebar:
  order: 27
---

## XP and Weapon Bonus Propagation
Added new parameters to CreateObject entries

* `InheritsWeaponBonus = No` - (Created objects will inherit their current weapon bonus from their caller. This allows delayed weapon explosions to properly use the parent object's upgrades and veterancy.)
* `ExperienceSinkForCaller = No` - (Any experience gained by the created object will be propagated to it's caller. This allows objects created by weapon's detonation OCL, like FireFields to gain XP for the shooter.)

## Water Impact (CreateObject)

Added parameters to `CreateObject` nuggets so that created debris reacts when it hits the water surface (used for naval wrecks/debris):

* `DebrisObject = <ObjectTemplate>` - (Object template to create as debris.)
* `WaterImpactSound = <AudioEvent>` - (Sound played when the created object impacts water.)
* `WaterImpactFX = <FXList>` - (FXList played when the created object impacts water.)

## New Disposition Flags

* `ALIGN_Z_UP` - Used in combination with LIKE_EXISTING; Keeps the Z axis of the object aligned upwards.

## DeliverPayload

Added parameters to the `DeliverPayload` nugget:

* `TargetOffset = X:0.0 Y:0.0` - (2D offset (relative to the delivery direction) applied to the payload's target/drop point. Combined with the formation size to spread out multiple delivered objects.)
* `FireWeaponSlots = PRIMARY` - (Which weapon slot(s) the delivering object fires while delivering the payload. Accepts a list of weapon slots, e.g. `PRIMARY SECONDARY`. Default = PRIMARY)
* `StrafingWeaponTargetsWater = No` - (If Yes, a strafing delivery (e.g. A-10 strafe genpower) will also target the water surface, so shots detonate on water instead of passing through. Default = No)
