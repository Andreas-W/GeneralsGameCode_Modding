---
title: "Bridges"
description: "Destroyable road bridges and drawbridges."
status: verified
verifiedAt: 7cdd826c5
sources:
  - Core/GameEngine/Source/GameClient/Terrain/TerrainRoads.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Map/TerrainLogic.cpp
  - GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DBridgeBuffer.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/BridgeBehavior.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/Thing/ThingTemplate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/DrawBridgeUpdate.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/DrawBridgeTowerUpdate.cpp
sidebar:
  order: 14
---

## Roads.ini: Bridge

Bridges are `Bridge` blocks in `Roads.ini`. A landmark bridge (a bridge object placed on the map) uses the `Bridge` block with the same name as its object.

* `BridgeHoleAreaPercentage = 0%` - (Part of the span, centered on the middle and across the full width, that becomes impassable while the bridge is destroyed or a drawbridge is open. 0 (not set) = the whole deck. Used by destroyable road bridges, landmark bridges and drawbridges. A drawbridge only pushes and crushes units inside this area, so give drawbridges a value.)

### Destroyable road bridges

Road bridges (the procedural bridges placed with the bridge tool in WorldBuilder, drawn from `BridgeModelName`) can now be destroyed and rebuilt like landmark bridges. Before, they were always indestructible and had no towers.

* `Destroyable = No` - (If Yes, the bridge can be damaged and destroyed. It is owned by the neutral player and gets the 4 targetable towers from `TowerObjectNameFromLeft/FromRight/ToLeft/ToRight`, which a Dozer/Worker can use to rebuild it (if the span object's `BridgeBehavior` has `Restoreable = Yes`). The deck model switches to `BridgeModelNameDamaged` / `BridgeModelNameReallyDamaged` / `BridgeModelNameBroken` (and the matching `Texture...`) with the span object's damage state.)
* `BridgeObjectName = GenericBridge` - (Object template that represents the bridge span in the logic. For a destroyable bridge this object needs a body, armor and `BridgeBehavior`, and `KindOf` should include `BRIDGE`. Its geometry is a placeholder: at runtime the box is resized to the bridge (half span length × half bridge width) so area weapons aimed at the deck hit it, and it collapses to nothing while the bridge is destroyed. Leave out to use `GenericBridge` as before.)
* `BridgeDeckHeight = 0.0` - (Thickness of the deck (and girders/arches) below the driving surface. It is subtracted from the clearance under the bridge, so units that need clearance (see `RequiredBridgeHeight` below) have to fit below the bottom of the deck, not the road surface. Applies to all bridge types.)

The sectional deck has no skeleton, so it cannot play a model animation. These parameters drive a procedural collapse instead: span sections fold toward the middle and drop. All default to 0, which keeps the old instant model swap.

* `BridgeCollapseDuration = 0` - (Time in ms the collapse animation runs. The broken model is swapped in when it ends. 0 = no animation.)
* `BridgeCollapseDrop = 0.0` - (World units a fully collapsed section falls.)
* `BridgeCollapseTilt = 0` - (Degrees a fully collapsed section folds by. The two halves fold toward each other, hinging at the end nearest their bank.)
* `BridgeCollapseStagger = 0%` - (Part of the duration spent rippling out from mid span: sections in the middle start falling immediately, those near the banks start up to this much later. Capped at 90%.)
* `BridgeRebuildDuration = 0` - (Time in ms of the rebuild animation, the collapse played backwards when the bridge goes from rubble back to healthy. 0 = no animation.)
* `BridgeCollapseSingleSpanRoll = 0` - (A bridge short enough to be a single span section has nothing to fold against, so it banks sideways by this many degrees instead. The sign picks the side.)
* `BridgeCollapseSingleSpanDrop = 0.0` - (World units a single-span deck sinks. 0 = use `BridgeCollapseDrop`.)

Notes:
* A `Bridge` block that leaves out `RadarColor` or `BridgeHoleAreaPercentage`, or any of the new fields, now takes them from `DefaultBridge`, like the other fields. Not inherited (a bug in the original code): `DamagedToSound`, `RepairedToSound`, `TransitionToOCL` and `TransitionToFX`.

Example (Roads.ini):
```ini
Bridge IndustrialDestroyable
  BridgeScale                    = 0.85
  BridgeModelName                = CBBridgeIn
  Texture                        = CBBridgeIn.tga
  BridgeModelNameDamaged         = CBBridgeIn_d
  TextureDamaged                 = CBBridgeIn_d.tga
  BridgeModelNameReallyDamaged   = CBBridgeIn_e
  TextureReallyDamaged           = CBBridgeIn_e.tga
  BridgeModelNameBroken          = CBBridgeIn_r
  TextureBroken                  = CBBridgeIn_r.tga
  TowerObjectNameFromLeft        = DestructibleBridgeTowerConcreteLeft01
  TowerObjectNameFromRight       = DestructibleBridgeTowerConcreteLeft02
  TowerObjectNameToLeft          = DestructibleBridgeTowerConcreteRight01
  TowerObjectNameToRight         = DestructibleBridgeTowerConcreteRight02
  ScaffoldObjectName             = BridgeScaffold01
  ScaffoldSupportObjectName      = BridgeScaffoldSupport01
  Destroyable                    = Yes
  BridgeObjectName               = GenericBridgeDestructible
  BridgeDeckHeight               = 5.0
  BridgeCollapseDuration         = 1200
  BridgeCollapseDrop             = 22
  BridgeCollapseTilt             = 25
  BridgeCollapseStagger          = 60%
  BridgeCollapseSingleSpanRoll   = 20
  BridgeRebuildDuration          = 1500
End
```

Span object for it:
```ini
Object GenericBridgeDestructible
  Draw = W3DDefaultDraw ModuleTag_Draw
    ; nothing -- the bridge system draws the span
  End
  EditorSorting = SYSTEM
  ArmorSet
    Conditions = None
    Armor = StructureArmor
    DamageFX = StructureDamageFXNoShake
  End
  RadarPriority = NOT_ON_RADAR
  KindOf = STRUCTURE IMMOBILE BRIDGE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 4000.0
    InitialHealth = 4000.0
  End
  Behavior = BridgeBehavior ModuleTag_Bridge
    LateralScaffoldSpeed = 100.0
    VerticalScaffoldSpeed = 70.0
    BridgeDieFX = FX:FX_RailroadBridgeCollapse Delay:0   SpanFraction:0.5
    BridgeDieFX = FX:FX_RailroadBridgeCollapse Delay:250 SpanFraction:0.3
    BridgeDieFX = FX:FX_RailroadBridgeCollapse Delay:250 SpanFraction:0.7
    Restoreable = Yes
    RepairPushDuration = 1500
    RepairPushForce = 150
  End
  Geometry = BOX
  GeometryMajorRadius = 1.0   ; overwritten at runtime (half span length)
  GeometryMinorRadius = 1.0   ; overwritten at runtime (half bridge width)
  GeometryHeight = 12.0
  GeometryIsSmall = No
  Shadow = NONE
End
```

## ThingTemplate

* `RequiredBridgeHeight = <from geometry>` - (Clearance this object needs to pass **under** a bridge. Lets tall units and ships be blocked from passing below bridges that are too low, while smaller ones still pass. When not set, the object's geometry height is used (at least one step; `NO_COLLIDE` objects need none). `0` = never blocked; a negative value = use the geometry height.)
  - Clearance is stored in steps of 10 world units, at most 15 steps (150); the requirement is rounded **up** to the next step (e.g. 12 needs 20), so an object is never let through a gap it does not fit under. The clearance is measured to the bottom of the deck (`BridgeDeckHeight`).
  - A closing drawbridge kills objects under it that need more clearance than it has (see `ClosingDamageTime` below).

## BridgeBehavior

* `Restoreable = No` - (If Yes, the bridge towers survive the bridge's death, so a Dozer/Worker can rebuild it from them. If No, the towers are killed with the bridge.)

Added parameters to clear units out of a bridge when it is repaired:
* `RepairPushDuration = 0` - (Time in ms after the repair during which units inside the bridge area (but not on top of the deck) are pushed sideways out of it.)
* `RepairPushForce = 0` - (Acceleration of that push, in distance/sec²; scaled by mass so every unit gets the same acceleration. 0 = no push.)

`BridgeDieFX` / `BridgeDieOCL` accept a new optional location label:
* `BridgeDieFX = FX:<FXList> Delay:<ms> [Bone:<bone>] [SpanFraction:<0..1>]`
* `SpanFraction` places the effect along the bridge centreline: 0 = the 'from' bank, 1 = the 'to' bank. FX are oriented along the span. Meant for road bridges, whose span object has no model and so no bones. `Bone` takes priority if both are given; with neither, a random spot on the deck is used as before.

## DrawBridgeUpdate / DrawBridgeTowerUpdate

**New in this fork.**

Modules for a landmark "drawbridge" that can be opened and closed. The towers carry the control: selecting a tower and using its special power opens or closes the bridge.

`DrawBridgeUpdate` (on the bridge object, next to its `BridgeBehavior`):
```ini
Behavior = DrawBridgeUpdate ModuleTag_DrawBridge
  OpeningDuration = 0       ; time in ms to open the bridge; also how long it is locked after a toggle
  ClosingDuration = 0       ; time in ms to close the bridge; also how long it is locked after a toggle
  OpeningPushForce = 0      ; acceleration (dist/sec^2) throwing units on the deck off it, 45 degrees up toward the end of their half, while it opens; 0 = no push
  ClosingDamageTime = 0     ; ms after closing starts when units under the bridge that are too tall for it (RequiredBridgeHeight) are crushed
  BridgeOpeningFX = <FXList>        ; when the bridge starts opening
  BridgeOpenFX = <FXList>           ; when it is fully open
  BridgeClosingFX = <FXList>        ; when it starts closing
  BridgeClosedFX = <FXList>         ; when it is fully closed
  BridgeOpeningAudio = <AudioEvent> ; plays while it opens (stopped when open)
  BridgeClosingAudio = <AudioEvent> ; plays while it closes (stopped when closed)
End
```

Notes:
* Opening sets the model condition `DOOR_1_OPENING` on the bridge object, which stays while it is open; closing sets `DOOR_1_CLOSING`, which stays while it is closed. Both are cleared when the bridge is destroyed or repaired. The open bridge is impassable over its `BridgeHoleAreaPercentage` area, and the push and the crush only reach units inside that area.
* Toggling is refused while the bridge is dead.

`DrawBridgeTowerUpdate` (on the bridge tower objects, named in the bridge's `TowerObjectName...` fields):
```ini
Behavior = SpecialAbility ModuleTag_DrawBridgeAbility
  SpecialPowerTemplate = <SpecialPower entry>   ; e.g. with Enum = SPECIAL_TOGGLE_DRAWBRIDGE
  UpdateModuleStartsAttack = Yes
End
Behavior = DrawBridgeTowerUpdate ModuleTag_DrawBridgeTower
  SpecialPowerTemplate = <SpecialPower entry>   ; special power that opens/closes the bridge
End
```

The tower needs a special power module for the same `SpecialPowerTemplate`. Its command buttons choose the direction with `Options`: `OPTION_ONE` closes the bridge, `OPTION_TWO` opens it. With `CHECK_LIKE` the button of the current state is shown checked.
