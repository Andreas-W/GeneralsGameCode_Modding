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

// GameMcpCmdScripts.cpp
// Script engine commands of the MCP bridge: run scripts, switch them on and off, read and set
// flags and counters.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"

namespace
{

McpJson cmdRun(const McpJson &args)
{
	gameMcpRequireGame();
	const std::string name = gameMcpArgString(args, "name");
	TheScriptEngine->runScript(name.c_str());
	return McpJson::makeObject().set("ran", name).set("frame", TheGameLogic->getFrame());
}

McpJson cmdAction(const McpJson &args)
{
	gameMcpRequireGame();
	const std::string name = gameMcpArgString(args, "name");
	Int type = 0;
	while (type < ScriptAction::NUM_ITEMS && TheScriptEngine->getActionTemplate(type)->m_internalName.compareNoCase(name.c_str()) != 0)
		++type;
	if (type == ScriptAction::NUM_ITEMS) {
		gameMcpFail("no script action named '%s' (internal names like CAMERA_MOD_SET_FINAL_ZOOM)", name.c_str());
	}
	const ActionTemplate *actionTemplate = TheScriptEngine->getActionTemplate(type);
	const McpJson empty = McpJson::makeArray();
	const McpJson &parameters = args.has("parameters") ? args.get("parameters") : empty;
	if ((Int)parameters.size() != actionTemplate->getNumParameters()) {
		gameMcpFail("%s takes %d parameters, got %d", name.c_str(), actionTemplate->getNumParameters(), (Int)parameters.size());
	}
	ScriptAction *action = newInstance(ScriptAction)((enum ScriptAction::ScriptActionType)type);
	for (Int i = 0; i < actionTemplate->getNumParameters(); i++) {
		const McpJson &value = parameters.at(i);
		Parameter *parameter = action->getParameter(i);
		switch (actionTemplate->getParameterType(i)) {
			case Parameter::REAL:
				parameter->friend_setReal((Real)(value.isNumber() ? value.asNumber() : atof(value.asString().c_str())));
				break;
			case Parameter::INT:
			case Parameter::BOOLEAN:
				parameter->friend_setInt(value.isBool() ? (value.asBool() ? 1 : 0) : (Int)(value.isNumber() ? value.asNumber() : atoi(value.asString().c_str())));
				break;
			default:
				if (!value.isString()) {
					deleteInstance(action);
					gameMcpFail("parameter %d of %s must be a string", i, name.c_str());
				}
				parameter->friend_setString(value.asString().c_str());
				break;
		}
	}
	TheScriptActions->executeAction(action);
	deleteInstance(action);
	return McpJson::makeObject().set("ran", name).set("frame", TheGameLogic->getFrame());
}

McpJson cmdSetActive(const McpJson &args)
{
	gameMcpRequireGame();
	const std::string name = gameMcpArgString(args, "name");
	const bool active = gameMcpArgBool(args, "active", true);
	if (!TheScriptEngine->setScriptActive(name.c_str(), active)) {
		gameMcpFail("no script or script group named '%s'", name.c_str());
	}
	return McpJson::makeObject().set("name", name).set("active", active);
}

McpJson cmdFlags(const McpJson &args)
{
	gameMcpRequireGame();
	if (args.has("set")) {
		const McpJson &values = args.get("set");
		for (size_t i = 0; i < values.numKeys(); i++) {
			if (!values.valueAt(i).isBool()) {
				gameMcpFail("flag '%s' must be set to true or false", values.keyAt(i).c_str());
			}
			TheScriptEngine->setFlagValue(values.keyAt(i).c_str(), values.valueAt(i).asBool());
		}
	}
	const std::string contains = gameMcpArgString(args, "contains", "");
	McpJson flags = McpJson::makeObject();
	for (Int i = 1; i < TheScriptEngine->friend_getNumFlags(); i++) {
		const TFlag *flag = TheScriptEngine->friend_getNthFlag(i);
		if (gameMcpContainsNoCase(flag->name.str(), contains.c_str())) {
			flags.set(flag->name.str(), flag->value != FALSE);
		}
	}
	return McpJson::makeObject().set("flags", flags);
}

McpJson cmdCounters(const McpJson &args)
{
	gameMcpRequireGame();
	if (args.has("set")) {
		const McpJson &values = args.get("set");
		for (size_t i = 0; i < values.numKeys(); i++) {
			if (!values.valueAt(i).isNumber()) {
				gameMcpFail("counter '%s' must be set to a number", values.keyAt(i).c_str());
			}
			TheScriptEngine->setCounterValue(values.keyAt(i).c_str(), (Int)values.valueAt(i).asNumber());
		}
	}
	const std::string contains = gameMcpArgString(args, "contains", "");
	McpJson counters = McpJson::makeObject();
	for (Int i = 1; i < TheScriptEngine->friend_getNumCounters(); i++) {
		const TCounter *counter = TheScriptEngine->friend_getNthCounter(i);
		if (gameMcpContainsNoCase(counter->name.str(), contains.c_str())) {
			counters.set(counter->name.str(), McpJson::makeObject().set("value", counter->value)
				.set("timer", counter->isCountdownTimer != FALSE));
		}
	}
	return McpJson::makeObject().set("counters", counters).set("note", "timers count logic frames (30 per second)");
}

} // namespace

void gameMcpRegisterScriptCommands()
{
	gameMcpRegisterCommand("scripts.run", cmdRun, "{name} Runs a script's actions now (like the RUN_SCRIPT action), whatever its conditions.");
	gameMcpRegisterCommand("scripts.action", cmdAction, "{name, parameters=[...]} Runs one script action now, by internal name, with its parameters in order (numbers, true/false, or strings for names and text).");
	gameMcpRegisterCommand("scripts.set_active", cmdSetActive, "{name, active=true} Enables or disables a script or script group.");
	gameMcpRegisterCommand("scripts.flags", cmdFlags, "{contains?, set?: {name: bool}} Script flags (set first, then list).");
	gameMcpRegisterCommand("scripts.counters", cmdCounters, "{contains?, set?: {name: number}} Script counters and timers (set first, then list).");
}

#endif // RTS_GAME_MCP
