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

// GameMcpCommands.h
// Command table, transport and shared helpers of the game's MCP bridge. Handlers run on the main
// thread between logic frames.

#pragma once

#include "Common/McpJson.h"
#include "Common/AsciiString.h"

#include <string>
#include <vector>

class Object;
class Player;
class ThingTemplate;

/// Thrown by command handlers to report an error to the client.
struct GameMcpError
{
	std::string message;
};

typedef McpJson (*GameMcpHandler)(const McpJson &args);

void gameMcpRegisterCommand(const char *name, GameMcpHandler handler, const char *help);

/// Parses one request line, runs the command and returns the reply line (without newline).
std::string gameMcpDispatch(const std::string &requestLine);

// Per-area registration, called once from gameMcpDispatch.
void gameMcpRegisterStateCommands();
void gameMcpRegisterObjectCommands();
void gameMcpRegisterPlayerCommands();
void gameMcpRegisterOrderCommands();
void gameMcpRegisterCheatCommands();
void gameMcpRegisterScriptCommands();
void gameMcpRegisterViewCommands();

/// Why the game asked for with -mcpGame did not start; empty if it did (GameMcp.cpp).
const AsciiString &gameMcpStartError();

// Transport (GameMcpServer.cpp).
bool gameMcpServerStart(int port);
void gameMcpServerStop();
/// Runs every queued request through gameMcpDispatch and hands the replies back.
void gameMcpServerDrain();

//-------------------------------------------------------------------------------------------------
// Helpers
//-------------------------------------------------------------------------------------------------

[[noreturn]] void gameMcpFail(const char *fmt, ...);

double gameMcpArgNumber(const McpJson &args, const char *key);
double gameMcpArgNumber(const McpJson &args, const char *key, double defaultValue);
int gameMcpArgInt(const McpJson &args, const char *key);
int gameMcpArgInt(const McpJson &args, const char *key, int defaultValue);
bool gameMcpArgBool(const McpJson &args, const char *key, bool defaultValue);
std::string gameMcpArgString(const McpJson &args, const char *key);
std::string gameMcpArgString(const McpJson &args, const char *key, const char *defaultValue);

/// Fails unless a game is running.
void gameMcpRequireGame();

/// Object by id; fails if there is none.
Object *gameMcpRequireObject(int id);
/// Object ids from "ids" (array) or "id" (number); fails on unknown ids or when none are given.
void gameMcpRequireObjects(const McpJson &args, std::vector<Object *> &objects);

/// Player by internal name ("player0", "SkirmishChina"), display name or index; fails if unknown.
Player *gameMcpRequirePlayer(const std::string &name);
/// The player named by args[key], or the local player when the key is missing.
Player *gameMcpPlayerArg(const McpJson &args, const char *key);
std::string gameMcpPlayerName(const Player *player);

/// Thing template by name; fails with a hint if unknown.
const ThingTemplate *gameMcpRequireTemplate(const std::string &name);

/// Case-insensitive substring test used by list filters.
bool gameMcpContainsNoCase(const char *haystack, const char *needle);

/// Unicode text as UTF-8.
std::string gameMcpUtf8(const UnicodeString &text);

McpJson gameMcpCoord(const Coord3D &pos);
/// World position from args x, y (and optional z; default: the terrain height there).
Coord3D gameMcpArgPosition(const McpJson &args);

/// Names of the bits set in a BitFlags value.
template <class FLAGS>
McpJson gameMcpFlagNames(const FLAGS &flags)
{
	McpJson names = McpJson::makeArray();
	for (Int i = 0; i < flags.size(); i++) {
		if (flags.test(i)) {
			const char *name = FLAGS::getNameFromSingleBit(i);
			names.push(name ? name : "?");
		}
	}
	return names;
}

/// Bit index of a BitFlags bit name (case-insensitive); fails with the valid names if unknown.
template <class FLAGS>
Int gameMcpRequireFlag(const std::string &name, const char *what)
{
	const char *const *names = FLAGS::getBitNames();
	std::string all;
	for (Int i = 0; names[i] != nullptr; i++) {
		if (_stricmp(names[i], name.c_str()) == 0) {
			return i;
		}
		if (all.size() < 1500) {
			all += all.empty() ? "" : ", ";
			all += names[i];
		}
	}
	gameMcpFail("unknown %s '%s' (known: %s)", what, name.c_str(), all.c_str());
}
