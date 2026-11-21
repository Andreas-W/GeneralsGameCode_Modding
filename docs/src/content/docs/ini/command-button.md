---
title: "CommandButton.ini"
description: "New commands and command button options."
status: verified
verifiedAt: 7cdd826c5
sources:
  - Core/GameEngine/Include/GameClient/ControlBar.h
  - Core/GameEngine/Source/GameClient/MessageStream/GUICommandTranslator.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Locomotor.cpp
sidebar:
  order: 30
---

This page documents new `CommandButton` command types and parameters.

## REVERSE_MOVE (New Command)

A new context command that orders the selected unit(s) to move to a clicked ground position **driving in reverse**, instead of turning around first.

* `Command = REVERSE_MOVE`

It works like `ATTACK_MOVE` (there is no `MOVE` command type): the player clicks the button, then clicks a target position on the terrain. The unit drives backwards along the whole path (no three-point turn), forcing reverse movement regardless of the [locomotor reverse heuristics](/GeneralsGameCode_Modding/ini/locomotor/#reverse-backwards-movement).

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
* Requires a locomotor with `CanMoveBackwards = Yes` and an appearance that supports reversing (`FOUR_WHEELS`, `MOTORCYCLE`, `TREADS` or `OTHER`; not `HOVER`). The reverse speed and behavior are tuned via the [Locomotor backwards-movement parameters](/GeneralsGameCode_Modding/ini/locomotor/#reverse-backwards-movement).
* Plays the normal move voice response.
* A manual `REVERSE_MOVE` order forces reversing for the entire path, but by default only when the goal is behind the unit by more than the locomotor's `BackwardsMoveAngleThreshold`. The GameData flag [`ReverseMoveIgnoreAngleThreshold`](/GeneralsGameCode_Modding/ini/gamedata/#reverse-movement) makes a manual order reverse regardless of heading.

## Multi-Target Special Powers

CommandButton parameters were added to aim a special power at multiple target points in one activation (`NumberOfTargets`, anchor/target radii, markers, ...). See [Multi-Target Special Powers](/GeneralsGameCode_Modding/ini/special-power/#multi-target-special-powers).

## Sidebar Button Capacity

A `CommandSet` (sidebar) can now hold up to **32** command buttons (`MAX_COMMANDS_PER_SET`), up from the vanilla limit of 18. Slots are numbered `1` to `32`. How many are visible depends on the control bar layout (vanilla shows 14).
