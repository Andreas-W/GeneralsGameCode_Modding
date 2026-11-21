---
title: "Buff system"
description: "BuffTemplate.ini and BuffUpdate."
status: unverified
sidebar:
  order: 34
---

## Overview

Added a new system to apply and track Buffs and Debuffs. A new INI file can be added (BuffTemplate.ini) to define templates that can be used in various modules.

### Buff Application

- via module: See BuffUpdate
- via weapon: TODO

## BuffTemplate.ini

### BuffTemplate definition

Each BuffTemplate has
- a name
- a number of modifiers (gameplay effects)
- a number of effects (visual effects)
- generic parameters

#### Generic Parameters

- MaxStacksSize = 1  ; Maximum number of stacks that can be applied
- HasPriorityOver = &lt;BuffSystem names&gt;  ; The BuffTemplate will override active buffs listed here if they are applied on the same object.

### Modifiers

#### ValueModifier
Scalars to various unit stats. These can be applied multiple times if stack size > 1

- MovementSpeedScalar = 1.0  ; increase/decrease a units movement speed
- ArmorDamageScalar = 1.0  ; increase/decrease how much damage unit takes
- SightRangeScalar = 1.0  ; increase/decrease a units' sight range

#### FlagModifier
Flag modifiers are binary modifiers, i.e. multiple stacks from the same buff will not increase the effect.

- WeaponBonus = &lt;WeaponBonus type&gt;  ; applies the weaponBonus for the unit
- WeaponBonusAgainst = &lt;WeaponBonus type&gt;  ; applies the weaponBonus when attacking the target (like Avenger targetDesignator)
- WeaponSetFlag = &lt;WeaponSet type&gt;  ; applies the weaponset type for the unit (this only works if the unit has a corresponding weaponset defined)
- ArmorSetFlag = &lt;ArmorSet type&gt; ; same as above
- StatusToSet = &lt;StatusName&gt;  ; set the status for the unit.

### Effects

#### ColorTintEffect

- TintStatusType = &lt;TintStatus type&gt;  ; Apply the color tint effect

#### ParticleSystemEffect

- ParticleSystem = &lt;ParticleSystemEntry&gt;   ; attaches the particlesystem to the object


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

Works similar to WeaponBonusUpdate (Frenzy module) to apply buffs to objects in an area

- `RequiredAffectKindOf = <List of KindOfs>`
- `RequiresAllKindOfs = Yes/No   ; Require all listed KindOfs, or just one of them`
- `ForbiddenAffectKindOf = <List of KindOfs>`
- `AffectsTargets = <combination of ALLIES | ENEMIES | NEUTRALS>`
- `AffectAirborne = Yes/No`
- `BuffDuration = duration in msec  ; how long to apply the buff`
- `BuffDelay = duration in msec  ; how often to apply the buff`
- `BuffRange = 0.0  ; area radius`
- `BuffTemplateName = <BuffTemplate.ini entry>  ; the template to apply`
