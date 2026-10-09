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

// McpCmdScripts.cpp
// MCP bridge commands for what the script editor (Edit > Scripts) and the teams dialog edit:
// scripts, script groups, their conditions and actions, script file (.scb) import/export and
// team templates. Like the script editor, every edit works on a copy of the sides list and
// commits it as one undo step.

#include "StdAfx.h"
#include "resource.h"
#include "WorldBuilder.h"

#define DEFINE_BUILDABLE_STATUS_NAMES

#include "mcp/McpCommands.h"
#include "mcp/McpScriptsShared.h"
#include "mcp/McpUndoables.h"

#include "CUndoable.h"
#include "EditParameter.h"
#include "ScriptDialog.h"
#include "WorldBuilderDoc.h"
#include "Common/DataChunk.h"
#include "Common/KindOf.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameClient/View.h"
#include "GameLogic/AI.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/Module/ContainModule.h"

#include <algorithm>
#include <limits.h>
#include <map>
#include <math.h>
#include <set>
#include <stdio.h>
#include <utility>
#include <vector>

namespace McpScripts
{

const double MCP_PI = 3.14159265358979323846;
const char *const NEUTRAL_PLAYER_LABEL = "(neutral)";

// Chunk versions of the script file format, as written by ScriptDialog::OnSave.
const Int K_SCRIPT_FILE_PLAYERS_VERSION_2 = 2;
const Int K_SCRIPT_FILE_TEAMS_VERSION_1 = 1;

//-------------------------------------------------------------------------------------------------
// Parameter types and values
//-------------------------------------------------------------------------------------------------

// In the order of Parameter::ParameterType.
const char *const PARAMETER_TYPE_NAMES[] =
{
	"INT", "REAL", "SCRIPT", "TEAM", "COUNTER", "FLAG", "COMPARISON", "WAYPOINT", "BOOLEAN", "TRIGGER_AREA",
	"TEXT_STRING", "SIDE", "SOUND", "SCRIPT_SUBROUTINE", "UNIT", "OBJECT_TYPE", "COORD3D", "ANGLE", "TEAM_STATE",
	"RELATION", "AI_MOOD", "DIALOG", "MUSIC", "MOVIE", "WAYPOINT_PATH", "LOCALIZED_TEXT", "BRIDGE", "KIND_OF_PARAM",
	"ATTACK_PRIORITY_SET", "RADAR_EVENT_TYPE", "SPECIAL_POWER", "SCIENCE", "UPGRADE", "COMMANDBUTTON_ABILITY",
	"BOUNDARY", "BUILDABLE", "SURFACES_ALLOWED", "SHAKE_INTENSITY", "COMMAND_BUTTON", "FONT_NAME", "OBJECT_STATUS",
	"COMMANDBUTTON_ALL_ABILITIES", "SKIRMISH_WAYPOINT_PATH", "COLOR", "EMOTICON", "OBJECT_PANEL_FLAG", "FACTION_NAME",
	"OBJECT_TYPE_LIST", "REVEALNAME", "SCIENCE_AVAILABILITY", "LEFT_OR_RIGHT", "PERCENT"
};
static_assert(ARRAY_SIZE(PARAMETER_TYPE_NAMES) == Parameter::NUM_ITEMS, "Incorrect array size");

const char *parameterTypeName(Parameter::ParameterType type)
{
	return (type >= 0 && type < Parameter::NUM_ITEMS) ? PARAMETER_TYPE_NAMES[type] : "UNKNOWN";
}

Int parameterTypeFromName(const char *name)
{
	for (Int i = 0; i < Parameter::NUM_ITEMS; i++) {
		if (_stricmp(name, PARAMETER_TYPE_NAMES[i]) == 0) return i;
	}
	return -1;
}

struct EnumName
{
	const char *name;
	Int value;
};

// The first name of a value is the one reported; later ones are only accepted on input.
const EnumName COMPARISON_NAMES[] =
{
	{ "LESS_THAN", Parameter::LESS_THAN }, { "LESS_EQUAL", Parameter::LESS_EQUAL }, { "EQUAL", Parameter::EQUAL },
	{ "GREATER_EQUAL", Parameter::GREATER_EQUAL }, { "GREATER", Parameter::GREATER }, { "NOT_EQUAL", Parameter::NOT_EQUAL },
	{ "<", Parameter::LESS_THAN }, { "<=", Parameter::LESS_EQUAL }, { "==", Parameter::EQUAL },
	{ ">=", Parameter::GREATER_EQUAL }, { ">", Parameter::GREATER }, { "!=", Parameter::NOT_EQUAL },
	{ nullptr, 0 }
};
const EnumName RELATION_NAMES[] =
{
	{ "ENEMY", Parameter::REL_ENEMY }, { "NEUTRAL", Parameter::REL_NEUTRAL }, { "FRIEND", Parameter::REL_FRIEND },
	{ nullptr, 0 }
};
const EnumName AI_MOOD_NAMES[] =
{
	{ "SLEEP", ATTITUDE_SLEEP }, { "PASSIVE", ATTITUDE_PASSIVE }, { "NORMAL", ATTITUDE_NORMAL },
	{ "ALERT", ATTITUDE_ALERT }, { "AGGRESSIVE", ATTITUDE_AGGRESSIVE },
	{ nullptr, 0 }
};
const EnumName RADAR_EVENT_NAMES[] =
{
	{ "CONSTRUCTION", RADAR_EVENT_CONSTRUCTION }, { "UPGRADE", RADAR_EVENT_UPGRADE },
	{ "UNDER_ATTACK", RADAR_EVENT_UNDER_ATTACK }, { "INFORMATION", RADAR_EVENT_INFORMATION },
	{ "INFILTRATION", RADAR_EVENT_INFILTRATION },
	{ nullptr, 0 }
};
const EnumName LEFT_OR_RIGHT_NAMES[] =
{
	{ "LEFT", EVAC_TO_LEFT }, { "RIGHT", EVAC_TO_RIGHT }, { "CENTER", EVAC_BURST_FROM_CENTER },
	{ nullptr, 0 }
};

const EnumName *enumTable(Parameter::ParameterType type)
{
	switch (type) {
		case Parameter::COMPARISON: return COMPARISON_NAMES;
		case Parameter::RELATION: return RELATION_NAMES;
		case Parameter::AI_MOOD: return AI_MOOD_NAMES;
		case Parameter::RADAR_EVENT_TYPE: return RADAR_EVENT_NAMES;
		case Parameter::LEFT_OR_RIGHT: return LEFT_OR_RIGHT_NAMES;
		default: return nullptr;
	}
}

/// Engine name lists for parameters that store an index: names[i] belongs to the value base + i.
struct NameList
{
	const char *const *names;
	Int base;
	Int count;
};

bool getNameList(Parameter::ParameterType type, NameList *out)
{
	switch (type) {
		case Parameter::KIND_OF_PARAM:
			out->names = KindOfMaskType::getBitNames();
			out->base = KINDOF_FIRST;
			out->count = KINDOF_COUNT;
			return true;
		case Parameter::BUILDABLE:
			out->names = BuildableStatusNames;
			out->base = BSTATUS_YES;
			out->count = BSTATUS_NUM_TYPES;
			return true;
		case Parameter::SURFACES_ALLOWED:
			out->names = Surfaces;
			out->base = 1;
			out->count = 3;
			return true;
		case Parameter::SHAKE_INTENSITY:
			out->names = ShakeIntensities;
			out->base = 0;
			out->count = View::SHAKE_COUNT;
			return true;
		default:
			return false;
	}
}

bool isNamedIntType(Parameter::ParameterType type)
{
	NameList list;
	return enumTable(type) != nullptr || getNameList(type, &list);
}

const char *namedIntName(Parameter::ParameterType type, Int value)
{
	if (const EnumName *table = enumTable(type)) {
		for (; table->name; table++) {
			if (table->value == value) return table->name;
		}
		return nullptr;
	}
	NameList list;
	if (getNameList(type, &list) && value >= list.base && value < list.base + list.count) {
		return list.names[value - list.base];
	}
	return nullptr;
}

bool namedIntValue(Parameter::ParameterType type, const char *name, Int *value)
{
	if (const EnumName *table = enumTable(type)) {
		for (; table->name; table++) {
			if (_stricmp(table->name, name) == 0) {
				*value = table->value;
				return true;
			}
		}
		return false;
	}
	NameList list;
	if (getNameList(type, &list)) {
		for (Int i = 0; i < list.count && list.names[i]; i++) {
			if (_stricmp(list.names[i], name) == 0) {
				*value = list.base + i;
				return true;
			}
		}
	}
	return false;
}

void namedIntNameList(Parameter::ParameterType type, std::vector<std::string> &names)
{
	if (const EnumName *table = enumTable(type)) {
		// Later entries of a value are input aliases.
		for (; table->name; table++) {
			if (namedIntName(type, table->value) == table->name) names.push_back(table->name);
		}
		return;
	}
	NameList list;
	if (getNameList(type, &list)) {
		for (Int i = 0; i < list.count && list.names[i]; i++) {
			names.push_back(list.names[i]);
		}
	}
}

/// The names a named-int parameter accepts, for error messages.
std::string namedIntChoices(Parameter::ParameterType type)
{
	if (type == Parameter::KIND_OF_PARAM) {
		return "a KindOf name such as STRUCTURE or INFANTRY";
	}
	std::string choices;
	if (const EnumName *table = enumTable(type)) {
		for (; table->name; table++) {
			if (!choices.empty()) choices += ", ";
			choices += table->name;
		}
		return choices;
	}
	NameList list;
	if (getNameList(type, &list)) {
		for (Int i = 0; i < list.count && list.names[i]; i++) {
			if (!choices.empty()) choices += ", ";
			choices += list.names[i];
		}
	}
	return choices;
}

bool isStringParamType(Parameter::ParameterType type)
{
	switch (type) {
		case Parameter::INT:
		case Parameter::BOUNDARY:
		case Parameter::BOOLEAN:
		case Parameter::REAL:
		case Parameter::ANGLE:
		case Parameter::PERCENT:
		case Parameter::COORD3D:
		case Parameter::COLOR:
			return false;
		default:
			return !isNamedIntType(type);
	}
}

McpJson paramValueToJson(const Parameter *parm)
{
	const Parameter::ParameterType type = parm->getParameterType();
	switch (type) {
		case Parameter::INT:
		case Parameter::BOUNDARY:
			return McpJson((int)parm->getInt());
		case Parameter::BOOLEAN:
			return McpJson(parm->getInt() != 0);
		case Parameter::REAL:
			return McpJson((double)parm->getReal());
		case Parameter::ANGLE:
			return McpJson(parm->getReal() * 180.0 / MCP_PI);
		case Parameter::PERCENT:
			return McpJson(parm->getReal() * 100.0);
		case Parameter::COORD3D: {
			Coord3D pos;
			parm->getCoord3D(&pos);
			McpJson arr = McpJson::makeArray();
			arr.push(McpJson((double)pos.x));
			arr.push(McpJson((double)pos.y));
			arr.push(McpJson((double)pos.z));
			return arr;
		}
		case Parameter::COLOR: {
			char buf[16];
			sprintf(buf, "#%08X", (unsigned int)parm->getInt());
			return McpJson((const char *)buf);
		}
		default:
			break;
	}
	if (isNamedIntType(type)) {
		const char *name = namedIntName(type, parm->getInt());
		if (name != nullptr) {
			return McpJson(name);
		}
		return McpJson((int)parm->getInt());
	}
	return McpJson(parm->getString().str());
}

/// Sets a parameter from JSON: either the plain value or an object with a "value" key, as
/// scripts.get reports it. Stores what the parameter dialog would store for the type.
void setParamValue(Parameter *parm, const McpJson &in, const char *where, Int ndx)
{
	const McpJson &v = in.isObject() ? in.get("value") : in;
	const Parameter::ParameterType type = parm->getParameterType();
	const char *typeName = parameterTypeName(type);
	switch (type) {
		case Parameter::INT:
		case Parameter::BOUNDARY:
			if (!v.isNumber()) mcpFail("%s: parameter %d (%s) must be a number", where, ndx + 1, typeName);
			parm->friend_setInt((Int)floor(v.asNumber() + 0.5));
			return;
		case Parameter::BOOLEAN:
			if (v.isBool()) {
				parm->friend_setInt(v.asBool() ? 1 : 0);
			} else if (v.isNumber()) {
				parm->friend_setInt(v.asNumber() != 0 ? 1 : 0);
			} else {
				mcpFail("%s: parameter %d (%s) must be true or false", where, ndx + 1, typeName);
			}
			return;
		case Parameter::REAL:
		case Parameter::ANGLE:
		case Parameter::PERCENT: {
			if (!v.isNumber()) {
				mcpFail("%s: parameter %d (%s) must be a number%s", where, ndx + 1, typeName,
					type == Parameter::ANGLE ? " (degrees)" : (type == Parameter::PERCENT ? " (percent, 0..100)" : ""));
			}
			double value = v.asNumber();
			if (type == Parameter::ANGLE) value = value * MCP_PI / 180.0;
			if (type == Parameter::PERCENT) value = value / 100.0;
			parm->friend_setReal((Real)value);
			return;
		}
		case Parameter::COORD3D: {
			if (!v.isArray() || v.size() < 2 || v.size() > 3 || !v.at(0).isNumber() || !v.at(1).isNumber()
				|| (v.size() == 3 && !v.at(2).isNumber())) {
				mcpFail("%s: parameter %d (%s) must be [x, y, z]", where, ndx + 1, typeName);
			}
			Coord3D pos;
			pos.x = (Real)v.at(0).asNumber();
			pos.y = (Real)v.at(1).asNumber();
			pos.z = v.size() == 3 ? (Real)v.at(2).asNumber() : 0;
			parm->friend_setCoord3D(&pos);
			return;
		}
		case Parameter::COLOR: {
			if (v.isNumber()) {
				parm->friend_setInt((Int)(Int64)v.asNumber());
				return;
			}
			// "#RRGGBB" (opaque) or "#AARRGGBB".
			const char *text = v.isString() ? v.asString().c_str() : "";
			if (*text == '#') text++;
			const size_t len = strlen(text);
			unsigned int color = 0;
			int consumed = 0;
			if ((len != 6 && len != 8) || sscanf(text, "%x%n", &color, &consumed) != 1 || consumed != (int)len) {
				mcpFail("%s: parameter %d (%s) must be \"#RRGGBB\" or \"#AARRGGBB\"", where, ndx + 1, typeName);
			}
			if (len == 6) color |= 0xFF000000;
			parm->friend_setInt((Int)color);
			return;
		}
		default:
			break;
	}

	if (isNamedIntType(type)) {
		Int value = 0;
		if (v.isNumber()) {
			value = (Int)floor(v.asNumber() + 0.5);
			if (namedIntName(type, value) == nullptr) {
				mcpFail("%s: parameter %d (%s) has no value %d; use one of: %s", where, ndx + 1, typeName, value,
					namedIntChoices(type).c_str());
			}
		} else if (!v.isString() || !namedIntValue(type, v.asString().c_str(), &value)) {
			mcpFail("%s: parameter %d (%s) must be one of: %s", where, ndx + 1, typeName, namedIntChoices(type).c_str());
		}
		parm->friend_setInt(value);
		if (type == Parameter::KIND_OF_PARAM) {
			parm->friend_setString(AsciiString(namedIntName(type, value)));
		}
		return;
	}

	if (!v.isString()) {
		mcpFail("%s: parameter %d (%s) must be a string", where, ndx + 1, typeName);
	}
	AsciiString text(v.asString().c_str());
	if (type == Parameter::OBJECT_STATUS && !text.isEmpty()) {
		// Stored by name, so use the engine's spelling.
		const char *const *statusNames = ObjectStatusMaskType::getBitNames();
		Int i;
		for (i = 0; i < OBJECT_STATUS_COUNT && statusNames[i]; i++) {
			if (text.compareNoCase(statusNames[i]) == 0) {
				text = statusNames[i];
				break;
			}
		}
		if (i >= OBJECT_STATUS_COUNT || statusNames[i] == nullptr) {
			mcpFail("%s: parameter %d (%s): unknown object status '%s'", where, ndx + 1, typeName, text.str());
		}
	}
	parm->friend_setString(text);
}

template <class T>
McpJson describeParams(T *item)
{
	McpJson params = McpJson::makeArray();
	for (Int i = 0; i < item->getNumParameters(); i++) {
		Parameter *parm = item->getParameter(i);
		params.push(McpJson::makeObject()
			.set("type", parameterTypeName(parm->getParameterType()))
			.set("value", paramValueToJson(parm)));
	}
	return params;
}

/// Fills the parameters of a new condition or action from the "params" array of its JSON entry.
template <class T>
void applyParams(T *item, const McpJson &entry, const std::string &where)
{
	const McpJson &params = entry.get("params");
	if (!params.isNull() && !params.isArray()) {
		mcpFail("%s: 'params' must be an array", where.c_str());
	}
	const Int expected = item->getNumParameters();
	const Int given = (Int)params.size();
	if (given != expected) {
		std::string signature;
		for (Int i = 0; i < expected; i++) {
			if (i) signature += ", ";
			signature += parameterTypeName(item->getParameter(i)->getParameterType());
		}
		mcpFail("%s takes %d parameter(s) [%s], got %d", where.c_str(), expected, signature.c_str(), given);
	}
	for (Int i = 0; i < expected; i++) {
		setParamValue(item->getParameter(i), params.at(i), where.c_str(), i);
	}
}

//-------------------------------------------------------------------------------------------------
// Condition and action templates
//-------------------------------------------------------------------------------------------------

Int findActionType(const char *name)
{
	for (Int i = 0; i < ScriptAction::NUM_ITEMS; i++) {
		const ActionTemplate *at = TheScriptEngine->getActionTemplate(i);
		if (at && !at->m_internalName.isEmpty() && at->m_internalName.compareNoCase(name) == 0) return i;
	}
	return -1;
}

Int findConditionType(const char *name)
{
	for (Int i = 0; i < Condition::NUM_ITEMS; i++) {
		const ConditionTemplate *ct = TheScriptEngine->getConditionTemplate(i);
		if (ct && !ct->m_internalName.isEmpty() && ct->m_internalName.compareNoCase(name) == 0) return i;
	}
	return -1;
}

/// The "type" of a condition or action entry; a plain string is an entry without parameters.
std::string entryTypeName(const McpJson &entry, const char *where)
{
	if (entry.isString()) {
		return entry.asString();
	}
	if (!entry.isObject() || !entry.get("type").isString()) {
		mcpFail("%s must be an object with a string 'type' (and 'params')", where);
	}
	return entry.get("type").asString();
}

McpJson describeTemplate(const Template *t)
{
	McpJson j = McpJson::makeObject();
	j.set("type", t->m_internalName.str()).set("ui_name", t->m_uiName.str());
	McpJson params = McpJson::makeArray();
	// The sentence the editor shows, with the parameter types where the values go.
	std::string sentence;
	for (Int i = 0; i < MAX_PARMS; i++) {
		if (i < t->m_numUiStrings) sentence += t->m_uiStrings[i].str();
		if (i < t->getNumParameters()) {
			const char *typeName = parameterTypeName(t->getParameterType(i));
			params.push(McpJson(typeName));
			sentence += std::string("<") + typeName + ">";
		}
	}
	j.set("params", params).set("sentence", sentence);
	if (!t->m_helpText.isEmpty()) {
		j.set("help", t->m_helpText.str());
	}
	return j;
}

McpJson listTemplates(const McpJson &args, bool actions)
{
	const std::string filter = mcpArgString(args, "filter", "");
	const Int limit = mcpArgInt(args, "limit", 50);
	const Int offset = mcpArgInt(args, "offset", 0);
	const Int count = actions ? (Int)ScriptAction::NUM_ITEMS : (Int)Condition::NUM_ITEMS;
	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (Int i = 0; i < count; i++) {
		const Template *t = actions ? (const Template *)TheScriptEngine->getActionTemplate(i)
			: (const Template *)TheScriptEngine->getConditionTemplate(i);
		if (t == nullptr || t->m_internalName.isEmpty()) continue;
		if (!mcpContainsNoCase(t->m_internalName.str(), filter.c_str()) && !mcpContainsNoCase(t->m_uiName.str(), filter.c_str())
			&& !mcpContainsNoCase(t->m_helpText.str(), filter.c_str())) continue;
		matched++;
		if (matched <= offset || (Int)list.size() >= limit) continue;
		list.push(describeTemplate(t));
	}
	return McpJson::makeObject().set("total_matches", matched).set(actions ? "actions" : "conditions", list);
}

McpJson cmdListActions(const McpJson &args)
{
	return listTemplates(args, true);
}

McpJson cmdListConditions(const McpJson &args)
{
	return listTemplates(args, false);
}

//-------------------------------------------------------------------------------------------------
// Scripts <-> JSON
//-------------------------------------------------------------------------------------------------

ScriptAction *buildActions(const McpJson &list, const char *key)
{
	if (!list.isArray()) {
		mcpFail("'%s' must be an array of actions", key);
	}
	ScriptAction *head = nullptr;
	ScriptAction *tail = nullptr;
	try {
		for (size_t i = 0; i < list.size(); i++) {
			char where[64];
			sprintf(where, "%s[%d]", key, (int)i);
			const std::string typeName = entryTypeName(list.at(i), where);
			const Int type = findActionType(typeName.c_str());
			if (type < 0) {
				mcpFail("%s: unknown action '%s' (see scripts.list_actions)", where, typeName.c_str());
			}
			ScriptAction *action = newInstance(ScriptAction)((ScriptAction::ScriptActionType)type);
			action->setNextAction(nullptr);
			action->setWarnings(false);
			if (tail) tail->setNextAction(action); else head = action;
			tail = action;
			applyParams(action, list.at(i), std::string(where) + " " + typeName);
		}
	} catch (...) {
		deleteInstance(head);
		throw;
	}
	return head;
}

/// Conditions are a list of OR clauses, each a list of conditions that must all be true. A flat
/// list of conditions is taken as a single clause.
OrCondition *buildConditions(const McpJson &list)
{
	if (!list.isArray() || list.size() == 0) {
		mcpFail("'conditions' must be a non-empty array (use CONDITION_TRUE for a script that always runs)");
	}
	const bool nested = list.at(0).isArray();
	const size_t numOrs = nested ? list.size() : 1;
	OrCondition *head = nullptr;
	OrCondition *tail = nullptr;
	try {
		for (size_t o = 0; o < numOrs; o++) {
			const McpJson &clause = nested ? list.at(o) : list;
			if (!clause.isArray() || clause.size() == 0) {
				mcpFail("conditions[%d] must be a non-empty array of conditions", (int)o);
			}
			OrCondition *orCond = newInstance(OrCondition);
			if (tail) tail->setNextOrCondition(orCond); else head = orCond;
			tail = orCond;
			Condition *lastAnd = nullptr;
			for (size_t a = 0; a < clause.size(); a++) {
				char where[64];
				sprintf(where, "conditions[%d][%d]", (int)o, (int)a);
				const std::string typeName = entryTypeName(clause.at(a), where);
				const Int type = findConditionType(typeName.c_str());
				if (type < 0) {
					mcpFail("%s: unknown condition '%s' (see scripts.list_conditions)", where, typeName.c_str());
				}
				Condition *cond = newInstance(Condition)((Condition::ConditionType)type);
				if (lastAnd) lastAnd->setNextCondition(cond); else orCond->setFirstAndCondition(cond);
				lastAnd = cond;
				applyParams(cond, clause.at(a), std::string(where) + " " + typeName);
			}
		}
	} catch (...) {
		deleteInstance(head);
		throw;
	}
	return head;
}

void collectParamUses(Script *script, std::vector<ParamUse> &out)
{
	ParamUse use;
	use.inCondition = true;
	for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition()) {
		for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext()) {
			for (Int i = 0; i < cond->getNumParameters(); i++) {
				use.parm = cond->getParameter(i);
				out.push_back(use);
			}
		}
	}
	use.inCondition = false;
	for (Int pass = 0; pass < 2; pass++) {
		for (ScriptAction *action = pass ? script->getFalseAction() : script->getAction(); action; action = action->getNext()) {
			for (Int i = 0; i < action->getNumParameters(); i++) {
				use.parm = action->getParameter(i);
				out.push_back(use);
			}
		}
	}
}

McpJson describeActions(ScriptAction *first)
{
	McpJson list = McpJson::makeArray();
	for (ScriptAction *action = first; action; action = action->getNext()) {
		const ActionTemplate *at = TheScriptEngine->getActionTemplate(action->getActionType());
		list.push(McpJson::makeObject()
			.set("type", at ? at->m_internalName.str() : "")
			.set("params", describeParams(action))
			.set("text", action->getUiText().str()));
	}
	return list;
}

McpJson describeConditions(Script *script)
{
	McpJson ors = McpJson::makeArray();
	for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition()) {
		McpJson ands = McpJson::makeArray();
		for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext()) {
			const ConditionTemplate *ct = TheScriptEngine->getConditionTemplate(cond->getConditionType());
			ands.push(McpJson::makeObject()
				.set("type", ct ? ct->m_internalName.str() : "")
				.set("params", describeParams(cond))
				.set("text", cond->getUiText().str()));
		}
		ors.push(ands);
	}
	return ors;
}

McpJson describeScriptBrief(Script *script)
{
	McpJson j = McpJson::makeObject();
	j.set("name", script->getName().str());
	j.set("active", (bool)(script->isActive() != 0));
	j.set("subroutine", (bool)(script->isSubroutine() != 0));
	if (!script->getComment().isEmpty()) {
		j.set("comment", script->getComment().str());
	}
	return j;
}

McpJson describeScript(Script *script)
{
	McpJson j = McpJson::makeObject();
	j.set("name", script->getName().str());
	j.set("comment", script->getComment().str());
	j.set("condition_comment", script->getConditionComment().str());
	j.set("action_comment", script->getActionComment().str());
	j.set("active", (bool)(script->isActive() != 0));
	j.set("one_shot", (bool)(script->isOneShot() != 0));
	j.set("subroutine", (bool)(script->isSubroutine() != 0));
	j.set("easy", (bool)(script->isEasy() != 0));
	j.set("normal", (bool)(script->isNormal() != 0));
	j.set("hard", (bool)(script->isHard() != 0));
	j.set("delay_seconds", (int)script->getDelayEvalSeconds());
	j.set("conditions", describeConditions(script));
	j.set("actions", describeActions(script->getAction()));
	j.set("false_actions", describeActions(script->getFalseAction()));
	return j;
}

/// Applies the keys present in a script JSON object; other parts of the script are kept. The
/// script must already belong to a sides list copy, which cleans up if this fails halfway.
void applyScriptJson(Script *script, const McpJson &def)
{
	static const char *const KNOWN_KEYS[] = { "name", "comment", "condition_comment", "action_comment", "active",
		"one_shot", "subroutine", "easy", "normal", "hard", "delay_seconds", "conditions", "actions", "false_actions",
		nullptr };
	for (size_t i = 0; i < def.numKeys(); i++) {
		Bool known = false;
		for (Int k = 0; KNOWN_KEYS[k] && !known; k++) {
			known = def.keyAt(i) == KNOWN_KEYS[k];
		}
		if (!known) {
			mcpFail("unknown script key '%s'", def.keyAt(i).c_str());
		}
	}

	if (def.has("name")) {
		const std::string name = mcpArgString(def, "name");
		if (name.empty()) mcpFail("the script name must not be empty");
		script->setName(AsciiString(name.c_str()));
	}
	if (def.has("comment")) script->setComment(AsciiString(mcpArgString(def, "comment").c_str()));
	if (def.has("condition_comment")) script->setConditionComment(AsciiString(mcpArgString(def, "condition_comment").c_str()));
	if (def.has("action_comment")) script->setActionComment(AsciiString(mcpArgString(def, "action_comment").c_str()));
	if (def.has("active")) script->setActive(mcpArgBool(def, "active", true));
	if (def.has("one_shot")) script->setOneShot(mcpArgBool(def, "one_shot", true));
	if (def.has("subroutine")) script->setSubroutine(mcpArgBool(def, "subroutine", false));
	if (def.has("easy")) script->setEasy(mcpArgBool(def, "easy", true));
	if (def.has("normal")) script->setNormal(mcpArgBool(def, "normal", true));
	if (def.has("hard")) script->setHard(mcpArgBool(def, "hard", true));
	if (def.has("delay_seconds")) {
		const Int delay = mcpArgInt(def, "delay_seconds");
		if (delay < 0) mcpFail("delay_seconds must be >= 0");
		script->setDelayEvalSeconds(delay);
	}
	if (def.has("conditions")) {
		OrCondition *conditions = buildConditions(def.get("conditions"));
		deleteInstance(script->getOrCondition());
		script->setOrCondition(conditions);
	}
	if (def.has("actions")) {
		ScriptAction *actions = buildActions(def.get("actions"), "actions");
		deleteInstance(script->getAction());
		script->setAction(actions);
	}
	if (def.has("false_actions")) {
		ScriptAction *actions = buildActions(def.get("false_actions"), "false_actions");
		deleteInstance(script->getFalseAction());
		script->setFalseAction(actions);
	}
}

//-------------------------------------------------------------------------------------------------
// Finding players, groups and scripts in a sides list
//-------------------------------------------------------------------------------------------------

/// The script editor works on its own copy of the sides list and writes it back on OK, which
/// would silently undo an edit made here in the meantime. It also owns the list that parameter
/// checks run against.
void requireScriptEditorClosed()
{
	if (ScriptDialog::isOpen()) {
		mcpFail("the WorldBuilder script editor window is open; close it (OK or Cancel) and retry");
	}
}

AsciiString sideName(SidesList &sides, Int side)
{
	return sides.getSideInfo(side)->getDict()->getAsciiString(TheKey_playerName);
}

Int findSide(SidesList &sides, const AsciiString &name)
{
	for (Int i = 0; i < sides.getNumSides(); i++) {
		if (sideName(sides, i) == name) return i;
	}
	return -1;
}

/// Player index by name; the neutral player is "" or "(neutral)".
Int requireSide(SidesList &sides, const std::string &name)
{
	AsciiString playerName(name.c_str());
	if (playerName.compareNoCase(NEUTRAL_PLAYER_LABEL) == 0) {
		playerName.clear();
	}
	const Int side = findSide(sides, playerName);
	if (side < 0) {
		mcpFail("no player named '%s' (see sides.list; the neutral player is \"\")", name.c_str());
	}
	return side;
}

ScriptList *requireScriptList(SidesList &sides, Int side)
{
	ScriptList *list = sides.getSideInfo(side)->getScriptList();
	if (list == nullptr) {
		mcpFail("player '%s' has no script list", sideName(sides, side).str());
	}
	return list;
}

std::vector<ScriptLoc> allScripts(SidesList &sides)
{
	std::vector<ScriptLoc> all;
	for (Int side = 0; side < sides.getNumSides(); side++) {
		ScriptList *list = sides.getSideInfo(side)->getScriptList();
		if (list == nullptr) continue;
		ScriptLoc loc;
		loc.side = side;
		loc.group = nullptr;
		loc.index = 0;
		for (loc.script = list->getScript(); loc.script; loc.script = loc.script->getNext(), loc.index++) {
			all.push_back(loc);
		}
		for (loc.group = list->getScriptGroup(); loc.group; loc.group = loc.group->getNext()) {
			loc.index = 0;
			for (loc.script = loc.group->getScript(); loc.script; loc.script = loc.script->getNext(), loc.index++) {
				all.push_back(loc);
			}
		}
	}
	return all;
}

/// Script names only have to be unique within a player: AI script sets repeat the same names
/// for every faction, and the game qualifies them per player when it loads them.
bool scriptNameExists(SidesList &sides, Int side, const std::string &name)
{
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		if (all[i].side == side && name == all[i].script->getName().str()) return true;
	}
	return false;
}

std::string describeLocation(SidesList &sides, Int side, ScriptGroup *group)
{
	AsciiString player = sideName(sides, side);
	std::string where = std::string("player '") + (player.isEmpty() ? NEUTRAL_PLAYER_LABEL : player.str()) + "'";
	if (group) {
		where += std::string(" group '") + group->getName().str() + "'";
	}
	return where;
}

/// A group by name, in one player (side >= 0) or in the whole map.
GroupLoc requireGroup(SidesList &sides, const std::string &name, Int side)
{
	std::vector<GroupLoc> matches;
	for (Int i = 0; i < sides.getNumSides(); i++) {
		if (side >= 0 && i != side) continue;
		ScriptList *list = sides.getSideInfo(i)->getScriptList();
		if (list == nullptr) continue;
		GroupLoc loc;
		loc.side = i;
		loc.index = 0;
		for (loc.group = list->getScriptGroup(); loc.group; loc.group = loc.group->getNext(), loc.index++) {
			if (name == loc.group->getName().str()) matches.push_back(loc);
		}
	}
	if (matches.empty()) {
		mcpFail("no script group named '%s'%s (see scripts.list)", name.c_str(), side >= 0 ? " for that player" : "");
	}
	if (matches.size() > 1) {
		mcpFail("%d script groups are named '%s'; pass player to pick one", (int)matches.size(), name.c_str());
	}
	return matches[0];
}

/// The script named by args.name, narrowed by the optional args.player, args.group
/// (group "" = top level) and args.script_index (position in its list), since maps do hold
/// several scripts of the same name.
ScriptLoc requireScript(SidesList &sides, const McpJson &args)
{
	const std::string name = mcpArgString(args, "name");
	const Int side = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const bool hasGroup = args.has("group");
	const std::string group = mcpArgString(args, "group", "");
	const Int scriptIndex = mcpArgInt(args, "script_index", -1);

	const std::vector<ScriptLoc> all = allScripts(sides);
	std::vector<ScriptLoc> matches;
	for (size_t i = 0; i < all.size(); i++) {
		const ScriptLoc &loc = all[i];
		if (name != loc.script->getName().str()) continue;
		if (side >= 0 && loc.side != side) continue;
		if (hasGroup && group != (loc.group ? loc.group->getName().str() : "")) continue;
		if (scriptIndex >= 0 && loc.index != scriptIndex) continue;
		matches.push_back(loc);
	}
	if (matches.empty()) {
		mcpFail("no script named '%s'%s (see scripts.list)", name.c_str(),
			(side >= 0 || hasGroup || scriptIndex >= 0) ? " in that player/group/position" : "");
	}
	if (matches.size() > 1) {
		std::string places;
		for (size_t i = 0; i < matches.size() && i < 4; i++) {
			char position[32];
			sprintf(position, " script_index %d", matches[i].index);
			if (i) places += "; ";
			places += describeLocation(sides, matches[i].side, matches[i].group) + position;
		}
		mcpFail("%d scripts are named '%s' (%s); pass player, group (\"\" = not in a group) and script_index to pick one",
			(int)matches.size(), name.c_str(), places.c_str());
	}
	return matches[0];
}

/// Where a new or moved script goes: a group (optionally of a given player) or a player's top level.
void resolveDestination(SidesList &sides, const McpJson &args, const char *playerKey, const char *groupKey,
	Int *side, ScriptGroup **group)
{
	const std::string groupName = mcpArgString(args, groupKey, "");
	if (!groupName.empty()) {
		const GroupLoc loc = requireGroup(sides, groupName, args.has(playerKey) ? requireSide(sides, mcpArgString(args, playerKey)) : -1);
		*side = loc.side;
		*group = loc.group;
		return;
	}
	if (!args.has(playerKey)) {
		mcpFail("pass '%s' (a player name, \"\" for the neutral player) or '%s'", playerKey, groupKey);
	}
	*side = requireSide(sides, mcpArgString(args, playerKey));
	*group = nullptr;
}

Int argIndex(const McpJson &args)
{
	const Int index = mcpArgInt(args, "index", INT_MAX);
	if (index < 0) {
		mcpFail("index must be >= 0");
	}
	return index;
}

McpJson locationJson(SidesList &sides, Int side, ScriptGroup *group)
{
	McpJson j = McpJson::makeObject();
	j.set("player", sideName(sides, side).str());
	if (group) {
		j.set("group", group->getName().str());
	}
	return j;
}

bool s_sidesBatch = false;

void setSidesBatch(bool active)
{
	s_sidesBatch = active;
}

void commitSides(SidesList &sides)
{
	if (s_sidesBatch) {
		// The next command of the batch has to see this edit.
		sides.validateSides();
		*TheSidesList = sides;
		return;
	}
	mcpCommit(new SidesListUndoable(sides, mcpDoc()));
}

//-------------------------------------------------------------------------------------------------
// Warnings
//-------------------------------------------------------------------------------------------------

ParameterCheckScope::ParameterCheckScope(SidesList *sides)
{
	EditParameter::setCurSidesList(sides);
}

ParameterCheckScope::~ParameterCheckScope()
{
	EditParameter::setCurSidesList(nullptr);
}

template <class T>
Bool collectParamWarnings(T *item, Bool isAction, const std::string &where, const McpJson &scriptRef, McpJson &out)
{
	Bool any = false;
	for (Int i = 0; i < item->getNumParameters(); i++) {
		AsciiString warning = EditParameter::getWarningText(item->getParameter(i), isAction);
		warning.trim();
		if (warning.isEmpty()) continue;
		any = true;
		McpJson entry = scriptRef;
		entry.set("where", where).set("parameter", i + 1).set("warning", warning.str());
		out.push(entry);
	}
	item->setWarnings(any);
	return any;
}

Bool collectActionWarnings(ScriptAction *first, const char *key, const McpJson &scriptRef, McpJson &out)
{
	Bool any = false;
	Int ndx = 0;
	for (ScriptAction *action = first; action; action = action->getNext(), ndx++) {
		char where[64];
		sprintf(where, "%s[%d]", key, ndx);
		if (collectParamWarnings(action, TRUE, where, scriptRef, out)) any = true;
	}
	return any;
}

/// Runs the script editor's parameter checks on one script and updates its warning flags.
/// Needs a ParameterCheckScope for the sides list the script is in.
void collectScriptWarnings(SidesList &sides, Int side, Script *script, McpJson &out)
{
	McpJson scriptRef = McpJson::makeObject();
	scriptRef.set("player", sideName(sides, side).str()).set("script", script->getName().str());
	Bool any = false;
	Int o = 0;
	for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition(), o++) {
		Int a = 0;
		for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext(), a++) {
			char where[64];
			sprintf(where, "conditions[%d][%d]", o, a);
			if (collectParamWarnings(cond, FALSE, where, scriptRef, out)) any = true;
		}
	}
	if (collectActionWarnings(script->getAction(), "actions", scriptRef, out)) any = true;
	if (collectActionWarnings(script->getFalseAction(), "false_actions", scriptRef, out)) any = true;
	script->setWarnings(any);
}

McpJson scriptWarnings(SidesList &sides, Int side, Script *script)
{
	ParameterCheckScope scope(&sides);
	McpJson warnings = McpJson::makeArray();
	collectScriptWarnings(sides, side, script, warnings);
	return warnings;
}

//-------------------------------------------------------------------------------------------------
// scripts.list / scripts.get / scripts.verify
//-------------------------------------------------------------------------------------------------

/// How much scripts.list reports per script.
enum ListDetail
{
	LIST_GROUPS,	///< Groups with their script counts only.
	LIST_NAMES,		///< Script names.
	LIST_FULL			///< Name, flags and comment per script.
};

/// Adds the scripts of one list that pass the filter, in the wanted detail, and counts them.
void listScriptsInto(Script *first, const std::string &filter, ListDetail detail, McpJson &entry, Int *total)
{
	McpJson scripts = McpJson::makeArray();
	Int count = 0, inactive = 0, subroutines = 0;
	for (Script *script = first; script; script = script->getNext()) {
		if (!mcpContainsNoCase(script->getName().str(), filter.c_str())) continue;
		count++;
		if (!script->isActive()) inactive++;
		if (script->isSubroutine()) subroutines++;
		if (detail == LIST_FULL) {
			scripts.push(describeScriptBrief(script));
		} else if (detail == LIST_NAMES) {
			scripts.push(McpJson(script->getName().str()));
		}
	}
	*total += count;
	entry.set("script_count", count);
	if (inactive) entry.set("inactive", inactive);
	if (subroutines) entry.set("subroutines", subroutines);
	if (detail != LIST_GROUPS) {
		entry.set("scripts", scripts);
	}
}

McpJson cmdListScripts(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const std::string filter = mcpArgString(args, "filter", "");
	const bool hasGroup = args.has("group");
	const std::string onlyGroup = mcpArgString(args, "group", "");

	// AI script sets hold thousands of scripts, so the default detail depends on how many match.
	const std::string detailName = mcpArgString(args, "detail", "auto");
	ListDetail detail = LIST_FULL;
	bool reduced = false;
	if (detailName == "groups") {
		detail = LIST_GROUPS;
	} else if (detailName == "names") {
		detail = LIST_NAMES;
	} else if (detailName == "auto") {
		Int matching = 0;
		const std::vector<ScriptLoc> all = allScripts(sides);
		for (size_t i = 0; i < all.size(); i++) {
			if (onlySide >= 0 && all[i].side != onlySide) continue;
			if (hasGroup && onlyGroup != (all[i].group ? all[i].group->getName().str() : "")) continue;
			if (mcpContainsNoCase(all[i].script->getName().str(), filter.c_str())) matching++;
		}
		if (matching > 400) {
			detail = LIST_GROUPS;
			reduced = true;
		} else if (matching > 120) {
			detail = LIST_NAMES;
			reduced = true;
		}
	} else if (detailName != "full") {
		mcpFail("detail must be auto, groups, names or full");
	}

	McpJson players = McpJson::makeArray();
	Int total = 0;
	for (Int side = 0; side < sides.getNumSides(); side++) {
		if (onlySide >= 0 && side != onlySide) continue;
		ScriptList *list = sides.getSideInfo(side)->getScriptList();
		if (list == nullptr) continue;
		McpJson player = McpJson::makeObject();
		player.set("player", sideName(sides, side).str());
		// The scripts outside of any group are the group "".
		if (!hasGroup || onlyGroup.empty()) {
			listScriptsInto(list->getScript(), filter, detail, player, &total);
		}
		McpJson groups = McpJson::makeArray();
		for (ScriptGroup *group = list->getScriptGroup(); group; group = group->getNext()) {
			if (hasGroup && onlyGroup != group->getName().str()) continue;
			McpJson entry = McpJson::makeObject();
			entry.set("name", group->getName().str());
			if (!group->isActive()) entry.set("active", false);
			if (group->isSubroutine()) entry.set("subroutine", true);
			Int inGroup = 0;
			listScriptsInto(group->getScript(), filter, detail, entry, &inGroup);
			// With a filter, only groups that hold a match are listed.
			if (!filter.empty() && inGroup == 0) continue;
			total += inGroup;
			groups.push(entry);
		}
		player.set("groups", groups);
		players.push(player);
	}
	McpJson j = McpJson::makeObject();
	j.set("players", players).set("script_count", total);
	j.set("detail", detail == LIST_GROUPS ? "groups" : (detail == LIST_NAMES ? "names" : "full"));
	if (reduced) {
		j.set("note", "many scripts: detail was reduced. Narrow with player, group or filter, or pass detail.");
	}
	return j;
}

McpJson cmdGetScript(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const ScriptLoc loc = requireScript(sides, args);
	McpJson j = locationJson(sides, loc.side, loc.group);
	j.set("index", loc.index);
	j.set("script", describeScript(loc.script));
	// The whole script as the editor words it.
	j.set("text", loc.script->getUiText().str());
	return j;
}

bool moreWarnings(const std::pair<Int, std::string> &a, const std::pair<Int, std::string> &b)
{
	return a.first != b.first ? a.first > b.first : a.second < b.second;
}

/// Warnings by their text with a count and the players they occur for, most frequent first.
McpJson summarizeWarnings(const McpJson &warnings)
{
	std::map<std::string, Int> counts;
	std::map<std::string, std::set<std::string> > players;
	for (size_t i = 0; i < warnings.size(); i++) {
		const std::string &text = warnings.at(i).get("warning").asString();
		counts[text]++;
		const McpJson &owner = warnings.at(i).has("player") ? warnings.at(i).get("player") : warnings.at(i).get("owner");
		players[text].insert(owner.asString());
	}
	std::vector<std::pair<Int, std::string> > order;
	for (std::map<std::string, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
		order.push_back(std::make_pair(it->second, it->first));
	}
	std::sort(order.begin(), order.end(), moreWarnings);
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < order.size() && i < 60; i++) {
		McpJson who = McpJson::makeArray();
		const std::set<std::string> &names = players[order[i].second];
		for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it) {
			who.push(McpJson(*it));
		}
		list.push(McpJson::makeObject().set("warning", order[i].second).set("count", order[i].first).set("players", who));
	}
	return list;
}

McpJson cmdVerifyScripts(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const std::string onlyName = mcpArgString(args, "name", "");

	// Not all of the editor's checks fall back to the map's sides list when none is set.
	ParameterCheckScope scope(TheSidesList);
	McpJson warnings = McpJson::makeArray();
	const std::vector<ScriptLoc> all = allScripts(sides);
	std::set<std::pair<Int, std::string> > names;
	McpJson duplicates = McpJson::makeArray();
	Int checked = 0;
	for (size_t i = 0; i < all.size(); i++) {
		const std::string name = all[i].script->getName().str();
		if (onlySide >= 0 && all[i].side != onlySide) continue;
		// A name one player uses twice is ambiguous wherever a script refers to it.
		if (!names.insert(std::make_pair(all[i].side, name)).second) {
			duplicates.push(McpJson::makeObject().set("player", sideName(sides, all[i].side).str()).set("script", name));
		}
		if (!onlyName.empty() && name != onlyName) continue;
		collectScriptWarnings(sides, all[i].side, all[i].script, warnings);
		checked++;
	}
	// The editor shows a group as flagged when one of its scripts is.
	for (Int side = 0; side < sides.getNumSides(); side++) {
		ScriptList *list = sides.getSideInfo(side)->getScriptList();
		if (list == nullptr) continue;
		for (ScriptGroup *group = list->getScriptGroup(); group; group = group->getNext()) {
			Bool any = false;
			for (Script *script = group->getScript(); script; script = script->getNext()) {
				if (script->hasWarnings()) any = true;
			}
			group->setWarnings(any);
		}
	}
	const bool summary = mcpArgBool(args, "summary", false);
	McpJson j = McpJson::makeObject();
	j.set("scripts_checked", checked).set("warning_count", (int)warnings.size());
	j.set("warnings", summary ? summarizeWarnings(warnings) : warnings);
	j.set("duplicate_names", summary ? McpJson((int)duplicates.size()) : duplicates);
	if (onlyName.empty()) {
		Int teamsChecked = 0;
		const McpJson teamWarnings = verifyTeams(sides, onlySide, "", &teamsChecked);
		j.set("team_warning_count", (int)teamWarnings.size());
		j.set("team_warnings", summary ? summarizeWarnings(teamWarnings) : teamWarnings);
		j.set("teams_checked", teamsChecked);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.find
//-------------------------------------------------------------------------------------------------

struct FindQuery
{
	std::string text;				///< Substring of the editor wording of a condition or action.
	std::string value;			///< Exact parameter value.
	std::string type;				///< Condition or action type.
	Int paramType;					///< Parameter::ParameterType, or -1 for any.
};

/// A parameter value as the text scripts.get shows for it.
std::string paramValueText(const Parameter *parm)
{
	const McpJson value = paramValueToJson(parm);
	return value.isString() ? value.asString() : value.dump();
}

/// True if a condition or action matches every part of the query. value and param_type have to
/// hold for the same parameter.
template <class T>
bool entryMatches(T *item, const char *typeName, const FindQuery &query)
{
	if (!query.type.empty() && _stricmp(query.type.c_str(), typeName) != 0) {
		return false;
	}
	if (!query.value.empty() || query.paramType >= 0) {
		bool found = false;
		for (Int i = 0; i < item->getNumParameters() && !found; i++) {
			Parameter *parm = item->getParameter(i);
			if (query.paramType >= 0 && parm->getParameterType() != query.paramType) continue;
			if (!query.value.empty() && _stricmp(query.value.c_str(), paramValueText(parm).c_str()) != 0) continue;
			found = true;
		}
		if (!found) {
			return false;
		}
	}
	if (!query.text.empty() && !mcpContainsNoCase(item->getUiText().str(), query.text.c_str())) {
		return false;
	}
	return true;
}

void findInActions(ScriptAction *first, const char *key, const FindQuery &query, McpJson &hits)
{
	Int ndx = 0;
	for (ScriptAction *action = first; action; action = action->getNext(), ndx++) {
		const ActionTemplate *at = TheScriptEngine->getActionTemplate(action->getActionType());
		const char *typeName = at ? at->m_internalName.str() : "";
		if (!entryMatches(action, typeName, query)) continue;
		char where[64];
		sprintf(where, "%s[%d]", key, ndx);
		hits.push(McpJson::makeObject().set("where", where).set("type", typeName).set("text", action->getUiText().str()));
	}
}

/// Searches inside scripts: which conditions and actions use a type, a parameter value (a team,
/// script, waypoint, object type ...) or some wording. Without type and param_type it also looks
/// at script names and comments and at the team properties, which name scripts too.
McpJson cmdFindScripts(const McpJson &args)
{
	mcpDoc();
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;

	FindQuery query;
	query.text = mcpArgString(args, "text", "");
	query.value = mcpArgString(args, "value", "");
	query.type = mcpArgString(args, "type", "");
	query.paramType = -1;
	if (args.has("param_type")) {
		const std::string paramType = mcpArgString(args, "param_type");
		for (Int i = 0; i < Parameter::NUM_ITEMS; i++) {
			if (_stricmp(paramType.c_str(), PARAMETER_TYPE_NAMES[i]) == 0) query.paramType = i;
		}
		if (query.paramType < 0) {
			mcpFail("unknown param_type '%s' (e.g. TEAM, SCRIPT, WAYPOINT, OBJECT_TYPE, FLAG, COUNTER)", paramType.c_str());
		}
	}
	if (query.text.empty() && query.value.empty() && query.type.empty() && query.paramType < 0) {
		mcpFail("pass at least one of text, value, type, param_type");
	}
	if (!query.type.empty() && findActionType(query.type.c_str()) < 0 && findConditionType(query.type.c_str()) < 0) {
		mcpFail("unknown condition or action type '%s' (see scripts.list_actions / scripts.list_conditions)", query.type.c_str());
	}
	const Int onlySide = args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1;
	const Int limit = mcpArgInt(args, "limit", 50);
	const Int offset = mcpArgInt(args, "offset", 0);
	const Int maxHits = mcpArgInt(args, "max_hits_per_script", 10);
	// Names, comments and team properties are plain text, so they only take part in a text or value search.
	const bool plainText = query.type.empty() && query.paramType < 0;

	McpJson found = McpJson::makeArray();
	Int matchedScripts = 0, totalHits = 0;
	const std::vector<ScriptLoc> all = allScripts(sides);
	for (size_t i = 0; i < all.size(); i++) {
		const ScriptLoc &loc = all[i];
		if (onlySide >= 0 && loc.side != onlySide) continue;
		Script *script = loc.script;
		McpJson hits = McpJson::makeArray();
		if (plainText) {
			const AsciiString name = script->getName();
			const AsciiString comment = script->getComment();
			if (query.value.empty() ? mcpContainsNoCase(name.str(), query.text.c_str()) : (_stricmp(name.str(), query.value.c_str()) == 0
				&& mcpContainsNoCase(name.str(), query.text.c_str()))) {
				hits.push(McpJson::makeObject().set("where", "name").set("text", name.str()));
			}
			if (query.value.empty() && !comment.isEmpty() && mcpContainsNoCase(comment.str(), query.text.c_str())) {
				hits.push(McpJson::makeObject().set("where", "comment").set("text", comment.str()));
			}
		}
		Int o = 0;
		for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition(), o++) {
			Int a = 0;
			for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext(), a++) {
				const ConditionTemplate *ct = TheScriptEngine->getConditionTemplate(cond->getConditionType());
				const char *typeName = ct ? ct->m_internalName.str() : "";
				if (!entryMatches(cond, typeName, query)) continue;
				char where[64];
				sprintf(where, "conditions[%d][%d]", o, a);
				hits.push(McpJson::makeObject().set("where", where).set("type", typeName).set("text", cond->getUiText().str()));
			}
		}
		findInActions(script->getAction(), "actions", query, hits);
		findInActions(script->getFalseAction(), "false_actions", query, hits);
		if (hits.size() == 0) continue;

		matchedScripts++;
		totalHits += (Int)hits.size();
		if (matchedScripts <= offset || (Int)found.size() >= limit) continue;
		McpJson entry = locationJson(sides, loc.side, loc.group);
		entry.set("script", script->getName().str()).set("script_index", loc.index);
		entry.set("active", (bool)(script->isActive() != 0)).set("hit_count", (int)hits.size());
		if ((Int)hits.size() > maxHits) {
			// Scripts that define object lists can match hundreds of times.
			McpJson shown = McpJson::makeArray();
			for (Int h = 0; h < maxHits; h++) {
				shown.push(hits.at(h));
			}
			entry.set("hits", shown);
		} else {
			entry.set("hits", hits);
		}
		found.push(entry);
	}

	McpJson teams = McpJson::makeArray();
	Int totalTeamHits = 0;
	if (plainText) {
		for (Int i = 0; i < sides.getNumTeams(); i++) {
			Dict *d = sides.getTeamInfo(i)->getDict();
			const AsciiString owner = d->getAsciiString(TheKey_teamOwner);
			if (onlySide >= 0 && owner != sideName(sides, onlySide)) continue;
			for (Int k = 0; k < d->getPairCount(); k++) {
				if (d->getNthType(k) != Dict::DICT_ASCIISTRING || d->getNthKey(k) == TheKey_teamOwner) continue;
				const AsciiString value = d->getNthAsciiString(k);
				if (value.isEmpty()) continue;
				if (!query.value.empty() && _stricmp(value.str(), query.value.c_str()) != 0) continue;
				if (!mcpContainsNoCase(value.str(), query.text.c_str())) continue;
				totalTeamHits++;
				if ((Int)teams.size() >= limit) continue;
				teams.push(McpJson::makeObject()
					.set("team", d->getAsciiString(TheKey_teamName).str())
					.set("owner", owner.str())
					.set("key", TheNameKeyGenerator->keyToName(d->getNthKey(k)).str())
					.set("value", value.str()));
			}
		}
	}

	McpJson j = McpJson::makeObject();
	j.set("total_scripts", matchedScripts).set("total_hits", totalHits).set("scripts", found);
	if (plainText) {
		j.set("total_team_hits", totalTeamHits).set("teams", teams);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.add / update / delete / move
//-------------------------------------------------------------------------------------------------

McpJson cmdAddScript(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const McpJson &def = args.get("script");
	if (!def.isObject()) {
		mcpFail("argument 'script' must be an object");
	}
	const std::string name = mcpArgString(def, "name");
	const Int index = argIndex(args);

	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	Int side;
	ScriptGroup *group;
	resolveDestination(sides, args, "player", "group", &side, &group);
	if (!mcpArgBool(args, "allow_duplicate_name", false) && scriptNameExists(sides, side, name)) {
		mcpFail("this player already has a script named '%s'; scripts are referenced by name, so pick another name or use scripts.update",
			name.c_str());
	}

	// Same starting point as the editor's New Script, minus its placeholder action.
	Script *script = newInstance(Script);
	OrCondition *orCond = newInstance(OrCondition);
	orCond->setFirstAndCondition(newInstance(Condition)(Condition::CONDITION_TRUE));
	script->setOrCondition(orCond);
	if (group) {
		group->addScript(script, index);
	} else {
		requireScriptList(sides, side)->addScript(script, index);
	}
	applyScriptJson(script, def);

	McpJson j = locationJson(sides, side, group);
	j.set("name", script->getName().str());
	j.set("warnings", scriptWarnings(sides, side, script));
	commitSides(sides);
	return j;
}

McpJson cmdUpdateScript(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const McpJson &def = args.get("script");
	if (!def.isObject()) {
		mcpFail("argument 'script' must be an object with the keys to change");
	}

	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const ScriptLoc loc = requireScript(sides, args);
	if (def.has("name")) {
		const std::string newName = mcpArgString(def, "name");
		if (newName != loc.script->getName().str() && !mcpArgBool(args, "allow_duplicate_name", false)
			&& scriptNameExists(sides, loc.side, newName)) {
			mcpFail("this player already has a script named '%s'", newName.c_str());
		}
	}
	applyScriptJson(loc.script, def);

	McpJson j = locationJson(sides, loc.side, loc.group);
	j.set("name", loc.script->getName().str());
	j.set("warnings", scriptWarnings(sides, loc.side, loc.script));
	commitSides(sides);
	return j;
}

McpJson cmdDeleteScript(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const ScriptLoc loc = requireScript(sides, args);
	McpJson j = locationJson(sides, loc.side, loc.group);
	j.set("deleted", loc.script->getName().str());
	if (loc.group) {
		loc.group->deleteScript(loc.script);
	} else {
		requireScriptList(sides, loc.side)->deleteScript(loc.script);
	}
	commitSides(sides);
	return j;
}

McpJson cmdMoveScript(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const Int index = argIndex(args);
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const ScriptLoc loc = requireScript(sides, args);

	// Without a destination the script stays in its list and only changes position.
	Int side = loc.side;
	ScriptGroup *group = loc.group;
	if (args.has("to_group") || args.has("to_player")) {
		if (!args.has("to_player") && mcpArgString(args, "to_group", "").empty()) {
			group = nullptr;	// to_group "": out of its group, to the top level of the same player.
		} else {
			resolveDestination(sides, args, "to_player", "to_group", &side, &group);
		}
	}

	Script *moved = loc.script->duplicate();
	if (loc.group) {
		loc.group->deleteScript(loc.script);
	} else {
		requireScriptList(sides, loc.side)->deleteScript(loc.script);
	}
	if (group) {
		group->addScript(moved, index);
	} else {
		requireScriptList(sides, side)->addScript(moved, index);
	}

	McpJson j = locationJson(sides, side, group);
	j.set("name", moved->getName().str());
	commitSides(sides);
	return j;
}

//-------------------------------------------------------------------------------------------------
// Script groups
//-------------------------------------------------------------------------------------------------

McpJson cmdAddGroup(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	const std::string name = mcpArgString(args, "name");
	if (name.empty()) {
		mcpFail("the group name must not be empty");
	}
	const Int index = argIndex(args);
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const Int side = requireSide(sides, mcpArgString(args, "player"));
	ScriptList *list = requireScriptList(sides, side);
	for (ScriptGroup *group = list->getScriptGroup(); group; group = group->getNext()) {
		if (name == group->getName().str()) {
			mcpFail("player '%s' already has a script group named '%s'", sideName(sides, side).str(), name.c_str());
		}
	}

	ScriptGroup *group = newInstance(ScriptGroup);
	group->setName(AsciiString(name.c_str()));
	group->setActive(mcpArgBool(args, "active", true));
	group->setSubroutine(mcpArgBool(args, "subroutine", false));
	list->addGroup(group, index);
	McpJson j = locationJson(sides, side, group);
	commitSides(sides);
	return j;
}

McpJson cmdUpdateGroup(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	GroupLoc loc = requireGroup(sides, mcpArgString(args, "name"),
		args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1);
	const Int toSide = args.has("to_player") ? requireSide(sides, mcpArgString(args, "to_player")) : loc.side;

	if (args.has("new_name")) {
		const std::string newName = mcpArgString(args, "new_name");
		if (newName.empty()) mcpFail("the group name must not be empty");
		loc.group->setName(AsciiString(newName.c_str()));
	}
	if (args.has("active")) loc.group->setActive(mcpArgBool(args, "active", true));
	if (args.has("subroutine")) loc.group->setSubroutine(mcpArgBool(args, "subroutine", false));

	ScriptGroup *group = loc.group;
	if (args.has("index") || toSide != loc.side) {
		const Int index = argIndex(args);
		ScriptList *target = requireScriptList(sides, toSide);
		group = loc.group->duplicate();
		requireScriptList(sides, loc.side)->deleteGroup(loc.group);
		target->addGroup(group, index);
	}
	// Two groups of one player with the same name could not be told apart.
	Int sameName = 0;
	for (ScriptGroup *other = requireScriptList(sides, toSide)->getScriptGroup(); other; other = other->getNext()) {
		if (other->getName() == group->getName()) sameName++;
	}
	if (sameName > 1) {
		mcpFail("player '%s' already has a script group named '%s'", sideName(sides, toSide).str(), group->getName().str());
	}

	McpJson j = locationJson(sides, toSide, group);
	j.set("active", (bool)(group->isActive() != 0)).set("subroutine", (bool)(group->isSubroutine() != 0));
	commitSides(sides);
	return j;
}

McpJson cmdDeleteGroup(const McpJson &args)
{
	mcpDoc();
	requireScriptEditorClosed();
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const GroupLoc loc = requireGroup(sides, mcpArgString(args, "name"),
		args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1);
	Int numScripts = 0;
	for (Script *script = loc.group->getScript(); script; script = script->getNext()) {
		numScripts++;
	}
	if (numScripts > 0 && !mcpArgBool(args, "delete_scripts", false)) {
		mcpFail("group '%s' holds %d script(s); pass delete_scripts=true to delete them with it, or move them out first",
			loc.group->getName().str(), numScripts);
	}
	McpJson j = locationJson(sides, loc.side, loc.group);
	j.set("scripts_deleted", numScripts);
	requireScriptList(sides, loc.side)->deleteGroup(loc.group);
	commitSides(sides);
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.export
//-------------------------------------------------------------------------------------------------

class FileOutputStream : public OutputStream
{
public:
	FileOutputStream(FILE *file) : m_file(file) {}
	virtual Int write(const void *pData, Int numBytes) override
	{
		return (Int)fwrite(pData, 1, numBytes, m_file);
	}

private:
	FILE *m_file;
};

template <class T>
void collectTeamNames(T *item, std::set<std::string> &teams)
{
	for (Int i = 0; i < item->getNumParameters(); i++) {
		Parameter *parm = item->getParameter(i);
		if (parm->getParameterType() == Parameter::TEAM) {
			teams.insert(parm->getString().str());
		}
	}
}

void collectTeamNames(Script *script, std::set<std::string> &teams)
{
	for (OrCondition *orCond = script->getOrCondition(); orCond; orCond = orCond->getNextOrCondition()) {
		for (Condition *cond = orCond->getFirstAndCondition(); cond; cond = cond->getNext()) {
			collectTeamNames(cond, teams);
		}
	}
	for (ScriptAction *action = script->getAction(); action; action = action->getNext()) {
		collectTeamNames(action, teams);
	}
	for (ScriptAction *action = script->getFalseAction(); action; action = action->getNext()) {
		collectTeamNames(action, teams);
	}
}

/// Writes a script file the way the script editor's Export does. Without a selection it holds
/// all scripts with their players and teams; a selection (player, group or one script) is written
/// as one list that imports into a player of choice, with the teams its scripts refer to.
/// Waypoints, trigger areas and units go along on request, as in the editor's export options.
McpJson cmdExportScripts(const McpJson &args)
{
	mcpDoc();
	const std::string path = mcpArgString(args, "path");
	ScriptList::updateDefaults();
	SidesList &sides = *TheSidesList;
	const bool selection = args.has("name") || args.has("group") || args.has("player");
	const bool includePlayers = mcpArgBool(args, "include_players", !selection);

	ScriptList *lists[MAX_PLAYER_COUNT];
	for (Int i = 0; i < MAX_PLAYER_COUNT; i++) {
		lists[i] = nullptr;
	}
	Int numLists = 1;
	ScriptList *ownedList = nullptr;	// Holds copies when only a group or a script is exported.
	if (!selection) {
		numLists = sides.getNumSides();
		for (Int i = 0; i < numLists; i++) {
			lists[i] = sides.getSideInfo(i)->getScriptList();
		}
	} else if (args.has("name")) {
		const ScriptLoc loc = requireScript(sides, args);
		ownedList = newInstance(ScriptList);
		ownedList->addScript(loc.script->duplicate(), 0);
		lists[0] = ownedList;
	} else if (!mcpArgString(args, "group", "").empty()) {
		const GroupLoc loc = requireGroup(sides, mcpArgString(args, "group"),
			args.has("player") ? requireSide(sides, mcpArgString(args, "player")) : -1);
		ownedList = newInstance(ScriptList);
		ownedList->addGroup(loc.group->duplicate(), 0);
		lists[0] = ownedList;
	} else {
		lists[0] = requireScriptList(sides, requireSide(sides, mcpArgString(args, "player")));
	}

	Int numScripts = 0;
	std::set<std::string> referencedTeams;
	for (Int i = 0; i < numLists; i++) {
		if (lists[i] == nullptr) continue;
		for (Script *script = lists[i]->getScript(); script; script = script->getNext()) {
			collectTeamNames(script, referencedTeams);
			numScripts++;
		}
		for (ScriptGroup *group = lists[i]->getScriptGroup(); group; group = group->getNext()) {
			for (Script *script = group->getScript(); script; script = script->getNext()) {
				collectTeamNames(script, referencedTeams);
				numScripts++;
			}
		}
	}

	// A script file is the whole AI of a mod, so it is not replaced by accident or without a copy.
	std::string backupPath;
	if (::GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
		if (!mcpArgBool(args, "overwrite", false)) {
			deleteInstance(ownedList);
			mcpFail("'%s' already exists; pass overwrite=true to replace it (a backup copy is kept)", path.c_str());
		}
		if (mcpArgBool(args, "backup", true)) {
			SYSTEMTIME now;
			::GetLocalTime(&now);
			char stamp[40];
			sprintf(stamp, ".bak-%04d%02d%02d-%02d%02d%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
			backupPath = path + stamp;
			if (!::CopyFile(path.c_str(), backupPath.c_str(), FALSE)) {
				deleteInstance(ownedList);
				mcpFail("could not back up '%s' to '%s'", path.c_str(), backupPath.c_str());
			}
		}
	}

	// What of the map the exported scripts need. A full export takes all waypoints and areas, a
	// selection those its scripts name; units are the named ones and those of the named teams.
	const bool includeWaypoints = mcpArgBool(args, "include_waypoints", false);
	const bool includeAreas = mcpArgBool(args, "include_areas", false);
	const bool includeUnits = mcpArgBool(args, "include_units", false);
	std::set<std::string> namedWaypoints, namedPaths, namedAreas, namedUnits;
	for (Int i = 0; i < numLists; i++) {
		if (lists[i] == nullptr) continue;
		for (Int pass = 0; pass < 2; pass++) {
			ScriptGroup *group = pass ? lists[i]->getScriptGroup() : nullptr;
			do {
				for (Script *script = group ? group->getScript() : (pass ? nullptr : lists[i]->getScript()); script; script = script->getNext()) {
					std::vector<ParamUse> uses;
					collectParamUses(script, uses);
					for (size_t k = 0; k < uses.size(); k++) {
						const std::string value = uses[k].parm->getString().str();
						switch (uses[k].parm->getParameterType()) {
							case Parameter::WAYPOINT: namedWaypoints.insert(value); break;
							case Parameter::WAYPOINT_PATH: namedPaths.insert(value); break;
							case Parameter::TRIGGER_AREA: namedAreas.insert(value); break;
							case Parameter::UNIT: namedUnits.insert(value); break;
							default: break;
						}
					}
				}
				group = group ? group->getNext() : nullptr;
			} while (group);
		}
	}
	std::vector<MapObject *> exportObjects;
	std::set<Int> exportWaypointIds;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		Dict *props = obj->getProperties();
		Bool exists = false;
		bool take = false;
		if (obj->isWaypoint()) {
			if (includeWaypoints) {
				take = !selection || namedWaypoints.count(obj->getWaypointName().str())
					|| namedPaths.count(props->getAsciiString(TheKey_waypointPathLabel1, &exists).str())
					|| namedPaths.count(props->getAsciiString(TheKey_waypointPathLabel2, &exists).str())
					|| namedPaths.count(props->getAsciiString(TheKey_waypointPathLabel3, &exists).str());
			}
			if (take) exportWaypointIds.insert(obj->getWaypointID());
		} else if (includeUnits && !(obj->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS))) {
			const AsciiString unitName = props->getAsciiString(TheKey_objectName, &exists);
			const AsciiString ownerTeam = props->getAsciiString(TheKey_originalOwner, &exists);
			if (!unitName.isEmpty() && namedUnits.count(unitName.str())) {
				take = true;
				referencedTeams.insert(ownerTeam.str());
			} else if (ownerTeam != "team" && referencedTeams.count(ownerTeam.str())) {
				take = true;
			}
		}
		if (take) exportObjects.push_back(obj);
	}
	std::vector<PolygonTrigger *> exportAreas;
	for (PolygonTrigger *trigger = PolygonTrigger::getFirstPolygonTrigger(); includeAreas && trigger; trigger = trigger->getNext()) {
		if (trigger->isWaterArea()) continue;
		if (!selection || namedAreas.count(trigger->getTriggerName().str())) exportAreas.push_back(trigger);
	}
	std::vector<std::pair<Int, Int> > exportLinks;
	CWorldBuilderDoc *linkDoc = mcpDoc();
	for (Int i = 0; i < linkDoc->getNumWaypointLinks(); i++) {
		Int from, to;
		linkDoc->getWaypointLink(i, &from, &to);
		if (exportWaypointIds.count(from) && exportWaypointIds.count(to)) exportLinks.push_back(std::make_pair(from, to));
	}

	FILE *fp = fopen(path.c_str(), "wb");
	if (fp == nullptr) {
		deleteInstance(ownedList);
		mcpFail("cannot write '%s'", path.c_str());
	}
	Int numTeams = 0;
	{
		FileOutputStream stream(fp);
		DataChunkOutput chunkWriter(&stream);
		ScriptList::WriteScriptsDataChunk(chunkWriter, lists, numLists);

		chunkWriter.openDataChunk("ScriptsPlayers", K_SCRIPT_FILE_PLAYERS_VERSION_2);
			chunkWriter.writeInt(includePlayers);
			if (!selection || includePlayers) {
				chunkWriter.writeInt(sides.getNumSides());
				for (Int i = 0; i < sides.getNumSides(); i++) {
					chunkWriter.writeAsciiString(sideName(sides, i));
					if (includePlayers) {
						chunkWriter.writeDict(*sides.getSideInfo(i)->getDict());
					}
				}
			} else {
				chunkWriter.writeInt(1);
				chunkWriter.writeAsciiString("**SELECTION**");
			}
		chunkWriter.closeDataChunk();

		chunkWriter.openDataChunk("ObjectsList", K_OBJECTS_VERSION_3);
			for (size_t i = 0; i < exportObjects.size(); i++) {
				MapObject *obj = exportObjects[i];
				chunkWriter.openDataChunk("Object", K_OBJECTS_VERSION_3);
					chunkWriter.writeReal(obj->getLocation()->x);
					chunkWriter.writeReal(obj->getLocation()->y);
					chunkWriter.writeReal(obj->getLocation()->z);
					chunkWriter.writeReal(obj->getAngle());
					chunkWriter.writeInt(obj->getFlags());
					chunkWriter.writeAsciiString(obj->getName());
					chunkWriter.writeDict(*obj->getProperties());
				chunkWriter.closeDataChunk();
			}
		chunkWriter.closeDataChunk();

		chunkWriter.openDataChunk("PolygonTriggers", K_TRIGGERS_VERSION_3);
			chunkWriter.writeInt((Int)exportAreas.size());
			for (size_t i = 0; i < exportAreas.size(); i++) {
				PolygonTrigger *trigger = exportAreas[i];
				chunkWriter.writeAsciiString(trigger->getTriggerName());
				chunkWriter.writeInt(trigger->getID());
				chunkWriter.writeByte(trigger->isWaterArea());
				chunkWriter.writeByte(trigger->isRiver());
				chunkWriter.writeInt(trigger->getRiverStart());
				chunkWriter.writeInt(trigger->getNumPoints());
				for (Int k = 0; k < trigger->getNumPoints(); k++) {
					chunkWriter.writeInt(trigger->getPoint(k)->x);
					chunkWriter.writeInt(trigger->getPoint(k)->y);
					chunkWriter.writeInt(trigger->getPoint(k)->z);
				}
			}
		chunkWriter.closeDataChunk();

		chunkWriter.openDataChunk("ScriptTeams", K_SCRIPT_FILE_TEAMS_VERSION_1);
			for (Int i = 0; i < sides.getNumTeams(); i++) {
				Dict *d = sides.getTeamInfo(i)->getDict();
				if (selection && !referencedTeams.count(d->getAsciiString(TheKey_teamName).str())) continue;
				chunkWriter.writeDict(*d);
				numTeams++;
			}
		chunkWriter.closeDataChunk();

		chunkWriter.openDataChunk("WaypointsList", K_WAYPOINTS_VERSION_1);
			chunkWriter.writeInt((Int)exportLinks.size());
			for (size_t i = 0; i < exportLinks.size(); i++) {
				chunkWriter.writeInt(exportLinks[i].first);
				chunkWriter.writeInt(exportLinks[i].second);
			}
		chunkWriter.closeDataChunk();
	}
	fclose(fp);
	deleteInstance(ownedList);

	McpJson j = McpJson::makeObject();
	j.set("path", path).set("scripts", numScripts).set("teams", numTeams);
	j.set("players", (!selection || includePlayers) ? sides.getNumSides() : 0);
	if (includeWaypoints || includeAreas || includeUnits) {
		j.set("waypoints", (int)exportWaypointIds.size()).set("waypoint_links", (int)exportLinks.size());
		j.set("units", (int)(exportObjects.size() - exportWaypointIds.size())).set("areas", (int)exportAreas.size());
	}
	if (!backupPath.empty()) {
		j.set("backup", backupPath);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// scripts.import
//-------------------------------------------------------------------------------------------------

struct ImportState
{
	SidesList *sides;
	AsciiString playerNames[MAX_PLAYER_COUNT];
	Int numNames;
	Int playersAdded;
	Int teamsAdded;
	McpJson notes;

	// Map data of the file: objects (units and waypoints), trigger areas and waypoint links.
	Bool wantMapData;
	MapObject *firstObject, *lastObject;
	Int numWaypoints, numUnits;
	std::vector<PolygonTrigger *> triggers;
	std::vector<std::pair<Int, Int> > links;
	Int waypointBase, maxWaypoint;
	std::map<Int, Int> existingWaypoint;	///< File waypoint id -> id of the map waypoint it duplicates.
};

void addNote(ImportState *state, const char *fmt, const char *a, const char *b = "")
{
	AsciiString note;
	note.format(fmt, a, b);
	state->notes.push(McpJson(note.str()));
}

/// Player names of the script lists in the file, in order, and optionally their dicts. Like the
/// editor, players the map does not have yet are added; existing ones are left alone.
Bool parseImportPlayers(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ImportState *state = (ImportState *)userData;
	Int readDicts = 0;
	if (info->version >= K_SCRIPT_FILE_PLAYERS_VERSION_2) {
		readDicts = file.readInt();
	}
	const Int numNames = file.readInt();
	for (Int i = 0; i < numNames && i < MAX_PLAYER_COUNT; i++) {
		state->playerNames[i] = file.readAsciiString();
		state->numNames = i + 1;
		if (!readDicts) continue;
		Dict sideDict = file.readDict();
		if (findSide(*state->sides, state->playerNames[i]) >= 0) continue;
		if (state->sides->getNumSides() >= MAX_PLAYER_COUNT) {
			addNote(state, "player '%s' not added: the map has the maximum number of players", state->playerNames[i].str());
			continue;
		}
		state->sides->addSide(&sideDict);
		SidesInfo *side = state->sides->findSideInfo(state->playerNames[i]);
		if (side != nullptr) {
			side->setScriptList(newInstance(ScriptList));
			state->playersAdded++;
		}
	}
	return true;
}

/// Teams the map does not have yet are added. The editor asks for a new owner when the owning
/// player is missing; here such a team goes to the neutral player.
Bool parseImportTeams(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ImportState *state = (ImportState *)userData;
	while (!file.atEndOfChunk()) {
		Dict teamDict = file.readDict();
		const AsciiString teamName = teamDict.getAsciiString(TheKey_teamName);
		if (state->sides->findTeamInfo(teamName)) continue;
		const AsciiString owner = teamDict.getAsciiString(TheKey_teamOwner);
		if (findSide(*state->sides, owner) < 0) {
			addNote(state, "team '%s': owner '%s' does not exist, given to the neutral player", teamName.str(), owner.str());
			teamDict.setAsciiString(TheKey_teamOwner, AsciiString::TheEmptyString);
		}
		state->sides->addTeam(&teamDict);
		state->teamsAdded++;
	}
	return true;
}

/// One unit or waypoint of the file. Like the editor, an object the map already has at the same
/// spot is skipped; unlike it, a waypoint whose name the map already uses is skipped too and
/// the file's links to it go to the map's waypoint.
Bool parseImportObject(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ImportState *state = (ImportState *)userData;
	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();
	const Real angle = file.readReal();
	const Int flags = file.readInt();
	const AsciiString name = file.readAsciiString();
	Dict d = file.readDict();

	MapObject *obj = newInstance(MapObject)(loc, name, angle, flags, &d, TheThingFactory->findTemplate(name, FALSE));
	Int fileWaypointId = 0;
	if (obj->getProperties()->getType(TheKey_waypointID) == Dict::DICT_INT) {
		obj->setIsWaypoint();
		fileWaypointId = obj->getWaypointID();
		obj->setWaypointID(fileWaypointId + state->waypointBase);
		if (state->maxWaypoint < obj->getWaypointID()) state->maxWaypoint = obj->getWaypointID();
	}
	for (MapObject *other = MapObject::getFirstMapObject(); other; other = other->getNext()) {
		const Coord3D *otherLoc = other->getLocation();
		const Bool sameSpot = loc.x == otherLoc->x && loc.y == otherLoc->y && other->isWaypoint() == obj->isWaypoint()
			&& other->getName() == obj->getName();
		const Bool sameWaypointName = obj->isWaypoint() && other->isWaypoint() && obj->getWaypointName() == other->getWaypointName();
		if (!sameSpot && !sameWaypointName) continue;
		if (sameWaypointName) {
			state->existingWaypoint[fileWaypointId] = other->getWaypointID();
			if (!sameSpot) {
				addNote(state, "waypoint '%s' already exists; the map's one is kept", obj->getWaypointName().str());
			}
		}
		deleteInstance(obj);
		return true;
	}
	if (obj->isWaypoint()) state->numWaypoints++; else state->numUnits++;
	if (state->lastObject) state->lastObject->setNextMap(obj); else state->firstObject = obj;
	state->lastObject = obj;
	return true;
}

Bool parseImportObjects(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	file.registerParser("Object", info->label, parseImportObject);
	return file.parse(userData);
}

Bool parseImportTriggers(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ImportState *state = (ImportState *)userData;
	Int count = file.readInt();
	while (count-- > 0) {
		const AsciiString triggerName = file.readAsciiString();
		file.readInt();	// The id; the map gives the area its own.
		Bool isWater = false, isRiver = false;
		Int riverStart = 0;
		if (info->version >= K_TRIGGERS_VERSION_2) {
			isWater = file.readByte();
		}
		if (info->version >= K_TRIGGERS_VERSION_3) {
			isRiver = file.readByte();
			riverStart = file.readInt();
		}
		const Int numPoints = file.readInt();
		PolygonTrigger *trigger = newInstance(PolygonTrigger)(numPoints + 1);
		trigger->setTriggerName(triggerName);
		trigger->setWaterArea(isWater);
		trigger->setRiver(isRiver);
		trigger->setRiverStart(riverStart);
		for (Int i = 0; i < numPoints; i++) {
			ICoord3D loc;
			loc.x = file.readInt();
			loc.y = file.readInt();
			loc.z = file.readInt();
			trigger->addPoint(loc);
		}
		Bool duplicate = false;
		for (PolygonTrigger *other = PolygonTrigger::getFirstPolygonTrigger(); other && !duplicate; other = other->getNext()) {
			duplicate = triggerName == other->getTriggerName();
		}
		for (size_t i = 0; i < state->triggers.size() && !duplicate; i++) {
			duplicate = triggerName == state->triggers[i]->getTriggerName();
		}
		if (duplicate) {
			addNote(state, "area '%s' already exists; the map's one is kept", triggerName.str());
			deleteInstance(trigger);
		} else {
			state->triggers.push_back(trigger);
		}
	}
	return true;
}

Bool parseImportLinks(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ImportState *state = (ImportState *)userData;
	const Int count = file.readInt();
	for (Int i = 0; i < count; i++) {
		const Int a = file.readInt();
		const Int b = file.readInt();
		state->links.push_back(std::make_pair(a, b));
	}
	return true;
}

/// Frees what the map data parsers created, when the import does not go through.
void discardImportedMapData(ImportState &state)
{
	deleteInstance(state.firstObject);
	state.firstObject = state.lastObject = nullptr;
	for (size_t i = 0; i < state.triggers.size(); i++) {
		deleteInstance(state.triggers[i]);
	}
	state.triggers.clear();
}

/// Reads a script file like the script editor's Import: scripts go to the player of the same
/// name (a single list goes to args.player), missing players and teams are added. With
/// map_data, the waypoints, trigger areas and units of the file are added too.
McpJson cmdImportScripts(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	requireScriptEditorClosed();
	const std::string path = mcpArgString(args, "path");
	ScriptList::updateDefaults();
	SidesList sides = *TheSidesList;
	const bool hasTarget = args.has("player");
	const AsciiString targetName = hasTarget ? sideName(sides, requireSide(sides, mcpArgString(args, "player"))) : AsciiString::TheEmptyString;

	ImportState state;
	state.sides = &sides;
	state.numNames = 0;
	state.playersAdded = 0;
	state.teamsAdded = 0;
	state.notes = McpJson::makeArray();
	state.wantMapData = mcpArgBool(args, "map_data", false);
	state.firstObject = state.lastObject = nullptr;
	state.numWaypoints = state.numUnits = 0;
	state.waypointBase = doc->getNextWaypointID();
	state.maxWaypoint = state.waypointBase;
	if (state.wantMapData && s_sidesBatch) {
		mcpFail("scripts.import with map_data cannot run in a batch (waypoints, areas and units are not part of the sides list)");
	}

	CachedFileInputStream stream;
	if (!stream.open(AsciiString(path.c_str()))) {
		mcpFail("cannot open '%s'", path.c_str());
	}
	Bool parsed = false;
	try {
		DataChunkInput file(&stream);
		file.registerParser("PlayerScriptsList", AsciiString::TheEmptyString, ScriptList::ParseScriptsDataChunk);
		file.registerParser("ScriptsPlayers", AsciiString::TheEmptyString, parseImportPlayers);
		file.registerParser("ScriptTeams", AsciiString::TheEmptyString, parseImportTeams);
		if (state.wantMapData) {
			file.registerParser("ObjectsList", AsciiString::TheEmptyString, parseImportObjects);
			file.registerParser("PolygonTriggers", AsciiString::TheEmptyString, parseImportTriggers);
			file.registerParser("WaypointsList", AsciiString::TheEmptyString, parseImportLinks);
		}
		parsed = file.parse(&state);
	} catch (...) {
		parsed = false;
	}

	// The read lists are ours now, whatever happened.
	ScriptList *read[MAX_PLAYER_COUNT];
	const Int count = ScriptList::getReadScripts(read);

	// Decide where every list goes before moving anything.
	Int dest[MAX_PLAYER_COUNT];
	std::string failure;
	if (!parsed) {
		failure = "'" + path + "' is not a readable script file (.scb)";
	}
	for (Int i = 0; i < count && failure.empty(); i++) {
		dest[i] = -1;
		if (read[i]->getScript() == nullptr && read[i]->getScriptGroup() == nullptr) continue;
		if (count == 1 && hasTarget) {
			dest[i] = findSide(sides, targetName);
		} else if (i < state.numNames) {
			dest[i] = findSide(sides, state.playerNames[i]);
		}
		if (dest[i] >= 0 && sides.getSideInfo(dest[i])->getScriptList() == nullptr) {
			sides.getSideInfo(dest[i])->setScriptList(newInstance(ScriptList));
		}
		if (dest[i] < 0) {
			if (count == 1) {
				failure = "the file holds a single script list without a player of this map; pass player to say where it goes";
			} else {
				addNote(&state, "scripts of player '%s' skipped: the map has no such player",
					i < state.numNames ? state.playerNames[i].str() : "?");
			}
		}
	}
	if (!failure.empty()) {
		for (Int i = 0; i < count; i++) {
			deleteInstance(read[i]);
		}
		discardImportedMapData(state);
		mcpFail("%s", failure.c_str());
	}

	std::set<std::pair<Int, std::string> > existingNames;
	{
		const std::vector<ScriptLoc> all = allScripts(sides);
		for (size_t i = 0; i < all.size(); i++) {
			existingNames.insert(std::make_pair(all[i].side, std::string(all[i].script->getName().str())));
		}
	}
	McpJson duplicates = McpJson::makeArray();
	Int numScripts = 0, numGroups = 0;
	for (Int i = 0; i < count; i++) {
		if (dest[i] < 0) {
			deleteInstance(read[i]);
			continue;
		}
		ScriptList *list = sides.getSideInfo(dest[i])->getScriptList();
		// Unlink each script and group and put them in front of the existing ones, in file order.
		Int position = 0;
		Script *nextScript;
		for (Script *script = read[i]->getScript(); script; script = nextScript) {
			nextScript = script->getNext();
			script->setNextScript(nullptr);
			if (existingNames.count(std::make_pair(dest[i], std::string(script->getName().str())))) duplicates.push(McpJson(script->getName().str()));
			list->addScript(script, position++);
			numScripts++;
		}
		position = 0;
		ScriptGroup *nextGroup;
		for (ScriptGroup *group = read[i]->getScriptGroup(); group; group = nextGroup) {
			nextGroup = group->getNext();
			group->setNextGroup(nullptr);
			for (Script *script = group->getScript(); script; script = script->getNext()) {
				if (existingNames.count(std::make_pair(dest[i], std::string(script->getName().str())))) duplicates.push(McpJson(script->getName().str()));
				numScripts++;
			}
			list->addGroup(group, position++);
			numGroups++;
		}
		read[i]->discard();	// Frees the list but not the scripts and groups that moved.
	}

	McpJson j = McpJson::makeObject();
	j.set("scripts_imported", numScripts).set("groups_imported", numGroups);
	j.set("players_added", state.playersAdded).set("teams_added", state.teamsAdded);

	// The file's links in map waypoint ids, without those the map already has.
	std::set<std::pair<Int, Int> > mapLinks;
	for (Int i = 0; i < doc->getNumWaypointLinks(); i++) {
		Int from, to;
		doc->getWaypointLink(i, &from, &to);
		mapLinks.insert(std::make_pair(from, to));
	}
	std::vector<std::pair<Int, Int> > newLinks;
	for (size_t i = 0; i < state.links.size(); i++) {
		Int ends[2] = { state.links[i].first, state.links[i].second };
		for (Int e = 0; e < 2; e++) {
			std::map<Int, Int>::const_iterator existing = state.existingWaypoint.find(ends[e]);
			ends[e] = existing != state.existingWaypoint.end() ? existing->second : ends[e] + state.waypointBase;
		}
		if (mapLinks.insert(std::make_pair(ends[0], ends[1])).second) {
			newLinks.push_back(std::make_pair(ends[0], ends[1]));
		}
	}

	const bool hasMapData = state.firstObject != nullptr || !state.triggers.empty() || !newLinks.empty();
	if (!hasMapData) {
		commitSides(sides);
		if (state.wantMapData) {
			j.set("waypoints_added", 0).set("units_added", 0).set("areas_added", 0).set("waypoint_links_added", 0);
		}
	} else {
		// Scripts and map data become one undo step.
		McpWaypointLinksUndoable *links = new McpWaypointLinksUndoable(doc);
		links->added = newLinks;
		MultipleUndoable *undo = new MultipleUndoable;
		undo->addUndoable(links);
		for (size_t i = 0; i < state.triggers.size(); i++) {
			undo->addUndoable(new AddPolygonUndoable(state.triggers[i]));
		}
		if (state.firstObject) {
			undo->addUndoable(new AddObjectUndoable(doc, state.firstObject));
		}
		undo->addUndoable(new SidesListUndoable(sides, doc));
		doc->setNextWaypointID(state.maxWaypoint + 1);
		mcpCommit(undo);
		mcpResetObjectHandles();
		j.set("waypoints_added", state.numWaypoints).set("units_added", state.numUnits);
		j.set("areas_added", (int)state.triggers.size()).set("waypoint_links_added", (int)newLinks.size());
	}
	j.set("duplicate_names", duplicates).set("notes", state.notes);
	return j;
}

} // namespace McpScripts

using namespace McpScripts;

void mcpRegisterScriptCommands()
{
	mcpRegisterCommand("scripts.list", cmdListScripts, "{player?,group?,filter?,detail?=auto|groups|names|full} Scripts and script groups per player; auto reduces the detail on large sets.");
	mcpRegisterCommand("scripts.get", cmdGetScript, "{name,player?,group?,script_index?} One script: flags, conditions (OR list of AND lists), actions, false_actions, and the editor's wording.");
	mcpRegisterCommand("scripts.find", cmdFindScripts, "{text?,value?,type?,param_type?,player?,limit?=50,offset?,max_hits_per_script?=10} Finds conditions and actions by wording, exact parameter value, type or parameter type; also script names, comments and team properties.");
	mcpRegisterCommand("scripts.add", cmdAddScript, "{script:{name,...},player?|group?,index?,allow_duplicate_name?} Adds a script to a player's top level or to a group.");
	mcpRegisterCommand("scripts.update", cmdUpdateScript, "{name,player?,group?,script_index?,script:{...}} Changes the given keys of a script; conditions/actions lists are replaced whole.");
	mcpRegisterCommand("scripts.delete", cmdDeleteScript, "{name,player?,group?,script_index?} Deletes a script.");
	mcpRegisterCommand("scripts.move", cmdMoveScript, "{name,player?,group?,script_index?,to_player?,to_group?,index?} Moves a script to another position, group or player.");
	mcpRegisterCommand("scripts.add_group", cmdAddGroup, "{player,name,active?=true,subroutine?=false,index?} Adds a script group (folder).");
	mcpRegisterCommand("scripts.update_group", cmdUpdateGroup, "{name,player?,new_name?,active?,subroutine?,index?,to_player?} Renames, flags or moves a script group.");
	mcpRegisterCommand("scripts.delete_group", cmdDeleteGroup, "{name,player?,delete_scripts?=false} Deletes a script group.");
	mcpRegisterCommand("scripts.list_actions", cmdListActions, "{filter?,limit?=50,offset?} Script action types with their parameter types.");
	mcpRegisterCommand("scripts.list_conditions", cmdListConditions, "{filter?,limit?=50,offset?} Script condition types with their parameter types.");
	mcpRegisterCommand("scripts.verify", cmdVerifyScripts, "{player?,name?,summary?=false} Runs the script editor's checks: references to missing scripts, teams, waypoints, objects etc.");
	mcpRegisterCommand("scripts.export", cmdExportScripts, "{path,player?,group?,name?,include_players?,include_waypoints?,include_areas?,include_units?,overwrite?=false,backup?=true} Writes a script file (.scb) with all scripts or a selection.");
	mcpRegisterCommand("scripts.import", cmdImportScripts, "{path,player?,map_data?=false} Reads a script file (.scb): scripts, plus players and teams the map lacks; with map_data also its waypoints, areas and units.");
}
