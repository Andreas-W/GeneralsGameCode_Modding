---
title: "Object definition"
description: "New fields on Object blocks and ObjectExtend."
status: unverified
sidebar:
  order: 0
---

## Object Improvements

### Upgrade Cameos
Added support for UpgradeCameo6-9 slots. These need to be defined in the controlbar .wnd files to work.

### WeaponSet
Added new parameters to an object's WeaponSet definition
* `WeaponReloadSharedAcrossSets = No` - If a unit switched between weaponset, it's reload time and ammo count (in percent) will be kept. For instance Migs will not reload in air when they get BlackNapalm. 
* `ShareWeaponClip = No` - (If Yes, all weapon slots in this WeaponSet draw from one shared ammo clip instead of reloading independently: firing any slot also ticks down the clip of every other slot in the set.)

#### AutoChooseSources
Added new types for AutoChooseSources

* `SYNC_TO_PRIMARY` - This weapon slot cannot fire on its own, but will be fired whenever PRIMARY is fired.
* `SYNC_TO_SECONDARY` - This weapon slot cannot fire on its own, but will be fired whenever SECONDARY is fired.
* `SYNC_TO_TERTIARY` - This weapon slot cannot fire on its own, but will be fired whenever TERTIARY is fired.

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

Note: This parameter might be moved to individual weapons in the future, to allow displaying ammo pips for multiple weapon slots at once.

## Misc Improvements

### ObjectExtend
Allows to use inheritence in the parser. `ObjectExtend` is similar to `ObjectReskin` but inherit all modules and allow modifications. The parent object must be defined before the ObjectExtend.  

Syntax: `ObjectExtend NewObject ObjectToInheritFrom`  

Use `RemoveModule ModuleTag_xx` to remove a module inherited from the parent.  
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
