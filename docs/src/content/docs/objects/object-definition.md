---
title: "Object definition"
description: "New fields on Object blocks and ObjectExtend."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Source/Common/Thing/ThingTemplate.cpp
  - GeneralsMD/Code/GameEngine/Include/Common/ThingTemplate.h
  - GeneralsMD/Code/GameEngine/Source/Common/Thing/ThingFactory.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/WeaponSet.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Weapon.cpp
  - GeneralsMD/Code/GameEngine/Include/GameLogic/AI.h
  - GeneralsMD/Code/GameEngine/Source/GameClient/Drawable.cpp
  - Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/System/Geometry.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/PartitionManager.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/System/BuildAssistant.cpp
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Body/ActiveBody.cpp
sidebar:
  order: 0
---

## Object Improvements

### Upgrade Cameos
Added support for `UpgradeCameo6` to `UpgradeCameo9` (retail has 1-5). The control bar shows them in the windows `ControlBar.wnd:UnitUpgrade6` to `UnitUpgrade9`, which need to be added to the control bar .wnd file to work.

### WeaponSet
Added new parameters to an object's WeaponSet definition
* `WeaponReloadSharedAcrossSets = No` - (When a unit switches to a WeaponSet with this flag, each slot keeps its reload time and ammo count (in percent of the clip) from the previous set. For instance China MiGs will not reload in the air when they get Black Napalm. The flag is read from the set being switched to.)
* `ShareWeaponClip = No` - (If Yes, all weapon slots in this WeaponSet draw from one shared ammo clip instead of reloading independently: firing any slot also ticks down the clip of every other slot in the set.)

#### AutoChooseSources
Added new types for AutoChooseSources

* `SYNC_TO_PRIMARY` - This weapon slot cannot fire on its own, but will be fired whenever PRIMARY is fired.
* `SYNC_TO_SECONDARY` - This weapon slot cannot fire on its own, but will be fired whenever SECONDARY is fired.
* `SYNC_TO_TERTIARY` - This weapon slot cannot fire on its own, but will be fired whenever TERTIARY is fired.
* `SYNC_TO_WEAPON_FOUR` to `SYNC_TO_WEAPON_EIGHT` - The same for the additional weapon slots WEAPON_FOUR to WEAPON_EIGHT.

Syncs a weapon slot to another. The weapon slot cannot be used except when it's parent weapon is fired. This ignores all conditions and checks of the synced weapon.

Example:
```ini
Weapon = PRIMARY MainWeapon
Weapon = SECONDARY SyncedWeapon
AutoChooseSources = SECONDARY SYNC_TO_PRIMARY
```
In this example, the secondary weapon SyncedWeapon will not be used on its own. Whenever the unit attacks with its primary MainWeapon, the secondary weapon will be fired as well. This can be used for Aircraft to reliably fire multiple weapons on each attack.


### AmmoPips style

Added options to customize Ammo Pips, e.g. for Aircraft with a very large clip size.

Added a new parameter for object definitions:

* `AmmoPipsStyle = DEFAULT` - (Change the unit's display style for ammo pips to one of these:)
  - `DEFAULT` - default ZH style; 1 yellow pip symbol for each shot.
  - `PERCENTAGE_BAR` - display a yellow bar similar to the health bar, showing the ammo count in percent.
  - `SINGLE` - default ZH pip visuals, but only show a single symbol regardless of clip size. Ammo count is displayed via fading yellow color.
  - `THIN` - like `DEFAULT`, one pip per shot without spacing, using the thinner images `SCPAmmoThinFull` / `SCPAmmoThinEmpty` (these need to exist in the mapped images, otherwise nothing is drawn).

Note: This parameter might be moved to individual weapons in the future, to allow displaying ammo pips for multiple weapon slots at once.

### Pathfinding and bridges

* `MaxPathfindingCellRadius = 2` - (Upper limit, in pathfinding cells, for the radius the pathfinder uses for this object. Retail always caps it at 2; raise it for very large units such as big ships so they path around obstacles according to their real size.)
* `RequiredBridgeHeight = 0` - (Minimum clearance this object needs to pass under bridges, see [Bridges](/GeneralsGameCode_Modding/systems/bridges/#thingtemplate).)

### Compound geometry

An object can have up to 7 shapes in addition to its normal geometry (8 shapes in total), so L-shaped bases, long ships or gates no longer need one big box. Each `AdditionalGeometry` block adds one shape. The `Geometry`, `GeometryMajorRadius`, `GeometryMinorRadius` and `GeometryHeight` fields stay the main shape at the object's position.

* `Type = BOX` - (`BOX`, `SPHERE` or `CYLINDER`, as for `Geometry`.)
* `MajorRadius = 0` - (Half length along the shape's forward axis; the radius of a sphere or cylinder.)
* `MinorRadius = 0` - (Half width of a box. Ignored for spheres and cylinders.)
* `Height = 0` - (Height of a box or cylinder. Ignored for spheres.)
* `Offset = X:0 Y:0 Z:0` - (Position of the shape relative to the object's position: X forward, Y to the left, Z up. A box or cylinder stands on this point; a sphere is centered on it. The offset turns with the object.)
* `Angle = 0` - (Rotation of the shape relative to the object, in degrees, counterclockwise.)

```ini
Object MyLShapedFactory
  Geometry            = BOX
  GeometryMajorRadius = 40
  GeometryMinorRadius = 20
  GeometryHeight      = 30
  AdditionalGeometry
    Type        = BOX
    MajorRadius = 15
    MinorRadius = 30
    Height      = 20
    Offset      = X:25 Y:50 Z:0
  End
End
```

The shapes are used by:

* Collisions between units and objects, and the "would this collide" checks (crushing, placing objects).
* The partition cells the object covers, and so fog of war and the ghost image of fogged structures.
* The pathfinding footprint of structures, building placement (overlap, terrain flatness, the area cleared of units) and the terrain flattening under a new structure.
* Weapon range and the radius of area damage: the distance is measured to the nearest shape instead of an enclosing circle or sphere.
* The collision extents debug view (`ShowCollisionExtents`, extra shapes drawn in light blue) and the object outline in WorldBuilder.

The bounding circle and sphere that the rest of the game uses enclose all shapes. Everything else only uses the main shape: random points in the footprint (EMP, special abilities), minefields, garrison fire points, structure collapse effects, faction bibs and water wakes. A compound geometry is never `GeometryIsSmall`.

At runtime:

* Modules that resize the geometry (`DynamicGeometryInfoUpdate`, gates of `CheckpointUpdate`) change only the main shape, unless `DynamicGeometryInfoUpdate` has `ScaleAdditionalGeometry = Yes`.
* [`GeometryUpgrade`](/GeneralsGameCode_Modding/modules/upgrades/#geometryupgrade) and [`DamageStateGeometryBehavior`](/GeneralsGameCode_Modding/modules/updates-and-behaviors/#damagestategeometrybehavior) replace the whole geometry, additional shapes included.
* When a structure turns to rubble, every shape that reaches higher than the rubble height is cut down to it.
* An open drawbridge has no shapes; closed, it gets its template geometry back.

An object that inherits from another one (`ObjectReskin`, `ObjectExtend`) keeps the parent's additional shapes until it defines its own: the first `AdditionalGeometry` block replaces all inherited ones.

## Misc Improvements

### ObjectExtend
Allows to use inheritance in the parser. `ObjectExtend` is similar to `ObjectReskin` but inherits all modules and allows modifications. The parent object must be defined before the ObjectExtend.  

Syntax: `ObjectExtend NewObject ObjectToInheritFrom`  

Use `RemoveModule ModuleTag_xx` to remove a module inherited from the parent. A tag that does not exist on the parent stops loading with an error.  
WeaponSet and ArmorSet are inherited, but if you define another one in the child, the inherited sets are removed.  
If the parent has multiple WeaponSets all will be removed from the child if the child defines one. 
`Prerequisites` work the same way: they are inherited, but a `Prerequisites` block in the child replaces the inherited ones instead of adding to them.

Examples:  
```ini
ObjectExtend ZExtendedTank Chem_GLAVehicleRadarVan  
  ; *** ART Parameters ***  
  SelectPortrait = SNHacker2_L ;define other icons  
  ButtonImage = SNHacker2  
    
  RemoveModule ModuleTag_01 ; Remove the parent's draw  
  
  Draw = W3DTankDraw ModuleTag_1337 ; add a new draw  
    OkToChangeModelColor = Yes  
    DefaultConditionState  
      Model = NVInferno  
      Turret = Turret  
      TurretPitch = TurretEL01  
      WeaponFireFXBone = PRIMARY Muzzle  
      WeaponRecoilBone = PRIMARY Barrel  
      WeaponLaunchBone = PRIMARY Muzzle  
      HideSubObject = PARTSUP TurretEl02  
      ShowSubObject = TurretEL01 TURRETPARTS  
    End  
    ConditionState = RUBBLE REALLYDAMAGED  
      Model = NVInferno_D  
    End  
    TrackMarks = EXTnkTrack.tga  
    TreadAnimationRate = 2.0  
  End  
  Draw = W3DModelDraw ModuleTag_0123 ; add a second draw (tree on top)  
    DefaultConditionState  
      Model = PTOak01  
    End  
  End  
  Prerequisites ; change the prerequisites  
    Object = Tank_ChinaCommandCenter TEST_MODE  
  End  
  WeaponSet ; define new weaponset, all parent inherited weapons are cleared  
    Conditions = None  
    Weapon = PRIMARY Tank_InfernoCannonGun  
  End  
  RemoveModule ModuleTag_Stealth ; Remove the stealth update inherited from the parent  
End  
ObjectExtend ZExtendedTank2 ZExtendedTank  ; inherit again
  RemoveModule ModuleTag_0123  ; remove the tree draw
  WeaponSet  ; replaces all weaponset
    Conditions = None  
    Weapon = PRIMARY MarauderTankGun  
  End  
  ArmorSet  ; replaces all armor sets
    Conditions = None  
    Armor = TruckArmorExtended  
    DamageFX = TankDamageFX  
  End  
End  
```
