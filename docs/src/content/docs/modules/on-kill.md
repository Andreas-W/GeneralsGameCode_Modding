---
title: "On-kill modules"
description: "Modules that act when the object kills something."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/OnKillModule.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/CreateObjectOnKillBehavior.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/CreateObjectOnKillBehavior.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/FireWeaponOnKillBehavior.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/FireWeaponOnKillBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Object.cpp
sidebar:
  order: 9
---

Two new behavior modules that trigger an effect when the object **kills** another object. Both share a common set of "kill condition" parameters that filter which kills count.

Both modules are also upgrade modules: they do nothing until they are activated, either with `StartsActive = Yes` (default = No) or by an upgrade in `TriggeredBy`. The generic upgrade parameters (`TriggeredBy`, `ConflictsWith`, `RemovesUpgrades`, `FXListUpgrade`, `RequiresAllTriggers`) work as usual.

## Shared Kill Conditions

These parameters are available on both On-Kill modules below. With none of them set, every kill of an enemy counts:

* `KilledKindOf = <KindOf list>` - (Only trigger when the killed victim matches these KindOfs. Default = no restriction.)
* `ForbiddenKilledKindOf = <KindOf list>` - (Do not trigger if the victim matches any of these KindOfs.)
* `RequiresAllKindOfs = Yes` - (If Yes (default), the victim must match all `KilledKindOf` entries; if No, any one of them is enough.)
* `RequiredKilledStatus = <ObjectStatus list>` - (Victim must have all of these status flags.)
* `ForbiddenKilledStatus = <ObjectStatus list>` - (Victim must not have any of these status flags.)
* `KilledRelationship = ENEMIES` - (Only count kills of victims with this relationship to the killer. Any combination of `ALLIES`, `ENEMIES`, `NEUTRALS`; default = `ENEMIES`.)
* `DamageTypes = ALL` - (Only trigger if the killing blow used one of these damage types. NONE/ALL +X -Y notation; default = ALL.)
* `DeathTypes = ALL` - (Only trigger for these death types. Same notation; default = ALL.)
* `TriggerChance = 100%` - (Chance to trigger on a qualifying kill. A failed roll does not start the cooldown.)
* `CooldownTime = 0` - (Minimum time (ms) between triggers. Kills during the cooldown are ignored.)

## CreateObjectOnKillBehavior

**New in this fork.**

Spawns an OCL when the object kills a qualifying victim.
```ini
Behavior = CreateObjectOnKillBehavior ModuleTag_CreateOnKill
  StartsActive = Yes
  ; --- shared kill conditions (see above) ---
  KilledKindOf = INFANTRY
  ; --- module params ---
  CreationList = <OCL entry>          ; OCL spawned at the victim's location
  CreateAtKillerLocation = No         ; if Yes, spawn at the killer's position instead of the victim's
  CreateObjectForVictim = No          ; if Yes, the created object is owned by the victim's player instead of the killer's
End
```

## FireWeaponOnKillBehavior

**New in this fork.**

Fires a weapon at the victim's position when the object kills a qualifying target.
```ini
Behavior = FireWeaponOnKillBehavior ModuleTag_FireOnKill
  StartsActive = Yes
  ; --- shared kill conditions (see above) ---
  KilledKindOf = VEHICLE
  ; --- module params ---
  KillWeapon = <Weapon entry>         ; weapon fired by the killer at the victim's position on kill
End
```
