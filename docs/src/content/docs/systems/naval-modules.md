---
title: "Naval and water modules"
description: "Modules for ships and objects on water."
status: unverified
sidebar:
  order: 13
---

## Naval / Water Modules

### PhysicsBehavior

Added parameters to support objects floating/impacting on water (used together with the ship death behavior and water effects).

* `DoWaterPhysics = No` - (If Yes, the object reacts to the water surface, e.g. floating and slowing down in water instead of falling through. Default = No)
* `WaterExtraFriction = 0.0` - (Additional friction applied while the object is in/on water. Default = 0.0)
* `WaterImpactFX = <FXList>` - (FXList played once when the object first impacts the water surface. Default = None)

### ShipSlowDeathBehavior

**New in this fork.**

A death behavior for ships and other large naval objects. It plays a multi-stage sinking sequence: an initial delay, an optional toppling motion, and finally sinking below the water surface, each with their own FX/OCL and model condition states.

The module has a lot of parameters, grouped by stage below. All durations are in milliseconds, all angles in degrees.

```ini
Behavior = ShipSlowDeathBehavior ModuleTag_ShipDeath
  ; --- common DieModule params (DeathTypes, VeterancyLevels, ExemptStatus, ...) ---

  ; --- Initial delay before the death sequence starts ---
  InitialDelay = 0
  InitialDelayVariance = 0
  InitialConditionFlag = <ModelCondition>   ; condition flag set when the death starts (default = RUBBLE)

  ; --- Wobble (gentle rocking while sinking) ---
  WobbleMaxAnglePitch = 0
  WobbleMaxAngleYaw = 0
  WobbleMaxAngleRoll = 0
  WobbleInterval = 0

  ; --- Topple stage (ship tips over) ---
  ToppleStyle = TOPPLE_FRONT   ; TOPPLE_FRONT, TOPPLE_BACK, TOPPLE_SIDE_LEFT, TOPPLE_SIDE_RIGHT (random if omitted)
  ToppleFrontMinPitch = 0
  ToppleFrontMaxPitch = 0
  ToppleBackMinPitch = 0
  ToppleBackMaxPitch = 0
  ToppleSideMinRoll = 0
  ToppleSideMaxRoll = 0
  ToppleMinHeightOffset = 0
  ToppleMaxHeightOffset = 0
  ToppleDuration = 0
  ToppleDurationVariance = 0
  ToppleMinPushForce = 0
  ToppleMaxPushForce = 0
  ToppleMinPushForceSide = 0
  ToppleMaxPushForceSide = 0
  ToppleAngleCorrectionRate = 0   ; angular velocity used to settle the topple angle
  ToppleConditionFlag = <ModelCondition>   ; condition flag set when toppling starts (default = RUBBLE)
  FXStartTopple = <FXList>
  OCLStartTopple = <OCL>

  ; --- Sink stage (ship goes below water) ---
  SinkHowFast = 100%       ; sink speed as a percentage
  SinkConditionFlag = <ModelCondition>   ; condition flag set when sinking starts (default = RUBBLE)
  FXStartSink = <FXList>
  OCLStartSink = <OCL>
  SinkAttachParticle = <ParticleSystem>   ; particle system attached while sinking (e.g. bubbles)
  SinkAttachParticleBones = <Bone1> <Bone2> ...   ; bones to attach the particle to
  SoundSinkLoop = <AudioEvent>            ; looping sound while sinking

  ; --- Pilot / ground impact ---
  OCLEjectPilot = <OCL>     ; OCL created to eject a pilot/crew
  FXHitGround = <FXList>    ; FX when the wreck reaches the sea/ground floor
  OCLHitGround = <OCL>      ; OCL when the wreck reaches the sea/ground floor
End
```

Notes:
* The three condition flags (`InitialConditionFlag`, `ToppleConditionFlag`, `SinkConditionFlag`) all default to `RUBBLE`. Set them to distinct states if you want different models/animations per stage.
* `ToppleStyle` accepts the topple direction names; if omitted, a random direction is chosen.

### HeightDieUpdate

Added parameter:
* `TargetHeightIncludesWater = No` - (If Yes, the target height check used to trigger the death also takes the water surface height into account, so the object dies relative to the water level rather than the terrain below it. Useful for projectiles/objects over water. Default = No)

### Die Modules - Water Depth Conditions

All Die modules (the shared `DieMux` conditions) now support restricting the death to a water depth range. This lets you trigger different death effects depending on how deep the water is (e.g. a normal death on land/shallow water, a sinking death in deep water).

* `MinWaterDepth = 0.0` - (Minimum water depth required for this death module to trigger. Default = 0)
* `MaxWaterDepth = 0.0` - (Maximum water depth allowed for this death module to trigger; 0 = no upper limit. Default = 0)

### BunkerBusterBehavior

Added parameter:
* `WorksOverWater = Yes` - (If Yes (default), the bunker buster triggers its behavior even when it hits the water surface. Set to No to make it ignore targets over water.)

## Water Genpower Support

Improvements to make existing generals-power superweapons work correctly over water. (PR #97)

### SpectreGunshipUpdate

* `HitWaterSurface = No` - (If Yes, the gunship's strafing shots detonate at the water surface instead of passing through / hitting the sea floor.)
* `GattlingStrafeFXParticleSystemWater = <ParticleSystem>` - (Particle system used for strafe impacts that land on water, instead of the regular land FX.)

### ParticleUplinkCannonUpdate

* `HitWaterSurface = No` - (If Yes, the uplink cannon beam detonates at the water surface over water.)

See also [OrbitalBeamUpdate `HitWaterSurface`](/GeneralsGameCode_Modding/systems/superweapons/#orbitalbeamupdate) and the [DeliverPayload `StrafingWeaponTargetsWater`](/GeneralsGameCode_Modding/ini/object-creation-list/#deliverpayload) flag.
