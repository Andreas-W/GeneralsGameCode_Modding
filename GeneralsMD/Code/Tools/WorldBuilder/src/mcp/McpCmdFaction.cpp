/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// McpCmdFaction.cpp
// MCP bridge command that describes a faction as the game data defines it: its tech tree (what
// builds what, prerequisites, upgrades, sciences, special powers) and the stats of its units and
// structures (cost, health, armor, weapons, movement). Everything is read from the loaded
// templates, so inheritance, reskins and module overrides are already resolved.

#include "StdAfx.h"
#define DEFINE_BUILDABLE_STATUS_NAMES
#define DEFINE_GUI_COMMAND_NAMES
#define DEFINE_COMMAND_OPTION_NAMES
#define DEFINE_WEAPONSLOTTYPE_NAMES
#define DEFINE_SURFACECATEGORY_NAMES
#include "mcp/McpCommands.h"

#include "Common/GameCommon.h"
#include "Common/KindOf.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/SpecialPowerMaskType.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/LocomotorSet.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CommandSetUpgrade.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/StealthDetectorUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"

#include <algorithm>
#include <map>
#include <math.h>
#include <set>
#include <string>
#include <vector>

namespace
{

/// A list that keeps the order in which things were first added.
template <class T>
struct OrderedSet
{
	std::vector<T> items;
	std::set<T> seen;

	bool add(const T &item)
	{
		if (!seen.insert(item).second) return false;
		items.push_back(item);
		return true;
	}
	bool has(const T &item) const { return seen.find(item) != seen.end(); }
};

/// A science as the purchase command sets of the faction offer it.
struct ScienceOffer
{
	std::string button;
	Int rank;
};

/// Everything the description of one faction refers to.
struct FactionData
{
	OrderedSet<const ThingTemplate *> objects;
	std::set<const ThingTemplate *> inTechTree;
	OrderedSet<const WeaponTemplate *> weapons;
	OrderedSet<const ArmorTemplate *> armors;
	OrderedSet<const UpgradeTemplate *> upgrades;
	OrderedSet<ScienceType> sciences;
	OrderedSet<const SpecialPowerTemplate *> specialPowers;

	std::map<const ThingTemplate *, OrderedSet<std::string> > producedBy;
	std::map<const UpgradeTemplate *, OrderedSet<std::string> > researchedAt;
	std::map<const SpecialPowerTemplate *, OrderedSet<std::string> > powerUsers;
	std::map<ScienceType, ScienceOffer> scienceOffers;
};

ControlBar *controlBar()
{
	// WorldBuilder has no control bar until something asks for command sets.
	if (TheControlBar == nullptr) {
		TheControlBar = new ControlBar;
		TheControlBar->init();
	}
	return TheControlBar;
}

McpJson stringArray(const OrderedSet<std::string> &set)
{
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < set.items.size(); i++) {
		list.push(McpJson(set.items[i]));
	}
	return list;
}

std::string displayText(const UnicodeString &text)
{
	AsciiString ascii;
	ascii.translate(text);
	return ascii.str();
}

/// Game data is single precision; rounding keeps 25 from reading 25.0000012.
double num(double value)
{
	return floor(value * 10000.0 + 0.5) / 10000.0;
}

double framesToSeconds(double frames)
{
	return num(frames / (double)LOGICFRAMES_PER_SECOND);
}

McpJson scienceNames(const ScienceVec &sciences)
{
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < sciences.size(); i++) {
		list.push(McpJson(TheScienceStore->getInternalNameForScience(sciences[i]).str()));
	}
	return list;
}

template <class FLAGS>
McpJson flagNames(const FLAGS &flags)
{
	McpJson list = McpJson::makeArray();
	const char *const *names = FLAGS::getBitNames();
	for (Int i = 0; names[i]; i++) {
		if (flags.test(i)) list.push(McpJson(names[i]));
	}
	return list;
}

McpJson kindNames(const ThingTemplate *tt)
{
	McpJson list = McpJson::makeArray();
	const char *const *names = KindOfMaskType::getBitNames();
	for (Int k = KINDOF_FIRST; k < KINDOF_COUNT && names[k]; k++) {
		if (tt->isKindOf((KindOfType)k)) list.push(McpJson(names[k]));
	}
	return list;
}

Bool isProductionCommand(GUICommandType type)
{
	return type == GUI_COMMAND_UNIT_BUILD || type == GUI_COMMAND_DOZER_CONSTRUCT
		|| type == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT || type == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT;
}

/// Commands every unit has; they say nothing about what a type can do.
Bool isPlainCommand(GUICommandType type)
{
	switch (type) {
		case GUI_COMMAND_NONE:
		case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
		case GUI_COMMAND_CANCEL_UNIT_BUILD:
		case GUI_COMMAND_CANCEL_UPGRADE:
		case GUI_COMMAND_ATTACK_MOVE:
		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
		case GUI_COMMAND_STOP:
		case GUI_COMMAND_WAYPOINTS:
		case GUI_COMMAND_EXIT_CONTAINER:
		case GUI_COMMAND_BEACON_DELETE:
		case GUI_COMMAND_SET_RALLY_POINT:
		case GUI_COMMAND_SELL:
		case GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE:
		case GUI_COMMAND_REVERSE_MOVE:
			return TRUE;
		default:
			return FALSE;
	}
}

/// The command sets a type can show: its own and those its command set upgrades switch to.
std::vector<AsciiString> commandSetsOf(const ThingTemplate *tt)
{
	std::vector<AsciiString> sets;
	if (!tt->friend_getCommandSetString().isEmpty()) sets.push_back(tt->friend_getCommandSetString());
	const ModuleInfo &modules = tt->getBehaviorModuleInfo();
	for (Int m = 0; m < modules.getCount(); m++) {
		const CommandSetUpgradeModuleData *data = dynamic_cast<const CommandSetUpgradeModuleData *>(modules.getNthData(m));
		if (data == nullptr) continue;
		const AsciiString *names[2] = { &data->m_newCommandSet, &data->m_newCommandSetAlt };
		for (Int n = 0; n < 2; n++) {
			if (names[n]->isEmpty() || names[n]->compareNoCase("None") == 0) continue;
			if (std::find(sets.begin(), sets.end(), *names[n]) == sets.end()) sets.push_back(*names[n]);
		}
	}
	return sets;
}

/// Adds a science and the sciences it needs.
void collectScience(FactionData &data, ScienceType science)
{
	if (science == SCIENCE_INVALID || !data.sciences.add(science)) return;
	ScienceVec prereqs;
	if (TheScienceStore->friend_getPrereqSciences(science, prereqs)) {
		for (size_t i = 0; i < prereqs.size(); i++) {
			collectScience(data, prereqs[i]);
		}
	}
}

void collectSpecialPower(FactionData &data, const SpecialPowerTemplate *power, const ThingTemplate *user)
{
	if (power == nullptr) return;
	data.specialPowers.add(power);
	if (user) data.powerUsers[power].add(user->getName().str());
	collectScience(data, power->getRequiredScience());
}

/// Adds a type and everything that can be built from it, following the command sets.
void collectTechTree(FactionData &data, const ThingTemplate *start)
{
	std::vector<const ThingTemplate *> open;
	open.push_back(start);
	while (!open.empty()) {
		const ThingTemplate *tt = open.back();
		open.pop_back();
		if (!data.inTechTree.insert(tt).second) continue;
		data.objects.add(tt);

		const std::vector<AsciiString> sets = commandSetsOf(tt);
		for (size_t s = 0; s < sets.size(); s++) {
			const CommandSet *commandSet = controlBar()->findCommandSet(sets[s]);
			if (commandSet == nullptr) continue;
			for (Int i = 0; i < MAX_COMMANDS_PER_SET; i++) {
				const CommandButton *button = commandSet->getCommandButton(i);
				if (button == nullptr || !isProductionCommand(button->getCommandType())) continue;
				const ThingTemplate *built = button->getThingTemplate();
				if (built) {
					data.producedBy[built].add(tt->getName().str());
					open.push_back(built);
				}
			}
		}
	}
}

/// Records the upgrades, special powers and sciences the buttons of a type refer to.
void collectCommandRefs(FactionData &data, const ThingTemplate *tt)
{
	const std::vector<AsciiString> sets = commandSetsOf(tt);
	for (size_t s = 0; s < sets.size(); s++) {
		const CommandSet *commandSet = controlBar()->findCommandSet(sets[s]);
		if (commandSet == nullptr) continue;
		for (Int i = 0; i < MAX_COMMANDS_PER_SET; i++) {
			const CommandButton *button = commandSet->getCommandButton(i);
			if (button == nullptr) continue;
			const GUICommandType type = button->getCommandType();
			if ((type == GUI_COMMAND_PLAYER_UPGRADE || type == GUI_COMMAND_OBJECT_UPGRADE) && button->getUpgradeTemplate()) {
				data.upgrades.add(button->getUpgradeTemplate());
				data.researchedAt[button->getUpgradeTemplate()].add(tt->getName().str());
			}
			collectSpecialPower(data, button->getSpecialPowerTemplate(), tt);
			const ScienceVec &sciences = button->getScienceVec();
			for (size_t k = 0; k < sciences.size(); k++) {
				collectScience(data, sciences[k]);
			}
		}
	}
}

/// The sciences a purchase command set offers (the General's promotion screen).
void collectScienceOffers(FactionData &data, const AsciiString &setName, Int rank)
{
	const CommandSet *commandSet = setName.isEmpty() ? nullptr : controlBar()->findCommandSet(setName);
	if (commandSet == nullptr) return;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; i++) {
		const CommandButton *button = commandSet->getCommandButton(i);
		if (button == nullptr) continue;
		const ScienceVec &sciences = button->getScienceVec();
		for (size_t k = 0; k < sciences.size(); k++) {
			collectScience(data, sciences[k]);
			if (data.scienceOffers.find(sciences[k]) == data.scienceOffers.end()) {
				ScienceOffer offer;
				offer.button = button->getName().str();
				offer.rank = rank;
				data.scienceOffers[sciences[k]] = offer;
			}
		}
		collectSpecialPower(data, button->getSpecialPowerTemplate(), nullptr);
	}
}

/// True for the types of a side that matter to an AI even when nothing in the tech tree builds them.
Bool isArmyType(const ThingTemplate *tt)
{
	if (tt->isKindOf(KINDOF_PROJECTILE) || tt->isKindOf(KINDOF_MINE)) return FALSE;
	return tt->isKindOf(KINDOF_STRUCTURE) || tt->isKindOf(KINDOF_INFANTRY) || tt->isKindOf(KINDOF_VEHICLE)
		|| tt->isKindOf(KINDOF_AIRCRAFT);
}

McpJson describePrerequisites(const ThingTemplate *tt)
{
	// All of it is required: one type out of every group of objects, and every science.
	McpJson groups = McpJson::makeArray();
	McpJson sciences = McpJson::makeArray();
	for (Int p = 0; p < tt->getPrereqCount(); p++) {
		const ProductionPrerequisite *prereq = tt->getNthPrereq(p);
		McpJson group = McpJson::makeArray();
		for (Int u = 0; u < prereq->getUnitPrereqCount(); u++) {
			if (u > 0 && !prereq->isNthUnitPrereqOrWithPrevious(u) && group.size() > 0) {
				groups.push(group);
				group = McpJson::makeArray();
			}
			const ThingTemplate *unit = prereq->getNthUnitPrereq(u);
			group.push(McpJson(unit ? unit->getName().str() : "<unknown>"));
		}
		if (group.size() > 0) groups.push(group);
		for (size_t s = 0; s < prereq->getSciencePrereqs().size(); s++) {
			sciences.push(McpJson(TheScienceStore->getInternalNameForScience(prereq->getSciencePrereqs()[s]).str()));
		}
	}
	McpJson j = McpJson::makeObject();
	if (groups.size() > 0) j.set("objects", groups);
	if (sciences.size() > 0) j.set("sciences", sciences);
	return j;
}

McpJson describeCommands(FactionData &data, const ThingTemplate *tt)
{
	McpJson builds = McpJson::makeArray();
	McpJson upgrades = McpJson::makeArray();
	McpJson powers = McpJson::makeArray();
	McpJson abilities = McpJson::makeArray();
	std::set<std::string> seen;

	const std::vector<AsciiString> sets = commandSetsOf(tt);
	for (size_t s = 0; s < sets.size(); s++) {
		const CommandSet *commandSet = controlBar()->findCommandSet(sets[s]);
		if (commandSet == nullptr) continue;
		for (Int i = 0; i < MAX_COMMANDS_PER_SET; i++) {
			const CommandButton *button = commandSet->getCommandButton(i);
			if (button == nullptr || button->getName().isEmpty()) continue;
			const GUICommandType type = button->getCommandType();
			if (isPlainCommand(type) || !seen.insert(button->getName().str()).second) continue;

			McpJson entry = McpJson::makeObject();
			entry.set("button", button->getName().str());
			if (s > 0) entry.set("command_set", sets[s].str());
			const ThingTemplate *built = button->getThingTemplate();
			if (isProductionCommand(type) && built) {
				entry.set("object", built->getName().str());
				if (type != GUI_COMMAND_UNIT_BUILD && type != GUI_COMMAND_DOZER_CONSTRUCT) {
					entry.set("type", TheGuiCommandNames[type]);
				}
				builds.push(entry);
			} else if ((type == GUI_COMMAND_PLAYER_UPGRADE || type == GUI_COMMAND_OBJECT_UPGRADE) && button->getUpgradeTemplate()) {
				entry.set("upgrade", button->getUpgradeTemplate()->getUpgradeName().str());
				upgrades.push(entry);
			} else {
				entry.set("type", TheGuiCommandNames[type]);
				if (button->getSpecialPowerTemplate()) entry.set("special_power", button->getSpecialPowerTemplate()->getName().str());
				if (!button->getScienceVec().empty()) entry.set("sciences", scienceNames(button->getScienceVec()));
				if (type == GUI_COMMAND_FIRE_WEAPON || type == GUI_COMMAND_SWITCH_WEAPON) {
					entry.set("weapon_slot", TheWeaponSlotTypeNames[button->getWeaponSlot()]);
				}
				McpJson options = McpJson::makeArray();
				for (Int bit = 0; TheCommandOptionNames[bit]; bit++) {
					if (button->getOptions() & (1u << bit)) options.push(McpJson(TheCommandOptionNames[bit]));
				}
				if (options.size() > 0) entry.set("options", options);
				if (button->getSpecialPowerTemplate()) powers.push(entry); else abilities.push(entry);
			}
		}
	}

	McpJson j = McpJson::makeObject();
	if (builds.size() > 0) j.set("builds", builds);
	if (upgrades.size() > 0) j.set("upgrades", upgrades);
	if (powers.size() > 0) j.set("special_powers", powers);
	if (abilities.size() > 0) j.set("abilities", abilities);
	return j;
}

McpJson describeWeaponSets(FactionData &data, const ThingTemplate *tt)
{
	McpJson list = McpJson::makeArray();
	const WeaponTemplateSetVector &sets = tt->getWeaponTemplateSets();
	for (size_t s = 0; s < sets.size(); s++) {
		McpJson weapons = McpJson::makeObject();
		for (Int slot = 0; slot < WEAPONSLOT_COUNT; slot++) {
			const WeaponTemplate *weapon = sets[s].getNth((WeaponSlotType)slot);
			if (weapon == nullptr) continue;
			data.weapons.add(weapon);
			weapons.set(TheWeaponSlotTypeNames[slot], weapon->getName().str());
		}
		if (weapons.numKeys() == 0) continue;
		list.push(McpJson::makeObject().set("conditions", flagNames(sets[s].getNthConditionsYes(0))).set("weapons", weapons));
	}
	return list;
}

McpJson describeArmorSets(FactionData &data, const ThingTemplate *tt)
{
	McpJson list = McpJson::makeArray();
	const ArmorTemplateSetVector &sets = tt->getArmorTemplateSets();
	for (size_t s = 0; s < sets.size(); s++) {
		const ArmorTemplate *armor = sets[s].getArmorTemplate();
		if (armor == nullptr) continue;
		data.armors.add(armor);
		list.push(McpJson::makeObject().set("conditions", flagNames(sets[s].getNthConditionsYes(0)))
			.set("armor", TheArmorStore->friend_findArmorTemplateName(armor).str()));
	}
	return list;
}

/// What the behavior modules of a type say about health, movement, cargo and stealth.
void describeModules(const ThingTemplate *tt, McpJson &j)
{
	OrderedSet<std::string> names;
	const ModuleInfo &modules = tt->getBehaviorModuleInfo();
	for (Int m = 0; m < modules.getCount(); m++) {
		names.add(modules.getNthName(m).str());
		const ModuleData *data = modules.getNthData(m);
		if (data == nullptr) continue;

		if (const ActiveBodyModuleData *body = dynamic_cast<const ActiveBodyModuleData *>(data)) {
			j.set("health", num(body->m_maxHealth));
		}
		if (data->isAiModuleData()) {
			const AIUpdateModuleData *ai = static_cast<const AIUpdateModuleData *>(data);
			const LocomotorTemplateVector *locos = ai->findLocomotorTemplateVector(LOCOMOTORSET_NORMAL);
			if (locos && !locos->empty() && (*locos)[0]) {
				const LocomotorTemplate *loco = (*locos)[0];
				McpJson surfaces = McpJson::makeArray();
				for (Int bit = 0; TheLocomotorSurfaceTypeNames[bit]; bit++) {
					if (loco->friend_getSurfaces() & (1 << bit)) surfaces.push(McpJson(TheLocomotorSurfaceTypeNames[bit]));
				}
				j.set("locomotor", McpJson::makeObject().set("name", loco->friend_getName().str())
					.set("speed", num(loco->friend_getMaxSpeed() * (double)LOGICFRAMES_PER_SECOND)).set("surfaces", surfaces));
			}
			if (const SupplyTruckAIUpdateModuleData *truck = dynamic_cast<const SupplyTruckAIUpdateModuleData *>(data)) {
				j.set("supply_boxes", truck->m_maxBoxesData);
			}
			if (const WorkerAIUpdateModuleData *worker = dynamic_cast<const WorkerAIUpdateModuleData *>(data)) {
				j.set("supply_boxes", worker->m_maxBoxesData);
			}
		}
		if (const OpenContainModuleData *contain = dynamic_cast<const OpenContainModuleData *>(data)) {
			McpJson c = McpJson::makeObject();
			c.set("module", modules.getNthName(m).str());
			if (contain->m_containMax >= 0) c.set("max", contain->m_containMax);
			if (const TransportContainModuleData *transport = dynamic_cast<const TransportContainModuleData *>(data)) {
				c.set("slots", transport->m_slotCapacity);
				if (!transport->m_initialPayload.name.isEmpty() && transport->m_initialPayload.count > 0) {
					c.set("initial_payload", transport->m_initialPayload.name.str()).set("initial_count", transport->m_initialPayload.count);
				}
			}
			if (contain->m_passengersAllowedToFire) c.set("passengers_fire", true);
			if (contain->m_allowInsideKindOf.any()) c.set("allows", flagNames(contain->m_allowInsideKindOf));
			if (contain->m_forbidInsideKindOf.any()) c.set("forbids", flagNames(contain->m_forbidInsideKindOf));
			j.set("contain", c);
		}
		if (const StealthUpdateModuleData *stealth = dynamic_cast<const StealthUpdateModuleData *>(data)) {
			// Without innate stealth the module only waits for an upgrade or special power.
			if (stealth->m_innateStealth) j.set("stealth", true);
		}
		if (const StealthDetectorUpdateModuleData *detector = dynamic_cast<const StealthDetectorUpdateModuleData *>(data)) {
			// A range of 0 means the vision range is used.
			j.set("detector_range", num(detector->m_detectionRange > 0 ? detector->m_detectionRange : tt->friend_calcVisionRange()));
		}
	}
	j.set("modules", stringArray(names));
}

McpJson describeObject(FactionData &data, const ThingTemplate *tt)
{
	McpJson j = McpJson::makeObject();
	j.set("name", tt->getName().str());
	const std::string displayName = displayText(tt->getDisplayName());
	if (!displayName.empty()) j.set("display_name", displayName);
	j.set("side", tt->getDefaultOwningSide().str());
	j.set("in_tech_tree", data.inTechTree.find(tt) != data.inTechTree.end());
	j.set("kind_of", kindNames(tt));
	j.set("cost", (int)tt->friend_getBuildCost()).set("build_time", num(tt->friend_getBuildTime()));
	const Int buildable = tt->getBuildable();
	if (buildable != BSTATUS_YES && buildable >= BSTATUS_YES && buildable < BSTATUS_NUM_TYPES) {
		j.set("buildable", BuildableStatusNames[buildable - BSTATUS_YES]);
	}
	if (tt->getEnergyProduction() != 0) j.set("energy", (int)tt->getEnergyProduction());
	if (tt->getEnergyBonus() != 0) j.set("energy_bonus", (int)tt->getEnergyBonus());
	if (tt->getMaxSimultaneousOfType() > 0) j.set("max_simultaneous", (int)tt->getMaxSimultaneousOfType());
	j.set("vision", num(tt->friend_calcVisionRange()));
	if (tt->getRawTransportSlotCount() > 0) j.set("size_in_transport", (int)tt->getRawTransportSlotCount());
	if (tt->getCrusherLevel() > 0) j.set("crusher", (int)tt->getCrusherLevel());
	if (tt->getCrushableLevel() > 0) j.set("crushable", (int)tt->getCrushableLevel());
	if (tt->isBuildFacility()) j.set("build_facility", true);
	if (!tt->getBuildVariations().empty()) {
		McpJson variations = McpJson::makeArray();
		for (size_t i = 0; i < tt->getBuildVariations().size(); i++) {
			variations.push(McpJson(tt->getBuildVariations()[i].str()));
		}
		j.set("build_variations", variations);
	}

	const McpJson prereqs = describePrerequisites(tt);
	if (prereqs.numKeys() > 0) j.set("prerequisites", prereqs);
	std::map<const ThingTemplate *, OrderedSet<std::string> >::const_iterator producers = data.producedBy.find(tt);
	if (producers != data.producedBy.end()) j.set("built_by", stringArray(producers->second));

	describeModules(tt, j);
	const McpJson weaponSets = describeWeaponSets(data, tt);
	if (weaponSets.size() > 0) j.set("weapon_sets", weaponSets);
	const McpJson armorSets = describeArmorSets(data, tt);
	if (armorSets.size() > 0) j.set("armor_sets", armorSets);

	if (!tt->friend_getCommandSetString().isEmpty()) j.set("command_set", tt->friend_getCommandSetString().str());
	const McpJson commands = describeCommands(data, tt);
	for (size_t k = 0; k < commands.numKeys(); k++) {
		j.set(commands.keyAt(k).c_str(), commands.valueAt(k));
	}
	return j;
}

McpJson describeWeapon(const WeaponTemplate *weapon)
{
	const WeaponBonus none;
	McpJson j = McpJson::makeObject();
	j.set("name", weapon->getName().str());
	j.set("damage", num(weapon->getPrimaryDamage(none))).set("damage_radius", num(weapon->getPrimaryDamageRadius(none)));
	if (weapon->getSecondaryDamage(none) > 0) {
		j.set("secondary_damage", num(weapon->getSecondaryDamage(none))).set("secondary_radius", num(weapon->getSecondaryDamageRadius(none)));
	}
	j.set("damage_type", DamageTypeFlags::getNameFromSingleBit(weapon->getDamageType()));
	j.set("range", num(weapon->getUnmodifiedAttackRange()));
	if (weapon->getMinimumAttackRange() > 0) j.set("min_range", num(weapon->getMinimumAttackRange()));

	const double delay = (weapon->getMinDelayBetweenShots() + weapon->getMaxDelayBetweenShots()) * 0.5;
	j.set("shot_delay", framesToSeconds(delay));
	const Int clip = weapon->getClipSize();
	if (clip > 0) {
		j.set("clip_size", clip).set("clip_reload", framesToSeconds(weapon->getClipReloadTime(none)));
	}
	if (weapon->getPreAttackDelay(none) > 0) j.set("pre_attack_delay", framesToSeconds(weapon->getPreAttackDelay(none)));

	// Sustained damage per second against one target, without armor: a clip of shots, then the reload.
	double cycleFrames = delay;
	double cycleDamage = weapon->getPrimaryDamage(none);
	if (clip > 0) {
		cycleFrames = delay * (clip - 1) + weapon->getClipReloadTime(none);
		cycleDamage *= clip;
	}
	if (cycleFrames > 0) j.set("dps", num(cycleDamage * (double)LOGICFRAMES_PER_SECOND / cycleFrames));

	static const char *const antiNames[] = { "AIRBORNE_VEHICLE", "GROUND", "PROJECTILE", "SMALL_MISSILE", "MINE",
		"AIRBORNE_INFANTRY", "BALLISTIC_MISSILE", "PARACHUTE", nullptr };
	McpJson anti = McpJson::makeArray();
	for (Int bit = 0; antiNames[bit]; bit++) {
		if (weapon->getAntiMask() & (1 << bit)) anti.push(McpJson(antiNames[bit]));
	}
	j.set("anti", anti);
	if (weapon->getProjectileTemplate()) {
		j.set("projectile", weapon->getProjectileTemplate()->getName().str());
		if (weapon->getWeaponSpeed() > 0 && weapon->getWeaponSpeed() < 99999.0f) {
			j.set("projectile_speed", num(weapon->getWeaponSpeed() * (double)LOGICFRAMES_PER_SECOND));
		}
	}
	if (weapon->getScatterRadius() > 0) j.set("scatter_radius", num(weapon->getScatterRadius()));
	return j;
}

McpJson describeArmor(const ArmorTemplate *armor)
{
	// The share of each damage type that gets through; types at 100% are left out.
	McpJson damage = McpJson::makeObject();
	for (Int type = 0; type < DAMAGE_NUM_TYPES; type++) {
		const Real factor = armor->adjustDamage((DamageType)type, 1.0f);
		if (factor != 1.0f) damage.set(DamageTypeFlags::getNameFromSingleBit(type), num(factor));
	}
	return McpJson::makeObject().set("name", TheArmorStore->friend_findArmorTemplateName(armor).str()).set("damage_taken", damage);
}

McpJson describeUpgrade(const FactionData &data, const UpgradeTemplate *upgrade)
{
	McpJson j = McpJson::makeObject();
	j.set("name", upgrade->getUpgradeName().str());
	j.set("type", upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER ? "PLAYER" : "OBJECT");
	j.set("cost", (int)upgrade->friend_getBuildCost()).set("build_time", num(upgrade->friend_getBuildTime()));
	std::map<const UpgradeTemplate *, OrderedSet<std::string> >::const_iterator where = data.researchedAt.find(upgrade);
	if (where != data.researchedAt.end()) j.set("researched_at", stringArray(where->second));
	return j;
}

McpJson describeScience(const FactionData &data, const PlayerTemplate *pt, ScienceType science)
{
	McpJson j = McpJson::makeObject();
	j.set("name", TheScienceStore->getInternalNameForScience(science).str());
	UnicodeString name, description;
	if (TheScienceStore->getNameAndDescription(science, name, description) && !name.isEmpty()) {
		j.set("display_name", displayText(name));
	}
	const Int cost = TheScienceStore->getSciencePurchaseCost(science);
	if (cost > 0) j.set("point_cost", cost);
	ScienceVec prereqs;
	if (TheScienceStore->friend_getPrereqSciences(science, prereqs) && !prereqs.empty()) j.set("prerequisites", scienceNames(prereqs));
	std::vector<AsciiString> granted;
	if (TheScienceStore->getGrantedUpgradeNames(science, granted) && !granted.empty()) {
		McpJson upgrades = McpJson::makeArray();
		for (size_t i = 0; i < granted.size(); i++) upgrades.push(McpJson(granted[i].str()));
		j.set("grants_upgrades", upgrades);
	}
	const ScienceVec &intrinsic = pt->getIntrinsicSciences();
	if (std::find(intrinsic.begin(), intrinsic.end(), science) != intrinsic.end()) j.set("intrinsic", true);
	std::map<ScienceType, ScienceOffer>::const_iterator offer = data.scienceOffers.find(science);
	if (offer != data.scienceOffers.end()) {
		j.set("purchase_rank", (int)offer->second.rank).set("purchase_button", offer->second.button);
	}

	// What owning the science unlocks.
	McpJson unlocks = McpJson::makeArray();
	for (size_t i = 0; i < data.objects.items.size(); i++) {
		const ThingTemplate *tt = data.objects.items[i];
		for (Int p = 0; p < tt->getPrereqCount(); p++) {
			const ScienceVec &needed = tt->getNthPrereq(p)->getSciencePrereqs();
			if (std::find(needed.begin(), needed.end(), science) != needed.end()) {
				unlocks.push(McpJson(tt->getName().str()));
				break;
			}
		}
	}
	if (unlocks.size() > 0) j.set("unlocks_objects", unlocks);
	McpJson powers = McpJson::makeArray();
	for (size_t i = 0; i < data.specialPowers.items.size(); i++) {
		if (data.specialPowers.items[i]->getRequiredScience() == science) powers.push(McpJson(data.specialPowers.items[i]->getName().str()));
	}
	if (powers.size() > 0) j.set("unlocks_special_powers", powers);
	return j;
}

McpJson describeSpecialPower(const FactionData &data, const SpecialPowerTemplate *power)
{
	McpJson j = McpJson::makeObject();
	j.set("name", power->getName().str());
	const char *typeName = SpecialPowerMaskType::getNameFromSingleBit(power->getSpecialPowerType());
	if (typeName) j.set("type", typeName);
	j.set("reload_time", framesToSeconds(power->getReloadTime()));
	if (power->getRequiredScience() != SCIENCE_INVALID) {
		j.set("required_science", TheScienceStore->getInternalNameForScience(power->getRequiredScience()).str());
	}
	if (power->isShortcutPower()) j.set("shortcut", true);
	if (power->hasPublicTimer()) j.set("public_timer", true);
	if (power->getCost() > 0) j.set("cost", (int)power->getCost());
	if (power->getRadiusCursorRadius() > 0) j.set("radius", num(power->getRadiusCursorRadius()));
	std::map<const SpecialPowerTemplate *, OrderedSet<std::string> >::const_iterator users = data.powerUsers.find(power);
	if (users != data.powerUsers.end()) j.set("used_by", stringArray(users->second));
	return j;
}

McpJson describeProductionChanges(const ProductionChangeMap &changes)
{
	McpJson j = McpJson::makeObject();
	for (ProductionChangeMap::const_iterator it = changes.begin(); it != changes.end(); ++it) {
		j.set(TheNameKeyGenerator->keyToName(it->first).str(), num(it->second));
	}
	return j;
}

McpJson describePlayerTemplate(const PlayerTemplate *pt)
{
	McpJson j = McpJson::makeObject();
	j.set("name", pt->getName().str()).set("side", pt->getSide().str());
	if (!pt->getBaseSide().isEmpty()) j.set("base_side", pt->getBaseSide().str());
	const std::string displayName = displayText(pt->getDisplayName());
	if (!displayName.empty()) j.set("display_name", displayName);
	j.set("playable", (bool)(pt->isPlayableSide() != 0));
	j.set("start_money", (int)pt->getMoney()->countMoney());
	j.set("starting_building", pt->getStartingBuilding().str());
	McpJson units = McpJson::makeArray();
	for (Int i = 0; i < MAX_MP_STARTING_UNITS; i++) {
		if (!pt->getStartingUnit(i).isEmpty()) units.push(McpJson(pt->getStartingUnit(i).str()));
	}
	j.set("starting_units", units);
	j.set("intrinsic_sciences", scienceNames(pt->getIntrinsicSciences()));
	if (pt->getIntrinsicSciencePurchasePoints() > 0) j.set("intrinsic_science_points", (int)pt->getIntrinsicSciencePurchasePoints());
	j.set("science_command_sets", McpJson::makeObject().set("rank1", pt->getPurchaseScienceCommandSetRank1().str())
		.set("rank3", pt->getPurchaseScienceCommandSetRank3().str()).set("rank8", pt->getPurchaseScienceCommandSetRank8().str()));
	if (!pt->getSpecialPowerShortcutCommandSet().isEmpty()) j.set("special_power_shortcut_command_set", pt->getSpecialPowerShortcutCommandSet().str());

	// Multipliers on cost and build time of single types (1 + the change given in the INI).
	if (!pt->getProductionCostChanges().empty()) j.set("production_cost_changes", describeProductionChanges(pt->getProductionCostChanges()));
	if (!pt->getProductionTimeChanges().empty()) j.set("production_time_changes", describeProductionChanges(pt->getProductionTimeChanges()));
	if (!pt->getProductionVeterancyLevels().empty()) {
		McpJson levels = McpJson::makeObject();
		const ProductionVeterancyMap &map = pt->getProductionVeterancyLevels();
		for (ProductionVeterancyMap::const_iterator it = map.begin(); it != map.end(); ++it) {
			levels.set(TheNameKeyGenerator->keyToName(it->first).str(), TheVeterancyNames[it->second]);
		}
		j.set("production_veterancy", levels);
	}
	return j;
}

const PlayerTemplate *requireFaction(const std::string &name)
{
	const PlayerTemplate *bySide = nullptr;
	for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); i++) {
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (pt == nullptr) continue;
		if (pt->getName().compareNoCase(name.c_str()) == 0) return pt;
		if (bySide == nullptr && pt->getSide().compareNoCase(name.c_str()) == 0) bySide = pt;
	}
	if (bySide == nullptr) {
		mcpFail("unknown faction '%s' (see sides.list_factions; give the faction name or its side)", name.c_str());
	}
	return bySide;
}

bool wantsSection(const McpJson &args, const char *section)
{
	if (!args.has("sections")) return true;
	const McpJson &sections = mcpArgArray(args, "sections");
	for (size_t i = 0; i < sections.size(); i++) {
		if (sections.at(i).isString() && sections.at(i).asString() == section) return true;
	}
	return false;
}

/// The tech tree and unit stats of a faction, from the loaded game data.
McpJson cmdDescribeFaction(const McpJson &args)
{
	const PlayerTemplate *pt = requireFaction(mcpArgString(args, "faction"));
	const std::string scope = mcpArgString(args, "scope", "side");
	if (scope != "side" && scope != "tree") {
		mcpFail("scope must be \"side\" (tech tree and the other unit and structure types of the side) or \"tree\"");
	}
	if (args.has("sections")) {
		static const char *const known[] = { "faction", "objects", "weapons", "armors", "upgrades", "sciences", "special_powers", nullptr };
		const McpJson &sections = mcpArgArray(args, "sections");
		for (size_t i = 0; i < sections.size(); i++) {
			Bool found = FALSE;
			for (Int k = 0; known[k]; k++) {
				if (sections.at(i).isString() && sections.at(i).asString() == known[k]) found = TRUE;
			}
			if (!found) mcpFail("sections may hold: faction, objects, weapons, armors, upgrades, sciences, special_powers");
		}
	}

	FactionData data;
	McpJson missing = McpJson::makeArray();
	std::vector<AsciiString> starts;
	starts.push_back(pt->getStartingBuilding());
	for (Int i = 0; i < MAX_MP_STARTING_UNITS; i++) {
		starts.push_back(pt->getStartingUnit(i));
	}
	for (size_t i = 0; i < starts.size(); i++) {
		if (starts[i].isEmpty()) continue;
		const ThingTemplate *tt = TheThingFactory->findTemplate(starts[i], FALSE);
		if (tt) collectTechTree(data, tt); else missing.push(McpJson(starts[i].str()));
	}
	if (scope == "side") {
		// The types of a faction need not carry the side of its player template, so the sides most
		// of the tech tree uses count as well. A single borrowed type does not pull its side in.
		std::map<std::string, Int> sideUse;
		for (size_t i = 0; i < data.objects.items.size(); i++) {
			sideUse[data.objects.items[i]->getDefaultOwningSide().str()]++;
		}
		std::set<std::string> sides;
		sides.insert(pt->getSide().str());
		for (std::map<std::string, Int>::const_iterator it = sideUse.begin(); it != sideUse.end(); ++it) {
			if (!it->first.empty() && it->second * 5 >= (Int)data.objects.items.size()) sides.insert(it->first);
		}
		for (const ThingTemplate *tt = TheThingFactory->firstTemplate(); tt; tt = tt->friend_getNextTemplate()) {
			if (isArmyType(tt) && sides.find(tt->getDefaultOwningSide().str()) != sides.end()) data.objects.add(tt);
		}
	}

	// Narrow the objects down to the ones asked for; the tech tree links were built from all of them.
	if (args.has("templates")) {
		const McpJson &names = mcpArgArray(args, "templates");
		OrderedSet<const ThingTemplate *> chosen;
		for (size_t i = 0; i < names.size(); i++) {
			if (!names.at(i).isString()) mcpFail("'templates' must be an array of template names");
			const ThingTemplate *tt = TheThingFactory->findTemplate(AsciiString(names.at(i).asString().c_str()), FALSE);
			if (tt) chosen.add(tt); else missing.push(names.at(i));
		}
		data.objects = chosen;
	}

	for (size_t i = 0; i < data.objects.items.size(); i++) {
		collectCommandRefs(data, data.objects.items[i]);
	}
	collectScienceOffers(data, pt->getPurchaseScienceCommandSetRank1(), 1);
	collectScienceOffers(data, pt->getPurchaseScienceCommandSetRank3(), 3);
	collectScienceOffers(data, pt->getPurchaseScienceCommandSetRank8(), 8);
	collectScienceOffers(data, pt->getSpecialPowerShortcutCommandSet(), 0);
	for (size_t i = 0; i < pt->getIntrinsicSciences().size(); i++) {
		collectScience(data, pt->getIntrinsicSciences()[i]);
	}
	for (size_t i = 0; i < data.objects.items.size(); i++) {
		const ThingTemplate *tt = data.objects.items[i];
		for (Int p = 0; p < tt->getPrereqCount(); p++) {
			const ScienceVec &needed = tt->getNthPrereq(p)->getSciencePrereqs();
			for (size_t k = 0; k < needed.size(); k++) collectScience(data, needed[k]);
		}
	}

	// Describing the objects is what finds the weapons and armors, so it runs in any case.
	McpJson objects = McpJson::makeArray();
	for (size_t i = 0; i < data.objects.items.size(); i++) {
		objects.push(describeObject(data, data.objects.items[i]));
	}

	McpJson j = McpJson::makeObject();
	if (wantsSection(args, "faction")) j.set("faction", describePlayerTemplate(pt));
	if (wantsSection(args, "objects")) j.set("objects", objects);
	if (wantsSection(args, "weapons")) {
		McpJson list = McpJson::makeArray();
		for (size_t i = 0; i < data.weapons.items.size(); i++) list.push(describeWeapon(data.weapons.items[i]));
		j.set("weapons", list);
	}
	if (wantsSection(args, "armors")) {
		McpJson list = McpJson::makeArray();
		for (size_t i = 0; i < data.armors.items.size(); i++) list.push(describeArmor(data.armors.items[i]));
		j.set("armors", list);
	}
	if (wantsSection(args, "upgrades")) {
		McpJson list = McpJson::makeArray();
		for (size_t i = 0; i < data.upgrades.items.size(); i++) list.push(describeUpgrade(data, data.upgrades.items[i]));
		j.set("upgrades", list);
	}
	if (wantsSection(args, "sciences")) {
		McpJson list = McpJson::makeArray();
		for (size_t i = 0; i < data.sciences.items.size(); i++) list.push(describeScience(data, pt, data.sciences.items[i]));
		j.set("sciences", list);
	}
	if (wantsSection(args, "special_powers")) {
		McpJson list = McpJson::makeArray();
		for (size_t i = 0; i < data.specialPowers.items.size(); i++) list.push(describeSpecialPower(data, data.specialPowers.items[i]));
		j.set("special_powers", list);
	}
	j.set("counts", McpJson::makeObject().set("objects", (int)data.objects.items.size()).set("in_tech_tree", (int)data.inTechTree.size())
		.set("weapons", (int)data.weapons.items.size()).set("armors", (int)data.armors.items.size())
		.set("upgrades", (int)data.upgrades.items.size()).set("sciences", (int)data.sciences.items.size())
		.set("special_powers", (int)data.specialPowers.items.size()));
	if (missing.size() > 0) j.set("unknown_templates", missing);
	return j;
}

} // namespace

void mcpRegisterFactionCommands()
{
	mcpRegisterCommand("faction.describe", cmdDescribeFaction, "{faction,scope?=side|tree,sections?:[faction|objects|weapons|armors|upgrades|sciences|special_powers],templates?:[names]} Tech tree and unit stats of a faction from the game data: what builds what, prerequisites, cost, health, armor, weapons, movement, upgrades, sciences, special powers.");
}
