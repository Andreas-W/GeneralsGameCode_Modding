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

// McpScriptsShared.h
// Helpers shared by the MCP bridge commands that edit the sides list: scripts, script groups,
// teams and players. Defined in McpCmdScripts.cpp unless noted.

#pragma once

#include "mcp/McpCommands.h"

#include "Common/Dict.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

#include <string>
#include <vector>

namespace McpScripts
{

struct ScriptLoc
{
	Int side;
	ScriptGroup *group;	///< Null for a script at the top level of the player.
	Script *script;
	Int index;					///< Position in its group or in the player's top level list.
};

struct GroupLoc
{
	Int side;
	ScriptGroup *group;
	Int index;
};

/// Points the parameter checks of the script editor at a sides list for the lifetime of the
/// object, so script, team and player references are checked against it.
class ParameterCheckScope
{
public:
	ParameterCheckScope(SidesList *sides);
	~ParameterCheckScope();
};

/// Fails while the script editor window is open: it works on its own copy of the sides list
/// and writes it back on OK, which would silently undo an edit made here in the meantime.
void requireScriptEditorClosed();

AsciiString sideName(SidesList &sides, Int side);
Int findSide(SidesList &sides, const AsciiString &name);	///< -1 if there is no such player.
Int requireSide(SidesList &sides, const std::string &name);	///< The neutral player is "" or "(neutral)".
ScriptList *requireScriptList(SidesList &sides, Int side);

std::vector<ScriptLoc> allScripts(SidesList &sides);
GroupLoc requireGroup(SidesList &sides, const std::string &name, Int side);
/// The script named by args.name, narrowed by the optional args.player, args.group and args.script_index.
ScriptLoc requireScript(SidesList &sides, const McpJson &args);

/// Commits an edited copy of the sides list as one undo step, like the script editor's OK.
void commitSides(SidesList &sides);
/// While a batch runs, commitSides applies the edit without an undo step; the batch records one at its end.
void setSidesBatch(bool active);

Int findActionType(const char *name);		///< ScriptAction type by internal name, -1 if unknown.
Int findConditionType(const char *name);	///< Condition type by internal name, -1 if unknown.

const char *parameterTypeName(Parameter::ParameterType type);
Int parameterTypeFromName(const char *name);	///< -1 if unknown.
/// The names of a parameter type that stores a number with a meaning (COMPARISON, KIND_OF_PARAM, ...).
void namedIntNameList(Parameter::ParameterType type, std::vector<std::string> &names);
McpJson paramValueToJson(const Parameter *parm);
std::string paramValueText(const Parameter *parm);
/// Sets a parameter from JSON; fails with a message that starts with `where`.
void setParamValue(Parameter *parm, const McpJson &in, const char *where, Int ndx);

/// One parameter of a script and where it sits.
struct ParamUse
{
	Parameter *parm;
	bool inCondition;	///< Otherwise in an action or a false action.
};
void collectParamUses(Script *script, std::vector<ParamUse> &out);
/// True for the parameter types whose value is a name or text rather than a number.
bool isStringParamType(Parameter::ParameterType type);
bool scriptNameExists(SidesList &sides, Int side, const std::string &name);

McpJson describeScript(Script *script);
/// Applies the keys present in a script JSON object; other parts of the script are kept.
void applyScriptJson(Script *script, const McpJson &def);
/// Runs the script editor's parameter checks on one script. Needs a ParameterCheckScope.
void collectScriptWarnings(SidesList &sides, Int side, Script *script, McpJson &out);

/// Stores one team property with the type the game reads it as (McpCmdTeams.cpp).
void setTeamPropertyChecked(Dict &dict, const std::string &key, const McpJson &value, bool allowUnknown);
/// What a team property names: "script", "object_type", "waypoint", "player", or null (McpCmdTeams.cpp).
const char *teamPropertyRef(const char *key);
/// Checks team properties: types, unit types, scripts and waypoints they name (McpCmdTeams.cpp).
McpJson verifyTeams(SidesList &sides, Int onlySide, const std::string &onlyName, Int *checked);

} // namespace McpScripts
