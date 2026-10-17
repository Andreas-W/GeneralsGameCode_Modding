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

// McpCmdScriptViews.cpp
// MCP bridge commands that read a script set from a particular angle: what a team or script
// sets in motion, a player's build order, its scripts and teams per difficulty, the stats of
// the unit types they use, and the players' build lists (the Build List tool).

#include "StdAfx.h"
#define DEFINE_BUILDABLE_STATUS_NAMES
#include "mcp/McpCommands.h"
#include "mcp/McpScriptsShared.h"

#include "WorldBuilderDoc.h"
#include "Common/KindOf.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"

#include <limits.h>
#include <map>
#include <math.h>
#include <set>
#include <string>
#include <vector>

using namespace McpScripts;

namespace
{

const double VIEW_PI = 3.14159265358979323846;

std::string trimmed(const AsciiString &text)
{
	// The editor marks entries that have warnings; that is not part of the script.
	const char *p = text.str();
	if (strncmp(p, "[???]", 5) == 0) p += 5;
	while (*p == ' ') p++;
	std::string out = p;
	while (!out.empty() && out[out.size() - 1] == ' ') out.erase(out.size() - 1);
	return out;
}

/// The conditions of a script as text, one entry per OR clause.
McpJson conditionClauses(Script *script)
{
	McpJson clauses = McpJson::makeArray();
	for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition()) {
		std::string clause;
		for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext()) {
			if (!clause.empty()) clause += " AND ";
			clause += trimmed(cond->getUiText());
		}
		clauses.push(McpJson(clause));
	}
	return clauses;
}

/// "ENH" for a script that runs on easy, normal and hard.
std::string difficultyFlags(Script *script)
{
	std::string flags;
	if (script->isEasy()) flags += "E";
	if (script->isNormal()) flags += "N";
	if (script->isHard()) flags += "H";
	return flags;
}

Script *findScriptInSide(const std::vector<ScriptLoc> &all, Int side, const std::string &name)
{
	for (size_t i = 0; i < all.size(); i++) {
		if (all[i].side == side && name == all[i].script->getName().str()) return all[i].script;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
// scripts.trace
//-------------------------------------------------------------------------------------------------

struct TraceContext
{
	SidesList *sides;
	Int side;
	std::vector<ScriptLoc> all;
	std::set<std::string> visited;
	Int nodes, maxNodes, maxDepth;
};

McpJson traceScript(TraceContext &ctx, const std::string &name, const std::string &via, Int depth);

/// The scripts of the player whose conditions read a flag or a counter/timer.
void traceReaders(TraceContext &ctx, Parameter::ParameterType type, const std::string &value, const std::string &via, Int depth,
	const std::string &self, McpJson &children)
{
	for (size_t i = 0; i < ctx.all.size(); i++) {
		if (ctx.all[i].side != ctx.side) continue;
		Script *script = ctx.all[i].script;
		if (self == script->getName().str()) continue;
		std::vector<ParamUse> uses;
		collectParamUses(script, uses);
		bool reads = false;
		for (size_t k = 0; k < uses.size() && !reads; k++) {
			reads = uses[k].inCondition && uses[k].parm->getParameterType() == type && value == uses[k].parm->getString().str();
		}
		if (reads) {
			children.push(traceScript(ctx, script->getName().str(), via, depth + 1));
		}
	}
}

McpJson traceScript(TraceContext &ctx, const std::string &name, const std::string &via, Int depth)
{
	McpJson node = McpJson::makeObject();
	node.set("script", name);
	if (!via.empty()) node.set("via", via);
	Script *script = findScriptInSide(ctx.all, ctx.side, name);
	if (script == nullptr) {
		return node.set("missing", true);
	}
	if (!ctx.visited.insert(name).second) {
		return node.set("shown_above", true);
	}
	ctx.nodes++;
	if (!script->isActive()) node.set("active", false);
	if (script->isSubroutine()) node.set("subroutine", true);
	if (difficultyFlags(script) != "ENH") node.set("difficulty", difficultyFlags(script));
	node.set("if", conditionClauses(script));
	McpJson then = McpJson::makeArray();
	for (ScriptAction *action = script->getAction(); action; action = action->getNext()) {
		then.push(McpJson(trimmed(action->getUiText())));
	}
	node.set("then", then);
	if (script->getFalseAction()) {
		McpJson otherwise = McpJson::makeArray();
		for (ScriptAction *action = script->getFalseAction(); action; action = action->getNext()) {
			otherwise.push(McpJson(trimmed(action->getUiText())));
		}
		node.set("else", otherwise);
	}
	if (depth >= ctx.maxDepth || ctx.nodes >= ctx.maxNodes) {
		return node.set("not_expanded", true);
	}

	// What its actions set in motion: scripts they run or enable, and scripts waiting for the
	// flags, counters and timers they set.
	McpJson children = McpJson::makeArray();
	std::set<std::string> done;
	for (Int pass = 0; pass < 2; pass++) {
		for (ScriptAction *action = pass ? script->getFalseAction() : script->getAction(); action; action = action->getNext()) {
			for (Int i = 0; i < action->getNumParameters(); i++) {
				Parameter *parm = action->getParameter(i);
				const std::string value = parm->getString().str();
				const Parameter::ParameterType type = parm->getParameterType();
				if (value.empty() || !done.insert(std::string(parameterTypeName(type)) + "\x1f" + value).second) continue;
				if (ctx.nodes >= ctx.maxNodes) break;
				if (type == Parameter::SCRIPT || type == Parameter::SCRIPT_SUBROUTINE) {
					children.push(traceScript(ctx, value, "runs or enables", depth + 1));
				} else if (type == Parameter::FLAG) {
					traceReaders(ctx, type, value, "waits for flag '" + value + "'", depth, name, children);
				} else if (type == Parameter::COUNTER) {
					traceReaders(ctx, type, value, "waits for counter/timer '" + value + "'", depth, name, children);
				}
			}
		}
	}
	if (children.size() > 0) {
		node.set("leads_to", children);
	}
	return node;
}

/// What a team or a script sets in motion: the scripts a team names, and from each script the
/// scripts it runs and those waiting for the flags and timers it sets.
McpJson cmdTrace(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const std::string name = mcpArgString(args, "name");
	TraceContext ctx;
	ctx.sides = &sides;
	ctx.all = allScripts(sides);
	ctx.nodes = 0;
	ctx.maxDepth = mcpArgInt(args, "depth", 4);
	ctx.maxNodes = mcpArgInt(args, "max_scripts", 60);

	McpJson j = McpJson::makeObject();
	TeamsInfo *team = args.has("player") ? nullptr : sides.findTeamInfo(AsciiString(name.c_str()));
	if (team && mcpArgString(args, "kind", "auto") != "script") {
		Dict *d = team->getDict();
		ctx.side = findSide(sides, d->getAsciiString(TheKey_teamOwner));
		if (ctx.side < 0) {
			mcpFail("team '%s' has no valid owner", name.c_str());
		}
		j.set("team", name).set("owner", sideName(sides, ctx.side).str());
		McpJson hooks = McpJson::makeArray();
		for (Int k = 0; k < d->getPairCount(); k++) {
			if (d->getNthType(k) != Dict::DICT_ASCIISTRING || d->getNthAsciiString(k).isEmpty()) continue;
			const AsciiString key = TheNameKeyGenerator->keyToName(d->getNthKey(k));
			const char *ref = teamPropertyRef(key.str());
			if (ref == nullptr || strcmp(ref, "script") != 0) continue;
			hooks.push(traceScript(ctx, d->getNthAsciiString(k).str(), key.str() + 4, 1));
		}
		j.set("scripts", hooks);
	} else {
		// A script: the name may exist for several players.
		std::vector<Int> owners;
		const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
		for (Int side = 0; side < sides.getNumSides(); side++) {
			if ((onlySide < 0 || side == onlySide) && findScriptInSide(ctx.all, side, name)) owners.push_back(side);
		}
		if (owners.empty()) {
			mcpFail("no team or script named '%s'%s", name.c_str(), onlySide >= 0 ? " for that player" : "");
		}
		if (owners.size() > 1) {
			mcpFail("%d players have a script named '%s'; pass player", (int)owners.size(), name.c_str());
		}
		ctx.side = owners[0];
		j.set("player", sideName(sides, ctx.side).str());
		j.set("trace", traceScript(ctx, name, "", 1));

		// And the other direction, one step: who starts it.
		McpJson callers = McpJson::makeArray();
		for (size_t i = 0; i < ctx.all.size() && callers.size() < 30; i++) {
			if (ctx.all[i].side != ctx.side) continue;
			std::vector<ParamUse> uses;
			collectParamUses(ctx.all[i].script, uses);
			for (size_t k = 0; k < uses.size(); k++) {
				const Parameter::ParameterType type = uses[k].parm->getParameterType();
				if ((type == Parameter::SCRIPT || type == Parameter::SCRIPT_SUBROUTINE) && name == uses[k].parm->getString().str()) {
					callers.push(McpJson(std::string("script '") + ctx.all[i].script->getName().str() + "'"));
					break;
				}
			}
		}
		for (Int i = 0; i < sides.getNumTeams() && callers.size() < 30; i++) {
			Dict *d = sides.getTeamInfo(i)->getDict();
			if (d->getAsciiString(TheKey_teamOwner) != sideName(sides, ctx.side)) continue;
			for (Int k = 0; k < d->getPairCount(); k++) {
				if (d->getNthType(k) != Dict::DICT_ASCIISTRING || name != d->getNthAsciiString(k).str()) continue;
				const AsciiString key = TheNameKeyGenerator->keyToName(d->getNthKey(k));
				const char *ref = teamPropertyRef(key.str());
				if (ref && strcmp(ref, "script") == 0) {
					callers.push(McpJson(std::string("team '") + d->getAsciiString(TheKey_teamName).str() + "' " + (key.str() + 4)));
				}
			}
		}
		j.set("started_by", callers);
	}
	j.set("scripts_shown", ctx.nodes);
	if (ctx.nodes >= ctx.maxNodes) {
		j.set("note", "stopped at max_scripts; trace one of the not_expanded scripts for more");
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.build_order
//-------------------------------------------------------------------------------------------------

/// True for the actions that make the AI build a structure or an upgrade.
bool isBuildAction(const char *typeName)
{
	static const char *const BUILD_ACTIONS[] = { "SKIRMISH_BUILD_BUILDING", "AI_PLAYER_BUILD_SUPPLY_CENTER",
		"AI_PLAYER_BUILD_TYPE_NEAREST_TEAM", "AI_PLAYER_BUILD_UPGRADE", "SKIRMISH_BUILD_BASE_DEFENSE_FRONT",
		"SKIRMISH_BUILD_BASE_DEFENSE_FLANK", "SKIRMISH_BUILD_STRUCTURE_FRONT", "SKIRMISH_BUILD_STRUCTURE_FLANK",
		"SKIRMISH_BUILD_SHIPYARD", nullptr };
	for (Int i = 0; BUILD_ACTIONS[i]; i++) {
		if (strcmp(typeName, BUILD_ACTIONS[i]) == 0) return true;
	}
	return false;
}

/// Every script of a player that builds something, in the order the scripts are evaluated.
McpJson cmdBuildOrder(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	const std::string onlyDifficulty = mcpArgString(args, "difficulty", "");
	const bool includeInactive = mcpArgBool(args, "include_inactive", true);

	McpJson steps = McpJson::makeArray();
	std::map<std::string, Int> totals;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		if (all[i].side != side) continue;
		Script *script = all[i].script;
		if (!includeInactive && !script->isActive()) continue;
		const std::string difficulty = difficultyFlags(script);
		if (!onlyDifficulty.empty() && difficulty.find(onlyDifficulty) == std::string::npos) continue;
		McpJson builds = McpJson::makeArray();
		for (Int pass = 0; pass < 2; pass++) {
			for (ScriptAction *action = pass ? script->getFalseAction() : script->getAction(); action; action = action->getNext()) {
				const ActionTemplate *at = TheScriptEngine->getActionTemplate(action->getActionType());
				if (at == nullptr || !isBuildAction(at->m_internalName.str())) continue;
				McpJson build = McpJson::makeObject();
				build.set("action", at->m_internalName.str());
				for (Int k = 0; k < action->getNumParameters(); k++) {
					if (action->getParameter(k)->getParameterType() == Parameter::OBJECT_TYPE
						|| action->getParameter(k)->getParameterType() == Parameter::UPGRADE) {
						build.set("what", action->getParameter(k)->getString().str());
						totals[action->getParameter(k)->getString().str()]++;
					}
				}
				build.set("text", trimmed(action->getUiText()));
				if (pass) build.set("when_false", true);
				builds.push(build);
			}
		}
		if (builds.size() == 0) continue;
		McpJson step = McpJson::makeObject();
		step.set("step", (int)steps.size() + 1);
		if (all[i].group) step.set("group", all[i].group->getName().str());
		step.set("script", script->getName().str());
		if (!script->isActive()) step.set("active", false);
		if (all[i].group && !all[i].group->isActive()) step.set("group_active", false);
		if (difficulty != "ENH") step.set("difficulty", difficulty);
		if (!script->isOneShot()) step.set("repeats", true);
		step.set("if", conditionClauses(script));
		step.set("builds", builds);
		steps.push(step);
	}
	McpJson totalList = McpJson::makeArray();
	for (std::map<std::string, Int>::const_iterator it = totals.begin(); it != totals.end(); ++it) {
		totalList.push(McpJson::makeObject().set("what", it->first).set("build_actions", it->second));
	}
	return McpJson::makeObject().set("player", sideName(sides, side).str()).set("steps", steps).set("totals", totalList);
}

//-------------------------------------------------------------------------------------------------
// scripts.by_difficulty
//-------------------------------------------------------------------------------------------------

/// The difficulty letter in a name such as "SCM E Attack Team 1" or "CT Def H": a word that is
/// just E, N or H.
char difficultyTag(const char *name)
{
	for (const char *p = name; *p; p++) {
		if ((*p == 'E' || *p == 'N' || *p == 'H') && (p == name || p[-1] == ' ') && (p[1] == ' ' || p[1] == 0)) return *p;
	}
	return 0;
}

/// A player's scripts and teams by difficulty: the easy/normal/hard switches of the scripts, and
/// the E / N / H words this kind of script set puts into names.
McpJson cmdByDifficulty(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	const Int limit = mcpArgInt(args, "limit", 40);

	std::map<std::string, McpJson> byFlags;
	std::map<std::string, Int> flagCounts;
	std::map<char, McpJson> scriptsByTag, teamsByTag;
	std::map<char, Int> scriptTagCounts, teamTagCounts;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		if (all[i].side != side) continue;
		Script *script = all[i].script;
		const std::string flags = difficultyFlags(script).empty() ? "none" : difficultyFlags(script);
		flagCounts[flags]++;
		// Scripts for every difficulty are the normal case and not worth listing.
		if (flags != "ENH") {
			if (!byFlags.count(flags)) byFlags[flags] = McpJson::makeArray();
			if ((Int)byFlags[flags].size() < limit) byFlags[flags].push(McpJson(script->getName().str()));
		}
		const char tag = difficultyTag(script->getName().str());
		if (tag) {
			scriptTagCounts[tag]++;
			if (!scriptsByTag.count(tag)) scriptsByTag[tag] = McpJson::makeArray();
			if ((Int)scriptsByTag[tag].size() < limit) scriptsByTag[tag].push(McpJson(script->getName().str()));
		}
	}
	Int untaggedTeams = 0;
	for (Int i = 0; i < sides.getNumTeams(); i++) {
		TeamsInfo *team = sides.getTeamInfo(i);
		if (team->getDict()->getAsciiString(TheKey_teamOwner) != sideName(sides, side) || sides.isPlayerDefaultTeam(team)) continue;
		const AsciiString name = team->getDict()->getAsciiString(TheKey_teamName);
		const char tag = difficultyTag(name.str());
		if (!tag) {
			untaggedTeams++;
			continue;
		}
		teamTagCounts[tag]++;
		if (!teamsByTag.count(tag)) teamsByTag[tag] = McpJson::makeArray();
		if ((Int)teamsByTag[tag].size() < limit) teamsByTag[tag].push(McpJson(name.str()));
	}

	McpJson switches = McpJson::makeArray();
	for (std::map<std::string, Int>::const_iterator it = flagCounts.begin(); it != flagCounts.end(); ++it) {
		McpJson entry = McpJson::makeObject();
		entry.set("runs_on", it->first).set("scripts", it->second);
		if (byFlags.count(it->first)) entry.set("names", byFlags[it->first]);
		switches.push(entry);
	}
	McpJson scriptTags = McpJson::makeArray(), teamTags = McpJson::makeArray();
	static const char TAGS[] = { 'E', 'N', 'H' };
	for (Int t = 0; t < 3; t++) {
		const char text[2] = { TAGS[t], 0 };
		if (scriptTagCounts.count(TAGS[t])) {
			scriptTags.push(McpJson::makeObject().set("tag", text).set("scripts", scriptTagCounts[TAGS[t]]).set("names", scriptsByTag[TAGS[t]]));
		}
		if (teamTagCounts.count(TAGS[t])) {
			teamTags.push(McpJson::makeObject().set("tag", text).set("teams", teamTagCounts[TAGS[t]]).set("names", teamsByTag[TAGS[t]]));
		}
	}
	McpJson j = McpJson::makeObject();
	j.set("player", sideName(sides, side).str());
	j.set("script_switches", switches);
	j.set("scripts_named", scriptTags).set("teams_named", teamTags).set("teams_without_tag", untaggedTeams);
	return j;
}

//-------------------------------------------------------------------------------------------------
// objects.info
//-------------------------------------------------------------------------------------------------

McpJson describeTemplateStats(const ThingTemplate *tt)
{
	McpJson j = McpJson::makeObject();
	j.set("template", tt->getName().str());
	AsciiString displayName;
	displayName.translate(tt->getDisplayName());
	if (!displayName.isEmpty()) j.set("display_name", displayName.str());
	j.set("side", tt->getDefaultOwningSide().str());
	j.set("cost", (int)tt->friend_getBuildCost());
	const Int buildable = tt->getBuildable();
	if (buildable >= BSTATUS_YES && buildable < BSTATUS_NUM_TYPES) {
		j.set("buildable", BuildableStatusNames[buildable - BSTATUS_YES]);
	}
	if (tt->getEnergyProduction() != 0) j.set("energy", (int)tt->getEnergyProduction());
	j.set("vision_range", (double)tt->friend_calcVisionRange());
	if (!tt->friend_getCommandSetString().isEmpty()) j.set("command_set", tt->friend_getCommandSetString().str());

	McpJson kinds = McpJson::makeArray();
	const char *const *kindNames = KindOfMaskType::getBitNames();
	for (Int k = KINDOF_FIRST; k < KINDOF_COUNT && kindNames[k]; k++) {
		if (tt->isKindOf((KindOfType)k)) kinds.push(McpJson(kindNames[k]));
	}
	j.set("kind_of", kinds);

	// Where it is produced, across all of its prerequisite sets.
	McpJson producers = McpJson::makeArray();
	std::set<std::string> seen;
	for (Int p = 0; p < tt->getPrereqCount(); p++) {
		const ThingTemplate *facilities[32];
		const Int count = tt->getNthPrereq(p)->getAllPossibleBuildFacilityTemplates(facilities, 32);
		for (Int f = 0; f < count; f++) {
			if (facilities[f] && seen.insert(facilities[f]->getName().str()).second) {
				producers.push(McpJson(facilities[f]->getName().str()));
			}
		}
	}
	j.set("prerequisite_sets", (int)tt->getPrereqCount()).set("built_at", producers);
	return j;
}

/// Stats of unit and structure types, to judge teams and build orders by.
McpJson cmdObjectInfo(const McpJson &args)
{
	McpJson list = McpJson::makeArray();
	McpJson unknown = McpJson::makeArray();
	if (args.has("templates")) {
		const McpJson &names = mcpArgArray(args, "templates");
		if (names.size() > 60) {
			mcpFail("at most 60 templates per call");
		}
		for (size_t i = 0; i < names.size(); i++) {
			if (!names.at(i).isString()) mcpFail("'templates' must be an array of template names");
			const ThingTemplate *tt = TheThingFactory->findTemplate(AsciiString(names.at(i).asString().c_str()), FALSE);
			if (tt) list.push(describeTemplateStats(tt)); else unknown.push(names.at(i));
		}
	} else {
		const std::string filter = mcpArgString(args, "filter");
		const Int limit = mcpArgInt(args, "limit", 20);
		Int matched = 0;
		for (const ThingTemplate *tt = TheThingFactory->firstTemplate(); tt; tt = tt->friend_getNextTemplate()) {
			if (!mcpContainsNoCase(tt->getName().str(), filter.c_str())) continue;
			matched++;
			if ((Int)list.size() < limit) list.push(describeTemplateStats(tt));
		}
		return McpJson::makeObject().set("total_matches", matched).set("templates", list);
	}
	return McpJson::makeObject().set("templates", list).set("unknown", unknown);
}

//-------------------------------------------------------------------------------------------------
// Build lists
//-------------------------------------------------------------------------------------------------

BuildListInfo *nthBuildListEntry(SidesInfo *side, Int index)
{
	BuildListInfo *entry = side->getBuildList();
	for (Int i = 0; entry && i < index; i++) {
		entry = entry->getNext();
	}
	return entry;
}

McpJson describeBuildListEntry(BuildListInfo *entry, Int index)
{
	McpJson j = McpJson::makeObject();
	j.set("index", index).set("template", entry->getTemplateName().str());
	if (!entry->getBuildingName().isEmpty()) j.set("name", entry->getBuildingName().str());
	j.set("x", (double)entry->getLocation()->x).set("y", (double)entry->getLocation()->y).set("z", (double)entry->getLocation()->z);
	j.set("angle_deg", entry->getAngle() * 180.0 / VIEW_PI);
	j.set("initially_built", (bool)(entry->isInitiallyBuilt() != 0));
	if ((UnsignedInt)entry->getNumRebuilds() == (UnsignedInt)BuildListInfo::UNLIMITED_REBUILDS) {
		j.set("rebuilds", "unlimited");
	} else {
		j.set("rebuilds", (int)entry->getNumRebuilds());
	}
	j.set("health", (int)entry->getHealth());
	if (!entry->getScript().isEmpty()) j.set("script", entry->getScript().str());
	j.set("whiner", (bool)(entry->getWhiner() != 0)).set("unsellable", (bool)(entry->getUnsellable() != 0));
	j.set("repairable", (bool)(entry->getRepairable() != 0));
	return j;
}

/// Applies the fields given in args to a build list entry.
void applyBuildListArgs(BuildListInfo *entry, const McpJson &args)
{
	if (args.has("template")) {
		const AsciiString name(mcpArgString(args, "template").c_str());
		if (TheThingFactory->findTemplate(name, FALSE) == nullptr) {
			mcpFail("unknown template '%s' (see objects.list_templates)", name.str());
		}
		entry->setTemplateName(name);
	}
	if (args.has("x") || args.has("y") || args.has("z")) {
		Coord3D loc = *entry->getLocation();
		loc.x = (Real)mcpArgNumber(args, "x", loc.x);
		loc.y = (Real)mcpArgNumber(args, "y", loc.y);
		loc.z = (Real)mcpArgNumber(args, "z", loc.z);
		entry->setLocation(loc);
	}
	if (args.has("angle_deg")) entry->setAngle((Real)(mcpArgNumber(args, "angle_deg") * VIEW_PI / 180.0));
	if (args.has("name")) entry->setBuildingName(AsciiString(mcpArgString(args, "name").c_str()));
	if (args.has("script")) entry->setScript(AsciiString(mcpArgString(args, "script").c_str()));
	if (args.has("initially_built")) entry->setInitiallyBuilt(mcpArgBool(args, "initially_built", false));
	if (args.has("health")) entry->setHealth(mcpArgInt(args, "health"));
	if (args.has("whiner")) entry->setWhiner(mcpArgBool(args, "whiner", true));
	if (args.has("unsellable")) entry->setUnsellable(mcpArgBool(args, "unsellable", false));
	if (args.has("repairable")) entry->setRepairable(mcpArgBool(args, "repairable", true));
	if (args.has("rebuilds")) {
		const McpJson &rebuilds = args.get("rebuilds");
		if (rebuilds.isString() && rebuilds.asString() == "unlimited") {
			entry->setNumRebuilds(BuildListInfo::UNLIMITED_REBUILDS);
		} else if (rebuilds.isNumber() && rebuilds.asNumber() >= 0) {
			entry->setNumRebuilds((UnsignedInt)rebuilds.asNumber());
		} else {
			mcpFail("rebuilds must be a number >= 0 or \"unlimited\"");
		}
	}
}

McpJson cmdListBuildList(const McpJson &args)
{
	mcpDoc();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	McpJson players = McpJson::makeArray();
	for (Int side = 0; side < sides.getNumSides(); side++) {
		if (onlySide >= 0 && side != onlySide) continue;
		McpJson entries = McpJson::makeArray();
		Int index = 0;
		for (BuildListInfo *entry = sides.getSideInfo(side)->getBuildList(); entry; entry = entry->getNext(), index++) {
			entries.push(describeBuildListEntry(entry, index));
		}
		if (onlySide < 0 && entries.size() == 0) continue;
		players.push(McpJson::makeObject().set("player", sideName(sides, side).str()).set("build_list", entries));
	}
	return McpJson::makeObject().set("players", players);
}

McpJson cmdAddBuildList(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	mcpArgString(args, "template");
	mcpArgNumber(args, "x");
	mcpArgNumber(args, "y");
	SidesInfo *info = sides.getSideInfo(side);
	Int count = 0;
	for (BuildListInfo *entry = info->getBuildList(); entry; entry = entry->getNext()) count++;
	const Int index = mcpArgInt(args, "index", count);
	if (index < 0 || index > count) {
		mcpFail("index must be 0..%d", count);
	}

	BuildListInfo *entry = newInstance(BuildListInfo);
	Coord3D loc;
	loc.x = loc.y = loc.z = 0;
	entry->setLocation(loc);
	info->addToBuildList(entry, index);
	applyBuildListArgs(entry, args);
	const McpJson j = describeBuildListEntry(entry, index);
	commitSides(sides);
	return j;
}

/// Replaces the whole build list of a player, as one undo step.
McpJson cmdSetBuildList(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	const McpJson &entries = mcpArgArray(args, "entries");
	SidesInfo *info = sides.getSideInfo(side);

	Int removed = 0;
	while (BuildListInfo *old = info->getBuildList()) {
		info->removeFromBuildList(old);
		// Deleting an entry deletes what is linked behind it.
		old->setNextBuildList(nullptr);
		deleteInstance(old);
		removed++;
	}
	for (size_t i = 0; i < entries.size(); i++) {
		const McpJson &fields = entries.at(i);
		if (!fields.isObject()) {
			mcpFail("entries[%d] must be an object with the fields of buildlist.add", (int)i);
		}
		mcpArgString(fields, "template");
		mcpArgNumber(fields, "x");
		mcpArgNumber(fields, "y");
		BuildListInfo *entry = newInstance(BuildListInfo);
		Coord3D loc;
		loc.x = loc.y = loc.z = 0;
		entry->setLocation(loc);
		info->addToBuildList(entry, (Int)i);
		applyBuildListArgs(entry, fields);
	}
	commitSides(sides);
	return McpJson::makeObject().set("player", sideName(sides, side).str()).set("entries", (int)entries.size()).set("replaced", removed);
}

McpJson cmdUpdateBuildList(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	SidesInfo *info = sides.getSideInfo(side);
	Int index = mcpArgInt(args, "index");
	BuildListInfo *entry = index >= 0 ? nthBuildListEntry(info, index) : nullptr;
	if (entry == nullptr) {
		mcpFail("player '%s' has no build list entry %d (see buildlist.list)", sideName(sides, side).str(), index);
	}
	applyBuildListArgs(entry, args);
	if (args.has("new_index")) {
		Int count = 0;
		for (BuildListInfo *e = info->getBuildList(); e; e = e->getNext()) count++;
		const Int newIndex = mcpArgInt(args, "new_index");
		if (newIndex < 0 || newIndex >= count) {
			mcpFail("new_index must be 0..%d", count - 1);
		}
		info->removeFromBuildList(entry);
		entry->setNextBuildList(nullptr);
		info->addToBuildList(entry, newIndex);
		index = newIndex;
	}
	const McpJson j = describeBuildListEntry(entry, index);
	commitSides(sides);
	return j;
}

McpJson cmdDeleteBuildList(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	SidesInfo *info = sides.getSideInfo(side);
	const Int index = mcpArgInt(args, "index");
	BuildListInfo *entry = index >= 0 ? nthBuildListEntry(info, index) : nullptr;
	if (entry == nullptr) {
		mcpFail("player '%s' has no build list entry %d (see buildlist.list)", sideName(sides, side).str(), index);
	}
	McpJson j = McpJson::makeObject();
	j.set("deleted", entry->getTemplateName().str()).set("index", index);
	info->removeFromBuildList(entry);
	// Deleting an entry deletes what is linked behind it.
	entry->setNextBuildList(nullptr);
	deleteInstance(entry);
	commitSides(sides);
	return j;
}

} // namespace

void mcpRegisterScriptViewCommands()
{
	mcpRegisterCommand("scripts.trace", cmdTrace, "{name,player?,kind?=auto|script,depth?=4,max_scripts?=60} What a team or script sets in motion: the scripts it runs and those waiting for its flags and timers.");
	mcpRegisterCommand("scripts.build_order", cmdBuildOrder, "{player,difficulty?:E|N|H,include_inactive?=true} The scripts that build structures or upgrades, in evaluation order, with their conditions.");
	mcpRegisterCommand("scripts.by_difficulty", cmdByDifficulty, "{player,limit?=40} Scripts by their easy/normal/hard switches, and scripts and teams by the E/N/H word in their names.");
	mcpRegisterCommand("objects.info", cmdObjectInfo, "{templates:[names] | filter,limit?=20} Cost, side, KindOf, energy, vision, command set and producing structures of object types.");
	mcpRegisterCommand("buildlist.list", cmdListBuildList, "{player?} The players' build lists (structures the AI builds or starts with).");
	mcpRegisterCommand("buildlist.add", cmdAddBuildList, "{player,template,x,y,z?,angle_deg?,name?,script?,initially_built?,rebuilds?,health?,whiner?,unsellable?,repairable?,index?} Adds a build list entry.");
	mcpRegisterCommand("buildlist.set", cmdSetBuildList, "{player,entries:[{template,x,y,...fields of buildlist.add}]} Replaces a player's whole build list in one undo step.");
	mcpRegisterCommand("buildlist.update", cmdUpdateBuildList, "{player,index,new_index?,...fields of buildlist.add} Changes or reorders a build list entry.");
	mcpRegisterCommand("buildlist.delete", cmdDeleteBuildList, "{player,index} Deletes a build list entry.");
}
