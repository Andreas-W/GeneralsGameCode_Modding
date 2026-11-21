---
title: "SpecialPower.ini"
description: "New special power fields, Eva events and the Kodiak power."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/SpecialPower.cpp
  - GeneralsMD/Code/GameEngine/Include/Common/SpecialPowerType.h
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/ActionManager.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/SpecialPower/SpecialPowerModule.cpp
  - GeneralsMD/Code/GameEngine/Source/GameClient/Eva.cpp
  - Core/GameEngine/Source/GameClient/GUI/ControlBar/ControlBar.cpp
  - Core/GameEngine/Include/GameClient/ControlBar.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/KodiakUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/KodiakDeploymentUpdate.cpp
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
* SPECIAL_TOGGLE_DRAWBRIDGE - opens / closes a drawbridge, see [Bridges](/GeneralsGameCode_Modding/systems/bridges/)
* SPECIAL_JUMPJET

Faction / general specific copies of vanilla powers were added as well. Without a `BehaviorEnum` they behave like the vanilla power given here:

* like SPECIAL_PARADROP_AMERICA: AIRF_SPECIAL_PARADROP_AMERICA, SOCOM_SPECIAL_SUPPLY_DROP, SOCOM_SPECIAL_TANK_PARADROP, TANK_SPECIAL_TANK_PARADROP, TANK_SPECIAL_PARADROP, SUPW_SPECIAL_PARADROP_AMERICA, SUPW_SPECIAL_TANK_PARADROP
* like SPECIAL_CIA_INTELLIGENCE: SECW_SPECIAL_HUNTER_SEEKER, SPECIAL_ALL_BATTLE_PLANS
* like SPECIAL_SPY_SATELLITE: CHINA_SPECIAL_SPY_SATELLITE, SECW_SPECIAL_SPY_SATELLITE, LAZR_SPECIAL_SPY_SATELLITE
* like SPECIAL_CARPET_BOMB: AIRF_SPECIAL_SUPERSONIC_AIRSTRIKE, AIRF_SPECIAL_HEAVY_AIRSTRIKE, SOCOM_SPECIAL_COASTAL_BOMBARDEMENT, TANK_SPECIAL_NAPALM_BOMB, TANK_SPECIAL_CHINA_CARPET_BOMB, NUKE_SPECIAL_NUCLEAR_AIRSTRIKE, NUKE_SPECIAL_CHINA_CARPET_BOMB, NUKE_SPECIAL_BALLISTIC_MISSILE, SECW_SPECIAL_SYSTEM_HACK, DEMO_SPECIAL_SUICIDE_PLANE, DEMO_SPECIAL_CARPET_BOMB, CHEM_SPECIAL_CARPET_BOMB, CHEM_SPECIAL_AIRSTRIKE, FORT_SPECIAL_AIRSTRIKE, FORT_SPECIAL_CARPET_BOMB, LAZR_SPECIAL_DAISY_CUTTER, LAZR_SPECIAL_AIRSTRIKE, SUPW_SPECIAL_AIRSTRIKE
* like SPECIAL_AMBUSH: AIRF_SPECIAL_HELICOPTER_AMBUSH, DEMO_SPECIAL_AMBUSH, CHEM_SPECIAL_AMBUSH, LAZR_SPECIAL_AMBUSH
* like SPECIAL_FRENZY: AIRF_SPECIAL_HOLO_PLANES, TANK_SPECIAL_FRENZY, NUKE_SPECIAL_FRENZY, DEMO_SPECIAL_FRENZY, CHEM_SPECIAL_FRENZY, FORT_SPECIAL_FRENZY
* like SPECIAL_CLUSTER_MINES: TANK_SPECIAL_CLUSTER_MINES
* like SPECIAL_REPAIR_VEHICLES: TANK_SPECIAL_REPAIR_VEHICLES, NUKE_SPECIAL_REPAIR_VEHICLES, DEMO_SPECIAL_REPAIR_VEHICLES, CHEM_SPECIAL_REPAIR_VEHICLES, FORT_SPECIAL_REPAIR_VEHICLES, LAZR_SPECIAL_NANO_SWARM, SUPW_SPECIAL_FORCEFIELD
* like SPECIAL_EMP_PULSE: TANK_SPECIAL_EMP_PULSE, NUKE_SPECIAL_NEUTRON_BOMB, SECW_SPECIAL_EMP_HACK
* like SPECIAL_ARTILLERY_BARRAGE: TANK_SPECIAL_ARTILLERY_BARRAGE, NUKE_SPECIAL_ARTILLERY_BARRAGE, DEMO_SPECIAL_ARTILLERY_BARRAGE, FORT_SPECIAL_ARTILLERY_BARRAGE, LAZR_SPECIAL_ORBITAL_STRIKE, SUPW_SPECIAL_ORBITAL_STRIKE
* like SPECIAL_CASH_HACK: NUKE_SPECIAL_CASH_HACK
* like SPECIAL_SPECTRE_GUNSHIP: SECW_SPECIAL_DRONE_GUNSHIP, LAZR_SPECIAL_SPECTRE_GUNSHIP, SUPW_SPECIAL_SPECTRE_GUNSHIP
* like SPECIAL_SNEAK_ATTACK: DEMO_SPECIAL_SNEAK_ATTACK, CHEM_SPECIAL_SNEAK_ATTACK
* like SPECIAL_GPS_SCRAMBLER: DEMO_SPECIAL_GPS_SCRAMBLER, CHEM_SPECIAL_GPS_SCRAMBLER, FORT_SPECIAL_GPS_SCRAMBLER
* like SPECIAL_ANTHRAX_BOMB: DEMO_SPECIAL_ANTHRAX_BOMB, CHEM_SPECIAL_ANTHRAX_BOMB
* like SPECIAL_LEAFLET_DROP: CHEM_SPECIAL_VIRUS, SUPW_SPECIAL_CRYOBOMB

All other new enums (the superweapons above, SPECIAL_JUMPJET) behave like SPECIAL_NEUTRON_MISSILE unless `BehaviorEnum` is set.

### New Parser Fields:
`BehaviorEnum = <OTHER ENUM>`  
Used to set how a power with one of the new enums behaves when it is targeted and checked (which targets it accepts, how the command is validated), since a lot of this is hardcoded per enum. Without it the defaults listed above apply (most new enums behave like `SPECIAL_NEUTRON_MISSILE`). It has no effect on vanilla enums.

`EvaDetectedOwn = <Name of eva event>`  
`EvaDetectedAlly = <Name of eva event>`  
`EvaDetectedEnemy = <Name of eva event>`  
`EvaLaunchedOwn = <Name of eva event>`  
`EvaLaunchedAlly = <Name of eva event>`  
`EvaLaunchedEnemy = <Name of eva event>`  
`EvaReadyOwn = <Name of eva event>`  
`EvaReadyAlly = <Name of eva event>`  
`EvaReadyEnemy = <Name of eva event>`  
These allow to specify eva events (names from the Eva.ini event list, see below) for a special power. Own / Ally / Enemy is the relation of the player hearing it to the power's owner.

* Launched / Ready: when the field for that relation is set, that event plays instead of the hardcoded one. `None` plays nothing. When not set, vanilla powers still play their hardcoded events; powers with the new enums play nothing.
* Detected (played when a structure with the power is completed): when any of the three `EvaDetected*` fields is set to an event, only these fields are used for that structure (an unset relation plays nothing) and the hardcoded detected events are not played. `None` alone does not switch off the hardcoded detected events.

`OwnerOnlyTimer = No`  
Used with `PublicTimer = Yes`: the countdown is shown only to the player who owns the power. Such a timer is also shown for objects that are not structures, e.g. a helper object that shows its owner how long a temporary effect still lasts. Default = No.

`FlashingTimer = No`  
Used with `PublicTimer = Yes`: the countdown is drawn like a ready timer (ready font, flashing) the whole time, e.g. while an effect is active. Default = No.

`Cost = 0`  
Optional credits cost to activate the special power. When set (> 0), the player must have enough money, and the cost is withdrawn when the power is triggered. The command button is disabled/greyed out when the player cannot afford it, and the cost is shown in the button tooltip. Default = 0 (free).

`NeedsTargetDesignator = No`  
When Yes, this special power can only be fired at a location within the radius of one of the player's own living target designators (objects with `KindOf = TARGET_DESIGNATOR` and a [SpecialPowerDesignatorUpdate](/GeneralsGameCode_Modding/systems/target-designator/#specialpowerdesignatorupdate) valid for this power, searched up to 5000 units around the target). Used to gate a superweapon behind a "designate target" unit/ability. Default = No.

#### Special power module: StartsReady

`StartsReady = No`  
Not a SpecialPower.ini field: it goes into the special power *module* of the object (the `SpecialPowerModule` family, e.g. `OCLSpecialPower`, `SpecialAbility`). If Yes, the power is ready to fire as soon as the object exists (already built), or as soon as an `UnpauseSpecialPowerUpgrade` unpauses it, instead of waiting for its first `ReloadTime`. It does nothing for powers with `SharedSyncedTimer = Yes`, and for an object that is still under construction when it is created. Default = No.

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

Added CommandButton parameters to let a special power be aimed at **multiple target points** in one activation (used together with the [MultiLocationSpecialPowerUpdate](/GeneralsGameCode_Modding/systems/superweapons/#multilocationspecialpowerupdate) module and `Options = NEED_N_TARGET_POS`). The player clicks the target points one after the other, optionally after an anchor.

* `NumberOfTargets = 2` - (How many target points the player must place for this power. Default = 2, the two-point chronosphere power.)
* `TargetRadiusMode = NONE` - (How the clicks are constrained; a click outside the allowed radius is rejected. Default = NONE)
  - `NONE` - no constraint, every click is a target
  - `AREA_FROM_FIRST` - the first click is a target and the center; later clicks must be within `TargetRadius` of it
  - `ANCHORED_AREA` - the first click is only an anchor (not a target); every target must be within `AnchorRadius` of it
  - `CHAIN_PREVIOUS` - each click after the first must be within `TargetRadius` of the previous one
* `TargetRadius = 0.0` - (Constraint radius for `AREA_FROM_FIRST` / `CHAIN_PREVIOUS`, and the fallback for the other radii.)
* `TargetDecalRadius = 0.0` - (Radius of the decal (`RadiusCursorType`) drawn at each placed target point and previewed at the cursor. 0 = use `TargetRadius`.)
* `AnchorRadius = 0.0` - (`ANCHORED_AREA` only: radius around the anchor within which targets must be placed. 0 = use `TargetRadius`.)
* `AnchorDecalRadius = 0.0` - (Radius of the decal drawn at the anchor. 0 = use the anchor's constraint radius.)
* `AnchorRadiusCursorType = NONE` - (Radius cursor type shown for the anchor; NONE = use `RadiusCursorType`.)
* `MarkerObject = <ObjectTemplate>` - (Object drawn as a visual marker at each placed target point, not at the anchor. Client-side only.)
* `MarkerFX = <FXList>` - (FXList played at each placed target point, not at the anchor.)
* `SecondCursorName = <cursor>` - (Mouse cursor used once the first point is placed, instead of `CursorName`.)

## Eva.ini

### New Eva events:

Eva.ini event names that can be used with the `Eva*` fields above, besides the vanilla ones:

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
* **MainWeapon**: Primary Weapon of the gunship itself that is fired when ready. Has its own scatter pattern and option to lock on to targets within a radius of scatter impact. Scatter Pattern is in KodiakUpdate, the weapon itself should not have a scatter radius or scatter targets. The Kodiak update will try to fire the weapon when ready going through scatter targets in same order. Use a clipsize. If clipreloadtime is smaller than OrbitTime, the weapon is fired again.

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
Corresponding CommandCenter Code:
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
