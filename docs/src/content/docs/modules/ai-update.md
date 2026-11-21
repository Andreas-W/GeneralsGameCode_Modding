---
title: "AI update modules"
description: "Changes to AIUpdateInterface and the other AIUpdate modules."
status: unverified
sidebar:
  order: 1
---

## AIUpdateInterface (And all other AIUpdate types)

New experimental parameter:
* `PreferredAttackAngle = <angle> [MIRRORED]` - (mirrored keyword is optional)
When a unit turns to attack, it will attempt to turn to this angle. If mirrored is enable it can also use the same angle +180°. Possibly useful for battleship setups with multiple turrets. Only works for locomotors that can turn in place (minTurnSpeed = 0)
Note: this is NOT needed for limited turret angles (See below). Units will follow the actual turret angles when trying to attack.

### Minimum range handling (reversing)
New parameters for vehicles with a weapon `MinimumAttackRange`, mainly slow-turning units without a turret (e.g. artillery). They use the locomotor's reverse movement, so the locomotor needs `CanMoveBackwards = Yes` (see [Locomotor](/GeneralsGameCode_Modding/ini/locomotor/#reverse-backwards-movement)).

* `MinRangeRetreatStraight = No` - (When the unit is inside its weapon's minimum range, it backs off straight away from the target, just far enough to get back into range (min range + one pathfind cell). Without it the pathfinder picks the nearest valid cell, often to the side, which is a long arc for slow-turning vehicles and leaves them facing the wrong way. Falls back to normal attack pathfinding if the straight spot is blocked. Ground units only.)
* `AimByReversing = No` - (Units without a turret that have to turn to aim: if the target is in front and a forward turn would carry the unit into its minimum range, it turns by backing up instead. Once it started reversing it keeps reversing until aligned. Not used if turning would take the unit out of its maximum range.)
* `AimByReversingMaxAngle = 90` - (Only reverse to aim if the target is at most this many degrees off the nose.)
* `AimByReversingMargin = 10.0` - (Safety margin, in world units, added to the minimum range in the "would a forward turn get too close" test.)
* `AimByReversingTurnFactor = 1.0` - (Scales the estimate of how much closer a forward turn brings the unit (turn radius × sin(angle)). Raise it if units still drive into their minimum range while turning, lower it if they reverse too eagerly.)

```ini
Behavior = AIUpdateInterface ModuleTag_AI
  ...
  MinRangeRetreatStraight = Yes
  AimByReversing = Yes
  AimByReversingMaxAngle = 90
End
```

### Turret
New paramters for Turret or AltTurret entries
* `MinTurretAngle = 0` - Minimum angle the turret is allowed to turn
* `MaxTurretAngle = 0` - Maximum angle the turret is allowed to turn
Notes:
- for backwards facing configurations, MaxTurretAngle can be > MinTurretAngle; Currently this is not working 100% reliably
- Remove the angle limit lines to use unlimited angle. A value of 0 will use 0 as limit.
- If the turret cannot turn to the front (e.g. side mounted gun on a helicopter), the unit will attempt to turn to the turret's firing arc
  - this feature only works for locomotors that can turn in place (minTurnSpeed = 0)
