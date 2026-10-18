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

// GameMcpCmdObjects.cpp
// Object inspection commands of the MCP bridge.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#define DEFINE_WEAPONSLOTTYPE_NAMES

#include "GameMcpCommands.h"

#include "Common/KindOf.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/Drawable.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/WeaponStatus.h"

#include <math.h>

namespace
{

const char *const WEAPON_STATUS_NAMES[] = { "READY_TO_FIRE", "OUT_OF_AMMO", "BETWEEN_FIRING_SHOTS", "RELOADING_CLIP", "PRE_ATTACK" };
static_assert(ARRAY_SIZE(WEAPON_STATUS_NAMES) == WEAPON_STATUS_COUNT, "Incorrect array size");

const char *const DAMAGE_STATE_NAMES[] = { "PRISTINE", "DAMAGED", "REALLYDAMAGED", "RUBBLE" };

double degrees(Real radians)
{
	return radians * 180.0 / PI;
}

std::string keyName(NameKeyType key)
{
	return TheNameKeyGenerator->keyToName(key).str();
}

/// One line per object, for lists.
McpJson describeBrief(const Object *object)
{
	McpJson row = McpJson::makeObject();
	row.set("id", (unsigned int)object->getID());
	row.set("template", object->getTemplate()->getName().str());
	if (object->getName().isNotEmpty()) {
		row.set("name", object->getName().str());
	}
	row.set("owner", gameMcpPlayerName(object->getControllingPlayer()));
	row.set("position", gameMcpCoord(*object->getPosition()));
	row.set("angle", degrees(object->getOrientation()));
	const BodyModuleInterface *body = object->getBodyModule();
	if (body) {
		row.set("health", body->getHealth()).set("max_health", body->getMaxHealth());
	}
	if (object->isEffectivelyDead()) {
		row.set("dead", true);
	}
	if (object->getContainedBy()) {
		row.set("contained_by", (unsigned int)object->getContainedBy()->getID());
	}
	return row;
}

McpJson describeWeapons(const Object *object)
{
	McpJson weapons = McpJson::makeArray();
	WeaponSlotType currentSlot = PRIMARY_WEAPON;
	const Weapon *current = object->getCurrentWeapon(&currentSlot);
	for (Int slot = 0; slot < WEAPONSLOT_COUNT; slot++) {
		const Weapon *weapon = object->getWeaponInWeaponSlot((WeaponSlotType)slot);
		if (weapon == nullptr) {
			continue;
		}
		const WeaponStatus status = weapon->getStatus();
		McpJson entry = McpJson::makeObject();
		entry.set("slot", TheWeaponSlotTypeNames[slot]);
		entry.set("name", weapon->getName().str());
		entry.set("current", current != nullptr && slot == (Int)currentSlot);
		entry.set("status", status >= 0 && status < WEAPON_STATUS_COUNT ? WEAPON_STATUS_NAMES[status] : "?");
		entry.set("ammo", weapon->getRemainingAmmo()).set("clip_size", weapon->getClipSize());
		entry.set("range", weapon->getAttackRange(object));
		entry.set("next_shot_frame", weapon->getPossibleNextShotFrame()).set("last_shot_frame", weapon->getLastShotFrame());
		entry.set("clip_reload_frames", weapon->getClipReloadTime(object));
		weapons.push(entry);
	}
	return weapons;
}

McpJson describeAI(const Object *object)
{
	const AIUpdateInterface *ai = object->getAIUpdateInterface();
	if (ai == nullptr) {
		return McpJson();
	}
	McpJson result = McpJson::makeObject();
	result.set("state", ai->getCurrentStateName().str());
	result.set("state_type", (int)ai->getAIStateType());
	result.set("idle", ai->isIdle() != FALSE).set("moving", ai->isMoving() != FALSE).set("attacking", ai->isAttacking() != FALSE);
	result.set("last_command_source", (int)ai->getLastCommandSource());
	result.set("speed", ai->getCurLocomotorSpeed());
	AIUpdateInterface *mutableAI = const_cast<AIUpdateInterface *>(ai);
	const Object *goal = mutableAI->getGoalObject();
	if (goal) {
		result.set("goal_object", (unsigned int)goal->getID());
	}
	const Coord3D *goalPos = ai->getGoalPosition();
	if (goalPos && (goalPos->x != 0 || goalPos->y != 0)) {
		result.set("goal_position", gameMcpCoord(*goalPos));
	}
	return result;
}

McpJson describeModules(const Object *object)
{
	McpJson modules = McpJson::makeArray();
	for (BehaviorModule **m = object->getBehaviorModules(); m && *m; ++m) {
		modules.push(McpJson::makeObject().set("module", keyName((*m)->getModuleNameKey())).set("tag", keyName((*m)->getModuleTagNameKey())));
	}
	return modules;
}

McpJson describeSpecialPowers(const Object *object)
{
	McpJson powers = McpJson::makeArray();
	for (BehaviorModule **m = object->getBehaviorModules(); m && *m; ++m) {
		const SpecialPowerModuleInterface *power = (*m)->getSpecialPower();
		if (power == nullptr) {
			continue;
		}
		powers.push(McpJson::makeObject().set("power", power->getPowerName().str()).set("ready", power->isReady() != FALSE)
			.set("percent_ready", power->getPercentReady()).set("ready_frame", power->getReadyFrame()));
	}
	return powers;
}

McpJson describeProduction(Object *object)
{
	ProductionUpdateInterface *production = object->getProductionUpdateInterface();
	if (production == nullptr) {
		return McpJson();
	}
	McpJson queue = McpJson::makeArray();
	for (const ProductionEntry *entry = production->firstProduction(); entry; entry = production->nextProduction(entry)) {
		McpJson row = McpJson::makeObject();
		if (entry->getProductionObject()) {
			row.set("unit", entry->getProductionObject()->getName().str());
		} else if (entry->getProductionUpgrade()) {
			row.set("upgrade", entry->getProductionUpgrade()->getUpgradeName().str());
		}
		row.set("percent", entry->getPercentComplete()).set("quantity", entry->getProductionQuantity())
			.set("remaining", entry->getProductionQuantityRemaining());
		queue.push(row);
	}
	return queue;
}

McpJson describeContain(const Object *object)
{
	const ContainModuleInterface *contain = object->getContain();
	if (contain == nullptr) {
		return McpJson();
	}
	McpJson ids = McpJson::makeArray();
	const ContainedItemsList *items = contain->getContainedItemsList();
	if (items) {
		for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it) {
			ids.push((unsigned int)(*it)->getID());
		}
	}
	return McpJson::makeObject().set("count", contain->getContainCount()).set("max", contain->getContainMax()).set("ids", ids);
}

McpJson describeUpgrades(const Object *object)
{
	McpJson names = McpJson::makeArray();
	for (const UpgradeTemplate *upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade; upgrade = upgrade->friend_getNext()) {
		if (upgrade->getUpgradeType() == UPGRADE_TYPE_OBJECT && object->hasUpgrade(upgrade)) {
			names.push(upgrade->getUpgradeName().str());
		}
	}
	return names;
}

/// Stat modifiers that bonuses such as battle plans, upgrades and powers change.
McpJson describeBonuses(Object *object)
{
	McpJson result = McpJson::makeObject();
	result.set("weapon_bonus", gameMcpFlagNames(object->getWeaponBonusCondition()));
	result.set("weapon_set", gameMcpFlagNames(object->getWeaponSetFlags()));
	BodyModuleInterface *body = object->getBodyModule();
	if (body) {
		McpJson armorSet = McpJson::makeArray();
		for (Int i = 0; i < ARMORSET_COUNT; i++) {
			if (body->testArmorSetFlag((ArmorSetType)i)) {
				const char *name = ArmorSetFlags::getNameFromSingleBit(i);
				armorSet.push(name ? name : "?");
			}
		}
		result.set("armor_set", armorSet);
		result.set("armor_damage_scalar", body->getDamageScalar());
	}
	result.set("vision_range", object->getVisionRange()).set("shroud_clearing_range", object->getShroudClearingRange());
	const AIUpdateInterface *ai = object->getAIUpdateInterface();
	if (ai) {
		result.set("speed_multiplier", ai->getSpeedMultiplier());
	}
	return result;
}

bool wants(const McpJson &sections, const char *name)
{
	if (!sections.isArray()) {
		return true;
	}
	for (size_t i = 0; i < sections.size(); i++) {
		if (_stricmp(sections.at(i).asString().c_str(), name) == 0) {
			return true;
		}
	}
	return false;
}

/// Everything about one object.
McpJson describeFull(Object *object, const McpJson &sections)
{
	McpJson result = describeBrief(object);
	result.set("team", object->getTeam() ? object->getTeam()->getName().str() : "");
	result.set("display_name", gameMcpUtf8(object->getTemplate()->getDisplayName()));
	result.set("status", gameMcpFlagNames(object->getStatusBits()));
	result.set("disabled", gameMcpFlagNames(object->getDisabledFlags()));
	if (wants(sections, "kindof")) {
		KindOfMaskType kindOf;
		for (Int i = 0; i < KINDOF_COUNT; i++) {
			if (object->isKindOf((KindOfType)i)) {
				kindOf.set(i);
			}
		}
		result.set("kindof", gameMcpFlagNames(kindOf));
	}
	const BodyModuleInterface *body = object->getBodyModule();
	if (body) {
		const Int state = (Int)body->getDamageState();
		result.set("damage_state", state >= 0 && state < (Int)ARRAY_SIZE(DAMAGE_STATE_NAMES) ? DAMAGE_STATE_NAMES[state] : "?");
	}
	result.set("veterancy", TheVeterancyNames[object->getVeterancyLevel()]);
	if (object->getExperienceTracker()) {
		result.set("experience", object->getExperienceTracker()->getCurrentExperience());
	}
	if (object->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION)) {
		result.set("construction_percent", object->getConstructionPercent());
	}
	if (object->getPhysics()) {
		result.set("velocity", gameMcpCoord(*object->getPhysics()->getVelocity()));
	}
	if (object->getDrawable() && wants(sections, "model")) {
		result.set("model_conditions", gameMcpFlagNames(object->getDrawable()->getModelConditionFlags()));
	}
	if (wants(sections, "ai")) {
		result.set("ai", describeAI(object));
	}
	if (wants(sections, "weapons")) {
		result.set("weapons", describeWeapons(object));
	}
	if (wants(sections, "upgrades")) {
		result.set("upgrades", describeUpgrades(object));
	}
	if (wants(sections, "bonuses")) {
		result.set("bonuses", describeBonuses(object));
	}
	if (wants(sections, "special_powers")) {
		result.set("special_powers", describeSpecialPowers(object));
	}
	if (wants(sections, "production")) {
		result.set("production", describeProduction(object));
	}
	if (wants(sections, "contain")) {
		result.set("contain", describeContain(object));
	}
	if (wants(sections, "modules")) {
		result.set("modules", describeModules(object));
	}
	return result;
}

McpJson cmdList(const McpJson &args)
{
	gameMcpRequireGame();
	const Player *owner = args.has("player") ? gameMcpPlayerArg(args, "player") : nullptr;
	const std::string templateFilter = gameMcpArgString(args, "template", "");
	const std::string nameFilter = gameMcpArgString(args, "name", "");
	const Int kindOf = args.has("kindof") ? gameMcpRequireFlag<KindOfMaskType>(gameMcpArgString(args, "kindof"), "kindof") : -1;
	const Int status = args.has("status") ? gameMcpRequireFlag<ObjectStatusMaskType>(gameMcpArgString(args, "status"), "status") : -1;
	const bool includeDead = gameMcpArgBool(args, "include_dead", false);
	const Int limit = gameMcpArgInt(args, "limit", 200);
	const bool circle = args.has("radius");
	const bool rect = args.has("x0");
	Real cx = 0, cy = 0, radius = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (circle) {
		cx = (Real)gameMcpArgNumber(args, "x");
		cy = (Real)gameMcpArgNumber(args, "y");
		radius = (Real)gameMcpArgNumber(args, "radius");
	} else if (rect) {
		x0 = (Real)gameMcpArgNumber(args, "x0");
		y0 = (Real)gameMcpArgNumber(args, "y0");
		x1 = (Real)gameMcpArgNumber(args, "x1");
		y1 = (Real)gameMcpArgNumber(args, "y1");
	}

	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject()) {
		if (!includeDead && object->isEffectivelyDead()) continue;
		if (owner && object->getControllingPlayer() != owner) continue;
		if (!gameMcpContainsNoCase(object->getTemplate()->getName().str(), templateFilter.c_str())) continue;
		if (!nameFilter.empty() && !gameMcpContainsNoCase(object->getName().str(), nameFilter.c_str())) continue;
		if (kindOf >= 0 && !object->isKindOf((KindOfType)kindOf)) continue;
		if (status >= 0 && !object->testStatus((ObjectStatusTypes)status)) continue;
		const Coord3D *pos = object->getPosition();
		if (circle && (pos->x - cx) * (pos->x - cx) + (pos->y - cy) * (pos->y - cy) > radius * radius) continue;
		if (rect && (pos->x < min(x0, x1) || pos->x > max(x0, x1) || pos->y < min(y0, y1) || pos->y > max(y0, y1))) continue;
		matched++;
		if ((Int)list.size() < limit) {
			list.push(describeBrief(object));
		}
	}
	return McpJson::makeObject().set("objects", list).set("matched", matched).set("frame", TheGameLogic->getFrame());
}

McpJson cmdGet(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < objects.size(); i++) {
		list.push(describeFull(objects[i], args.get("sections")));
	}
	return McpJson::makeObject().set("objects", list).set("frame", TheGameLogic->getFrame());
}

McpJson cmdTemplates(const McpJson &args)
{
	const std::string contains = gameMcpArgString(args, "contains", "");
	const std::string side = gameMcpArgString(args, "side", "");
	const Int kindOf = args.has("kindof") ? gameMcpRequireFlag<KindOfMaskType>(gameMcpArgString(args, "kindof"), "kindof") : -1;
	const Int limit = gameMcpArgInt(args, "limit", 300);
	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (const ThingTemplate *tmpl = TheThingFactory->firstTemplate(); tmpl; tmpl = tmpl->friend_getNextTemplate()) {
		if (!gameMcpContainsNoCase(tmpl->getName().str(), contains.c_str())) continue;
		if (!side.empty() && _stricmp(tmpl->getDefaultOwningSide().str(), side.c_str()) != 0) continue;
		if (kindOf >= 0 && !tmpl->isKindOf((KindOfType)kindOf)) continue;
		matched++;
		if ((Int)list.size() < limit) {
			list.push(McpJson::makeObject().set("name", tmpl->getName().str()).set("side", tmpl->getDefaultOwningSide().str())
				.set("cost", tmpl->friend_getBuildCost()).set("build_seconds", tmpl->friend_getBuildTime()));
		}
	}
	return McpJson::makeObject().set("templates", list).set("matched", matched);
}

} // namespace

void gameMcpRegisterObjectCommands()
{
	gameMcpRegisterCommand("objects.list", cmdList,
		"{player?, template?, name?, kindof?, status?, x,y,radius | x0,y0,x1,y1, include_dead?, limit=200} "
		"Objects in the game: id, template, owner, position, angle (degrees), health.");
	gameMcpRegisterCommand("objects.get", cmdGet,
		"{ids | id, sections?} Everything about objects: status, disabled, kindof, veterancy, ai, weapons, upgrades, bonuses, "
		"special_powers, production, contain, modules, model (sections limits the optional parts).");
	gameMcpRegisterCommand("objects.templates", cmdTemplates, "{contains?, side?, kindof?, limit=300} Object templates that can be spawned.");
}

#endif // RTS_GAME_MCP
