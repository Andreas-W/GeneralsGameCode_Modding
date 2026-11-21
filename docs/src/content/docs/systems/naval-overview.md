---
title: "Naval support overview"
description: "Ships, water effects, naval weapons and carriers."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/PhysicsUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/ObjectCreationList.cpp
  - Core/GameEngine/Source/GameClient/FXList.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/DumbProjectileBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/MissileAIUpdate.cpp
  - Core/GameEngine/Source/GameLogic/System/CrateSystem.cpp
sidebar:
  order: 37
---

A number of changes were added for improved naval / water-based unit support. The relevant parameters live across several modules and INI files; this page collects the water-related features and links to the detailed documentation.

## Ships and Naval Death

* **[ShipSlowDeathBehavior](/GeneralsGameCode_Modding/systems/naval-modules/#shipslowdeathbehavior)** - multi-stage sinking death (delay → topple → sink) for ships, with FX/OCL and condition states per stage.
* **[PhysicsBehavior water parameters](/GeneralsGameCode_Modding/systems/naval-modules/#physicsbehavior)** - `DoWaterPhysics`, `WaterExtraFriction`, `WaterImpactFX` for objects entering the water.
* **[Die water depth conditions](/GeneralsGameCode_Modding/systems/naval-modules/#die-modules---water-depth-conditions)** - `MinWaterDepth` / `MaxWaterDepth` to trigger different death effects depending on water depth.
* **[HeightDieUpdate `TargetHeightIncludesWater`](/GeneralsGameCode_Modding/systems/naval-modules/#heightdieupdate)** - die relative to the water surface.
* **[Water Impact OCL nugget](/GeneralsGameCode_Modding/ini/object-creation-list/#water-impact-createdebris)** - `WaterImpactFX` / `WaterImpactSound` for debris hitting water (the debris needs `DoWaterPhysics`).

## Water Effects

* **[FXList height & surface restrictions](/GeneralsGameCode_Modding/ini/fxlist-and-particles/#height-and-surface-restrictions)** - `AllowedSurface = WATER/LAND/ALL` and `Min/MaxAllowedHeight` for ParticleSystem and Sound nuggets, so effects can be restricted to water or land.
* **[Water depth terrain lighting](/GeneralsGameCode_Modding/ini/gamedata/#water-depth-terrain-lighting)** - darken/tint terrain based on depth below the water surface.
* **[`HideScorchmarksAboveGround`](/GeneralsGameCode_Modding/ini/gamedata/#scorchmarks-on-water)** - avoid scorch decals floating on the water surface.

## Naval Weapons

* **[DumbProjectileBehavior `DynamicHeightMinScale`/`DynamicHeightMinRange`](/GeneralsGameCode_Modding/modules/projectiles-and-lasers/#dumbprojectilebehavior)** - scale projectile arc height by distance, useful for torpedoes and arcing naval guns.
* **[MissileAIUpdate `IsTorpedo`](/GeneralsGameCode_Modding/modules/projectiles-and-lasers/#missileaiupdate)** - torpedoes head for the water surface above their target and detonate once they leave the water.

## Salvage / Crates on Water

* **[Crate `AllowWater`](/GeneralsGameCode_Modding/systems/crates-and-salvage/#crateini-cratedata)** - allow salvage/pickup crates to be created on water.

## Carriers

* **[Drone Carrier system](/GeneralsGameCode_Modding/systems/drone-carriers/)** - modules for a simplified mobile aircraft carrier (`DroneCarrierContain`, `DroneCarrierAIUpdate`, `CarrierDroneAIUpdate`, ...).
