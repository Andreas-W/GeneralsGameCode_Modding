---
title: "Contain modules"
description: "Changes to transports, garrisons and other containers."
status: unverified
sidebar:
  order: 4
---

## Contain Modules

Added new features for all contain modules. WeaponBonus types that are passed to passengers can now be configured.

* `PassengerWeaponBonusList = <BONUS1> <BONUS2>` - (provide a list of WeaponBonus types that will be applied to the contained objects)

Default values:
- GarrisonContain: GARRISONED
- TransportContain: CONTAINED

Setting `PassengerWeaponBonusList = None` will override the default value.

Note: HelixContain grants GARRISONED to its passengers in vanilla ZH. This is changed to CONTAINED. To restore vanilla behaviour you will need to manually set the bonus here.

## MultiAddOnContain

**New in this fork.**

New contain module to replicate MARV turret functionality from Kane's Wrath:
The object (Can be used on a structure or vehicle) has a container for multiple infantry units.
Each contained infantry, will spawn a portable structure on the unit, that will move and attack along with it.
Each type of infantry has it's own portable structure defined. Only units defined in the list can enter.
There is no limit on how many addOns can be defined.

Example:

```ini
Behavior = MultiAddOnContain ModuleTag_123
  ; Params from TransportContain:  
  Slots = 4
  EnterSound = GarrisonEnter
  ExitSound = GarrisonExit
  ImmuneToClearBuildingAttacks = Yes
  DamagePercentToUnits = 100%
  IsEnclosingContainer = No
  ; New Params
  PayloadTemplateName = <Name of Infantry object to be contained by default> ; can have multiple PayloadTemplateName entries
  AddOnBoneName = STATION  ;Name of bone to place the addOns (uses numbering from 00-99 for available slots)
  AddOnEntry = <Name of contained Infantry object 1> <Name of Portable Structure object 1>
  AddOnEntry = <Name of contained Infantry object 2> <Name of Portable Structure object 2>
  AddOnEntry = <Name of contained Infantry object 3> <Name of Portable Structure object 3>
  ...
  EmptySlotSubObjectName = COVER_EMPTY   ;Name of any subobject (numbered) that should be visible when a slot is empty
  OccupiedSlotSubObjectName = COVER_OCCUPIED   ;Name of any subobject (numbered) that should be visible when a slot is filled (hide this in art code by default)
End
```
### Container Draw Module Requirements

Like with OverlordContain, the container object needs to have one of

- `W3DOverlordTankDraw`
- `W3DOverlordTruckDraw`
- `W3DOverlordAircraftDraw`  - Note: Contary to the name, this is the default module to use (e.g for Structures) when no extra behavior is required

A new parameter was added to these modules that needs to be set when used with MultiAddOnContain:
* `HasMultiAddOns = Yes`

### AddOn Draw Module Requirements

Like with Overlord addOns, the contained portable structure object needs to have `W3DDependencyModelDraw`

`AttachToBoneInContainer` should now be set to the same numbered bone used for `AddOnBoneName` in MultiAddonContain. E.g. "STATION" if using STATION01-XX

## Multiple Portable Structures (Overlord contain)

`OverlordContain` (and Overlord-style contain redirection) now allows holding multiple add-ons at once, as long as they are `KINDOF_PORTABLE_STRUCTURE`. Objects with this KindOf are always added directly to the Overlord's own contain list (rather than being redirected to a single passenger), so several portable structures can be mounted on the same carrier. See also [MultiAddOnContain](#multiaddoncontain).

New `OverlordContain` parameter:
* `AddOnBoneName = FIREPOINT` - (Bone prefix the riders sit on. Rider N (in contain order) is placed at `<AddOnBoneName><NN>` (e.g. `FIREPOINT01`, `FIREPOINT02`), falling back to the bone without number, then to the container's own position. Must match the riders' `AttachToBoneInContainer`, so the logic position (turret aiming, projectile launch) matches where the rider is drawn. With `PassengersInTurret = Yes` the bones are looked up on the main turret.)
