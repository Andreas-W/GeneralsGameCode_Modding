---
title: "Projectile and laser modules"
description: "Missiles, projectiles, scatter shots and lasers."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/MissileAIUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/DumbProjectileBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/FreeFallProjectileBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ScatterShotUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/LaserUpdate.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/PointDefenseLaserUpdate.cpp
sidebar:
  order: 3
---

## MissileAIUpdate

Added new parameters to MissileAIUpdate module:

* `RandomPathOffset = 0.0` - (Distance in world units. When the missile has finished its no-turn distance, it first flies to a random point near the halfway point to its target, offset by up to this distance sideways and up to half of it upwards, then turns back towards the target. Makes missile volleys spread out and fly random arcs. 0 = off.)

* `ZCorrectionFactor = 2.0` - (When the target is higher than the missile at launch, the launch direction is tilted upwards by this factor times the height/distance ratio. 2.0 is the retail Zero Hour value; 0 = launch straight along the launcher's facing.)

* `ApplyLauncherBonus = No` - (The projectile takes over the launcher's weapon bonus conditions, so any extra weapon fired from the projectile (on death, FireWeaponUpdate, etc.) gets the shooter's weapon bonuses.)

* `IsTorpedo = No` - (The missile behaves like a torpedo: it heads for a point at the water surface plus its locomotor's preferred height instead of the target object itself, keeps updating that point while the target moves, and detonates as soon as it is no longer over water.)

Notes:
- *ZCorrectionFactor* is what causes AA missiles in retail Zero Hour to never be aligned to the launcher. For units with actual fire pitch (e.g. USA Patriot Battery), it is recommended to set this value to 0. For units that cannot pitch (e.g. USA Missile Defender) a value between 1 and 2 is recommended.

## DumbProjectileBehavior

New parameters:

* `ApplyLauncherBonus = No` - (The projectile takes over the launcher's weapon bonus conditions, so any extra weapon fired from the projectile (on death, FireWeaponUpdate, etc.) gets the shooter's weapon bonuses.)

* `DynamicHeightMinScale = 1.0` - (Scales the projectile's arc height (`FirstHeight` / `SecondHeight`) based on the distance to the target. At the detonation weapon's maximum range the full arc height is used; the closer the target, the more the height is scaled down towards this value. `1.0` = no scaling (default). Useful so short-range shots don't arc as high, e.g. for torpedoes/arcing guns.)
* `DynamicHeightMinRange = 1.0` - (The target distance at which `DynamicHeightMinScale` is reached. The height scale interpolates between this range and the weapon's max range. The scale is not clamped: targets closer than this range get an even lower arc.)
* `LinearFlightPath = No` - (If Yes, the projectile flies a straight line from launch point to target at constant speed instead of the Bezier arc. The arc parameters (`FirstHeight`, `SecondHeight`, `FirstPercentIndent`, ...) are ignored.)
* `KillSelfDelay = 0` - (Time in ms the projectile lingers at the impact point after detonating before it dies. During that time it is hidden, frozen in place and has no collisions, but its exhaust and model-condition particle systems keep emitting, so trails can catch up instead of being cut off. The detonation weapon still fires immediately; only the death (and its die modules) is delayed. 0 = die immediately (old behavior).)

Notes:
* Lingering projectiles are ignored by `PointDefenseLaserUpdate` and no longer trigger `ScatterShotUpdate`.

## FreeFallProjectileBehavior

**New in this fork.**

New type of projectile module that mimics the behavior of bombs dropped by general's powers.
Projectiles will move based on physics, making it suitable for carpet bomb or strafing run aircraft weapons.
On collision, the weapon's damage and detonation effects are applied.

Example:
```ini
Behavior = FreeFallProjectileBehavior ModuleTag_009
   MaxLifespan = 10000  ; Maximum lifetime of the object in ms (default 10000); the projectile detonates when it runs out
   TumbleRandomly = No  ; Adds random rotation, same as for DumbProjectileBehavior
   DetonateCallsKill = No  ; Same as DumbProjectileBehavior
   CourseCorrectionScalar = 1.0   ; guide the bomb towards the intended target. 1=no homing, 0=snapto; 0.99=smooth, 0.95=too-fast (clamped to 0..1)
   UseWeaponSpeed = No   ; push the projectile forward with the detonation weapon's WeaponSpeed (respects ScaleWeaponSpeed)
   ExitPitchRate = 0 ; Pitch rate in degrees per second, multiplied by the projectile's physics CenterOfMassOffset (no effect without an offset)
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
* For basic carpet bombing, you can copy the settings from existing general's powers.
* When `ExitPitchRate` is left out, its internal default is a very large rate (1 radian per frame), still multiplied by `CenterOfMassOffset`. Set it explicitly if your projectile has a center of mass offset.
* Course correction only runs while the projectile is significantly above the terrain.

## ScatterShotUpdate

**New in this fork.**

New module intended for weapon projectiles, to allow scattering in mid air and fire multiple shots to targets within range.

Parameters:
* `Weapon` = &lt;entry from weapon.ini&gt; - (The weapon to be fired at targets.)
* `NumShots` = &lt;integer&gt; - (default = 0; number of shots to fire)
* `TargetSearchRadius` = &lt;decimal&gt; - (default = 0; range around the weapon's target location or object that we look for enemy targets)
* `TargetMinRadius` = &lt;decimal&gt; - (default = 0; minimum distance targets need to be away from the scattering projectile (the object with this module, not the original target). When set, the original target is not picked first, and ground shots keep at least this distance from the target point.)
* `MaxShotsPerTarget` = &lt;integer&gt; - (default = 0; How many shots to fire at *each* target. 0 = randomly attack ground only; 1 = attack each target once; >1 when each target was attacked once, start from the beginning)
* `PreferSimilarTargets` = &lt;bool&gt; - (default = No; Only pick air targets if the main target object is airborne, only ground targets otherwise)
* `PreferNearestTargets` = &lt;bool&gt; - (default = No; Prefer targets closest to the original target; Otherwise choose in no particular order)
* `NoTargetsScatterRadius` = &lt;decimal&gt; - (default = 0; Shots left over after all targets were picked are fired at the ground at a random point within this radius around the target)
* `NoTargetsScatterMinRadius` = &lt;decimal&gt; - (default = see source; Minimum radius for those ground shots)
* `NoTargetsScatterMaxAngle` = &lt;degrees&gt; - (default = see source; If greater than 0, ground shots are only placed within this angle to either side of the projectile's facing; otherwise in any direction)
* `AttackGroundWhenNoTargets` = &lt;bool&gt; - (default = Yes; Meant to stop leftover shots from firing at the ground when set to No. **Currently not read by the code**: leftover shots are always fired at the ground.)
* `AvoidOriginalTargetObject` = &lt;bool&gt; - (default = see source; If Yes, we never try to hit the original target of the attack)
* `AvoidPreviousTargetWhenChain` = &lt;bool&gt; - (default = see source; When the projectile was fired from another ScatterShotUpdate, avoid that module's original target)
* `TriggerDistanceToTarget` = &lt;real&gt; - (default = 0; How close the projectile needs to be to the target to trigger the scatter shot.)
* `TriggerDistancePercent` = &lt;percent&gt; - (default = 0%; Meant to trigger at a share of the distance to the target. The current code triggers when the remaining distance divided by the start distance is *greater* than this value, which is already true right after launch for any value below 100%. Test before relying on it.)
* `TriggerLifetime` = &lt;integer&gt; - (default = 0; Time in ms after the projectile was launched (or the object created) to trigger the scatter shot. 0 = no limit.)
* `TriggerOnImpact` = &lt;bool&gt; - (default = No; Trigger scatter shot when the projectile dies by detonating.)
* `TriggerInstantly` = &lt;bool&gt; - (default = No; Trigger scatter shot instantly after the projectile was launched)
* `StayAliveAfterTrigger` = &lt;bool&gt; - (default = No; The original weapon's projectile will stay alive after the scatter shot was triggered.)
* `TriggeredDeathType` = &lt;DeathType&gt; - (default = NORMAL; The death type to use for the projectile object when the scatter shot is triggered.)
* `ScatterFX` = &lt;FXList&gt; - (default = None; entry from FXList.ini to play when scatter shot is triggered.)

**Notes:**
- The weapon used for the scatter shot needs to have a clip size large enough to fire all shots.
- When picking targets for the scatter shots, they need to be viable targets for the weapon's damage type.
- When the original weapon targets an object, that original target is picked first for one shot, unless `AvoidOriginalTargetObject = Yes`, `TargetMinRadius` is set or `MaxShotsPerTarget = 0`.
- The projectile takes over the launcher's weapon bonus conditions, so the scatter shots get the shooter's weapon bonuses.
- This module can be used on non-projectile objects as well (using lifetime or instant triggers), e.g. to create an object that fires at multiple units in range simultaneously. Without a projectile module the object's own position is the target point.

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
* `UseHouseColorInner = No` - (apply house color to the laser's inner color)

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
