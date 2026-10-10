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

// AIMatch.cpp
// A skirmish between AI players, started from the command line and written to a log.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/AIMatch.h"

#include "Common/Energy.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Science.h"
#include "Common/ScoreKeeper.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameInfo.h"

#include <stdlib.h>

AIMatch *TheAIMatch = nullptr;

namespace
{

const UnsignedInt TEAM_SCAN_FRAMES = LOGICFRAMES_PER_SECOND;

/// A string as a JSON string literal.
std::string quoted(const char *text)
{
	std::string out = "\"";
	for (const char *c = text; *c; c++) {
		if (*c == '"' || *c == '\\') {
			out += '\\';
			out += *c;
		} else if ((unsigned char)*c < 0x20) {
			char buffer[8];
			sprintf(buffer, "\\u%04x", (unsigned char)*c);
			out += buffer;
		} else {
			out += *c;
		}
	}
	return out + "\"";
}

std::string quoted(const AsciiString &text)
{
	return quoted(text.str());
}

std::string playerName(Player *player)
{
	return TheNameKeyGenerator->keyToName(player->getPlayerNameKey()).str();
}

void writeCounts(FILE *file, const std::map<std::string, Int> &counts)
{
	fputc('{', file);
	for (std::map<std::string, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
		fprintf(file, "%s%s:%d", it == counts.begin() ? "" : ",", quoted(it->first.c_str()).c_str(), it->second);
	}
	fputc('}', file);
}

Int slotStateFor(const AsciiString &difficulty)
{
	if (difficulty.compareNoCase("easy") == 0) return SLOT_EASY_AI;
	if (difficulty.compareNoCase("normal") == 0 || difficulty.compareNoCase("medium") == 0) return SLOT_MED_AI;
	if (difficulty.compareNoCase("hard") == 0 || difficulty.compareNoCase("brutal") == 0) return SLOT_BRUTAL_AI;
	return -1;
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

/// What counts as a unit or structure of a player in the log.
Bool isArmyObject(const Object *object)
{
	if (object->isEffectivelyDead() || object->isKindOf(KINDOF_PROJECTILE)) {
		return FALSE;
	}
	return object->isKindOf(KINDOF_STRUCTURE) || object->isKindOf(KINDOF_INFANTRY) || object->isKindOf(KINDOF_VEHICLE)
		|| object->isKindOf(KINDOF_AIRCRAFT);
}

} // namespace

//-------------------------------------------------------------------------------------------------
AIMatch::AIMatch() :
	m_maxFrames(30 * 60 * LOGICFRAMES_PER_SECOND),
	m_snapshotFrames(10 * LOGICFRAMES_PER_SECOND),
	m_startingCash(0),
	m_log(nullptr),
	m_started(FALSE),
	m_finished(FALSE),
	m_failed(FALSE),
	m_aiPlayer(nullptr),
	m_askingForPlayer(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
AIMatch::~AIMatch()
{
	if (m_log) {
		fclose(m_log);
	}
}

//-------------------------------------------------------------------------------------------------
AIMatch *AIMatch::createFromCommandLine()
{
	// The options are read here and not with the other command line options, which both games share.
	AIMatch *match = nullptr;
	AsciiString players;
	for (Int i = 1; i < __argc; i++) {
		const char *arg = __argv[i];
		const char *value = (i + 1 < __argc) ? __argv[i + 1] : nullptr;
		if (value == nullptr) {
			continue;
		}
		if (stricmp(arg, "-aiMatch") == 0) {
			match = match ? match : new AIMatch;
			match->m_map = value;
		} else if (stricmp(arg, "-aiPlayers") == 0) {
			players = value;
		} else if (match && stricmp(arg, "-aiMatchLog") == 0) {
			match->m_logPath = value;
		} else if (match && stricmp(arg, "-aiMatchMinutes") == 0) {
			match->m_maxFrames = (UnsignedInt)(atof(value) * 60.0 * (Real)LOGICFRAMES_PER_SECOND);
		} else if (match && stricmp(arg, "-aiMatchInterval") == 0) {
			match->m_snapshotFrames = (UnsignedInt)(atof(value) * (Real)LOGICFRAMES_PER_SECOND);
		} else if (match && stricmp(arg, "-aiMatchCash") == 0) {
			match->m_startingCash = atoi(value);
		} else if (match && stricmp(arg, "-aiMatchScripts") == 0) {
			match->m_scriptFile = value;
		} else if (match && stricmp(arg, "-aiMatchAIData") == 0) {
			match->m_aiDataFile = value;
		}
	}
	if (match == nullptr) {
		return nullptr;
	}
	if (match->m_snapshotFrames < 1) {
		match->m_snapshotFrames = 1;
	}
	if (match->m_logPath.isEmpty()) {
		match->m_logPath = "AIMatch.jsonl";
	}

	// Faction:difficulty[:team], ...
	AsciiString entry;
	while (players.nextToken(&entry, ",")) {
		Entrant entrant;
		entrant.difficulty = SLOT_BRUTAL_AI;
		entrant.team = -1;
		AsciiString part;
		if (entry.nextToken(&part, ":")) {
			entrant.faction = part;
		}
		if (entry.nextToken(&part, ":")) {
			entrant.difficulty = slotStateFor(part);
		}
		if (entry.nextToken(&part, ":")) {
			entrant.team = atoi(part.str());
		}
		match->m_entrants.push_back(entrant);
	}
	return match;
}

//-------------------------------------------------------------------------------------------------
void AIMatch::fail(const char *reason)
{
	if (m_log == nullptr) {
		m_log = fopen(m_logPath.str(), "w");
	}
	if (m_log) {
		fprintf(m_log, "{\"t\":\"error\",\"reason\":%s}\n", quoted(reason).c_str());
		fflush(m_log);
	}
	m_finished = TRUE;
	m_failed = TRUE;
	TheGameEngine->setQuitting(TRUE);
}

//-------------------------------------------------------------------------------------------------
void AIMatch::start()
{
	m_log = fopen(m_logPath.str(), "w");
	if (m_log == nullptr) {
		m_finished = TRUE;
		m_failed = TRUE;
		TheGameEngine->setQuitting(TRUE);
		return;
	}

	// The map: its key in the map cache, or the first multiplayer map whose path holds the name.
	AsciiString mapKey = m_map;
	mapKey.toLower();
	if (TheMapCache->find(mapKey) == TheMapCache->end()) {
		AsciiString found;
		for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it) {
			if (it->second.m_isMultiplayer && strstr(it->first.str(), mapKey.str()) != nullptr) {
				found = it->first;
				break;
			}
		}
		if (found.isEmpty()) {
			AsciiString reason;
			reason.format("no multiplayer map matches '%s'", m_map.str());
			fail(reason.str());
			return;
		}
		mapKey = found;
	}
	const MapMetaData *mapData = TheMapCache->findMap(mapKey);

	// Files that stand in for the game's own, to try changes without installing them.
	if (m_scriptFile.isNotEmpty() && !TheFileSystem->doesFileExist(m_scriptFile.str())) {
		AsciiString reason;
		reason.format("script file '%s' not found", m_scriptFile.str());
		fail(reason.str());
		return;
	}
	if (m_aiDataFile.isNotEmpty()) {
		if (!TheFileSystem->doesFileExist(m_aiDataFile.str())) {
			AsciiString reason;
			reason.format("AIData file '%s' not found", m_aiDataFile.str());
			fail(reason.str());
			return;
		}
		// Read over what the game has loaded: its settings, side infos and build lists replace those of the same name.
		INI ini;
		ini.load(m_aiDataFile, INI_LOAD_OVERWRITE, nullptr);
	}

	if (m_entrants.size() < 2) {
		fail("-aiPlayers needs at least two players (Faction:difficulty,Faction:difficulty)");
		return;
	}
	if (mapData && (Int)m_entrants.size() > mapData->m_numPlayers) {
		AsciiString reason;
		reason.format("the map has %d start positions for %d players", mapData->m_numPlayers, (Int)m_entrants.size());
		fail(reason.str());
		return;
	}

	if (TheSkirmishGameInfo == nullptr) {
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	}
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	// The local player only watches.
	UnicodeString observerName;
	observerName.translate("Observer");
	GameSlot observer;
	observer.setState(SLOT_PLAYER, observerName);
	observer.setPlayerTemplate(PLAYERTEMPLATE_OBSERVER);
	TheSkirmishGameInfo->setSlot(0, observer);

	for (size_t i = 0; i < m_entrants.size(); i++) {
		const Entrant &entrant = m_entrants[i];
		Int templateIndex = -1;
		for (Int t = 0; t < ThePlayerTemplateStore->getPlayerTemplateCount(); t++) {
			const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(t);
			if (playerTemplate && (playerTemplate->getName().compareNoCase(entrant.faction.str()) == 0
					|| playerTemplate->getSide().compareNoCase(entrant.faction.str()) == 0)) {
				templateIndex = t;
				break;
			}
		}
		if (templateIndex < 0 || entrant.difficulty < 0) {
			AsciiString reason;
			reason.format("-aiPlayers entry %d: unknown faction '%s' or difficulty (easy, normal, hard)", (Int)i + 1, entrant.faction.str());
			fail(reason.str());
			return;
		}
		GameSlot slot;
		slot.setState((SlotState)entrant.difficulty);
		slot.setPlayerTemplate(templateIndex);
		slot.setTeamNumber(entrant.team);
		TheSkirmishGameInfo->setSlot((Int)i + 1, slot);
	}

	const Int seed = TheGlobalData->m_fixedSeed >= 0 ? TheGlobalData->m_fixedSeed : (Int)GetTickCount();
	TheSkirmishGameInfo->setSeed(seed);
	TheSkirmishGameInfo->setMap(mapKey);
	TheSkirmishGameInfo->setMapCRC(mapData ? mapData->m_CRC : 0);
	TheSkirmishGameInfo->setMapSize(mapData ? mapData->m_filesize : 0);
	if (m_startingCash > 0) {
		Money cash;
		cash.init();
		cash.deposit(m_startingCash, FALSE);
		TheSkirmishGameInfo->setStartingCash(cash);
	} else {
		TheSkirmishGameInfo->setStartingCash(TheGlobalData->m_defaultStartingCash);
	}

	fprintf(m_log, "{\"t\":\"setup\",\"map\":%s,\"seed\":%d,\"max_frames\":%u,\"snapshot_frames\":%u,\"frames_per_second\":%d,"
		"\"scripts\":%s,\"ai_data\":%s}\n",
		quoted(mapKey).c_str(), seed, m_maxFrames, m_snapshotFrames, (Int)LOGICFRAMES_PER_SECOND,
		quoted(m_scriptFile).c_str(), quoted(m_aiDataFile).c_str());
	fflush(m_log);

	// From here on as the skirmish menu starts a game.
	TheWritableGlobalData->m_mapName = mapKey;
	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheSkirmishGameInfo->startGame(0);
	InitRandom(seed);

	// Without the client nobody carries messages from the stream to the logic, so the message
	// goes to the logic's command list directly, as it does when a replay is simulated.
	GameMessage *msg;
	if (TheGlobalData->m_headless) {
		msg = newInstance(GameMessage)(GameMessage::MSG_NEW_GAME);
		TheCommandList->appendMessage(msg);
	} else {
		msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	}
	msg->appendIntegerArgument(GAME_SKIRMISH);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	msg->appendIntegerArgument(1000);	// no frame rate limit: the match runs as fast as the machine allows
}

//-------------------------------------------------------------------------------------------------
Int AIMatch::runHeadless()
{
	// Only the logic is updated; the match ends itself in update().
	UnsignedInt framesNotInGame = 0;
	while (!m_finished && !TheGameEngine->getQuitting()) {
		TheGameLogic->UPDATE();
		if (TheGameLogic->isInGame()) {
			framesNotInGame = 0;
		} else if (++framesNotInGame > 300) {
			fail(m_started ? "the game ended by itself" : "the game did not start");
		}
	}
	return m_failed ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------
std::vector<Player *> AIMatch::matchPlayers() const
{
	std::vector<Player *> players;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player == nullptr || TheGameInfo == nullptr) {
			continue;
		}
		const Int slotIndex = ThePlayerList->getSlotIndex(player->getPlayerIndex());
		const GameSlot *slot = slotIndex >= 0 ? TheGameInfo->getConstSlot(slotIndex) : nullptr;
		if (slot && slot->isAI()) {
			players.push_back(player);
		}
	}
	return players;
}

//-------------------------------------------------------------------------------------------------
void AIMatch::writeStart()
{
	const std::vector<Player *> players = matchPlayers();
	fprintf(m_log, "{\"t\":\"start\",\"frame\":%u,\"players\":[", TheGameLogic->getFrame());
	for (size_t i = 0; i < players.size(); i++) {
		Player *player = players[i];
		const Int slotIndex = ThePlayerList->getSlotIndex(player->getPlayerIndex());
		const GameSlot *slot = TheGameInfo->getConstSlot(slotIndex);
		fprintf(m_log, "%s{\"name\":%s,\"side\":%s,\"faction\":%s,\"difficulty\":\"%s\",\"team\":%d,\"start_position\":%d}",
			i ? "," : "", quoted(playerName(player).c_str()).c_str(), quoted(player->getSide()).c_str(),
			quoted(player->getPlayerTemplate() ? player->getPlayerTemplate()->getName() : AsciiString::TheEmptyString).c_str(),
			difficultyName(player->getPlayerDifficulty()), slot->getTeamNumber(), slot->getStartPos());
		m_state[player->getPlayerIndex()].alive = TRUE;
	}
	fprintf(m_log, "]}\n");
	fflush(m_log);
}

//-------------------------------------------------------------------------------------------------
void AIMatch::writePlayerScore(Player *player)
{
	ScoreKeeper *score = player->getScoreKeeper();
	fprintf(m_log, "\"score\":{\"units_built\":%d,\"units_lost\":%d,\"units_destroyed\":%d,\"buildings_built\":%d,"
		"\"buildings_lost\":%d,\"buildings_destroyed\":%d,\"money_earned\":%d,\"money_spent\":%d}",
		score->getTotalUnitsBuilt(), score->getTotalUnitsLost(), score->getTotalUnitsDestroyed(), score->getTotalBuildingsBuilt(),
		score->getTotalBuildingsLost(), score->getTotalBuildingsDestroyed(), score->getTotalMoneyEarned(), score->getTotalMoneySpent());
}

//-------------------------------------------------------------------------------------------------
void AIMatch::scanTeams()
{
	// A team exists from the moment the AI starts to build it.
	const std::vector<Player *> players = matchPlayers();
	for (size_t i = 0; i < players.size(); i++) {
		Player *player = players[i];
		PlayerState &state = m_state[player->getPlayerIndex()];
		const Player::PlayerTeamList *prototypes = player->getPlayerTeams();
		for (Player::PlayerTeamList::const_iterator it = prototypes->begin(); it != prototypes->end(); ++it) {
			for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance()) {
				if (state.teams.insert((UnsignedInt)iter.cur()->getID()).second && TheGameLogic->getFrame() > TEAM_SCAN_FRAMES) {
					fprintf(m_log, "{\"t\":\"team\",\"frame\":%u,\"player\":%s,\"team\":%s}\n", TheGameLogic->getFrame(),
						quoted(playerName(player).c_str()).c_str(), quoted((*it)->getName()).c_str());
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AIMatch::writeSnapshot()
{
	const std::vector<Player *> players = matchPlayers();
	const UnsignedInt frame = TheGameLogic->getFrame();

	// Objects per player and type.
	std::map<Int, std::map<std::string, Int> > objects;
	for (Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject()) {
		Player *owner = object->getControllingPlayer();
		if (owner && isArmyObject(object)) {
			objects[owner->getPlayerIndex()][object->getTemplate()->getName().str()]++;
		}
	}

	static std::vector<AsciiString> scienceNames;
	if (scienceNames.empty()) {
		scienceNames = TheScienceStore->friend_getScienceNames();
	}

	// What a player gained since the last snapshot comes first, as events.
	for (size_t i = 0; i < players.size(); i++) {
		Player *player = players[i];
		PlayerState &state = m_state[player->getPlayerIndex()];
		const std::string name = quoted(playerName(player).c_str());
		for (size_t s = 0; s < scienceNames.size(); s++) {
			if (player->hasScience(TheScienceStore->getScienceFromInternalName(scienceNames[s]))
					&& state.sciences.insert(scienceNames[s].str()).second && frame > m_snapshotFrames) {
				fprintf(m_log, "{\"t\":\"science\",\"frame\":%u,\"player\":%s,\"science\":%s}\n", frame, name.c_str(), quoted(scienceNames[s]).c_str());
			}
		}
		for (const UpgradeTemplate *upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade; upgrade = upgrade->friend_getNext()) {
			if (player->hasUpgradeComplete(upgrade) && state.upgrades.insert(upgrade->getUpgradeName().str()).second) {
				fprintf(m_log, "{\"t\":\"upgrade\",\"frame\":%u,\"player\":%s,\"upgrade\":%s}\n", frame, name.c_str(),
					quoted(upgrade->getUpgradeName()).c_str());
			}
		}
	}

	fprintf(m_log, "{\"t\":\"snap\",\"frame\":%u,\"players\":[", frame);
	for (size_t i = 0; i < players.size(); i++) {
		Player *player = players[i];
		PlayerState &state = m_state[player->getPlayerIndex()];
		std::map<std::string, Int> teams;
		const Player::PlayerTeamList *prototypes = player->getPlayerTeams();
		for (Player::PlayerTeamList::const_iterator it = prototypes->begin(); it != prototypes->end(); ++it) {
			const Int instances = (*it)->countTeamInstances();
			if (instances > 0) {
				teams[(*it)->getName().str()] = instances;
			}
		}
		fprintf(m_log, "%s{\"name\":%s,\"alive\":%s,\"money\":%u,\"power\":[%d,%d],\"rank\":%d,\"science_points\":%d,",
			i ? "," : "", quoted(playerName(player).c_str()).c_str(), state.alive ? "true" : "false", player->getMoney()->countMoney(),
			player->getEnergy()->getProduction(), player->getEnergy()->getConsumption(), player->getRankLevel(),
			player->getSciencePurchasePoints());
		writePlayerScore(player);
		fprintf(m_log, ",\"objects\":");
		writeCounts(m_log, objects[player->getPlayerIndex()]);
		fprintf(m_log, ",\"teams\":");
		writeCounts(m_log, teams);
		fputc('}', m_log);
	}
	fprintf(m_log, "]}\n");
	fflush(m_log);
}

//-------------------------------------------------------------------------------------------------
void AIMatch::finish(const char *reason)
{
	if (m_finished) {
		return;
	}
	m_finished = TRUE;
	const std::vector<Player *> players = matchPlayers();

	fprintf(m_log, "{\"t\":\"end\",\"frame\":%u,\"reason\":\"%s\",\"winners\":[", TheGameLogic->getFrame(), reason);
	Bool first = TRUE;
	for (size_t i = 0; i < players.size(); i++) {
		if (m_state[players[i]->getPlayerIndex()].alive && strcmp(reason, "victory") == 0) {
			fprintf(m_log, "%s%s", first ? "" : ",", quoted(playerName(players[i]).c_str()).c_str());
			first = FALSE;
		}
	}
	fprintf(m_log, "],\"players\":[");
	for (size_t i = 0; i < players.size(); i++) {
		Player *player = players[i];
		std::map<std::string, Int> built, lost;
		player->getScoreKeeper()->friend_countObjects(built, lost);
		fprintf(m_log, "%s{\"name\":%s,\"alive\":%s,", i ? "," : "", quoted(playerName(player).c_str()).c_str(),
			m_state[player->getPlayerIndex()].alive ? "true" : "false");
		writePlayerScore(player);
		fprintf(m_log, ",\"built\":");
		writeCounts(m_log, built);
		fprintf(m_log, ",\"lost\":");
		writeCounts(m_log, lost);
		fputc('}', m_log);
	}
	fprintf(m_log, "]}\n");

	// How often every script ran, and what was reported how often.
	for (std::map<std::string, std::map<std::string, ScriptUse> >::const_iterator owner = m_scripts.begin(); owner != m_scripts.end(); ++owner) {
		fprintf(m_log, "{\"t\":\"scripts\",\"player\":%s,\"scripts\":{", quoted(owner->first.c_str()).c_str());
		for (std::map<std::string, ScriptUse>::const_iterator it = owner->second.begin(); it != owner->second.end(); ++it) {
			fprintf(m_log, "%s%s:[%d,%u,%u]", it == owner->second.begin() ? "" : ",", quoted(it->first.c_str()).c_str(),
				it->second.count, it->second.first, it->second.last);
		}
		fprintf(m_log, "}}\n");
	}
	fprintf(m_log, "{\"t\":\"messages\",\"messages\":[");
	for (std::map<std::pair<std::string, std::string>, ScriptUse>::const_iterator it = m_messages.begin(); it != m_messages.end(); ++it) {
		fprintf(m_log, "%s{\"player\":%s,\"text\":%s,\"count\":%d,\"first\":%u,\"last\":%u}", it == m_messages.begin() ? "" : ",",
			quoted(it->first.first.c_str()).c_str(), quoted(it->first.second.c_str()).c_str(), it->second.count, it->second.first, it->second.last);
	}
	fprintf(m_log, "]}\n");
	fclose(m_log);
	m_log = nullptr;

	// Leave as the main menu's quit button does. Without the client there is nothing to tell.
	TheGameEngine->setQuitting(TRUE);
	if (TheGameLogic->isInGame() && !TheGlobalData->m_headless) {
		TheGameLogic->exitGame();
	}
}

//-------------------------------------------------------------------------------------------------
void AIMatch::update()
{
	if (m_finished || m_log == nullptr || !TheGameLogic->isInGame() || TheGameLogic->getGameMode() != GAME_SKIRMISH) {
		return;
	}
	const UnsignedInt frame = TheGameLogic->getFrame();
	if (!m_started) {
		if (frame < 2) {
			return;
		}
		m_started = TRUE;
		writeStart();
	}

	if (frame % TEAM_SCAN_FRAMES == 0) {
		scanTeams();
	}
	if (frame % m_snapshotFrames != 0 && frame < m_maxFrames) {
		return;
	}

	// A player is out when it has neither structures that count for victory nor units, as in multiplayer.
	KindOfMaskType victoryMask;
	victoryMask.set(KINDOF_MP_COUNT_FOR_VICTORY);
	const std::vector<Player *> players = matchPlayers();
	std::set<Int> sidesAlive;
	for (size_t i = 0; i < players.size(); i++) {
		Player *player = players[i];
		PlayerState &state = m_state[player->getPlayerIndex()];
		if (state.alive && frame > 10 * LOGICFRAMES_PER_SECOND && !player->hasAnyBuildings(victoryMask) && !player->hasAnyUnits()) {
			state.alive = FALSE;
			fprintf(m_log, "{\"t\":\"defeated\",\"frame\":%u,\"player\":%s}\n", frame, quoted(playerName(player).c_str()).c_str());
		}
		if (state.alive) {
			const GameSlot *slot = TheGameInfo->getConstSlot(ThePlayerList->getSlotIndex(player->getPlayerIndex()));
			// Players without a team stand alone.
			sidesAlive.insert(slot->getTeamNumber() >= 0 ? slot->getTeamNumber() : 100 + player->getPlayerIndex());
		}
	}
	writeSnapshot();

	if (sidesAlive.size() <= 1) {
		finish("victory");
	} else if (frame >= m_maxFrames) {
		finish("time_limit");
	}
}

//-------------------------------------------------------------------------------------------------
/// The player whose script is running, or null.
Player *AIMatch::scriptPlayer()
{
	// The script engine reports it when there is none, which would come straight back to scriptMessage.
	m_askingForPlayer = TRUE;
	Player *player = TheScriptEngine->getCurrentPlayer();
	m_askingForPlayer = FALSE;
	return player;
}

//-------------------------------------------------------------------------------------------------
void AIMatch::scriptFired(const AsciiString &scriptName, Bool conditionsTrue)
{
	if (m_finished || m_log == nullptr || !conditionsTrue || TheGameLogic->getGameMode() != GAME_SKIRMISH) {
		return;
	}
	Player *player = scriptPlayer();
	const std::string owner = player ? playerName(player) : std::string();
	std::map<std::string, ScriptUse> &scripts = m_scripts[owner];
	std::map<std::string, ScriptUse>::iterator it = scripts.find(scriptName.str());
	const UnsignedInt frame = TheGameLogic->getFrame();
	if (it == scripts.end()) {
		ScriptUse use;
		use.count = 1;
		use.first = use.last = frame;
		scripts[scriptName.str()] = use;
		// The first run of a script goes into the timeline.
		if (m_started) {
			fprintf(m_log, "{\"t\":\"script\",\"frame\":%u,\"player\":%s,\"script\":%s}\n", frame, quoted(owner.c_str()).c_str(), quoted(scriptName).c_str());
		}
	} else {
		it->second.count++;
		it->second.last = frame;
	}
}

//-------------------------------------------------------------------------------------------------
void AIMatch::scriptMessage(const AsciiString &text)
{
	if (!m_started || m_finished || m_askingForPlayer) {
		return;
	}
	const UnsignedInt frame = TheGameLogic->getFrame();
	// The AI of a player reports while it is being updated; scripts report for the player that owns them.
	Player *player = m_aiPlayer ? m_aiPlayer : scriptPlayer();
	const std::pair<std::string, std::string> key(player ? playerName(player) : std::string(), text.str());
	std::map<std::pair<std::string, std::string>, ScriptUse>::iterator it = m_messages.find(key);
	if (it == m_messages.end()) {
		ScriptUse use;
		use.count = 1;
		use.first = use.last = frame;
		m_messages[key] = use;
		fprintf(m_log, "{\"t\":\"message\",\"frame\":%u,\"player\":%s,\"text\":%s}\n", frame,
			quoted(key.first.c_str()).c_str(), quoted(text).c_str());
	} else {
		it->second.count++;
		it->second.last = frame;
	}
}
