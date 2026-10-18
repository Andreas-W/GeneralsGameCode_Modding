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

// GameMcpCmdState.cpp
// Game state commands of the MCP bridge: status, pause, frame steps, speed, messages, quit.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/FramePacer.h"
#include "Common/GameEngine.h"
#include "Common/GameMcp.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"

namespace
{

const char *gameModeName(Int mode)
{
	switch (mode) {
		case GAME_SINGLE_PLAYER: return "single_player";
		case GAME_LAN: return "lan";
		case GAME_SKIRMISH: return "skirmish";
		case GAME_REPLAY: return "replay";
		case GAME_SHELL: return "shell";
		case GAME_INTERNET: return "internet";
		default: return "none";
	}
}

void requirePausable()
{
	gameMcpRequireGame();
	if (TheGameLogic->isInMultiplayerGame()) {
		gameMcpFail("a multiplayer game cannot be paused");
	}
}

McpJson cmdStatus(const McpJson &)
{
	McpJson result = McpJson::makeObject();
	const Bool inGame = TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
	result.set("in_game", inGame != FALSE);
	result.set("loading", TheGameLogic->isLoadingMap() != FALSE);
	result.set("mode", gameModeName(TheGameLogic->getGameMode()));
	result.set("map", TheGlobalData->m_mapName.str());
	result.set("frame", TheGameLogic->getFrame());
	result.set("seconds", (double)TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND);
	result.set("frames_per_second", (int)LOGICFRAMES_PER_SECOND);
	result.set("paused", TheGameLogic->isGamePaused() != FALSE);
	result.set("time_frozen_by_script", TheScriptEngine && TheScriptEngine->isTimeFrozenScript());
	result.set("headless", TheGlobalData->m_headless != FALSE);
	result.set("sandbox", TheGameMcp->isSandbox() != FALSE);
	if (TheGlobalData->m_headless) {
		result.set("speed_fps", TheGameMcp->getHeadlessFps());
	} else {
		result.set("speed_fps", TheFramePacer->isFramesPerSecondLimitEnabled() ? TheFramePacer->getFramesPerSecondLimit() : 0);
	}
	result.set("object_count", (unsigned int)(inGame ? TheGameLogic->getObjectCount() : 0));
	if (inGame && ThePlayerList->getLocalPlayer()) {
		result.set("local_player", gameMcpPlayerName(ThePlayerList->getLocalPlayer()));
	}
	if (gameMcpStartError().isNotEmpty()) {
		result.set("start_error", gameMcpStartError().str());
	}
	return result;
}

McpJson cmdPause(const McpJson &)
{
	requirePausable();
	TheGameLogic->setGamePaused(TRUE, FALSE, FALSE);
	return McpJson::makeObject().set("paused", true).set("frame", TheGameLogic->getFrame());
}

McpJson cmdResume(const McpJson &)
{
	requirePausable();
	TheGameLogic->setGamePaused(FALSE, FALSE, FALSE);
	return McpJson::makeObject().set("paused", false).set("frame", TheGameLogic->getFrame());
}

McpJson cmdStep(const McpJson &args)
{
	requirePausable();
	const Int frames = gameMcpArgInt(args, "frames", 1);
	if (frames < 1) {
		gameMcpFail("frames must be at least 1");
	}
	const UnsignedInt target = TheGameLogic->getFrame() + frames;
	TheGameLogic->setGamePaused(FALSE, FALSE, FALSE);
	TheGameLogic->setGamePausedInFrame(target, FALSE);
	return McpJson::makeObject().set("frame", TheGameLogic->getFrame()).set("pause_at_frame", target);
}

McpJson cmdSetSpeed(const McpJson &args)
{
	// Logic frames run at most once per render frame, so the frame rate limit is the game speed.
	const Int fps = gameMcpArgInt(args, "fps");
	if (fps < 0) {
		gameMcpFail("fps must be 0 (as fast as possible) or more");
	}
	if (TheGlobalData->m_headless) {
		TheGameMcp->setHeadlessFps(fps);
	} else {
		TheFramePacer->enableLogicTimeScale(FALSE);
		if (fps == 0) {
			TheFramePacer->enableFramesPerSecondLimit(FALSE);
		} else {
			TheFramePacer->enableFramesPerSecondLimit(TRUE);
			TheFramePacer->setFramesPerSecondLimit(fps);
		}
	}
	return McpJson::makeObject().set("speed_fps", fps).set("normal_fps", (int)LOGICFRAMES_PER_SECOND);
}

McpJson cmdMessages(const McpJson &args)
{
	const UnsignedInt since = (UnsignedInt)gameMcpArgInt(args, "since", 0);
	const std::string contains = gameMcpArgString(args, "contains", "");
	const Int limit = gameMcpArgInt(args, "limit", 200);
	McpJson list = McpJson::makeArray();
	const std::deque<GameMcp::Message> &messages = TheGameMcp->getMessages();
	for (std::deque<GameMcp::Message>::const_iterator it = messages.begin(); it != messages.end(); ++it) {
		if (it->serial <= since || !gameMcpContainsNoCase(it->text.c_str(), contains.c_str())) {
			continue;
		}
		list.push(McpJson::makeObject().set("serial", it->serial).set("frame", it->frame).set("text", it->text));
		if ((Int)list.size() >= limit) {
			break;
		}
	}
	return McpJson::makeObject().set("messages", list).set("next_serial", TheGameMcp->getNextMessageSerial());
}

McpJson cmdQuit(const McpJson &)
{
	// Leave as the main menu's quit button does. Without the client there is nothing to tell.
	TheGameEngine->setQuitting(TRUE);
	if (TheGameLogic->isInGame() && !TheGlobalData->m_headless) {
		TheGameLogic->exitGame();
	}
	return McpJson::makeObject().set("quitting", true);
}

} // namespace

void gameMcpRegisterStateCommands()
{
	gameMcpRegisterCommand("game.status", cmdStatus,
		"In game, mode, map, frame (30 per second), paused, speed, headless, sandbox, local player, start_error.");
	gameMcpRegisterCommand("game.pause", cmdPause, "Pauses the game logic (the window keeps drawing).");
	gameMcpRegisterCommand("game.resume", cmdResume, "Lets the game logic run again.");
	gameMcpRegisterCommand("game.step", cmdStep, "{frames=1} Runs that many logic frames, then pauses.");
	gameMcpRegisterCommand("game.set_speed", cmdSetSpeed, "{fps} Logic frames per second: 30 is normal speed, 0 as fast as possible.");
	gameMcpRegisterCommand("game.messages", cmdMessages, "{since?, contains?, limit?} What the script engine and the AI reported (serial > since).");
	gameMcpRegisterCommand("game.quit", cmdQuit, "Ends the game and closes it.");
}

#endif // RTS_GAME_MCP
