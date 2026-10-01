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

// McpCmdObjects.cpp
// MCP bridge commands for map objects, players and teams.

#include "StdAfx.h"
#define DEFINE_EDITOR_SORTING_NAMES
#include "mcp/McpCommands.h"
#include "mcp/McpUndoables.h"

#include "CUndoable.h"
#include "PointerTool.h"
#include "WorldBuilderDoc.h"
#include "wbview3d.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingSort.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/SidesList.h"

#include <math.h>
#include <vector>

namespace
{

std::vector<MapObject *> requireObjects(const McpJson &args)
{
	std::vector<MapObject *> objects;
	if (args.has("id")) {
		objects.push_back(mcpRequireObject(mcpArgInt(args, "id")));
	}
	if (args.has("ids")) {
		const McpJson &ids = mcpArgArray(args, "ids");
		for (size_t i = 0; i < ids.size(); i++) {
			if (!ids.at(i).isNumber()) {
				mcpFail("ids must be numbers");
			}
			objects.push_back(mcpRequireObject((int)ids.at(i).asNumber()));
		}
	}
	if (objects.empty()) {
		mcpFail("pass 'id' or 'ids'");
	}
	return objects;
}

/// Accepts a team name ("teamPlayer_1"), a player name ("Player_1") or "" / "neutral" for the neutral team.
AsciiString resolveOwner(const std::string &owner)
{
	if (owner.empty() || _stricmp(owner.c_str(), "neutral") == 0) {
		return AsciiString("team");
	}
	AsciiString name(owner.c_str());
	Int ndx;
	if (TheSidesList->findTeamInfo(name, &ndx)) {
		return name;
	}
	AsciiString teamName;
	teamName.format("team%s", owner.c_str());
	if (TheSidesList->findTeamInfo(teamName, &ndx)) {
		return teamName;
	}
	mcpFail("unknown owner '%s' (see sides.list for team and player names)", owner.c_str());
	return AsciiString::TheEmptyString;
}

const ThingTemplate *requireTemplate(const std::string &name)
{
	const ThingTemplate *tt = TheThingFactory->findTemplate(AsciiString(name.c_str()), FALSE);
	if (tt == nullptr) {
		mcpFail("unknown object template '%s' (see objects.list_templates)", name.c_str());
	}
	return tt;
}

const char *editorSortingName(EditorSortingType sorting)
{
	if (sorting >= ES_FIRST && sorting < ES_NUM_SORTING_TYPES && EditorSortingNames[sorting]) {
		return EditorSortingNames[sorting];
	}
	return "NONE";
}

//-------------------------------------------------------------------------------------------------
// Templates
//-------------------------------------------------------------------------------------------------

McpJson cmdListTemplates(const McpJson &args)
{
	const std::string filter = mcpArgString(args, "filter", "");
	const std::string sorting = mcpArgString(args, "editor_sorting", "");
	const std::string side = mcpArgString(args, "side", "");
	const Int limit = mcpArgInt(args, "limit", 200);
	const Int offset = mcpArgInt(args, "offset", 0);

	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (const ThingTemplate *tt = TheThingFactory->firstTemplate(); tt; tt = tt->friend_getNextTemplate()) {
		if (!mcpContainsNoCase(tt->getName().str(), filter.c_str())) continue;
		const char *sortName = editorSortingName(tt->getEditorSorting());
		if (!sorting.empty() && _stricmp(sortName, sorting.c_str()) != 0) continue;
		if (!side.empty() && _stricmp(tt->getDefaultOwningSide().str(), side.c_str()) != 0) continue;
		matched++;
		if (matched <= offset || (Int)list.size() >= limit) continue;
		McpJson t = McpJson::makeObject();
		t.set("name", tt->getName().str());
		t.set("editor_sorting", sortName);
		t.set("side", tt->getDefaultOwningSide().str());
		AsciiString displayName;
		displayName.translate(tt->getDisplayName());
		if (!displayName.isEmpty()) t.set("display_name", displayName.str());
		if (tt->isBridge()) t.set("bridge", true);
		list.push(t);
	}
	return McpJson::makeObject().set("total_matches", matched).set("templates", list);
}

//-------------------------------------------------------------------------------------------------
// Sides
//-------------------------------------------------------------------------------------------------

McpJson cmdListSides(const McpJson &)
{
	mcpDoc();
	McpJson players = McpJson::makeArray();
	for (Int i = 0; i < TheSidesList->getNumSides(); i++) {
		Dict *d = TheSidesList->getSideInfo(i)->getDict();
		McpJson p = mcpDictToJson(*d);
		AsciiString name = d->getAsciiString(TheKey_playerName);
		p.set("is_neutral", name.isEmpty());
		players.push(p);
	}
	McpJson teams = McpJson::makeArray();
	for (Int i = 0; i < TheSidesList->getNumTeams(); i++) {
		Dict *d = TheSidesList->getTeamInfo(i)->getDict();
		teams.push(McpJson::makeObject()
			.set("name", d->getAsciiString(TheKey_teamName).str())
			.set("owner", d->getAsciiString(TheKey_teamOwner).str()));
	}
	return McpJson::makeObject().set("players", players).set("teams", teams);
}

McpJson cmdListFactions(const McpJson &)
{
	McpJson list = McpJson::makeArray();
	for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); i++) {
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (pt == nullptr) continue;
		list.push(McpJson::makeObject().set("name", pt->getName().str()).set("side", pt->getSide().str()));
	}
	return McpJson::makeObject().set("factions", list);
}

McpJson cmdAddPlayer(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	const std::string faction = mcpArgString(args, "faction");
	const PlayerTemplate *pt = ThePlayerTemplateStore->findPlayerTemplate(NAMEKEY(faction.c_str()));
	if (pt == nullptr) {
		mcpFail("unknown faction '%s' (see sides.list_factions)", faction.c_str());
	}
	SidesList newSides = *TheSidesList;
	if (newSides.getNumSides() >= MAX_PLAYER_COUNT - 1) {
		mcpFail("the map already has the maximum number of players");
	}

	AsciiString name;
	if (args.has("name")) {
		name = mcpArgString(args, "name").c_str();
		if (name.isEmpty() || newSides.findSideInfo(name)) {
			mcpFail("player name must be unique and not empty");
		}
	} else {
		Int num = 1;
		do {
			name.format("player%04d", num++);
		} while (newSides.findSideInfo(name));
	}
	UnicodeString displayName;
	displayName.translate(AsciiString(mcpArgString(args, "display_name", name.str()).c_str()));

	Dict playerDict;
	playerDict.setAsciiString(TheKey_playerName, name);
	playerDict.setBool(TheKey_playerIsHuman, mcpArgBool(args, "is_human", true));
	playerDict.setUnicodeString(TheKey_playerDisplayName, displayName);
	playerDict.setAsciiString(TheKey_playerFaction, pt->getName());
	playerDict.setAsciiString(TheKey_playerEnemies, AsciiString(mcpArgString(args, "enemies", "").c_str()));
	playerDict.setAsciiString(TheKey_playerAllies, AsciiString(mcpArgString(args, "allies", "").c_str()));
	newSides.addSide(&playerDict);
	newSides.validateSides();

	mcpCommit(new SidesListUndoable(newSides, doc));
	AsciiString teamName;
	teamName.format("team%s", name.str());
	return McpJson::makeObject().set("player", name.str()).set("default_team", teamName.str());
}

//-------------------------------------------------------------------------------------------------
// Objects
//-------------------------------------------------------------------------------------------------

McpJson cmdListObjects(const McpJson &args)
{
	mcpDoc();
	const std::string templateFilter = mcpArgString(args, "template", "");
	const std::string owner = mcpArgString(args, "owner", "");
	const bool includeAll = mcpArgBool(args, "include_waypoints_and_roads", false);
	const Int limit = mcpArgInt(args, "limit", 500);
	const bool hasRect = args.has("x0");
	double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (hasRect) {
		x0 = mcpArgNumber(args, "x0");
		y0 = mcpArgNumber(args, "y0");
		x1 = mcpArgNumber(args, "x1");
		y1 = mcpArgNumber(args, "y1");
	}

	McpJson list = McpJson::makeArray();
	Int matched = 0;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (!includeAll && (obj->isWaypoint() || (obj->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS)))) continue;
		if (!mcpContainsNoCase(obj->getName().str(), templateFilter.c_str())) continue;
		if (!owner.empty() && _stricmp(obj->getProperties()->getAsciiString(TheKey_originalOwner).str(), owner.c_str()) != 0) continue;
		if (hasRect) {
			const Coord3D *loc = obj->getLocation();
			if (loc->x < x0 || loc->x > x1 || loc->y < y0 || loc->y > y1) continue;
		}
		matched++;
		if ((Int)list.size() < limit) {
			list.push(mcpDescribeObject(obj));
		}
	}
	return McpJson::makeObject().set("total_matches", matched).set("objects", list);
}

McpJson cmdGetObject(const McpJson &args)
{
	MapObject *obj = mcpRequireObject(mcpArgInt(args, "id"));
	McpJson j = mcpDescribeObject(obj);
	j.set("properties", mcpDictToJson(*obj->getProperties()));
	return j;
}

McpJson cmdPlaceObject(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	const ThingTemplate *tt = requireTemplate(mcpArgString(args, "template"));
	Coord3D loc;
	loc.x = (Real)mcpArgNumber(args, "x");
	loc.y = (Real)mcpArgNumber(args, "y");
	loc.z = (Real)mcpArgNumber(args, "z", 0); // 0 means "on the ground".
	Real angle = args.has("angle_deg") ? (Real)(mcpArgNumber(args, "angle_deg") * PI / 180.0) : tt->getPlacementViewAngle();
	AsciiString owner = resolveOwner(mcpArgString(args, "owner", ""));

	Dict props;
	if (args.has("properties")) {
		mcpJsonToDict(args.get("properties"), props);
	}
	props.setAsciiString(TheKey_originalOwner, owner);
	if (args.has("name")) {
		props.setAsciiString(TheKey_objectName, AsciiString(mcpArgString(args, "name").c_str()));
	}

	MapObject *obj = newInstance(MapObject)(loc, tt->getName(), angle, 0, &props, tt);
	PointerTool::clearSelection();
	mcpCommit(new AddObjectUndoable(doc, obj));
	return mcpDescribeObject(obj);
}

McpJson cmdModifyObjects(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	std::vector<MapObject *> objects = requireObjects(args);
	const ThingTemplate *tt = args.has("template") ? requireTemplate(mcpArgString(args, "template")) : nullptr;
	if ((args.has("x") || args.has("y")) && objects.size() > 1) {
		mcpFail("absolute x/y can only be set for one object; use dx/dy for several");
	}

	McpModifyObjectsUndoable *undo = new McpModifyObjectsUndoable(doc);
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < objects.size(); i++) {
		MoveInfo *info = undo->add(objects[i]);
		info->m_newLocation.x = (Real)mcpArgNumber(args, "x", info->m_oldLocation.x) + (Real)mcpArgNumber(args, "dx", 0);
		info->m_newLocation.y = (Real)mcpArgNumber(args, "y", info->m_oldLocation.y) + (Real)mcpArgNumber(args, "dy", 0);
		info->m_newLocation.z = (Real)mcpArgNumber(args, "z", info->m_oldLocation.z);
		if (args.has("angle_deg")) {
			info->m_newAngle = (Real)(mcpArgNumber(args, "angle_deg") * PI / 180.0);
		} else if (args.has("rotate_deg")) {
			info->m_newAngle = info->m_oldAngle + (Real)(mcpArgNumber(args, "rotate_deg") * PI / 180.0);
		}
		if (tt) {
			info->m_newThing = tt;
			info->m_newName = tt->getName();
			info->m_oldName = objects[i]->getName();
			undo->setTemplateChanged();
		}
	}
	mcpCommit(undo);
	for (size_t i = 0; i < objects.size(); i++) {
		list.push(mcpDescribeObject(objects[i]));
	}
	return McpJson::makeObject().set("objects", list);
}

McpJson cmdSetProperties(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	std::vector<MapObject *> objects = requireObjects(args);
	McpJson props = args.get("properties");
	if (props.isNull()) {
		props = McpJson::makeObject();
	}
	if (args.has("owner")) {
		props.set("originalOwner", McpJson(resolveOwner(mcpArgString(args, "owner")).str()));
	}
	if (args.has("name")) {
		props.set("objectName", McpJson(mcpArgString(args, "name")));
	}
	if (props.numKeys() == 0) {
		mcpFail("nothing to change; pass properties, owner or name");
	}

	MultipleUndoable *undo = new MultipleUndoable;
	for (size_t i = 0; i < objects.size(); i++) {
		Dict newDict = *objects[i]->getProperties();
		try {
			mcpJsonToDict(props, newDict);
		} catch (const McpError &) {
			REF_PTR_RELEASE(undo);
			throw;
		}
		Dict *target = objects[i]->getProperties();
		undo->addUndoable(new DictItemUndoable(&target, newDict, NAMEKEY_INVALID, 1, doc, true));
	}
	mcpCommit(undo);
	McpJson list = McpJson::makeArray();
	for (size_t i = 0; i < objects.size(); i++) {
		list.push(mcpDescribeObject(objects[i]));
	}
	return McpJson::makeObject().set("objects", list);
}

McpJson cmdDeleteObjects(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	std::vector<MapObject *> objects = requireObjects(args);
	PointerTool::clearSelection();
	for (size_t i = 0; i < objects.size(); i++) {
		objects[i]->setSelected(true);
	}
	mcpCommit(new DeleteObjectUndoable(doc));
	PointerTool::clearSelection();
	return McpJson::makeObject().set("deleted", (int)objects.size());
}

} // namespace

void mcpRegisterObjectCommands()
{
	mcpRegisterCommand("objects.list_templates", cmdListTemplates, "{filter?,editor_sorting?,side?,limit?,offset?} Object templates that can be placed.");
	mcpRegisterCommand("objects.list", cmdListObjects, "{template?,owner?,x0,y0,x1,y1?,include_waypoints_and_roads?,limit?} Objects in the map.");
	mcpRegisterCommand("objects.get", cmdGetObject, "{id} One object with all its properties.");
	mcpRegisterCommand("objects.place", cmdPlaceObject, "{template,x,y,z?,angle_deg?,owner?,name?,properties?} Places an object.");
	mcpRegisterCommand("objects.modify", cmdModifyObjects, "{id|ids, x?,y?,dx?,dy?,z?,angle_deg?,rotate_deg?,template?} Moves, rotates or swaps objects.");
	mcpRegisterCommand("objects.set_properties", cmdSetProperties, "{id|ids, properties?, owner?, name?} Changes object properties (null removes a key).");
	mcpRegisterCommand("objects.delete", cmdDeleteObjects, "{id|ids} Deletes objects.");
	mcpRegisterCommand("sides.list", cmdListSides, "Players and teams; object owners are team names.");
	mcpRegisterCommand("sides.list_factions", cmdListFactions, "Player templates usable with sides.add_player.");
	mcpRegisterCommand("sides.add_player", cmdAddPlayer, "{faction,name?,display_name?,is_human?,allies?,enemies?} Adds a player and its default team.");
}
