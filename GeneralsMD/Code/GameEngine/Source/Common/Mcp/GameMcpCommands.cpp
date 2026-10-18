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

// GameMcpCommands.cpp
// Command table and shared helpers of the game's MCP bridge.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include <map>
#include <stdarg.h>

namespace
{

struct CommandEntry
{
	GameMcpHandler handler;
	std::string help;
};

typedef std::map<std::string, CommandEntry> CommandTable;

CommandTable &commandTable()
{
	static CommandTable table;
	return table;
}

McpJson cmdListCommands(const McpJson &)
{
	McpJson list = McpJson::makeArray();
	const CommandTable &table = commandTable();
	for (CommandTable::const_iterator it = table.begin(); it != table.end(); ++it) {
		list.push(McpJson::makeObject().set("name", it->first).set("help", it->second.help));
	}
	return McpJson::makeObject().set("commands", list);
}

/// Where the game runs, so that clients can find its files.
McpJson cmdAppInfo(const McpJson &)
{
	char exePath[MAX_PATH];
	exePath[0] = 0;
	::GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
	std::string exeDir = exePath;
	const size_t slash = exeDir.find_last_of("\\/");
	if (slash != std::string::npos) exeDir.erase(slash);
	char workDir[MAX_PATH];
	workDir[0] = 0;
	::GetCurrentDirectoryA(sizeof(workDir), workDir);
	return McpJson::makeObject().set("exe_path", exePath).set("exe_dir", exeDir).set("working_dir", workDir)
		.set("process_id", (unsigned int)::GetCurrentProcessId());
}

void registerAllCommands()
{
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;
	gameMcpRegisterCommand("list_commands", cmdListCommands, "Lists all bridge commands.");
	gameMcpRegisterCommand("app.info", cmdAppInfo, "Path and folder of the game executable, its working directory and process id.");
	gameMcpRegisterStateCommands();
	gameMcpRegisterObjectCommands();
	gameMcpRegisterPlayerCommands();
	gameMcpRegisterOrderCommands();
	gameMcpRegisterCheatCommands();
	gameMcpRegisterScriptCommands();
	gameMcpRegisterViewCommands();
}

} // namespace

//-------------------------------------------------------------------------------------------------
void gameMcpRegisterCommand(const char *name, GameMcpHandler handler, const char *help)
{
	CommandEntry entry;
	entry.handler = handler;
	entry.help = help ? help : "";
	commandTable()[name] = entry;
}

//-------------------------------------------------------------------------------------------------
std::string gameMcpDispatch(const std::string &requestLine)
{
	registerAllCommands();

	McpJson reply = McpJson::makeObject();
	McpJson request;
	std::string parseError;
	if (!McpJson::parse(requestLine, request, parseError) || !request.isObject()) {
		reply.set("id", McpJson()).set("ok", false).set("error", "invalid request: " + parseError);
		return reply.dump();
	}
	reply.set("id", request.get("id"));

	const std::string cmd = request.get("cmd").asString();
	const CommandTable &table = commandTable();
	CommandTable::const_iterator it = table.find(cmd);
	if (it == table.end()) {
		reply.set("ok", false).set("error", "unknown command '" + cmd + "' (see list_commands)");
		return reply.dump();
	}

	McpJson args = request.get("args");
	if (args.isNull()) {
		args = McpJson::makeObject();
	}
	try {
		McpJson result = it->second.handler(args);
		reply.set("ok", true).set("result", result);
	} catch (const GameMcpError &e) {
		reply.set("ok", false).set("error", e.message);
	}
	return reply.dump();
}

//-------------------------------------------------------------------------------------------------
void gameMcpFail(const char *fmt, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	buffer[sizeof(buffer) - 1] = 0;
	GameMcpError error;
	error.message = buffer;
	throw error;
}

//-------------------------------------------------------------------------------------------------
double gameMcpArgNumber(const McpJson &args, const char *key)
{
	const McpJson &value = args.get(key);
	if (!value.isNumber()) {
		gameMcpFail("argument '%s' must be a number", key);
	}
	return value.asNumber();
}

double gameMcpArgNumber(const McpJson &args, const char *key, double defaultValue)
{
	return args.has(key) && !args.get(key).isNull() ? gameMcpArgNumber(args, key) : defaultValue;
}

int gameMcpArgInt(const McpJson &args, const char *key)
{
	return (int)floor(gameMcpArgNumber(args, key) + 0.5);
}

int gameMcpArgInt(const McpJson &args, const char *key, int defaultValue)
{
	return args.has(key) && !args.get(key).isNull() ? gameMcpArgInt(args, key) : defaultValue;
}

bool gameMcpArgBool(const McpJson &args, const char *key, bool defaultValue)
{
	const McpJson &value = args.get(key);
	if (value.isNull()) {
		return defaultValue;
	}
	if (!value.isBool()) {
		gameMcpFail("argument '%s' must be true or false", key);
	}
	return value.asBool();
}

std::string gameMcpArgString(const McpJson &args, const char *key)
{
	const McpJson &value = args.get(key);
	if (!value.isString()) {
		gameMcpFail("argument '%s' must be a string", key);
	}
	return value.asString();
}

std::string gameMcpArgString(const McpJson &args, const char *key, const char *defaultValue)
{
	return args.has(key) && !args.get(key).isNull() ? gameMcpArgString(args, key) : std::string(defaultValue);
}

//-------------------------------------------------------------------------------------------------
void gameMcpRequireGame()
{
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame()) {
		gameMcpFail("no game is running (start one with -mcpGame, or wait until it has loaded; see game.status)");
	}
}

//-------------------------------------------------------------------------------------------------
Object *gameMcpRequireObject(int id)
{
	gameMcpRequireGame();
	Object *object = TheGameLogic->findObjectByID((ObjectID)id);
	if (object == nullptr) {
		gameMcpFail("no object with id %d (it may have been destroyed; see objects.list)", id);
	}
	return object;
}

//-------------------------------------------------------------------------------------------------
void gameMcpRequireObjects(const McpJson &args, std::vector<Object *> &objects)
{
	objects.clear();
	const McpJson &ids = args.get("ids");
	if (ids.isArray()) {
		for (size_t i = 0; i < ids.size(); i++) {
			if (!ids.at(i).isNumber()) {
				gameMcpFail("'ids' must hold object ids (numbers)");
			}
			objects.push_back(gameMcpRequireObject((int)ids.at(i).asNumber()));
		}
	} else if (ids.isNumber()) {
		objects.push_back(gameMcpRequireObject((int)ids.asNumber()));
	} else if (args.get("id").isNumber()) {
		objects.push_back(gameMcpRequireObject(gameMcpArgInt(args, "id")));
	}
	if (objects.empty()) {
		gameMcpFail("give the objects as 'ids' (a list of object ids) or 'id'");
	}
}

//-------------------------------------------------------------------------------------------------
std::string gameMcpPlayerName(const Player *player)
{
	if (player == nullptr) {
		return std::string();
	}
	return TheNameKeyGenerator->keyToName(player->getPlayerNameKey()).str();
}

//-------------------------------------------------------------------------------------------------
Player *gameMcpRequirePlayer(const std::string &name)
{
	gameMcpRequireGame();
	if (name.empty() || _stricmp(name.c_str(), "local") == 0) {
		return ThePlayerList->getLocalPlayer();
	}
	if (_stricmp(name.c_str(), "neutral") == 0) {
		return ThePlayerList->getNeutralPlayer();
	}
	std::string names;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player == nullptr) {
			continue;
		}
		const std::string internal = gameMcpPlayerName(player);
		if (_stricmp(internal.c_str(), name.c_str()) == 0 || _stricmp(gameMcpUtf8(player->getPlayerDisplayName()).c_str(), name.c_str()) == 0) {
			return player;
		}
		names += names.empty() ? "" : ", ";
		names += internal;
	}
	char *end = nullptr;
	const long index = strtol(name.c_str(), &end, 10);
	if (end != name.c_str() && *end == 0 && index >= 0 && index < ThePlayerList->getPlayerCount()) {
		return ThePlayerList->getNthPlayer((Int)index);
	}
	gameMcpFail("unknown player '%s' (players: %s; or 'local', 'neutral', an index)", name.c_str(), names.c_str());
}

//-------------------------------------------------------------------------------------------------
Player *gameMcpPlayerArg(const McpJson &args, const char *key)
{
	return gameMcpRequirePlayer(gameMcpArgString(args, key, ""));
}

//-------------------------------------------------------------------------------------------------
const ThingTemplate *gameMcpRequireTemplate(const std::string &name)
{
	const ThingTemplate *tmpl = TheThingFactory->findTemplate(name.c_str());
	if (tmpl == nullptr) {
		std::string similar;
		Int count = 0;
		for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t && count < 10; t = t->friend_getNextTemplate()) {
			if (gameMcpContainsNoCase(t->getName().str(), name.c_str())) {
				similar += similar.empty() ? "" : ", ";
				similar += t->getName().str();
				count++;
			}
		}
		gameMcpFail("unknown object template '%s'%s%s", name.c_str(), similar.empty() ? "" : "; similar: ", similar.c_str());
	}
	return tmpl;
}

//-------------------------------------------------------------------------------------------------
bool gameMcpContainsNoCase(const char *haystack, const char *needle)
{
	if (needle == nullptr || *needle == 0) {
		return true;
	}
	if (haystack == nullptr) {
		return false;
	}
	const size_t needleLen = strlen(needle);
	for (const char *h = haystack; *h; h++) {
		if (_strnicmp(h, needle, needleLen) == 0) {
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
std::string gameMcpUtf8(const UnicodeString &text)
{
	const WideChar *wide = text.str();
	if (wide == nullptr || *wide == 0) {
		return std::string();
	}
	const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (bytes <= 1) {
		return std::string();
	}
	std::string out(bytes - 1, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], bytes, nullptr, nullptr);
	return out;
}

//-------------------------------------------------------------------------------------------------
McpJson gameMcpCoord(const Coord3D &pos)
{
	return McpJson::makeObject().set("x", pos.x).set("y", pos.y).set("z", pos.z);
}

//-------------------------------------------------------------------------------------------------
Coord3D gameMcpArgPosition(const McpJson &args)
{
	Coord3D pos;
	pos.x = (Real)gameMcpArgNumber(args, "x");
	pos.y = (Real)gameMcpArgNumber(args, "y");
	pos.z = args.has("z") ? (Real)gameMcpArgNumber(args, "z") : TheTerrainLogic->getGroundHeight(pos.x, pos.y);
	return pos;
}

#endif // RTS_GAME_MCP
