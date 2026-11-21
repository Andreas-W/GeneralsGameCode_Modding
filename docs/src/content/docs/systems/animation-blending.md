---
title: "Animation blending"
description: "Blending between model animations and steering animations."
status: unverified
sidebar:
  order: 16
---

A set of features to smoothly blend between animations and trigger frame-accurate effects. (PRs #85, #86, #91, #93)

## W3DModelDraw (module level)

* `KeepRecoilAcrossStates = No` - (If Yes, weapon recoil offset is kept when the model switches condition states, instead of being reset. Avoids the gun "snapping back" when the firing state changes. Default = No)

## ConditionState parameters

* `AnimationBlendTime = 0` - (Per ConditionState; number of animation frames to blend from the previous state's animation into this state's animation, for a smooth transition. Default = 0 = no blending)

* `AutoConditionState = <ConditionFlag1> <ConditionFlag2> ...` - (Shorthand that auto-generates a combined condition state from the **previously defined** ConditionState by OR-ing in the listed condition flags. It copies the previous state's model/animation/overrides and creates a new state for each of the previous state's condition sets combined with the auto flags. It has no body.)

  Requirements/notes:
  - Must come after a regular ConditionState (it refers to the last defined one).
  - Requires at least one condition flag.
  - It is an asset error if it would produce a condition state that already exists.
  - Useful for animation blending, e.g. to duplicate a state for an additional flag without retyping the whole block.

* `FXEvent = <frame> <boneName> <FXList>` - (Per ConditionState; plays the given FXList at the given bone when the animation playback crosses the given frame number. Can be added multiple times. Lets you trigger effects at exact animation frames, e.g. footstep dust or muzzle smoke.)

## AnimationSteeringUpdate

Existing (retail) behavior module that plays steering animations from the unit's turning, as used by the GLA Combat Cycle. When the unit starts turning it sets `CENTER_TO_LEFT` / `CENTER_TO_RIGHT`; when it stops, it switches to `LEFT_TO_CENTER` / `RIGHT_TO_CENTER` and clears that once the unit drives straight. New parameters were added to the retail `MinTransitionTime`.

```ini
Behavior = AnimationSteeringUpdate ModuleTag_AnimSteer
  MinTransitionTime = 0     ; (retail) minimum time in ms between two steering state changes
  MinAngle = 0              ; (new) degrees; don't start a turn animation while the AI's goal is less than this far off the nose
  SkipCenteringAnims = No   ; (new) if Yes, only CENTER_TO_LEFT / CENTER_TO_RIGHT are used; they are cleared directly when the turn ends, without LEFT_TO_CENTER / RIGHT_TO_CENTER
  BackwardsCondition = No   ; (new) if Yes, set the MOVING_BACKWARDS condition state while the unit drives in reverse
  BackwardsTurnAnims = No   ; (new) if Yes, keep playing the turn animations while reversing (otherwise they are suppressed)
End
```

Notes:
* `MinAngle` uses the AI's current goal object or goal position. It only prevents a turn animation from starting; a running one finishes as usual. 0 = off (retail behavior).

Notes on reversing (`BackwardsCondition = Yes`):
* `MOVING_BACKWARDS` follows the actual movement (the unit's speed is backwards), with a little hysteresis so it doesn't flicker on small nudges.
* Without `BackwardsTurnAnims`, the turn conditions (`CENTER_TO_LEFT`, `CENTER_TO_RIGHT`, `LEFT_TO_CENTER`, `RIGHT_TO_CENTER`) are cleared while reversing. With it, `MinAngle` is measured against the unit's rear.
