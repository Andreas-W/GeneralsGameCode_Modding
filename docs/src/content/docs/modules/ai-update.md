---
title: "AI update modules"
description: "Changes to AIUpdateInterface and the other AIUpdate modules."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/AIUpdate.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/AI/AIStates.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/AI/TurretAI.cpp
sidebar:
  order: 1
---

## AIUpdateInterface (And all other AIUpdate types)

New experimental parameter:
* `PreferredAttackAngle = Min:<degrees> Max:<degrees>` - (an allowed range of angles, relative to the unit's facing, at which the target should be. The line can be repeated to add more ranges. Without `Max:` the range ends at 360; `Max:` alone starts the range at 0.)
When a unit turns to attack, it turns so that the target lies in the closest of these ranges. Possibly useful for battleship setups with multiple turrets, e.g. a broadside on either side:

```ini
  PreferredAttackAngle = Min:45 Max:135
  PreferredAttackAngle = Min:-135 Max:-45
```

Only works for locomotors that can turn in place (minTurnSpeed = 0). The older syntax `PreferredAttackAngle = <angle> [MIRRORED]` is no longer supported: every line must start with `Min:` or `Max:`.
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
New parameters for Turret or AltTurret entries (and `Turret1` ... `Turret8`, see [Turrets](/GeneralsGameCode_Modding/systems/turrets/))
* `MinTurretAngle = 0` - Minimum angle (degrees) the turret is allowed to turn
* `MaxTurretAngle = 0` - Maximum angle (degrees) the turret is allowed to turn

Setting either of them limits the turret; without both lines the turret is unlimited.
Notes:
- for backwards facing configurations, MaxTurretAngle can be > MinTurretAngle; Currently this is not working 100% reliably
- Remove the angle limit lines to use unlimited angle. A value of 0 will use 0 as limit.
- If the turret cannot turn to the front (e.g. side mounted gun on a helicopter), the unit will attempt to turn to the turret's firing arc
  - this feature only works for locomotors that can turn in place (minTurnSpeed = 0)
