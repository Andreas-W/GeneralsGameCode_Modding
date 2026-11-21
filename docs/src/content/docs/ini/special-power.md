---
title: "SpecialPower.ini"
description: "New special power fields, Eva events and the Kodiak power."
status: unverified
sidebar:
  order: 29
---

## SpecialPower.ini

### New Special Power Enums:
* SPECIAL_ION_CANNON
* SPECIAL_CLUSTER_MISSILE
* SPECIAL_SUNSTORM_MISSILE
* SPECIAL_METEOR_STRIKE
* SPECIAL_PUNISHER_CANNON
* SPECIAL_CHEMICAL_MISSILE
* SPECIAL_CHRONOSPHERE
* SPECIAL_RIFT_GENERATOR
* SPECIAL_ALL_BATTLE_PLANS - behaves like the no-target CIA Intelligence power; meant for [AllBattlePlansUpdate](/GeneralsGameCode_Modding/systems/battle-plans/#allbattleplansupdate)
* SPECIAL_ALL_BATTLE_PLANS_ACTIVE - display-only power for the remaining time of the above

### New Parser Fields:
`BehaviorEnum = <OTHER ENUM>`  
Used to set how a new SpecialPower behaves, new enums will be like "SPECIAL_NEUTRON_MISSILE", with this field another behavior can be set, there is a lot of hardcoded stuff in the exe.

`EvaDetectedOwn = <Name of eva event>`  
`EvaDetectedAlly = <Name of eva event>`  
`EvaDetectedEnemy = <Name of eva event>`  
`EvaLaunchedOwn = <Name of eva event>`  
`EvaLaunchedAlly = <Name of eva event>`  
`EvaLaunchedEnemy = <Name of eva event>`  
`EvaReadyOwn = <Name of eva event>`  
`EvaReadyAlly = <Name of eva event>`  
`EvaReadyEnemy = <Name of eva event>`  
These allow to specify eva events for a special power. When not set, vanilla special power still have their hardcoded events. When set, only the given event plays (the hardcoded detected/launched events of the vanilla superweapons are no longer played in addition).

`OwnerOnlyTimer = No`  
Used with `PublicTimer = Yes`: the countdown is shown only to the player who owns the power. Such a timer is also shown for objects that are not structures, e.g. a helper object that shows its owner how long a temporary effect still lasts. Default = No.

`FlashingTimer = No`  
Used with `PublicTimer = Yes`: the countdown is drawn like a ready timer (ready font, flashing) the whole time, e.g. while an effect is active. Default = No.

`Cost = 0`  
Optional credits cost to activate the special power. When set (> 0), the player must have enough money, and the cost is deducted when the power is fired. The command button is disabled/greyed out when the player cannot afford it, and the cost is shown in the button tooltip. Default = 0 (free).

`NeedsTargetDesignator = No`  
When Yes, this special power can only be fired while a valid target designator is active for the player (see the [SpecialPowerDesignatorUpdate](/GeneralsGameCode_Modding/systems/target-designator/#specialpowerdesignatorupdate) module). Used to gate a superweapon behind a "designate target" unit/ability. Default = No.

`StartsReady = No`  
If Yes, the special power is ready to fire immediately once available (no initial reload wait). Default = No (normal reload-on-creation behavior).

#### Code Example:
```ini
SpecialPower SupW_SpecialPowerChronoSphere
  Enum = SPECIAL_CHRONOSPHERE ; new Enum constant
  ReloadTime = 240000     ; in milliseconds
  PublicTimer = Yes
  InitiateAtLocationSound = ChronoSphereTargetInitiateSound
  SharedSyncedTimer = No
  RadiusCursorRadius = 50
  ShortcutPower = Yes     ; Capable of being fired by the side-bar shortcut.
  AcademyClassify = ACT_SUPERPOWER     ; Considered a powerful special power that a player could fire. Not for simpler unit based powers.
  BehaviorEnum = SPECIAL_SNEAK_ATTACK ; behave like sneak attack, required for chronosphere
  EvaDetectedEnemy = SUPERWEAPONDETECTED_ENEMY_CHRONOSPHERE ; Vanilla superweapons have these 5 eva events, define events in Eva.ini
  EvaLaunchedOwn = SUPERWEAPONLAUNCHED_OWN_CHRONOSPHERE
  EvaLaunchedAlly = SUPERWEAPONLAUNCHED_ALLY_CHRONOSPHERE
  EvaLaunchedEnemy = SUPERWEAPONLAUNCHED_ENEMY_CHRONOSPHERE
  EvaReadyOwn = SUPERWEAPONREADY_OWN_CHRONOSPHERE
End
```

## CommandButton.ini

### Multi-Target Special Powers

Added CommandButton parameters to let a special power be aimed at **multiple target points** in one activation (used together with the [MultiLocationSpecialPowerUpdate](/GeneralsGameCode_Modding/systems/superweapons/#multilocationspecialpowerupdate) module). The player places an anchor and then several target points.

* `NumberOfTargets = 1` - (How many target points the player must place for this power.)
* `TargetRadius = 0.0` - (Radius cursor shown at each target point.)
* `TargetDecalRadius = 0.0` - (Radius of the decal drawn at each placed target point.)
* `TargetRadiusMode = <mode>` - (How the target radius is interpreted/displayed.)
* `AnchorRadius = 0.0` - (Radius around the initial anchor point within which targets must be placed.)
* `AnchorDecalRadius = 0.0` - (Radius of the decal drawn at the anchor point.)
* `AnchorRadiusCursorType = <RadiusCursor>` - (Radius cursor type shown for the anchor.)
* `MarkerObject = <ObjectTemplate>` - (Object placed as a visual marker at each confirmed target point.)
* `MarkerFX = <FXList>` - (FXList played at each confirmed target point.)
* `SecondCursorName = <cursor>` - (Mouse cursor used while placing the additional target points, after the anchor.)

## Eva.ini

### New Eva events:

* SUPERWEAPONDETECTED_OWN_ION_CANNON
* SUPERWEAPONDETECTED_ALLY_ION_CANNON
* SUPERWEAPONDETECTED_ENEMY_ION_CANNON
* SUPERWEAPONLAUNCHED_OWN_ION_CANNON
* SUPERWEAPONLAUNCHED_ALLY_ION_CANNON
* SUPERWEAPONLAUNCHED_ENEMY_ION_CANNON
* SUPERWEAPONREADY_OWN_ION_CANNON
* SUPERWEAPONREADY_ALLY_ION_CANNON
* SUPERWEAPONREADY_ENEMY_ION_CANNON

* SUPERWEAPONDETECTED_OWN_CLUSTER_MISSILE
* SUPERWEAPONDETECTED_ALLY_CLUSTER_MISSILE
* SUPERWEAPONDETECTED_ENEMY_CLUSTER_MISSILE
* SUPERWEAPONLAUNCHED_OWN_CLUSTER_MISSILE
* SUPERWEAPONLAUNCHED_ALLY_CLUSTER_MISSILE
* SUPERWEAPONLAUNCHED_ENEMY_CLUSTER_MISSILE
* SUPERWEAPONREADY_OWN_CLUSTER_MISSILE
* SUPERWEAPONREADY_ALLY_CLUSTER_MISSILE
* SUPERWEAPONREADY_ENEMY_CLUSTER_MISSILE

* SUPERWEAPONDETECTED_OWN_SUNSTORM_MISSILE
* SUPERWEAPONDETECTED_ALLY_SUNSTORM_MISSILE
* SUPERWEAPONDETECTED_ENEMY_SUNSTORM_MISSILE
* SUPERWEAPONLAUNCHED_OWN_SUNSTORM_MISSILE
* SUPERWEAPONLAUNCHED_ALLY_SUNSTORM_MISSILE
* SUPERWEAPONLAUNCHED_ENEMY_SUNSTORM_MISSILE
* SUPERWEAPONREADY_OWN_SUNSTORM_MISSILE
* SUPERWEAPONREADY_ALLY_SUNSTORM_MISSILE
* SUPERWEAPONREADY_ENEMY_SUNSTORM_MISSILE

* SUPERWEAPONDETECTED_OWN_METEOR_STRIKE
* SUPERWEAPONDETECTED_ALLY_METEOR_STRIKE
* SUPERWEAPONDETECTED_ENEMY_METEOR_STRIKE
* SUPERWEAPONLAUNCHED_OWN_METEOR_STRIKE
* SUPERWEAPONLAUNCHED_ALLY_METEOR_STRIKE
* SUPERWEAPONLAUNCHED_ENEMY_METEOR_STRIKE
* SUPERWEAPONREADY_OWN_METEOR_STRIKE
* SUPERWEAPONREADY_ALLY_METEOR_STRIKE
* SUPERWEAPONREADY_ENEMY_METEOR_STRIKE

* SUPERWEAPONDETECTED_OWN_PUNISHER_CANNON
* SUPERWEAPONDETECTED_ALLY_PUNISHER_CANNON
* SUPERWEAPONDETECTED_ENEMY_PUNISHER_CANNON
* SUPERWEAPONLAUNCHED_OWN_PUNISHER_CANNON
* SUPERWEAPONLAUNCHED_ALLY_PUNISHER_CANNON
* SUPERWEAPONLAUNCHED_ENEMY_PUNISHER_CANNON
* SUPERWEAPONREADY_OWN_PUNISHER_CANNON
* SUPERWEAPONREADY_ALLY_PUNISHER_CANNON
* SUPERWEAPONREADY_ENEMY_PUNISHER_CANNON

* SUPERWEAPONDETECTED_OWN_CHEMICAL_MISSILE
* SUPERWEAPONDETECTED_ALLY_CHEMICAL_MISSILE
* SUPERWEAPONDETECTED_ENEMY_CHEMICAL_MISSILE
* SUPERWEAPONLAUNCHED_OWN_CHEMICAL_MISSILE
* SUPERWEAPONLAUNCHED_ALLY_CHEMICAL_MISSILE
* SUPERWEAPONLAUNCHED_ENEMY_CHEMICAL_MISSILE
* SUPERWEAPONREADY_OWN_CHEMICAL_MISSILE
* SUPERWEAPONREADY_ALLY_CHEMICAL_MISSILE
* SUPERWEAPONREADY_ENEMY_CHEMICAL_MISSILE

* SUPERWEAPONDETECTED_OWN_CHRONOSPHERE
* SUPERWEAPONDETECTED_ALLY_CHRONOSPHERE
* SUPERWEAPONDETECTED_ENEMY_CHRONOSPHERE
* SUPERWEAPONLAUNCHED_OWN_CHRONOSPHERE
* SUPERWEAPONLAUNCHED_ALLY_CHRONOSPHERE
* SUPERWEAPONLAUNCHED_ENEMY_CHRONOSPHERE
* SUPERWEAPONREADY_OWN_CHRONOSPHERE
* SUPERWEAPONREADY_ALLY_CHRONOSPHERE
* SUPERWEAPONREADY_ENEMY_CHRONOSPHERE

* SUPERWEAPONDETECTED_OWN_RIFT_GENERATOR
* SUPERWEAPONDETECTED_ALLY_RIFT_GENERATOR
* SUPERWEAPONDETECTED_ENEMY_RIFT_GENERATOR
* SUPERWEAPONLAUNCHED_OWN_RIFT_GENERATOR
* SUPERWEAPONLAUNCHED_ALLY_RIFT_GENERATOR
* SUPERWEAPONLAUNCHED_ENEMY_RIFT_GENERATOR
* SUPERWEAPONREADY_OWN_RIFT_GENERATOR
* SUPERWEAPONREADY_ALLY_RIFT_GENERATOR
* SUPERWEAPONREADY_ENEMY_RIFT_GENERATOR

## New SpecialPower "Kodiak"
based on spectre gunship, a unit with multiple turrets and a primary weapon are fired in the target area
* **MainTurrets**: Follow manual targetting (spectre)
* **SideTurrets**: Auto-Acquire targets (cheap first)
* **AATurrets**: Auto-Acquire targets
* **MainWeapon**: Primary Weapon of the gunship itself that is fired when ready. Has it's own scatter pattern and option to lock on to targets within a radius of scatter impact. Scatter Pattern is in KodiakUpdate, the weapon itself should not have a scatter radius or scatter targets. The Kodiak update will try to fire the weapon when ready going through scatter targets in same order. Use a clipsize. If clipreloadtime is smaller than OrbitTime, the weapon is fired again.

All turrets must be contained objects, this allows individual attack orders. 

Example Code in the gunship unit:
```ini
 Behavior = SpecialAbility ModuleTag_32
    SpecialPowerTemplate = ZSupW_SuperweaponKodiakGunship
    UpdateModuleStartsAttack = Yes
  End
  Behavior = KodiakUpdate ModuleTag_10
    SpecialPowerTemplate = ZSupW_SuperweaponKodiakGunship
    OrbitTime = 30000 ; total time in attack mode.
    TurretRecenterTimeBeforeExit = 3000 ; Time before finishing where turrets stop and recenter
    InitialAttackDelay = 1500 ; when entering target area time until attacks start, leaves time for animations
    ; Effective Attacking time = OrbitTime - TurretRecenterTimeBeforeExit - InitialAttackDelay
    ; during TurretRecenterTimeBeforeExit and InitialAttackDelay DOOR_1_CLOSING and DOOR_1_OPENING animations are played
    ; Locomotorset change happens at reaching the target and end of OrbitTime (NORMAL to PANIC like spectre) 
    TargetingReticleRadius = 25 ; Like spectre
    GunshipOrbitRadius = 400 ;radius from target point at where to go in attack mode
    AreaDecalRadius = 250 ; radius of the AttackAreaDecal, independent of AttackAreaRadius
    MainTurrets = 2 ; how many turrets are main, should have at least one, must be first x contained objects
    MainTurretsFireInSequence = No ; if Yes, main turrets take turns: each fires one shot, then the next one may fire
    MainTurretSequenceDelay = 0 ; min time in ms between consecutive main turret shots in sequence mode (never the same frame)
    SideTurrets = 2 ; how many turrets are side, must be contained after mainTurrets
    AATurrets = 2 ; how many turrets are aa, must be contained after sideTurrets
    MainTargetingRate = 200     ; how often target for main turrets can change
    AttackAreaRadius = 250.0 ; Attack area around target like spectre
    SideTargetingRate = 150     ; how often target for side turrets can change
    SideAttackAreaRadius = 250.0 ; which radius side turrets can shoot at, calculated from current gunship pos
    AATargetingRate = 200     ; how often target for aa turrets can change
    AAAttackAreaRadius = 325.0 ; which radius aa turrets can shoot at air targets, calculated from current gunship pos
    MissileLockRadius = 50.0 ; when firing main weapon units within this radius can be locked at
    MissileScatterRadius = 250.0 ; area scalar for ScatterTargets
    ScatterTarget = X: 0.000 Y: 0.133 ; add as many scatter targets as clipsize
    ScatterTarget = X: 0.133 Y:-0.200 ; if less then clipsize they will be repeated
    ScatterTarget = X:-0.067 Y: 0.667 ; if no targets at all, shoot at gunship location
    ScatterTarget = X: 0.300 Y: 0.300
    ScatterTarget = X: 0.767 Y: 0.000
    ScatterTarget = X: 0.500 Y:-0.567
    ScatterTarget = X:-0.333 Y:-0.800
    ScatterTarget = X:-0.600 Y:-0.1333
    ScatterTarget = X:-0.567 Y: 0.433
    ScatterTarget = X: 0.000 Y: 0.133
    ScatterTarget = X: 0.133 Y:-0.200
    ScatterTarget = X:-0.067 Y: 0.667
    ScatterTarget = X: 0.300 Y: 0.300
    ScatterTarget = X: 0.767 Y: 0.000
    ScatterTarget = X: 0.500 Y:-0.567
    ScatterTarget = X:-0.333 Y:-0.800
    ; Decals Like spectre
    AttackAreaDecal 
      Texture = SCCSpecTarg
      Style = SHADOW_ALPHA_DECAL
      OpacityMin = 25%
      OpacityMax = 50%
      OpacityThrobTime = 1500
      Color = R:127 G:177 B:222 A:255
      OnlyVisibleToOwningPlayer = Yes
    End
    TargetingReticleDecal
      Texture = SCCSpecRet
      Style = SHADOW_ALPHA_DECAL
      OpacityMin = 50%
      OpacityMax = 100%
      OpacityThrobTime = 300
      Color = R:127 G:177 B:222 A:255
      OnlyVisibleToOwningPlayer = Yes
    End
  End
```
Coresponding CommandCenter Code:
```ini
  Behavior = SpecialAbility ModuleTag_496
    SpecialPowerTemplate = ZSupW_SuperweaponKodiakGunship
    UpdateModuleStartsAttack = Yes
  End
  Behavior = KodiakDeploymentUpdate ModuleKodiak_1 ; Parameters like spectre, just different module name
    SpecialPowerTemplate = ZSupW_SuperweaponKodiakGunship
    RequiredScience = SupW_SCIENCE_KodiakGunship
    GunshipTemplateName = SupW_AmericaJetKodiak
    AttackAreaRadius = 250
    CreateLocation = CREATE_AT_EDGE_NEAR_SOURCE
  ; other choices are: *NEAR_SOURCE *FARTHEST_FROM_SOURCE *NEAR_TARGET
  End
```
