---
title: "Weapon.ini"
description: "New Weapon fields and WeaponExtend."
status: unverified
sidebar:
  order: 22
---

## Weapon.ini

### Scatter Target Improvements
Added new weapon parameters to tweak ScatterTargets

* `ScatterTargetAligned = No` - (Aligns target pattern with the firing unit. Default = No)

* `ScatterTargetRandomOrder = Yes` - (Use target points random or linear order. Default = Yes)

* `ScatterTargetRandomAngle = No` - (Use a random angle for the target pattern. Default = No)

* `ScatterTargetMinScalar = 0` - (if > 0, the target scalar will be linearly scaled between min and max weapon range; ScatterTargetMinScalar defines the scalar at minimum range, while ScatterTargetScalar defines the value at maxium range.)

* `ScatterTargetCenteredAtShooter = No` - (Scatter target coordinates will be used relative to the shooter's position instead of the target, i.e. the firing unit is considered to be at 0/0)

* `ScatterTargetResetTime = 0` (Duration in msec; Scatter targets will be reset after the unit is not firing for this amount of time)

* `ScatterTargetResetRecenter = No` - When scatter targets are reset, with only a portion of the clip remaining, chose targets in the center of the list of ScatterTargets. This is recommended for "sweeping" targets that form a line in both sides of the main target.

These features can be used to e.g. fire a laser in a line, or create strafing runs for aircraft, by using a linear pattern with ScatterTargetAligned=Yes and ScatterTargetRandomOrder=No

### Pre Attack FX
Added new weapon parameters to define FXList that is played on PreAttack

* `PreAttackFX = <FXListEntry>` - (FXList entry; played when PRE_ATTACK starts. Default = None)
* `VeterancyPreAttackFX = [VETERAN | ELITE | HEROIC] <FXListEntry>` - (Veterancy level and FXList entry. played when PRE_ATTACK starts. Default = None)
* `PreAttackFXDelay` = 200 - (Minimal delay in ms when PreAttack FX can be played again. In some situations, e.g. when following a moving target, PreAttack state can be entered repeatedly and this avoids spamming the FX. Default = 200)

Notes:
- FXList will be aligned to the object's FireFX bone.
- BulletTracer used in the FX will be aligned to the target.
- TODO: Add flag to spawn FX at object's origin position instead.

### Laser Improvements
Added improvements for lasers.
#### Continuous Laser
* `ContinuousLaserLoopTime = 0` - (time in ms. If this is > 0, the laser object will be kept and moved with the next shot, instead of creating a new laser object. Default = 0)

Note: This is can be used for continous laser weapons along with the new fade in/out and grow/shrink parameters in LaserUpdate

#### Detonation FX and OCL

* `ProjectileDetonationFX` and `ProjectileDetonationOCL` are now supported for lasers. FX and OCL will be created at the target location.

#### Laser Target Height Offsets
Laser weapons are hardcoded to hit with an offset of 10.0 above the target unit's height. This can now be configured

* `LaserGroundUnitTargetHeight = 10.0` - Lasers will hit this high above a ground target unit's origin.

* `LaserGroundTargetHeight = 0.0` - Lasers will hit this high above the ground when attacking the ground (e.g. force fire).

### Damage Variance

`PrimaryDamage` and `SecondaryDamage` now accept an optional random range instead of a single fixed value.

* `PrimaryDamage = 100` - fixed damage (vanilla behavior)
* `PrimaryDamage = Min:50 Max:100` - each shot rolls a random damage value between Min and Max
* `SecondaryDamage = Min:10 Max:30` - same for secondary (splash) damage

Notes:
- Backwards compatible: a plain number works as before (no variance).
- The value is rolled once per shot. The weapon bonus damage scalar (e.g. veterancy) is applied to both the nominal value and the variance.

### Damage Taper Off

Scale damage down towards the edge of the blast radius, so the center hits harder than the rim.

* `PrimaryDamageTaperOff = 1.0` - (Factor of primary damage applied at the edge of the `PrimaryDamageRadius`. `1.0` = no taper (default, full damage across the whole radius); `0.5` = half damage at the edge, full at the center.)
* `SecondaryDamageTaperOff = 1.0` - (Same, for the secondary damage radius.)

### Damage / Radius Scaling by Engagement Distance

Scale a weapon's damage and radii based on how far the target is, interpolated from point-blank to the weapon's attack range. Useful e.g. for artillery/naval guns that are stronger up close or at long range.

* `DamageFactorAtMaxRange = 1.0` - (Damage multiplier reached at maximum attack range. `1.0` = no scaling (default). At point-blank the factor is `1.0`, interpolating to this value at max range.)
* `RadiusFactorAtMaxRange = 1.0` - (Same, applied to the damage radii. Also affects the radius used to align the detonation FX.)
* `ScatterRadiusFactorAtMaxRange = 1.0` - (Same, applied to the weapon's `ScatterRadius`, so shots scatter more/less depending on distance.)

## WeaponExtend

Added support to create WeaponExtend definitions, which will inherit all parameters from the parent Weapon.
All parameters from Weapons can be used to override values from the parent weapon. It is possible to extend an extended weapon.

- `WeaponExtend <ExtendedWeapon> <ParentWeapon>`

Example

```ini
WeaponExtend Chem_HindRocketPodWeapon HindRocketPodWeapon
  ProjectileDetonationOCL = Chem_OCL_PoisonFieldSmall
End
```
This defines a new weapon Chem_HindRocketPodWeapon, using all parameters from HindRocketPodWeapon, overriding or adding the ProjectileDetonationOCL param

Note: The extended weapon needs to be defined below the parent weapon in the ini file. (or alphabetically in a later order if defined in a different ini file)
