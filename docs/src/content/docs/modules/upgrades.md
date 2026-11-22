---
title: "Upgrade modules"
description: "New and changed upgrade modules."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/WeaponSetUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/ArmorUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/LocomotorSetUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/MaxHealthUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/CostModifierUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/CostModifierUpgrade.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/ProductionTimeModifierUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/ExperienceScalarUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/UnitProductionBonusUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/PropagateUpgradeToContainedUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/StealthUpgrade.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/DelayedUpgradeBehavior.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/DelayedUpgradeBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Upgrade/GeometryUpgrade.cpp
sidebar:
  order: 2
---

## WeaponSetUpgrade
Added new parameters for WeaponSetUpgrade
* `WeaponSetFlag = <WeaponSetFlag>` - define which Weaponset to enable (default = PLAYER_UPGRADE; `NONE` sets no flag, so the module only clears `WeaponSetFlagsToClear`)
* `WeaponSetFlagsToClear = <WeaponSetFlag1> <WeaponSetFlag2> ..` - define which Weaponsets to disable
* `NeedsParkedAircraft = No` - If Yes, the upgrade only applies to units with JetAIUpdate while they are parked in a hangar

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
* `ArmorSetFlag = <ArmorSetFlag>` - define which Armorset to enable (default = PLAYER_UPGRADE; `NONE` is not accepted here)
* `ArmorSetFlagsToClear = <ArmorSetFlag1> <ArmorSetFlag2> ..` - define which ArmorSets to disable

**Notes:**
ArmorSetFlagsToClear can be used to switch between ArmorSets using multiple upgrades (see WeaponSetUpgrade).

## LocomotorSetUpgrade
Added new parameters for LocomotorSetUpgrade
* `EnableUpgrade = No` - (if 'Yes' (default),  locomotor is switched to SET_NORMAL_UPGRADED; if 'No' locomotor is switched back to SET_NORMAL)
* `ExplicitLocomotorType = SET_SUPERSONIC` - Optional (default = none); explicitly switch to the defined locomotor set (This is not persistent, unlike EnableUpgrade). Names: `SET_NORMAL`, `SET_NORMAL_UPGRADED`, `SET_FREEFALL`, `SET_WANDER`, `SET_PANIC`, `SET_TAXIING`, `SET_SUPERSONIC`, `SET_SLUGGISH`, `SET_VTOL`. `SET_NORMAL_UPGRADED` (or `None`) falls back to the `EnableUpgrade` behavior

**Notes:**
EnableUpgrade only allows for a single locomotor upgrade, but it can be toggled on/off with multiple upgrades.
With ExplicitLocomotorType, Locomotor sets can be set freely, but they will not persist if the unit changes its locomotor through other means (e.g. Aircraft taking off, or combat bike switching rider)

## MaxHealthUpgrade

Added new parameter for MaxHealthUpgrade

* `MultiplyMaxHealth = 1.0` - (multiplier on the unit's current maximum health; default = 1.0 -> no change. How current health follows is set by the existing `ChangeType`.)

**Notes:**
* It is recommended to change all health upgrades to multiplicative as they will not be affected by other HP changes like veterancy, and can be freely stacked (and even reversed).
* You can still use AddMaxHealth for additive boni, and could even combine it with MultiplyMaxHealth. The formula is: new max health = (current max health * MultiplyMaxHealth) + AddMaxHealth.

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

~~Added parameter to make module active by default (useful for negative scalars):~~
* ~~`StartsActive = No`~~

Update: this did not work reliably; grant a dummy upgrade instead. (The field is still accepted by the parser.)


Added new parameter to modify XP value of a unit (i.e. XP that is gained for the enemy when the unit is killed)
* `AddXPValueScalar = 0.0` - Added to the object's XP value scalar (which starts at 1.0). I.e. with no other changes: XPValueNew = XPValueOld * (1.0 + AddXPValueScalar).

* `SetMaxVeterancyLevel = <VeterancyLevel>` - (Sets the unit's maximum veterancy level (e.g. to `LEVEL_FOUR` or `LEVEL_FIVE`) when this upgrade is active. Lets you unlock the extra veterancy levels via an upgrade. See [Veterancy Object Parameters](/GeneralsGameCode_Modding/systems/veterancy/#veterancy-object-parameters).)

## UnitProductionBonusUpgrade

**New in this fork.**

This upgrade module allows to set a cost and/or build time modifier for individual types of units. This affects the whole player and not just individual factories.

Parameters:
* `<All common upgrade params; e.g. TriggeredBy>`
* `CostModifierPercentage = 0` - Percentage amount that the unit's costs are modified
* `BuildTimeModifierPercentage = 0` - Percentage amount that the unit's build time is modified
* `UnitTemplateName = <Name of an Object>` - The unit to apply this bonus to. (multiple lines are allowed)
* `IsOneShotUpgrade = No` - (If Yes, the bonus is applied once and is not tracked/removed - matches the old, pre-stacking behavior. If No (default), the bonus is now correctly removed if the upgrade module is deleted, and transferred to the new owner if the object is captured.)
* `BonusStacksWith = DIFFERENT_VALUE` - (Controls stacking when the same unit type receives this bonus from multiple sources; same values as for CostModifierUpgrade. `DIFFERENT_VALUE` (default; a second source with the same percentage adds no further bonus, a different percentage does), `OTHER_TYPE` (same percentages stack only when they come from different source object types), `SAME_TYPE` (every source stacks, including several objects of the same type).)

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

An upgrade module that, when triggered, gives one or more object upgrades to every object currently contained (garrisoned/loaded) inside this object - so passengers with upgrade modules triggered by that upgrade react as if they had been upgraded directly. The object needs a Contain module. A rider that is kept outside the normal passenger list (e.g. the portable structure of a HelixContain) gets it too.

* `UpgradeToPropagate = <Upgrade name> ...` - (One or more upgrade names to grant to each currently-contained object, on one line separated by spaces. Only upgrades with `Type = OBJECT` are propagated; player upgrades are skipped (with an assert in debug builds). Riders that already have the upgrade or have no module reacting to it are skipped. Objects that enter later do not get it.)

**Example**
```ini
Behavior = PropagateUpgradeToContainedUpgrade ModuleTag_01
  TriggeredBy = Upgrade_GLAHindGroundAttackLoadout   ; object upgrade
  UpgradeToPropagate = Upgrade_GLAHindGroundAttackLoadout
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
  StartsActive = No   ; If Yes, the module triggers itself when the object is created
  UpgradesToTrigger = <Upgrade1> <Upgrade2> ...   ; Upgrades that will be granted after the delay (player or object upgrades)
  UpgradesToRemove = <Upgrade1> <Upgrade2> ...    ; Upgrades that will be removed after the delay (player or object upgrades)
  TriggerAfterTime = <time in msec>    ; Time delay (default = 0)
End
```

Notes:
- Upgrade names are separated by spaces; commas are not separators.
- With `TriggerAfterTime = 0` (the default) nothing is triggered or removed.
- It triggers once; it can trigger again only after the triggering upgrade was removed and given again.
- An unknown upgrade name throws an INI error when the delay ends.

## GeometryUpgrade

**New in this fork.**
Replaces the object's whole geometry when the upgrade is given: the main shape and all [additional shapes](/GeneralsGameCode_Modding/objects/object-definition/#compound-geometry). Collisions, the cells the object covers and the pathfinding footprint follow at once.

* `Geometry` - (A block up to `End` with the new geometry, written like on an Object: `Geometry`, `GeometryMajorRadius`, `GeometryMinorRadius`, `GeometryHeight`, `GeometryIsSmall` and any number of `AdditionalGeometry` blocks. A field left out takes its default, not the object's value.)

```ini
Behavior = GeometryUpgrade ModuleTag_20
  TriggeredBy = Upgrade_MyBaseExtension
  Geometry
    Geometry = BOX
    GeometryMajorRadius = 40
    GeometryMinorRadius = 20
    GeometryHeight = 30
    AdditionalGeometry
      Type        = BOX
      MajorRadius = 15
      MinorRadius = 30
      Height      = 20
      Offset      = X:25 Y:50 Z:0
    End
  End
End
```

Notes:
- Removing the upgrade does not bring the old geometry back; a second `GeometryUpgrade` with the old geometry does.
- With a `DamageStateGeometryBehavior` on the object, a damage state geometry in use stays until the object is healed; the upgraded geometry then becomes the undamaged one.
