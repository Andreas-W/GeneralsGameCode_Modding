---
title: "On-kill modules"
description: "Modules that act when the object kills something."
status: unverified
sidebar:
  order: 9
---

Two new behavior modules that trigger an effect when the object **kills** another object. Both share a common set of "kill condition" parameters (`KillMux`) that filter which kills count.

## Shared Kill Conditions (KillMux)

These parameters are available on both On-Kill modules below:

* `KilledKindOf = <KindOf list>` - (Only trigger when the killed victim matches these KindOfs.)
* `ForbiddenKilledKindOf = <KindOf list>` - (Do not trigger if the victim matches any of these KindOfs.)
* `RequiresAllKindOfs = No` - (If Yes, the victim must match all `KilledKindOf` entries instead of any.)
* `RequiredKilledStatus = <ObjectStatus list>` - (Victim must have these status flags.)
* `ForbiddenKilledStatus = <ObjectStatus list>` - (Victim must not have these status flags.)
* `KilledRelationship = <ALLIES | ENEMIES | NEUTRALS>` - (Only count kills of victims with this relationship to the killer.)
* `DamageTypes = <DamageType flags>` - (Only trigger if the killing blow used one of these damage types. NONE/ALL +X -Y notation.)
* `DeathTypes = <DeathType flags>` - (Only trigger for these death types.)
* `TriggerChance = 100%` - (Chance to trigger on a qualifying kill.)
* `CooldownTime = 0` - (Minimum time (ms) between triggers.)

## CreateObjectOnKillBehavior

**New in this fork.**

Spawns an OCL when the object kills a qualifying victim.
```ini
Behavior = CreateObjectOnKillBehavior ModuleTag_CreateOnKill
  StartsActive = Yes
  ; --- shared KillMux conditions (see above) ---
  KilledKindOf = INFANTRY
  ; --- module params ---
  CreationList = <OCL entry>          ; OCL spawned at the victim's location
  CreateAtKillerLocation = No         ; if Yes, spawn at the killer's position instead of the victim's
  CreateObjectForVictim = No          ; if Yes, the created object is owned by the victim's player instead of the killer's
End
```

## FireWeaponOnKillBehavior

**New in this fork.**

Fires a weapon at the victim when the object kills a qualifying target.
```ini
Behavior = FireWeaponOnKillBehavior ModuleTag_FireOnKill
  StartsActive = Yes
  ; --- shared KillMux conditions (see above) ---
  KilledKindOf = VEHICLE
  ; --- module params ---
  KillWeapon = <Weapon entry>         ; weapon fired at the victim on kill
End
```
