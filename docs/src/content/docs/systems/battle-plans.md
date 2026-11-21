---
title: "Battle plans"
description: "Battle plan bonuses for any object."
status: unverified
sidebar:
  order: 10
---

## BattlePlanBonusBehavior

**New in this fork.**

New module that can be used to customize the effects of BattlePlans for individual units. This module needs to go to the unit that receives the battle plan bonus effects, not the StrategyCenter.

Parameters:
```ini
Behavior = BattlePlanBonusBehavior ModuleTag_BPB
  ; Generic Upgrade parameters:  
  TriggeredBy = <Upgrade entry>
  ; ---
  StartsActive = No  ; Active immediately or requires upgrade?
  OverrideGlobalBonus = No  ; Enable to skip default BattlePlan bonus effects (i.e. values from BattlePlanUpdate, or WeaponBonus effects) for this unit
  ShouldParalyzeOnPlanChange = Yes  ; Should this unit be paralyzed/disabled while switching battle plans?
  ; -- Bonus entries -- Valid <BATTLE_PLAN_NAME> are BOMBARDMENT, SEARCH_AND_DESTROY and HOLD_THE_LINE
  WeaponSet = <BATTLE_PLAN_NAME> <WEAPON_SET_FLAG>  ; Set this weaponset flag while this battleplan is active
  ArmorSet = <BATTLE_PLAN_NAME> <WEAPON_SET_FLAG>  ; Set this armorset flag while this battleplan is active
  WeaponBonus = <BATTLE_PLAN_NAME> <WEAPON_BONUS_NAME>  ; Grant this weaponbonus while this battleplan is active
  ArmorDamageScalar = <BATTLE_PLAN_NAME> 1.0   ; Set damage reduction while this battleplan is active
  SightRangeScalar = <BATTLE_PLAN_NAME> 1.0  ; Set a sight range bonus while this battleplan is active
  MovementSpeedScalar = <BATTLE_PLAN_NAME> 1.0  ; Multiply movement speed while this battleplan is active (kept across locomotor set changes)
  StatusToSet = <BATTLE_PLAN_NAME> <STATUS_TYPE>   ; Set this status when enabling this battle plan. Clear it on battle plan removal
  StatusToClear = <BATTLE_PLAN_NAME> <STATUS_TYPE>  ; Clear this status when enabling this battle plan. Set it on battle plan removal
End
```

### Examples

**Example 1 - Give extra effects to Battle plans**
```ini
Behavior = BattlePlanBonusBehavior ModuleTag_BP1
  TriggeredBy = Upgrade_BattlePlanBonusExample
  OverrideGlobalBonus = No
  ShouldParalyzeOnPlanChange = Yes
  WeaponSet = BOMBARDMENT PLAYER_UPGRADE
  WeaponBonus = SEARCH_AND_DESTROY FRENZY_THREE
  StatusToClear = HOLD_THE_LINE CAN_STEALTH
  ArmorDamageScalar = HOLD_THE_LINE 0.9
End
```
This unit gains the regular battlePlan bonus effects with additional effects:
- With BOMBARDMENT we switch to the upgraded WeaponSet
- With SEARCH_AND_DESTROY we gain an extra weapon bonus
- With HoldTheLine we gain additional damage reduction, but lose the ability to stealth.

**Example 2 - Allow units to gain battle plans**

```ini
Behavior = BattlePlanBonusBehavior ModuleTag_BP0
  StartsActive = Yes
  OverrideGlobalBonus = Yes
  ConflictsWith = Upgrade_GrantBattlePlans
  ShouldParalyzeOnPlanChange = No
End
  
Behavior = BattlePlanBonusBehavior ModuleTag_BP1
  TriggeredBy = Upgrade_GrantBattlePlans
  OverrideGlobalBonus = No
  ShouldParalyzeOnPlanChange = Yes
End
```
This adds no additional effects, but makes the unit skip default BattlePlan effects (and paralyze) initially, and applies them only after the upgrade.
Note: For this to work, the unit needs to be inlcuded in the BattlePlan's valid KindOfs, even though the intention is to not give them the battlePlan effects by default.

## AllBattlePlansUpdate

**New in this fork.**

Grants the controlling player the army bonuses of **all three** battle plans (Bombardment, Hold the Line, Search & Destroy) while its object exists. It counts as an additional battle plan source next to the Strategy Centers, so the plan a Strategy Center runs, plan switching and the building-only plan effects are not touched. When the object is deleted the bonuses are removed again; if it is captured they move to the new owner.

```ini
Behavior = AllBattlePlansUpdate ModuleTag_AllPlans
  StrategyCenterTemplate = AmericaStrategyCenter   ; object whose BattlePlanUpdate provides the bonus values and member KindOfs
End
```

The bonus values (Hold the Line armor scalar, Search & Destroy sight scalar) and the valid/invalid member KindOfs are read from the `BattlePlanUpdate` of `StrategyCenterTemplate`, so they match the permanent plans exactly. Units with a `BattlePlanBonusBehavior` get their custom effects as usual.

Intended use: an `OCLSpecialPower` spawns an invisible marker object with this module and a `DeletionUpdate` for the duration. The special power enum `SPECIAL_ALL_BATTLE_PLANS` (behaves like the no-target CIA Intelligence power) is meant for the power. The marker can carry a second, never-fired power with `SPECIAL_ALL_BATTLE_PLANS_ACTIVE` that only shows the owner the remaining time (`PublicTimer` + `OwnerOnlyTimer` + `FlashingTimer`, see [SpecialPowers](/GeneralsGameCode_Modding/ini/special-power/#new-parser-fields)).

Example marker object (the power that creates it is a normal `OCLSpecialPower` with `Enum = SPECIAL_ALL_BATTLE_PLANS`):
```ini
Object SupW_StrategicEscalationMarker
  ; no Draw module, invisible
  EditorSorting = SYSTEM
  KindOf = IMMOBILE NO_COLLIDE UNATTACKABLE NO_SELECT INERT
  Body = InactiveBody ModuleTag_01
  End
  Behavior = AllBattlePlansUpdate ModuleTag_02
    StrategyCenterTemplate = SupW_AmericaStrategyCenter
  End
  Behavior = DeletionUpdate ModuleTag_03     ; power duration
    MinLifetime = 60000
    MaxLifetime = 60000
  End
  Behavior = OCLSpecialPower ModuleTag_04    ; never fired, only shows the remaining duration to the owner
    SpecialPowerTemplate = SupW_SpecialPowerStrategicEscalationActive
  End
End

SpecialPower SupW_SpecialPowerStrategicEscalationActive
  Enum = SPECIAL_ALL_BATTLE_PLANS_ACTIVE
  ReloadTime = 60000      ; = marker lifetime
  PublicTimer = Yes
  OwnerOnlyTimer = Yes
  FlashingTimer = Yes
  ShortcutPower = No
End
```
