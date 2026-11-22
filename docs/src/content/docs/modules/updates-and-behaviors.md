---
title: "Update and behavior modules"
description: "Other new and changed update and behavior modules."
status: verified
verifiedAt: 1501b9d26
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ArmorDamageScalarUpdate.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/LifetimeUpdate.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/RadiusDecalBehavior.cpp
  - Core/GameEngine/Source/GameClient/RadiusDecal.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/ParkingPlaceBehavior.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/ParkingPlaceBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/StealthDetectorUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/WeaponBonusUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/OCLUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Map/TerrainLogic.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/ChronoDeathBehavior.cpp
  - Core/GameEngine/Source/GameClient/Drawable/Update/DynamicGeometryClientUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ResetSpecialPowerTimerWhileAliveUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/DynamicGeometryInfoUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/DamageStateGeometryBehavior.cpp
sidebar:
  order: 6
---

## ArmorDamageScalarUpdate

**New in this fork.**

This module was designed for a temporary armor buff ability. The module will apply a damage/armor scale value to every unit in the area for the given amount of time. In addition, various visual effects can be added.

example:
```ini
Behavior = ArmorDamageScalarUpdate ModuleTag_01
    AllowedAffectKindOf = VEHICLE STRUCTURE  ; (required; any object matching *any* of these KindOfs is a valid target)
    ForbiddenAffectKindOf = INFANTRY  ; (optional; any object matching *any* of these KindOfs is an invalid target)
    AffectsTargets = ALLIES NEUTRALS  ; (required; any combination of ALLIES, NEUTRALS, ENEMIES)
    AffectAirborne = Yes   ; (default = Yes; Should this affect units currently in the air) 
    InitialDelay = 0     ; (default = 0; time in ms before the effect is applied)
    BonusDuration = 10000     ; (required; How long effect lasts, in ms)
    BonusRange = 100     ; (required; Radius of the effect)
    ArmorDamageScalar = 0.9   ; (default = 1.0; damage scalar to apply; 1.0 = no effect; 0.5 = takes half damage; 2.0 = takes double damage; min = 0.01)
    EffectParticleSystem = <entry from particlesystem.ini>  ; (optional; apply particlesystem to affected objects. Its system lifetime is set to BonusDuration.)
    OverrideDamageFX = <entry from damageFX.ini> ; (optional; change the object's DamageFX to this entry over the duration)
    ScaleParticleSystem = Yes  ; (default = No; If enabled, the particle system's emission box is set to the object's geometry and its burst count and delay are adjusted for the object's size)
    TintStatusType = FRENZY  ; (default = none; color tint status applied to the objects for the duration, e.g. FRENZY, SHIELDED, BOOST, IRON_CURTAIN)
End
```
**Notes:**
- This module is designed to be put on a dummy object marker, like for EmergencyRepair or Frenzy.
- To correctly track the objects over the duration, the marker object needs to stay alive for the duration.
Lifetime is tracked in the module itself: the marker object is destroyed when BonusDuration has passed. You do not need to add a LifetimeUpdate as well. If the object is killed earlier, the effect will be removed from all units.
- The effect is applied once at the beginning. Objects leaving or entering the radius later will not get the effect.
- The module can be used to apply a damage increase to enemy units (i.e. lower their armor)

## LifetimeUpdate

Add new parameter:
* `ShowProgressBar = No` - Enables progress bar above unit health to visualize lifetime. Requires SHOW_PROGRESS_BAR KindOf.

## RadiusDecalBehavior

**New in this fork.**

New module to create a radius decal that follows a unit. Can be triggered via upgrade.

Example:
```ini
Behavior = RadiusDecalBehavior ModuleTag_decal1
    TriggeredBy = <Upgrade entry>  ; Optional
    ; < All other basic upgrade entries (ConflictsWith, RemovesUpgrades, FXListUpgrade, RequiresAllTriggers) >
    StartsActive = No  ; Set to yes, to enable initially
    Radius = 100.0 ; (default = 0; radius of the decal in world units; the decal is only shown if > 0)
    RadiusDecal   ; Decal Template, Same parameters as DeliveryDecal in OCL ini
      Texture           = SCCFuelAirBomb_USA   ; Texture name (required)
      Style             = SHADOW_ALPHA_DECAL   ; default = SHADOW_ALPHA_DECAL; SHADOW_DECAL, SHADOW_ALPHA_DECAL or SHADOW_ADDITIVE_DECAL
      OpacityMin        = 25%    ; default = 100%, Opacity will move between min and max
      OpacityMax        = 50%    ; default = 100%
      OpacityThrobTime  = 500    ; default = 1000
      Color             = R:255 G:0 B:0 A:255   ; default = the owning player's color
      OnlyVisibleToOwningPlayer = Yes     ; default = Yes; Disable to make the decal visible to all players
    End
End
```
Note: This module reacts to the upgrade being removed (by a different upgrade). This means you can make the decal toggle on/off.

Added parameters:
* `WorksWhileContained = No` - (If Yes, the decal stays visible while the object is held (`DISABLED_HELD`, e.g. inside a container). If No, it is hidden while held and comes back afterwards.)

## ParkingPlaceBehavior

Added new features and parameters to ParkingPlaceBehavior (Airfield)

* `ParkedUnitsDamageScalar = 1.0` - scalar for damage taken applied to all parked aircraft (0.9 = only take 90% of damage)
* `ParkedUnitsDamageScalarUpgraded = 1.0` - same as above, after upgrade
* `DamageScalarUpgradedTriggeredBy = <upgrade name>` - upgrade to trigger the upgraded scalar value (a player upgrade, or an object upgrade on the airfield). Once applied it stays applied
* `RequiredKindOf = <KindOf list>` - if set, only aircraft that has all of these KindOfs is allowed to dock here
* `ForbiddenKindOf = <KindOf list>` - if set, aircraft that has any of these KindOfs is not allowed to dock here.

Notes:
* ParkedUnitsDamageScalar can be used to apply an upgrade that grants damage protection to parked aircraft
* Required/Forbidden KindOf can be used to allow only specific kinds of aircraft to land (i.e. to use different sizes, or differ between VTOL/Regular jets)

## StealthDetectorUpdate

Can now be switched on by an upgrade, and limited to playable owners. Without `TriggeredBy` it is always active, as before.

```ini
Behavior = StealthDetectorUpdate ModuleTag_SD
  ; ... existing parameters ...
  ; Generic Upgrade parameters:
  TriggeredBy = <Upgrade entry>
  ConflictsWith = <Upgrade entry>
  RequiresAllTriggers = No
  ; ---
  StartsActive = No          ; active from the start even with TriggeredBy
  RequiresPlayerOwner = No   ; if Yes, inactive while owned by a non-playable side (neutral, civilian), e.g. a tech building that only detects once captured
End
```

Removing the trigger upgrade switches the detector off again. `InitiallyDisabled` and the battle plan switch still work on top of this.

## WeaponBonusUpdate

Added Parameters:
* `AffectsTargets = ALLIES` - (any combination of ALLIES, ENEMIES, NEUTRALS; allows applying a bonus to enemies)
* `AffectAirborne = Yes` - (allow/disallow the bonus be applied to currently airborne units)
* `TintStatusType = FRENZY` - (which color tint to apply, e.g. FRENZY, SHIELDED, BOOST, DEMORALIZED, or NONE)

The existing `BonusConditionType` (the WeaponBonus condition granted to affected units) has no default and must be set.

## OCLUpdate

Added parameters for directional payload delivery (e.g. for OCLs that spawn a delivery aircraft, to make it approach from a consistent direction). Both only matter with `CreateAtEdge = Yes`:
* `DirectionalDelivery = No` - (If Yes, the OCL is created at the map edge point in the direction the source object faces, instead of the closest map edge point.)
* `DirectionalDeliveryFurthestEdge = No` - (If Yes, take the farthest map edge hit along that direction instead of the nearest. This only differs when the object stands on or outside the map border.)

## ChronoDeathBehavior

**New in this fork.**
Death Module for "Chrono" death. This makes the unit fade out and shrink.

Example:
```ini
Behavior = ChronoDeathBehavior ModuleTag_ChronoDeath
  DeathTypes = NONE +CHRONO  ; Death types to use
  ; Additional DieModule params (VeterancyLevels, ExemptStatus, RequiredStatus)
  StartFX = <FXList entry> ; FX to play at the start (the KindOf specific FX below play in addition)
  EndFX = <FXList entry> ; FX to play at the end
  ; Note: The following params are meant to be used (instead of StartFX/EndFX) when used in Default/Object.ini:
  StartFXInfantry = <FXList entry> ; played if the object is INFANTRY
  StartFXVehicle = <FXList entry> ; VEHICLE
  StartFXStructure = <FXList entry>  ; STRUCTURE
  EndFXInfantry = <FXList entry> ; played if the object is INFANTRY
  EndFXVehicle = <FXList entry> ; VEHICLE
  EndFXStructure = <FXList entry>  ; STRUCTURE

  OCL = <OCL entry> ; OCL to create at the start
  OCLDynamicGeometryScaleFactor = 1.0  ; default = 1.0; Used in combination with DynamicGeometryClientUpdate on the created object: it is scaled by (dying object's bounding radius, clamped to 10..100) / this value. Set it to the model's default size (e.g. a value of 10.0 corresponds to a sphere with radius 10.0). 0 disables the scaling

  StartScale = 1.0  ; Unit scale at the start (default = 1.0)
  EndScale = 0.1    ; Unit scale at the end (default = 1.0)
  StartOpacity = 25%  ; Opacity at the start (default = 100%)
  EndOpacity = 0%    ; Opacity at the end (default = 100%)
  DestructionDelay = 500   ; Duration of the sequence in ms (default = 1 frame); the object is removed afterwards
End
```

## DynamicGeometryClientUpdate

**New in this fork.**
Client module for simple animations on a drawable's opacity and scale. Can be useful for effects like EMP bubbles.

Example:
```ini
ClientUpdate = DynamicGeometryClientUpdate ModuleTag_02
  Opacity = INITIAL 0.0      ; value when object is created (all values default to 1.0)
  Opacity = MIDPOINT 1.0     ; value at midpoint duration
  Opacity = FINAL 0.0        ; value at end of duration
  Scale = INITIAL 0.0
  Scale = MIDPOINT 1.0
  Scale = FINAL 0.0
  Interpolation = SMOOTH   ; How values in between are interpolated - SMOOTH (cosine ease in/out) or LINEAR (default)
  TotalDuration = 666   ; in ms (default = 1000)
  MidpointDuration = 200    ; in ms (default = 500); must be > 0 and less than TotalDuration
End
```

Scale multiplies the drawable's own scale. After TotalDuration the last values stay.

## ResetSpecialPowerTimerWhileAliveUpdate

**New in this fork.**
Update Module to reset a player global (shared synch timer) superweapon while a unit is alive, this allows super unit deploys to start the cooldown when the unit is dead. The timer of the owning player is reset every frame while the object is alive

Example:
```ini
  Behavior = ResetSpecialPowerTimerWhileAliveUpdate ModuleTag_x
    SpecialPowerTemplate = Tank_SuperweaponUniqueUnit  ; a superweapon template with shared synch timer ( like genpowers)
  End
```

## DynamicGeometryInfoUpdate

Added a new parameter:
* `ScaleAdditionalGeometry = No` - (If Yes, the [additional shapes](/GeneralsGameCode_Modding/objects/object-definition/#compound-geometry) grow and shrink with the main shape: offsets and sizes along the object's forward axis follow the major radius, along its side axis the minor radius, heights and Z offsets the height, each as a ratio to the geometry before the first change. Radii of spheres and cylinders, and boxes not turned by a multiple of 90 degrees, use the geometric mean of the forward and side ratios. If No, only the main shape changes.)

## DamageStateGeometryBehavior

**New in this fork.**
Gives the object another geometry (main shape and [additional shapes](/GeneralsGameCode_Modding/objects/object-definition/#compound-geometry)) while it is damaged, for example a broken tower that no longer blocks. Healing back to undamaged restores the geometry it had before. Collisions, the cells the object covers and the pathfinding footprint follow at once.

* `DamagedGeometry` - (Optional block up to `End` with the geometry in the damaged state, written like on an Object: `Geometry`, `GeometryMajorRadius`, `GeometryMinorRadius`, `GeometryHeight`, `GeometryIsSmall`, `AdditionalGeometry` blocks.)
* `ReallyDamagedGeometry` - (Optional block, the same for the really damaged state. Without it, the really damaged state uses `DamagedGeometry`.)

```ini
Behavior = DamageStateGeometryBehavior ModuleTag_21
  ReallyDamagedGeometry
    Geometry = BOX
    GeometryMajorRadius = 40
    GeometryMinorRadius = 20
    GeometryHeight = 30
  End
End
```

Notes:
- Rubble keeps the geometry of the state before, cut down to the rubble height.
- Only changes of the damage state through damage and healing count; setting health directly (for example from a script) does not switch the geometry.
