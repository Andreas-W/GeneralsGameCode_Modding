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

// AIMatch.h
// A skirmish between AI players that is started from the command line, watched by an observer
// and written down: what every player built, bought and lost, which of its scripts ran and what
// the script engine complained about. Tools use the log to judge AI scripts without anyone
// watching the match.
//
//   -aiMatch <map>               map name or part of its path; starts the match
//   -aiPlayers <list>            Faction:difficulty[:team], comma separated, e.g.
//                                FactionChina:hard,FactionGLA:hard (difficulty easy|normal|hard)
//   -aiMatchLog <file>           where the log goes (JSON, one record per line)
//   -aiMatchMinutes <n>          game time after which the match is stopped (default 30)
//   -aiMatchInterval <seconds>   game time between snapshots (default 10)
//   -aiMatchCash <n>             starting cash (default: the game's)
//   -aiMatchScripts <file>       AI scripts to use instead of Data\Scripts\SkirmishScripts.scb
//   -aiMatchAIData <file>        an AIData.ini read on top of the game's, to try out changes to it
//   -seed <n>                    random seed, for a repeatable match

#pragma once

#include "Common/AsciiString.h"
#include "Common/GameType.h"

#include <map>
#include <set>
#include <stdio.h>
#include <string>
#include <vector>

class Player;

class AIMatch
{
public:
	/// Reads the command line; returns the match if one was asked for.
	static AIMatch *createFromCommandLine();

	~AIMatch();

	/// Sets the skirmish up and asks the game logic to start it.
	void start();

	/// Called once per logic frame.
	void update();

	/// A script ran its actions (or its false actions).
	void scriptFired(const AsciiString &scriptName, Bool conditionsTrue);

	/// The script engine or the AI reported something.
	void scriptMessage(const AsciiString &text);

	/// The player whose AI is at work, so that what it reports can be put down to it. Null when none is.
	void setAIPlayer(Player *player) { m_aiPlayer = player; }

	/// The script file the skirmish players get their AI from; empty for the game's own.
	const AsciiString &getScriptFile() const { return m_scriptFile; }

private:
	struct Entrant
	{
		AsciiString faction;
		Int difficulty;		///< SlotState of the AI
		Int team;					///< -1 for none
	};

	struct ScriptUse
	{
		Int count;
		UnsignedInt first;
		UnsignedInt last;
	};

	struct PlayerState
	{
		std::set<std::string> sciences;
		std::set<std::string> upgrades;
		std::set<UnsignedInt> teams;
		Bool alive;
	};

	AIMatch();

	void fail(const char *reason);
	void writeStart();
	void writeSnapshot();
	void scanTeams();
	void finish(const char *reason);
	void writePlayerScore(Player *player);
	std::vector<Player *> matchPlayers() const;
	Player *scriptPlayer();

	AsciiString m_map;
	AsciiString m_logPath;
	AsciiString m_scriptFile;
	AsciiString m_aiDataFile;
	std::vector<Entrant> m_entrants;
	UnsignedInt m_maxFrames;
	UnsignedInt m_snapshotFrames;
	Int m_startingCash;

	FILE *m_log;
	Bool m_started;
	Bool m_finished;
	std::map<Int, PlayerState> m_state;																///< by player index
	std::map<std::string, std::map<std::string, ScriptUse> > m_scripts;	///< by player name, then script name
	std::map<std::pair<std::string, std::string>, ScriptUse> m_messages;	///< by player name and text
	Player *m_aiPlayer;
	Bool m_askingForPlayer;
};

extern AIMatch *TheAIMatch;	///< null unless a match was asked for on the command line
