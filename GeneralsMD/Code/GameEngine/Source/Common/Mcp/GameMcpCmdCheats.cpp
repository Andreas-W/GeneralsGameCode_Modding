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

// GameMcpCmdCheats.cpp
// Test commands of the MCP bridge that change the game directly, between two logic frames:
// spawning and removing objects, changing objects and players. Unlike orders they are not
// recorded, so a replay of a game changed this way does not play back the same.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/Science.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"

namespace
{

VeterancyLevel requireVeterancy(const McpJson &value)
{
	if (value.isNumber()) {
		const Int level = (Int)value.asNumber();
		if (level >= LEVEL_FIRST && level <= LEVEL_LAST) {
			return (VeterancyLevel)level;
		}
	} else if (value.isString()) {
		for (Int level = LEVEL_FIRST; level <= LEVEL_LAST; level++) {
			if (_stricmp(TheVeterancyNames[level], value.asString().c_str()) == 0) {
				return (VeterancyLevel)level;
			}
		}
	}
	gameMcpFail("veterancy must be 0..%d or one of REGULAR, VETERAN, ELITE, HEROIC", (int)LEVEL_LAST);
}

const UpgradeTemplate *requireUpgrade(const std::string &name)
{
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade(name.c_str());
	if (upgrade == nullptr) {
		gameMcpFail("unknown upgrade '%s'", name.c_str());
	}
	return upgrade;
}

ScienceType requireScience(const std::string &name)
{
	const ScienceType science = TheScienceStore->getScienceFromInternalName(name.c_str());
	if (science == SCIENCE_INVALID) {
		gameMcpFail("unknown science '%s'", name.c_str());
	}
	return science;
}

McpJson cmdSpawn(const McpJson &args)
{
	gameMcpRequireGame();
	const ThingTemplate *tmpl = gameMcpRequireTemplate(gameMcpArgString(args, "template"));
	Player *player = gameMcpPlayerArg(args, "player");
	Team *team = player->getDefaultTeam();
	if (args.has("team")) {
		const std::string teamName = gameMcpArgString(args, "team");
		team = TheScriptEngine->getTeamNamed(teamName.c_str());
		if (team == nullptr) {
			gameMcpFail("unknown team '%s'", teamName.c_str());
		}
	}
	if (team == nullptr) {
		gameMcpFail("player %s has no team to put the object in", gameMcpPlayerName(player).c_str());
	}
	const Coord3D center = gameMcpArgPosition(args);
	const Real angle = (Real)(gameMcpArgNumber(args, "angle", 0) * PI / 180.0);
	const Int count = gameMcpArgInt(args, "count", 1);
	const Real spacing = (Real)gameMcpArgNumber(args, "spacing", 20);
	if (count < 1 || count > 500) {
		gameMcpFail("count must be 1..500");
	}

	// Several objects stand in a square grid around the position.
	Int columns = 1;
	while (columns * columns < count) {
		columns++;
	}
	McpJson ids = McpJson::makeArray();
	for (Int i = 0; i < count; i++) {
		Coord3D pos = center;
		if (count > 1) {
			pos.x += ((i % columns) - (columns - 1) * 0.5f) * spacing;
			pos.y += ((i / columns) - (columns - 1) * 0.5f) * spacing;
			pos.z = args.has("z") ? center.z : TheTerrainLogic->getGroundHeight(pos.x, pos.y);
		}
		Object *object = TheThingFactory->newObject(tmpl, team);
		if (object == nullptr) {
			gameMcpFail("could not create '%s'", tmpl->getName().str());
		}
		object->setOrientation(angle);
		object->setPosition(&pos);
		if (object->isKindOf(KINDOF_BLAST_CRATER)) {
			TheTerrainLogic->createCraterInTerrain(object);
			TheAI->pathfinder()->addObjectToPathfindMap(object);
		}
		if (args.has("name") && count == 1) {
			object->setName(gameMcpArgString(args, "name").c_str());
			TheScriptEngine->addObjectToCache(object);
		}
		ids.push((unsigned int)object->getID());
	}
	return McpJson::makeObject().set("ids", ids).set("frame", TheGameLogic->getFrame());
}

McpJson cmdDestroy(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	// kill lets the object die as if it were destroyed (death effects, rubble); remove takes it away silently.
	const bool remove = gameMcpArgString(args, "how", "kill") == "remove";
	for (size_t i = 0; i < objects.size(); i++) {
		if (remove) {
			TheGameLogic->destroyObject(objects[i]);
		} else {
			objects[i]->kill();
		}
	}
	return McpJson::makeObject().set("count", (int)objects.size()).set("how", remove ? "remove" : "kill");
}

McpJson cmdSet(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	for (size_t i = 0; i < objects.size(); i++) {
		Object *object = objects[i];
		BodyModuleInterface *body = object->getBodyModule();
		if (args.has("max_health")) {
			if (body == nullptr) gameMcpFail("object %u has no body", (unsigned int)object->getID());
			body->setMaxHealth((Real)gameMcpArgNumber(args, "max_health"), SAME_CURRENTHEALTH);
		}
		if (args.has("health") || args.has("health_percent")) {
			if (body == nullptr) gameMcpFail("object %u has no body", (unsigned int)object->getID());
			const Real health = args.has("health") ? (Real)gameMcpArgNumber(args, "health")
				: body->getMaxHealth() * (Real)gameMcpArgNumber(args, "health_percent") / 100.0f;
			if (health <= 0) {
				object->kill();
				continue;
			}
			body->internalChangeHealth(min(health, body->getMaxHealth()) - body->getHealth());
		}
		if (args.has("x") || args.has("y")) {
			Coord3D pos = *object->getPosition();
			pos.x = (Real)gameMcpArgNumber(args, "x", pos.x);
			pos.y = (Real)gameMcpArgNumber(args, "y", pos.y);
			pos.z = args.has("z") ? (Real)gameMcpArgNumber(args, "z") : TheTerrainLogic->getGroundHeight(pos.x, pos.y);
			object->setPosition(&pos);
		}
		if (args.has("angle")) {
			object->setOrientation((Real)(gameMcpArgNumber(args, "angle") * PI / 180.0));
		}
		if (args.has("veterancy") && object->getExperienceTracker()) {
			object->getExperienceTracker()->setVeterancyLevel(requireVeterancy(args.get("veterancy")));
		}
		if (args.has("upgrade")) {
			object->giveUpgrade(requireUpgrade(gameMcpArgString(args, "upgrade")));
		}
		if (args.has("set_status") || args.has("clear_status")) {
			const bool set = args.has("set_status");
			const Int bit = gameMcpRequireFlag<ObjectStatusMaskType>(gameMcpArgString(args, set ? "set_status" : "clear_status"), "status");
			ObjectStatusMaskType mask;
			mask.set(bit);
			object->setStatus(mask, set);
		}
	}
	return McpJson::makeObject().set("count", (int)objects.size());
}

McpJson cmdPlayerSet(const McpJson &args)
{
	Player *player = gameMcpPlayerArg(args, "player");
	Money *money = player->getMoney();
	if (args.has("money")) {
		money->withdraw(money->countMoney(), FALSE);
		money->deposit((UnsignedInt)max(0, gameMcpArgInt(args, "money")), FALSE, FALSE);
	}
	if (args.has("add_money")) {
		money->deposit((UnsignedInt)max(0, gameMcpArgInt(args, "add_money")), FALSE, FALSE);
	}
	if (args.has("rank")) {
		player->setRankLevel(gameMcpArgInt(args, "rank"));
	}
	if (args.has("add_skill_points")) {
		player->addSkillPoints(gameMcpArgInt(args, "add_skill_points"));
	}
	if (args.has("add_science_points")) {
		player->addSciencePurchasePoints(gameMcpArgInt(args, "add_science_points"));
	}
	const McpJson &sciences = args.get("sciences");
	for (size_t i = 0; i < sciences.size(); i++) {
		player->grantScience(requireScience(sciences.at(i).asString()));
	}
	if (gameMcpArgBool(args, "all_sciences", false)) {
		const std::vector<AsciiString> names = TheScienceStore->friend_getScienceNames();
		for (size_t i = 0; i < names.size(); i++) {
			const ScienceType science = TheScienceStore->getScienceFromInternalName(names[i]);
			if (science != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(science)) {
				player->grantScience(science);
			}
		}
	}
	const McpJson &upgrades = args.get("upgrades");
	for (size_t i = 0; i < upgrades.size(); i++) {
		const UpgradeTemplate *upgrade = requireUpgrade(upgrades.at(i).asString());
		if (upgrade->getUpgradeType() != UPGRADE_TYPE_PLAYER) {
			gameMcpFail("'%s' is an object upgrade; give it to an object with objects.set upgrade", upgrade->getUpgradeName().str());
		}
		player->addUpgrade(upgrade, UPGRADE_STATUS_COMPLETE);
	}
	if (args.has("instant_build") && gameMcpArgBool(args, "instant_build", false) != (player->buildsInstantly() != FALSE)) {
		player->toggleInstantBuild();
	}
	if (args.has("special_power_delays")) {
		// Applies to every player, as the cheat does.
		TheWritableGlobalData->m_specialPowerUsesDelay = gameMcpArgBool(args, "special_power_delays", true);
	}
	return McpJson::makeObject().set("player", gameMcpPlayerName(player)).set("money", money->countMoney())
		.set("instant_build", player->buildsInstantly() != FALSE).set("special_power_delays", TheGlobalData->m_specialPowerUsesDelay != FALSE);
}

} // namespace

void gameMcpRegisterCheatCommands()
{
	gameMcpRegisterCommand("objects.spawn", cmdSpawn,
		"{template, player?, team?, x, y, z?, angle=0, count=1, spacing=20, name?} Creates objects for a player (default: the local one).");
	gameMcpRegisterCommand("objects.destroy", cmdDestroy, "{ids, how=kill|remove} Kills objects (they die normally) or removes them silently.");
	gameMcpRegisterCommand("objects.set", cmdSet,
		"{ids, health? | health_percent?, max_health?, x?, y?, z?, angle?, veterancy?, upgrade?, set_status? | clear_status?} Changes objects.");
	gameMcpRegisterCommand("players.set", cmdPlayerSet,
		"{player?, money?, add_money?, rank?, add_skill_points?, add_science_points?, sciences?, all_sciences?, upgrades?, "
		"instant_build?, special_power_delays?} Changes a player.");
}

#endif // RTS_GAME_MCP
