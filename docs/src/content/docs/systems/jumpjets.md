---
title: "Jumpjets and special abilities"
description: "Jumpjet infantry and special ability modules."
status: unverified
sidebar:
  order: 15
---

## SpecialAbilityUpdate

Added parameters to control turning before an ability is used:
* `RequiresMoveToTurn = No` - (If Yes, the unit must use its movement/locomotor to turn towards the target before the ability can fire, instead of snapping its facing.)
* `FacingAngleTolerance = 0` - (Allowed facing angle error (in degrees) towards the target before the ability is considered "facing" the target.)

## JumpjetContain

**New in this fork.**

A contain module for a "jumpjet"-style carrier that launches and lands riders which fly using jumpjet missiles. Handles the flying/landing animation states and landing safety checks.

```ini
Behavior = JumpjetContain ModuleTag_Jumpjet
  ;<TransportContain parameters>
  FlyingConditionFlag = <ModelCondition>    ; condition flag set while flying
  LandingConditionFlag = <ModelCondition>   ; condition flag set while landing
  LandingDistance = 0.0                     ; distance from the ground at which landing begins
  KillWhenLandingInWater = No               ; kill the unit if it tries to land in water
  KillWhenLandingInWaterSlop = 0.0          ; extra water-depth tolerance before the water kill triggers
  KillWhenLandingOnCliff = No               ; kill the unit if it tries to land on an (impassable) cliff
  KillWhenLandingOnImpassable = No          ; kill the unit if it tries to land on impassable terrain
End
```

## JumpjetMissileAIUpdate

**New in this fork.**

AI update for the projectiles/riders launched by a jumpjet carrier.

```ini
Behavior = JumpjetMissileAIUpdate ModuleTag_JumpjetMissile
  ScatterRadius = 0.0        ; random scatter radius around the target (default = 0)
  MaxSearchRadius = 100.0    ; radius to search for targets (default = 100)
  InitialPitchAngle = 0.0    ; initial launch pitch angle in degrees (default = 0)
End
```
