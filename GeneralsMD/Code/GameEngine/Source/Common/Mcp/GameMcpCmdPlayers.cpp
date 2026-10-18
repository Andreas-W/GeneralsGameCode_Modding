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

// GameMcpCmdPlayers.cpp
// Player inspection commands of the MCP bridge.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/Energy.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/ScoreKeeper.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/Object.h"

#include <map>

namespace
{

const char *relationshipName(Relationship relationship)
{
	switch (relationship) {
		case ENEMIES: return "enemies";
		case ALLIES: return "allies";
		default: return "neutral";
	}
}

const char *difficultyName(GameDifficulty difficulty)
{
	switch (difficulty) {
		case DIFFICULTY_EASY: return "easy";
		case DIFFICULTY_NORMAL: return "normal";
		case DIFFICULTY_HARD: return "hard";
		default: return "";
	}
}

McpJson describeBrief(Player *player)
{
	McpJson row = McpJson::makeObject();
	row.set("name", gameMcpPlayerName(player));
	row.set("display_name", gameMcpUtf8(player->getPlayerDisplayName()));
	row.set("index", player->getPlayerIndex());
	row.set("side", player->getSide().str());
	row.set("faction", player->getPlayerTemplate() ? player->getPlayerTemplate()->getName().str() : "");
	row.set("type", player->getPlayerType() == PLAYER_HUMAN ? "human" : "computer");
	if (player->getPlayerType() == PLAYER_COMPUTER) {
		row.set("difficulty", difficultyName(player->getPlayerDifficulty()));
	}
	row.set("local", player->isLocalPlayer() != FALSE);
	row.set("active", player->isPlayerActive() != FALSE);
	row.set("money", player->getMoney()->countMoney());
	return row;
}

McpJson describeFull(Player *player)
{
	McpJson result = describeBrief(player);
	result.set("power", McpJson::makeObject().set("production", player->getEnergy()->getProduction())
		.set("consumption", player->getEnergy()->getConsumption()));
	result.set("rank", player->getRankLevel()).set("skill_points", player->getSkillPoints())
		.set("science_points", player->getSciencePurchasePoints());
	result.set("instant_build", player->buildsInstantly() != FALSE);

	McpJson sciences = McpJson::makeArray();
	const std::vector<AsciiString> scienceNames = TheScienceStore->friend_getScienceNames();
	for (size_t s = 0; s < scienceNames.size(); s++) {
		if (player->hasScience(TheScienceStore->getScienceFromInternalName(scienceNames[s]))) {
			sciences.push(scienceNames[s].str());
		}
	}
	result.set("sciences", sciences);

	McpJson upgrades = McpJson::makeArray();
	for (const UpgradeTemplate *upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade; upgrade = upgrade->friend_getNext()) {
		if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER && player->hasUpgradeComplete(upgrade)) {
			upgrades.push(upgrade->getUpgradeName().str());
		}
	}
	result.set("upgrades", upgrades);

	// Active battle plan sources per plan (Strategy Centers and other plan sources) and the
	// accumulated army bonuses that new or captured units receive.
	McpJson plans = McpJson::makeObject();
	plans.set("bombardment", player->getBattlePlansActiveSpecific(PLANSTATUS_BOMBARDMENT));
	plans.set("hold_the_line", player->getBattlePlansActiveSpecific(PLANSTATUS_HOLDTHELINE));
	plans.set("search_and_destroy", player->getBattlePlansActiveSpecific(PLANSTATUS_SEARCHANDDESTROY));
	const BattlePlanBonuses *planBonuses = player->getBattlePlanBonuses();
	if (planBonuses) {
		plans.set("armor_scalar", planBonuses->m_armorScalar).set("sight_range_scalar", planBonuses->m_sightRangeScalar);
	}
	result.set("battle_plans", plans);

	ScoreKeeper *score = player->getScoreKeeper();
	result.set("score", McpJson::makeObject().set("units_built", score->getTotalUnitsBuilt()).set("units_lost", score->getTotalUnitsLost())
		.set("units_destroyed", score->getTotalUnitsDestroyed()).set("buildings_built", score->getTotalBuildingsBuilt())
		.set("buildings_lost", score->getTotalBuildingsLost()).set("buildings_destroyed", score->getTotalBuildingsDestroyed())
		.set("money_earned", score->getTotalMoneyEarned()).set("money_spent", score->getTotalMoneySpent()));

	McpJson relations = McpJson::makeObject();
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++) {
		Player *other = ThePlayerList->getNthPlayer(i);
		if (other && other != player && other != ThePlayerList->getNeutralPlayer() && other->getDefaultTeam()) {
			relations.set(gameMcpPlayerName(other).c_str(), relationshipName(player->getRelationship(other->getDefaultTeam())));
		}
	}
	result.set("relationships", relations);

	std::map<std::string, Int> counts;
	for (Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject()) {
		if (object->getControllingPlayer() == player && !object->isEffectivelyDead()) {
			counts[object->getTemplate()->getName().str()]++;
		}
	}
	McpJson objects = McpJson::makeObject();
	for (std::map<std::string, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
		objects.set(it->first.c_str(), it->second);
	}
	result.set("objects", objects);
	return result;
}

McpJson cmdList(const McpJson &)
{
	gameMcpRequireGame();
	McpJson list = McpJson::makeArray();
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player) {
			list.push(describeBrief(player));
		}
	}
	return McpJson::makeObject().set("players", list);
}

McpJson cmdGet(const McpJson &args)
{
	return describeFull(gameMcpPlayerArg(args, "player"));
}

} // namespace

void gameMcpRegisterPlayerCommands()
{
	gameMcpRegisterCommand("players.list", cmdList, "All players: name, side, faction, human or computer, local, active, money.");
	gameMcpRegisterCommand("players.get", cmdGet,
		"{player?} One player (default: the local one): money, power, rank, sciences, upgrades, battle plans, score, relationships, objects by type.");
}

#endif // RTS_GAME_MCP
