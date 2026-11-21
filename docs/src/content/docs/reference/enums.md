---
title: "New enum values"
description: "New values for weapon slots, damage types, KindOfs and other enums."
status: verified
verifiedAt: 7cdd826c5
sources:
  - GeneralsMD/Code/GameEngine/Include/GameLogic/WeaponSet.h
  - GeneralsMD/Code/GameEngine/Source/GameLogic/Object/WeaponSet.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/BitFlags.cpp
  - GeneralsMD/Code/GameEngine/Source/Common/System/KindOf.cpp
  - Core/GameEngine/Source/GameLogic/System/Damage.cpp
  - Core/GameEngine/Include/GameLogic/Damage.h
  - GeneralsMD/Code/GameEngine/Include/GameClient/InGameUI.h
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Locomotor.h
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Weapon.h
  - GeneralsMD/Code/GameEngine/Include/GameLogic/AI.h
  - GeneralsMD/Code/GameEngine/Include/GameLogic/Module/StealthUpdate.h
  - GeneralsMD/Code/GameEngine/Source/Common/System/DisabledTypes.cpp
  - Core/GameEngine/Source/Common/System/GameCommon.cpp
  - Core/GameEngine/Source/Common/System/ObjectStatusTypes.cpp
  - GeneralsMD/Code/GameEngine/Source/GameClient/Drawable.cpp
sidebar:
  order: 38
---

## New Enum Values

### Weapon Slots
New weapon slots were added for 8 slots in total
In addition to PRIMARY, SECONDARY, TERTIARY, you can use
* `WEAPON_FOUR`
* `WEAPON_FIVE`
* `WEAPON_SIX`
* `WEAPON_SEVEN`
* `WEAPON_EIGHT`

For conditionstates you can use the letters D-H for the new slots (`PREATTACK_D`, `FIRING_D`, `BETWEEN_FIRING_SHOTS_D`, `RELOADING_D`, `USING_WEAPON_D` ... `_H`).

For Stealth forbidden conditions, you can use `FIRING_WEAPON_FOUR` ... `FIRING_WEAPON_EIGHT`.

For synced weapons (AutoChooseSources) you can use `SYNC_TO_PRIMARY`, `SYNC_TO_SECONDARY`, `SYNC_TO_TERTIARY` and `SYNC_TO_WEAPON_FOUR` ... `SYNC_TO_WEAPON_EIGHT` (the weapon fires whenever that slot fires).

### DamageTypes

#### Generic DamageTypes

(no special logic attached, can be used for any weapon)

* `SONIC`
* `ACID`
* `JET_BOMB`
* `ANTI_TANK_GUN`
* `ANTI_TANK_MISSILE`
* `ANTI_AIR_GUN`
* `ANTI_AIR_MISSILE`
* `ARTILLERY`
* `SEISMIC`
* `RAD_BEAM`
* `TESLA`
* `JET_TORPEDO`
* `ANTI_SHIP`
* `CRYO`
* `BLACK_HOLE` - (used by [RiftSlowDeathBehavior](/GeneralsGameCode_Modding/systems/superweapons/#riftslowdeathbehavior), no special logic)

#### Functional DamageTypes

* `CHRONO_GUN` - Disables units and removes them after a health treshold is reached. Uses parameters from GameData. TODO: detailed tutorial
* `CHRONO_UNRESISTABLE` - Used internally for units recovering from `CHRONO_GUN` damage. Ignores armor and damage scalars. Not meant for weapons.
* `TORPEDO` - A weapon with this damage type cannot target units that are not over water.
* `ANTI_TORPEDO` - Meant for weapons that destroy torpedoes. Currently has no hardcoded logic, so it behaves like a generic type.

### DeathTypes

* `CHRONO`
* `CRYO`
* `BLACK_HOLE` - (used by [RiftSlowDeathBehavior](/GeneralsGameCode_Modding/systems/superweapons/#riftslowdeathbehavior))

### KindOfs

#### Functional KindOfs
These kindofs have some hardcoded functions attached to them and should not be used for general classification.

* `CAN_RETALIATE` - required for DRONE units to bypass hardcoded cannot_retaliate behavior
* `ENABLE_INFANTRY_LIGHTING` - Enables infantry ambient lighting for non-infantry
* `DISABLE_INFANTRY_LIGHTING` - Disables infantry ambient lighting for infantry
* `TELEPORTER` - Used with TeleportAIUpdate module. Required for pathfinding tweaks.
* `SHOW_PROGRESS_BAR` - Allows the object to show progress bar above healthbar (used for EnergyShields)
* `NO_ATTACK_WARNING` - Suppresses the "under attack" EVA/radar alarm event when this object is attacked.
* `IMMUNE_TO_PULL` - Never dragged around by pulling effects such as [RiftSlowDeathBehavior](/GeneralsGameCode_Modding/systems/superweapons/#riftslowdeathbehavior). Still takes their damage.
* `SHIPYARD` - Structure built on the shore or on water: it is placed at water height, skips the flat-ground and path checks when placed, and the skirmish AI builds it with `SKIRMISH_BUILD_SHIPYARD` (see [MapData](/GeneralsGameCode_Modding/ini/mapdata/)).
* `NO_MOVE_EFFECTS_ON_WATER` - While the object is over water: no track marks, no move debris/dust, no tread scrolling and no wheel animation (for ships and amphibious units).
* `TARGET_DESIGNATOR` - Marks an object as a target designator for [SpecialPowerDesignatorUpdate](/GeneralsGameCode_Modding/systems/target-designator/); only objects with this KindOf are searched for designators.

#### Generic KindOfs

New generic kindofs were added. These do not serve any internal purposes beyond classification. You can use them in various combinations for Required or Forbidden kindofs in various modules:

* `NO_BATTLE_PLAN` (note: this still needs to be added as invalidKindof)
* `VTOL`
* `LARGE_AIRCRAFT`
* `MEDIUM_AIRCRAFT`
* `SMALL_AIRCRAFT`
* `ARTILLERY`
* `HEAVY_ARTILLERY`
* `ANTI_AIR`
* `SCOUT`
* `COMMANDO`
* `HEAVY_INFANTRY`
* `SUPERHEAVY_VEHICLE`
* `SHIP`
* `SUBMARINE`
* `EXTRA1`
* `EXTRA2`
* `EXTRA3`
* `EXTRA4`
* `EXTRA5`
* `EXTRA6`
* `EXTRA7`
* `EXTRA8`
* `EXTRA9`
* `EXTRA10`
* `EXTRA11`
* `EXTRA12`
* `EXTRA13`
* `EXTRA14`
* `EXTRA15`
* `EXTRA16`

### RadiusCursorType

New radius cursor types were added, usable in `RadiusCursorType =` fields (Weapon/SpecialPower):

* `JUMPJET` - used for Jumpjet-related abilities
* `ATTACK_DAMAGE_AREA2`, `ATTACK_DAMAGE_AREA3`, `ATTACK_DAMAGE_AREA4` - extra tiers alongside the vanilla `ATTACK_DAMAGE_AREA`, for weapons/powers that need more than one distinctly-styled damage-area cursor.
* `NEUTRON_BOMB`, `RIFT_GENERATOR` - superweapon target cursors.

### Locomotor Sets

* `SET_VTOL` - This is used for VTOL aircraft for takeoff and landing (JetAIUpdate). This should be a HOVER (=helicopter) locomotor, so a jet with WINGS locomotor can properly land and take off. In INI: `Locomotor = SET_VTOL <LocomotorName>`.

### ArmorSetFlags
Added new ArmorSetFlags:

* `PLAYER_UPGRADE2`
* `PLAYER_UPGRADE3`
* `PLAYER_UPGRADE4`
* `LEVEL_FOUR`, `LEVEL_FIVE` - set automatically while the object has the matching [veterancy level](#veterancy-levels), like the vanilla `VETERAN` / `ELITE` / `HERO`
* `ARMOR_RIDER1` ... `ARMOR_RIDER16` - see [Rider Slots](#rider-slots-combat-bike)

### WeaponSetFlags
Added new WeaponSetFlags that can be manually used for upgrades:

* `PLAYER_UPGRADE2`
* `PLAYER_UPGRADE3`
* `PLAYER_UPGRADE4`
(Note: There will be more in the future)

Added new WeaponSetFlags for specific conditions:

* `GARRISONED` - If a unit has a weaponset with this flag, it will be chosen when the unit is garrisoned inside a structure
* `CONTAINED` - If a unit has a weaponset with this flag, it will be chosen when the unit is contained inside a transport
* `LEVEL_FOUR`, `LEVEL_FIVE` - set automatically while the object has the matching [veterancy level](#veterancy-levels), like the vanilla `VETERAN` / `ELITE` / `HERO`
* `WEAPON_RIDER9` ... `WEAPON_RIDER16` - see [Rider Slots](#rider-slots-combat-bike)

### ConditionStates
For each new WeaponSetFlag there is a corresponding ConditionState

* `WEAPONSET_PLAYER_UPGRADE2`
* `WEAPONSET_PLAYER_UPGRADE3`
* `WEAPONSET_PLAYER_UPGRADE4`
* `WEAPONSET_FOUR`, `WEAPONSET_FIVE` - for the `LEVEL_FOUR` / `LEVEL_FIVE` weapon set flags
* `RIDER9` ... `RIDER16` - for `WEAPON_RIDER9` ... `WEAPON_RIDER16`

Conditionstates that are used for VTOL aircraft (i.e. JetAIUpdate with NeedsRunway = No) and by CarrierDroneAIUpdate

* `TAKEOFF`
* `LANDING`

Conditionstate used for TeleporterAIUpdate
* `TELEPORT_RECOVER`

Conditionstate set by AnimationSteeringUpdate (`BackwardsCondition = Yes`) while the unit drives in reverse
* `MOVING_BACKWARDS`

Conditionstates set on a drone carrier by DroneCarrierAIUpdate while drones land (main door)
* `CARRIER_DOOR_OPENING`
* `CARRIER_DOOR_CLOSING`

Conditionstates for ship deaths. The engine does not set them itself; set them from INI where a module takes a condition state.
* `SHIP_TOPPLING`
* `SHIP_SINKING`

Conditionstates for armor salvage tiers (`ARMORSET_CRATEUPGRADE_ONE`, `ARMORSET_CRATEUPGRADE_TWO`) already exist in vanilla; the fork sets them together with the armor crate tiers (see [Crates and salvage](/GeneralsGameCode_Modding/systems/crates-and-salvage/)).

### WeaponBonus types

Added new WeaponBonus types for the following conditions:

* `CONTAINED` - this bonus is applied to passengers in TransportContain (similar to GARRISONED)
* `FRENZY_FOUR`, `FRENZY_FIVE` - extra tiers above the vanilla FRENZY_ONE/TWO/THREE
* `BOOST_ONE`, `BOOST_TWO`, `BOOST_THREE` - generic "boost" bonus tiers, for use with the `BuffTemplate` `WeaponBonus` FlagModifier (see [BuffSystem](/GeneralsGameCode_Modding/systems/buffs/))
* `DEMORALIZED_ONE`, `DEMORALIZED_TWO`, `DEMORALIZED_THREE` - generic "demoralized" penalty tiers
* `TARGET_PAINT_ONE`, `TARGET_PAINT_TWO`, `TARGET_PAINT_THREE` - bonus applied to a target that has been "painted" (e.g. by a target designator)
* `CRYO_ONE`, `CRYO_TWO`, `CRYO_THREE` - generic "cryo/slow" penalty tiers (used e.g. by the Cryo Bomb buff example in [BuffSystem](/GeneralsGameCode_Modding/systems/buffs/))
* `LEVEL_FOUR`, `LEVEL_FIVE` - set automatically while the object has the matching `LEVEL_FOUR`/`LEVEL_FIVE` veterancy rank, like the vanilla `VETERAN` / `ELITE` / `HERO` bonuses
* `BATTLEPLAN_BOMBARDMENT_TWO`, `BATTLEPLAN_HOLDTHELINE_TWO`, `BATTLEPLAN_SEARCHANDDESTROY_TWO` - second-tier battle plan bonuses
* `EXTRA1` ... `EXTRA8` - free-to-use slots reserved for mods

Bonus values can be defined in GameData.ini (no bonus by default). Example:

`WeaponBonus = CONTAINED RANGE 125%`

### Projectile collide types

* `WATER` - for `ProjectileCollidesWith` of the detonation weapon: the projectile (DumbProjectileBehavior, FreeFallProjectileBehavior, MissileAIUpdate) detonates when it hits the water surface.

### DisabledTypes

* `DISABLED_TELEPORT` - set by TeleporterAIUpdate on a unit that is recovering after a teleport
* `DISABLED_CHRONO` - set on units disabled by `CHRONO_GUN` damage (see [GameData ChronoGun parameters](/GeneralsGameCode_Modding/ini/gamedata/#chronogun-parameters))

### Veterancy Levels

Added two extra veterancy levels above HEROIC, for a total of 6 (`REGULAR`, `VETERAN`, `ELITE`, `HEROIC`, `LEVEL_FOUR`, `LEVEL_FIVE`).

* `LEVEL_FOUR`
* `LEVEL_FIVE`

Used by the object veterancy parameters (`MaxVeterancyLevel`, `ExperienceRequired`, `ExperienceValue`, `SkillPointValue`), the [`SetMaxVeterancyLevel`](/GeneralsGameCode_Modding/modules/upgrades/#experiencescalarupgrade) upgrade, and the `HealthBonus_Four` / `HealthBonus_Five` GameData parameters.

### Rider Slots (Combat Bike)

Extended `RiderChangeContain` from 8 to 16 rider slots. Corresponding enums were added for the new slots (9-16). Use these names in INI:

* WeaponSetFlags: `WEAPON_RIDER9` ... `WEAPON_RIDER16`
* ModelConditions: `RIDER9` ... `RIDER16`
* ObjectStatus: `STATUS_RIDER9` ... `STATUS_RIDER16`
* ArmorSetFlags: `ARMOR_RIDER1` ... `ARMOR_RIDER16` (all 16 are new, used by the optional armor set of a rider line)

See [RiderChangeContain](/GeneralsGameCode_Modding/systems/veterancy/#riderchangecontain).

## New Enum Types

### TintStatus Types
TintStatus is used to define color tint effects for various game mechanics (e.g. Frenzy).
Most entries are unused by default.

The colors for each type can be defined in in GameData.ini. The TintStatus types that are used in vanilla ZH have their default values hardcoded, but they can be overriden in GameData.ini.

Values:
* `DISABLED` - Used for Disabled units
* `IRRADIATED`
* `POISONED`
* `GAINING_SUBDUAL_DAMAGE` - Used for units being disabled
* `FRENZY` - Used for Frenzy
* `SHIELDED`
* `DEMORALIZED`
* `BOOST`
* `TELEPORT_RECOVER` - Chrono Legionnaire-style teleport recovery tint
* `DISABLED_CHRONO` - see [GameData ChronoGun parameters](/GeneralsGameCode_Modding/ini/gamedata/#chronogun-parameters)
* `GAINING_CHRONO_DAMAGE` - see [GameData ChronoGun parameters](/GeneralsGameCode_Modding/ini/gamedata/#chronogun-parameters)
* `FORCE_FIELD`
* `IRON_CURTAIN`
* `FROZEN`
* `EXTRA1`
* `EXTRA2`
* `EXTRA3`
* `EXTRA4`
* `EXTRA5`
* `EXTRA6`
* `EXTRA7`
* `EXTRA8`
* `EXTRA9`
* `EXTRA10`
