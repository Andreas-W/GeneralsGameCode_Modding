---
title: "Target designator"
description: "SpecialPowerDesignatorUpdate."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/SpecialPowerDesignatorUpdate.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/SpecialPowerDesignatorUpdate.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/RadiusDecalBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/SpecialPower/SpecialPowerModule.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/ActionManager.cpp
sidebar:
  order: 17
---

## SpecialPowerDesignatorUpdate

**New in this fork.**

Update module that lets a unit "designate" an area, enabling special powers that require a designator (see [`NeedsTargetDesignator`](/GeneralsGameCode_Modding/ini/special-power/#new-parser-fields) on the SpecialPower). It is a [RadiusDecalBehavior](/GeneralsGameCode_Modding/modules/updates-and-behaviors/#radiusdecalbehavior), so it also takes that module's parameters (`StartsActive`, `RadiusDecal`, `Radius`, `WorksWhileContained`) and the upgrade parameters (`TriggeredBy`, ...); it only works while active.

A power with `NeedsTargetDesignator = Yes` can be targeted only within `DesignatorRadius` of an active designator of the same player for that power. The designator object needs `KindOf = TARGET_DESIGNATOR`. When the power fires, the designator closest to the target location is triggered: it gets `TriggerStatusType` for `TriggerStatusTime` and plays `TriggerFX` on itself.

While the player is choosing a target for that power, the designator shows its radius decal (always, with `AlwaysShowDecal = Yes`).

```ini
Behavior = SpecialPowerDesignatorUpdate ModuleTag_Designator
  ;<RadiusDecalBehavior parameters>
  StartsActive = Yes
  SpecialPowerTemplate = <SpecialPower entry>   ; the power that this designator enables
  DesignatorRadius = 0.0       ; radius around the designator in which the power may be targeted
  AlwaysShowDecal = No         ; always show the radius decal, not only while targeting the power
  DecalRadius = 0.0            ; radius of the decal (same value as the RadiusDecalBehavior Radius)
  TriggerStatusType = <ObjectStatus>   ; one status set on the designator itself when the power fires
  TriggerStatusTime = 0        ; how long (ms) that status stays on; 0 = the status is not set
  TriggerFX = <FXList>         ; FX played on the designator when the power fires
  WorksWhileContained = No     ; (RadiusDecalBehavior) also works while the designator is inside a container
End
```
