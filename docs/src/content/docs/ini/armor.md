---
title: "Armor.ini"
description: "ArmorExtend."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Armor.cpp
  - Core/GameEngine/Source/Common/INI/INI.cpp
sidebar:
  order: 23
---

## ArmorExtend
Allows to inherit an Armor set from another one. The parent must already be loaded when the ArmorExtend is read (defined above it, or in a file that loads earlier); otherwise loading fails with an INI error.

### Syntax:
`ArmorExtend newArmorName ParentArmor`

this will copy all values from Parent before parsing the new values. Do not use `Armor = DEFAULT` as this will set all damage types to this value and overrides inherited values.
If an armor with the new name already exists, it is replaced.

### Coding example:
```ini
ArmorExtend TruckArmorExtended TruckArmor ; creates new TruckArmorExtended which inherits all values from TruckArmor
  Armor = SMALL_ARMS 5000% ; set a new value to SMALL_ARMS
End
```

ArmorExtend also allows to use `ArmorMult = <DamageType> <percent>` instead of `Armor`; this multiplies the value inherited from the parent (or set by an earlier line) by this factor. `ArmorMult = DEFAULT <percent>` multiplies all damage types. `ArmorMult` is only available in ArmorExtend, not in a plain `Armor` block. Make sure the damage type name is spelled correctly: an unknown name is not reported.
```ini
ArmorExtend DozerArmorExtended DozerArmor
  ArmorMult = EXPLOSION 1%
End
```
