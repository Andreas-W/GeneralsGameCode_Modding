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

// McpCmdScriptTools.cpp
// MCP bridge commands that help writing whole script sets: the values a parameter accepts,
// copying scripts and teams, cloning a player's set, renaming things with their references,
// batches, a JSON dump of everything, player editing and a search for dead logic.

#include "StdAfx.h"
#include "resource.h"
#include "WorldBuilder.h"

#include "mcp/McpCommands.h"
#include "mcp/McpScriptsShared.h"

#include "EditParameter.h"
#include "WorldBuilderDoc.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

#include <algorithm>
#include <ctype.h>
#include <limits.h>
#include <map>
#include <math.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace McpScripts;

namespace
{

//-------------------------------------------------------------------------------------------------
// scripts.list_values
//-------------------------------------------------------------------------------------------------

/// Fills a combo box the way the parameter dialog of the script editor does for a type.
/// Returns false for types the dialog does not offer a list for.
/// The lists are protected helpers of the dialog, hence the derived class; it is never created.
class EditorChoices : public EditParameter
{
public:
	static bool load(Parameter::ParameterType type, CComboBox *combo)
	{
		switch (type) {
			case Parameter::SCRIPT: loadScripts(combo, false); return true;
			case Parameter::SCRIPT_SUBROUTINE: loadScripts(combo, true); return true;
			case Parameter::ATTACK_PRIORITY_SET: loadAttackPrioritySets(combo); return true;
			case Parameter::WAYPOINT: loadWaypoints(combo); return true;
			case Parameter::WAYPOINT_PATH: loadWaypointPaths(combo); return true;
			case Parameter::TRIGGER_AREA: loadTriggerAreas(combo); return true;
			case Parameter::COMMAND_BUTTON: loadCommandButtons(combo); return true;
			case Parameter::FONT_NAME: loadFontNames(combo); return true;
			case Parameter::TEAM: loadTeams(combo); return true;
			case Parameter::UNIT: loadUnits(combo); return true;
			case Parameter::BRIDGE: loadBridges(combo); return true;
			case Parameter::OBJECT_TYPE: loadObjectType(combo); return true;
			case Parameter::OBJECT_TYPE_LIST: loadObjectTypeList(combo); return true;
			case Parameter::SIDE: loadSides(combo); return true;
			case Parameter::COUNTER: loadCounters(combo); return true;
			case Parameter::FLAG: loadFlags(combo); return true;
			case Parameter::SOUND:
			case Parameter::DIALOG:
			case Parameter::MUSIC: loadAudioType(type, combo); return true;
			case Parameter::MOVIE: loadMovies(combo); return true;
			case Parameter::SPECIAL_POWER: loadSpecialPowers(combo); return true;
			case Parameter::SCIENCE: loadSciences(combo); return true;
			case Parameter::SCIENCE_AVAILABILITY: loadScienceAvailabilities(combo); return true;
			case Parameter::UPGRADE: loadUpgrades(combo); return true;
			// The dialog narrows abilities to one unit; without a unit all of them are valid.
			case Parameter::COMMANDBUTTON_ABILITY:
			case Parameter::COMMANDBUTTON_ALL_ABILITIES: loadAllAbilities(combo); return true;
			case Parameter::EMOTICON: loadEmoticons(combo); return true;
			case Parameter::REVEALNAME: loadRevealNames(combo); return true;
			case Parameter::OBJECT_PANEL_FLAG: loadObjectFlags(combo); return true;
			case Parameter::LOCALIZED_TEXT: loadLocalizedText(combo); return true;
			default: return false;
		}
	}
};

/// How to write a value of a type that has no list of choices.
const char *freeFormHelp(Parameter::ParameterType type)
{
	switch (type) {
		case Parameter::INT: return "whole number";
		case Parameter::REAL: return "number";
		case Parameter::ANGLE: return "number, degrees";
		case Parameter::PERCENT: return "number, percent (0..100)";
		case Parameter::BOOLEAN: return "true or false";
		case Parameter::COORD3D: return "[x, y, z] in world units";
		case Parameter::COLOR: return "\"#RRGGBB\" or \"#AARRGGBB\"";
		case Parameter::BOUNDARY: return "map boundary index, 0 = first";
		case Parameter::TEXT_STRING: return "any text";
		case Parameter::TEAM_STATE: return "any text; a state name the scripts agree on";
		default: return nullptr;
	}
}

/// The values the script editor accepts for a parameter type. Returns false for a type
/// without a list.
bool collectEditorValues(Parameter::ParameterType type, std::vector<std::string> &values)
{
	namedIntNameList(type, values);
	if (values.empty()) {
		switch (type) {
			case Parameter::OBJECT_STATUS:
				for (Int i = 0; i < OBJECT_STATUS_COUNT && ObjectStatusMaskType::getBitNames()[i]; i++) {
					values.push_back(ObjectStatusMaskType::getBitNames()[i]);
				}
				break;
			case Parameter::SKIRMISH_WAYPOINT_PATH: {
				static const char *const PATHS[] = { "Center", "Backdoor", "Flank", "Special", "Naval", "NavalFlank" };
				values.assign(PATHS, PATHS + ARRAY_SIZE(PATHS));
				break;
			}
			case Parameter::FACTION_NAME: {
				AsciiStringList sideList;
				ThePlayerTemplateStore->getAllSideStrings(&sideList);
				for (AsciiStringListIterator it = sideList.begin(); it != sideList.end(); ++it) {
					values.push_back(it->str());
				}
				break;
			}
			default: {
				// The editor's own lists go through a combo box, so borrow a hidden one.
				requireScriptEditorClosed();
				CComboBox combo;
				if (!combo.Create(WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, CRect(0, 0, 10, 200), AfxGetMainWnd(), 0xE7F0)) {
					mcpFail("could not create the list for %s", parameterTypeName(type));
				}
				bool loaded;
				{
					ParameterCheckScope scope(TheSidesList);
					loaded = EditorChoices::load(type, &combo);
				}
				for (Int i = 0; loaded && i < combo.GetCount(); i++) {
					CString text;
					combo.GetLBText(i, text);
					values.push_back((LPCTSTR)text);
				}
				combo.DestroyWindow();
				return loaded;
			}
		}
	}
	return true;
}

/// The values the script editor offers for a parameter type, e.g. every upgrade or science.
McpJson cmdListValues(const McpJson &args)
{
	mcpDoc();
	const std::string typeName = mcpArgString(args, "param_type");
	const Int typeIndex = parameterTypeFromName(typeName.c_str());
	if (typeIndex < 0) {
		mcpFail("unknown param_type '%s' (the types are listed by scripts.list_actions / scripts.list_conditions)", typeName.c_str());
	}
	const Parameter::ParameterType type = (Parameter::ParameterType)typeIndex;
	const std::string filter = mcpArgString(args, "filter", "");
	const Int limit = mcpArgInt(args, "limit", 100);
	const Int offset = mcpArgInt(args, "offset", 0);

	McpJson j = McpJson::makeObject();
	j.set("param_type", parameterTypeName(type));
	if (const char *help = freeFormHelp(type)) {
		return j.set("free_form", help);
	}

	std::vector<std::string> values;
	if (!collectEditorValues(type, values)) {
		mcpFail("no value list for %s", parameterTypeName(type));
	}

	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (size_t i = 0; i < values.size(); i++) {
		if (!mcpContainsNoCase(values[i].c_str(), filter.c_str())) continue;
		matched++;
		if (matched <= offset || (Int)list.size() >= limit) continue;
		list.push(McpJson(values[i]));
	}
	return j.set("total_matches", matched).set("values", list);
}

//-------------------------------------------------------------------------------------------------
// Copying
//-------------------------------------------------------------------------------------------------

Int argIndexOrEnd(const McpJson &args)
{
	const Int index = mcpArgInt(args, "index", INT_MAX);
	if (index < 0) {
		mcpFail("index must be >= 0");
	}
	return index;
}

ScriptGroup *findGroupInSide(SidesList &sides, Int side, const std::string &name)
{
	ScriptList *list = sides.getSideInfo(side)->getScriptList();
	for (ScriptGroup *group = list ? list->getScriptGroup() : nullptr; group; group = group->getNext()) {
		if (name == group->getName().str()) return group;
	}
	return nullptr;
}

McpJson cmdCopyScript(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const std::string newName = mcpArgString(args, "new_name");
	if (newName.empty()) {
		mcpFail("new_name must not be empty");
	}
	const Int index = argIndexOrEnd(args);
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const ScriptLoc loc = requireScript(sides, args);

	// The copy goes next to the original unless a destination is given.
	Int side = args.has("to_player") ? requireSide(sides, mcpArgString(args, "to_player")) : loc.side;
	ScriptGroup *group = (side == loc.side) ? loc.group : nullptr;
	if (args.has("to_group")) {
		const std::string groupName = mcpArgString(args, "to_group");
		group = nullptr;
		if (!groupName.empty()) {
			group = findGroupInSide(sides, side, groupName);
			if (group == nullptr) {
				mcpFail("player '%s' has no script group named '%s'", sideName(sides, side).str(), groupName.c_str());
			}
		}
	}
	if (scriptNameExists(sides, side, newName)) {
		mcpFail("that player already has a script named '%s'", newName.c_str());
	}

	Script *copy = loc.script->duplicate();
	copy->setName(AsciiString(newName.c_str()));
	if (group) {
		group->addScript(copy, index);
	} else {
		requireScriptList(sides, side)->addScript(copy, index);
	}
	McpJson j = McpJson::makeObject();
	j.set("player", sideName(sides, side).str());
	if (group) j.set("group", group->getName().str());
	j.set("name", newName);
	commitSides(sides);
	return j;
}

McpJson cmdCopyTeam(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const std::string name = mcpArgString(args, "name");
	const std::string newName = mcpArgString(args, "new_name");
	if (newName.empty()) {
		mcpFail("new_name must not be empty");
	}
	SidesList sides = *TheSidesList;
	TeamsInfo *team = sides.findTeamInfo(AsciiString(name.c_str()));
	if (team == nullptr) {
		mcpFail("no team named '%s' (see teams.list)", name.c_str());
	}
	if (sides.findTeamInfo(AsciiString(newName.c_str()))) {
		mcpFail("a team named '%s' already exists", newName.c_str());
	}
	Dict d = *team->getDict();
	d.setAsciiString(TheKey_teamName, AsciiString(newName.c_str()));
	d.setBool(TheKey_teamIsSingleton, false);
	if (args.has("owner")) {
		d.setAsciiString(TheKey_teamOwner, sideName(sides, requireSide(sides, mcpArgString(args, "owner"))));
	}
	sides.addTeam(&d);
	McpJson j = McpJson::makeObject();
	j.set("name", newName).set("owner", d.getAsciiString(TheKey_teamOwner).str());
	commitSides(sides);
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.clone_player
//-------------------------------------------------------------------------------------------------

/// Ordered substring replacements, applied one after the other.
struct Replacements
{
	std::vector<std::pair<std::string, std::string> > pairs;
	std::vector<Int> counts;

	void read(const McpJson &json)
	{
		if (json.isNull()) return;
		if (!json.isObject()) {
			mcpFail("'replace' must be an object of {\"old text\": \"new text\"}");
		}
		for (size_t i = 0; i < json.numKeys(); i++) {
			if (json.keyAt(i).empty() || !json.valueAt(i).isString()) {
				mcpFail("'replace' entries need a non-empty key and a string value");
			}
			pairs.push_back(std::make_pair(json.keyAt(i), json.valueAt(i).asString()));
			counts.push_back(0);
		}
	}

	AsciiString apply(const AsciiString &in)
	{
		std::string text = in.str();
		for (size_t i = 0; i < pairs.size(); i++) {
			const std::string &from = pairs[i].first;
			const std::string &to = pairs[i].second;
			size_t pos = 0;
			while ((pos = text.find(from, pos)) != std::string::npos) {
				text.replace(pos, from.size(), to);
				pos += to.size();
				counts[i]++;
			}
		}
		return AsciiString(text.c_str());
	}
};

void transformScript(Script *script, Replacements &replacements, const AsciiString &fromPlayer, const AsciiString &toPlayer)
{
	script->setName(replacements.apply(script->getName()));
	std::vector<ParamUse> uses;
	collectParamUses(script, uses);
	for (size_t i = 0; i < uses.size(); i++) {
		Parameter *parm = uses[i].parm;
		if (!isStringParamType(parm->getParameterType())) continue;
		if (parm->getParameterType() == Parameter::SIDE && !fromPlayer.isEmpty() && parm->getString() == fromPlayer) {
			parm->friend_setString(toPlayer);
			continue;
		}
		parm->friend_setString(replacements.apply(parm->getString()));
	}
}

/// Copies all scripts, groups and teams of one player to another, rewriting names on the way.
/// This is how a faction's AI is derived from an existing one.
McpJson cmdClonePlayer(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const Int from = requireSide(sides, mcpArgString(args, "from"));
	const Int to = requireSide(sides, mcpArgString(args, "to"));
	if (from == to) {
		mcpFail("'from' and 'to' are the same player");
	}
	Replacements replacements;
	replacements.read(args.get("replace"));
	const std::string existing = mcpArgString(args, "existing", "fail");
	if (existing != "fail" && existing != "replace" && existing != "append") {
		mcpFail("existing must be fail, replace or append");
	}
	const bool includeTeams = mcpArgBool(args, "include_teams", true);
	const bool dryRun = mcpArgBool(args, "dry_run", false);
	const AsciiString fromName = sideName(sides, from);
	const AsciiString toName = sideName(sides, to);

	ScriptList *source = requireScriptList(sides, from);
	ScriptList *target = requireScriptList(sides, to);
	if (target->getScript() || target->getScriptGroup()) {
		if (existing == "fail") {
			mcpFail("player '%s' already has scripts; pass existing=\"replace\" to delete them first or \"append\" to keep them", toName.str());
		}
		if (existing == "replace") {
			while (target->getScript()) target->deleteScript(target->getScript());
			while (target->getScriptGroup()) target->deleteGroup(target->getScriptGroup());
		}
	}

	Int numScripts = 0, numGroups = 0, numTeams = 0;
	for (Script *script = source->getScript(); script; script = script->getNext()) {
		Script *copy = script->duplicate();
		transformScript(copy, replacements, fromName, toName);
		target->addScript(copy, INT_MAX);
		numScripts++;
	}
	for (ScriptGroup *group = source->getScriptGroup(); group; group = group->getNext()) {
		ScriptGroup *copy = group->duplicate();
		copy->setName(replacements.apply(copy->getName()));
		for (Script *script = copy->getScript(); script; script = script->getNext()) {
			transformScript(script, replacements, fromName, toName);
			numScripts++;
		}
		target->addGroup(copy, INT_MAX);
		numGroups++;
	}

	std::vector<std::string> collisions;
	if (includeTeams) {
		if (existing == "replace") {
			for (Int i = sides.getNumTeams() - 1; i >= 0; i--) {
				TeamsInfo *team = sides.getTeamInfo(i);
				if (team->getDict()->getAsciiString(TheKey_teamOwner) == toName && !sides.isPlayerDefaultTeam(team)) {
					sides.removeTeam(i);
				}
			}
		}
		// Adding teams can move the team storage, so copy the dicts first.
		std::vector<Dict> newTeams;
		for (Int i = 0; i < sides.getNumTeams(); i++) {
			TeamsInfo *team = sides.getTeamInfo(i);
			if (team->getDict()->getAsciiString(TheKey_teamOwner) != fromName || sides.isPlayerDefaultTeam(team)) continue;
			Dict d = *team->getDict();
			for (Int k = 0; k < d.getPairCount(); k++) {
				if (d.getNthType(k) != Dict::DICT_ASCIISTRING) continue;
				const NameKeyType key = d.getNthKey(k);
				if (key == TheKey_teamOwner) continue;
				d.setAsciiString(key, replacements.apply(d.getNthAsciiString(k)));
			}
			d.setAsciiString(TheKey_teamOwner, toName);
			newTeams.push_back(d);
		}
		std::set<std::string> newNames;
		for (size_t i = 0; i < newTeams.size(); i++) {
			const AsciiString name = newTeams[i].getAsciiString(TheKey_teamName);
			if (sides.findTeamInfo(name) || !newNames.insert(name.str()).second) {
				collisions.push_back(name.str());
			}
		}
		if (!collisions.empty()) {
			std::string sample;
			for (size_t i = 0; i < collisions.size() && i < 3; i++) {
				sample += (i ? ", '" : "'") + collisions[i] + "'";
			}
			mcpFail("%d team name(s) would exist twice (%s). Team names are unique in a map, so 'replace' has to change them, e.g. {\"SCM \": \"XYZ \"}",
				(int)collisions.size(), sample.c_str());
		}
		for (size_t i = 0; i < newTeams.size(); i++) {
			sides.addTeam(&newTeams[i]);
			numTeams++;
		}
	}

	// What still points at something that does not exist is the list of things left to adapt.
	McpJson scriptWarnings = McpJson::makeArray();
	{
		ParameterCheckScope scope(&sides);
		const std::vector<ScriptLoc> all = allScripts(sides);
		for (size_t i = 0; i < all.size(); i++) {
			if (all[i].side == to) collectScriptWarnings(sides, to, all[i].script, scriptWarnings);
		}
	}
	Int teamsChecked = 0;
	const McpJson teamWarnings = includeTeams ? verifyTeams(sides, to, "", &teamsChecked) : McpJson::makeArray();

	McpJson applied = McpJson::makeObject();
	for (size_t i = 0; i < replacements.pairs.size(); i++) {
		applied.set(replacements.pairs[i].first.c_str(), replacements.counts[i]);
	}
	McpJson j = McpJson::makeObject();
	j.set("from", fromName.str()).set("to", toName.str());
	j.set("scripts", numScripts).set("groups", numGroups).set("teams", numTeams);
	j.set("replacements_applied", applied);
	j.set("script_warnings", (int)scriptWarnings.size()).set("team_warnings", (int)teamWarnings.size());
	j.set("dry_run", dryRun);
	if (!dryRun) {
		commitSides(sides);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.rename
//-------------------------------------------------------------------------------------------------

/// Replaces an exact value in the team properties of one kind ("script", "object_type", ...).
Int renameInTeams(SidesList &sides, Int onlySide, const char *refKind, const AsciiString &from, const AsciiString &to)
{
	Int changed = 0;
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		Dict *d = sides.getTeamInfo(i)->getDict();
		if (onlySide >= 0 && d->getAsciiString(TheKey_teamOwner) != sideName(sides, onlySide)) continue;
		// Collect first: setting a value re-sorts the pairs.
		std::vector<NameKeyType> keys;
		for (Int k = 0; k < d->getPairCount(); k++) {
			if (d->getNthType(k) != Dict::DICT_ASCIISTRING || d->getNthAsciiString(k) != from) continue;
			const char *ref = teamPropertyRef(TheNameKeyGenerator->keyToName(d->getNthKey(k)).str());
			if (ref && strcmp(ref, refKind) == 0) keys.push_back(d->getNthKey(k));
		}
		for (size_t k = 0; k < keys.size(); k++) {
			d->setAsciiString(keys[k], to);
			changed++;
		}
	}
	return changed;
}

Int renameInParams(SidesList &sides, Int onlySide, Parameter::ParameterType typeA, Parameter::ParameterType typeB,
	const AsciiString &from, const AsciiString &to)
{
	Int changed = 0;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		if (onlySide >= 0 && all[i].side != onlySide) continue;
		std::vector<ParamUse> uses;
		collectParamUses(all[i].script, uses);
		for (size_t k = 0; k < uses.size(); k++) {
			Parameter *parm = uses[k].parm;
			if (parm->getParameterType() != typeA && parm->getParameterType() != typeB) continue;
			if (parm->getString() != from) continue;
			parm->friend_setString(to);
			changed++;
		}
	}
	return changed;
}

/// Renames a script, group, team or any named value (flag, counter, object type, ...) together
/// with everything that refers to it.
McpJson cmdRename(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const std::string kind = mcpArgString(args, "kind");
	const std::string fromText = mcpArgString(args, "from");
	const std::string toText = mcpArgString(args, "to");
	if (fromText.empty() || toText.empty() || fromText == toText) {
		mcpFail("'from' and 'to' must be two different, non-empty names");
	}
	const AsciiString from(fromText.c_str());
	const AsciiString to(toText.c_str());
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;

	Int renamed = 0, inScripts = 0, inTeams = 0;
	if (kind == "script" || kind == "group") {
		// Scripts and groups share one name space: a group can be called like a subroutine.
		for (Int side = 0; side < sides.getNumSides(); side++) {
			if (onlySide >= 0 && side != onlySide) continue;
			Bool hasFrom = false;
			const std::vector<ScriptLoc> all = allScripts(sides);
			if (kind == "script") {
				for (size_t i = 0; i < all.size(); i++) {
					if (all[i].side == side && all[i].script->getName() == from) hasFrom = true;
				}
			} else {
				hasFrom = findGroupInSide(sides, side, fromText) != nullptr;
			}
			if (!hasFrom) continue;
			if (scriptNameExists(sides, side, toText) || findGroupInSide(sides, side, toText)) {
				mcpFail("player '%s' already has a script or group named '%s'", sideName(sides, side).str(), toText.c_str());
			}
			if (kind == "script") {
				for (size_t i = 0; i < all.size(); i++) {
					if (all[i].side == side && all[i].script->getName() == from) {
						all[i].script->setName(to);
						renamed++;
					}
				}
			} else {
				findGroupInSide(sides, side, fromText)->setName(to);
				renamed++;
			}
			inScripts += renameInParams(sides, side, Parameter::SCRIPT, Parameter::SCRIPT_SUBROUTINE, from, to);
			inTeams += renameInTeams(sides, side, "script", from, to);
		}
		if (renamed == 0) {
			mcpFail("no %s named '%s'%s", kind.c_str(), fromText.c_str(), onlySide >= 0 ? " for that player" : "");
		}
	} else if (kind == "team") {
		TeamsInfo *team = sides.findTeamInfo(from);
		if (team == nullptr) {
			mcpFail("no team named '%s'", fromText.c_str());
		}
		if (sides.isPlayerDefaultTeam(team)) {
			mcpFail("'%s' is the default team of its player; it is named after the player", fromText.c_str());
		}
		if (sides.findTeamInfo(to)) {
			mcpFail("a team named '%s' already exists", toText.c_str());
		}
		const Int owned = MapObject::countMapObjectsWithOwner(from);
		if (owned > 0) {
			mcpFail("%d map object(s) belong to team '%s'; give them another owner first (objects.set_properties)", owned, fromText.c_str());
		}
		team->getDict()->setAsciiString(TheKey_teamName, to);
		renamed = 1;
		inScripts = renameInParams(sides, -1, Parameter::TEAM, Parameter::TEAM, from, to);
	} else if (kind == "script_reference" || kind == "team_reference") {
		// Points references somewhere else without renaming anything: for names that do not exist.
		if (kind == "script_reference") {
			inScripts = renameInParams(sides, onlySide, Parameter::SCRIPT, Parameter::SCRIPT_SUBROUTINE, from, to);
			inTeams = renameInTeams(sides, onlySide, "script", from, to);
		} else {
			inScripts = renameInParams(sides, onlySide, Parameter::TEAM, Parameter::TEAM, from, to);
		}
		if (inScripts + inTeams == 0) {
			mcpFail("nothing refers to '%s'%s", fromText.c_str(), onlySide >= 0 ? " for that player" : "");
		}
	} else {
		const Int typeIndex = parameterTypeFromName(kind.c_str());
		if (typeIndex < 0 || !isStringParamType((Parameter::ParameterType)typeIndex)) {
			mcpFail("kind must be script, group, team, script_reference, team_reference or a parameter type that holds a name (FLAG, COUNTER, OBJECT_TYPE, WAYPOINT, UPGRADE, ...)");
		}
		const Parameter::ParameterType type = (Parameter::ParameterType)typeIndex;
		if (type == Parameter::SCRIPT || type == Parameter::SCRIPT_SUBROUTINE || type == Parameter::TEAM) {
			mcpFail("use kind script, group or team to rename those, or script_reference / team_reference to only retarget references");
		}
		inScripts = renameInParams(sides, onlySide, type, type, from, to);
		// Teams name unit types and waypoints too.
		if (type == Parameter::OBJECT_TYPE) inTeams = renameInTeams(sides, onlySide, "object_type", from, to);
		if (type == Parameter::WAYPOINT) inTeams = renameInTeams(sides, onlySide, "waypoint", from, to);
		if (inScripts + inTeams == 0) {
			mcpFail("nothing uses %s '%s'%s", parameterTypeName(type), fromText.c_str(), onlySide >= 0 ? " for that player" : "");
		}
	}

	McpJson j = McpJson::makeObject();
	j.set("kind", kind).set("from", fromText).set("to", toText);
	j.set("renamed", renamed).set("references_in_scripts", inScripts).set("references_in_teams", inTeams);
	commitSides(sides);
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.batch
//-------------------------------------------------------------------------------------------------

/// Runs several script and team commands as one undo step. If one fails, none is applied.
McpJson cmdBatch(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	(void)doc;
	requireScriptEditorClosed();
	const McpJson &commands = mcpArgArray(args, "commands");
	const bool full = mcpArgString(args, "results", "summary") == "full";
	for (size_t i = 0; i < commands.size(); i++) {
		const McpJson &entry = commands.at(i);
		if (!entry.isObject() || !entry.get("cmd").isString()) {
			mcpFail("commands[%d] must be {\"cmd\": \"scripts.add\", \"args\": {...}}", (int)i);
		}
		const std::string &cmd = entry.get("cmd").asString();
		// Only commands that go through the sides list can be rolled back together.
		const bool allowed = (cmd.compare(0, 8, "scripts.") == 0 || cmd.compare(0, 6, "teams.") == 0
			|| cmd == "sides.set" || cmd == "sides.delete") && cmd != "scripts.batch";
		if (!allowed) {
			mcpFail("commands[%d]: '%s' cannot run in a batch (only scripts.*, teams.*, sides.set and sides.delete)", (int)i, cmd.c_str());
		}
	}

	ScriptList::updateDefaults();
	const SidesList before = *TheSidesList;
	McpJson results = McpJson::makeArray();
	McpJson warnings = McpJson::makeArray();
	setSidesBatch(true);
	size_t i = 0;
	try {
		for (; i < commands.size(); i++) {
			const McpJson &entry = commands.at(i);
			McpJson commandArgs = entry.get("args");
			if (commandArgs.isNull()) commandArgs = McpJson::makeObject();
			const McpJson result = mcpRunCommand(entry.get("cmd").asString(), commandArgs);
			if (full) {
				results.push(result);
			}
			const McpJson &commandWarnings = result.get("warnings");
			for (size_t w = 0; w < commandWarnings.size(); w++) {
				McpJson warning = commandWarnings.at(w);
				if (warning.isObject()) warning.set("command", (int)i);
				warnings.push(warning);
			}
		}
	} catch (const McpError &err) {
		setSidesBatch(false);
		*TheSidesList = before;
		mcpFail("commands[%d] (%s) failed, nothing was changed: %s", (int)i, commands.at(i).get("cmd").asString().c_str(),
			err.message.c_str());
	}
	setSidesBatch(false);

	// Hand the result to the undo system as a single step.
	SidesList after = *TheSidesList;
	*TheSidesList = before;
	commitSides(after);

	McpJson j = McpJson::makeObject();
	j.set("commands_run", (int)commands.size()).set("warnings", warnings);
	if (full) {
		j.set("results", results);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.dump / scripts.load
//-------------------------------------------------------------------------------------------------

McpJson dumpScripts(Script *first)
{
	McpJson list = McpJson::makeArray();
	for (Script *script = first; script; script = script->getNext()) {
		list.push(describeScript(script));
	}
	return list;
}

/// Everything about the scripts and teams of the map (or one player) as one JSON document,
/// in the form scripts.load reads.
McpJson cmdDumpScripts(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const bool includeTeams = mcpArgBool(args, "include_teams", true);

	McpJson players = McpJson::makeArray();
	Int numScripts = 0;
	for (Int side = 0; side < sides.getNumSides(); side++) {
		if (onlySide >= 0 && side != onlySide) continue;
		ScriptList *list = sides.getSideInfo(side)->getScriptList();
		McpJson player = McpJson::makeObject();
		player.set("player", sideName(sides, side).str());
		player.set("properties", mcpDictToJson(*sides.getSideInfo(side)->getDict()));
		const McpJson scripts = dumpScripts(list ? list->getScript() : nullptr);
		numScripts += (Int)scripts.size();
		player.set("scripts", scripts);
		McpJson groups = McpJson::makeArray();
		for (ScriptGroup *group = list ? list->getScriptGroup() : nullptr; group; group = group->getNext()) {
			const McpJson groupScripts = dumpScripts(group->getScript());
			numScripts += (Int)groupScripts.size();
			groups.push(McpJson::makeObject()
				.set("name", group->getName().str())
				.set("active", (bool)(group->isActive() != 0))
				.set("subroutine", (bool)(group->isSubroutine() != 0))
				.set("scripts", groupScripts));
		}
		player.set("groups", groups);
		players.push(player);
	}

	McpJson j = McpJson::makeObject();
	j.set("format", "worldbuilder-scripts-1").set("script_count", numScripts).set("players", players);
	if (includeTeams) {
		McpJson teams = McpJson::makeArray();
		for (Int i = 0; i < sides.getNumTeams(); i++) {
			TeamsInfo *team = sides.getTeamInfo(i);
			if (sides.isPlayerDefaultTeam(team)) continue;
			if (onlySide >= 0 && team->getDict()->getAsciiString(TheKey_teamOwner) != sideName(sides, onlySide)) continue;
			teams.push(mcpDictToJson(*team->getDict()));
		}
		j.set("teams", teams);
	}
	return j;
}

/// Builds the scripts of a JSON list into `add`, naming the script in any error.
template <class T>
Int loadScriptsInto(T *owner, const McpJson &list, const char *playerName)
{
	if (!list.isArray()) {
		mcpFail("player '%s': 'scripts' must be an array", playerName);
	}
	for (size_t i = 0; i < list.size(); i++) {
		const McpJson &def = list.at(i);
		Script *script = newInstance(Script);
		OrCondition *orCond = newInstance(OrCondition);
		orCond->setFirstAndCondition(newInstance(Condition)(Condition::CONDITION_TRUE));
		script->setOrCondition(orCond);
		owner->addScript(script, INT_MAX);
		try {
			if (!def.isObject()) mcpFail("must be an object");
			applyScriptJson(script, def);
		} catch (const McpError &err) {
			mcpFail("player '%s', script %d '%s': %s", playerName, (int)i, def.get("name").isString() ? def.get("name").asString().c_str() : "?",
				err.message.c_str());
		}
	}
	return (Int)list.size();
}

/// Builds the scripts and groups of one player entry of a dump document.
ScriptList *buildScriptList(const McpJson &entry, const char *playerName, Int *numScripts, Int *numGroups)
{
	ScriptList *list = newInstance(ScriptList);
	try {
		if (entry.has("scripts")) {
			*numScripts += loadScriptsInto(list, entry.get("scripts"), playerName);
		}
		const McpJson &groups = entry.get("groups");
		if (!groups.isNull() && !groups.isArray()) {
			mcpFail("player '%s': 'groups' must be an array", playerName);
		}
		for (size_t g = 0; g < groups.size(); g++) {
			const McpJson &groupDef = groups.at(g);
			if (!groupDef.isObject() || !groupDef.get("name").isString()) {
				mcpFail("player '%s': groups[%d] needs a 'name'", playerName, (int)g);
			}
			ScriptGroup *group = newInstance(ScriptGroup);
			group->setName(AsciiString(groupDef.get("name").asString().c_str()));
			group->setActive(mcpArgBool(groupDef, "active", true));
			group->setSubroutine(mcpArgBool(groupDef, "subroutine", false));
			list->addGroup(group, INT_MAX);
			if (groupDef.has("scripts")) {
				*numScripts += loadScriptsInto(group, groupDef.get("scripts"), playerName);
			}
			(*numGroups)++;
		}
	} catch (...) {
		deleteInstance(list);
		throw;
	}
	return list;
}

/// Replaces the scripts (and teams) of every player in a scripts.dump document with its contents.
McpJson cmdLoadScripts(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const McpJson &data = args.get("data");
	if (!data.isObject() || !data.get("players").isArray()) {
		mcpFail("'data' must be a scripts.dump document with a 'players' array");
	}
	const bool createPlayers = mcpArgBool(args, "create_players", true);
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;

	const McpJson &players = data.get("players");
	std::set<std::string> loadedPlayers;
	Int numScripts = 0, numGroups = 0, numTeams = 0, playersAdded = 0;
	for (size_t p = 0; p < players.size(); p++) {
		const McpJson &entry = players.at(p);
		if (!entry.isObject() || !entry.get("player").isString()) {
			mcpFail("players[%d] needs a 'player' name", (int)p);
		}
		const AsciiString playerName(entry.get("player").asString().c_str());
		Int side = findSide(sides, playerName);
		if (side < 0) {
			if (!createPlayers || !entry.get("properties").isObject()) {
				mcpFail("the map has no player '%s' (and the document has no 'properties' to create it from)", playerName.str());
			}
			if (sides.getNumSides() >= MAX_PLAYER_COUNT) {
				mcpFail("cannot add player '%s': the map has the maximum number of players", playerName.str());
			}
			Dict d;
			mcpJsonToDict(entry.get("properties"), d);
			d.setAsciiString(TheKey_playerName, playerName);
			sides.addSide(&d);
			side = findSide(sides, playerName);
			playersAdded++;
		}
		if (!loadedPlayers.insert(playerName.str()).second) {
			mcpFail("player '%s' is in the document twice", playerName.str());
		}

		// The new list replaces the old one only once it is complete.
		ScriptList *list = buildScriptList(entry, playerName.str(), &numScripts, &numGroups);
		deleteInstance(sides.getSideInfo(side)->getScriptList());
		sides.getSideInfo(side)->setScriptList(list);
	}

	if (data.has("teams")) {
		const McpJson &teams = data.get("teams");
		if (!teams.isArray()) {
			mcpFail("'teams' must be an array of team property objects");
		}
		// The loaded players' teams are replaced like their scripts.
		for (Int i = sides.getNumTeams() - 1; i >= 0; i--) {
			TeamsInfo *team = sides.getTeamInfo(i);
			if (sides.isPlayerDefaultTeam(team)) continue;
			if (loadedPlayers.count(team->getDict()->getAsciiString(TheKey_teamOwner).str())) {
				sides.removeTeam(i);
			}
		}
		for (size_t t = 0; t < teams.size(); t++) {
			const McpJson &props = teams.at(t);
			if (!props.isObject() || !props.get("teamName").isString() || !props.get("teamOwner").isString()) {
				mcpFail("teams[%d] needs teamName and teamOwner", (int)t);
			}
			const std::string &teamName = props.get("teamName").asString();
			if (!loadedPlayers.count(props.get("teamOwner").asString())) {
				mcpFail("team '%s': its owner '%s' is not one of the players in the document", teamName.c_str(),
					props.get("teamOwner").asString().c_str());
			}
			if (sides.findTeamInfo(AsciiString(teamName.c_str()))) {
				mcpFail("team '%s' exists twice (team names are unique in a map)", teamName.c_str());
			}
			Dict d;
			try {
				for (size_t k = 0; k < props.numKeys(); k++) {
					setTeamPropertyChecked(d, props.keyAt(k), props.valueAt(k), true);
				}
			} catch (const McpError &err) {
				mcpFail("team '%s': %s", teamName.c_str(), err.message.c_str());
			}
			sides.addTeam(&d);
			numTeams++;
		}
	}

	McpJson j = McpJson::makeObject();
	j.set("players_loaded", (int)players.size()).set("players_added", playersAdded);
	j.set("scripts", numScripts).set("groups", numGroups).set("teams", numTeams);
	commitSides(sides);
	return j;
}

//-------------------------------------------------------------------------------------------------
// sides.set / sides.delete
//-------------------------------------------------------------------------------------------------

/// Replaces one name in a space separated list of names (playerAllies, playerEnemies).
AsciiString replaceInNameList(const AsciiString &list, const AsciiString &from, const AsciiString &to)
{
	std::string out;
	std::string text = list.str();
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find(' ', pos);
		if (end == std::string::npos) end = text.size();
		std::string token = text.substr(pos, end - pos);
		if (!token.empty()) {
			if (token == from.str()) token = to.str();
			if (!token.empty()) {
				if (!out.empty()) out += " ";
				out += token;
			}
		}
		pos = end + 1;
	}
	return AsciiString(out.c_str());
}

Int objectsOwnedByPlayer(SidesList &sides, const AsciiString &playerName)
{
	Int owned = 0;
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		Dict *d = sides.getTeamInfo(i)->getDict();
		if (d->getAsciiString(TheKey_teamOwner) == playerName) {
			owned += MapObject::countMapObjectsWithOwner(d->getAsciiString(TheKey_teamName));
		}
	}
	return owned;
}

/// Changes a player's properties or its name (with the references to the name).
McpJson cmdSetSide(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "name"));
	Dict *dict = sides.getSideInfo(side)->getDict();
	const AsciiString oldName = sideName(sides, side);

	if (args.has("properties")) {
		const McpJson &props = args.get("properties");
		if (props.has("playerName")) {
			mcpFail("use new_name to rename a player; its teams and scripts refer to the name");
		}
		mcpJsonToDict(props, *dict);
	}

	Int references = 0;
	if (args.has("new_name")) {
		const AsciiString newName(mcpArgString(args, "new_name").c_str());
		if (oldName.isEmpty()) {
			mcpFail("the neutral player cannot be renamed");
		}
		if (newName.isEmpty() || findSide(sides, newName) >= 0) {
			mcpFail("the new player name must be unique and not empty");
		}
		const Int owned = objectsOwnedByPlayer(sides, oldName);
		if (owned > 0) {
			mcpFail("%d map object(s) belong to teams of '%s'; object owners are team names, so give them another owner first", owned, oldName.str());
		}
		// The default team is named after its player.
		AsciiString oldTeam, newTeam;
		oldTeam.format("team%s", oldName.str());
		newTeam.format("team%s", newName.str());
		if (sides.findTeamInfo(newTeam)) {
			mcpFail("a team named '%s' already exists", newTeam.str());
		}
		dict->setAsciiString(TheKey_playerName, newName);
		for (Int i = 0; i < sides.getNumTeams(); i++) {
			Dict *d = sides.getTeamInfo(i)->getDict();
			if (d->getAsciiString(TheKey_teamOwner) == oldName) d->setAsciiString(TheKey_teamOwner, newName);
			if (d->getAsciiString(TheKey_teamName) == oldTeam) d->setAsciiString(TheKey_teamName, newTeam);
		}
		for (Int i = 0; i < sides.getNumSides(); i++) {
			Dict *d = sides.getSideInfo(i)->getDict();
			d->setAsciiString(TheKey_playerAllies, replaceInNameList(d->getAsciiString(TheKey_playerAllies), oldName, newName));
			d->setAsciiString(TheKey_playerEnemies, replaceInNameList(d->getAsciiString(TheKey_playerEnemies), oldName, newName));
		}
		references += renameInParams(sides, -1, Parameter::SIDE, Parameter::SIDE, oldName, newName);
		references += renameInParams(sides, -1, Parameter::TEAM, Parameter::TEAM, oldTeam, newTeam);
	}

	McpJson j = McpJson::makeObject();
	j.set("player", sideName(sides, side).str()).set("references_in_scripts", references);
	j.set("properties", mcpDictToJson(*sides.getSideInfo(side)->getDict()));
	commitSides(sides);
	return j;
}

McpJson cmdDeleteSide(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "name"));
	const AsciiString name = sideName(sides, side);
	if (name.isEmpty()) {
		mcpFail("the neutral player cannot be deleted");
	}
	const Int owned = objectsOwnedByPlayer(sides, name);
	if (owned > 0) {
		mcpFail("%d map object(s) belong to teams of '%s'; give them another owner or delete them first", owned, name.str());
	}
	Int numScripts = 0, numTeams = 0;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		if (all[i].side == side) numScripts++;
	}
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		if (team->getDict()->getAsciiString(TheKey_teamOwner) == name && !sides.isPlayerDefaultTeam(team)) numTeams++;
	}
	if ((numScripts > 0 || numTeams > 0) && !mcpArgBool(args, "force", false)) {
		mcpFail("player '%s' has %d script(s) and %d team(s); pass force=true to delete them with it", name.str(), numScripts, numTeams);
	}
	for (Int i = sides.getNumTeams() - 1; i >= 0; i--) {
		if (sides.getTeamInfo(i)->getDict()->getAsciiString(TheKey_teamOwner) == name) {
			sides.removeTeam(i);
		}
	}
	sides.removeSide(side);
	for (Int i = 0; i < sides.getNumSides(); i++) {
		Dict *d = sides.getSideInfo(i)->getDict();
		d->setAsciiString(TheKey_playerAllies, replaceInNameList(d->getAsciiString(TheKey_playerAllies), name, AsciiString::TheEmptyString));
		d->setAsciiString(TheKey_playerEnemies, replaceInNameList(d->getAsciiString(TheKey_playerEnemies), name, AsciiString::TheEmptyString));
	}
	McpJson j = McpJson::makeObject();
	j.set("deleted", name.str()).set("scripts_deleted", numScripts).set("teams_deleted", numTeams);
	commitSides(sides);
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.analyze
//-------------------------------------------------------------------------------------------------

void pushCapped(McpJson &list, Int limit, const AsciiString &player, const char *name)
{
	if ((Int)list.size() < limit) {
		list.push(McpJson::makeObject().set("player", player.str()).set("name", name));
	}
}

/// Looks for dead logic that the reference checks of scripts.verify cannot see.
McpJson cmdAnalyzeScripts(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const Int limit = mcpArgInt(args, "limit", 40);

	McpJson unusedSubroutines = McpJson::makeArray(), neverEnabled = McpJson::makeArray(), noActions = McpJson::makeArray();
	McpJson flagsNeverSet = McpJson::makeArray(), flagsNeverRead = McpJson::makeArray();
	McpJson countersNeverSet = McpJson::makeArray(), countersNeverRead = McpJson::makeArray();
	McpJson emptyGroups = McpJson::makeArray(), unusedTeams = McpJson::makeArray();
	Int counts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	// Teams named by any script of the map.
	std::set<std::string> teamsNamed;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		std::vector<ParamUse> uses;
		collectParamUses(all[i].script, uses);
		for (size_t k = 0; k < uses.size(); k++) {
			if (uses[k].parm->getParameterType() == Parameter::TEAM) teamsNamed.insert(uses[k].parm->getString().str());
		}
	}

	for (Int side = 0; side < sides.getNumSides(); side++) {
		if (onlySide >= 0 && side != onlySide) continue;
		const AsciiString player = sideName(sides, side);

		// What the scripts and teams of this player call, set and read.
		std::set<std::string> called, flagsSet, flagsRead, countersSet, countersRead;
		for (size_t i = 0; i < all.size(); i++) {
			if (all[i].side != side) continue;
			std::vector<ParamUse> uses;
			collectParamUses(all[i].script, uses);
			for (size_t k = 0; k < uses.size(); k++) {
				const Parameter *parm = uses[k].parm;
				const std::string value = parm->getString().str();
				switch (parm->getParameterType()) {
					case Parameter::SCRIPT:
					case Parameter::SCRIPT_SUBROUTINE:
						called.insert(value);
						break;
					case Parameter::FLAG:
						(uses[k].inCondition ? flagsRead : flagsSet).insert(value);
						break;
					case Parameter::COUNTER:
						(uses[k].inCondition ? countersRead : countersSet).insert(value);
						break;
					default:
						break;
				}
			}
		}
		for (Int i = 0; i < sides.getNumTeams(); i++) {
			TeamsInfo *team = sides.getTeamInfo(i);
			Dict *d = team->getDict();
			if (d->getAsciiString(TheKey_teamOwner) != player) continue;
			Bool hasCondition = false;
			for (Int k = 0; k < d->getPairCount(); k++) {
				if (d->getNthType(k) != Dict::DICT_ASCIISTRING || d->getNthAsciiString(k).isEmpty()) continue;
				const char *ref = teamPropertyRef(TheNameKeyGenerator->keyToName(d->getNthKey(k)).str());
				if (ref == nullptr || strcmp(ref, "script") != 0) continue;
				called.insert(d->getNthAsciiString(k).str());
				if (d->getNthKey(k) == TheKey_teamProductionCondition) hasCondition = true;
			}
			// A team the AI is never told to build and no script ever names.
			const AsciiString teamName = d->getAsciiString(TheKey_teamName);
			if (!hasCondition && !sides.isPlayerDefaultTeam(team) && !teamsNamed.count(teamName.str())) {
				counts[8]++;
				pushCapped(unusedTeams, limit, player, teamName.str());
			}
		}

		ScriptList *list = sides.getSideInfo(side)->getScriptList();
		for (ScriptGroup *group = list ? list->getScriptGroup() : nullptr; group; group = group->getNext()) {
			// Separator groups ("===== X =====") are empty on purpose.
			if (group->getScript() == nullptr && !group->getName().startsWith("=")) {
				counts[7]++;
				pushCapped(emptyGroups, limit, player, group->getName().str());
			}
		}
		for (size_t i = 0; i < all.size(); i++) {
			if (all[i].side != side) continue;
			Script *script = all[i].script;
			const char *name = script->getName().str();
			// Calling a subroutine group runs its scripts.
			const bool isCalled = called.count(name) || (all[i].group && called.count(all[i].group->getName().str()));
			if (script->isSubroutine() && !isCalled) {
				counts[0]++;
				pushCapped(unusedSubroutines, limit, player, name);
			} else if (!script->isActive() && !script->isSubroutine() && !isCalled) {
				counts[1]++;
				pushCapped(neverEnabled, limit, player, name);
			}
			if (script->getAction() == nullptr && script->getFalseAction() == nullptr) {
				counts[2]++;
				pushCapped(noActions, limit, player, name);
			}
		}
		for (std::set<std::string>::const_iterator it = flagsRead.begin(); it != flagsRead.end(); ++it) {
			if (!flagsSet.count(*it)) { counts[3]++; pushCapped(flagsNeverSet, limit, player, it->c_str()); }
		}
		for (std::set<std::string>::const_iterator it = flagsSet.begin(); it != flagsSet.end(); ++it) {
			if (!flagsRead.count(*it)) { counts[4]++; pushCapped(flagsNeverRead, limit, player, it->c_str()); }
		}
		for (std::set<std::string>::const_iterator it = countersRead.begin(); it != countersRead.end(); ++it) {
			if (!countersSet.count(*it)) { counts[5]++; pushCapped(countersNeverSet, limit, player, it->c_str()); }
		}
		for (std::set<std::string>::const_iterator it = countersSet.begin(); it != countersSet.end(); ++it) {
			if (!countersRead.count(*it)) { counts[6]++; pushCapped(countersNeverRead, limit, player, it->c_str()); }
		}
	}

	McpJson j = McpJson::makeObject();
	j.set("unused_subroutines", McpJson::makeObject().set("count", counts[0]).set("items", unusedSubroutines));
	j.set("inactive_never_enabled", McpJson::makeObject().set("count", counts[1]).set("items", neverEnabled));
	j.set("scripts_without_actions", McpJson::makeObject().set("count", counts[2]).set("items", noActions));
	j.set("flags_read_never_set", McpJson::makeObject().set("count", counts[3]).set("items", flagsNeverSet));
	j.set("flags_set_never_read", McpJson::makeObject().set("count", counts[4]).set("items", flagsNeverRead));
	j.set("counters_read_never_set", McpJson::makeObject().set("count", counts[5]).set("items", countersNeverSet));
	j.set("counters_set_never_read", McpJson::makeObject().set("count", counts[6]).set("items", countersNeverRead));
	j.set("empty_groups", McpJson::makeObject().set("count", counts[7]).set("items", emptyGroups));
	j.set("teams_never_built_or_named", McpJson::makeObject().set("count", counts[8]).set("items", unusedTeams));
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.missing
//-------------------------------------------------------------------------------------------------

std::string lowerCase(const std::string &text)
{
	std::string out = text;
	for (size_t i = 0; i < out.size(); i++) {
		out[i] = (char)tolower((unsigned char)out[i]);
	}
	return out;
}

/// Length of the longest common subsequence; names that differ by a dropped or added part
/// ("Tank_Battlemaster_FAST" / "Tank_ChinaTankBattleMaster") still share most of it.
Int commonSubsequence(const std::string &a, const std::string &b)
{
	std::vector<Int> row(b.size() + 1, 0);
	for (size_t i = 1; i <= a.size(); i++) {
		Int diagonal = 0;
		for (size_t k = 1; k <= b.size(); k++) {
			const Int above = row[k];
			row[k] = a[i - 1] == b[k - 1] ? diagonal + 1 : (row[k] > row[k - 1] ? row[k] : row[k - 1]);
			diagonal = above;
		}
	}
	return row[b.size()];
}

/// The existing names closest to a missing one, best first.
std::vector<std::string> suggestNames(const std::string &missing, const std::vector<std::string> &candidates, const std::vector<std::string> &lowered)
{
	const std::string wanted = lowerCase(missing);
	std::vector<std::pair<double, std::string> > scored;
	for (size_t i = 0; i < candidates.size(); i++) {
		const std::string &name = lowered[i];
		if (name.empty() || name == wanted) continue;
		// Cheap bound first: the score cannot exceed what the lengths allow.
		const double shorter = (double)(name.size() < wanted.size() ? name.size() : wanted.size());
		if (2.0 * shorter / (name.size() + wanted.size()) < 0.72) continue;
		const double score = 2.0 * commonSubsequence(wanted, name) / (name.size() + wanted.size());
		if (score >= 0.72) scored.push_back(std::make_pair(-score, candidates[i]));
	}
	std::sort(scored.begin(), scored.end());
	std::vector<std::string> best;
	for (size_t i = 0; i < scored.size() && i < 3; i++) {
		best.push_back(scored[i].second);
	}
	return best;
}

struct MissingGroup
{
	std::string kind, value;
	Int count;
	std::set<std::string> players;
	MissingGroup() : count(0) {}
	std::vector<std::string> samples;
};

bool moreMissing(const MissingGroup *a, const MissingGroup *b)
{
	return a->count != b->count ? a->count > b->count : a->value < b->value;
}

/// Everything scripts and teams refer to that does not exist, grouped by the missing name with
/// the closest existing names. One typo usually accounts for many warnings.
McpJson cmdMissingReferences(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const Int limit = mcpArgInt(args, "limit", 50);
	const bool suggest = mcpArgBool(args, "suggest", true);

	std::map<std::pair<std::string, std::string>, MissingGroup> groups;
	Int total = 0;
	{
		ParameterCheckScope scope(TheSidesList);
		const std::vector<ScriptLoc> all = allScripts(sides);
		for (size_t i = 0; i < all.size(); i++) {
			if (onlySide >= 0 && all[i].side != onlySide) continue;
			std::vector<ParamUse> uses;
			collectParamUses(all[i].script, uses);
			for (size_t k = 0; k < uses.size(); k++) {
				Parameter *parm = uses[k].parm;
				if (!isStringParamType(parm->getParameterType())) continue;
				if (EditParameter::getWarningText(parm, !uses[k].inCondition).isEmpty()) continue;
				const std::string kind = parameterTypeName(parm->getParameterType());
				MissingGroup &group = groups[std::make_pair(kind, std::string(parm->getString().str()))];
				group.kind = kind;
				group.value = parm->getString().str();
				group.count++;
				group.players.insert(sideName(sides, all[i].side).str());
				if (group.samples.size() < 3) group.samples.push_back(std::string("script '") + all[i].script->getName().str() + "'");
				total++;
			}
		}
	}
	Int teamsChecked = 0;
	const McpJson teamWarnings = verifyTeams(sides, onlySide, "", &teamsChecked);
	for (size_t i = 0; i < teamWarnings.size(); i++) {
		const McpJson &warning = teamWarnings.at(i);
		if (!warning.get("missing_kind").isString()) continue;
		const std::string kind = warning.get("missing_kind").asString();
		const std::string value = warning.get("missing").asString();
		MissingGroup &group = groups[std::make_pair(kind, value)];
		group.kind = kind;
		group.value = value;
		group.count++;
		group.players.insert(warning.get("owner").asString());
		if (group.samples.size() < 3) {
			group.samples.push_back("team '" + warning.get("team").asString() + "' " + warning.get("property").asString());
		}
		total++;
	}

	std::vector<MissingGroup *> order;
	for (std::map<std::pair<std::string, std::string>, MissingGroup>::iterator it = groups.begin(); it != groups.end(); ++it) {
		order.push_back(&it->second);
	}
	std::sort(order.begin(), order.end(), moreMissing);

	// The names that do exist, per kind, as the script editor lists them.
	std::map<std::string, std::vector<std::string> > candidates, lowered;
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < order.size() && (Int)i < limit; i++) {
		const MissingGroup &group = *order[i];
		McpJson entry = McpJson::makeObject();
		entry.set("kind", group.kind).set("missing", group.value).set("uses", group.count);
		McpJson players = McpJson::makeArray();
		for (std::set<std::string>::const_iterator it = group.players.begin(); it != group.players.end(); ++it) {
			players.push(McpJson(*it));
		}
		McpJson samples = McpJson::makeArray();
		for (size_t k = 0; k < group.samples.size(); k++) {
			samples.push(McpJson(group.samples[k]));
		}
		entry.set("players", players).set("used_by", samples);
		if (suggest && !group.value.empty()) {
			if (!candidates.count(group.kind)) {
				std::vector<std::string> &names = candidates[group.kind];
				const Int type = parameterTypeFromName(group.kind.c_str());
				if (type >= 0) collectEditorValues((Parameter::ParameterType)type, names);
				std::vector<std::string> &lower = lowered[group.kind];
				for (size_t k = 0; k < names.size(); k++) {
					lower.push_back(lowerCase(names[k]));
				}
			}
			const std::vector<std::string> &names = candidates[group.kind];
			// A script or team another player has is a different thing than a typo.
			if ((group.kind == "SCRIPT" || group.kind == "SCRIPT_SUBROUTINE") && std::find(names.begin(), names.end(), group.value) != names.end()) {
				entry.set("exists_for_another_player", true);
			}
			const std::vector<std::string> best = suggestNames(group.value, names, lowered[group.kind]);
			McpJson suggestions = McpJson::makeArray();
			for (size_t k = 0; k < best.size(); k++) {
				suggestions.push(McpJson(best[k]));
			}
			entry.set("suggestions", suggestions);
		}
		// What scripts.rename calls the kind that retargets these references.
		if (group.kind == "SCRIPT" || group.kind == "SCRIPT_SUBROUTINE") {
			entry.set("rename_kind", "script_reference");
		} else if (group.kind == "TEAM") {
			entry.set("rename_kind", "team_reference");
		} else {
			entry.set("rename_kind", group.kind);
		}
		list.push(entry);
	}
	return McpJson::makeObject().set("total_uses", total).set("distinct_missing", (int)order.size()).set("missing", list);
}

//-------------------------------------------------------------------------------------------------
// scripts.set_values
//-------------------------------------------------------------------------------------------------

struct BulkEdit
{
	// Which parameters.
	Int onlySide;
	bool hasGroup;
	std::string group, scriptFilter, entryType, current, text, in;
	Parameter::ParameterType paramType;
	// What to do with them.
	std::string op;
	McpJson value;
	bool hasMin, hasMax;
	double minValue, maxValue;
	// Results.
	Int matched, changed;
	McpJson samples;
};

bool isNumericParamType(Parameter::ParameterType type)
{
	return type == Parameter::INT || type == Parameter::REAL || type == Parameter::ANGLE || type == Parameter::PERCENT;
}

/// Applies the edit to the parameters of one condition or action.
template <class T>
void bulkEditEntry(BulkEdit &edit, SidesList &sides, const ScriptLoc &loc, T *item, const char *typeName, const char *where)
{
	if (!edit.entryType.empty() && _stricmp(edit.entryType.c_str(), typeName) != 0) return;
	if (!edit.text.empty() && !mcpContainsNoCase(item->getUiText().str(), edit.text.c_str())) return;
	const AsciiString before = item->getUiText();
	Bool any = false;
	for (Int i = 0; i < item->getNumParameters(); i++) {
		Parameter *parm = item->getParameter(i);
		if (parm->getParameterType() != edit.paramType) continue;
		const std::string old = paramValueText(parm);
		if (!edit.current.empty() && _stricmp(edit.current.c_str(), old.c_str()) != 0) continue;
		edit.matched++;
		if (edit.op == "set") {
			setParamValue(parm, edit.value, where, i);
		} else {
			double number = paramValueToJson(parm).asNumber();
			number = edit.op == "multiply" ? number * edit.value.asNumber() : number + edit.value.asNumber();
			if (edit.hasMin && number < edit.minValue) number = edit.minValue;
			if (edit.hasMax && number > edit.maxValue) number = edit.maxValue;
			if (edit.paramType == Parameter::INT) number = floor(number + 0.5);
			setParamValue(parm, McpJson(number), where, i);
		}
		if (paramValueText(parm) != old) {
			edit.changed++;
			any = true;
		}
	}
	if (any && edit.samples.size() < 15) {
		McpJson sample = McpJson::makeObject();
		sample.set("player", sideName(sides, loc.side).str()).set("script", loc.script->getName().str()).set("where", where);
		sample.set("before", before.str()).set("after", item->getUiText().str());
		edit.samples.push(sample);
	}
}

/// Changes one kind of parameter in many scripts at once, e.g. every credit threshold of the
/// build conditions or every use of one unit type.
McpJson cmdSetValues(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;

	BulkEdit edit;
	edit.onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	edit.hasGroup = args.has("group");
	edit.group = mcpArgString(args, "group", "");
	edit.scriptFilter = mcpArgString(args, "script_filter", "");
	edit.entryType = mcpArgString(args, "type", "");
	edit.current = mcpArgString(args, "current", "");
	edit.text = mcpArgString(args, "text", "");
	edit.in = mcpArgString(args, "in", "any");
	if (edit.in != "any" && edit.in != "conditions" && edit.in != "actions") {
		mcpFail("in must be any, conditions or actions");
	}
	const Int typeIndex = parameterTypeFromName(mcpArgString(args, "param_type").c_str());
	if (typeIndex < 0) {
		mcpFail("unknown param_type (the types are listed by scripts.list_actions / scripts.list_conditions)");
	}
	edit.paramType = (Parameter::ParameterType)typeIndex;
	if (!edit.entryType.empty() && findActionType(edit.entryType.c_str()) < 0 && findConditionType(edit.entryType.c_str()) < 0) {
		mcpFail("unknown condition or action type '%s'", edit.entryType.c_str());
	}
	edit.op = mcpArgString(args, "op", "set");
	edit.value = args.get("value");
	if (edit.value.isNull()) {
		mcpFail("pass 'value': the new value for op set, or the number to add / multiply by");
	}
	if (edit.op == "add" || edit.op == "multiply") {
		if (!isNumericParamType(edit.paramType) || !edit.value.isNumber()) {
			mcpFail("op %s needs a number and a numeric param_type (INT, REAL, ANGLE, PERCENT)", edit.op.c_str());
		}
	} else if (edit.op != "set") {
		mcpFail("op must be set, add or multiply");
	}
	edit.hasMin = args.has("min");
	edit.hasMax = args.has("max");
	edit.minValue = mcpArgNumber(args, "min", 0);
	edit.maxValue = mcpArgNumber(args, "max", 0);
	// Without any narrowing this would rewrite a whole set by accident.
	if (edit.onlySide < 0 && !edit.hasGroup && edit.scriptFilter.empty() && edit.entryType.empty() && edit.current.empty()
		&& edit.text.empty() && !mcpArgBool(args, "all_scripts", false)) {
		mcpFail("narrow it down (player, group, script_filter, type, current or text), or pass all_scripts=true");
	}
	const bool dryRun = mcpArgBool(args, "dry_run", false);
	edit.matched = 0;
	edit.changed = 0;
	edit.samples = McpJson::makeArray();

	Int scriptsChanged = 0;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		const ScriptLoc &loc = all[i];
		if (edit.onlySide >= 0 && loc.side != edit.onlySide) continue;
		if (edit.hasGroup && edit.group != (loc.group ? loc.group->getName().str() : "")) continue;
		if (!mcpContainsNoCase(loc.script->getName().str(), edit.scriptFilter.c_str())) continue;
		const Int changedBefore = edit.changed;
		char where[64];
		if (edit.in != "actions") {
			Int o = 0;
			for (OrCondition *orCond = loc.script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition(), o++) {
				Int a = 0;
				for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext(), a++) {
					const ConditionTemplate *ct = TheScriptEngine->getConditionTemplate(cond->getConditionType());
					sprintf(where, "conditions[%d][%d]", o, a);
					bulkEditEntry(edit, sides, loc, cond, ct ? ct->m_internalName.str() : "", where);
				}
			}
		}
		if (edit.in != "conditions") {
			for (Int pass = 0; pass < 2; pass++) {
				Int ndx = 0;
				for (ScriptAction *action = pass ? loc.script->getFalseAction() : loc.script->getAction(); action; action = action->getNext(), ndx++) {
					const ActionTemplate *at = TheScriptEngine->getActionTemplate(action->getActionType());
					sprintf(where, "%s[%d]", pass ? "false_actions" : "actions", ndx);
					bulkEditEntry(edit, sides, loc, action, at ? at->m_internalName.str() : "", where);
				}
			}
		}
		if (edit.changed != changedBefore) scriptsChanged++;
	}

	McpJson j = McpJson::makeObject();
	j.set("parameters_matched", edit.matched).set("parameters_changed", edit.changed).set("scripts_changed", scriptsChanged);
	j.set("samples", edit.samples).set("dry_run", dryRun);
	if (!dryRun && edit.changed > 0) {
		commitSides(sides);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.compare
//-------------------------------------------------------------------------------------------------

struct ComparedScript
{
	std::string key, group, name;
	std::vector<std::string> lines;
};

std::string cleanText(const AsciiString &text)
{
	// The editor marks entries that have warnings; that is not part of the script.
	const char *p = text.str();
	if (strncmp(p, "[???]", 5) == 0) p += 5;
	while (*p == ' ') p++;
	std::string out = p;
	while (!out.empty() && out[out.size() - 1] == ' ') out.erase(out.size() - 1);
	return out;
}

/// A script as lines that can be compared: its flags, then every condition and action as worded
/// by the editor.
void scriptToLines(Script *script, std::vector<std::string> &lines)
{
	char flags[160];
	sprintf(flags, "flags: active=%d one_shot=%d subroutine=%d easy=%d normal=%d hard=%d delay=%d", script->isActive() ? 1 : 0,
		script->isOneShot() ? 1 : 0, script->isSubroutine() ? 1 : 0, script->isEasy() ? 1 : 0, script->isNormal() ? 1 : 0,
		script->isHard() ? 1 : 0, (int)script->getDelayEvalSeconds());
	lines.push_back(flags);
	Int o = 0;
	for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition(), o++) {
		for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext()) {
			char prefix[24];
			sprintf(prefix, "IF[%d] ", o);
			lines.push_back(prefix + cleanText(cond->getUiText()));
		}
	}
	for (ScriptAction *action = script->getAction(); action; action = action->getNext()) {
		lines.push_back("THEN " + cleanText(action->getUiText()));
	}
	for (ScriptAction *action = script->getFalseAction(); action; action = action->getNext()) {
		lines.push_back("ELSE " + cleanText(action->getUiText()));
	}
}

/// Collects the scripts of a list. With replacements, names are rewritten first so that two
/// factions' sets line up ("HTC " -> "SCM ").
void collectCompared(ScriptList *list, Replacements *replacements, const AsciiString &fromPlayer, const AsciiString &toPlayer,
	std::vector<ComparedScript> &out, std::vector<std::string> &groupNames)
{
	std::map<std::string, Int> seen;
	for (Int pass = 0; pass < 2; pass++) {
		ScriptGroup *group = pass ? list->getScriptGroup() : nullptr;
		do {
			std::string groupName;
			if (group) {
				groupName = replacements ? replacements->apply(group->getName()).str() : group->getName().str();
				groupNames.push_back(groupName);
			}
			for (Script *script = group ? group->getScript() : (pass ? nullptr : list->getScript()); script; script = script->getNext()) {
				ComparedScript entry;
				Script *work = script;
				if (replacements) {
					work = script->duplicate();
					transformScript(work, *replacements, fromPlayer, toPlayer);
				}
				entry.group = groupName;
				entry.name = work->getName().str();
				scriptToLines(work, entry.lines);
				if (replacements) deleteInstance(work);
				// Same-named scripts in one group are told apart by their order.
				char occurrence[16];
				sprintf(occurrence, "#%d", seen[entry.group + "\x1f" + entry.name]++);
				entry.key = entry.group + "\x1f" + entry.name + occurrence;
				out.push_back(entry);
			}
			group = group ? group->getNext() : nullptr;
		} while (group);
	}
}

/// Lines of `a` that `b` does not have (each line of b cancels one of a).
std::vector<std::string> linesOnlyIn(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
	std::map<std::string, Int> counts;
	for (size_t i = 0; i < b.size(); i++) counts[b[i]]++;
	std::vector<std::string> only;
	for (size_t i = 0; i < a.size(); i++) {
		if (counts[a[i]] > 0) counts[a[i]]--; else only.push_back(a[i]);
	}
	return only;
}

McpJson linesToJson(const std::vector<std::string> &lines, size_t cap)
{
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < lines.size() && i < cap; i++) list.push(McpJson(lines[i]));
	if (lines.size() > cap) {
		char more[48];
		sprintf(more, "... %d more", (int)(lines.size() - cap));
		list.push(McpJson((const char *)more));
	}
	return list;
}

McpJson cappedList(const std::vector<std::string> &items, Int limit)
{
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < items.size() && (Int)i < limit; i++) list.push(McpJson(items[i]));
	return McpJson::makeObject().set("count", (int)items.size()).set("items", list);
}

/// The teams of a player as name -> properties, optionally with names rewritten.
void collectComparedTeams(SidesList &sides, const AsciiString &owner, Replacements *replacements, std::map<std::string, Dict> &out)
{
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		if (team->getDict()->getAsciiString(TheKey_teamOwner) != owner || sides.isPlayerDefaultTeam(team)) continue;
		Dict d = *team->getDict();
		if (replacements) {
			for (Int k = 0; k < d.getPairCount(); k++) {
				if (d.getNthType(k) != Dict::DICT_ASCIISTRING || d.getNthKey(k) == TheKey_teamOwner) continue;
				d.setAsciiString(d.getNthKey(k), replacements->apply(d.getNthAsciiString(k)));
			}
		}
		out[d.getAsciiString(TheKey_teamName).str()] = d;
	}
}

/// Compares the scripts and teams of two players: what only one has and what differs, line by
/// line. The other side can be a player of the map or of a scripts.dump document.
McpJson cmdCompareScripts(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int sideA = requireSide(sides, mcpArgString(args, "player"));
	const AsciiString nameA = sideName(sides, sideA);
	const Int limit = mcpArgInt(args, "limit", 30);
	const bool includeTeams = mcpArgBool(args, "include_teams", true);
	Replacements replacements;
	replacements.read(args.get("replace"));
	Replacements *rewrite = replacements.pairs.empty() ? nullptr : &replacements;

	std::vector<ComparedScript> scriptsA, scriptsB;
	std::vector<std::string> groupsA, groupsB;
	std::map<std::string, Dict> teamsA, teamsB;
	collectCompared(requireScriptList(sides, sideA), nullptr, nameA, nameA, scriptsA, groupsA);
	collectComparedTeams(sides, nameA, nullptr, teamsA);

	AsciiString nameB;
	const McpJson &data = args.get("with_data");
	if (!data.isNull()) {
		// The other side comes from a dump document: build its scripts and teams in a scratch sides list.
		if (!data.isObject() || !data.get("players").isArray()) {
			mcpFail("'with_data' must be a scripts.dump document");
		}
		nameB = mcpArgString(args, "with_player", nameA.str()).c_str();
		const McpJson *entry = nullptr;
		for (size_t p = 0; p < data.get("players").size(); p++) {
			const McpJson &candidate = data.get("players").at(p);
			if (candidate.get("player").isString() && nameB == candidate.get("player").asString().c_str()) entry = &candidate;
		}
		if (entry == nullptr) {
			mcpFail("the document has no player '%s'; pass with_player", nameB.str());
		}
		Int numScripts = 0, numGroups = 0;
		ScriptList *list = buildScriptList(*entry, nameB.str(), &numScripts, &numGroups);
		try {
			collectCompared(list, rewrite, nameB, nameA, scriptsB, groupsB);
		} catch (...) {
			deleteInstance(list);
			throw;
		}
		deleteInstance(list);
		const McpJson &teams = data.get("teams");
		for (size_t t = 0; includeTeams && t < teams.size(); t++) {
			const McpJson &props = teams.at(t);
			if (!props.isObject() || !props.get("teamOwner").isString() || nameB != props.get("teamOwner").asString().c_str()) continue;
			Dict d;
			for (size_t k = 0; k < props.numKeys(); k++) {
				setTeamPropertyChecked(d, props.keyAt(k), props.valueAt(k), true);
			}
			if (rewrite) {
				for (Int k = 0; k < d.getPairCount(); k++) {
					if (d.getNthType(k) != Dict::DICT_ASCIISTRING || d.getNthKey(k) == TheKey_teamOwner) continue;
					d.setAsciiString(d.getNthKey(k), rewrite->apply(d.getNthAsciiString(k)));
				}
			}
			teamsB[d.getAsciiString(TheKey_teamName).str()] = d;
		}
	} else {
		const Int sideB = requireSide(sides, mcpArgString(args, "with_player"));
		if (sideB == sideA) {
			mcpFail("pass another player as with_player, or a dump document as with_data");
		}
		nameB = sideName(sides, sideB);
		collectCompared(requireScriptList(sides, sideB), rewrite, nameB, nameA, scriptsB, groupsB);
		collectComparedTeams(sides, nameB, rewrite, teamsB);
	}

	// Scripts.
	std::map<std::string, const ComparedScript *> indexB;
	for (size_t i = 0; i < scriptsB.size(); i++) indexB[scriptsB[i].key] = &scriptsB[i];
	std::set<std::string> keysA;
	std::vector<std::string> onlyA, onlyB;
	McpJson changed = McpJson::makeArray();
	Int identical = 0, numChanged = 0;
	for (size_t i = 0; i < scriptsA.size(); i++) {
		const ComparedScript &a = scriptsA[i];
		keysA.insert(a.key);
		std::map<std::string, const ComparedScript *>::const_iterator it = indexB.find(a.key);
		if (it == indexB.end()) {
			onlyA.push_back(a.group.empty() ? a.name : a.group + " / " + a.name);
			continue;
		}
		const ComparedScript &b = *it->second;
		if (a.lines == b.lines) {
			identical++;
			continue;
		}
		numChanged++;
		if ((Int)changed.size() >= limit) continue;
		McpJson entry = McpJson::makeObject();
		entry.set("script", a.name);
		if (!a.group.empty()) entry.set("group", a.group);
		const std::vector<std::string> inA = linesOnlyIn(a.lines, b.lines);
		const std::vector<std::string> inB = linesOnlyIn(b.lines, a.lines);
		if (inA.empty() && inB.empty()) {
			entry.set("note", "same conditions and actions in another order");
		} else {
			entry.set("a", linesToJson(inA, 8)).set("b", linesToJson(inB, 8));
		}
		changed.push(entry);
	}
	for (size_t i = 0; i < scriptsB.size(); i++) {
		if (!keysA.count(scriptsB[i].key)) {
			onlyB.push_back(scriptsB[i].group.empty() ? scriptsB[i].name : scriptsB[i].group + " / " + scriptsB[i].name);
		}
	}

	std::vector<std::string> groupsOnlyA = linesOnlyIn(groupsA, groupsB);
	std::vector<std::string> groupsOnlyB = linesOnlyIn(groupsB, groupsA);

	McpJson j = McpJson::makeObject();
	j.set("a", McpJson::makeObject().set("player", nameA.str()).set("scripts", (int)scriptsA.size()).set("teams", (int)teamsA.size()));
	j.set("b", McpJson::makeObject().set("player", nameB.str()).set("scripts", (int)scriptsB.size()).set("teams", (int)teamsB.size())
		.set("from_document", !data.isNull()));
	j.set("scripts", McpJson::makeObject()
		.set("identical", identical)
		.set("only_in_a", cappedList(onlyA, limit))
		.set("only_in_b", cappedList(onlyB, limit))
		.set("different", McpJson::makeObject().set("count", numChanged).set("items", changed)));
	j.set("groups", McpJson::makeObject().set("only_in_a", cappedList(groupsOnlyA, limit)).set("only_in_b", cappedList(groupsOnlyB, limit)));

	if (includeTeams) {
		std::vector<std::string> teamsOnlyA, teamsOnlyB;
		McpJson teamsChanged = McpJson::makeArray();
		Int teamsIdentical = 0, numTeamsChanged = 0;
		for (std::map<std::string, Dict>::const_iterator it = teamsA.begin(); it != teamsA.end(); ++it) {
			std::map<std::string, Dict>::const_iterator other = teamsB.find(it->first);
			if (other == teamsB.end()) {
				teamsOnlyA.push_back(it->first);
				continue;
			}
			const McpJson a = mcpDictToJson(it->second);
			const McpJson b = mcpDictToJson(other->second);
			McpJson changes = McpJson::makeObject();
			for (Int pass = 0; pass < 2; pass++) {
				const McpJson &from = pass ? b : a;
				const McpJson &to = pass ? a : b;
				for (size_t k = 0; k < from.numKeys(); k++) {
					const std::string &key = from.keyAt(k);
					// The owner differs by definition; the export flag is editor bookkeeping.
					if (key == "teamOwner" || key == "exportWithScript" || changes.has(key.c_str())) continue;
					if (from.valueAt(k).dump() == to.get(key.c_str()).dump()) continue;
					McpJson pair = McpJson::makeArray();
					pair.push(a.get(key.c_str()));
					pair.push(b.get(key.c_str()));
					changes.set(key.c_str(), pair);
				}
			}
			if (changes.numKeys() == 0) {
				teamsIdentical++;
				continue;
			}
			numTeamsChanged++;
			if ((Int)teamsChanged.size() < limit) {
				teamsChanged.push(McpJson::makeObject().set("team", it->first).set("a_b", changes));
			}
		}
		for (std::map<std::string, Dict>::const_iterator it = teamsB.begin(); it != teamsB.end(); ++it) {
			if (!teamsA.count(it->first)) teamsOnlyB.push_back(it->first);
		}
		j.set("teams", McpJson::makeObject()
			.set("identical", teamsIdentical)
			.set("only_in_a", cappedList(teamsOnlyA, limit))
			.set("only_in_b", cappedList(teamsOnlyB, limit))
			.set("different", McpJson::makeObject().set("count", numTeamsChanged).set("items", teamsChanged)));
	}
	return j;
}

} // namespace

void mcpRegisterScriptToolCommands()
{
	mcpRegisterCommand("scripts.compare", cmdCompareScripts, "{player,with_player?,with_data?,replace?:{old:new},include_teams?=true,limit?=30} Differences between two players' scripts and teams, or against a dump document.");
	mcpRegisterCommand("scripts.set_values", cmdSetValues, "{param_type,op?=set|add|multiply,value,player?,group?,script_filter?,type?,current?,text?,in?=any|conditions|actions,min?,max?,all_scripts?,dry_run?} Changes one kind of parameter in many scripts.");
	mcpRegisterCommand("scripts.missing", cmdMissingReferences, "{player?,limit?=50,suggest?=true} Missing references of scripts and teams grouped by name, with the closest existing names.");
	mcpRegisterCommand("scripts.batch", cmdBatch, "{commands:[{cmd,args}],results?=summary|full} Runs scripts.*, teams.* and sides.set/delete commands as one undo step; all or nothing.");
	mcpRegisterCommand("scripts.dump", cmdDumpScripts, "{player?,include_teams?=true} All scripts, groups and teams as one JSON document.");
	mcpRegisterCommand("scripts.load", cmdLoadScripts, "{data,create_players?=true} Replaces the scripts and teams of the players in a scripts.dump document.");
	mcpRegisterCommand("scripts.analyze", cmdAnalyzeScripts, "{player?,limit?=40} Dead logic: unused subroutines, scripts nothing enables, flags and counters used one-sided, teams never built.");
	mcpRegisterCommand("sides.set", cmdSetSide, "{name,new_name?,properties?} Changes a player's properties or renames it with its references.");
	mcpRegisterCommand("sides.delete", cmdDeleteSide, "{name,force?=false} Deletes a player with its scripts and teams.");
	mcpRegisterCommand("scripts.copy", cmdCopyScript, "{name,player?,group?,script_index?,new_name,to_player?,to_group?,index?} Duplicates a script.");
	mcpRegisterCommand("scripts.clone_player", cmdClonePlayer, "{from,to,replace?:{old:new},existing?=fail|replace|append,include_teams?=true,dry_run?} Copies a player's scripts, groups and teams to another player, rewriting names.");
	mcpRegisterCommand("scripts.rename", cmdRename, "{kind:script|group|team|<PARAM_TYPE>,from,to,player?} Renames something and every reference to it in scripts and teams.");
	mcpRegisterCommand("teams.copy", cmdCopyTeam, "{name,new_name,owner?} Duplicates a team.");
	mcpRegisterCommand("scripts.list_values", cmdListValues, "{param_type,filter?,limit?=100,offset?} The values the script editor offers for a parameter type (upgrades, sciences, special powers, command buttons, object types, teams, ...).");
}
