---
title: "Veterancy"
description: "Extra veterancy levels and rider changes."
status: verified
verifiedAt: 7cdd826c5
sources:
  - Core/GameEngine/Source/Common/System/GameCommon.cpp
  - Core/GameEngine/Include/Common/GameCommon.h
  - GeneralsMD/Code/GameEngine/Source/Common/Thing/ThingTemplate.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/ExperienceScalarUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/RiderChangeContain.cpp
sidebar:
  order: 19
---

## Veterancy Object Parameters

With the two extra veterancy levels (`LEVEL_FOUR`, `LEVEL_FIVE`; see [New Enum Definitions](/GeneralsGameCode_Modding/reference/enums/#veterancy-levels)), the per-object veterancy parameters now support up to 6 levels:

* `MaxVeterancyLevel = HEROIC` - (The highest veterancy level this object may ever reach. Default = `HEROIC` (vanilla behavior). Set to `LEVEL_FOUR` / `LEVEL_FIVE` to allow the extra levels.)
* `ExperienceRequired = <lvl0> <lvl1> ... <lvl5>` - (XP required to reach each level. Accepts up to 6 values now. Levels left out are unreachable, so an object only climbs into `LEVEL_FOUR` / `LEVEL_FIVE` through experience when values are given for them.)
* `ExperienceValue = <lvl0> <lvl1> ... <lvl5>` - (XP granted to the killer when this object is destroyed, per level. Up to 6 values. Levels left out use the last value given.)
* `SkillPointValue = <lvl0> <lvl1> ... <lvl5>` - (Skill points granted per level. Up to 6 values. Levels left out use the `ExperienceValue` of that level, as in retail.)

Notes:
* Health bonuses for the new levels are set in GameData via `HealthBonus_Four` / `HealthBonus_Five` (percent, like `HealthBonus_Heroic`).
* The maximum level can also be raised at runtime via the [`SetMaxVeterancyLevel`](/GeneralsGameCode_Modding/modules/upgrades/#experiencescalarupgrade) upgrade.

## RiderChangeContain

Extended from 8 to 16 rider slots. Additional slots `Rider9` through `Rider16` can now be defined, using the same syntax as `Rider1`-`Rider8`. Matching enums were added for the new slots: `WEAPON_RIDER9`-`WEAPON_RIDER16` (WeaponSet), `RIDER9`-`RIDER16` (ModelCondition) and `STATUS_RIDER9`-`STATUS_RIDER16` (ObjectStatus) (see [New Enum Definitions](/GeneralsGameCode_Modding/reference/enums/#rider-slots-combat-bike)).

A rider line takes an optional armor set as its last token (`ARMOR_RIDER1`-`ARMOR_RIDER16`). It is set while that rider is inside and cleared when it leaves; existing lines without it stay valid.
```ini
Rider9 = <RiderTemplate> RIDER9 WEAPON_RIDER9 STATUS_RIDER9 <CommandSet> SET_NORMAL ARMOR_RIDER9
```
