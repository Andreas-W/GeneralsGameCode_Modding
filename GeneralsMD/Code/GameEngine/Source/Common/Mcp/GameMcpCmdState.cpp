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
// Game state commands of the MCP bridge: status, pause, frame steps, speed, messages, save, load, quit.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/FramePacer.h"
#include "Common/GameEngine.h"
#include "Common/GameMcp.h"
#include "Common/GameState.h"
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
		result.set("render_fps", (double)TheFramePacer->getUpdateFps());
		result.set("render_fps_limit", TheFramePacer->getRenderFramesPerSecondLimit());
		result.set("render_interpolation", TheGlobalData->m_useRenderInterpolation != FALSE);
		result.set("render_interpolation_factor", (double)TheFramePacer->getLogicInterpolation());
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
	// With render_fps the logic runs at its own rate and the window renders at another, as in network games.
	const Int renderFps = gameMcpArgInt(args, "render_fps", -1);
	if (TheGlobalData->m_headless) {
		TheGameMcp->setHeadlessFps(fps);
	} else if (renderFps >= 0) {
		if (fps == 0) {
			gameMcpFail("fps must be more than 0 when render_fps is given");
		}
		TheFramePacer->setLogicTimeScaleFps(fps);
		TheFramePacer->enableLogicTimeScale(TRUE);
		if (renderFps == 0) {
			TheFramePacer->enableFramesPerSecondLimit(FALSE);
		} else {
			TheFramePacer->enableFramesPerSecondLimit(TRUE);
			TheFramePacer->setFramesPerSecondLimit(renderFps);
		}
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

const char *saveCodeName(SaveCode code)
{
	switch (code) {
		case SC_OK: return "ok";
		case SC_NO_FILE_AVAILABLE: return "no file name available";
		case SC_FILE_NOT_FOUND: return "file not found";
		case SC_UNABLE_TO_OPEN_FILE: return "unable to open file";
		case SC_INVALID_XFER: return "invalid xfer";
		case SC_UNKNOWN_BLOCK: return "unknown block";
		case SC_INVALID_DATA: return "invalid data";
		case SC_ERROR: return "error";
		default: return "invalid";
	}
}

/// A save file name is a leaf in the save directory: no folders, and .sav is added when missing.
AsciiString saveFileArg(const McpJson &args)
{
	AsciiString filename = gameMcpArgString(args, "filename", "").c_str();
	if (filename.isEmpty()) {
		return filename;
	}
	if (strpbrk(filename.str(), "/\\:") != nullptr) {
		gameMcpFail("filename must be a plain file name in the save directory, without folders");
	}
	if (!filename.endsWithNoCase(".sav")) {
		filename.concat(".sav");
	}
	return filename;
}

/// Save files also hold the client side (terrain visuals, particles), which a headless game does not have.
void requireWindow()
{
	if (TheGlobalData->m_headless) {
		gameMcpFail("saving and loading need the window; launch the game without headless");
	}
}

McpJson cmdSave(const McpJson &args)
{
	requirePausable();
	requireWindow();
	const AsciiString filename = saveFileArg(args);
	UnicodeString desc;
	desc.translate(AsciiString(gameMcpArgString(args, "description", "MCP save").c_str()));
	const SaveResult result = TheGameState->saveGame(filename, desc, SAVE_FILE_TYPE_NORMAL);
	if (result.saveCode != SC_OK) {
		gameMcpFail("saving failed: %s", saveCodeName(result.saveCode));
	}
	return McpJson::makeObject().set("filename", result.filename.str())
		.set("path", TheGameState->getFilePathInSaveDirectory(result.filename).str()).set("frame", TheGameLogic->getFrame());
}

McpJson cmdLoad(const McpJson &args)
{
	gameMcpRequireGame();
	requireWindow();
	if (TheGameLogic->isInMultiplayerGame()) {
		gameMcpFail("a multiplayer game cannot be loaded over");
	}
	AvailableGameInfo info;
	info.filename = saveFileArg(args);
	info.next = info.prev = nullptr;
	if (info.filename.isEmpty()) {
		gameMcpFail("filename is required");
	}
	if (!TheGameState->doesSaveGameExist(info.filename)) {
		gameMcpFail("no save file %s in %s", info.filename.str(), TheGameState->getSaveDirectory().str());
	}
	// Unlike loadGame, this reads from a full path, and it throws on a broken file.
	Bool readable = TRUE;
	try {
		TheGameState->getSaveGameInfoFromFile(TheGameState->getFilePathInSaveDirectory(info.filename), &info.saveGameInfo);
	} catch (...) {
		readable = FALSE;
	}
	if (!readable) {
		gameMcpFail("save file %s could not be read", info.filename.str());
	}

	// Same as loading from the in-game menu: the engine is reset and the saved game replaces this one.
	const SaveCode code = TheGameState->loadGame(info);
	if (code != SC_OK) {
		gameMcpFail("loading failed: %s", saveCodeName(code));
	}
	if (gameMcpArgBool(args, "paused", true)) {
		TheGameLogic->setGamePaused(TRUE, FALSE, FALSE);
	}
	return McpJson::makeObject().set("filename", info.filename.str()).set("frame", TheGameLogic->getFrame())
		.set("paused", TheGameLogic->isGamePaused() != FALSE);
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
	gameMcpRegisterCommand("game.save", cmdSave,
		"{filename?, description?} Saves the game in the save directory, windowed only (filename is a plain name, .sav is added; none: a new numbered file).");
	gameMcpRegisterCommand("game.load", cmdLoad,
		"{filename, paused=true} Loads a save from the save directory as the in-game load menu does (windowed only), then pauses.");
	gameMcpRegisterCommand("game.quit", cmdQuit, "Ends the game and closes it.");
}

#endif // RTS_GAME_MCP
