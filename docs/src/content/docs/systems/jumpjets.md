---
title: "Jumpjets and special abilities"
description: "Jumpjet infantry and special ability modules."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/SpecialAbilityUpdate.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/SpecialAbilityUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/JumpjetContain.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/JumpjetMissileAIUpdate.cpp
sidebar:
  order: 15
---

## SpecialAbilityUpdate

Added parameters to control turning before an ability is used (with `NeedToFaceTarget = Yes`):
* `RequiresMoveToTurn = No` - (If Yes and the unit's current locomotor has a `MinTurnSpeed` above 0 (it cannot turn in place, e.g. planes), the unit drives towards the target to turn, and stops once it faces the target, instead of trying to turn in place.)
* `FacingAngleTolerance = 5.7` - (Allowed facing angle error (in degrees) towards the target before the ability is considered "facing" the target. Only used with `RequiresMoveToTurn`. Default = 0.1 radians, about 5.7 degrees.)

## JumpjetContain

**New in this fork.**

A contain module (based on OpenContain) for the "jumpjet" carrier object a jumping unit rides in while it flies; the carrier itself flies with `JumpjetMissileAIUpdate`. Handles the rider's flying/landing condition states and the landing safety checks.

```ini
Behavior = JumpjetContain ModuleTag_Jumpjet
  ;<OpenContain parameters>
  FlyingConditionFlag = FREEFALL            ; condition flag set on the rider while flying (default FREEFALL)
  LandingConditionFlag = PARACHUTING        ; condition flag set on the rider while landing (default PARACHUTING)
  LandingDistance = 0.0                     ; distance to the goal below which the landing state begins; negative = always landing; 0 = never switches to landing
  KillWhenLandingInWater = No               ; kill the rider if it lands in water
  KillWhenLandingInWaterSlop = 10.0         ; height above the water surface at which the rider counts as in the water (default 10)
  KillWhenLandingOnCliff = No               ; kill the rider if it lands on a cliff cell
  KillWhenLandingOnImpassable = No          ; kill the rider if it lands on impassable terrain
End
```

## JumpjetMissileAIUpdate

**New in this fork.**

AI update for the jumpjet carrier object; it is a MissileAIUpdate (all MissileAIUpdate parameters apply) that picks a free landing spot near the target.

```ini
Behavior = JumpjetMissileAIUpdate ModuleTag_JumpjetMissile
  ;<MissileAIUpdate parameters>
  ScatterRadius = 0.0        ; random scatter radius around the target for the landing spot (default = 0; not used for formation group launches)
  MaxSearchRadius = 100.0    ; if no free spot is found within ScatterRadius, search out to this radius; if none is found there either, the launch is aborted (default = 100)
  InitialPitchAngle = 0.0    ; pitch the object up by this many degrees at launch (default = 0)
End
```
