---
title: "Energy shields"
description: "ShieldBody and EnergyShieldBehavior."
status: unverified
sidebar:
  order: 11
---

## Energy Shields
Added a Body and Behavior module for rechargable energy shields. This adds another health bar on top and absorbs damage until it's depleted.

### ShieldBody
This Body is required for shields to work.

```ini
Body = ShieldBody ModuleTag_ES1
  ;<ActiveBody paramters>
  StartsActive = No  ; Enable Shields by default; Otherwise an upgrade is required (defined in EnergyShieldBehavior)
  ShieldMaxHealth = 100  ; Health points of the shield (only needed if ShieldMaxHealthPercent is not set)
  ShieldMaxHealthPercent = 30%  ; Health points relative to MaxHealth (overrides ShieldMaxHealth)
  ShieldArmorSetFlag = PLAYER_UPGRADE  ; (Optional) ArmorSetFlag to use while the shield is up
  ShieldPassThroughDamageTypes = <Damage Types> ; List of damage types that will go through the shield and damage the Health. NONE +X, ALL -Y notation.
  DefaultShieldPassThroughDamageTypes = <Damage Types> ; This has the same functionalty as above, but is predefined with all utility damage types that should always be passed through. This should not be defined in most cases, but can be overriden
End
```
NOTE: DefaultShieldPassThroughDamageTypes default values: NONE +STATUS +DEPLOY +UNRESISTABLE +HEALING +PENALTY +DISARM +HAZARD_CLEANUP +TOPPLING +SUBDUAL_UNRESISTABLE +CHRONO_UNRESISTABLE

### EnergyShieldBehavior
The second required module for shield logic

```ini
Behavior = EnergyShieldBehavior ModuleTag_ES2
    StartsActive = No  ; Enabled by default (no upgrade required)
    TriggeredBy = <Upgrade Name>
    ShieldRechargeDelay = 5000  ; Time (ms) of no damage taken, until shield starts recharging.
    ShieldRechargeRate = 100  ; Delay (ms) between shield recharge ticks
    ShieldRechargeAmount = 0  ; How much shield to recharge per tick (Absolute. Use only one)
    ShieldRechargeAmountPercent = 2%  ; How much shield to recharge per tick (Percentage. Use only one)
    ShieldHealthBarColor = R:128 G:255 B:255 A:255  ; RGBA color for the shield health bar
    ShieldHealthBarBackgroundColor = R:0 G:0 B:0 A:255  ; RGBA color for the shield health bar outline
    ShowHealthBarBackgroundWhenEmpty = No   ; Show empty frame when shield is depleted
    ShowHealthBarWhenUnselected = No ; Show shield health bar when the unit is not selected or mousover
    ShieldModelCondition = <ConditionStateName>  ; Optional: Conditionstate to set when shield is active
    ShieldSubObjectName = <SubObjName> ; Optional: Model subobject to show when the shield is active
    ShieldHitModelCondition = <ConditionStateName> ; Optional: Conditionstate to set when shield was hit
    ShieldHitSubObjectName = <SubObjName> ; Optional: Model subobject to show when the shield was hit
    ShieldHitConditionDuration = 500  ; Optional: How long to show conditionstate when shield was hit
    ShieldDownFX = <FXList> ; Optional: FX to play when shield is depleted
    ShieldUpFX = <FXList> ; Optional: FX to play when a depleted shield starts recharging
End
```

** Note: for the shield health bar to work, the KindOf "SHOW_PROGRESS_BAR" is required on the object** 

** Note2: When using both ShieldHitModelCondition and ShieldModelCondition parameters: When the shield is hit, both conditionstates are active at the same time. So a setup like this is required:

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
