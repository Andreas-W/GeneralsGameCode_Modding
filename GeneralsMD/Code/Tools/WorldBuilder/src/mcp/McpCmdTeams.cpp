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

// McpCmdTeams.cpp
// MCP bridge commands for team templates (the Teams dialog). A team is a property dict; the
// table below lists the properties TeamPrototype reads, with the type it reads them as. A
// property stored with another type is silently ignored by the game, so writes are checked
// against the table.

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpScriptsShared.h"

#include "WorldBuilderDoc.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/SidesList.h"

#include <algorithm>
#include <limits.h>
#include <map>
#include <math.h>
#include <set>
#include <utility>
#include <vector>

using namespace McpScripts;

namespace
{

enum { NUM_UNIT_SLOTS = 7 };

/// What a string property refers to.
enum TeamRef
{
	REF_NONE,
	REF_SCRIPT,
	REF_OBJECT_TYPE,
	REF_WAYPOINT,
	REF_PLAYER
};

struct TeamKeyInfo
{
	const char *name;				///< Key, or its prefix when it is numbered.
	Dict::DataType type;
	TeamRef ref;
	Int firstNumber, lastNumber;	///< Number range appended to the name; -1 when it has none.
	const char *help;
};

const TeamKeyInfo TEAM_KEYS[] =
{
	{ "teamName", Dict::DICT_ASCIISTRING, REF_NONE, -1, -1, "Team name; objects, scripts and other teams refer to it." },
	{ "teamOwner", Dict::DICT_ASCIISTRING, REF_PLAYER, -1, -1, "Owning player (\"\" = neutral)." },
	{ "teamIsSingleton", Dict::DICT_BOOL, REF_NONE, -1, -1, "Only one instance ever exists; true for the players' default teams." },
	{ "teamDescription", Dict::DICT_ASCIISTRING, REF_NONE, -1, -1, "Free text for the editor." },
	{ "teamHome", Dict::DICT_ASCIISTRING, REF_WAYPOINT, -1, -1, "Waypoint the team is built at / returns to." },
	{ "teamUnitType", Dict::DICT_ASCIISTRING, REF_OBJECT_TYPE, 1, NUM_UNIT_SLOTS, "Unit type of slot N." },
	{ "teamUnitMinCount", Dict::DICT_INT, REF_NONE, 1, NUM_UNIT_SLOTS, "Units of slot N the team needs before it is complete." },
	{ "teamUnitMaxCount", Dict::DICT_INT, REF_NONE, 1, NUM_UNIT_SLOTS, "Units of slot N the AI builds at most; 0 disables the slot." },
	{ "teamMaxInstances", Dict::DICT_INT, REF_NONE, -1, -1, "How many copies of the team may exist at once." },
	{ "teamProductionCondition", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script whose conditions must be true for the AI to build the team." },
	{ "teamProductionPriority", Dict::DICT_INT, REF_NONE, -1, -1, "The AI builds the buildable team with the highest priority first." },
	{ "teamProductionPrioritySuccessIncrease", Dict::DICT_INT, REF_NONE, -1, -1, "Added to the priority when the team succeeds." },
	{ "teamProductionPriorityFailureDecrease", Dict::DICT_INT, REF_NONE, -1, -1, "Subtracted from the priority when the team is destroyed." },
	{ "teamExecutesActionsOnCreate", Dict::DICT_BOOL, REF_NONE, -1, -1, "Run the actions of the production condition script when the team is created." },
	{ "teamOnCreateScript", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script run when the team is created." },
	{ "teamOnIdleScript", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script run when the team is idle." },
	{ "teamInitialIdleFrames", Dict::DICT_INT, REF_NONE, -1, -1, "Frames to wait before the team counts as idle." },
	{ "teamEnemySightedScript", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script run when the team sees an enemy." },
	{ "teamAllClearScript", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script run when no enemy is in sight any more." },
	{ "teamOnUnitDestroyedScript", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script run each time a unit of the team is destroyed." },
	{ "teamOnDestroyedScript", Dict::DICT_ASCIISTRING, REF_SCRIPT, -1, -1, "Script run when the team counts as destroyed." },
	{ "teamDestroyedThreshold", Dict::DICT_REAL, REF_NONE, -1, -1, "Fraction of units lost (0..1) at which the team counts as destroyed." },
	{ "teamGenericScriptHook", Dict::DICT_ASCIISTRING, REF_SCRIPT, 0, MAX_GENERIC_SCRIPTS - 1, "Script N the team keeps trying to run." },
	{ "teamAggressiveness", Dict::DICT_INT, REF_NONE, -1, -1, "Initial attitude: -2 sleep, -1 passive, 0 normal, 1 alert, 2 aggressive." },
	{ "teamIsAIRecruitable", Dict::DICT_BOOL, REF_NONE, -1, -1, "The AI may take units of this team for other teams." },
	{ "teamIsBaseDefense", Dict::DICT_BOOL, REF_NONE, -1, -1, "Team defends the base." },
	{ "teamIsPerimeterDefense", Dict::DICT_BOOL, REF_NONE, -1, -1, "Team defends the base perimeter." },
	{ "teamAutoReinforce", Dict::DICT_BOOL, REF_NONE, -1, -1, "Replace lost units automatically." },
	{ "teamAttackCommonTarget", Dict::DICT_BOOL, REF_NONE, -1, -1, "All units attack the same target." },
	{ "teamAvoidThreats", Dict::DICT_BOOL, REF_NONE, -1, -1, "Path around enemy defenses." },
	{ "teamTransport", Dict::DICT_ASCIISTRING, REF_OBJECT_TYPE, -1, -1, "Reinforcement: transport unit type." },
	{ "teamReinforcementOrigin", Dict::DICT_ASCIISTRING, REF_WAYPOINT, -1, -1, "Reinforcement: waypoint the team arrives at." },
	{ "teamStartsFull", Dict::DICT_BOOL, REF_NONE, -1, -1, "Reinforcement: the transports start loaded." },
	{ "teamTransportsExit", Dict::DICT_BOOL, REF_NONE, -1, -1, "Reinforcement: transports leave the map after unloading." },
	{ "teamTransportsReturn", Dict::DICT_BOOL, REF_NONE, -1, -1, "Reinforcement: transports return to their origin." },
	{ "teamVeterancy", Dict::DICT_INT, REF_NONE, -1, -1, "Starting veterancy: 0 regular, 1 veteran, 2 elite, 3 heroic." },
	// Written by the script export of the editor; the game does not read it.
	{ "exportWithScript", Dict::DICT_BOOL, REF_NONE, -1, -1, nullptr },
};

/// Looks up a team property, with or without its number.
const TeamKeyInfo *findTeamKey(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(TEAM_KEYS); i++) {
		const TeamKeyInfo &info = TEAM_KEYS[i];
		const size_t len = strlen(info.name);
		if (info.firstNumber < 0) {
			if (strcmp(name, info.name) == 0) return &info;
			continue;
		}
		if (strncmp(name, info.name, len) != 0 || name[len] == 0) continue;
		const char *digits = name + len;
		Bool allDigits = true;
		for (const char *p = digits; *p; p++) {
			if (*p < '0' || *p > '9') allDigits = false;
		}
		// "teamUnitType01" would be another key than the one the game reads.
		if (!allDigits || (digits[0] == '0' && digits[1] != 0)) continue;
		const Int number = atoi(digits);
		if (number >= info.firstNumber && number <= info.lastNumber) return &info;
	}
	return nullptr;
}

const char *dictTypeName(Dict::DataType type)
{
	switch (type) {
		case Dict::DICT_BOOL: return "bool";
		case Dict::DICT_INT: return "int";
		case Dict::DICT_REAL: return "real";
		case Dict::DICT_ASCIISTRING: return "string";
		case Dict::DICT_UNICODESTRING: return "unicode string";
		default: return "none";
	}
}

const char *teamRefName(TeamRef ref)
{
	switch (ref) {
		case REF_SCRIPT: return "script";
		case REF_OBJECT_TYPE: return "object_type";
		case REF_WAYPOINT: return "waypoint";
		case REF_PLAYER: return "player";
		default: return nullptr;
	}
}

/// The Teams dialog stores "<none>" for an unused unit slot.
bool isNoUnit(const AsciiString &type)
{
	return type.isEmpty() || type == "<none>";
}

NameKeyType numberedKey(const char *prefix, Int number)
{
	AsciiString name;
	name.format("%s%d", prefix, number);
	return TheNameKeyGenerator->nameToKey(name);
}

/// Stores one team property with the type the game reads it as.
void setTeamProperty(Dict &dict, const std::string &keyName, const McpJson &value, bool allowUnknown)
{
	const NameKeyType key = TheNameKeyGenerator->nameToKey(keyName.c_str());
	if (value.isNull()) {
		dict.remove(key);
		return;
	}
	const TeamKeyInfo *info = findTeamKey(keyName.c_str());
	if (info == nullptr) {
		if (!allowUnknown) {
			mcpFail("unknown team property '%s' (see teams.list_properties; pass allow_unknown_properties=true to store it anyway)",
				keyName.c_str());
		}
		McpJson single = McpJson::makeObject();
		single.set(keyName.c_str(), value);
		mcpJsonToDict(single, dict);
		return;
	}
	switch (info->type) {
		case Dict::DICT_BOOL:
			if (!value.isBool()) mcpFail("team property '%s' must be true or false", keyName.c_str());
			dict.setBool(key, value.asBool());
			break;
		case Dict::DICT_INT:
			if (!value.isNumber() || value.asNumber() != floor(value.asNumber())) {
				mcpFail("team property '%s' must be a whole number", keyName.c_str());
			}
			dict.setInt(key, (Int)value.asNumber());
			break;
		case Dict::DICT_REAL:
			if (!value.isNumber()) mcpFail("team property '%s' must be a number", keyName.c_str());
			dict.setReal(key, (Real)value.asNumber());
			break;
		default:
			if (!value.isString()) mcpFail("team property '%s' must be a string", keyName.c_str());
			dict.setAsciiString(key, AsciiString(value.asString().c_str()));
			break;
	}
}

/// The unit slots as "units": [{"type", "min", "max"}], which replaces all seven slots.
void setTeamUnits(Dict &dict, const McpJson &units)
{
	if (!units.isArray() || units.size() > NUM_UNIT_SLOTS) {
		mcpFail("'units' must be an array of at most %d {type, min, max} entries", NUM_UNIT_SLOTS);
	}
	for (Int slot = 1; slot <= NUM_UNIT_SLOTS; slot++) {
		AsciiString type;
		Int minCount = 0, maxCount = 0;
		if ((size_t)(slot - 1) < units.size()) {
			const McpJson &unit = units.at(slot - 1);
			if (!unit.isObject() || !unit.get("type").isString() || unit.get("type").asString().empty()) {
				mcpFail("units[%d] must be an object with a 'type' (unit type name), and 'min' / 'max' counts", slot - 1);
			}
			type = unit.get("type").asString().c_str();
			maxCount = mcpArgInt(unit, "max", 1);
			minCount = mcpArgInt(unit, "min", maxCount);
			if (minCount < 0 || maxCount < minCount) {
				mcpFail("units[%d]: need 0 <= min <= max", slot - 1);
			}
		}
		dict.setAsciiString(numberedKey("teamUnitType", slot), type);
		dict.setInt(numberedKey("teamUnitMinCount", slot), minCount);
		dict.setInt(numberedKey("teamUnitMaxCount", slot), maxCount);
	}
}

McpJson describeUnits(Dict *d, bool asText, const std::string &unitFilter, bool *unitMatch)
{
	McpJson units = McpJson::makeArray();
	for (Int slot = 1; slot <= NUM_UNIT_SLOTS; slot++) {
		Bool exists = false;
		const AsciiString type = d->getAsciiString(numberedKey("teamUnitType", slot), &exists);
		if (!exists || isNoUnit(type)) continue;
		const Int minCount = d->getInt(numberedKey("teamUnitMinCount", slot), &exists);
		const Int maxCount = d->getInt(numberedKey("teamUnitMaxCount", slot), &exists);
		if (maxCount <= 0) continue;
		if (unitMatch && mcpContainsNoCase(type.str(), unitFilter.c_str())) *unitMatch = true;
		if (asText) {
			// "3-6 AmericaTankCrusader"
			AsciiString text;
			if (minCount == maxCount) {
				text.format("%d %s", maxCount, type.str());
			} else {
				text.format("%d-%d %s", minCount, maxCount, type.str());
			}
			units.push(McpJson(text.str()));
		} else {
			units.push(McpJson::makeObject().set("type", type.str()).set("min", minCount).set("max", maxCount));
		}
	}
	return units;
}

//-------------------------------------------------------------------------------------------------
// Verification
//-------------------------------------------------------------------------------------------------

/// missingKind (a parameter type name) and missing say what does not exist, for scripts.missing.
void addTeamWarning(McpJson &out, const AsciiString &team, const AsciiString &owner, const char *property, const AsciiString &warning,
	const char *missingKind = nullptr, const AsciiString &missing = AsciiString::TheEmptyString)
{
	McpJson entry = McpJson::makeObject();
	entry.set("team", team.str()).set("owner", owner.str()).set("property", property).set("warning", warning.str());
	if (missingKind) {
		entry.set("missing_kind", missingKind).set("missing", missing.str());
	}
	out.push(entry);
}

bool waypointExists(const AsciiString &name)
{
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->isWaypoint() && obj->getWaypointName() == name) return true;
	}
	return false;
}

/// Script and group names per player; teams name scripts of their own player.
std::vector<std::set<std::string> > scriptNamesPerSide(SidesList &sides)
{
	std::vector<std::set<std::string> > names(sides.getNumSides());
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		names[all[i].side].insert(all[i].script->getName().str());
	}
	for (Int side = 0; side < sides.getNumSides(); side++) {
		ScriptList *list = sides.getSideInfo(side)->getScriptList();
		if (list == nullptr) continue;
		for (ScriptGroup *group = list->getScriptGroup(); group; group = group->getNext()) {
			names[side].insert(group->getName().str());
		}
	}
	return names;
}

void collectTeamWarnings(SidesList &sides, TeamsInfo *team, const std::vector<std::set<std::string> > &scriptNames, McpJson &out)
{
	Dict *d = team->getDict();
	const AsciiString name = d->getAsciiString(TheKey_teamName);
	const AsciiString owner = d->getAsciiString(TheKey_teamOwner);
	const Int side = findSide(sides, owner);
	AsciiString warning;
	if (side < 0) {
		warning.format("Player '%s' does not exist.", owner.str());
		addTeamWarning(out, name, owner, "teamOwner", warning);
	}

	for (Int i = 0; i < d->getPairCount(); i++) {
		const AsciiString key = TheNameKeyGenerator->keyToName(d->getNthKey(i));
		const TeamKeyInfo *info = findTeamKey(key.str());
		if (info == nullptr) {
			addTeamWarning(out, name, owner, key.str(), "Unknown team property; the game does not read it.");
			continue;
		}
		if (d->getNthType(i) != info->type) {
			warning.format("Stored as %s, but the game reads it as %s and ignores it.", dictTypeName(d->getNthType(i)), dictTypeName(info->type));
			addTeamWarning(out, name, owner, key.str(), warning);
			continue;
		}
		if (info->type != Dict::DICT_ASCIISTRING) continue;
		const AsciiString value = d->getNthAsciiString(i);
		if (value.isEmpty()) continue;
		switch (info->ref) {
			case REF_SCRIPT: {
				if (side >= 0 && scriptNames[side].count(value.str())) break;
				Int elsewhere = -1;
				for (Int other = 0; other < sides.getNumSides() && elsewhere < 0; other++) {
					if (other != side && scriptNames[other].count(value.str())) elsewhere = other;
				}
				if (elsewhere < 0) {
					warning.format("Script '%s' does not exist.", value.str());
				} else {
					warning.format("Script '%s' does not exist for player '%s' (player '%s' has one).", value.str(), owner.str(),
						sideName(sides, elsewhere).str());
				}
				addTeamWarning(out, name, owner, key.str(), warning, "SCRIPT", value);
				break;
			}
			case REF_OBJECT_TYPE:
				if (!isNoUnit(value) && TheThingFactory->findTemplate(value, FALSE) == nullptr) {
					warning.format("Object type '%s' does not exist.", value.str());
					addTeamWarning(out, name, owner, key.str(), warning, "OBJECT_TYPE", value);
				}
				break;
			case REF_WAYPOINT:
				if (!waypointExists(value)) {
					warning.format("Waypoint '%s' does not exist.", value.str());
					addTeamWarning(out, name, owner, key.str(), warning, "WAYPOINT", value);
				}
				break;
			default:
				break;
		}
	}

	for (Int slot = 1; slot <= NUM_UNIT_SLOTS; slot++) {
		Bool exists = false;
		const AsciiString type = d->getAsciiString(numberedKey("teamUnitType", slot), &exists);
		const Int minCount = d->getInt(numberedKey("teamUnitMinCount", slot), &exists);
		const Int maxCount = d->getInt(numberedKey("teamUnitMaxCount", slot), &exists);
		AsciiString key;
		key.format("teamUnitMinCount%d", slot);
		if (minCount > maxCount) {
			warning.format("Minimum %d is above the maximum %d.", minCount, maxCount);
			addTeamWarning(out, name, owner, key.str(), warning);
		}
		if (maxCount > 0 && isNoUnit(type)) {
			key.format("teamUnitType%d", slot);
			addTeamWarning(out, name, owner, key.str(), "The slot has a count but no unit type.");
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Commands
//-------------------------------------------------------------------------------------------------

McpJson cmdListTeamProperties(const McpJson &)
{
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < ARRAY_SIZE(TEAM_KEYS); i++) {
		const TeamKeyInfo &info = TEAM_KEYS[i];
		if (info.help == nullptr) continue;
		McpJson entry = McpJson::makeObject();
		if (info.firstNumber >= 0) {
			AsciiString name;
			name.format("%s<N>", info.name);
			entry.set("key", name.str()).set("first", info.firstNumber).set("last", info.lastNumber);
		} else {
			entry.set("key", info.name);
		}
		entry.set("type", dictTypeName(info.type));
		if (teamRefName(info.ref)) entry.set("names", teamRefName(info.ref));
		entry.set("help", info.help);
		list.push(entry);
	}
	return McpJson::makeObject().set("properties", list);
}

/// The team filters shared by the commands that work on many teams.
struct TeamFilter
{
	bool hasOwner, includeDefault;
	AsciiString owner;
	std::string name, unit, script;

	void read(SidesList &sides, const McpJson &args)
	{
		hasOwner = args.has("owner");
		owner = hasOwner ? sideName(sides, requireSide(sides, mcpArgString(args, "owner"))) : AsciiString::TheEmptyString;
		name = mcpArgString(args, "filter", "");
		unit = mcpArgString(args, "unit", "");
		script = mcpArgString(args, "script", "");
		includeDefault = mcpArgBool(args, "include_default_teams", false);
	}

	bool hasAny() const
	{
		return hasOwner || !name.empty() || !unit.empty() || !script.empty();
	}

	bool matches(SidesList &sides, TeamsInfo *team) const
	{
		Dict *d = team->getDict();
		if (hasOwner && d->getAsciiString(TheKey_teamOwner) != owner) return false;
		if (!includeDefault && sides.isPlayerDefaultTeam(team)) return false;
		if (!mcpContainsNoCase(d->getAsciiString(TheKey_teamName).str(), name.c_str())) return false;
		if (!unit.empty()) {
			bool unitMatch = false;
			describeUnits(d, true, unit, &unitMatch);
			if (!unitMatch) return false;
		}
		if (!script.empty()) {
			bool scriptMatch = false;
			for (Int k = 0; k < d->getPairCount() && !scriptMatch; k++) {
				if (d->getNthType(k) != Dict::DICT_ASCIISTRING) continue;
				const char *ref = McpScripts::teamPropertyRef(TheNameKeyGenerator->keyToName(d->getNthKey(k)).str());
				scriptMatch = ref && strcmp(ref, "script") == 0 && _stricmp(d->getNthAsciiString(k).str(), script.c_str()) == 0;
			}
			if (!scriptMatch) return false;
		}
		return true;
	}
};

/// Teams in short form for reviewing many at once: unit composition and the scripts they name.
McpJson cmdListTeams(const McpJson &args)
{
	mcpDoc();
	SidesList &sides = *TheSidesList;
	const bool hasOwner = args.has("owner");
	const AsciiString owner = hasOwner ? sideName(sides, requireSide(sides, mcpArgString(args, "owner"))) : AsciiString::TheEmptyString;
	const std::string filter = mcpArgString(args, "filter", "");
	const std::string unit = mcpArgString(args, "unit", "");
	const std::string script = mcpArgString(args, "script", "");
	const bool includeDefault = mcpArgBool(args, "include_default_teams", false);
	const Int limit = mcpArgInt(args, "limit", 100);
	const Int offset = mcpArgInt(args, "offset", 0);

	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		Dict *d = team->getDict();
		const AsciiString name = d->getAsciiString(TheKey_teamName);
		const AsciiString teamOwner = d->getAsciiString(TheKey_teamOwner);
		if (hasOwner && teamOwner != owner) continue;
		if (!includeDefault && sides.isPlayerDefaultTeam(team)) continue;
		if (!mcpContainsNoCase(name.str(), filter.c_str())) continue;

		bool unitMatch = unit.empty();
		const McpJson units = describeUnits(d, true, unit, &unitMatch);
		if (!unitMatch) continue;

		// Every property that names a script, without the "team" prefix of the key.
		McpJson scripts = McpJson::makeObject();
		bool scriptMatch = script.empty();
		for (Int k = 0; k < d->getPairCount(); k++) {
			if (d->getNthType(k) != Dict::DICT_ASCIISTRING) continue;
			const AsciiString key = TheNameKeyGenerator->keyToName(d->getNthKey(k));
			const TeamKeyInfo *info = findTeamKey(key.str());
			if (info == nullptr || info->ref != REF_SCRIPT) continue;
			const AsciiString value = d->getNthAsciiString(k);
			if (value.isEmpty()) continue;
			scripts.set(key.str() + 4, value.str());
			if (_stricmp(value.str(), script.c_str()) == 0) scriptMatch = true;
		}
		if (!scriptMatch) continue;

		matched++;
		if (matched <= offset || (Int)list.size() >= limit) continue;
		McpJson entry = McpJson::makeObject();
		entry.set("name", name.str()).set("owner", teamOwner.str());
		if (sides.isPlayerDefaultTeam(team)) entry.set("is_default_team", true);
		entry.set("units", units);
		Bool exists = false;
		const Int priority = d->getInt(TheKey_teamProductionPriority, &exists);
		if (exists) entry.set("priority", priority);
		const Int maxInstances = d->getInt(TheKey_teamMaxInstances, &exists);
		if (exists) entry.set("max_instances", maxInstances);
		entry.set("scripts", scripts);
		list.push(entry);
	}
	return McpJson::makeObject().set("total_matches", matched).set("teams", list);
}

McpJson cmdGetTeam(const McpJson &args)
{
	mcpDoc();
	const std::string name = mcpArgString(args, "name");
	TeamsInfo *team = TheSidesList->findTeamInfo(AsciiString(name.c_str()));
	if (team == nullptr) {
		mcpFail("no team named '%s' (see teams.list)", name.c_str());
	}
	McpJson j = McpJson::makeObject();
	j.set("name", name).set("is_default_team", (bool)(TheSidesList->isPlayerDefaultTeam(team) != 0));
	j.set("units", describeUnits(team->getDict(), false, "", nullptr));
	j.set("properties", mcpDictToJson(*team->getDict()));
	return j;
}

/// Creates a team or changes the properties of an existing one (the team template dialog).
McpJson cmdSetTeam(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const std::string name = mcpArgString(args, "name");
	if (name.empty()) {
		mcpFail("the team name must not be empty");
	}
	const AsciiString teamName(name.c_str());
	const bool allowUnknown = mcpArgBool(args, "allow_unknown_properties", false);
	SidesList sides = *TheSidesList;
	TeamsInfo *team = sides.findTeamInfo(teamName);
	const bool created = team == nullptr;
	if (created) {
		if (!args.has("owner")) {
			mcpFail("no team named '%s'; pass owner (a player name) to create it", name.c_str());
		}
		const Int side = requireSide(sides, mcpArgString(args, "owner"));
		// The Teams dialog starts a new team with these three.
		Dict d;
		d.setAsciiString(TheKey_teamName, teamName);
		d.setAsciiString(TheKey_teamOwner, sideName(sides, side));
		d.setBool(TheKey_teamIsSingleton, false);
		sides.addTeam(&d);
		team = sides.findTeamInfo(teamName);
		if (team == nullptr) {
			mcpFail("could not add team '%s'", name.c_str());
		}
	} else if (args.has("owner")) {
		const Int side = requireSide(sides, mcpArgString(args, "owner"));
		if (sides.isPlayerDefaultTeam(team) && team->getDict()->getAsciiString(TheKey_teamOwner) != sideName(sides, side)) {
			mcpFail("'%s' is the default team of its player and cannot change owner", name.c_str());
		}
		team->getDict()->setAsciiString(TheKey_teamOwner, sideName(sides, side));
	}

	Dict *dict = team->getDict();
	if (args.has("units")) {
		setTeamUnits(*dict, args.get("units"));
	}
	if (args.has("properties")) {
		const McpJson &props = args.get("properties");
		if (!props.isObject()) {
			mcpFail("'properties' must be an object");
		}
		for (size_t i = 0; i < props.numKeys(); i++) {
			setTeamProperty(*dict, props.keyAt(i), props.valueAt(i), allowUnknown);
		}
		// Objects and scripts refer to the team by name and the owner must be a player.
		if (dict->getAsciiString(TheKey_teamName) != teamName) {
			mcpFail("properties must not change teamName (use scripts.rename)");
		}
		if (findSide(sides, dict->getAsciiString(TheKey_teamOwner)) < 0) {
			mcpFail("teamOwner '%s' is not a player", dict->getAsciiString(TheKey_teamOwner).str());
		}
	}

	McpJson warnings = McpJson::makeArray();
	collectTeamWarnings(sides, team, scriptNamesPerSide(sides), warnings);
	McpJson j = McpJson::makeObject();
	j.set("name", name).set("created", created);
	j.set("units", describeUnits(dict, false, "", nullptr));
	j.set("properties", mcpDictToJson(*dict)).set("warnings", warnings);
	commitSides(sides);
	return j;
}

McpJson cmdDeleteTeam(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const std::string name = mcpArgString(args, "name");
	SidesList sides = *TheSidesList;
	Int index = -1;
	TeamsInfo *team = sides.findTeamInfo(AsciiString(name.c_str()), &index);
	if (team == nullptr) {
		mcpFail("no team named '%s' (see teams.list)", name.c_str());
	}
	if (sides.isPlayerDefaultTeam(team)) {
		mcpFail("'%s' is the default team of its player and cannot be deleted", name.c_str());
	}
	const Int owned = MapObject::countMapObjectsWithOwner(AsciiString(name.c_str()));
	if (owned > 0 && !mcpArgBool(args, "force", false)) {
		mcpFail("%d map object(s) belong to team '%s'; give them another owner first or pass force=true", owned, name.c_str());
	}
	sides.removeTeam(index);
	commitSides(sides);
	return McpJson::makeObject().set("deleted", name).set("objects_owned", owned);
}

//-------------------------------------------------------------------------------------------------
// teams.set_many
//-------------------------------------------------------------------------------------------------

/// The keys of a team a bulk change names: one key, or with a trailing '*' every numbered key
/// of that prefix the team has ("teamUnitMaxCount*").
std::vector<NameKeyType> matchTeamKeys(Dict *d, const std::string &pattern)
{
	std::vector<NameKeyType> keys;
	if (pattern.empty() || pattern[pattern.size() - 1] != '*') {
		const NameKeyType key = TheNameKeyGenerator->nameToKey(pattern.c_str());
		if (d->getType(key) != Dict::DICT_NONE) keys.push_back(key);
		return keys;
	}
	const std::string prefix = pattern.substr(0, pattern.size() - 1);
	for (Int k = 0; k < d->getPairCount(); k++) {
		const AsciiString name = TheNameKeyGenerator->keyToName(d->getNthKey(k));
		if (strncmp(name.str(), prefix.c_str(), prefix.size()) == 0 && findTeamKey(name.str())) keys.push_back(d->getNthKey(k));
	}
	return keys;
}

/// Adds to or scales the numeric properties named in `ops` ({"key or prefix*": number}).
void applyTeamNumbers(Dict *d, const McpJson &ops, bool multiply, const char *argName)
{
	if (ops.isNull()) return;
	if (!ops.isObject()) {
		mcpFail("'%s' must be an object of {\"property\": number}", argName);
	}
	for (size_t i = 0; i < ops.numKeys(); i++) {
		const std::string &pattern = ops.keyAt(i);
		if (!ops.valueAt(i).isNumber()) {
			mcpFail("%s.%s must be a number", argName, pattern.c_str());
		}
		const double amount = ops.valueAt(i).asNumber();
		std::string probe = pattern;
		if (!probe.empty() && probe[probe.size() - 1] == '*') probe = probe.substr(0, probe.size() - 1) + "1";
		const TeamKeyInfo *info = findTeamKey(probe.c_str());
		if (info == nullptr || (info->type != Dict::DICT_INT && info->type != Dict::DICT_REAL)) {
			mcpFail("%s: '%s' is not a numeric team property (see teams.list_properties)", argName, pattern.c_str());
		}
		const std::vector<NameKeyType> keys = matchTeamKeys(d, pattern);
		for (size_t k = 0; k < keys.size(); k++) {
			if (d->getType(keys[k]) == Dict::DICT_INT) {
				const double value = multiply ? d->getInt(keys[k]) * amount : d->getInt(keys[k]) + amount;
				// Counts and priorities are whole numbers; a scaled 1 should not become 0 by truncation.
				d->setInt(keys[k], (Int)floor(value + 0.5));
			} else if (d->getType(keys[k]) == Dict::DICT_REAL) {
				d->setReal(keys[k], (Real)(multiply ? d->getReal(keys[k]) * amount : d->getReal(keys[k]) + amount));
			}
		}
	}
}

/// What differs between two versions of a team dict, as {key: [old, new]}.
McpJson dictChanges(const Dict &before, const Dict &after)
{
	const McpJson a = mcpDictToJson(before);
	const McpJson b = mcpDictToJson(after);
	McpJson changes = McpJson::makeObject();
	for (size_t i = 0; i < b.numKeys(); i++) {
		const McpJson &old = a.get(b.keyAt(i).c_str());
		if (old.dump() != b.valueAt(i).dump()) {
			McpJson pair = McpJson::makeArray();
			pair.push(old);
			pair.push(b.valueAt(i));
			changes.set(b.keyAt(i).c_str(), pair);
		}
	}
	for (size_t i = 0; i < a.numKeys(); i++) {
		if (!b.has(a.keyAt(i).c_str())) {
			McpJson pair = McpJson::makeArray();
			pair.push(a.valueAt(i));
			pair.push(McpJson());
			changes.set(a.keyAt(i).c_str(), pair);
		}
	}
	return changes;
}

/// Changes many teams at once: the same filters as teams.list pick them.
McpJson cmdSetManyTeams(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	SidesList sides = *TheSidesList;
	TeamFilter filter;
	filter.read(sides, args);
	if (!filter.hasAny() && !mcpArgBool(args, "all_teams", false)) {
		mcpFail("pass a filter (owner, filter, unit or script), or all_teams=true to change every team of the map");
	}
	const bool allowUnknown = mcpArgBool(args, "allow_unknown_properties", false);
	const bool dryRun = mcpArgBool(args, "dry_run", false);
	const McpJson &props = args.get("properties");
	if (!props.isNull() && !props.isObject()) {
		mcpFail("'properties' must be an object");
	}
	if (props.has("teamName") || props.has("teamOwner")) {
		mcpFail("teamName and teamOwner cannot be set for many teams at once");
	}
	if (props.isNull() && args.get("multiply").isNull() && args.get("add").isNull()) {
		mcpFail("pass properties, multiply or add");
	}

	McpJson samples = McpJson::makeArray();
	Int matched = 0, changed = 0;
	const std::vector<std::set<std::string> > scriptNames = scriptNamesPerSide(sides);
	McpJson warnings = McpJson::makeArray();
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		if (!filter.matches(sides, team)) continue;
		matched++;
		Dict *d = team->getDict();
		const Dict before = *d;
		for (size_t k = 0; k < props.numKeys(); k++) {
			setTeamProperty(*d, props.keyAt(k), props.valueAt(k), allowUnknown);
		}
		applyTeamNumbers(d, args.get("multiply"), true, "multiply");
		applyTeamNumbers(d, args.get("add"), false, "add");
		const McpJson changes = dictChanges(before, *d);
		if (changes.numKeys() == 0) continue;
		changed++;
		if (samples.size() < 15) {
			samples.push(McpJson::makeObject().set("team", d->getAsciiString(TheKey_teamName).str()).set("changes", changes));
		}
		collectTeamWarnings(sides, team, scriptNames, warnings);
	}

	McpJson j = McpJson::makeObject();
	j.set("teams_matched", matched).set("teams_changed", changed).set("samples", samples);
	j.set("warnings_on_changed_teams", (int)warnings.size()).set("dry_run", dryRun);
	if (!dryRun && changed > 0) {
		commitSides(sides);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// teams.summary
//-------------------------------------------------------------------------------------------------

struct UnitUse
{
	Int teams, minTotal, maxTotal, cost;
	UnitUse() : teams(0), minTotal(0), maxTotal(0), cost(-1) {}
};

bool moreTeams(const std::pair<std::string, UnitUse> &a, const std::pair<std::string, UnitUse> &b)
{
	return a.second.teams != b.second.teams ? a.second.teams > b.second.teams : a.first < b.first;
}

McpJson countsToJson(const std::map<std::string, Int> &counts, const char *keyName, size_t limit)
{
	std::vector<std::pair<Int, std::string> > order;
	for (std::map<std::string, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
		order.push_back(std::make_pair(-it->second, it->first));
	}
	std::sort(order.begin(), order.end());
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < order.size() && i < limit; i++) {
		list.push(McpJson::makeObject().set(keyName, order[i].second).set("teams", -order[i].first));
	}
	return list;
}

/// One owner's teams in numbers: which units they use and how many, team sizes and build costs,
/// priorities and production conditions.
McpJson summarizeTeams(SidesList &sides, const AsciiString &owner, const TeamFilter &filter, Int limit)
{
	std::map<std::string, UnitUse> units;
	std::map<std::string, Int> priorities, conditions;
	Int numTeams = 0, withoutUnits = 0, withoutCondition = 0;
	Int sizeMin = INT_MAX, sizeMax = 0, sizeSum = 0, costMin = INT_MAX, costMax = 0;
	double costSum = 0;
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		Dict *d = team->getDict();
		if (d->getAsciiString(TheKey_teamOwner) != owner || !filter.matches(sides, team)) continue;
		numTeams++;
		Int teamSize = 0, teamCost = 0;
		for (Int slot = 1; slot <= NUM_UNIT_SLOTS; slot++) {
			Bool exists = false;
			const AsciiString type = d->getAsciiString(numberedKey("teamUnitType", slot), &exists);
			const Int minCount = d->getInt(numberedKey("teamUnitMinCount", slot), &exists);
			const Int maxCount = d->getInt(numberedKey("teamUnitMaxCount", slot), &exists);
			if (isNoUnit(type) || maxCount <= 0) continue;
			UnitUse &use = units[type.str()];
			if (use.teams == 0) {
				const ThingTemplate *tt = TheThingFactory->findTemplate(type, FALSE);
				use.cost = tt ? tt->friend_getBuildCost() : -1;
			}
			use.teams++;
			use.minTotal += minCount;
			use.maxTotal += maxCount;
			teamSize += maxCount;
			if (use.cost > 0) teamCost += maxCount * use.cost;
		}
		if (teamSize == 0) {
			withoutUnits++;
		} else {
			sizeMin = teamSize < sizeMin ? teamSize : sizeMin;
			sizeMax = teamSize > sizeMax ? teamSize : sizeMax;
			sizeSum += teamSize;
			costMin = teamCost < costMin ? teamCost : costMin;
			costMax = teamCost > costMax ? teamCost : costMax;
			costSum += teamCost;
		}
		Bool exists = false;
		const Int priority = d->getInt(TheKey_teamProductionPriority, &exists);
		if (exists) {
			char text[16];
			sprintf(text, "%d", priority);
			priorities[text]++;
		}
		const AsciiString condition = d->getAsciiString(TheKey_teamProductionCondition, &exists);
		if (exists && !condition.isEmpty()) {
			conditions[condition.str()]++;
		} else {
			withoutCondition++;
		}
	}

	std::vector<std::pair<std::string, UnitUse> > order(units.begin(), units.end());
	std::sort(order.begin(), order.end(), moreTeams);
	McpJson unitList = McpJson::makeArray();
	for (size_t i = 0; i < order.size() && (Int)i < limit; i++) {
		McpJson entry = McpJson::makeObject();
		entry.set("type", order[i].first).set("teams", order[i].second.teams);
		entry.set("min_total", order[i].second.minTotal).set("max_total", order[i].second.maxTotal);
		if (order[i].second.cost >= 0) {
			entry.set("cost", order[i].second.cost);
		} else {
			entry.set("unknown_type", true);
		}
		unitList.push(entry);
	}

	const Int withUnits = numTeams - withoutUnits;
	McpJson j = McpJson::makeObject();
	j.set("owner", owner.str()).set("teams", numTeams).set("teams_without_units", withoutUnits);
	j.set("teams_without_production_condition", withoutCondition);
	if (withUnits > 0) {
		j.set("team_size", McpJson::makeObject().set("min", sizeMin).set("max", sizeMax).set("average", (double)sizeSum / withUnits));
		j.set("team_cost", McpJson::makeObject().set("min", costMin).set("max", costMax).set("average", floor(costSum / withUnits + 0.5)));
	}
	j.set("unit_types", (int)order.size()).set("units", unitList);
	j.set("priorities", countsToJson(priorities, "priority", 20));
	j.set("production_conditions", countsToJson(conditions, "script", 30));
	return j;
}

McpJson cmdSummarizeTeams(const McpJson &args)
{
	mcpDoc();
	SidesList &sides = *TheSidesList;
	TeamFilter filter;
	filter.read(sides, args);
	const Int limit = mcpArgInt(args, "limit", 40);
	McpJson owners = McpJson::makeArray();
	for (Int side = 0; side < sides.getNumSides(); side++) {
		const AsciiString owner = sideName(sides, side);
		if (filter.hasOwner && owner != filter.owner) continue;
		const McpJson summary = summarizeTeams(sides, owner, filter, limit);
		// Without an owner filter, players that have no teams of their own are left out.
		if (!filter.hasOwner && summary.get("teams").asNumber() == 0) continue;
		owners.push(summary);
	}
	return McpJson::makeObject().set("owners", owners);
}

McpJson cmdVerifyTeams(const McpJson &args)
{
	mcpDoc();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("owner") ? requireSide(sides, mcpArgString(args, "owner")) : -1;
	Int checked = 0;
	const McpJson warnings = verifyTeams(sides, onlySide, mcpArgString(args, "name", ""), &checked);
	return McpJson::makeObject().set("teams_checked", checked).set("warnings", warnings);
}

} // namespace

void McpScripts::setTeamPropertyChecked(Dict &dict, const std::string &key, const McpJson &value, bool allowUnknown)
{
	setTeamProperty(dict, key, value, allowUnknown);
}

const char *McpScripts::teamPropertyRef(const char *key)
{
	const TeamKeyInfo *info = findTeamKey(key);
	return info ? teamRefName(info->ref) : nullptr;
}

McpJson McpScripts::verifyTeams(SidesList &sides, Int onlySide, const std::string &onlyName, Int *checked)
{
	const std::vector<std::set<std::string> > scriptNames = scriptNamesPerSide(sides);
	McpJson warnings = McpJson::makeArray();
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		Dict *d = team->getDict();
		if (onlySide >= 0 && d->getAsciiString(TheKey_teamOwner) != sideName(sides, onlySide)) continue;
		if (!onlyName.empty() && onlyName != d->getAsciiString(TheKey_teamName).str()) continue;
		collectTeamWarnings(sides, team, scriptNames, warnings);
		(*checked)++;
	}
	return warnings;
}

void mcpRegisterTeamCommands()
{
	mcpRegisterCommand("teams.list", cmdListTeams, "{owner?,filter?,unit?,script?,include_default_teams?=false,limit?=100,offset?} Teams in short form: units with counts, priority and the scripts they name.");
	mcpRegisterCommand("teams.list_properties", cmdListTeamProperties, "Team properties the game reads, with type, meaning and what they name.");
	mcpRegisterCommand("teams.get", cmdGetTeam, "{name} One team: units and all its properties.");
	mcpRegisterCommand("teams.set", cmdSetTeam, "{name,owner?,units?:[{type,min,max}],properties?,allow_unknown_properties?} Creates a team (owner required) or changes it; property types are checked.");
	mcpRegisterCommand("teams.delete", cmdDeleteTeam, "{name,force?=false} Deletes a team.");
	mcpRegisterCommand("teams.set_many", cmdSetManyTeams, "{owner?,filter?,unit?,script?,all_teams?,properties?,multiply?:{key|prefix*:factor},add?:{...},dry_run?} Changes every matching team.");
	mcpRegisterCommand("teams.summary", cmdSummarizeTeams, "{owner?,filter?,unit?,script?,limit?=40} Per owner: unit usage with costs, team sizes and costs, priorities, production conditions.");
	mcpRegisterCommand("teams.verify", cmdVerifyTeams, "{owner?,name?} Checks team properties: wrong types, unknown keys, missing unit types, scripts and waypoints.");
}
