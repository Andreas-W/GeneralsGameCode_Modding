---
title: "Drone carriers"
description: "Carriers that launch and recover drones."
status: unverified
sidebar:
  order: 12
---

## Drone Carrier system

Added modules to support a simplified mobile aircraft carrier.

### DroneCarrierContain

```ini
  Behavior = DroneCarrierContain ModuleTag_Contain
    ;<TransportContain parameters>
    Slots = 123
    HealthRegen%PerSec = 3.0
    ExitBone = STATION
    ExitDelay = 300
    HealthRegen%PerSec = 3.0
    ...
    ;
    LaunchVelocityBoost = 2.5
    LaunchBone = STATION
    NumLaunchBones = 1
    EnterPositionOffset = X:0 Y:0 Z:0
    EnterPositionOffset = X:0 Y:0 Z:0  ;Multiple entries allowed
    ...
    ContainedUnitsDeathType = TOPPLED
    KeepSlotAssignment = Yes
  End
```
### DroneCarrierAIUpdate

```ini
  Behavior = DroneCarrierAIUpdate ModuleTag_AI
    ;<AIUpdate parameters>
    Slots = 8
    RespawnTime = 3000
    DroneTemplateName = Lazr_AmericaVehicleCarrierDrone
    DronesEnterMainDoor = Yes
  End
```

### DroneCarrierSlavedUpdate

```ini
  Behavior = DroneCarrierSlavedUpdate ModuleTag_123
    <SlavedUpdate params>
    LeashRange = 400.0
  End
```

### CarrierDroneAIUpdate
```ini
  Behavior = CarrierDroneAIUpdate ModuleTag_312
    DockingDistance = 70.0
    DockingLocomotorType = SET_SLUGGISH
    LaunchingLocomotorType = SET_TAXIING
    LaunchTime = 750
  End
```
TODO: In-depth description
