---
title: "Locomotor.ini"
description: "Reverse movement, turning and LocomotorExtend."
status: unverified
sidebar:
  order: 24
---

## Locomotor.ini

### Reverse (Backwards) Movement

Locomotors that can rotate in place (e.g. `TREADS`, `HOVER`) can now reverse towards a goal that is behind them instead of always turning around first. The behavior is now configurable per locomotor.

* `BackwardsMoveAngleThreshold = 90` - (The goal must be at least this many degrees off the current heading before the unit reverses instead of turning around. Default = 90°)
* `BackwardsMoveDistanceFactorThreshold = 5.0` - (Maximum reverse distance, as a factor of the object's MajorRadius. Beyond this the unit turns around instead. Default = 5.0)
* `BackwardsMoveSpeedFactor = 1.0` - (Speed multiplier applied while moving backwards. E.g. 0.5 = reverse at half speed. Default = 1.0)

Notes:
* Defaults match the values the hardcoded backwards-movement logic used before these were configurable.
* A global override exists in GameData: [`ReverseMoveIgnoreAngleThreshold`](/GeneralsGameCode_Modding/ini/gamedata/#reverse-movement).
* AIUpdate can use reversing to stay out of a weapon's minimum range: [`MinRangeRetreatStraight` / `AimByReversing`](/GeneralsGameCode_Modding/modules/ai-update/#minimum-range-handling-reversing).
* `AnimationSteeringUpdate` can set the `MOVING_BACKWARDS` condition state while reversing: [`BackwardsCondition`](/GeneralsGameCode_Modding/systems/animation-blending/#animationsteeringupdate).

### Turning without pivoting

* `RequiresMoveToTurn = No` - (If Yes, the unit cannot rotate in place even when `MinSpeed = 0`. When it has to face a direction (aiming, facing, PreferredAttackAngle) it drives forward and turns through its normal movement, using `MinTurnSpeed` (at least 1/4 of max speed), until it is within about 2° of the goal. Meant for `WHEELS` vehicles that should not spin on the spot.)

### Ships
#### TODO

## LocomotorExtend

Locomotor definitions can be extended via a LocomotorExtend entry:

- `LocomotorExtend &lt;NewLocomotor&gt; &lt;ParentLocomotor&gt;

All parameters from Locomotor can be used to override values from the parent Locomotors . It is possible to extend an extended Locomotor.
Note: The extended locomotor needs to be defined below the parent locomotor.
