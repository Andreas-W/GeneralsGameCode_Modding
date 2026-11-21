---
title: "New enum values"
description: "New values for weapon slots, damage types, KindOfs and other enums."
status: unverified
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

For conditionstates (e.g. FIRING_A) you can use the letters D-H for the new types.

For Stealth forbidden conditions, you can use "FIRING_WEAPON_FOUR", etc.

For synced weapons (AutoChooseSources) you can use "SYNC_TO_WEAPON_FOUR", etc.

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

* `LOCOMOTORSET_VTOL` - This is used for VTOL aircraft for takeoff and landing. This should be a HOVER (=helicopter) locomotor, so a jet with WINGS locomotor can properly land and take off.

### ArmorSetFlags
Added new ArmorSetFlags:

* `PLAYER_UPGRADE2`
* `PLAYER_UPGRADE3`
* `PLAYER_UPGRADE4`

### WeaponSetFlags
Added new WeaponSetFlags that can be manually used for upgrades:

* `PLAYER_UPGRADE2`
* `PLAYER_UPGRADE3`
* `PLAYER_UPGRADE4`
(Note: There will be more in the future)

Added new WeaponSetFlags for specific conditions:

* `GARRISONED` - If a unit has a weaponset with this flag, it will be chosen when the unit is garrisoned inside a structure
* `CONTAINED` - If a unit has a weaponset with this flag, it will be chosen when the unit is contained inside a transport

### ConditionStates
For each new WeaponSetFlag there is a corresponding ConditionState

* `WEAPONSET_PLAYER_UPGRADE2`
* `WEAPONSET_PLAYER_UPGRADE3`
* `WEAPONSET_PLAYER_UPGRADE4`

Conditionstates that are used for VTOL aircraft (i.e. JetAIUpdate with NeedsRunway = No)

* `TAKEOFF`
* `LANDING`

Conditionstate used for TeleporterAIUpdate
* `TELEPORT_RECOVER`

Conditionstate set by AnimationSteeringUpdate (`BackwardsCondition = Yes`) while the unit drives in reverse
* `MOVING_BACKWARDS`

### WeaponBonus types

Added new WeaponBonus types for the following conditions:

* `CONTAINED` - this bonus is applied to passengers in TransportContain (similar to GARRISONED)
* `FRENZY_FOUR`, `FRENZY_FIVE` - extra tiers above the vanilla FRENZY_ONE/TWO/THREE
* `BOOST_ONE`, `BOOST_TWO`, `BOOST_THREE` - generic "boost" bonus tiers, for use with the `BuffTemplate` `WeaponBonus` FlagModifier (see [BuffSystem](/GeneralsGameCode_Modding/systems/buffs/))
* `DEMORALIZED_ONE`, `DEMORALIZED_TWO`, `DEMORALIZED_THREE` - generic "demoralized" penalty tiers
* `TARGET_PAINT_ONE`, `TARGET_PAINT_TWO`, `TARGET_PAINT_THREE` - bonus applied to a target that has been "painted" (e.g. by a target designator)
* `CRYO_ONE`, `CRYO_TWO`, `CRYO_THREE` - generic "cryo/slow" penalty tiers (used e.g. by the Cryo Bomb buff example in [BuffSystem](/GeneralsGameCode_Modding/systems/buffs/))
* `VETERANCY_FOUR`, `VETERANCY_FIVE` - bonus tiers matching the new `LEVEL_FOUR`/`LEVEL_FIVE` veterancy ranks
* `BATTLEPLAN_BOMBARDMENT_TWO`, `BATTLEPLAN_HOLDTHELINE_TWO`, `BATTLEPLAN_SEARCHANDDESTROY_TWO` - second-tier battle plan bonuses
* `EXTRA1` ... `EXTRA8` - free-to-use slots reserved for mods

Bonus values can be defined in GameData.ini (no bonus by default). Example:

`WeaponBonus = CONTAINED RANGE 125%`

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
