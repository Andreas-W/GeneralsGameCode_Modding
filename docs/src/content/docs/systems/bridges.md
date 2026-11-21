---
title: "Bridges"
description: "Destroyable road bridges and drawbridges."
status: unverified
sidebar:
  order: 14
---

## Bridge.ini / TerrainRoad (Bridge type)

* `BridgeHoleAreaPercentage = 100%` - (Percentage of the bridge span that is removed/becomes impassable when the bridge is destroyed. Lets you tune how much of the bridge collapses.)

### Destroyable road bridges

Road bridges (the procedural bridges placed with the bridge tool in WorldBuilder, drawn from `BridgeModelName`) can now be destroyed and rebuilt like landmark bridges. Before, they were always indestructible and had no towers.

* `Destroyable = No` - (If Yes, the bridge can be damaged and destroyed. It is owned by the neutral player and gets the 4 targetable towers from `TowerObjectNameFromLeft/FromRight/ToLeft/ToRight`, which a Dozer/Worker can use to rebuild it (if the span object's `BridgeBehavior` has `Restoreable = Yes`). The deck model switches to `BridgeModelNameDamaged` / `ReallyDamaged` / `Broken` with the span object's damage state.)
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
* A `Bridge` block that leaves out `RadarColor` or `BridgeHoleAreaPercentage`, or any of the new fields, now takes them from `DefaultBridge`, like the other fields.

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

* `RequiredBridgeHeight = 0` - (Minimum bridge-layer height this object's pathfinding requires to be allowed to cross a bridge. Lets tall units/ships be blocked from bridges that aren't high enough for them, while shorter units can still cross.)
  - Clearance is stored in steps of 10 world units; the requirement is rounded **up** to the next step (e.g. 12 needs 20), so an object is never let through a gap it does not fit under. The clearance is measured to the bottom of the deck (`BridgeDeckHeight`).

## BridgeBehavior

Added parameters to push objects on a bridge when it is repaired:
* `RepairPushDuration = 0` - (Duration in ms over which objects standing on the bridge are pushed up when the bridge is repaired/restored.)
* `RepairPushForce = 0` - (Acceleration applied to objects on the bridge while it is being repaired.)

`BridgeDieFX` / `BridgeDieOCL` accept a new optional location label:
* `BridgeDieFX = FX:<FXList> Delay:<ms> [Bone:<bone>] [SpanFraction:<0..1>]`
* `SpanFraction` places the effect along the bridge centreline: 0 = the 'from' bank, 1 = the 'to' bank. FX are oriented along the span. Meant for road bridges, whose span object has no model and so no bones. `Bone` takes priority if both are given; with neither, a random spot on the deck is used as before.

## DrawBridgeUpdate (New) / DrawBridgeTowerUpdate

**New in this fork.**

New modules implementing an openable "draw bridge" that can be raised and lowered, triggered via a special power.

`DrawBridgeUpdate` (on the bridge object):
```ini
Behavior = DrawBridgeUpdate ModuleTag_DrawBridge
  OpeningDuration = 0       ; time in ms to open (raise) the bridge
  ClosingDuration = 0       ; time in ms to close (lower) the bridge
  OpeningPushForce = 0      ; acceleration applied to objects when the bridge opens (pushes them off)
  ClosingDamageTime = 0     ; time in ms during closing when objects underneath take damage
End
```

`DrawBridgeTowerUpdate` (on the bridge tower/control object):
```ini
Behavior = DrawBridgeTowerUpdate ModuleTag_DrawBridgeTower
  SpecialPowerTemplate = <SpecialPower entry>   ; special power that triggers opening/closing
End
```
