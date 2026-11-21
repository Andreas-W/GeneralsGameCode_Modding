---
title: "Special power modules"
description: "Special power modules and their update modules."
status: unverified
sidebar:
  order: 7
---

## UpgradeSpecialPower

**New in this fork.**

Trigger an upgrade via a special power. This can be used to (e.g. in combination with DelayedUpgradeBehavior) to create a temporary upgrade with a cooldown.

Example:
```ini
Behavior = UpgradeSpecialPower ModuleTag_SpecUp01
  SpecialPowerTemplate = <SpecialPower Entry>
  ; Other params from SpecialPower module (UpdateModuleStartsAttack, StartsPaused, InitiateSound, ScriptedSpecialPowerOnly)
  UpgradeToGrant = <Upgrade Entry>
End
```

## OCLSpecialPower

Added new parameter:
*`SelectCreatedObject = No` The (first) created object will be automatically selected by the player

* `MinDistToSimilarRadius = 0.0` - (If > 0, the special power cannot be targeted within this radius of another object created by the same OCL special power. Useful to prevent stacking multiple deployments on top of each other. Default = 0 = no limit)

## TeleporterAIUpdate

**New in this fork (experimental).**
** Warning: This module is experimental, i.e. it is not fully tested and might undergo revisions**

AIUpdate module designed to work like a Chrono Legionnaire in RA2. I.e. The unit teleports instead of moving normally, and needs to recharge depending on the distance it teleported.
** Note: Currently this module is only working properly for infantry units. It currently does not work properly for vehicles and it is not certain it ever will.**

Example:
```ini
Behavior = TeleporterAIUpdate ModuleTag_123
   TeleportStartFX = [FXList entry] ; FX to play at the unit's previous position when it teleports
   TeleportTargetFX = [FXList entry] ; FX to play at the unit's target position when it teleports
   TeleportRecoverEndFX = [FXList entry] ; FX to play on the unit, when it has finished recovering
   MinDistanceForTeleport = 20.0  ; distance in which the unit moves normally instead of teleporting
   DisabledDurationPerDistance = 10.0 ; How long the unit will need to recover, linear to the distance (in MS per distance). Recommended values = 5-10. 
   TeleportRecoverSoundAmbient = [AudioEvent entry] ; Ambient sound played on the unit while it recovers from a teleport.
   TeleportRecoverOpacityStart = 10%  ; Unit maximum transparency when it starts to recover.
   TeleportRecoverOpacityEnd = 80%  ; Unit minimum transparency when it has finished recovering.
   TeleportRecoverTint = TELEPORT_RECOVER  ; Color tint status to apply while recovering
   [entries from AIUpdate]
End
```
When a unit is recovering from a teleport, the conditionstate `TELEPORT_RECOVER` is set. Note: In most cases, the unit will also be MOVING after a teleport, so it's recommended to define both states.
