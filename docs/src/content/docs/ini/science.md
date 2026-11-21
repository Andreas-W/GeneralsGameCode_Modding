---
title: "Science.ini"
description: "New Science fields."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/Science.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp
sidebar:
  order: 32
---

## Added GrantUpgrades parameters

* `GrantUpgrades = <Upgrade1> <Upgrade2> ...` - (Upgrades from Upgrade.ini, all on one line. A second `GrantUpgrades` line replaces the first. Default = none)

When the player gets the science, this grants the upgrades to the player, however the science was obtained (purchased, granted by script or by `GrantScienceUpgrade`). Only `Type = PLAYER` upgrades are granted; OBJECT type upgrades are skipped. Unknown upgrade names are skipped (they only show up in the debug log).

Only when the player purchases the science himself (clicking the button), the upgrade's `ResearchSound` is played (or the generic "upgrade complete" EVA event) and the "upgrade complete" text is shown, unless the upgrade has `SilentCompletion = Yes` or no `DisplayName`. Sciences bought by the AI or by script, and granted sciences, give their upgrades silently.
