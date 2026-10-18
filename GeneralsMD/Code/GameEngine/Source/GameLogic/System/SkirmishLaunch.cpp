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

// SkirmishLaunch.cpp
// Starts a skirmish without the skirmish menu.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/SkirmishLaunch.h"

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

//-------------------------------------------------------------------------------------------------
AsciiString SkirmishLaunch::findMap(const AsciiString &name)
{
	// The map: its key in the map cache, or the first multiplayer map whose path holds the name.
	AsciiString mapKey = name;
	mapKey.toLower();
	if (TheMapCache->find(mapKey) != TheMapCache->end()) {
		return mapKey;
	}
	for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it) {
		if (it->second.m_isMultiplayer && strstr(it->first.str(), mapKey.str()) != nullptr) {
			return it->first;
		}
	}
	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
Int SkirmishLaunch::countStartPositions(const AsciiString &mapKey)
{
	const MapMetaData *mapData = TheMapCache->findMap(mapKey);
	return mapData ? mapData->m_numPlayers : -1;
}

//-------------------------------------------------------------------------------------------------
Int SkirmishLaunch::slotStateForDifficulty(const AsciiString &difficulty)
{
	if (difficulty.compareNoCase("easy") == 0) return SLOT_EASY_AI;
	if (difficulty.compareNoCase("normal") == 0 || difficulty.compareNoCase("medium") == 0) return SLOT_MED_AI;
	if (difficulty.compareNoCase("hard") == 0 || difficulty.compareNoCase("brutal") == 0) return SLOT_BRUTAL_AI;
	return -1;
}

//-------------------------------------------------------------------------------------------------
static Int findPlayerTemplate(const AsciiString &faction)
{
	for (Int t = 0; t < ThePlayerTemplateStore->getPlayerTemplateCount(); t++) {
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(t);
		if (playerTemplate && (playerTemplate->getName().compareNoCase(faction.str()) == 0
				|| playerTemplate->getSide().compareNoCase(faction.str()) == 0)) {
			return t;
		}
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
static Bool isObserverFaction(const AsciiString &faction)
{
	return faction.isEmpty() || faction.compareNoCase("Observer") == 0;
}

//-------------------------------------------------------------------------------------------------
Bool SkirmishLaunch::prepare(const AsciiString &mapKey, const std::vector<SkirmishSlot> &slots, Int seed, Int startingCash, AsciiString &error)
{
	const MapMetaData *mapData = TheMapCache->findMap(mapKey);

	if (TheSkirmishGameInfo == nullptr) {
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	}
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	Int entry = 0;
	for (size_t i = 0; i < slots.size(); i++) {
		const SkirmishSlot &wanted = slots[i];
		GameSlot slot;
		if (i == 0) {
			// The local player plays a faction or only watches.
			UnicodeString name;
			if (isObserverFaction(wanted.faction)) {
				name.translate("Observer");
				slot.setState(SLOT_PLAYER, name);
				slot.setPlayerTemplate(PLAYERTEMPLATE_OBSERVER);
				TheSkirmishGameInfo->setSlot(0, slot);
				continue;
			}
			name.translate("Player");
			slot.setState(SLOT_PLAYER, name);
		} else {
			slot.setState((SlotState)wanted.slotState);
		}
		entry++;
		const Int templateIndex = findPlayerTemplate(wanted.faction);
		if (templateIndex < 0 || wanted.slotState < 0) {
			error.format("entry %d: unknown faction '%s' or difficulty (easy, normal, hard)", entry, wanted.faction.str());
			return FALSE;
		}
		slot.setPlayerTemplate(templateIndex);
		slot.setTeamNumber(wanted.team);
		TheSkirmishGameInfo->setSlot((Int)i, slot);
	}

	TheSkirmishGameInfo->setSeed(seed);
	TheSkirmishGameInfo->setMap(mapKey);
	TheSkirmishGameInfo->setMapCRC(mapData ? mapData->m_CRC : 0);
	TheSkirmishGameInfo->setMapSize(mapData ? mapData->m_filesize : 0);
	if (startingCash > 0) {
		Money cash;
		cash.init();
		cash.deposit(startingCash, FALSE);
		TheSkirmishGameInfo->setStartingCash(cash);
	} else {
		TheSkirmishGameInfo->setStartingCash(TheGlobalData->m_defaultStartingCash);
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void SkirmishLaunch::launch(const AsciiString &mapKey, Int seed, Int maxFps)
{
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
	msg->appendIntegerArgument(maxFps);
}
