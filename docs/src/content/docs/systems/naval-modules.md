---
title: "Naval and water modules"
description: "Modules for ships and objects on water."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/PhysicsUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ShipSlowDeathBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/HeightDieUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Die/DieModule.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/BunkerBusterBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/SpectreGunshipUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ParticleUplinkCannonUpdate.cpp
sidebar:
  order: 13
---

## PhysicsBehavior

Added parameters for objects that react to the water surface (used together with the ship death behavior and water effects).

* `DoWaterPhysics = No` - (If Yes, the object checks whether it is below the water surface. Only then do `WaterExtraFriction` and `WaterImpactFX` (and the OCL nugget's `WaterImpactSound`) apply. It does not make the object float.)
* `WaterExtraFriction = 0.0` - (Extra friction added while the object is below the water surface, e.g. to slow down sinking debris. Needs `DoWaterPhysics = Yes`.)
* `WaterImpactFX = <FXList>` - (FXList played when the object goes from above to below the water surface. Needs `DoWaterPhysics = Yes`. Objects thrown by an OCL nugget (`Disposition` `SEND_IT_FLYING`, `SEND_IT_UP` or `RANDOM_FORCE`) use that nugget's [`WaterImpactFX` / `WaterImpactSound`](/GeneralsGameCode_Modding/ini/object-creation-list/#water-impact-createdebris) instead, or none if the nugget leaves them out.)

## ShipSlowDeathBehavior

**New in this fork.**

A slow death behavior for ships and other large naval objects. It plays a multi-stage sinking sequence: an initial delay, an optional toppling motion, and finally sinking until the wreck reaches the sea floor, each with its own FX/OCL and model condition. A gentle wobble runs through all stages.

It extends `SlowDeathBehavior`, so its parameters (`DeathTypes`, `ProbabilityModifier`, `DestructionDelay`, `FX`/`OCL` phases, the [water depth conditions](#die-modules---water-depth-conditions) ...) work too. The ship is destroyed when it is within 1 unit of the ground (the sea floor), so give it a `DestructionDelay` long enough for the whole sequence.

All durations are in milliseconds, all angles in degrees. Fields left out are 0 / none.

```ini
Behavior = ShipSlowDeathBehavior ModuleTag_ShipDeath
  ; --- SlowDeathBehavior params (DeathTypes, DestructionDelay, FX, OCL, ...) ---

  ; --- Initial delay before the topple/sink starts ---
  InitialDelay = 0
  InitialDelayVariance = 0                  ; random extra delay, 0 .. this
  InitialConditionFlag = RUBBLE             ; model condition set when the death starts

  ; --- Wobble (gentle rocking during all stages) ---
  WobbleMaxAnglePitch = 0
  WobbleMaxAngleYaw = 0
  WobbleMaxAngleRoll = 0
  WobbleInterval = 0                        ; length of one wobble cycle; 0 = no wobble

  ; --- Topple stage (ship tips over); skipped when ToppleDuration = 0 ---
  ToppleStyle = NONE           ; NONE, FRONT, BACK, LEFT, RIGHT, SIDE (left or right at random), RANDOM (any of the four)
  ToppleDuration = 0
  ToppleFrontMinPitch = 0      ; FRONT: pitch reached, random between min and max
  ToppleFrontMaxPitch = 0
  ToppleBackMinPitch = 0       ; BACK
  ToppleBackMaxPitch = 0
  ToppleSideMinRoll = 0        ; LEFT / RIGHT
  ToppleSideMaxRoll = 0
  ToppleMinHeightOffset = 0    ; height change during the topple (negative = lower)
  ToppleMaxHeightOffset = 0
  ToppleMinPushForce = 0       ; push along the topple direction for FRONT / BACK
  ToppleMaxPushForce = 0
  ToppleMinPushForceSide = 0   ; push for LEFT / RIGHT
  ToppleMaxPushForceSide = 0
  ToppleAngleCorrectionRate = 0             ; deg/sec at which the angles are settled
  ToppleConditionFlag = RUBBLE              ; model condition set when toppling starts
  FXStartTopple = <FXList>
  OCLStartTopple = <OCL>

  ; --- Sink stage (ship goes down) ---
  SinkHowFast = 0%             ; how much of gravity pulls the ship down: 0% = it keeps floating, 100% = full gravity
  SinkConditionFlag = RUBBLE                ; model condition set when sinking starts
  FXStartSink = <FXList>
  OCLStartSink = <OCL>
  SinkAttachParticle = <ParticleSystem>     ; particle system attached while sinking (e.g. bubbles)
  SinkAttachParticleBones = <Bone1> <Bone2> ...   ; bones to attach it to
  SoundSinkLoop = <AudioEvent>              ; looping sound from the sink start until the ship is gone

  ; --- Sea floor ---
  FXHitGround = <FXList>       ; FX when the wreck reaches the sea floor (then the ship is destroyed)
  OCLHitGround = <OCL>         ; OCL when the wreck reaches the sea floor
End
```

Notes:
* The three condition flags (`InitialConditionFlag`, `ToppleConditionFlag`, `SinkConditionFlag`) all default to `RUBBLE`. Set them to distinct states if you want different models/animations per stage. Each stage clears the flag of the stage before it, but the sink stage clears `InitialConditionFlag` instead of `ToppleConditionFlag` (see the code), so a model that uses all three should not rely on the topple flag being gone while sinking.
* `ToppleDurationVariance` and `OCLEjectPilot` are read but not used.

## HeightDieUpdate

Added parameter:
* `TargetHeightIncludesWater = No` - (If Yes, over water the target height is measured from the water surface instead of the sea floor, so the object dies relative to the water level. Useful for projectiles/objects over water.)

## Die Modules - Water Depth Conditions

All die modules (and everything else that takes the common die conditions `DeathTypes`, `VeterancyLevels`, `ExemptStatus`, `RequiredStatus`, such as slow death behaviors) can restrict the death to a water depth range. This lets you pick different death effects depending on how deep the water is (e.g. a normal death on land/shallow water, a sinking death in deep water). The depth is water surface minus terrain height at the object's position.

* `MinWaterDepth = 0.0` - (Minimum water depth for this module to apply. Above 0, the module never applies over land or on a bridge.)
* `MaxWaterDepth = 10000.0` - (The module applies only where the water is shallower than this. Land always passes this check, so `MaxWaterDepth = 0` means "on land only" (not "no limit"). Default = no upper limit.)

## BunkerBusterBehavior

Added parameter:
* `WorksOverWater = Yes` - (If No, a bunker buster over water neither plays its crash-through FX nor kills the occupants of the target.)

## Water Genpower Support

Improvements to make existing generals-power superweapons work correctly over water. (PR #97)

### SpectreGunshipUpdate

* `HitWaterSurface = No` - (If Yes, the gunship's shots and strafing aim at the water surface instead of the sea floor.)
* `GattlingStrafeFXParticleSystemWater = <ParticleSystem>` - (Particle system used instead of `GattlingStrafeFXParticleSystem` for strafe impacts on water. Only with `HitWaterSurface = Yes`.)

### ParticleUplinkCannonUpdate

* `HitWaterSurface = No` - (If Yes, the uplink cannon beam hits the water surface over water instead of the sea floor.)

See also [OrbitalBeamUpdate `HitWaterSurface`](/GeneralsGameCode_Modding/systems/superweapons/#orbitalbeamupdate) and the [DeliverPayload `StrafingWeaponTargetsWater`](/GeneralsGameCode_Modding/ini/object-creation-list/#deliverpayload) flag.
