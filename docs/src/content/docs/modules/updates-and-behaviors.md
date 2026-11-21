---
title: "Update and behavior modules"
description: "Other new and changed update and behavior modules."
status: unverified
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
    BonusDuration = 10000     ; (required; How long effect lasts, in ms)
    BonusRange = 100     ; (required; Radius of the effect)
    ArmorDamageScalar = 0.9;   ; (default = 1.0; damage scalar to apply; 1.0 = no effect; 0.5 = takes half damage; 2.0 = takes double damage; min = 0.01)
    EffectParticleSystem = <entry from particlesystem.ini>  ; (optional; apply particlesystem to affected objects. SystemLifeTime is scaled automatically. If VolumeType = BOX, the size will be changed to the object's geometry.)
    OverrideDamageFX = <entry from damageFX.ini> ; (optional; change the object's DamageFX to this entry over the duration)
    ScaleParticleSystem = Yes  ; (default = No; If enabled, the particle system's spawn rate will be adjusted for the object's size)
    ApplyColorTint = Yes  ; (default = No; Apply a dark red color tint to the objects for the duration)
End
```
**Notes:**
- This module is designed to be put on a dummy object marker, like for EmergencyRepair or Frenzy.
- To correctly track the objects over the duration, the marker object needs to stay alive for the duration.
Lifetime is tracked in the module itself. You do not need to add a lifetimeupdate as well. If the object is killed earlier, the effect will be removed from all units.
- The effect is applied once at the beginning. Objects leaving or entering the radius later will not get the effect.
- The module can be used to apply a damage increase to enemy units (i.e. lower their armor)

## LifeTimeUpdate

Add new parameter:
*`ShowProgressBar = No` Enables progress bar above unit health to visualize lifetime. Requires SHOW_PROGRESS_BAR KindOf.

## RadiusDecalBehavior

**New in this fork.**

New module to create a radius decal that follows a unit. Can be triggered via upgrade.

Example:
```ini
Behavior = RadiusDecalBehavior ModuleTag_decal1
    TriggeredBy = <Upgrade entry>  ; Optional
    ; < All other basic upgrade entries (ConflictsWith, RemovesUpgrades, FXListUpgrade, RequiresAllTriggers) >
    StartsActive = No  ; Set to yes, to enable initially
    Radius = 100.0 ; (radius in meters)
    RadiusDecal   ; Decal Template, Same parameters as DeliveryDecal in OCL ini
      Texture           = SCCFuelAirBomb_USA   ; Texture name
      Style             = SHADOW_ALPHA_DECAL   ; SHADOW_DECAL, SHADOW_ALPHA_DECAL or SHADOW_ADDITIVE_DECAL
      OpacityMin        = 25%    ; default = 100%, Opacity will move between min and max
      OpacityMax        = 50%    ; default = 100%
      OpacityThrobTime  = 500    ; default = 1000
      Color             = R:255 G:0 B:0 A:255 
      OnlyVisibleToOwningPlayer = Yes     ; Disable to make the decal visible to all players
      End
  End
```
Note: This module reacts to the upgrade being removed (by a different upgrade). This means you can make the decal toggle on/off.

Added parameters:
* `DecalRadius = 0.0` - (Overrides the decal radius separately from the `Radius` field; if 0, `Radius` is used.)
* `WorksWhileContained = No` - (If Yes, the decal stays visible/active while the object is contained inside a transport or structure.)

## ParkingPlaceBehavior

Added new features and parameters to ParkingPlaceBehavior (Airfield)

* `ParkedUnitsDamageScalar = 1.0` - scalar for damage taken applied to all parked aircraft (0.9 = only take 90% of damage)
* `ParkedUnitsDamageScalarUpgraded = 1.0` - same as above, after upgrade
* `DamageScalarUpgradedTriggeredBy = <upgrade name>` - upgrade to trigger the upgraded scalar value
* `RequiredKindOf = <KindOf list>` - if set, only aircraft that has these KindOfs is allowed to dock here
* `ForbiddenKindOf = <KindOf list>` - if set, aircraft that has these KindOfs is not allowed to dock here.

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
* `AffectsTargets` = [ALLIES/ENEMIES/NEUTRALS]  ; Allows applying a bonus to enemies
* `AffectsAirborne` = Yes  ; allow/disallow the bonus be applied to currently airborne units
* `TintStatusType` = [TINT_STATUS type]  ; which color tint to apply
* `BonusConditionType` = [WeaponBonus type]  ; The WeaponBonus condition flag that is granted to affected units (default = the module's own bonus). Lets you pick which WeaponBonus the module applies.

## OCLUpdate

Added parameters for directional payload delivery (e.g. for OCLs that spawn a delivery aircraft, to make it approach from a consistent direction):
* `DirectionalDelivery = No` - (If Yes, the delivered payload/aircraft is aligned to the source object's direction instead of a fixed/default direction.)
* `DirectionalDeliveryFurthestEdge = No` - (If Yes, always pick the map edge furthest along the delivery angle as the spawn/approach point.)

## ChronoDeathBehavior

**New in this fork.**
Death Module for "Chrono" death. This makes the unit fade out and shrink.

Example:
```ini
Behavior = ChronoDeathBehavior ModuleTag_ChronoDeath
  DeathTypes = NONE +CHRONO  ; Death types to use
  ; Additional DieModule params (VeterancyLevels, ExemptStatus, RequiredStatus)
  StartFX = <FXList entry> ; FX to play at the start
  EndFX = <FXList entry> ; FX to play at the end
  ; Note: The following params are meant to be used (instead of StartFX/EndFX) when used in Default/Object.ini:
  StartFXInfantry = <FXList entry> ; played if the object is INFANTRY
  StartFXVehicle = <FXList entry> ; VEHICLE
  StartFXStructure = <FXList entry>  ; STRUCTURE
  EndFXInfantry = <FXList entry> ; played if the object is INFANTRY
  EndFXVehicle = <FXList entry> ; VEHICLE
  EndFXStructure = <FXList entry>  ; STRUCTURE

  OCL = <OCL entry> ; OCL to create at the start
  OCLDynamicGeometryScaleFactor = 0.0;  Used in combination with DynamicGeometryClientUpdate. If this is set, the created object will be scaled to the parent object's radius. The value here should be set to the model's default size (e.g. a value of 10.0 corresponds to a sphere with radius 10.0)

  StartScale = 1.0  ; Unit scale at the start
  EndScale = 0.1    ; Unit scale at the end
  StartOpacity = 25%  ; Opacity at the start
  EndOpacity = 0%    ; Opacity at the end
  DestructionDelay = 500   ; Duration of the sequence in ms
End
```

## DynamicGeometryClientUpdate

**New in this fork.**
Client module for simple animations on a drawable's opacity and scale. Can be useful for effects like EMP bubbles.

Example:
```ini
ClientUpdate = DynamicGeometryClientUpdate ModuleTag_02
  Opacity = INITIAL 0.0      ; value when object is created
  Opacity = MIDPOINT 1.0     ; value at midpoint duration
  Opacity = FINAL 0.0        ; value at end of duration
  Scale = INITIAL 0.0
  Scale = MIDPOINT 1.0
  Scale = FINAL 0.0
  Interpolation = SMOOTH   ; How values in between are interpolated - SMOOTH (sinus curve) or LINEAR
  TotalDuration = 666   ; in ms
  MidpointDuration = 200    ; in ms
End
```

## ResetSpecialPowerTimerWhileAliveUpdate

**New in this fork.**
Update Module to reset a player global (shared synch timer) superweapon while a unit is alive, this allows super unit deploys to start the cooldown when the unit is dead

Example:
```ini
  Behavior = ResetSpecialPowerTimerWhileAliveUpdate ModuleTag_x
    SpecialPowerTemplate = Tank_SuperweaponUniqueUnit  ; a superweapon template with shared synch timer ( like genpowers)
  End
```
