---
title: "Energy shields"
description: "ShieldBody and EnergyShieldBehavior."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Body/ShieldBody.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/EnergyShieldBehavior.cpp
sidebar:
  order: 11
---

Added a Body and Behavior module for rechargable energy shields. This adds another health bar on top and absorbs damage until it's depleted.

## ShieldBody
This Body is required for shields to work. It is an `ActiveBody` with an extra shield health pool on top.

```ini
Body = ShieldBody ModuleTag_ES1
  ;<ActiveBody parameters>
  MaxHealth = 500
  StartsActive = No  ; Yes = the shield is up with full health from the start, without the upgrade from EnergyShieldBehavior (the shield model/subobject is not shown until the first recharge; prefer StartsActive on EnergyShieldBehavior)
  ShieldMaxHealth = 0  ; Health points of the shield
  ShieldMaxHealthPercent = 30%  ; Shield health as a part of MaxHealth. Computed when read, so put it after MaxHealth. Whichever of the two comes last wins.
  ShieldArmorSetFlag = PLAYER_UPGRADE  ; (Optional) ArmorSet flag set while the shield has health. Damage to the shield uses that armor; pass-through damage uses the armor without it.
  ShieldPassThroughDamageTypes = <Damage Types> ; Damage types that ignore the shield and damage the health directly. NONE +X / ALL -Y notation.
  DefaultShieldPassThroughDamageTypes = <Damage Types> ; Same, but predefined with the utility damage types that should always pass through (see below). Rarely needs to be changed.
End
```
NOTE: DefaultShieldPassThroughDamageTypes default values: NONE +STATUS +DEPLOY +UNRESISTABLE +HEALING +PENALTY +DISARM +HAZARD_CLEANUP +TOPPLING +SUBDUAL_UNRESISTABLE +CHRONO_UNRESISTABLE

When the object becomes disabled (e.g. EMP), an active shield drops to 0 and starts its recharge delay.

## EnergyShieldBehavior
The second required module for shield logic. It is an upgrade module: the shield is switched on (at full health) by `TriggeredBy` or `StartsActive`, and it takes the other upgrade fields (`ConflictsWith`, `RequiresAllTriggers`, ...).

```ini
Behavior = EnergyShieldBehavior ModuleTag_ES2
    StartsActive = No  ; Yes = enabled from the start (no upgrade required)
    TriggeredBy = <Upgrade Name>
    ShieldRechargeDelay = 0  ; Time (ms) without damage until the shield starts recharging.
    ShieldRechargeRate = 0  ; Time (ms) between shield recharge ticks
    ShieldRechargeAmount = 0  ; Shield health recharged per tick (absolute)
    ShieldRechargeAmountPercent = 0%  ; Shield health recharged per tick (part of the shield's max health). Used instead of ShieldRechargeAmount when set.
    ShieldHealthBarColor = R:255 G:255 B:255 A:255  ; RGBA color for the shield health bar
    ShieldHealthBarBackgroundColor = R:255 G:255 B:255 A:255  ; RGBA color for the shield health bar outline
    ShowHealthBarBackgroundWhenEmpty = No   ; Show the empty frame when the shield is depleted
    ShowHealthBarWhenUnselected = No ; Show the shield health bar when the unit is not selected or moused over
    ShieldModelCondition = <ConditionStateName>  ; Optional: condition state set while the shield is up
    ShieldSubObjectName = <SubObjName> ; Optional: model subobject shown while the shield is up
    ShieldHitModelCondition = <ConditionStateName> ; Optional: condition state set when the shield is hit
    ShieldHitSubObjectName = <SubObjName> ; Optional: model subobject shown when the shield is hit
    ShieldHitConditionDuration = 0  ; How long (ms) the hit state is shown; 0 = never
    ShieldDownFX = <FXList> ; Optional: FX played when the shield is depleted
    ShieldUpFX = <FXList> ; Optional: FX played when a depleted shield starts recharging
End
```

Note: with `ShieldRechargeDelay = 0` the shield never recharges, and with `ShieldRechargeRate = 0` it recharges only one tick after each delay. Give both a value.

Note: the hit state (`ShieldHit...`) and `ShieldDownFX` only work when `ShieldModelCondition` or `ShieldSubObjectName` is set.

**Note:** for the shield health bar to work, the object needs the KindOf `SHOW_PROGRESS_BAR`.

**Note:** when using both `ShieldHitModelCondition` and `ShieldModelCondition`, both condition states are active at the same time while the shield is hit. So a setup like this is required:

```ini
Draw = W3DModelDraw ModuleTag_ES0
  ; Inactive Shield
  DefaultConditionState
    Model = None
  End
		
  ; Active Energy Shield
  ConditionState = USER_1
    Model = ShieldActive
  End
	
  ; Energy Shield Hit
  ConditionState = USER_1 USER_2
    Model = ShieldHit
  End
End
```
