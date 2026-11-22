---
title: "Chat commands"
description: "ChatCommands.ini and the singleplayer chat window."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/ChatCommand.cpp
  - GeneralsMD/Code/GameEngine/Include/Common/ChatCommand.h
  - Core/GameEngine/Source/GameClient/GUI/GUICallbacks/InGameChat.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp
sidebar:
  order: 35
---

Added a system for user-defined "chat commands" - cheat/debug-style commands that the local player can trigger by typing them into the in-game chat window. They are defined in an optional `Data\INI\ChatCommands.ini` file. The file is client-only and not part of the INI CRC.

## Enabling the chat window in Singleplayer

Chat commands work in **singleplayer and skirmish** games only. By default the chat window cannot be opened there, so you need to enable it in `GameData.ini`:

* `EnableSingleplayerChatwindow = Yes`

With this set, the in-game chat window can be opened (Enter key) in singleplayer/skirmish. (See [GameData](/GeneralsGameCode_Modding/ini/gamedata/#singleplayer-chat-window).)

## Usage

Open the chat window and type a slash followed by the command name, e.g. `/money`. The first word after the slash is matched against the defined command names (not case-sensitive). If it matches a `ChatCommand` block, the command is executed for the local player, the message "Chat command executed: <name>" is shown, and the message is not sent as chat. A message without the leading `/` is not treated as a command.

## ChatCommands.ini

Each command is defined as a `ChatCommand <name> ... End` block. The `<name>` is what the player types to trigger it. All attributes are optional - a command can do one or several things at once.

* `AddMoney = 0` - (Signed amount of credits to add to the player; negative removes money, but never below 0. Plays the Cash Hack deposit sound. Default = 0)
* `AddRank = 0` - (Number of ranks (general's promotions) to grant, capped at the max rank. Default = 0)
* `ReadyTimers = No` - (If Yes, set all of the player's special power timers to ready, both per-object ones and the player's shared ones.)
* `SpawnObjectAtCursor = <ObjectTemplate>` - (Spawn one object of the given template for the local player on the terrain under the mouse cursor. Nothing happens when the cursor is not over the terrain or the template does not exist.)
* `TogglePrerequisites = No` - (If Yes, toggles ignoring unit/building build prerequisites. Science requirements still apply.)
* `ToggleInfiniteEnergy = No` - (If Yes, toggles infinite power for the local player.)
* `GrantAllUpgrades = No` - (If Yes, grants the local player all player-type upgrades.)
* `AddVeterancyLevel = 0` - (Promote the selected units by this many veterancy levels; negative demotes. Capped to the valid range. Only units that can gain experience are affected.)
* `AddSalvageTier = 0` - (Change the selected salvagers' crate-upgrade tier by this much; negative removes. Capped 0..2. Units with `KindOf = WEAPON_SALVAGER` get `CRATEUPGRADE_ONE` / `CRATEUPGRADE_TWO` weapon set flags, units with `ARMOR_SALVAGER` get the `CRATE_UPGRADE_ONE` / `CRATE_UPGRADE_TWO` armor set flags and the `ARMORSET_CRATEUPGRADE_ONE` / `_TWO` model conditions.)
* `ProductionSpeedMultiplier = 0.0` - (Sets the build-speed multiplier for the local player; > 1 builds faster, 1 is normal speed. It replaces the current value, it does not multiply it. 0 means the field is absent / no change.)
* `RevealMap = No` - (If Yes, reveals the whole map for the local player for the rest of the game, like the `MAP_REVEAL_ALL_PERM` script action.)

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

With these defined (and `EnableSingleplayerChatwindow = Yes`), typing `/money` in the chat window grants 10000 credits, etc.

Notes:
* Commands run only for the local player, in singleplayer/skirmish (not in multiplayer games). They are intended for testing/debugging and custom cheat setups.
* Command names must be unique; a duplicate `ChatCommand` name is an INI error.
* `AddVeterancyLevel` and `AddSalvageTier` act on the currently selected units.
* Fixed: `SpawnObjectAtCursor` now spawns the object through the game logic (in sync with the simulation) instead of directly on the client, avoiding desyncs/incorrect placement.
