---
title: "Projectile and laser modules"
description: "Missiles, projectiles, scatter shots and lasers."
status: unverified
sidebar:
  order: 3
---

## MissileAIUpdate

Added new parameters to MissileAIUpdate module:

* `RandomPathOffset = 0.0` - distance in meters. Missile projectils will spread out randomly to fly a random arc pattern for more interesting visuals.

* `ZCorrectionFactor = 2.0` - Missiles attacking air units will have their direction shifted upwards by this factor. (2.0 is vZH default)

* `ApplyLauncherBonus = No` - Any extra weapon that is fired from the projectile (On death, fireweaponupdate, etc.) will inherit the shooter's weapon bonus values 

Notes:
- *ZCorrectionFactor* is what causes AA missiles in vZH to be never aligned to the launcher. For units with actual fire pitch (e.g. Patriot), it is recommended to set this value to 0. For units that cannot pitch (e.g. Missile Defender) a value between 1 and 2 is recommended.

## DumbProjectileBehavior

New parameters:

* `ApplyLauncherBonus = No` - Any extra weapon that is fired from the projectile (On death, fireweaponupdate, etc.) will inherit the shooter's weapon bonus values 

* `DynamicHeightMinScale = 1.0` - (Scales the projectile's arc height based on the distance to the target. At maximum weapon range the full arc height is used; the closer the target, the more the height is scaled down towards this value. `1.0` = no scaling (default). Useful so short-range shots don't arc as high, e.g. for torpedoes/arcing guns.)
* `DynamicHeightMinRange = 1.0` - (The target distance at/below which `DynamicHeightMinScale` is fully reached. The height scale interpolates between this range and the weapon's max range.)
* `LinearFlightPath = No` - (If Yes, the projectile flies a straight line from launch point to target at constant speed instead of the Bezier arc. The arc parameters (`FirstHeight`, `SecondHeight`, `FirstPercentIndent`, ...) are ignored.)
* `KillSelfDelay = 0` - (Time in ms the projectile lingers at the impact point after detonating before it dies. During that time it is hidden, frozen in place and has no collisions, but its exhaust and model-condition particle systems keep emitting, so trails can catch up instead of being cut off. The detonation weapon still fires immediately; only the death (and its die modules) is delayed. 0 = die immediately (old behavior).)

Notes:
* Lingering projectiles are ignored by `PointDefenseLaserUpdate` and no longer trigger `ScatterShotUpdate`.

## FreeFallProjectileBehavior
New type of projectile module that mimics the behavior of Bombs dropped by genpowers.
Projectiles will move based on physics, making it suitable for carpet bomb or strafing run aircraft weapons.
On collision, the weapon's damage and detonation effects are applied.

Example:
```ini
Behavior = FreeFallProjectileBehavior ModuleTag_009
   MaxLifespan = 10000  ; Maximum lifetime of the object
   TumbleRandomly = No  ; Adds random rotation, same as for DumbProjectileBehavior
   CourseCorrectionScalar = 1.0   ; guide the bomb towards the intended target. 1=no homing, 0=snapto; 0.99=smooth, 0.95=too-fast
   UseWeaponSpeed = No   ; apply additional speed from the unit's weapon
   ExitPitchRate = 0 ; Angle tilt per second to apply (respects centerOfMassOffset in the projectile's physics)
   ApplyLauncherBonus = Yes  ; Apply weapon bonus from launcher for any secondary weapon effects
   DetonateOnGround = Yes   ; Will the projectile detonate when hitting the ground or live on (to bounce or rest on ground)?
   DetonateOnCollide = Yes    ; Same as above, when hitting another object (See Weapon for collision settings)
   GarrisonHitKillRequiredKindOf = <Kindof list>  ; Same as MissileAI/DumbProjectile
   GarrisonHitKillForbiddenKindOf= <Kindof list>  ; Same as MissileAI/DumbProjectile
   GarrisonHitKillCount = 0  ; Same as MissileAI/DumbProjectile
   GarrisonHitKillFX = <FXList>  ; Same as MissileAI/DumbProjectile
End
```

Notes:
* This module's behavior depends *a lot* on the projectile's physics settings, such as mass, friction, etc. It takes some finetuning to get the exact behavior you want.
* For basic carpet bombing, you can copy the settings from existing genpowers.

## ScatterShotUpdate

**New in this fork.**

New module intended for weapon projectiles, to allow scattering in mid air and fire multiple shots to targets within range.

Parameters:
* `Weapon` = &lt;entry from weapon.ini&gt; - (The weapon to be fired at targets.)
* `NumShots` = &lt;integer&gt; - (default = 0; number of shots to fire)
* `TargetSearchRadius` = &lt;decimal&gt; - (default = 0; range around the weapon's target location or object that we look for targets)
* `TargetMinRadius` = &lt;decimal&gt; - (default = 0; minimum range targets need to be away from the original target)
* `MaxShotsPerTarget` = &lt;integer&gt; - (default = 0; How many shots to fire at *each* target. 0 = randomly attack ground only; 1 = attack each target once; >1 when each target was attacked once, start from the beginning)
* `PreferSimilarTargets` = &lt;bool&gt; - (default = No; Choose air or ground targets similar to the main target object. I.e. if we attack an air unit, all targets will be picked from air units first)
* `PreferNearestTargets` = &lt;bool&gt; - (default = No; Prefer targets closest to the original target; Otherwise choose randomly)
* `NoTargetsScatterRadius` = &lt;decimal&gt; - (default = 0; If not targets were found/picked, use this random scatter radius to attack the ground)
* `NoTargetsScatterMinRadius` = &lt;decimal&gt; - (default = 0; Minimum scatter radius when no target is found)
* `NoTargetsScatterMaxAngle` = &lt;decimal&gt; - (default = 0; Acceptable aim delta angle for scatter targets)
* `AttackGroundWhenNoTargets` = &lt;bool&gt; - (default = yes; If Yes, when we run out of targets, fire randomly at the ground; If No, do not fire.)
* `AvoidOriginalTargetObject` = &lt;bool&gt; - (default = no; If Yes, we never try to hit the original target of the attack)
* `AvoidPreviousTargetWhenChain` = &lt;bool&gt; - (default = no; When the attack was fired from another ScatterShot module, avoid that modules original target)
* `TriggerDistanceToTarget` = &lt;real&gt; - (default = 0; How close the projectile needs to be to the target to trigger the scatter shot.)
* `TriggerLifetime` = &lt;integer&gt; - (default = 0; How long after the projectile was created to trigger the scatter shot. 0 = no limit.)
* `TriggerOnImpact` = &lt;bool&gt; - (default = No; Trigger scatter shot on projectile impact.)
* `TriggerInstantly` = &lt;bool&gt; - (default = No; Trigger scatter shot instantly after the projectile was created)
* `StayAliveAfterTrigger` = &lt;bool&gt; - (default = No; The original weapon's projectile will stay alive after the scatter shot was triggered.)
* `TriggeredDeathType` = &lt;DeathType&gt; - (default = NORMAL; The death type to use for the projectile object when the scatter shot is triggered.)
* `ScatterFX` = &lt;FXList&gt; - (default = None; entry from FXList.ini to play when scatter shot is triggered.)

**Notes:**
- The weapon used for the scatter shot needs to have a clip size large enough to fire all shots.
- When picking targets for the scatter shots, they need to be viable targets for the weapon's damage type.
- When the original weapon targets an object, that original target will always be picked to fire a scatter weapon at.
- This module can be used on non-projectile objects as well (using lifetime or instant triggers), e.g. to create an object that fires at multiple units in range simultaneously

**Examples:**
```ini
  Behavior = ScatterShotUpdate ModuleTag_Scatter
    Weapon = TomahawkMissileScatterWeapon    ; simple guided rocket weapon
    NumShots = 5
    TargetSearchRadius = 120.0    ; Pick targets in 120 range
    NoTargetsScatterRadius = 60.0   ; If no targets, scatter randomly in 60 radius
    MaxShotsPerTarget = 1       ; Fire at each targets once. If we have more than 5 targets, hit the ground randomly
    TriggerDistanceToTarget = 150.0   ; Trigger at this distance to target
  End
```

## Laser Improvements

### LaserUpdate
New parameters are added to allow the laser to grow/shrink or fade in/out
* `BeamFadeInDuration = 0` - (time in ms. Alpha scalar is linear interpolated from 0 to 1 over the given time at the start.)
* `BeamFadeOutDuration = 0`  - (time in ms. Alpha scalar is linear interpolated from 1 to 0 at the end of the beam's lifetime. Requires LifetimeUpdate)
* `BeamGrowDuration = 0` - (time in ms. Beam width is linear interpolated from 0 to 1 over the given time at the start.)
* `BeamShrinkDuration = 0` - (time in ms. Beam width is linear interpolated from 1 to 0 at the end of the beam's lifetime. Requires LifetimeUpdate)
* `UseMultiLaserDraw = No` - (This flag is needed, if a Laser object has multiple LaserDraw modules, to correctly update all of them)
* `UseHouseColoredParticles = No` - (Apply house colored tint to the laser's muzzle and target particle systems)

### W3DLaserDraw
Added parameters to specify grid animation. Currently only a 1xn grid is allowed (i.e. multiple columns) to work together with scrollRate.
* `TextureGridTotalColumns = 1` - (total number of columns in the texture)
* `TextureGridColumns = 1` - (actual number of columns used for the animation)
* `UseHouseColorOuter = No` - (apply house color to the laser's outer color)
* `UseHouseColorOuter = No` - (apply house color to the laser's inner color)

Note: Usually both TextureGridColumns and TextureGridTotalColumns  numbers would be the same. But having separate numbers allows to use an odd number of frames and still keep proper DDS texture sizes. E.g. you have a texture width of 512, with 8 frames of 64. But you only want 7 frames, so you can set TextureGridTotalColumns=8 and TextureGridColumns=7.

## PointDefenseLaserUpdate

Can now be switched on by an upgrade. Without `TriggeredBy` it is always active, as before.

```ini
Behavior = PointDefenseLaserUpdate ModuleTag_PDL
  ; ... existing parameters ...
  ; Generic Upgrade parameters:
  TriggeredBy = <Upgrade entry>
  ConflictsWith = <Upgrade entry>
  RequiresAllTriggers = No
  ; ---
  StartsActive = No   ; active from the start even with TriggeredBy
End
```

Removing the trigger upgrade switches the laser off again.
