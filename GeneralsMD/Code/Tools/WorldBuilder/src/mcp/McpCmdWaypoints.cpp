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

// McpCmdWaypoints.cpp
// MCP bridge commands for waypoints, polygon triggers (including water areas) and roads.

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "CUndoable.h"
#include "PointerTool.h"
#include "WaypointOptions.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "mapobjectprops.h"
#include "Common/GlobalData.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameClient/TerrainRoads.h"
#include "GameLogic/PolygonTrigger.h"

#include <math.h>

namespace
{

//-------------------------------------------------------------------------------------------------
// Waypoints
//-------------------------------------------------------------------------------------------------

MapObject *findWaypointByName(const std::string &name)
{
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->isWaypoint() && _stricmp(obj->getWaypointName().str(), name.c_str()) == 0) {
			return obj;
		}
	}
	return nullptr;
}

/// Accepts a waypoint name or its numeric waypoint id.
MapObject *requireWaypoint(const McpJson &args, const char *key)
{
	const McpJson &v = args.get(key);
	MapObject *obj = nullptr;
	if (v.isNumber()) {
		obj = mcpDoc()->getWaypointByID((Int)v.asNumber());
	} else if (v.isString()) {
		obj = findWaypointByName(v.asString());
	} else {
		mcpFail("argument '%s' must be a waypoint name or waypoint id", key);
	}
	if (obj == nullptr) {
		mcpFail("waypoint '%s' not found", v.isString() ? v.asString().c_str() : "(id)");
	}
	return obj;
}

McpJson describeWaypoint(MapObject *obj)
{
	McpJson j = mcpDescribeObject(obj);
	Dict *props = obj->getProperties();
	McpJson labels = McpJson::makeArray();
	const NameKeyType labelKeys[3] = { TheKey_waypointPathLabel1, TheKey_waypointPathLabel2, TheKey_waypointPathLabel3 };
	for (Int i = 0; i < 3; i++) {
		Bool exists = false;
		AsciiString label = props->getAsciiString(labelKeys[i], &exists);
		if (exists && !label.isEmpty()) {
			labels.push(label.str());
		}
	}
	j.set("path_labels", labels);
	return j;
}

McpJson cmdListWaypoints(const McpJson &)
{
	CWorldBuilderDoc *doc = mcpDoc();
	McpJson waypoints = McpJson::makeArray();
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->isWaypoint()) {
			waypoints.push(describeWaypoint(obj));
		}
	}
	McpJson links = McpJson::makeArray();
	for (Int i = 0; i < doc->getNumWaypointLinks(); i++) {
		Int from, to;
		doc->getWaypointLink(i, &from, &to);
		MapObject *fromObj = doc->getWaypointByID(from);
		MapObject *toObj = doc->getWaypointByID(to);
		McpJson link = McpJson::makeObject().set("from", from).set("to", to);
		if (fromObj) link.set("from_name", fromObj->getWaypointName().str());
		if (toObj) link.set("to_name", toObj->getWaypointName().str());
		links.push(link);
	}
	return McpJson::makeObject().set("waypoints", waypoints).set("links", links);
}

McpJson cmdAddWaypoint(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	Coord3D loc;
	loc.x = (Real)mcpArgNumber(args, "x");
	loc.y = (Real)mcpArgNumber(args, "y");
	loc.z = 0;

	AsciiString name;
	if (args.has("name")) {
		std::string requested = mcpArgString(args, "name");
		if (requested.empty() || findWaypointByName(requested)) {
			mcpFail("waypoint name must be unique and not empty");
		}
		name = requested.c_str();
	}

	MapObject *linkFrom = args.has("link_from") ? requireWaypoint(args, "link_from") : nullptr;

	MapObject *obj = newInstance(MapObject)(loc, "*Waypoints/Waypoint", 0, 0, nullptr, nullptr);
	const Int id = doc->getNextWaypointID();
	if (name.isEmpty()) {
		name = WaypointOptions::GenerateUniqueName(id);
	}
	obj->setIsWaypoint();
	obj->setWaypointID(id);
	obj->setWaypointName(name);
	obj->getProperties()->setAsciiString(TheKey_originalOwner, "team");

	const McpJson &labels = args.get("path_labels");
	if (labels.isArray()) {
		const NameKeyType labelKeys[3] = { TheKey_waypointPathLabel1, TheKey_waypointPathLabel2, TheKey_waypointPathLabel3 };
		for (size_t i = 0; i < labels.size() && i < 3; i++) {
			if (labels.at(i).isString()) {
				obj->getProperties()->setAsciiString(labelKeys[i], AsciiString(labels.at(i).asString().c_str()));
			}
		}
	}
	PointerTool::clearSelection();
	mcpCommit(new AddObjectUndoable(doc, obj));

	if (linkFrom) {
		doc->addWaypointLink(linkFrom->getWaypointID(), id);
		doc->updateLinkedWaypointLabels(linkFrom);
	}
	return describeWaypoint(obj);
}

McpJson cmdLinkWaypoints(const McpJson &args, bool link)
{
	CWorldBuilderDoc *doc = mcpDoc();
	MapObject *from = requireWaypoint(args, "from");
	MapObject *to = requireWaypoint(args, "to");
	const Int fromId = from->getWaypointID();
	const Int toId = to->getWaypointID();
	if (fromId == toId) {
		mcpFail("cannot link a waypoint to itself");
	}
	if (link) {
		if (!doc->waypointLinkExists(fromId, toId)) {
			doc->addWaypointLink(fromId, toId);
		}
	} else {
		doc->removeWaypointLink(fromId, toId);
	}
	doc->updateLinkedWaypointLabels(from);
	// Links are not part of the undo system, so flag the change directly.
	doc->SetModifiedFlag();
	doc->invalObject(nullptr);
	return McpJson::makeObject().set("from", fromId).set("to", toId).set("linked", doc->waypointLinkExists(fromId, toId) != 0);
}

McpJson cmdLink(const McpJson &args)
{
	return cmdLinkWaypoints(args, true);
}

McpJson cmdUnlink(const McpJson &args)
{
	return cmdLinkWaypoints(args, false);
}

//-------------------------------------------------------------------------------------------------
// Polygon triggers and water
//-------------------------------------------------------------------------------------------------

McpJson describePolygon(PolygonTrigger *trig, bool withPoints)
{
	McpJson j = McpJson::makeObject();
	j.set("id", trig->getID());
	j.set("name", trig->getTriggerName().str());
	j.set("kind", trig->isRiver() ? "river" : (trig->isWaterArea() ? "water" : "area"));
	if (!trig->getLayerName().isEmpty()) {
		j.set("layer", trig->getLayerName().str());
	}
	j.set("num_points", trig->getNumPoints());
	if (trig->isWaterArea() && trig->getNumPoints() > 0) {
		j.set("water_height", trig->getPoint(0)->z);
	}
	if (withPoints) {
		McpJson points = McpJson::makeArray();
		for (Int i = 0; i < trig->getNumPoints(); i++) {
			const ICoord3D *pt = trig->getPoint(i);
			McpJson p = McpJson::makeArray();
			p.push(pt->x);
			p.push(pt->y);
			p.push(pt->z);
			points.push(p);
		}
		j.set("points", points);
	}
	return j;
}

McpJson cmdListPolygons(const McpJson &args)
{
	mcpDoc();
	const bool withPoints = mcpArgBool(args, "include_points", false);
	McpJson list = McpJson::makeArray();
	for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
		list.push(describePolygon(trig, withPoints));
	}
	return McpJson::makeObject().set("polygons", list);
}

McpJson cmdAddPolygon(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const McpJson &points = mcpArgArray(args, "points");
	const std::string kind = mcpArgString(args, "kind", "area");
	const bool isWater = (kind == "water" || kind == "river");
	if (!isWater && kind != "area") {
		mcpFail("kind must be area, water or river");
	}
	if (points.size() < 3) {
		mcpFail("a polygon needs at least 3 points");
	}
	const Int waterHeight = (Int)floor(mcpArgNumber(args, "water_height", TheGlobalData->m_waterPositionZ) + 0.5);

	PolygonTrigger *trig = newInstance(PolygonTrigger)((Int)points.size());
	for (size_t i = 0; i < points.size(); i++) {
		const McpJson &p = points.at(i);
		if (!p.isArray() || p.size() < 2 || !p.at(0).isNumber() || !p.at(1).isNumber()) {
			deleteInstance(trig);
			mcpFail("points must be [x, y] pairs in world units");
		}
		ICoord3D pt;
		pt.x = (Int)floor(p.at(0).asNumber() + 0.5);
		pt.y = (Int)floor(p.at(1).asNumber() + 0.5);
		if (isWater) {
			pt.z = waterHeight;
		} else {
			// Trigger areas sit on the terrain, like the ones drawn with the polygon tool.
			Int ix = mcpWorldToIndexX(pt.x);
			Int iy = mcpWorldToIndexY(pt.y);
			pt.z = (Int)floor(map->getHeight(ix, iy) * MAP_HEIGHT_SCALE + 0.5);
		}
		trig->addPoint(pt);
	}
	AsciiString name;
	if (args.has("name")) {
		name = mcpArgString(args, "name").c_str();
	} else {
		name.format("%s %d", isWater ? "Water Area" : "Area", trig->getID());
	}
	trig->setTriggerName(name);
	trig->setWaterArea(isWater);
	trig->setRiver(kind == "river");
	if (kind == "river") {
		trig->setRiverStart(mcpArgInt(args, "river_start", 0));
	}
	mcpCommit(new AddPolygonUndoable(trig));
	return describePolygon(trig, true);
}

McpJson cmdDeletePolygon(const McpJson &args)
{
	mcpDoc();
	PolygonTrigger *trig = mcpRequirePolygon(mcpArgInt(args, "id"));
	mcpCommit(new DeletePolygonUndoable(trig));
	return McpJson::makeObject().set("deleted", mcpArgInt(args, "id"));
}

//-------------------------------------------------------------------------------------------------
// Roads
//-------------------------------------------------------------------------------------------------

McpJson cmdListRoadTypes(const McpJson &)
{
	McpJson roads = McpJson::makeArray();
	for (TerrainRoadType *road = TheTerrainRoads->firstRoad(); road; road = TheTerrainRoads->nextRoad(road)) {
		roads.push(road->getName().str());
	}
	McpJson bridges = McpJson::makeArray();
	for (TerrainRoadType *bridge = TheTerrainRoads->firstBridge(); bridge; bridge = TheTerrainRoads->nextBridge(bridge)) {
		McpJson b = McpJson::makeObject().set("name", bridge->getName().str());
		const ThingTemplate *tt = TheThingFactory->findTemplate(bridge->getName(), FALSE);
		b.set("landmark", tt != nullptr && tt->isBridge());
		bridges.push(b);
	}
	return McpJson::makeObject().set("roads", roads).set("bridges", bridges);
}

McpJson cmdAddRoad(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	const std::string type = mcpArgString(args, "type");
	AsciiString roadName(type.c_str());
	const bool isBridge = TheTerrainRoads->findBridge(roadName) != nullptr;
	if (!isBridge && TheTerrainRoads->findRoad(roadName) == nullptr) {
		mcpFail("unknown road type '%s' (see roads.list_types)", type.c_str());
	}
	const ThingTemplate *tt = isBridge ? TheThingFactory->findTemplate(roadName, FALSE) : nullptr;
	if (tt && tt->isBridge()) {
		mcpFail("'%s' is a landmark bridge; place it with objects.place", type.c_str());
	}
	const McpJson &points = mcpArgArray(args, "points");
	if (points.size() < 2) {
		mcpFail("a road needs at least 2 points");
	}
	if (isBridge && points.size() != 2) {
		mcpFail("a bridge is a single segment with exactly 2 points");
	}
	const std::string corner = mcpArgString(args, "corners", "curved");
	Int cornerFlag = 0;
	if (corner == "angled") cornerFlag = FLAG_ROAD_CORNER_ANGLED;
	else if (corner == "tight") cornerFlag = FLAG_ROAD_CORNER_TIGHT;
	else if (corner != "curved") mcpFail("corners must be curved, angled or tight");

	// Each segment is a pair of map objects; consecutive segments share end locations so the
	// renderer joins them.
	MapObject *head = nullptr;
	MapObject *tail = nullptr;
	for (size_t i = 0; i + 1 < points.size(); i++) {
		Coord3D loc[2];
		for (Int k = 0; k < 2; k++) {
			const McpJson &p = points.at(i + k);
			if (!p.isArray() || p.size() < 2 || !p.at(0).isNumber() || !p.at(1).isNumber()) {
				if (head) deleteInstance(head);
				mcpFail("points must be [x, y] pairs in world units");
			}
			loc[k].x = (Real)p.at(0).asNumber();
			loc[k].y = (Real)p.at(1).asNumber();
			loc[k].z = 0; // roads stick to the terrain.
		}
		MapObject *p1 = newInstance(MapObject)(loc[0], roadName, 0.0f, 0, nullptr, nullptr);
		MapObject *p2 = newInstance(MapObject)(loc[1], roadName, 0.0f, 0, nullptr, nullptr);
		p1->setColor(RGB(255, 255, 0));
		p2->setColor(RGB(255, 255, 0));
		p1->setFlag(isBridge ? FLAG_BRIDGE_POINT1 : FLAG_ROAD_POINT1);
		p2->setFlag(isBridge ? FLAG_BRIDGE_POINT2 : FLAG_ROAD_POINT2);
		if (!isBridge && cornerFlag) {
			p1->setFlag(cornerFlag);
			p2->setFlag(cornerFlag);
		}
		p1->getProperties()->setAsciiString(TheKey_originalOwner, NEUTRAL_TEAM_INTERNAL_STR);
		p2->getProperties()->setAsciiString(TheKey_originalOwner, NEUTRAL_TEAM_INTERNAL_STR);
		p1->setNextMap(p2);
		if (tail) tail->setNextMap(p1); else head = p1;
		tail = p2;
	}
	PointerTool::clearSelection();
	mcpCommit(new AddObjectUndoable(doc, head));

	McpJson ids = McpJson::makeArray();
	for (MapObject *obj = head; obj; obj = obj->getNext()) {
		ids.push(mcpObjectHandle(obj));
		if (obj == tail) break;
	}
	return McpJson::makeObject().set("type", type).set("bridge", isBridge).set("segments", (int)points.size() - 1).set("point_ids", ids);
}

} // namespace

void mcpRegisterWaypointCommands()
{
	mcpRegisterCommand("waypoints.list", cmdListWaypoints, "All waypoints and the links between them.");
	mcpRegisterCommand("waypoints.add", cmdAddWaypoint, "{x,y,name?,path_labels?,link_from?} Adds a waypoint.");
	mcpRegisterCommand("waypoints.link", cmdLink, "{from,to} Links two waypoints (names or waypoint ids). Not undoable.");
	mcpRegisterCommand("waypoints.unlink", cmdUnlink, "{from,to} Removes a waypoint link. Not undoable.");
	mcpRegisterCommand("polygons.list", cmdListPolygons, "{include_points?} Trigger areas and water areas.");
	mcpRegisterCommand("polygons.add", cmdAddPolygon, "{points:[[x,y]...],kind:area|water|river,name?,water_height?,river_start?} Adds a polygon.");
	mcpRegisterCommand("polygons.delete", cmdDeletePolygon, "{id} Deletes a polygon.");
	mcpRegisterCommand("roads.list_types", cmdListRoadTypes, "Road and bridge types.");
	mcpRegisterCommand("roads.add", cmdAddRoad, "{type,points:[[x,y]...],corners?:curved|angled|tight} Adds a road polyline or a 2-point bridge.");
}
