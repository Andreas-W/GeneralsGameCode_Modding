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

// McpCmdResize.cpp
// MCP bridge command that grows or crops the map on any side while everything on it stays where
// it is on the terrain (idea from the Resize command of the Genesis map tools, The CWC Team).
// WorldBuilder's own File > Resize moves objects only; here areas, water and build lists move too.

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpUndoables.h"

#include "PointerTool.h"
#include "TerrainMaterial.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "wbview.h"
#include "Common/MapObject.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"

#include <set>
#include <string>

namespace
{

// Same limits as map.new.
const Int MIN_MAP_SIZE = 8;
const Int MAX_MAP_SIZE = 1024;
const Int MAX_BORDER = 100;

//-------------------------------------------------------------------------------------------------
// Undoables
//-------------------------------------------------------------------------------------------------

/// Swaps in the resized heightmap and makes the views pick up the new map size.
class ResizeHeightMapUndoable : public WBDocUndoable
{
public:
	ResizeHeightMapUndoable(CWorldBuilderDoc *doc, WorldHeightMapEdit *newMap) : WBDocUndoable(doc, newMap), m_doc(doc) {}

	virtual void Do() override
	{
		WBDocUndoable::Do();
		// Do() sets the heightmap without telling the views.
		IRegion2D all = { 0, 0, 0, 0 };
		m_doc->updateHeightMap(m_doc->GetHeightMap(), false, all);
		adjustViews();
	}
	virtual void Undo() override
	{
		WBDocUndoable::Undo();
		adjustViews();
	}
	virtual void Redo() override
	{
		WBDocUndoable::Redo();
		adjustViews();
	}

private:
	void adjustViews()
	{
		POSITION pos = m_doc->GetFirstViewPosition();
		while (pos != nullptr) {
			WbView *view = (WbView *)m_doc->GetNextView(pos);
			view->adjustDocSize();
			view->Invalidate();
		}
	}

	CWorldBuilderDoc *m_doc;
};

/// Shifts everything that has a world position: objects, waypoints, road points and build list
/// entries by an offset, areas and water to the points given with addPolygon().
class OffsetWorldUndoable : public Undoable
{
public:
	OffsetWorldUndoable(CWorldBuilderDoc *doc, Int dx, Int dy) : m_doc(doc), m_dx(dx), m_dy(dy) {}

	void addPolygon(PolygonTrigger *trig, const std::vector<ICoord3D> &newPoints)
	{
		PolygonMove entry;
		entry.trig = trig;
		entry.newPoints = newPoints;
		for (Int p = 0; p < trig->getNumPoints(); p++) {
			entry.oldPoints.push_back(*trig->getPoint(p));
		}
		m_polygons.push_back(entry);
	}

	virtual void Do() override { apply(1); }
	virtual void Undo() override { apply(-1); }

private:
	struct PolygonMove
	{
		PolygonTrigger *trig;
		std::vector<ICoord3D> oldPoints, newPoints;
	};

	void apply(Int sign)
	{
		const Int dx = sign * m_dx, dy = sign * m_dy;
		for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
			Coord3D loc = *obj->getLocation();
			loc.x += dx;
			loc.y += dy;
			obj->setLocation(&loc);
		}
		for (Int i = 0; i < TheSidesList->getNumSides(); i++) {
			for (BuildListInfo *build = TheSidesList->getSideInfo(i)->getBuildList(); build; build = build->getNext()) {
				Coord3D loc = *build->getLocation();
				loc.x += dx;
				loc.y += dy;
				build->setLocation(loc);
			}
		}
		for (size_t i = 0; i < m_polygons.size(); i++) {
			const std::vector<ICoord3D> &pts = sign > 0 ? m_polygons[i].newPoints : m_polygons[i].oldPoints;
			for (size_t p = 0; p < pts.size(); p++) {
				m_polygons[i].trig->setPoint(pts[p], (Int)p);
			}
		}
		m_doc->invalObject(nullptr);
	}

	CWorldBuilderDoc *m_doc;
	Int m_dx, m_dy;
	std::vector<PolygonMove> m_polygons;
};

/// World extents of a heightmap, border included.
struct Extents
{
	Int minX, minY, maxX, maxY;

	void init(Int width, Int height, Int border)
	{
		minX = minY = -border * (Int)MAP_XY_FACTOR;
		maxX = (width - 1 + border) * (Int)MAP_XY_FACTOR;
		maxY = (height - 1 + border) * (Int)MAP_XY_FACTOR;
	}
	bool contains(double x, double y) const
	{
		return x >= minX && y >= minY && x <= maxX && y <= maxY;
	}
};

/// True if every point lies on or beyond a corner of the map, as for the "Default Water" area that
/// WorldBuilder creates around a new map. Such a polygon is meant to cover the map whatever its size.
bool coversWholeMap(PolygonTrigger *trig, const Extents &ext)
{
	if (trig->getNumPoints() < 4) return false;
	bool corner[4] = { false, false, false, false };
	for (Int p = 0; p < trig->getNumPoints(); p++) {
		const ICoord3D *pt = trig->getPoint(p);
		const bool low = pt->x <= ext.minX, right = pt->x >= ext.maxX;
		const bool bottom = pt->y <= ext.minY, top = pt->y >= ext.maxY;
		if (!(low || right) || !(bottom || top)) return false;
		corner[(right ? 1 : 0) + (top ? 2 : 0)] = true;
	}
	return corner[0] && corner[1] && corner[2] && corner[3];
}

//-------------------------------------------------------------------------------------------------
// map.resize
//-------------------------------------------------------------------------------------------------

bool isRoadPoint(MapObject *obj)
{
	return (obj->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS)) != 0;
}

bool isRoadStart(MapObject *obj)
{
	return (obj->getFlag(FLAG_ROAD_POINT1) || obj->getFlag(FLAG_BRIDGE_POINT1)) && obj->getNext() != nullptr;
}

/// Splits a size change between the two sides of an axis. anchor is -1 to keep the low side
/// (left or bottom) in place, 1 to keep the high side, 0 to keep the center.
void splitChange(Int change, Int anchor, Int *addLow, Int *addHigh)
{
	if (anchor < 0) {
		*addLow = 0;
	} else if (anchor > 0) {
		*addLow = change;
	} else {
		*addLow = change / 2;
	}
	*addHigh = change - *addLow;
}

McpJson cmdResize(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int oldBorder = map->getBorderSize();
	const Int oldWidth = map->getXExtent() - 2 * oldBorder;
	const Int oldHeight = map->getYExtent() - 2 * oldBorder;

	const bool bySides = args.has("add_left") || args.has("add_right") || args.has("add_bottom") || args.has("add_top");
	const bool bySize = args.has("width") || args.has("height") || args.has("anchor");
	if (bySides && bySize) {
		mcpFail("give either width/height with an anchor, or add_left/add_right/add_bottom/add_top, not both");
	}
	Int addLeft = 0, addRight = 0, addBottom = 0, addTop = 0;
	if (bySides) {
		addLeft = mcpArgInt(args, "add_left", 0);
		addRight = mcpArgInt(args, "add_right", 0);
		addBottom = mcpArgInt(args, "add_bottom", 0);
		addTop = mcpArgInt(args, "add_top", 0);
	} else {
		const std::string anchor = mcpArgString(args, "anchor", "center");
		Int anchorX = 0, anchorY = 0;
		if (anchor == "center") { anchorX = 0; anchorY = 0; }
		else if (anchor == "left") { anchorX = -1; anchorY = 0; }
		else if (anchor == "right") { anchorX = 1; anchorY = 0; }
		else if (anchor == "bottom") { anchorX = 0; anchorY = -1; }
		else if (anchor == "top") { anchorX = 0; anchorY = 1; }
		else if (anchor == "bottom_left") { anchorX = -1; anchorY = -1; }
		else if (anchor == "bottom_right") { anchorX = 1; anchorY = -1; }
		else if (anchor == "top_left") { anchorX = -1; anchorY = 1; }
		else if (anchor == "top_right") { anchorX = 1; anchorY = 1; }
		else mcpFail("anchor must be center, left, right, bottom, top, bottom_left, bottom_right, top_left or top_right");
		splitChange(mcpArgInt(args, "width", oldWidth) - oldWidth, anchorX, &addLeft, &addRight);
		splitChange(mcpArgInt(args, "height", oldHeight) - oldHeight, anchorY, &addBottom, &addTop);
	}
	const Int newWidth = oldWidth + addLeft + addRight;
	const Int newHeight = oldHeight + addBottom + addTop;
	if (newWidth < MIN_MAP_SIZE || newHeight < MIN_MAP_SIZE || newWidth > MAX_MAP_SIZE || newHeight > MAX_MAP_SIZE) {
		mcpFail("the new size would be %d x %d cells; width and height must be between %d and %d",
			newWidth, newHeight, MIN_MAP_SIZE, MAX_MAP_SIZE);
	}
	const Int border = mcpArgInt(args, "border", oldBorder);
	if (border < 0 || border > MAX_BORDER) {
		mcpFail("border must be 0..%d", MAX_BORDER);
	}
	const Int fillHeight = args.has("fill_height") ? mcpArgInt(args, "fill_height") : -1;
	if (args.has("fill_height") && (fillHeight < 0 || fillHeight > 255)) {
		mcpFail("fill_height must be 0..255");
	}
	Int fillTexture = -1;
	if (args.has("fill_texture")) {
		fillTexture = mcpFindTextureClass(args, "fill_texture");
		if (!map->isTexClassUsed(fillTexture) && !map->canFitTexture(fillTexture)) {
			mcpFail("the map has no room for another texture; use a fill_texture the map already has");
		}
	}
	const std::string removeOutside = mcpArgString(args, "remove_outside", "none");
	if (removeOutside != "none" && removeOutside != "objects" && removeOutside != "waypoints" && removeOutside != "all") {
		mcpFail("remove_outside must be none, objects, waypoints or all");
	}
	const bool removeObjects = removeOutside == "objects" || removeOutside == "all";
	const bool removeWaypoints = removeOutside == "waypoints" || removeOutside == "all";

	const Int offsetX = addLeft * (Int)MAP_XY_FACTOR;
	const Int offsetY = addBottom * (Int)MAP_XY_FACTOR;
	McpJson j = McpJson::makeObject();
	j.set("width", newWidth).set("height", newHeight).set("border", border);
	j.set("added", McpJson::makeObject().set("left", addLeft).set("right", addRight).set("bottom", addBottom).set("top", addTop));
	j.set("world_offset", McpJson::makeObject().set("x", offsetX).set("y", offsetY));
	if (addLeft == 0 && addRight == 0 && addBottom == 0 && addTop == 0 && border == oldBorder) {
		j.set("changed", false);
		return j;
	}

	WorldHeightMapEdit *copy = map->duplicate();
	if (copy == nullptr || !copy->resizeBySides(addLeft, addBottom, addRight, addTop, border, fillHeight, fillTexture)) {
		REF_PTR_RELEASE(copy);
		mcpFail("the heightmap could not be resized");
	}

	Extents oldExt, newExt;
	oldExt.init(oldWidth, oldHeight, oldBorder);
	newExt.init(newWidth, newHeight, border);
	// Objects that end up outside the new heightmap (border included), judged by their new position.
	struct Outside
	{
		Extents ext;
		Int dx, dy;
		bool operator()(MapObject *obj) const
		{
			return !ext.contains(obj->getLocation()->x + dx, obj->getLocation()->y + dy);
		}
	};
	const Outside outside = { newExt, offsetX, offsetY };

	Int objectsMoved = 0, removedObjects = 0, removedWaypoints = 0, removedRoadSegments = 0, outsideKept = 0;
	std::vector<MapObject *> toDelete;
	std::set<Int> deletedWaypointIds;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		objectsMoved++;
		if (isRoadStart(obj)) {
			// A road segment is two consecutive objects. It only goes when both ends are outside:
			// roads that run off the map are normal.
			MapObject *end = obj->getNext();
			objectsMoved++;
			if (outside(obj) && outside(end)) {
				if (removeObjects) {
					toDelete.push_back(obj);
					toDelete.push_back(end);
					removedRoadSegments++;
				} else {
					outsideKept += 2;
				}
			}
			obj = end;
			continue;
		}
		if (isRoadPoint(obj) || !outside(obj)) {
			continue;
		}
		if (obj->isWaypoint() ? removeWaypoints : removeObjects) {
			toDelete.push_back(obj);
			if (obj->isWaypoint()) {
				deletedWaypointIds.insert(obj->getWaypointID());
				removedWaypoints++;
			} else {
				removedObjects++;
			}
		} else {
			outsideKept++;
		}
	}
	objectsMoved -= (Int)toDelete.size();

	// Areas and water move with the terrain. One that covers the whole map is refit to the new
	// map instead, keeping its margin to each edge.
	OffsetWorldUndoable *offset = new OffsetWorldUndoable(doc, offsetX, offsetY);
	Int areasMoved = 0, areasRefit = 0, areasOutside = 0;
	for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
		const bool refit = coversWholeMap(trig, oldExt);
		std::vector<ICoord3D> pts;
		bool anyInside = refit || trig->getNumPoints() == 0;
		bool changed = false;
		for (Int p = 0; p < trig->getNumPoints(); p++) {
			ICoord3D pt = *trig->getPoint(p);
			if (refit) {
				pt.x = pt.x <= oldExt.minX ? newExt.minX + (pt.x - oldExt.minX) : newExt.maxX + (pt.x - oldExt.maxX);
				pt.y = pt.y <= oldExt.minY ? newExt.minY + (pt.y - oldExt.minY) : newExt.maxY + (pt.y - oldExt.maxY);
			} else {
				pt.x += offsetX;
				pt.y += offsetY;
				if (newExt.contains(pt.x, pt.y)) anyInside = true;
			}
			if (pt.x != trig->getPoint(p)->x || pt.y != trig->getPoint(p)->y) changed = true;
			pts.push_back(pt);
		}
		if (changed) {
			offset->addPolygon(trig, pts);
			if (refit) areasRefit++; else areasMoved++;
		}
		if (!anyInside) areasOutside++;
	}

	// MultipleUndoable runs the step added last first: remove the links and objects that fall off
	// the map, swap the heightmap, then shift what is left. Undo runs the other way round.
	MultipleUndoable *undo = new MultipleUndoable;
	undo->addUndoable(offset);
	undo->addUndoable(new ResizeHeightMapUndoable(doc, copy));
	if (!toDelete.empty()) {
		PointerTool::clearSelection();
		for (size_t i = 0; i < toDelete.size(); i++) toDelete[i]->setSelected(true);
		undo->addUndoable(new DeleteObjectUndoable(doc));
		PointerTool::clearSelection();
	}
	if (!deletedWaypointIds.empty()) {
		McpWaypointLinksUndoable *links = new McpWaypointLinksUndoable(doc);
		for (Int i = 0; i < doc->getNumWaypointLinks(); i++) {
			Int a, b;
			doc->getWaypointLink(i, &a, &b);
			if (deletedWaypointIds.count(a) || deletedWaypointIds.count(b)) links->removed.push_back(std::make_pair(a, b));
		}
		undo->addUndoable(links);
	}
	mcpCommit(undo);
	REF_PTR_RELEASE(copy);
	TerrainMaterial::updateTextures(doc->GetHeightMap());
	mcpResetObjectHandles();

	j.set("changed", true);
	j.set("objects_moved", offsetX != 0 || offsetY != 0 ? objectsMoved : 0);
	j.set("areas_moved", areasMoved).set("areas_refit", areasRefit);
	j.set("removed_objects", removedObjects).set("removed_waypoints", removedWaypoints);
	j.set("removed_road_segments", removedRoadSegments);
	j.set("objects_outside", outsideKept).set("areas_outside", areasOutside);
	return j;
}

} // namespace

void mcpRegisterResizeCommands()
{
	mcpRegisterCommand("map.resize", cmdResize, "{width?, height?, anchor?=center | add_left?, add_right?, add_bottom?, add_top?, border?, fill_height?, fill_texture?, remove_outside?=none|objects|waypoints|all} Grows or crops the map; terrain, objects and areas keep their place.");
}
