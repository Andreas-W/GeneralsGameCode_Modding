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

// GameMcp.cpp
// The game's MCP bridge: command line, game start and the main thread side.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "Common/GameMcp.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "GameLogic/AIMatch.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SkirmishLaunch.h"
#include "GameNetwork/GameInfo.h"

#include "GameMcpCommands.h"

#include <stdlib.h>

GameMcp *TheGameMcp = nullptr;
GameMcpScreenshotFunc TheGameMcpScreenshot = nullptr;
GameMcpPostProcess TheGameMcpPostProcess = { nullptr, nullptr, nullptr, nullptr, nullptr };
GameMcpShadowMap TheGameMcpShadowMap = { nullptr, nullptr, nullptr };
GameMcpMaterials TheGameMcpMaterials = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

namespace
{

const size_t MAX_KEPT_MESSAGES = 1000;

/// Faction[:team] of the local player.
SkirmishSlot localSlot(const AsciiString &entry)
{
	SkirmishSlot slot;
	slot.slotState = SLOT_PLAYER;
	slot.team = -1;
	AsciiString rest = entry;
	AsciiString part;
	if (rest.nextToken(&part, ":")) {
		slot.faction = part;
	}
	if (rest.nextToken(&part, ":")) {
		slot.team = atoi(part.str());
	}
	return slot;
}

/// Faction:difficulty[:team] of an AI player.
SkirmishSlot aiSlot(const AsciiString &entry)
{
	SkirmishSlot slot;
	slot.slotState = SLOT_BRUTAL_AI;
	slot.team = -1;
	AsciiString rest = entry;
	AsciiString part;
	if (rest.nextToken(&part, ":")) {
		slot.faction = part;
	}
	if (rest.nextToken(&part, ":")) {
		slot.slotState = SkirmishLaunch::slotStateForDifficulty(part);
	}
	if (rest.nextToken(&part, ":")) {
		slot.team = atoi(part.str());
	}
	return slot;
}

AsciiString s_players;
Int s_startingCash = 0;
AsciiString s_startError;

} // namespace

//-------------------------------------------------------------------------------------------------
GameMcp::GameMcp() :
	m_port(0),
	m_sandbox(FALSE),
	m_pauseAtStart(FALSE),
	m_headlessFps(0),
	m_nextMessageSerial(1)
{
}

//-------------------------------------------------------------------------------------------------
GameMcp::~GameMcp()
{
	gameMcpServerStop();
}

//-------------------------------------------------------------------------------------------------
GameMcp *GameMcp::createFromCommandLine()
{
	// The options are read here and not with the other command line options, which both games share.
	Int port = 0;
	AsciiString map;
	Bool pauseAtStart = FALSE;
	for (Int i = 1; i < __argc; i++) {
		const char *arg = __argv[i];
		const char *value = (i + 1 < __argc) ? __argv[i + 1] : nullptr;
		if (stricmp(arg, "-mcp") == 0) {
			if (port == 0) {
				port = DEFAULT_PORT;
			}
		} else if (strnicmp(arg, "-mcpport:", 9) == 0 || strnicmp(arg, "-mcpport=", 9) == 0) {
			port = atoi(arg + 9);
		} else if (stricmp(arg, "-mcpStartPaused") == 0) {
			pauseAtStart = TRUE;
		} else if (value && stricmp(arg, "-mcpGame") == 0) {
			map = value;
		} else if (value && stricmp(arg, "-mcpPlayers") == 0) {
			s_players = value;
		} else if (value && stricmp(arg, "-mcpCash") == 0) {
			s_startingCash = atoi(value);
		}
	}
	if (port <= 0 || port > 65535) {
		return nullptr;
	}
	if (!gameMcpServerStart(port)) {
		return nullptr;
	}
	GameMcp *mcp = new GameMcp;
	mcp->m_port = port;
	mcp->m_map = map;
	mcp->m_pauseAtStart = pauseAtStart;
	return mcp;
}

//-------------------------------------------------------------------------------------------------
void GameMcp::startGameFromCommandLine()
{
	// An AI match starts its own game.
	if (m_map.isEmpty() || TheAIMatch != nullptr) {
		return;
	}

	const AsciiString mapKey = SkirmishLaunch::findMap(m_map);
	if (mapKey.isEmpty()) {
		s_startError.format("-mcpGame: no multiplayer map matches '%s'", m_map.str());
		DEBUG_LOG(("Game MCP bridge: %s", s_startError.str()));
		return;
	}

	// Faction of the local player first, then the AI players.
	std::vector<SkirmishSlot> slots;
	AsciiString players = s_players;
	AsciiString entry;
	while (players.nextToken(&entry, ",")) {
		slots.push_back(slots.empty() ? localSlot(entry) : aiSlot(entry));
	}
	if (slots.empty()) {
		s_startError = "-mcpPlayers needs the faction of the local player, e.g. FactionAmerica or FactionAmerica,FactionChina:hard";
		DEBUG_LOG(("Game MCP bridge: %s", s_startError.str()));
		return;
	}
	const Int startPositions = SkirmishLaunch::countStartPositions(mapKey);
	if (startPositions >= 0 && (Int)slots.size() > startPositions) {
		s_startError.format("-mcpGame: the map has %d start positions for %d players", startPositions, (Int)slots.size());
		DEBUG_LOG(("Game MCP bridge: %s", s_startError.str()));
		return;
	}

	const Int seed = TheGlobalData->m_fixedSeed >= 0 ? TheGlobalData->m_fixedSeed : (Int)GetTickCount();
	AsciiString error;
	if (!SkirmishLaunch::prepare(mapKey, slots, seed, s_startingCash, error)) {
		s_startError.format("-mcpPlayers %s", error.str());
		DEBUG_LOG(("Game MCP bridge: %s", s_startError.str()));
		return;
	}
	m_map = mapKey;
	m_sandbox = slots.size() == 1;
	SkirmishLaunch::launch(mapKey, seed, TheGlobalData->m_framesPerSecondLimit);
}

//-------------------------------------------------------------------------------------------------
const AsciiString &gameMcpStartError()
{
	return s_startError;
}

//-------------------------------------------------------------------------------------------------
void GameMcp::poll()
{
	gameMcpServerDrain();

	if (m_pauseAtStart && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && TheGameLogic->getFrame() >= 1) {
		m_pauseAtStart = FALSE;
		TheGameLogic->setGamePaused(TRUE, FALSE, FALSE);
	}
}

//-------------------------------------------------------------------------------------------------
Int GameMcp::runHeadless()
{
	// As AIMatch::runHeadless, but the logic waits while the game is paused and the commands are run in between.
	DWORD nextFrameTime = timeGetTime();
	while (!TheGameEngine->getQuitting()) {
		poll();
		TheGameLogic->preUpdate();
		if (TheGameLogic->isGamePaused()) {
			::Sleep(1);
			continue;
		}
		if (m_headlessFps > 0) {
			const DWORD now = timeGetTime();
			if ((Int)(nextFrameTime - now) > 0) {
				::Sleep(1);
				continue;
			}
			nextFrameTime = max(nextFrameTime + 1000 / m_headlessFps, now);
		}
		TheGameLogic->UPDATE();
		if (!TheGameLogic->isInGame()) {
			::Sleep(1);
		}
	}
	return 0;
}

//-------------------------------------------------------------------------------------------------
void GameMcp::scriptMessage(const AsciiString &text)
{
	Message message;
	message.serial = m_nextMessageSerial++;
	message.frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	message.text = text.str();
	m_messages.push_back(message);
	while (m_messages.size() > MAX_KEPT_MESSAGES) {
		m_messages.pop_front();
	}
}

#endif // RTS_GAME_MCP
