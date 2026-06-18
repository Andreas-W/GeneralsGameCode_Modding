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

// FILE: ChatCommand.h //////////////////////////////////////////////////////////////////////////
// Desc: Parsing and storage for ChatCommand blocks defined in the optional ChatCommands.ini.
//       Commands carry no behavior yet; key/value attributes and dispatch logic come later.
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ChatCommand_H_
#define _ChatCommand_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"
#include <vector>

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class INI;
struct FieldParse;

//-------------------------------------------------------------------------------------------------
/** A single chat command parsed from a "ChatCommand <name> ... End" block. */
//-------------------------------------------------------------------------------------------------
class ChatCommand
{
public:
	ChatCommand() {}

	const AsciiString& getName() const { return m_name; }
	void setName( const AsciiString& name ) { m_name = name; }

	const FieldParse* getFieldParse() const { return s_fieldParseTable; }

	Int getMoney() const { return m_money; }
	UnsignedInt getRank() const { return m_rank; }
	Bool getReadyTimers() const { return m_readyTimers; }
	const AsciiString& getSpawnUnit() const { return m_spawnUnit; }
	Bool getToggleUnitRequirements() const { return m_toggleUnitRequirements; }
	Bool getToggleInfiniteEnergy() const { return m_toggleInfiniteEnergy; }
	Bool getGrantAllUpgrades() const { return m_grantAllUpgrades; }
	Int getPromoteUnit() const { return m_promoteUnit; }
	Int getGiveSalvage() const { return m_giveSalvage; }

	/** Run this command's effects. Inspects the parsed members and acts accordingly. */
	void execute() const;

private:
	AsciiString m_name;
	Int m_money = 0;			///< "Money" attribute; signed amount, defaults to 0.
	UnsignedInt m_rank = 0;		///< "Rank" attribute; ranks to grant, capped at the max rank. Defaults to 0.
	Bool m_readyTimers = FALSE;	///< "ReadyTimers" attribute; when TRUE, set all of the player's special power timers to ready.
	AsciiString m_spawnUnit;	///< "SpawnUnit" attribute; ObjectTemplate name to spawn for the local player at the mouse cursor.
	Bool m_toggleUnitRequirements = FALSE;	///< "ToggleUnitRequirements" attribute; when TRUE, toggles ignoring unit/building build prereqs (science still applies).
	Bool m_toggleInfiniteEnergy = FALSE;	///< "ToggleInfiniteEnergy" attribute; when TRUE, toggles infinite power for the local player.
	Bool m_grantAllUpgrades = FALSE;		///< "GrantAllUpgrades" attribute; when TRUE, grants the local player all player-type upgrades.
	Int m_promoteUnit = 0;					///< "PromoteUnit" attribute; promote selected units by this many veterancy levels (negative demotes), capped to the valid range.
	Int m_giveSalvage = 0;					///< "GiveSalvage" attribute; change selected salvagers' crate-upgrade tier by this much (negative removes), capped 0..2.

	static const FieldParse s_fieldParseTable[];
};

//-------------------------------------------------------------------------------------------------
/** The store that owns all ChatCommands parsed from ChatCommands.ini. */
//-------------------------------------------------------------------------------------------------
class ChatCommandStore : public SubsystemInterface
{
public:
	ChatCommandStore() {}
	virtual ~ChatCommandStore();

	virtual void init() {}
	virtual void reset();
	virtual void update() {}

	/** Return the command with the given name, or NULL if none exists. */
	const ChatCommand* findChatCommand( const AsciiString& name ) const;

	/** Number of parsed commands. */
	UnsignedInt getChatCommandCount() const { return (UnsignedInt)m_commands.size(); }

	// INI block parser, registered in INI's block table.
	static void parseChatCommandDefinition( INI* ini );

private:
	void clear();

	std::vector<ChatCommand*> m_commands;
};

// EXTERNALS //////////////////////////////////////////////////////////////////////////////////////
extern ChatCommandStore* TheChatCommandStore;

#endif // _ChatCommand_H_
