---
title: "Chat commands"
description: "ChatCommands.ini and the singleplayer chat window."
status: unverified
sidebar:
  order: 35
---

Added a system for user-defined "chat commands" - cheat/debug-style commands that the local player can trigger by typing them into the in-game chat window. They are defined in an optional `ChatCommands.ini` file.

## Enabling the chat window in Singleplayer

Chat commands work in **singleplayer and skirmish** games only. By default the chat window cannot be opened there, so you need to enable it in `GameData.ini`:

* `EnableSingleplayerChatwindow = Yes`

With this set, the in-game chat window can be opened (Enter key) in singleplayer/skirmish. (See [GameData](/GeneralsGameCode_Modding/ini/gamedata/#singleplayer-chat-window).)

## Usage

Open the chat window and type the command name (the first word of the message is matched against the defined command names). If it matches a `ChatCommand` block, the command is executed for the local player and the message is not sent as chat.

## ChatCommands.ini

Each command is defined as a `ChatCommand <name> ... End` block. The `<name>` is what the player types to trigger it. All attributes are optional - a command can do one or several things at once.

* `AddMoney = 0` - (Signed amount of credits to add to the player. Default = 0)
* `AddRank = 0` - (Number of ranks (general's promotions) to grant, capped at the max rank. Default = 0)
* `ReadyTimers = No` - (If Yes, set all of the player's special power timers to ready.)
* `SpawnObjectAtCursor = <ObjectTemplate>` - (Spawn the given object template for the local player at the mouse cursor.)
* `TogglePrerequisites = No` - (If Yes, toggles ignoring unit/building build prerequisites. Science requirements still apply.)
* `ToggleInfiniteEnergy = No` - (If Yes, toggles infinite power for the local player.)
* `GrantAllUpgrades = No` - (If Yes, grants the local player all player-type upgrades.)
* `AddVeterancyLevel = 0` - (Promote the selected units by this many veterancy levels; negative demotes. Capped to the valid range.)
* `AddSalvageTier = 0` - (Change the selected salvagers' crate-upgrade tier by this much; negative removes. Capped 0..2.)
* `ProductionSpeedMultiplier = 0.0` - (Build-speed multiplier for the local player; > 1 builds faster. 0 means the field is absent / no change.)

## Example

```ini
ChatCommand money
  AddMoney = 10000
End

ChatCommand maxrank
  AddRank = 5
  GrantAllUpgrades = Yes
End

ChatCommand fast
  ProductionSpeedMultiplier = 5.0
  ToggleInfiniteEnergy = Yes
End

ChatCommand spawntank
  SpawnObjectAtCursor = AmericaTankCrusader
End
```

With these defined (and `EnableSingleplayerChatwindow = Yes`), typing `money` in the chat window grants 10000 credits, etc.

Notes:
* Commands run only for the local player, in singleplayer/skirmish. They are intended for testing/debugging and custom cheat setups.
* `AddVeterancyLevel` and `AddSalvageTier` act on the currently selected units.
* Fixed: `SpawnObjectAtCursor` now spawns the object through the game logic (in sync with the simulation) instead of directly on the client, avoiding desyncs/incorrect placement.
