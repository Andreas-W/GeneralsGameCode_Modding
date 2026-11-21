---
title: "Armor.ini"
description: "ArmorExtend."
status: unverified
sidebar:
  order: 23
---

## ArmorExtend
Allows to inherit an Armor set from another one. The parent must be defined before

### Syntax:
`ArmorExtend newArmorName ParentArmor`

this will copy all values from Parent before parsing the new values. Do not use `Armor = DEFAULT` as this will set all damage types to this value and overrides inherited values.

### Coding example:
```ini
ArmorExtend TruckArmorExtended TruckArmor ; creates new TruckArmorExtended which inherits all values from TruckArmor
  Armor = SMALL_ARMS 5000% ; set a new value to SMALL_ARMS
End
```

ArmorExtend also allows to use ArmorMult instead of armor, this will multiply the armor value inherited by the parent with this factor
```ini
ArmorExtend DozerArmorExtended DozerArmor
  ArmorMult = EXPLOSION 1%
End
```
