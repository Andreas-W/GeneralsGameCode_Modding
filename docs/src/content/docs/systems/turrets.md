---
title: "Turrets"
description: "Up to eight turrets per object."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/AI/TurretAI.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/WeaponSet.h
sidebar:
  order: 36
---

Up to 8 turrets can now be used instead of only 2.
They use the name Turret3, Turret4, ... Turret8. Turret1 can be used as alias to Turret and Turret2 can be used as alias to AltTurret. An alias and its original name fill the same turret, so give only one of them (a second definition of the same turret is an INI error).

The names exist in two places:
* In the AI update (any AIUpdate type): `Turret1` ... `Turret8` blocks with the usual turret fields (`TurretTurnRate`, `ControlledWeaponSlots`, ...; see [AIUpdate turret fields](/GeneralsGameCode_Modding/modules/ai-update/#turret)).
* In the W3DModelDraw condition states: `Turret<N> = <bone>`, `Turret<N>Pitch = <bone>`, `Turret<N>ArtAngle = <degrees>` and `Turret<N>ArtPitch = <degrees>` for N = 1..8 (`Turret` / `TurretPitch` / `TurretArtAngle` / `TurretArtPitch` and the `AltTurret...` names still work for turrets 1 and 2).

Weapon slots go up to eight: `PRIMARY`, `SECONDARY`, `TERTIARY`, `WEAPON_FOUR` ... `WEAPON_EIGHT`.

Example Draw (unit with 6 turrets and 7 weapons):
```ini
  Draw = W3DModelDraw ModuleTag_095
    ParticlesAttachedToAnimatedBones = Yes
    DefaultConditionState
      Model = Kodiak
      Animation = Kodiak.Kodiak
      AnimationMode = MANUAL
      Turret1 = TURRETFR
      Turret2 = TURRETFL
      Turret3 = TURRETSR
      Turret4 = TURRETSL
      Turret5 = TURRETTF
      Turret6 = TURRETTB
      Turret1Pitch = TURRETFREL
      Turret2Pitch = TURRETFLEL
      Turret3Pitch = TURRETSREL
      Turret4Pitch = TURRETSLEL
      Turret5Pitch = TURRETTFEL
      Turret6Pitch = TURRETTBEL
      WeaponFireFXBone = PRIMARY WEAPONFR
      WeaponLaunchBone = PRIMARY WEAPONFR
      WeaponRecoilBone = PRIMARY BARRELFR
      WeaponFireFXBone = SECONDARY WEAPONFL
      WeaponLaunchBone = SECONDARY WEAPONFL
      WeaponRecoilBone = SECONDARY BARRELFL
      WeaponFireFXBone = TERTIARY WEAPONSR
      WeaponLaunchBone = TERTIARY WEAPONSR
      WeaponRecoilBone = TERTIARY BARRELSR
      WeaponFireFXBone = WEAPON_FOUR WEAPONSL
      WeaponLaunchBone = WEAPON_FOUR WEAPONSL
      WeaponRecoilBone = WEAPON_FOUR BARRELSL
      WeaponFireFXBone = WEAPON_FIVE WEAPONTF
      WeaponLaunchBone = WEAPON_FIVE WEAPONTF
      WeaponRecoilBone = WEAPON_FIVE BARRELTF
      WeaponFireFXBone = WEAPON_SIX WEAPONTB
      WeaponLaunchBone = WEAPON_SIX WEAPONTB
      WeaponRecoilBone = WEAPON_SIX BARRELTB
      WeaponFireFXBone = WEAPON_SEVEN WEAPONM
      WeaponLaunchBone = WEAPON_SEVEN WEAPONM
      Flags = START_FRAME_LAST
    End
    OkToChangeModelColor = Yes
  End
```

Corresponding AI Update example:
```ini
  Behavior = ChinookAIUpdate ModuleTag_07
    AutoAcquireEnemiesWhenIdle = Yes
    RotorWashParticleSystem = HelixRotorWashRing
    Turret1
      TurretTurnRate = 90
      TurretPitchRate = 60
      AllowsPitch = Yes
      MinPhysicalPitch = -89
      ControlledWeaponSlots = PRIMARY
    End
    Turret2
      TurretTurnRate = 90
      TurretPitchRate = 60
      AllowsPitch = Yes
      MinPhysicalPitch = -89
      ControlledWeaponSlots = SECONDARY
    End
    Turret3
      TurretTurnRate = 120
      TurretPitchRate = 90
      AllowsPitch = Yes
      MinPhysicalPitch = -89
      NaturalTurretAngle = -90
      ControlledWeaponSlots = TERTIARY
    End
    Turret4
      TurretTurnRate = 120
      TurretPitchRate = 90
      AllowsPitch = Yes
      MinPhysicalPitch = -89
      NaturalTurretAngle = 90
      ControlledWeaponSlots = WEAPON_FOUR
    End
    Turret5
      TurretTurnRate = 150
      TurretPitchRate = 120
      AllowsPitch = Yes
      ControlledWeaponSlots = WEAPON_FIVE
    End
    Turret6
      TurretTurnRate = 150
      TurretPitchRate = 120
      AllowsPitch = Yes
      ControlledWeaponSlots = WEAPON_SIX
    End
  End
```
