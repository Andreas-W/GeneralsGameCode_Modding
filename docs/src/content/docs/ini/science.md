---
title: "Science.ini"
description: "New Science fields."
status: unverified
sidebar:
  order: 32
---

## Added GrantUpgrades parameters

* `GrantUpgrades = <Upgrade1> <Upgrade2> ...` ; (multiple entries from Upgrade.ini)

When picking the science, this grants the upgrades for the player. OBJECT type upgrades are not possible.
This will play ResearchCompletion sounds and show the text notification, unless the Upgrade has SilentCompletion (or no description text defined)
