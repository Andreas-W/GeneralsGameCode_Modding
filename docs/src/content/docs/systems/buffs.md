---
title: "Buff system"
description: "BuffTemplate.ini and BuffUpdate."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/BuffSystem.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/BuffSystem.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Helper/BuffEffectHelper.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/BuffUpdate.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/BuffUpdate.h
sidebar:
  order: 34
---

## Overview

Added a new system to apply and track Buffs and Debuffs. A new INI file can be added (`Data\INI\BuffTemplate.ini`) to define templates that can be used in various modules.

### Buff Application

- via module: See BuffUpdate
- via weapon: not implemented yet (BuffUpdate is currently the only way to apply a buff)

## BuffTemplate.ini

### BuffTemplate definition

Each BuffTemplate has
- a name
- a number of modifiers (gameplay effects)
- a number of effects (visual effects)
- generic parameters

#### Generic Parameters

- `MaxStacksSize = 1`  ; Maximum number of stacks that can be applied. Each time the buff is applied again while it is active, its duration is refreshed and, while below this number, one more stack is added (the modifiers are applied once more).
- `HasPriorityOver = <BuffTemplate names>`  ; The BuffTemplate will override (switch off) active buffs listed here if they are applied on the same object, and a buff listed here does not become active while this one is. Can be given multiple times; the names add up.

### Modifiers

The block keywords are `ValueModifier`, `FlagModifier`, `ColorTintEffect` and `ParticleSystemEffect`, each closed with `End`. A template can have several of them.

#### ValueModifier
Scalars to various unit stats. These are multiplied in once per stack if stack size > 1. Values are clamped to 0.01 .. 100.

- `MovementSpeedScalar = 1.0`  ; increase/decrease a units movement speed
- `ArmorDamageScalar = 1.0`  ; increase/decrease how much damage unit takes
- `SightRangeScalar = 1.0`  ; increase/decrease a units' sight range (also scales the shroud clearing range)

#### FlagModifier
Flag modifiers are binary modifiers, i.e. multiple stacks from the same buff will not increase the effect.

- `WeaponBonus = <WeaponBonus type>`  ; applies the weaponBonus for the unit (one value)
- `WeaponBonusAgainst = <WeaponBonus type>`  ; applies the weaponBonus when attacking the target (like Avenger targetDesignator) (one value)
- `WeaponSetFlag = <WeaponSet type>`  ; applies the weaponset type for the unit (this only works if the unit has a corresponding weaponset defined) (one value)
- `ArmorSetFlag = <ArmorSet type>` ; same as above (one value)
- `StatusToSet = <StatusName> ...`  ; set the status flags for the unit (a list of object status names)

When one buff's flags are removed while another active buff sets the same flag, the flag is set again.

### Effects

#### ColorTintEffect

- `TintStatusType = <TintStatus type>`  ; Apply the color tint effect (applied with the first stack only)

#### ParticleSystemEffect

- `ParticleSystem = <ParticleSystem name>`   ; attaches the particlesystem to the object (created with the first stack only, removed when the buff ends)


## Examples

```ini
BuffTemplate SupW_BuffForceShieldOne
  ValueModifier
    ArmorDamageScalar  = 0.7
  End
  
  ColorTintEffect
    TintStatusType = FORCE_FIELD
  End
End

BuffTemplate SupW_BuffCryoBombSlow
  ValueModifier
    MovementSpeedScalar  = 0.4
  End

  FlagModifier
    WeaponBonus = CRYO_THREE
  End
  
  ColorTintEffect
    TintStatusType = FROZEN
  End
  
  ParticleSystemEffect
    ParticleSystem = CryoBuffEffectIceParticles
  End
  
End
```

## BuffUpdate

Works similar to WeaponBonusUpdate (Frenzy module) to apply buffs to objects in an area. Every `BuffDelay` it applies the buff to all matching objects within `BuffRange`, and also to the passengers of containers in range (passengers are checked against the same KindOf and airborne rules).

- `RequiredAffectKindOf = <List of KindOfs>`  ; objects must have one of these (or all of them with `RequiresAllKindOfs = Yes`). With the default `RequiresAllKindOfs = No`, an empty list matches nothing, so set this.
- `RequiresAllKindOfs = Yes/No`   ; Require all listed KindOfs, or just one of them
- `ForbiddenAffectKindOf = <List of KindOfs>`
- `AffectsTargets = <combination of ALLIES | ENEMIES | NEUTRALS>`  ; default ALLIES
- `AffectAirborne = Yes/No`  ; default Yes
- `BuffDuration = duration in msec  ; how long to apply the buff`
- `BuffDelay = duration in msec  ; how often to apply the buff`
- `BuffRange = 0.0  ; area radius`
- `BuffTemplateName = <BuffTemplate.ini entry>  ; the template to apply` (if it is not found, the module stops working)

```ini
Behavior = BuffUpdate ModuleTag_Buff
  RequiredAffectKindOf = INFANTRY
  AffectsTargets = ALLIES ENEMIES NEUTRALS
  AffectAirborne = No
  BuffDuration = 3000
  BuffDelay = 500
  BuffRange = 50.0
  BuffTemplateName = SupW_BuffCryoBombSlow
End
```
