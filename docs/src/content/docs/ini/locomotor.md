---
title: "Locomotor.ini"
description: "Reverse movement, turning and LocomotorExtend."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Locomotor.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Locomotor.h
  - Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp
sidebar:
  order: 24
---

## Locomotor.ini

### Reverse (Backwards) Movement

Locomotors with `CanMoveBackwards = Yes` reverse towards a goal that is behind them instead of always turning around first. Vanilla only did this for wheeled locomotors (`Appearance = FOUR_WHEELS` / `MOTORCYCLE`); `TREADS` and `OTHER` locomotors now do it too (not `HOVER`, `SHIP`, legs or aircraft), and the thresholds are configurable per locomotor. Without `CanMoveBackwards = Yes` (default No) none of the fields below have an effect.

* `BackwardsMoveAngleThreshold = 90` - (Degrees. The goal must be more than this many degrees off the current heading before the unit reverses instead of turning around. Default = 90)
* `BackwardsMoveDistanceFactorThreshold = 5.0` - (Maximum reverse distance, as a factor of the object's `GeometryMajorRadius`. Beyond this a tracked unit turns around instead, a wheeled one does a three-point turn. Default = 5.0)
* `BackwardsMoveSpeedFactor = 1.0` - (Speed multiplier applied while moving backwards. E.g. 0.5 = reverse at half speed. Default = 1.0)

Notes:
* Reversing only starts while the unit stands still, and stops once the goal is in front of it again (or farther away than the reverse distance).
* Defaults match the values the hardcoded backwards-movement logic used before these were configurable.
* A global override exists in GameData: [`ReverseMoveIgnoreAngleThreshold`](/GeneralsGameCode_Modding/ini/gamedata/#reverse-movement).
* AIUpdate can use reversing to stay out of a weapon's minimum range: [`MinRangeRetreatStraight` / `AimByReversing`](/GeneralsGameCode_Modding/modules/ai-update/#minimum-range-handling-reversing).
* `AnimationSteeringUpdate` can set the `MOVING_BACKWARDS` condition state while reversing: [`BackwardsCondition`](/GeneralsGameCode_Modding/systems/animation-blending/#animationsteeringupdate).

### Turning without pivoting

* `RequiresMoveToTurn = No` - (If Yes, the unit cannot rotate in place even when `MinSpeed = 0`. When it has to face a direction (aiming, facing, PreferredAttackAngle) it drives forward and turns through its normal movement, using `MinTurnSpeed` (at least 1/4 of max speed), until it is within about 2° of the goal. Meant for wheeled vehicles (`Appearance = FOUR_WHEELS`) that should not spin on the spot. Default = No)

### Ships

* `Appearance = SHIP` - (New appearance for ships. It moves like `HOVER` (same movement and position keeping code); the drawable and the AI treat it as a ship.)
* `RequiredWaterLevel = 0` - (Only for `Appearance = SHIP`. Water cells are only passable when they are at least this many cells away from the shore, so large ships keep off shallow coasts. `0` = no requirement. A value above 0 on a non-SHIP locomotor is a debug error. When an object has several ship locomotors, the largest value is used. Default = 0)

Not documented further here yet; see the source.

## LocomotorExtend

Locomotor definitions can be extended via a LocomotorExtend entry:

- `LocomotorExtend <NewLocomotor> <ParentLocomotor>`

All parameters from Locomotor can be used to override values from the parent locomotor. It is possible to extend an extended Locomotor.

```ini
LocomotorExtend FastTankLocomotor BasicTankLocomotor
  Speed = 45
End
```

Note: The parent must already be loaded when the LocomotorExtend is read: define it below the parent locomotor (or in a file that loads later). A missing parent is a debug crash and the locomotor is not created.
