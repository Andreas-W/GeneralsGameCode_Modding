---
title: "Upgrade.ini"
description: "New Upgrade fields."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/System/Upgrade.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/ProductionUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp
sidebar:
  order: 31
---

## Added SilentCompletion parameter

* `SilentCompletion = No` - (When set to Yes, completing this upgrade in a production queue, or getting it from a purchased Science's `GrantUpgrades`, does not show the "upgrade complete" text message, does not play the `ResearchSound` or the generic upgrade-complete EVA event, and creates no radar event. The upgrade itself is applied as usual. An upgrade without a `DisplayName` is always silent.)
