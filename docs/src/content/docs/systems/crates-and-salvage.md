---
title: "Crates and salvage"
description: "Upgrades from crates and salvage."
status: unverified
sidebar:
  order: 18
---

## AutoHealBehavior

Added parameters so that auto-healing can also grant salvage upgrades / promotions (e.g. a repair bay that "salvages" or veterans up units while healing them):
* `GrantSalvageUpgrade = No` - (If Yes, apply a salvage crate upgrade to the unit while it is being healed.)
* `GrantPromotion = No` - (If Yes, give the unit a veterancy level-up while it is being healed.)

## CrateApplyUpgrade

**New in this fork.**

An upgrade module that applies "crate"-style effects (salvage weapon/armor upgrades or a veterancy level-up) when triggered, reusing the salvage-crate logic.
* `SalvageCrate = No` - (If Yes, apply a salvage weapon/armor upgrade. Only affects units that are `KINDOF_WEAPON_SALVAGER` / `KINDOF_ARMOR_SALVAGER` and not already at the highest salvage tier.)
* `LevelUp = No` - (If Yes, grant a veterancy level-up, up to HEROIC, if the unit is trainable.)

## Crate.ini (CrateData)

* `AllowWater = No` - (If Yes, this salvage/pickup crate may be created on water. By default crates are only created on land. Default = No)
