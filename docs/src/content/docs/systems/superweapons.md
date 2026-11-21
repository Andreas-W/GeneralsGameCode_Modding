---
title: "Superweapon modules"
description: "Orbital beam, chronosphere, multi-location powers and the rift."
status: unverified
sidebar:
  order: 20
---

## OrbitalBeamUpdate

**New in this fork.**

A special power update module for an "orbital beam" style superweapon that charges up several beams and then fires a final beam/weapon. (PR #99)

```ini
Behavior = OrbitalBeamUpdate ModuleTag_OrbitalBeam
  SpecialPowerTemplate = <SpecialPower entry>   ; (via SpecialPowerModule params)
  InitialDelay = 0             ; ms before the sequence starts
  InitialSound = <AudioEvent>
  InitialFX = <FXList>          ; FX played right at the start of the sequence
  InitialOCL = <OCL>            ; OCL created right at the start of the sequence

  ; --- Charge beams (multiple beams that converge before the final shot) ---
  NumChargeBeams = 1
  ChargeBeamName = <LaserName>            ; laser template used for charge beams
  ChargeBeamRadius = 0.0                  ; radius the charge beams spread over
  ChargeBeamHeight = 0.0
  ChargeBeamRotation = 0                  ; rotation angle (degrees)
  ChargeBeamsStartCenter = No             ; start beams from the center
  ChargeBeamInterpolation = <InterpolationType>   ; how the beams move in
  RandomizeChargeBeamOrder = No
  ChargeBeamStartFX = <FXList>
  ChargeStartFX = <FXList>      ; FX played when the charge phase begins
  ChargeStartOCL = <OCL>        ; OCL created when the charge phase begins
  ChargeSound = <AudioEvent>
  ChargeWeapon = <Weapon entry>           ; weapon(s) fired per charge beam (can repeat)
  ChargeBeamParticleSystem = <ParticleSystem> ...       ; particle systems for the charge beam
  ChargeBeamParticleSystemLand  = <ParticleSystem> ...  ; used over land
  ChargeBeamParticleSystemWater = <ParticleSystem> ...  ; used over water
  DelayBetweenChargeBeams = 0
  DelayChargeToAnim = 0
  BeamAnimationDuration = 0
  DelayAnimationToFinal = 0

  ; --- Final beam / weapon ---
  FinalBeamName = <LaserName>
  FinalBeamDuration = 0
  FinalWeapon = <Weapon entry>
  FinalFX = <FXList>            ; FX played when the final beam appears
  FinalOCL = <OCL>              ; OCL created when the final beam appears

  HitWaterSurface = No          ; if Yes, beam/effects detonate at the water surface over water
End
```
Note: many fields have a matching `...Frames` internal name; all `Delay`/`Duration` values are in milliseconds. Refer to the field list for the full set.

## ChronoSphereUpdate

**New in this fork.**

Special power update module implementing a Chronosphere-style teleport superweapon: teleports units from a source area to a target area after a delay. (PR #100)

```ini
Behavior = ChronoSphereUpdate ModuleTag_Chrono
  SpecialPowerTemplate = <SpecialPower entry>
  Radius = 0.0                 ; radius of the source/target area
  RequiredKindOf = <KindOf list>     ; only these units are teleported
  ForbiddenKindOf = <KindOf list>    ; these units are never teleported
  TeleportDelay = 0            ; ms between activation and the teleport happening
  SourceFX = <FXList>          ; FX at the source area
  SourceOCL = <OCL>            ; OCL at the source area
  TargetFX = <FXList>          ; FX at the target area
  TargetOCL = <OCL>            ; OCL at the target area
  UnitSourceFX = <FXList>      ; FX on each teleported unit at the source
  UnitTargetFX = <FXList>      ; FX on each teleported unit at the target
End
```

## MultiLocationSpecialPowerUpdate

**New in this fork.**

Special power update that can create objects/OCLs at multiple resolved map locations (e.g. edges relative to the source/target). Used together with the [multi-target CommandButton](/GeneralsGameCode_Modding/ini/special-power/#multi-target-special-powers) fields.

```ini
Behavior = MultiLocationSpecialPowerUpdate ModuleTag_MultiLoc
  SpecialPowerTemplate = <SpecialPower entry>
  CreateLocation = CREATE_AT_EDGE_NEAR_SOURCE   ; where the OCL/object is created
  ; valid values:
  ;   CREATE_AT_EDGE_NEAR_SOURCE (default)
  ;   CREATE_AT_EDGE_NEAR_TARGET
  ;   CREATE_AT_EDGE_FARTHEST_FROM_TARGET
  ;   CREATE_AT_LOCATION
  InitialDelay = 0        ; ms before the first creation
  Delay = 0               ; ms between creations at each location
  OCL = <OCL entry>       ; default OCL created at each location
  UpgradeOCL = <Upgrade> <OCL>   ; optional per-upgrade OCL override (can repeat)
End
```

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
