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

// McpCommands.cpp
// Command table, request dispatch and shared helpers for the WorldBuilder MCP command bridge.

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "CUndoable.h"
#include "MainFrm.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "Common/Dict.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"

#include <map>
#include <math.h>
#include <stdarg.h>

namespace
{

struct CommandEntry
{
	McpHandler handler;
	std::string help;
};

typedef std::map<std::string, CommandEntry> CommandTable;

CommandTable &commandTable()
{
	static CommandTable table;
	return table;
}

std::map<MapObject *, int> s_objectToHandle;
std::map<int, MapObject *> s_handleToObject;
int s_nextHandle = 1;

McpJson cmdListCommands(const McpJson &)
{
	McpJson list = McpJson::makeArray();
	const CommandTable &table = commandTable();
	for (CommandTable::const_iterator it = table.begin(); it != table.end(); ++it) {
		list.push(McpJson::makeObject().set("name", it->first).set("help", it->second.help));
	}
	return McpJson::makeObject().set("commands", list);
}

void registerAllCommands()
{
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;
	mcpRegisterCommand("list_commands", cmdListCommands, "Lists all bridge commands.");
	mcpRegisterMapCommands();
	mcpRegisterTerrainCommands();
	mcpRegisterObjectCommands();
	mcpRegisterWaypointCommands();
	mcpRegisterImageCommands();
	mcpRegisterSymmetryCommands();
	mcpRegisterGenerateCommands();
	mcpRegisterSkirmishCommands();
	mcpRegisterScatterCommands();
	mcpRegisterRouteCommands();
	mcpRegisterReplaceCommands();
	mcpRegisterResizeCommands();
}

std::string makeErrorReply(const McpJson &id, const std::string &message)
{
	McpJson reply = McpJson::makeObject();
	reply.set("id", id).set("ok", false).set("error", message);
	return reply.dump();
}

/// Returns a reason string when WorldBuilder cannot safely run a command right now.
const char *getBusyReason()
{
	CWnd *mainWnd = AfxGetMainWnd();
	if (mainWnd == nullptr || mainWnd->GetSafeHwnd() == nullptr) {
		return "WorldBuilder main window is not available";
	}
	if (!mainWnd->IsWindowEnabled()) {
		return "WorldBuilder is busy (a modal dialog is open). Close it and retry.";
	}
	if (::GetCapture() != nullptr) {
		return "WorldBuilder is busy (a mouse drag is in progress). Retry shortly.";
	}
	return nullptr;
}

} // namespace

void mcpRegisterCommand(const char *name, McpHandler handler, const char *help)
{
	CommandEntry entry;
	entry.handler = handler;
	entry.help = help ? help : "";
	commandTable()[name] = entry;
}

std::string mcpDispatch(const std::string &requestLine)
{
	registerAllCommands();

	McpJson request;
	std::string parseError;
	if (!McpJson::parse(requestLine, request, parseError)) {
		return makeErrorReply(McpJson(), "invalid JSON: " + parseError);
	}
	const McpJson &id = request.get("id");
	const McpJson &cmd = request.get("cmd");
	if (!cmd.isString()) {
		return makeErrorReply(id, "request needs a string 'cmd'");
	}

	CommandTable::const_iterator it = commandTable().find(cmd.asString());
	if (it == commandTable().end()) {
		return makeErrorReply(id, "unknown command '" + cmd.asString() + "' (use list_commands)");
	}

	const char *busy = getBusyReason();
	if (busy != nullptr) {
		return makeErrorReply(id, busy);
	}

	McpJson args = request.get("args");
	if (args.isNull()) {
		args = McpJson::makeObject();
	}
	if (!args.isObject()) {
		return makeErrorReply(id, "'args' must be an object");
	}

	try {
		McpJson result = it->second.handler(args);
		McpJson reply = McpJson::makeObject();
		reply.set("id", id).set("ok", true).set("result", result);
		return reply.dump();
	} catch (const McpError &err) {
		return makeErrorReply(id, err.message);
	}
}

//-------------------------------------------------------------------------------------------------
// Argument helpers
//-------------------------------------------------------------------------------------------------

void mcpFail(const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;
	McpError err;
	err.message = buf;
	throw err;
}

double mcpArgNumber(const McpJson &args, const char *key)
{
	const McpJson &v = args.get(key);
	if (!v.isNumber()) {
		mcpFail("argument '%s' must be a number", key);
	}
	return v.asNumber();
}

double mcpArgNumber(const McpJson &args, const char *key, double defaultValue)
{
	if (!args.has(key)) {
		return defaultValue;
	}
	return mcpArgNumber(args, key);
}

int mcpArgInt(const McpJson &args, const char *key)
{
	return (int)floor(mcpArgNumber(args, key) + 0.5);
}

int mcpArgInt(const McpJson &args, const char *key, int defaultValue)
{
	if (!args.has(key)) {
		return defaultValue;
	}
	return mcpArgInt(args, key);
}

bool mcpArgBool(const McpJson &args, const char *key, bool defaultValue)
{
	if (!args.has(key)) {
		return defaultValue;
	}
	const McpJson &v = args.get(key);
	if (!v.isBool()) {
		mcpFail("argument '%s' must be true or false", key);
	}
	return v.asBool();
}

std::string mcpArgString(const McpJson &args, const char *key)
{
	const McpJson &v = args.get(key);
	if (!v.isString()) {
		mcpFail("argument '%s' must be a string", key);
	}
	return v.asString();
}

std::string mcpArgString(const McpJson &args, const char *key, const char *defaultValue)
{
	if (!args.has(key)) {
		return defaultValue;
	}
	return mcpArgString(args, key);
}

const McpJson &mcpArgArray(const McpJson &args, const char *key)
{
	const McpJson &v = args.get(key);
	if (!v.isArray()) {
		mcpFail("argument '%s' must be an array", key);
	}
	return v;
}

//-------------------------------------------------------------------------------------------------
// Document helpers
//-------------------------------------------------------------------------------------------------

CWorldBuilderDoc *mcpDoc()
{
	CWorldBuilderDoc *doc = CWorldBuilderDoc::GetActiveDoc();
	if (doc == nullptr) {
		mcpFail("no map is open");
	}
	return doc;
}

WorldHeightMapEdit *mcpHeightMap()
{
	WorldHeightMapEdit *map = mcpDoc()->GetHeightMap();
	if (map == nullptr) {
		mcpFail("the open map has no heightmap");
	}
	return map;
}

void mcpCommit(Undoable *undo)
{
	mcpDoc()->AddAndDoUndoable(undo);
	REF_PTR_RELEASE(undo); // belongs to the doc now.
}

int mcpWorldToIndexX(double worldX)
{
	return (int)floor(worldX / MAP_XY_FACTOR + 0.5) + mcpHeightMap()->getBorderSize();
}

int mcpWorldToIndexY(double worldY)
{
	return (int)floor(worldY / MAP_XY_FACTOR + 0.5) + mcpHeightMap()->getBorderSize();
}

double mcpIndexToWorldX(int ndx)
{
	return (ndx - mcpHeightMap()->getBorderSize()) * MAP_XY_FACTOR;
}

double mcpIndexToWorldY(int ndx)
{
	return (ndx - mcpHeightMap()->getBorderSize()) * MAP_XY_FACTOR;
}

bool mcpContainsNoCase(const char *haystack, const char *needle)
{
	if (needle == nullptr || *needle == 0) {
		return true;
	}
	if (haystack == nullptr) {
		return false;
	}
	size_t needleLen = strlen(needle);
	for (const char *p = haystack; *p; p++) {
		if (_strnicmp(p, needle, needleLen) == 0) {
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
// Object handles
//-------------------------------------------------------------------------------------------------

int mcpObjectHandle(MapObject *obj)
{
	std::map<MapObject *, int>::iterator it = s_objectToHandle.find(obj);
	if (it != s_objectToHandle.end()) {
		return it->second;
	}
	int handle = s_nextHandle++;
	s_objectToHandle[obj] = handle;
	s_handleToObject[handle] = obj;
	return handle;
}

MapObject *mcpFindObject(int handle)
{
	std::map<int, MapObject *>::iterator it = s_handleToObject.find(handle);
	if (it == s_handleToObject.end()) {
		return nullptr;
	}
	// The pointer may be stale (deleted or undone), so only trust it if it is still in the map list.
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj == it->second) {
			return obj;
		}
	}
	return nullptr;
}

MapObject *mcpRequireObject(int handle)
{
	MapObject *obj = mcpFindObject(handle);
	if (obj == nullptr) {
		mcpFail("no object with id %d in the map (list objects again to get current ids)", handle);
	}
	return obj;
}

void mcpResetObjectHandles()
{
	s_objectToHandle.clear();
	s_handleToObject.clear();
}

PolygonTrigger *mcpRequirePolygon(int id)
{
	PolygonTrigger *trig = PolygonTrigger::getPolygonTriggerByID(id);
	if (trig == nullptr) {
		mcpFail("no polygon trigger with id %d", id);
	}
	return trig;
}

McpJson mcpDescribeObject(MapObject *obj)
{
	const Coord3D *loc = obj->getLocation();
	McpJson j = McpJson::makeObject();
	j.set("id", mcpObjectHandle(obj));
	j.set("template", obj->getName().str());
	j.set("x", loc->x).set("y", loc->y).set("z", loc->z);
	j.set("angle_deg", obj->getAngle() * 180.0 / PI);
	Dict *props = obj->getProperties();
	j.set("owner", props->getAsciiString(TheKey_originalOwner).str());
	Bool exists = false;
	AsciiString name = props->getAsciiString(TheKey_objectName, &exists);
	if (exists && !name.isEmpty()) {
		j.set("name", name.str());
	}
	AsciiString uniqueID = props->getAsciiString(TheKey_uniqueID, &exists);
	if (exists && !uniqueID.isEmpty()) {
		j.set("unique_id", uniqueID.str());
	}
	if (obj->isWaypoint()) {
		j.set("kind", "waypoint");
		j.set("waypoint_id", (int)obj->getWaypointID());
		j.set("waypoint_name", obj->getWaypointName().str());
	} else if (obj->getFlags() & FLAG_ROAD_FLAGS) {
		j.set("kind", "road_point");
	} else if (obj->getFlags() & FLAG_BRIDGE_FLAGS) {
		j.set("kind", "bridge_point");
	} else if (obj->isLight()) {
		j.set("kind", "light");
	} else if (obj->isScorch()) {
		j.set("kind", "scorch");
	} else {
		j.set("kind", obj->getThingTemplate() ? "object" : "unknown_template");
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// Dict conversion
//-------------------------------------------------------------------------------------------------

McpJson mcpDictToJson(const Dict &dict)
{
	McpJson out = McpJson::makeObject();
	for (Int i = 0; i < dict.getPairCount(); i++) {
		AsciiString key = TheNameKeyGenerator->keyToName(dict.getNthKey(i));
		switch (dict.getNthType(i)) {
			case Dict::DICT_BOOL:
				out.set(key.str(), (bool)(dict.getNthBool(i) != 0));
				break;
			case Dict::DICT_INT:
				out.set(key.str(), (int)dict.getNthInt(i));
				break;
			case Dict::DICT_REAL:
				out.set(key.str(), (double)dict.getNthReal(i));
				break;
			case Dict::DICT_ASCIISTRING:
				out.set(key.str(), dict.getNthAsciiString(i).str());
				break;
			case Dict::DICT_UNICODESTRING: {
				AsciiString s;
				s.translate(dict.getNthUnicodeString(i));
				out.set(key.str(), s.str());
				break;
			}
			default:
				break;
		}
	}
	return out;
}

void mcpJsonToDict(const McpJson &props, Dict &dict)
{
	if (!props.isObject()) {
		mcpFail("properties must be a JSON object");
	}
	for (size_t i = 0; i < props.numKeys(); i++) {
		const std::string &keyName = props.keyAt(i);
		const McpJson &value = props.valueAt(i);
		NameKeyType key = TheNameKeyGenerator->nameToKey(keyName.c_str());
		if (value.isNull()) {
			dict.remove(key);
			continue;
		}
		Dict::DataType type = dict.getType(key);
		if (type == Dict::DICT_NONE) {
			if (value.isBool()) {
				type = Dict::DICT_BOOL;
			} else if (value.isNumber()) {
				type = (value.asNumber() == floor(value.asNumber())) ? Dict::DICT_INT : Dict::DICT_REAL;
			} else if (value.isString()) {
				type = Dict::DICT_ASCIISTRING;
			} else {
				mcpFail("property '%s' must be a bool, number or string", keyName.c_str());
			}
		}
		switch (type) {
			case Dict::DICT_BOOL:
				if (!value.isBool()) mcpFail("property '%s' must be a bool", keyName.c_str());
				dict.setBool(key, value.asBool());
				break;
			case Dict::DICT_INT:
				if (!value.isNumber()) mcpFail("property '%s' must be a number", keyName.c_str());
				dict.setInt(key, (Int)floor(value.asNumber() + 0.5));
				break;
			case Dict::DICT_REAL:
				if (!value.isNumber()) mcpFail("property '%s' must be a number", keyName.c_str());
				dict.setReal(key, (Real)value.asNumber());
				break;
			case Dict::DICT_ASCIISTRING:
				if (!value.isString()) mcpFail("property '%s' must be a string", keyName.c_str());
				dict.setAsciiString(key, AsciiString(value.asString().c_str()));
				break;
			case Dict::DICT_UNICODESTRING: {
				if (!value.isString()) mcpFail("property '%s' must be a string", keyName.c_str());
				UnicodeString u;
				u.translate(AsciiString(value.asString().c_str()));
				dict.setUnicodeString(key, u);
				break;
			}
			default:
				break;
		}
	}
}
