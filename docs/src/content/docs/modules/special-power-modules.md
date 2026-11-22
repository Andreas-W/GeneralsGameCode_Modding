---
title: "Special power modules"
description: "Special power modules and their update modules."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/SpecialPower/UpgradeSpecialPower.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/SpecialPower/OCLSpecialPower.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/TeleporterAIUpdate.cpp
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

A player upgrade is given to the player; an object upgrade to the object. For a power used on a target object, the target gets the object upgrade (and a player upgrade goes to the target's owner). A power used on a location grants nothing.

## OCLSpecialPower

Added new parameter:
* `SelectCreatedObject = No` - The (first) created object will be automatically selected by the player

* `MinDistToSimilarRadius = 0.0` - (If > 0, a special power without a target (used at the object's own position) does nothing while an object of the `ReferenceObject` type, of any owner, is within this radius; the player hears the "can't do that" sound. Useful to prevent stacking multiple deployments on top of each other. Needs `ReferenceObject`. Default = 0 = no limit)

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
   MinDisabledDuration = 0  ; base recovery time in ms, added to the distance based part
   DisabledDurationPerDistance = 10.0 ; How long the unit will need to recover, linear to the distance (in MS per distance). Recommended values = 5-10. 
   TeleportRecoverSoundAmbient = [AudioEvent entry] ; Ambient sound played on the unit while it recovers from a teleport.
   TeleportRecoverOpacityStart = 10%  ; Unit opacity when it starts to recover (default = 100%).
   TeleportRecoverOpacityEnd = 80%  ; Unit opacity when it has finished recovering (default = 100%).
   TeleportRecoverTint = TELEPORT_RECOVER  ; Color tint status to apply while recovering (default = none)
   [entries from AIUpdate]
End
```
When a unit is recovering from a teleport, the conditionstate `TELEPORT_RECOVER` is set. Note: In most cases, the unit will also be MOVING after a teleport, so it's recommended to define both states.
