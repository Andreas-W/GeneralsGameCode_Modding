---
title: "GameData.ini"
description: "New GameData and MiscAudio fields."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp
  - GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h
  - Core/GameEngine/Source/Common/INI/INIMiscAudio.cpp
sidebar:
  order: 25
---

## GameData.ini

### Diagonal Movement Speed Fix

All units now move with the same speed in all directions. In vanilla ZH, diagonal movement was faster.

Vanilla behavior can be restored with this parameter:

* `UseVanillaDiagonalMoveSpeed = Yes` - (Default = No)

### Color Tint (TintStatus) definitions

Colors for tint effects (E.g. frenzy) can be defined here. You can define multiple entries for different types.

* `TintStatus = <TintStatusType>  <Tint Color>   <Tint Color (Infantry)>   <Fade in Frames>   <Fade out Frames>`

Example:

* `TintStatus = SHIELDED  R:-0.1 G:-0.2 B:-0.2  R:-0.1 G:-0.2 B:-0.2  30  30`

Notes:
* Colors are defined separately for Infantry. Due to different shading it might be needed to finetune values for infantry.
* Colors are defined as `R:<r> G:<g> B:<b>` decimal numbers that are added to the model's color: positive values brighten that channel, negative values darken it (e.g. `R:-0.1` removes some red).
* Fade in/out are given in logic frames (30 per second) and define how long the transition takes when applying or removing a color tint.

[You can find a list of TintStatus types here.](/GeneralsGameCode_Modding/reference/enums/#tintstatus-types)

### Viewport fixes
* `ViewportHeightScale = 0.8` - (Share of the screen height used by the 3D view while the control bar is not collapsed. The engine default is `0.8` (vanilla); `1.0` removes the black bar behind the bottom command bar. It is forced to `1.0` when a full-viewport control bar addon is installed or the game is started with `-forcefullviewport`.)

Example:

```ini
ViewportHeightScale = 1.0 ; no reduced viewport with non collapsed UI
```

### ChronoGun parameters
These parameters configure behavior of CHRONO_GUN damage type, designed to replicate RA2 Chrono Legionnaire logic.

Tint Colors:

* `TintStatus = DISABLED_CHRONO  R:-0.5 G:-0.5 B:2.0   R:-0.5 G:-0.5 B:2.0   10   10`   ; Used when a target is currently being disabled/removed
* `TintStatus = GAINING_CHRONO_DAMAGE  R:0.5 G:0.5 B:1.5   R:0.5 G:0.5 B:1.5   30   60` ; Used when a target is starting to get disabled/removed

The values below are the engine defaults unless stated otherwise.

* `ChronoDamageDisableThreshold = 10%`   ; CHRONO_GUN damage must reach this threshold of the unit's maxHP to disable it
* `ChronoDamageHealRate = 500`   ; Interval (in ms) in which units recover from CHRONO_DAMAGE after not getting damaged
* `ChronoDamageHealAmountPercent = 10%`  ; "Chrono Health" in percent recovered per interval
* `ChronoDamageOpacityStart = 75%`   ; Visual opacity of the unit when it starts to be disabled by CHRONO_GUN damage (engine default 100%)
* `ChronoDamageOpacityEnd = 25%`  ; Visual opacity when the unit reaches critical CHRONO_GUN damage (engine default 100%)
* `ChronoDamageParticleSystemLarge = ChronoSparksLarge`   ; ParticleSystem applied to STRUCTURES. (BOX volume scales to unit geometry; the three particle systems have no default)
* `ChronoDamageParticleSystemMedium = ChronoSparksMedium`  ; ParticleSystem applied to everyting else. (BOX volume scales to unit geometry)
* `ChronoDamageParticleSystemSmall = ChronoSparksSmall`   ; ParticleSystem applied to INFANTRY. (BOX volume scales to unit geometry)
  
### Water Depth Terrain Lighting

**Moved:** this feature was reworked and is no longer configured in `GameData.ini`. It is now a per-map setting in a `MapData` block — see [MapData](/GeneralsGameCode_Modding/ini/mapdata/#water-depth-terrain-lighting). The "work in progress" issues from the old `GameData.ini`-based version are resolved.

### Default Excluded Death types

Example:
* `DefaultExcludedDeathTypes = CHRONO`

This means the listed death types will be excluded from any Death module that uses "ALL -XXX" notation. It can be explicitly added by using "+" notation, e.g. "ALL +CHRONO". This can be used to define global exceptions without having to edit every existing death module.

### Scorchmarks on Water

* `HideScorchmarksAboveGround = No` - (If Yes, FXList `TerrainScorch` nuggets are skipped when the explosion is higher above the ground than the scorch's radius, or on a bridge, e.g. on top of the water surface. Useful to avoid floating scorch marks for naval combat. Default = No)

### Height Above Water

* `HeightAboveTerrainIncludesWater = No` - (If Yes, the water surface counts as the ground for several height checks: group move destinations and waypoints are placed on the water surface, whether a unit counts as an airborne target compares its height above terrain *or water* with the locomotor's `AirborneTargetingHeight`, OCL drop/height positions use the water surface, and shadow decals are drawn on the water. Meant for maps with ships. Default = No)

### Reverse Movement

* `ReverseMoveIgnoreAngleThreshold = No` - (Global override for the reverse move order (CommandButton `REVERSE_MOVE`). Normally a reverse move order only makes the unit drive backwards when the goal is behind it by more than the locomotor's `BackwardsMoveAngleThreshold`; if Yes, the angle check is skipped and the unit always reverses along the path. Automatic reversing is not affected. See [Locomotor reverse movement](/GeneralsGameCode_Modding/ini/locomotor/#reverse-backwards-movement). Default = No)

### Smart Garrison

`SmartGarrison` is a command that distributes a selected group of units across a target transport/structure **and nearby transports** (round-robin), instead of cramming everyone into the clicked one. Triggered by holding ALT while right-clicking (or hovering, for the cursor hint) a transport or structure the selected units can enter. There is no CommandButton for it. While hovering, the mouse shows the `SmartGarrison` cursor (define it in Mouse.ini).

* `SmartGarrisonRange = 100.0` - (Radius searched around the clicked target for additional transports/structures to distribute units into. Default = 100.0)

### Extra Veterancy Levels

Added two extra veterancy levels above HEROIC (see [New Enum Definitions](/GeneralsGameCode_Modding/reference/enums/#veterancy-levels)). Health bonuses for the new levels are defined here:

* `HealthBonus_Four = 100%` - (Maximum health multiplier at veterancy level `LEVEL_FOUR`, like `HealthBonus_Heroic = 150%` in vanilla. Default = 100%, no bonus.)
* `HealthBonus_Five = 100%` - (Same, at veterancy level `LEVEL_FIVE`. Default = 100%.)

Notes:
* These extend the existing `HealthBonus_Veteran` / `HealthBonus_Elite` / `HealthBonus_Heroic` parameters.
* See [ThingTemplate veterancy parameters](/GeneralsGameCode_Modding/systems/veterancy/#veterancy-object-parameters) for per-object level configuration.

### Weapon Scatter on Water

* `WeaponScatterOnWaterSurfaceDefault = No` - (Default value for the per-weapon `ScatterOnWaterSurface` flag when it is not set explicitly on a WeaponTemplate. Controls whether scattered shots that land on water detonate at the water surface. Individual weapons can still override this via `ScatterOnWaterSurface` in Weapon.ini. Default = No)

### Radius Decals Above Water

* `RadiusDecalsAboveWater = No` - (Global default. If Yes, radius decals (e.g. weapon/special-power range indicators, [RadiusDecalBehavior](/GeneralsGameCode_Modding/modules/updates-and-behaviors/#radiusdecalbehavior)) render above the water surface instead of being hidden/clipped by it. Default = No)

Note: a separate per-decal override exists for FXList-driven decals — see [W3DDecalDraw `RenderAboveWater`](/GeneralsGameCode_Modding/modules/draw/#w3ddecaldraw).

### Singleplayer Chat Window

* `EnableSingleplayerChatwindow = No` - (If Yes, the in-game chat window (Enter key) can be opened in singleplayer and skirmish games. This is required to use [Chat Commands](/GeneralsGameCode_Modding/systems/chat-commands/). Default = No)

### Render Interpolation and Render FPS Limit

* `UseRenderInterpolation = No` - (If Yes, drawables are drawn at positions interpolated between logic frames, so movement looks smooth when the renderer runs faster than the 30 logic frames per second. Ignored in headless games. Default = No)
* `RenderFramesPerSecondLimit = 0` - (Limit for rendered frames per second, separate from the logic `FramesPerSecondLimit`. The player's own render FPS option overrides it; it only affects that client. A value above 0 decouples rendering from the logic frame rate (only outside network games); `0` keeps rendering tied to the logic frame pacing. Default = 0)

## MiscAudio.ini

The following new entries in MiscAudio.ini are available

* `ChronoDisabledSoundAmbient = <AudioEvent>` - (looping) audio to play when an object is being removed/disabled by a CHRONO_GUN
