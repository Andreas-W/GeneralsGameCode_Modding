---
title: "Crates and salvage"
description: "Upgrades from crates and salvage."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/AutoHealBehavior.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/AutoHealBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/CrateApplyUpgrade.cpp
  - Core/GameEngine/Source/GameLogic/System/CrateSystem.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Die/CreateCrateDie.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/System/KindOf.cpp
sidebar:
  order: 18
---

## AutoHealBehavior

Added parameters so that auto-healing can also grant salvage upgrades / promotions (e.g. a repair bay that "salvages" or veterans up units while healing them):
* `GrantSalvageUpgrade = No` - (If Yes, each heal pulse applies one salvage crate tier to the unit: units with `KindOf = ARMOR_SALVAGER` get the next armor tier, otherwise units with `WEAPON_SALVAGER` get the next weapon tier, until the second tier is reached.)
* `GrantPromotion = No` - (If Yes, each heal pulse gives a trainable unit one veterancy level, up to HEROIC. Used only when no salvage tier was granted in that pulse.)

With either of them set, units at full health are not healed, but still receive the grants.

## CrateApplyUpgrade

**New in this fork.**

An upgrade module that applies "crate"-style effects (salvage weapon/armor upgrades or a veterancy level-up) when triggered, reusing the salvage-crate logic.
* `SalvageCrate = No` - (If Yes, apply one salvage armor or weapon tier, like picking up a salvage crate. Only affects units with `KindOf = ARMOR_SALVAGER` / `WEAPON_SALVAGER` that are not already at the second tier; armor comes first.)
* `LevelUp = No` - (If Yes, grant a veterancy level-up, up to HEROIC, if the unit is trainable. Used only when no salvage tier applies.)

The upgrade counts as not applicable (`wouldUpgrade` is false) when none of its effects can apply any more.

## Crate.ini (CrateData)

* `AllowWater = No` - (If Yes, a crate of this `CrateData` may be created on water. By default crates are only created on land. Default = No)
