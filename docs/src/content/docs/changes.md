---
title: "Changes and bug fixes"
description: "Raised limits, changed defaults and fixed engine bugs."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Include/Common/Upgrade.h
  - GeneralsMD/Code/GameEngine/Include/GameLogic/AI.h
  - Core/GameEngine/Include/Common/SciencePurchaseSlots.h
sidebar:
  order: 39
---

## Introduction

This page describes changes made to the default behavior of the code. This will be mostly bug fixes.
We try to keep changes to a minimum. This list contains all changes that will affect the default behavior. 

## Limits
### Increased Upgrade Limit to 1024
The game can define up to 1024 `Upgrade` blocks (`UPGRADE_MAX_COUNT`), instead of 128 in retail.

### AI SkillSets can list 35 sciences
A `SkillSet1` .. `SkillSet5` block of a `SideInfo` in AIData.ini can now list up to 35 `Science` entries (every slot of the three purchase screens) instead of 20. Further entries are still dropped, with a debug crash.

## Code Changes

### Minor Laser visual improvements
#### Module: LaserUpdate
In vanilla ZH, a laser beam always moves along with the shooter's firebone, but the `MuzzleParticleSystem` will stay in place.
This has been changed. The muzzle particle system will now always move with the shooter's firebone along with the laser beam.

### Fixed HelixContain passenger fireports not working

#### Module: HelixContain

Transports with HelixContain and fireports, now have their passenger correctly fire from the `FIREPOINTxx` bones.

Note: This requires the transport to have the `FIREPOINTxx` bones. It is not enough to put them on a portable Bunker object.

### Changed default behavior for Airborne collisions in PhysicsUpdate

To avoid helicopters randomly dying via a chain reaction, we changed the conditions when "VehicleCrashesIntoNonBuildingWeaponTemplate" is fired. This weapon should be triggered when an object (i.e. a dying helicopter) crashes into a ground vehicle, but it often triggers already mid air if the object collides with airborne projectiles, lasers or dummy objects. We changed the default behavior to ignore airborne collisions.

#### Module: PhysicsBehavior

Changed conditions for _VehicleCrashesIntoNonBuildingWeaponTemplate_ to be fired only when the object it falls into is not airborne. Added new parameter to restore default behavior

- `VehicleCrashWeaponAllowAirborne = No` - (`Yes` restores the vanilla behavior to also trigger VehicleCrashesIntoNonBuildingWeaponTemplate on airborne collisions. Not Recommended)

### VTOL Improvements
(Module: JetAIUpdate)

VTOL Aircraft = Aircraft which needs parking space in airfields but with NeedsRunway = No in JetAIUpdate.

Various fixes and improvements:

* Add support for Jets (WINGS locomotor) to land/take off without runways. Previously only aircraft with HOVER locomotor could park/land. This requires a HOVER locomotor in locomotor set SET_VTOL.
* Slightly changed landing and takeoff behavior of all VTOL aircraft. Vertical take off now takes slightly longer.
* Fixed wrong parking orientation for VTOL aircraft. VTOL aircraft were turning the wrong way when reloading ammo in their parking spot. This is fixed.

### Fixed Spectre Gunship getting selected for every player

Fixed issue in SpectreGunshipDeploymentUpdate causing the gunship to get selected for every player.
The gunship is now selected for the controlling player only.

### Fixed Bunker Buster clearing out Tunnels while under construction

When a bunker buster bomb hits a garrisoned structure that is under construction (this can only really happen for tunnels), the contained units will no longer be thrown out.

### Fixed USA Chemsuit Upgrade crashing when a unit starts as veteran

The `ArmorUpgrade` triggered by `Upgrade_AmericaChemicalSuits` sets the Chemsuit terrain decal on the unit's drawable. It used to crash when the unit had no drawable at that moment; now the decal is only set when there is one.

### Fixed Decal Shadows being applied multiple times

Fixed issue for units with texture based shadows (SHADOW_DECAL, SHADOW_ALPHA_DECAL or SHADOW_ADDITIVE_DECAL) where the shadow decal is applied multiple times if the unit has multiple draw modules. Now only 1 instance of the shadow is applied.

### Fixed VeterancyCrateCollide module not working for regular pick up crates

VeterancyCrateCollide only worked for pilots. Now it can be used for regular crates.

### Fixed PoisonedBehavior not using POISON damage type
The damage over time of `PoisonedBehavior` is `POISON` damage again. In order to prevent an infinite self-poisoning loop, the damage being dealt was set to DAMAGE_UNRESISTABLE.
This is now fixed (the poison source is checked instead) and POISON damage is applied.

### Fixed Transitionstates anims not working reliably on Night/Snow maps
Fixed an issue where TransitionStates are incorrectly initialized if Night or Snow states are not explicitly defined for a Draw module. This caused Turrets or firebones sometimes not working during Transition animations. 

### Fixed FXLists being wrongly aligned
By default, a `ParticleSystem` nugget of an FXList without `OrientToObject = Yes` will not have its `Offset` aligned to the parent object. This prevents the issue of large effects like Nuke mushroom clouds to be "drifting away". The original behavior can be restored by using `OrientOffset = Yes` in the nugget.

### Fixed shared weapon reload
Fixed an issue with `WeaponReloadSharedAcrossSets` (see [Objects & Modules](/GeneralsGameCode_Modding/objects/object-definition/#weaponset)) where reload time / ammo count was not shared correctly when switching between weapon sets.

### Fixed looping firing sound issue
#### Module: FiringTracker
Fixed an issue where a weapon's looping firing sound could keep playing after the unit stopped firing.

### Fixed LocomotorWorksWhenDisabled
#### Module: AIUpdate
Fixed and added safeguards for the Locomotor field `LocomotorWorksWhenDisabled`, so disabled units with this flag move correctly across the various AIUpdate types.

### Veterancy weapon FX for scatter/secondary projectiles
Projectiles (`DumbProjectileBehavior`, `FreeFallProjectileBehavior`, `MissileAIUpdate`, `NeutronMissileUpdate`) now snapshot the **launcher's** veterancy level and use it to pick the per-veterancy weapon FX (`VeterancyProjectileExhaust`, `VeterancyFireFX`, `VeterancyProjectileDetonationFX`, `VeterancyFireOCL`, ...). This makes veterancy-based exhaust/FX work correctly for ScatterShot and other secondary weapon effects.

### KINDOF_NO_SELECT now actually works
Previously a no-op, `KindOf = NO_SELECT` (`KINDOF_NO_SELECT` in the code) now correctly prevents an object from being selected (`Object::isSelectable()`). Any object relying on this flag to be an unselectable helper/decoration now behaves as documented.

### Lowercase name keys are stored in lowercase
Fixed `NameKeyGenerator::nameToLowercaseKey` to store the name in lowercase instead of in the letter casing it was first seen with. Previously, a later case-sensitive lookup of the lowercase name did not find that entry and created a second, different key for the same name.

### Contain fixes
* Occupants are now preserved when transferring a garrisoned/contained object to an ally (previously they could be lost).
* Tunnel Network passengers can once again exit from any tunnel entrance, not just the one they entered through.
* Passengers can now exit an immobilized Battle Bus (previously they were stuck inside).

### Fixed re-placing Booby Traps
StickyBomb-based Booby Traps can be placed on an object that already has one again.

### Fixed diving missiles missing moving targets
Diving missiles (`MissileAIUpdate`) now correctly track and hit moving targets instead of frequently missing.

### JetAIUpdate: Airfield loss fixes
* Aircraft no longer fall to their death if their Airfield is lost/destroyed while they are returning to it.
* Helicopters no longer land or remain landed at an Airfield that has been destroyed.
* Helicopter landing position is now saved/restored correctly.

### Removed the 100pt font size cap
The engine's hardcoded upper limit of 100pt for game fonts has been removed, relevant for UI/HUD mods that want larger text.

### Bridge fixes
Found while adding [destroyable road bridges](/GeneralsGameCode_Modding/systems/bridges/#destroyable-road-bridges):
* Destroyed bridges no longer block shots and effects. Explosions and OCL spawns were placed on the deck plane of a bridge that no longer existed, and projectiles (`DumbProjectileBehavior`, `MissileAIUpdate`) flying through the plane of a wrecked deck detonated on it.
* A destroyed bridge (or an open drawbridge) without `BridgeHoleAreaPercentage` now counts as having lost its whole deck. Before, it was still reported as intact.
* A destroyed bridge's collision box is collapsed, so the wreck no longer catches area damage and range checks. It is restored on repair, and a repaired bridge gets its collisions back.
* The bridge is punched open on death whether or not it is `Restoreable`.
* `Bridge` blocks in Roads.ini now inherit `RadarColor` and `BridgeHoleAreaPercentage` from `DefaultBridge`. Before, a block without `RadarColor` drew black on the radar.

### RequiredBridgeHeight rounds up
`RequiredBridgeHeight` (explicit or derived from the object's geometry height) is now rounded up to the next 10 world units instead of down, so e.g. a 12 unit tall object no longer passes under a 10 unit gap. Some objects that used to fit under a bridge may now be blocked. The clearance under a bridge is also reduced by the new `BridgeDeckHeight` (default 0).

### Fixed multi-target special powers
Special powers with `NumberOfTargets` > 1 (see [Multi-Target Special Powers](/GeneralsGameCode_Modding/ini/special-power/#multi-target-special-powers)) lost their pending command after the first click, so the remaining targets (or a right-click cancel) could not be placed.

### OverlordContain: rider positions match the drawn bones
Riders of an `OverlordContain` are now positioned in the logic on the bone of their own slot (`FIREPOINT01`, `FIREPOINT02`, ..., see `AddOnBoneName`), the same bone their draw module attaches to. Before, with several riders, turret aiming and projectile launch positions were calculated from a different mount than the one drawn.

### Battle plan fixes
* Activating a battle plan no longer clears the weapon bonus of another active plan (e.g. two Strategy Centers running different plans).
* `BattlePlanBonusBehavior` no longer applies a plan's effects twice or removes effects that were never applied. This used to stack or invert armor, sight and speed scalars, e.g. when the module was activated by a science or upgrade while plans were active.
* Loading a saved game no longer applies the battle plan bonuses a second time to objects that already had them (sight range stacked).

### Superweapon EVA no longer plays twice
Vanilla superweapons (Particle Cannon, Nuke, SCUD Storm, GPS Scrambler, Sneak Attack) played their hardcoded "detected"/"launched" EVA events even when the special power defined its own `EvaDetected*` / `EvaLaunched*` events, so both played. Now the hardcoded events are only used when the special power defines none.

### Particle Cannon beam sound closer to the ground
The positional sound of the `ParticleUplinkCannonUpdate` beam now sits 250 world units above the target instead of 500, so it is heard louder at normal camera height.

### ObjectExtend: Prerequisites replace the inherited ones
An `ObjectExtend` object that defines its own `Prerequisites` now replaces the parent's prerequisites, like WeaponSets and ArmorSets. Before, they were added to the inherited ones.

### Rift: invulnerable structures no longer spawn debris
`RiftSlowDeathBehavior` only tears debris off structures it can actually destroy. Structures with `ImmortalBody` (e.g. `GenericBridge` and its towers) or `HighlanderBody` report damage but never die, and used to spawn debris every tick.

### HeightMapScale: max terrain height
Code that assumed a maximum terrain height of 255 (terrain and road height ranges, WorldBuilder mesh mold and map preview) now uses 255 × the map's `HeightMapScale`, so maps with a scale above 1 are handled over their full height range.

### Fixed audio deadlock
Fixed a freeze that could happen when the audio file cache evicted a sound at the same moment another sample finished playing.

### Self-contained mod folders without a Generals install
A mod folder that carries the original Generals archives itself (e.g. `INI.big` in a subfolder) no longer triggers the "Go install Generals!" assert when the Generals registry entry is missing.
