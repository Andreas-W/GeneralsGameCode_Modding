---
title: "Superweapon modules"
description: "Orbital beam, chronosphere, multi-location powers and the rift."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/OrbitalBeamUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ChronoSphereUpdateModule.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/MultiLocationSpecialPowerUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/RiftSlowDeathUpdate.cpp
sidebar:
  order: 20
---

## OrbitalBeamUpdate

**New in this fork.**

An update module for an "orbital beam" style strike: a ring of charge beams appears around the object, converges on its center, and then a final beam fires. (PR #99)

It is not a special power module and has no `SpecialPowerTemplate`. Put it on a helper object (usually created by the superweapon's OCL at the target point). The sequence starts on the object's first update, is centered on the object's position, and the object destroys itself when the final beam (and any fading beam) is gone.

Stages: **initial delay** → **spawn charge beams** (one by one around a ring) → **animate** (the beams move from the ring to the center) → **final beam** → object destroyed.

```ini
Behavior = OrbitalBeamUpdate ModuleTag_OrbitalBeam
  InitialDelay = 0             ; ms before the charge beams start spawning
  InitialSound = <AudioEvent>   ; plays during the initial delay and while the charge beams spawn
  InitialFX = <FXList>          ; FX at the object position on the first update (before InitialDelay)
  InitialOCL = <OCL>            ; OCL at the object position on the first update (before InitialDelay)

  ; --- Charge beams (a ring of beams that converge before the final shot) ---
  NumChargeBeams = 0                      ; number of charge beams, evenly spaced around the ring
  ChargeBeamName = <Object>               ; object template of the beam (a drawable with a LaserUpdate client update)
  ChargeBeamRadius = 0.0                  ; radius of the ring the charge beams start on
  ChargeBeamHeight = 3500.0               ; height above the surface the charge and final beams come from
  ChargeBeamsStartCenter = No             ; Yes = all beams come from one point above the center instead of straight above their impact point
  ChargeBeamRotation = 0                  ; degrees each beam sweeps around the center while converging (spiral); 0 = straight in
  ChargeBeamInterpolation = LINEAR        ; easing of the convergence, see below
  RandomizeChargeBeamOrder = No           ; spawn the ring slots in random order (same positions)
  DelayBetweenChargeBeams = 0             ; ms between two charge beams spawning
  ChargeStartFX = <FXList>      ; FX at the center when the charge beams begin (after InitialDelay)
  ChargeStartOCL = <OCL>        ; OCL at the center when the charge beams begin
  ChargeBeamStartFX = <FXList>  ; FX at each charge beam's impact point when it spawns
  ChargeBeamParticleSystem = <ParticleSystem> ...       ; particle systems that follow each charge beam's impact point
  ChargeBeamParticleSystemLand  = <ParticleSystem> ...  ; additional systems while that point is over land
  ChargeBeamParticleSystemWater = <ParticleSystem> ...  ; additional systems while that point is over water
  DelayChargeToAnim = 0                   ; ms between the last charge beam spawning and the beams starting to move
  BeamAnimationDuration = 0               ; ms the beams take to reach the center
  ChargeSound = <AudioEvent>              ; plays while the beams move
  ChargeWeapon = <Weapon>                 ; fired at the center during the animation, repeatedly at its own fire rate (one weapon per line, can repeat)
  DelayAnimationToFinal = 0               ; ms between the beams reaching the center and the final beam

  ; --- Final beam / weapon ---
  FinalBeamName = <Object>      ; object template of the final beam (LaserUpdate)
  FinalBeamDuration = 0         ; ms the final beam stays
  FinalWeapon = <Weapon>        ; fired at the center when the final beam appears, then again at its own fire rate while it lasts
  FinalFX = <FXList>            ; FX at the center when the final beam appears
  FinalOCL = <OCL>              ; OCL at the center when the final beam appears

  HitWaterSurface = No          ; Yes = beam endpoints, FX and weapons rest on the water surface instead of the sea floor
End
```

Notes:
* All `Delay` / `Duration` values are in milliseconds.
* `ChargeBeamParticleSystem*` take several names per line, and every line adds more.
* The charge beams start fading out when they reach the center, using the fade-out time of their own `LaserUpdate`.
* `ChargeBeamInterpolation` values: `LINEAR`, `SMOOTH`, `SMOOTHER`, `EASE_IN`, `EASE_OUT`, `EASE_IN_CUBIC`, `EASE_OUT_CUBIC`, `SINE`, `EXPO_IN`, `EXPO_OUT`, `CIRCULAR`, `BACK`, `ELASTIC`, `BOUNCE`, `STEP_HOLD`, `PULSE`.

## ChronoSphereUpdateModule

**New in this fork.**

Special power update module for a Chronosphere-style teleport superweapon: after a delay it moves every matching unit within `Radius` of a source point to a target point, keeping each unit's offset from the center. (PR #100)

The player picks the two points with a [multi-target CommandButton](/GeneralsGameCode_Modding/ini/special-power/#multi-target-special-powers) (first click = source, second click = target). Pair it with a `SpecialAbility` for the same `SpecialPowerTemplate` with `UpdateModuleStartsAttack = Yes`: the cost, EVA and recharge are triggered only once both points are chosen. List it above other update modules for the same power (e.g. `MissileLauncherBuildingUpdate`), since the first module that accepts the power wins.

```ini
Behavior = SpecialAbility ModuleTag_Chrono01
  SpecialPowerTemplate = <SpecialPower entry>
  UpdateModuleStartsAttack = Yes
End
Behavior = ChronoSphereUpdateModule ModuleTag_Chrono02
  SpecialPowerTemplate = <SpecialPower entry>
  Radius = 0.0                 ; 2D radius of the source/target area
  RequiredKindOf = <KindOf list>     ; only these units are teleported
  ForbiddenKindOf = <KindOf list>    ; these units are never teleported
  TeleportDelay = 0            ; ms between the second click and the teleport
  SourceFX = <FXList>          ; FX at the source point, at the teleport
  TargetFX = <FXList>          ; FX at the target point, at the teleport
  SourceOCL = <OCL>            ; OCL at the source point, right away (ignores TeleportDelay)
  TargetOCL = <OCL>            ; OCL at the target point, right away (ignores TeleportDelay)
  UnitSourceFX = <FXList>      ; FX on each teleported unit before it moves
  UnitTargetFX = <FXList>      ; FX on each teleported unit after it moves
End
```

Notes:
* The object carrying the module is never teleported itself. Teleported units are put on the ground at the target and their current orders are dropped.
* Structures can be teleported too unless `ForbiddenKindOf` excludes them (e.g. `ForbiddenKindOf = IMMOBILE STRUCTURE`).

## MultiLocationSpecialPowerUpdate

**New in this fork.**

Special power update that fires an OCL at each of several target points, one after the other. The player picks the points with the [multi-target CommandButton](/GeneralsGameCode_Modding/ini/special-power/#multi-target-special-powers) fields. As with `ChronoSphereUpdateModule`, pair it with a `SpecialAbility` for the same power with `UpdateModuleStartsAttack = Yes`, listed above other update modules for that power.

```ini
Behavior = MultiLocationSpecialPowerUpdate ModuleTag_MultiLoc
  SpecialPowerTemplate = <SpecialPower entry>
  OCL = <OCL entry>               ; OCL created for each point
  UpgradeOCL = <Science> <OCL>    ; used instead of OCL when the player has the science; first match wins (can repeat)
  CreateLocation = CREATE_AT_EDGE_NEAR_SOURCE   ; where the OCL is created, see below
  InitialDelay = 0                ; ms from the last click to the first point
  Delay = 0                       ; ms between two points (0 = next frame)
  WaitForAttackComplete = No      ; Yes = wait until the caster has stopped attacking before the next point
  MaxWaitPerTarget = 0            ; with WaitForAttackComplete: longest wait per point in ms; 0 = 30 seconds
End
```

`CreateLocation` values (as in `OCLSpecialPower`):
* `CREATE_AT_EDGE_NEAR_SOURCE` (default) - map edge closest to the caster
* `CREATE_AT_EDGE_NEAR_TARGET` - map edge closest to the point
* `CREATE_AT_EDGE_FARTHEST_FROM_TARGET` - map edge farthest from the point, 300 above it
* `CREATE_AT_LOCATION` - at the point
* `CREATE_ABOVE_LOCATION` - 300 above the point
* `USE_OWNER_OBJECT` - at the point (in this module it works the same as `CREATE_AT_LOCATION`)

Notes:
* Points are handled in click order.
* `WaitForAttackComplete` is meant for OCLs that order the caster to attack (an `Attack` nugget): without it the next point's order would cut the current barrage short. In this mode `Delay` is how often the module checks and the minimum gap between points (0 = every quarter second).

## RiftSlowDeathBehavior

**New in this fork.**

A slow death behavior that turns its object into a gravity well ("rift" / black hole). Put it on a helper object (e.g. created by an OCL) and let it die right away (e.g. `LifetimeUpdate` with a 1 ms lifetime): when the slow death starts, the rift locks in its position `LiftHeight` above the ground and runs through its phases. Everything pullable within `Radius` is lifted, spun and sucked toward the center; things reaching `EventHorizonRadius` are consumed. At the end the rift collapses with an outward blast.

Phases: **charge** (`InitialDelay`, FX only, nothing is pulled), **ramp-up** (`RampupTime`, strength 0 → 1), **main** (`MainDuration`, full strength), **fade-out** (`FadeOutTime`, strength 1 → 0), then **collapse**. Strength scales suction and damage. Suction also falls off linearly from the center to `Radius`.

It extends `SlowDeathBehavior`, so all its parameters (`DestructionDelay`, `DeathTypes`, `FX`/`OCL` phases, ...) work too. `DestructionDelay` must be at least the sum of the four phase times, or the rift is cut off.

```ini
Behavior = RiftSlowDeathBehavior ModuleTag_Rift
  DestructionDelay = 16050      ; >= sum of the phases below

  ; --- Phases (ms) ---
  InitialDelay = 0              ; charge phase, FXCharge only
  RampupTime = 0                ; strength climbs 0 -> 1
  MainDuration = 0              ; full strength
  FadeOutTime = 0               ; strength falls 1 -> 0, then the rift collapses

  ; --- Shape ---
  Radius = 0.0                  ; 2D radius of the effect (the circle on the ground)
  EventHorizonRadius = 0.0      ; victims closer than this (3D) are killed with ConsumeDeathType; 0 = off
  LiftHeight = 100.0            ; height of the rift center above the ground

  ; --- Suction ---
  PullAcceleration = 0.0        ; inward acceleration, dist/sec^2
  SwirlFactor = 0.0             ; bends the pull sideways: 0 = falls straight in, ~1 = orbits
  PullDamping = 12%             ; velocity bled off per frame; terminal speed is about PullAcceleration / PullDamping
  LiftAcceleration = 0.0        ; vertical acceleration toward the rift plane, dist/sec^2
  MaxPullSpeed = 0.0            ; speed cap for held victims, dist/sec; 0 = no cap
  InitialLiftVelocity = 0.0     ; upward kick on capture, dist/sec
  CancelGravity = Yes           ; cancel world gravity while a victim is held
  SpinRate = 0                  ; tumble, deg/sec
  HugeVehiclePullScalar = 1.0   ; suction multiplier for KINDOF_HUGE_VEHICLE

  ; --- Damage (everything within Radius, pullable or not) ---
  DamagePerSecond = 0.0         ; at full strength (inside MaximumDamageRadius)
  MaximumDamageRadius = 0.0     ; 2D distance within which the full DamagePerSecond applies
  OuterDamagePerSecond = -1     ; damage per second at Radius, linear falloff from MaximumDamageRadius; negative = no falloff
  DamageInterval = 1000         ; ms between damage ticks
  DamageType = EXPLOSION
  DeathType = NORMAL            ; death type of the continuous damage
  ConsumeDeathType = EXPLODED   ; death type at the event horizon, and for damage kills within ConsumeDeathRadius
  ConsumeDeathRadius = 0.0      ; victims killed within this distance keep falling in until they are destroyed

  ; --- Collapse ---
  FinalPushForce = 0.0          ; shockwave over the whole Radius at the collapse; 0 = none
  FinalPushSpeed = 0.0          ; outward throw for victims still held, dist/sec
  FinalPushDeathType = SPLATTED

  ; --- Filters ---
  PullKindOf = <KindOf list>    ; if set, only these kinds are pulled
  NoPullKindOf = PROJECTILE SMALL_MISSILE BALLISTIC_MISSILE INERT DRAWABLE_ONLY OPTIMIZED_TREE MINE  ; (default) never pulled

  ; --- Effects ---
  FXCharge = <FXList>           ; at activation
  FXMain = <FXList>             ; at the start of the ramp-up
  FXFinal = <FXList>            ; at the collapse
  FXConsume = <FXList>          ; on each victim crossing the event horizon
  OCLConsume = <OCL>            ; on each victim crossing the event horizon

  ; --- Structure debris ---
  StructureDebrisOCL = <OCL>    ; fired at a random spot on a damaged structure; the created object is pulled in
  StructureDebrisChance = 100%  ; per damaged structure per damage tick
  StructureDebrisCount = 1      ; how often the OCL fires per spawn, each at its own spot
End
```

Notes:
* Never pulled (they only take damage): `IMMOBILE`, `STRUCTURE`, `BRIDGE`, `BRIDGE_TOWER`, the new `IMMUNE_TO_PULL` KindOf, contained passengers, invulnerable objects and objects with `ImmortalBody` / `HighlanderBody`.
* Aircraft whose locomotor has `LocomotorWorksWhenDisabled` are steered through their own locomotor; everything else is taken over by physics (disabled as in freefall while held).
* Only the first object a `StructureDebrisOCL` creates can be grabbed, so use `Count = 1` in that OCL and `StructureDebrisCount` for more pieces. Debris is destroyed at the event horizon. Structures that can't be destroyed (indestructible, `ImmortalBody`, `HighlanderBody`) never spawn debris.
* Victims consumed at the event horizon stay held until they are destroyed, so a death that plays out over time (e.g. a shrinking `ChronoDeathBehavior`) happens inside the singularity instead of dropping to the ground.
* A new DamageType and DeathType `BLACK_HOLE` are available for `DamageType` / `ConsumeDeathType` and matching armor and die modules.
* Rift generator superweapons can use the special power enum `SPECIAL_RIFT_GENERATOR`, the radius cursor `RIFT_GENERATOR` and the `*_RIFT_GENERATOR` EVA events (see [SpecialPowers](/GeneralsGameCode_Modding/ini/special-power/)).
