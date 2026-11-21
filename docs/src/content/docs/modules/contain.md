---
title: "Contain modules"
description: "Changes to transports, garrisons and other containers."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/OpenContain.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/GarrisonContain.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/TransportContain.cpp
  - Core/GameEngine/Source/Common/INI/INI.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/MultiAddOnContain.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/OverlordContain.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DOverlordAircraftDraw.cpp
  - Core/GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DDependencyModelDraw.cpp
sidebar:
  order: 4
---

## All contain modules

Added new features for all contain modules. WeaponBonus types that are passed to passengers can now be configured.

* `PassengerWeaponBonusList = <BONUS1> <BONUS2>` - (provide a list of WeaponBonus types that will be set on the contained objects while they are inside, and cleared when they leave)

Default values:
- GarrisonContain: GARRISONED
- TransportContain and the modules built on it (HelixContain, OverlordContain, MultiAddOnContain, RiderChangeContain, InternetHackContain, DroneCarrierContain, RailedTransportContain): CONTAINED
- all other contain modules: none

The listed types are **added** to the default, and several `PassengerWeaponBonusList` lines add up. `None` clears the list (including the default); anything after `None` on the same line is ignored. To replace the default, clear it first:

```ini
PassengerWeaponBonusList = None
PassengerWeaponBonusList = EXTRA1   ; only EXTRA1, no CONTAINED
```

Note: HelixContain grants GARRISONED to its passengers in vanilla ZH. This is changed to CONTAINED. To restore vanilla behaviour you will need to manually set the bonus here.

## MultiAddOnContain

**New in this fork.**

New contain module to replicate MARV turret functionality from Kane's Wrath:
The object (Can be used on a structure or vehicle) has a container for multiple infantry units.
Each contained infantry, will spawn a portable structure on the unit, that will move and attack along with it.
Each type of infantry has its own portable structure defined. Only units listed in an `AddOnEntry` can enter.
There is no limit on how many addOns can be defined. The number of slots is the container's `Slots`; sub object names are numbered like the bones (`COVER_EMPTY01`, `COVER_EMPTY02`, ...).

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
  AddOnBoneName = STATION  ;Bone prefix to place the addOns; slot N uses <AddOnBoneName><NN>, numbered from 01 (default FIREPOINT)
  AddOnEntry = <Name of contained Infantry object 1> <Name of Portable Structure object 1>
  AddOnEntry = <Name of contained Infantry object 2> <Name of Portable Structure object 2>
  AddOnEntry = <Name of contained Infantry object 3> <Name of Portable Structure object 3>
  ...
  EmptySlotSubObjectName = COVER_EMPTY   ;Name of any subobject (numbered) that should be visible when a slot is empty
  OccupiedSlotSubObjectName = COVER_OCCUPIED   ;Name of any subobject (numbered) that should be visible when a slot is filled (hide this in art code by default)
  ShouldDrawPips = Yes   ;Show the container's passenger pips
End
```
### Container Draw Module Requirements

Like with OverlordContain, the container object needs to have one of

- `W3DOverlordTankDraw`
- `W3DOverlordTruckDraw`
- `W3DOverlordAircraftDraw`  - Note: Contrary to the name, this is the default module to use (e.g for Structures) when no extra behavior is required

A new parameter was added to these modules that needs to be set when used with MultiAddOnContain:
* `HasMultiAddOns = Yes`

### AddOn Draw Module Requirements

Like with Overlord addOns, the contained portable structure object needs to have `W3DDependencyModelDraw`

`AttachToBoneInContainer` should be set to the same bone prefix used for `AddOnBoneName` in MultiAddOnContain, e.g. `STATION` if using STATION01-XX. The draw module adds the slot number itself and falls back to the bone without number.

## Multiple Portable Structures (Overlord contain)

`OverlordContain` (and Overlord-style contain redirection) now allows holding multiple add-ons at once, as long as they have `KindOf = PORTABLE_STRUCTURE`. Objects with this KindOf are always added directly to the Overlord's own contain list (rather than being redirected to a single passenger), so several portable structures can be mounted on the same carrier. See also [MultiAddOnContain](#multiaddoncontain).

New `OverlordContain` parameter:
* `AddOnBoneName = FIREPOINT` - (Bone prefix the riders sit on. Rider N (in contain order) is placed at `<AddOnBoneName><NN>` (e.g. `FIREPOINT01`, `FIREPOINT02`), falling back to the bone without number, then to the container's own position. Must match the riders' `AttachToBoneInContainer`, so the logic position (turret aiming, projectile launch) matches where the rider is drawn. With `PassengersInTurret = Yes` the numbered bones are looked up on the main turret, without the fallback to the bone without number.)
