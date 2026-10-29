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

// GameMcp.h
// A localhost command bridge that lets an external MCP server drive the running game: pause and
// step it, inspect objects and players, give orders, change values and run scripts. It exists only
// when the game is built with RTS_GAME_MCP and started with -mcp.
//
//   -mcp                     listen on 127.0.0.1:47801
//   -mcpport:<n>             listen on another port
//   -mcpGame <map>           start a skirmish on that map (map name or part of its path)
//   -mcpPlayers <list>       Faction[:team] of the local player (or Observer), then the AI players as
//                            Faction:difficulty[:team], comma separated. Only the local player: a
//                            sandbox without opponents in which nobody wins or loses.
//   -mcpCash <n>             starting cash (default: the game's)
//   -mcpStartPaused          pause as soon as the game has started
//   -seed <n>                random seed

#pragma once

#ifdef RTS_GAME_MCP

#include "Common/AsciiString.h"
#include "GameClient/Display.h"

#include <deque>
#include <string>
#include <vector>

class GameMcp
{
public:
	enum { DEFAULT_PORT = 47801 };

	struct Message
	{
		UnsignedInt serial;
		UnsignedInt frame;
		std::string text;
	};

	/// Reads the command line; returns the bridge if -mcp asked for one and it could listen.
	static GameMcp *createFromCommandLine();

	~GameMcp();

	/// Starts the skirmish asked for with -mcpGame, if any.
	void startGameFromCommandLine();

	/// Runs the commands that have arrived. Main thread only, between logic frames.
	void poll();

	/// Runs the game logic without the client, honoring pause and frame steps. Returns the process exit code.
	Int runHeadless();

	/// The script engine reported something.
	void scriptMessage(const AsciiString &text);

	Bool isSandbox() const { return m_sandbox; }
	Int getPort() const { return m_port; }
	const AsciiString &getStartedMap() const { return m_map; }

	/// Frames per second the headless loop is held to; 0 for as fast as possible.
	void setHeadlessFps(Int fps) { m_headlessFps = fps; }
	Int getHeadlessFps() const { return m_headlessFps; }

	const std::deque<Message> &getMessages() const { return m_messages; }
	UnsignedInt getNextMessageSerial() const { return m_nextMessageSerial; }

private:
	GameMcp();

	Int m_port;
	AsciiString m_map;
	Bool m_sandbox;
	Bool m_pauseAtStart;
	Int m_headlessFps;
	std::deque<Message> m_messages;
	UnsignedInt m_nextMessageSerial;
};

extern GameMcp *TheGameMcp;	///< null unless the game was started with -mcp

/// Writes what the display shows to a file; set by the display device, null without one (headless).
typedef bool (*GameMcpScreenshotFunc)(const char *pathname, ScreenshotFormat format, Int jpegQuality);
extern GameMcpScreenshotFunc TheGameMcpScreenshot;

/// Post-processing of the Direct3D 12 renderer; set by the display device, all null without one.
struct GameMcpPostProcess
{
	bool (*isAvailable)();
	bool (*isRunning)();
	void (*reload)();
	bool (*setParameter)(const char *pass, const char *name, const Real *values, Int count);
	void (*getPasses)(std::vector<AsciiString> &passes, AsciiString &error);
};
extern GameMcpPostProcess TheGameMcpPostProcess;

/// Shadow maps of the Direct3D 12 renderer; set by the display device, all null without one.
struct GameMcpShadowMap
{
	bool (*isAvailable)();
	void (*getState)(bool &active, bool &rendered, Int &casters, Real &coverage, Real &sunElevation, bool &debug);
	void (*setDebug)(bool debug);
};
extern GameMcpShadowMap TheGameMcpShadowMap;

/// HLSL materials of the Direct3D 12 renderer; set by the display device, all null without one.
struct GameMcpMaterials
{
	bool (*isAvailable)();
	bool (*isRunning)();
	void (*setEnabled)(bool enabled);
	void (*reload)();
	bool (*setParameter)(const char *material, const char *name, const Real *values, Int count);
	void (*getState)(std::vector<AsciiString> &names, std::vector<Int> &meshTextures, AsciiString &error);
	void (*collectTextures)(std::vector<AsciiString> &names);
};
extern GameMcpMaterials TheGameMcpMaterials;

#endif // RTS_GAME_MCP
