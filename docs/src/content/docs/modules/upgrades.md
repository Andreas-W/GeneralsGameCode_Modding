---
title: "Upgrade modules"
description: "New and changed upgrade modules."
status: unverified
sidebar:
  order: 2
---

## WeaponSetUpgrade
Added new parameters for WeaponSetUpgrade
* `WeaponSetFlag = <WeaponSetFlag>` - define which Weaponset to enable (default = PLAYER_UPGRADE)
* `WeaponSetFlagsToClear = <WeaponSetFlag1> <WeaponSetFlag2> ..` - define which Weaponsets to disable
* `NeedsParkedAircraft = No` - Units with JetAIUpdate need to be parked in hangar to research this upgrade

**Notes:**
WeaponSetFlagsToClear can be used to switch between WeaponSets using multiple upgrades.
example:

```ini
  Behavior = WeaponSetUpgrade ModuleTag_04a
    WeaponSetFlag = PLAYER_UPGRADE
    WeaponSetFlagsToClear = PLAYER_UPGRADE2 PLAYER_UPGRADE3
    TriggeredBy = Upgrade_GLAWorkerFakeCommandSet
    RemovesUpgrades = Upgrade_GLAWorkerRealCommandSet Upgrade_GLAWorkerFakeCommandSet Test_Upgrade_DummyToggle
  End
  
  Behavior = WeaponSetUpgrade ModuleTag_04b
    WeaponSetFlag = PLAYER_UPGRADE2
    WeaponSetFlagsToClear = PLAYER_UPGRADE PLAYER_UPGRADE3
    TriggeredBy = Upgrade_GLAWorkerRealCommandSet
    RemovesUpgrades = Upgrade_GLAWorkerRealCommandSet Upgrade_GLAWorkerFakeCommandSet Test_Upgrade_DummyToggle
  End
  
  Behavior = WeaponSetUpgrade ModuleTag_04c
    WeaponSetFlag = PLAYER_UPGRADE3
    WeaponSetFlagsToClear = PLAYER_UPGRADE PLAYER_UPGRADE2
    TriggeredBy = Test_Upgrade_DummyToggle
    RemovesUpgrades = Upgrade_GLAWorkerRealCommandSet Upgrade_GLAWorkerFakeCommandSet Test_Upgrade_DummyToggle
  End
```
You can combine this with NeedsParkedAircraft to allow Jets to switch their ammo type when parked using multiple object upgrades.

## ArmorUpgrade
Added new parameters for ArmorUpgrade
* `ArmorSetFlag = <ArmorSetFlag>` - define which Armorset to enable (default = PLAYER_UPGRADE)
* `ArmorSetFlagsToClear = <ArmorSetFlag1> <ArmorSetFlag2> ..` - define which ArmorSets to disable

**Notes:**
ArmorSetFlagsToClear can be used to switch between ArmorSets using multiple upgrades (see WeaponSetUpgrade).

## LocomotorSetUpgrade
Added new parameters for LocomotorSetUpgrade
* `EnableUpgrade = No` - (if 'Yes' (default),  locomotor is switched to SET_NORMAL_UPGRADED; if 'No' locomotor is switched back to SET_NORMAL)
* `ExplicitLocomotorType = SET_SUPERSONIC` - Optional; explicitly switch to the defined locomotor set (This is not persistant, unlike EnableUpgrade)

**Notes:**
EnableUpgrade only allows for a single locomotor upgrade, but it can be toggled on/off with multiple upgrades.
With ExplicitLocomotorType, Locomotor sets can be set freely, but they will not persist if the unit changes its locomotor through other means (e.g. Aircraft taking off, or combat bike switching rider)

## MaxHealthUpgrade

Added new parameter for MaxHealthUpgrade

* `MultiplyMaxHealth = 1.0` - (multiplier on the unit's current HP; default = 1.0 -> no change.)

**Notes:**
* It is recommended to change all health upgrades to multiplicative as they will not be affected by other HP changes like veterancy, and can be freely stacked (and even reversed).
* You can still use AddMaxHealth for additive boni, and could even combine it with MultiplyMaxHealth. The formula is `NewHP = (CurrentHP * MultiplyMaxHealth) + AddMaxHealth`.

## CostModifierUpgrade

New Parameters added. Example:

```ini
Behavior = CostModifierUpgrade ModuleTag_123
  TriggeredBy = Upgrade_CostReduction  ; entry from upgrade.ini
  EffectKindOf = VEHICLE  ; Units with this KindOf will be affected
  Percentage = -20%  ; amount that cost will be reduced
  IsOneShotUpgrade = No  ; (NEW) Is this a one time global upgrade, or should it be removed when the object dies or changes owner?
  BonusStacksWith = DIFFERENT_VALUE  ; (NEW) Stacking behavior when multiple bonus sources exist
End
```

** BonusStacksWith types **
* `DIFFERENT_VALUE` - Default behavior: Does not stack with other bonus with the same value. Does stack with bonus with different value.
* `OTHER_TYPE` - Does stack with bonus from other type of unit with the same value. Does not stack with bonus from the same type of unit. I.e. Multiple OilRefineries would not stack, but a Refinery would stack with an IndustrialPlant if it had the same value.
* `SAME_TYPE` - Bonus from multiple sources of the same type will stack. I.e. Owning multiple Refineries grants additional bonus.

**Notes:**
- Bonus stacking is additive, not multiplicative

## ProductionTimeModifierUpgrade

**New in this fork.**

This works just like CostModifierUpgrade (Oil Refinery) to apply a global production time reduction for the given KindOf
example:

```ini
Behavior = ProductionTimeModifierUpgrade ModuleTag_123
  TriggeredBy = Upgrade_CostReduction  ; entry from upgrade.ini
  EffectKindOf = VEHICLE  ; Units with this KindOf will be affected
  Percentage = -20%  ; amount that build time will be reduced
  IsOneShotUpgrade = No  ; Is this a one time global upgrade, or should it be removed when the object dies or changes owner?
  BonusStacksWith = DIFFERENT_VALUE  ; Stacking behavior when multiple bonus sources exist
End
```

## ExperienceScalarUpgrade

~~Added parameter to make module active by default (useful for negativ scalars):~~
* ~~`StartsActive = False`~~

Update: removed this, because it didn't work. You will need to grant a dummy upgrade instead.


Added new parameter to modify XP value of a unit (i.e. XP that is gained for the enemy when the unit is killed)
* `AddXPValueScalar = 0.0` - Additive modifier for XP value scalar. I.e.: XPValueNew = XPValueOld * (1.0 + AddXPValueScalar).

* `SetMaxVeterancyLevel = <VeterancyLevel>` - (Raises the unit's maximum veterancy level (e.g. to `LEVEL_FOUR` or `LEVEL_FIVE`) when this upgrade is active. Lets you unlock the extra veterancy levels via an upgrade. See [Veterancy Object Parameters](/GeneralsGameCode_Modding/systems/veterancy/#veterancy-object-parameters).)

## UnitProductionBonusUpgrade

**New in this fork.**

This upgrade module allows to set a cost and/or build time modifier for individual types of units. This affects the whole player and not just individual factories.

Parameters:
* `<All common upgrade params; e.g. TriggeredBy>`
* `CostModifierPercentage = 0` - Percentage amount that the unit's costs are modified
* `BuildTimeModifierPercentage = 0` - Percentage amount that the unit's build time is modified
* `UnitTemplateName = <Name of an Object>` - The unit to apply this bonus to. (multiple lines are allowed)
* `IsOneShotUpgrade = No` - (If Yes, the bonus is applied once and is not tracked/removed - matches the old, pre-stacking behavior. If No (default), the bonus is now correctly removed if the upgrade module is deleted, and transferred to the new owner if the object is captured.)
* `BonusStacksWith = NO_STACKING` - (Controls stacking when the same unit type receives this bonus from multiple sources. Options: `NO_STACKING` (default; a second source does not add further bonus), `SAME_TYPE` (stacks with bonuses from any source, including the same upgrade/object type), `OTHER_TYPE` (stacks only with bonuses coming from a different source object type).)

**Example**
```ini
Behavior = UnitProductionBonusUpgrade ModuleTag_01
  TriggeredBy = Upgrade_CostReduction   ; The upgrade trigger
  CostModifierPercentage = -40%    ; Cost is reduced by 40%
  BuildTimeModifierPercentage = -80%    ; Build time is reduced by 80%
  UnitTemplateName = Tank_ChinaInfantryRedguard    ; The units this bonus applies to
  UnitTemplateName = Tank_ChinaTankBattleMaster
  BonusStacksWith = SAME_TYPE
End
```

**Notes**
- This upgrade is now removable and capture-aware (as long as `IsOneShotUpgrade = No`): the bonus goes away with the granting module and follows the object if it's captured by another player, similar to the CostModifierUpgrade module for Tech OilRefinery.

## PropagateUpgradeToContainedUpgrade

**New in this fork.**

An upgrade module that, when triggered, forwards one or more player upgrades onto every object currently contained (garrisoned/loaded) inside this object - so passengers with a matching `UpgradeModule` react as if they had been upgraded directly, without needing the upgrade to be player-wide.

* `UpgradeToPropagate = <Upgrade name> ...` - (One or more upgrade template names to grant to each currently-contained object. Multiple names can be listed.)

**Example**
```ini
Behavior = PropagateUpgradeToContainedUpgrade ModuleTag_01
  TriggeredBy = Upgrade_NationalismReactorBonus
  UpgradeToPropagate = Upgrade_Nationalism
End
```

## StealthUpgrade

Added new parameters
* `EnableStealth = Yes` - If set to No, this will disable the object's stealth instead of enabling it. Can be used for a toggleable upgrade.

* `OverrideStealthForbiddenConditions = <StealthForbiddenConditionFlags>` - Override StealthForbiddenConditions in StealthUpdate for this unit.

Note: if OverrideStealthForbiddenConditions is set, EnableStealth is ignored as there is no reason to use both functionalities at the same time.

## DelayedUpgradeBehavior

**New in this fork.**

Applies or Removes Upgrade(s) after a set time. Initially triggered by an upgrade. This can be used to make temporary upgrades.

Example:
```ini
Behavior = DelayedUpgradeBehavior ModuleTag_DelUp
  TriggeredBy = <Upgrade entry>   ; Initial Trigger
  ; additional generic Upgrade params (ConflictsWith, RemovesUpgrades, FXListUpgrade, RequiresAllTriggers)
  UpgradesToTrigger = <Upgrade1, Upgrade2, ...>   ; Upgrades that will be triggered after the delay
  UpgradesToRemove = <Upgrade1, Upgrade2, ...>    ; Upgrades that will be removed after the delay
  TriggerAfterTime = <time in msec>    ; Time delay
End
```
