---
title: "Drone carriers"
description: "Carriers that launch and recover drones."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Contain/DroneCarrierContain.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/DroneCarrierAIUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/DroneCarrierSlavedUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate/CarrierDroneAIUpdate.cpp
sidebar:
  order: 12
---


Modules for a simplified mobile aircraft carrier. The carrier builds its own drones (`DroneCarrierAIUpdate`), keeps them inside (`DroneCarrierContain`), launches them when its target is within range of its primary weapon and calls them back. The drones use `DroneCarrierSlavedUpdate` and `CarrierDroneAIUpdate`.

## DroneCarrierContain

The carrier's contain module. It extends `TransportContain`, so all its parameters work.

```ini
  Behavior = DroneCarrierContain ModuleTag_Contain
    ;<TransportContain parameters>
    Slots = 8
    ExitDelay = 300
    HealthRegen%PerSec = 3.0
    ...
    LaunchBone = STATION          ; drones launch from the bones STATION01, STATION02, ...
    NumLaunchBones = 1            ; number of launch bones, used in turn; 0 = no launch bones
    LaunchVelocityBoost = 0.0     ; push forward (along the drone's facing) when a drone launches; 0 = none
    EnterPositionOffset = X:0 Y:0 Z:0   ; point (relative to the carrier) a drone flies to when it returns, one per slot
    EnterPositionOffset = X:0 Y:0 Z:0   ; repeat for more slots; used in turn when there are fewer entries than slots
    ContainedUnitsDeathType = DETONATED ; death type of the drones inside when the carrier dies
    KeepSlotAssignment = No       ; Yes = each drone keeps its slot (and so its EnterPositionOffset) for its whole life
  End
```

Notes:
* Without `LaunchBone`, drones leave by the `TransportContain` exit rules (`ExitBone` + numbered bones when there are no exit paths).
* Drones inside reload their weapon clips over their clip reload time, counted from when they entered.

## DroneCarrierAIUpdate

The carrier's AI. It extends `AIUpdateInterface`, so all its parameters work.

```ini
  Behavior = DroneCarrierAIUpdate ModuleTag_AI
    ;<AIUpdate parameters>
    Slots = 0                     ; number of drones; the contain module needs at least as many slots
    RespawnTime = 0               ; time (ms) to build a replacement for a lost drone
    DroneTemplateName = <Object>  ; drone object(s) to build; several names (on one line or repeated) are built in turn
    DronesEnterMainDoor = No      ; Yes = the carrier opens a door while drones approach (model conditions CARRIER_DOOR_OPENING / CARRIER_DOOR_CLOSING)
  End
```

The carrier starts with all its drones. It sends them out while its target is within range of its own primary weapon (always, if it has none), and passes guard and attack orders on to them.

## DroneCarrierSlavedUpdate

On the drone. It extends `SlavedUpdate`, so all its parameters work.

```ini
  Behavior = DroneCarrierSlavedUpdate ModuleTag_123
    ;<SlavedUpdate parameters>
    LeashRange = 0.0              ; a drone farther than this from the carrier returns to it; 0 = no leash
  End
```

## CarrierDroneAIUpdate

The drone's AI. It extends `AIUpdateInterface`, so all its parameters work.

```ini
  Behavior = CarrierDroneAIUpdate ModuleTag_312
    DockingDistance = 0.0                ; distance to the carrier at which the drone starts its landing approach
    DockingLocomotorType = SET_NORMAL    ; locomotor set used while landing
    LaunchingLocomotorType = SET_NORMAL  ; locomotor set used while launching
    LaunchTime = 0                       ; time (ms) the launch state lasts
  End
```

Notes:
* While launching the drone has the model condition `TAKEOFF`; after `LaunchTime` it goes back to the `SET_NORMAL` locomotor.
* While landing it has the model condition `LANDING`, and it slows down as it gets closer to the carrier.
* Locomotor set names: `SET_NORMAL`, `SET_NORMAL_UPGRADED`, `SET_FREEFALL`, `SET_WANDER`, `SET_PANIC`, `SET_TAXIING`, `SET_SUPERSONIC`, `SET_SLUGGISH`, `SET_VTOL`.

Example values from a carrier: `DockingDistance = 70.0`, `DockingLocomotorType = SET_SLUGGISH`, `LaunchingLocomotorType = SET_TAXIING`, `LaunchTime = 750`.
