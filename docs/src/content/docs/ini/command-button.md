---
title: "CommandButton.ini"
description: "New commands and command button options."
status: unverified
sidebar:
  order: 30
---

This page documents new `CommandButton` command types and parameters.

## REVERSE_MOVE (New Command)

A new context command that orders the selected unit(s) to move to a clicked ground position **driving in reverse**, instead of turning around first.

* `Command = REVERSE_MOVE`

It works like `ATTACK_MOVE`/`MOVE`: the player clicks the button, then clicks a target position on the terrain. The unit drives backwards along the whole path (no three-point turn), forcing reverse movement regardless of the [locomotor reverse heuristics](/GeneralsGameCode_Modding/ini/locomotor/#reverse-backwards-movement).

Example:
```ini
CommandButton Command_ReverseMove
  Command       = REVERSE_MOVE
  Options       = NEED_TARGET_POS
  TextLabel     = CONTROLBAR:ReverseMove
  ButtonImage   = SCReverseMove
  CursorName    = Move
  InvalidCursorName = GenericInvalid
End
```

Add the button to the unit's `CommandSet` like any other command button.

**Notes:**
* Requires a locomotor that can rotate in place / reverse (e.g. `TREADS`, `HOVER`). The reverse speed and behavior are tuned via the [Locomotor backwards-movement parameters](/GeneralsGameCode_Modding/ini/locomotor/#reverse-backwards-movement).
* Plays the normal move voice response.
* A manual `REVERSE_MOVE` order forces reversing for the entire path. The GameData flag [`ReverseMoveIgnoreAngleThreshold`](/GeneralsGameCode_Modding/ini/gamedata/#reverse-movement) controls whether a manual order reverses regardless of heading.

## Multi-Target Special Powers

CommandButton parameters were added to aim a special power at multiple target points in one activation (`NumberOfTargets`, anchor/target radii, markers, ...). See [Multi-Target Special Powers](/GeneralsGameCode_Modding/ini/special-power/#multi-target-special-powers).

## Sidebar Button Capacity

A `CommandSet` (sidebar) can now hold up to **32** command buttons, up from the vanilla limit of 18.
