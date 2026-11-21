---
title: "Target designator"
description: "SpecialPowerDesignatorUpdate."
status: unverified
sidebar:
  order: 17
---

## SpecialPowerDesignatorUpdate

**New in this fork.**

Update module that lets a unit "designate" an area, enabling special powers that require a designator (see [`NeedsTargetDesignator`](/GeneralsGameCode_Modding/ini/special-power/#new-parser-fields) on the SpecialPower). While active, it can show a radius decal and optionally apply a temporary status to objects in the area.

```ini
Behavior = SpecialPowerDesignatorUpdate ModuleTag_Designator
  SpecialPowerTemplate = <SpecialPower entry>   ; the power that this designator enables
  DesignatorRadius = 0.0       ; radius of the designated area
  AlwaysShowDecal = No         ; always show the radius decal, even when the power is not ready
  TriggerStatusType = <ObjectStatus>   ; status applied to objects in the designated area
  TriggerStatusTime = 0        ; how long (ms) the status stays on after leaving the area
  TriggerFX = <FXList>         ; FX played when the designator triggers
End
```
