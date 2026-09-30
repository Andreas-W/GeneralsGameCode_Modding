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

// McpCmdMap.cpp
// MCP bridge commands for map management, undo and the 3D view.

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpScreenshot.h"

#include "resource.h"
#include "WHeightMapEdit.h"
#include "WorldBuilder.h"
#include "WorldBuilderDoc.h"
#include "wbview3d.h"
#include "Common/GameType.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"

#include <math.h>

namespace
{

McpJson cmdMapInfo(const McpJson &)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int border = map->getBorderSize();
	const Int xExtent = map->getXExtent();
	const Int yExtent = map->getYExtent();

	McpJson j = McpJson::makeObject();
	j.set("path", (LPCTSTR)doc->GetPathName());
	j.set("title", (LPCTSTR)doc->GetTitle());
	j.set("modified", doc->IsModified() != 0);
	j.set("map_name", MapObject::getWorldDict()->getAsciiString(TheKey_mapName).str());

	McpJson grid = McpJson::makeObject();
	grid.set("x_extent", xExtent).set("y_extent", yExtent).set("border", border);
	grid.set("playable_cells_x", xExtent - 1 - 2 * border).set("playable_cells_y", yExtent - 1 - 2 * border);
	j.set("heightmap", grid);

	McpJson world = McpJson::makeObject();
	world.set("min_x", -border * MAP_XY_FACTOR).set("min_y", -border * MAP_XY_FACTOR);
	world.set("max_x", (xExtent - 1 - border) * MAP_XY_FACTOR).set("max_y", (yExtent - 1 - border) * MAP_XY_FACTOR);
	world.set("playable_max_x", (xExtent - 1 - 2 * border) * MAP_XY_FACTOR);
	world.set("playable_max_y", (yExtent - 1 - 2 * border) * MAP_XY_FACTOR);
	world.set("cell_size", MAP_XY_FACTOR).set("height_scale", MAP_HEIGHT_SCALE);
	j.set("world", world);

	Int minH = 255, maxH = 0;
	for (Int y = 0; y < yExtent; y++) {
		for (Int x = 0; x < xExtent; x++) {
			Int h = map->getHeight(x, y);
			if (h < minH) minH = h;
			if (h > maxH) maxH = h;
		}
	}
	j.set("height_raw_min", minH).set("height_raw_max", maxH);

	Int numObjects = 0, numWaypoints = 0, numRoadPoints = 0;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->isWaypoint()) numWaypoints++;
		else if (obj->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS)) numRoadPoints++;
		else numObjects++;
	}
	Int numPolygons = 0, numWater = 0;
	for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
		numPolygons++;
		if (trig->isWaterArea()) numWater++;
	}
	McpJson counts = McpJson::makeObject();
	counts.set("objects", numObjects).set("waypoints", numWaypoints).set("road_points", numRoadPoints);
	counts.set("polygons", numPolygons).set("water_areas", numWater);
	j.set("counts", counts);

	McpJson boundaries = McpJson::makeArray();
	for (Int i = 0; i < doc->getNumBoundaries(); i++) {
		ICoord2D b;
		doc->getBoundary(i, &b);
		boundaries.push(McpJson::makeObject().set("width_cells", b.x).set("height_cells", b.y));
	}
	j.set("boundaries", boundaries);

	Int tod = TheGlobalData->m_timeOfDay;
	j.set("time_of_day", (tod >= TIME_OF_DAY_FIRST && tod < TIME_OF_DAY_COUNT) ? TimeOfDayNames[tod] : "INVALID");
	Int weather = TheGlobalData->m_weather;
	j.set("weather", (weather >= 0 && weather < WEATHER_COUNT) ? WeatherNames[weather] : "INVALID");
	return j;
}

void requireUnmodifiedOrDiscard(const McpJson &args)
{
	CWorldBuilderDoc *doc = CWorldBuilderDoc::GetActiveDoc();
	if (doc == nullptr || !doc->IsModified()) {
		return;
	}
	if (!mcpArgBool(args, "discard_changes", false)) {
		mcpFail("the open map has unsaved changes; save it first or pass discard_changes=true");
	}
	doc->SetModifiedFlag(FALSE);
}

McpJson cmdMapNew(const McpJson &args)
{
	const Int width = mcpArgInt(args, "width", 128);
	const Int height = mcpArgInt(args, "height", 128);
	const Int initialHeight = mcpArgInt(args, "initial_height", 16);
	const Int border = mcpArgInt(args, "border", 30);
	if (width < 8 || height < 8 || width > 1024 || height > 1024) {
		mcpFail("width and height must be between 8 and 1024 cells");
	}
	if (initialHeight < 0 || initialHeight > 255) {
		mcpFail("initial_height must be 0..255");
	}
	if (border < 0 || border > 100) {
		mcpFail("border must be 0..100");
	}
	requireUnmodifiedOrDiscard(args);

	// WorldHeightMapEdit adds the border on both sides itself.
	CWorldBuilderDoc::setNewMapOverride(width, height, initialHeight, border);
	AfxGetMainWnd()->SendMessage(WM_COMMAND, ID_FILE_NEW);
	mcpResetObjectHandles();
	return cmdMapInfo(args);
}

McpJson cmdMapOpen(const McpJson &args)
{
	std::string path = mcpArgString(args, "path");
	if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		mcpFail("file not found: %s", path.c_str());
	}
	requireUnmodifiedOrDiscard(args);
	CDocument *opened = WbApp()->OpenDocumentFile(path.c_str());
	mcpResetObjectHandles();
	if (opened == nullptr) {
		mcpFail("WorldBuilder could not open %s", path.c_str());
	}
	return cmdMapInfo(args);
}

McpJson cmdMapSave(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	CString path = mcpArgString(args, "path", "").c_str();
	if (path.IsEmpty()) {
		path = doc->GetPathName();
	}
	if (path.IsEmpty()) {
		mcpFail("the map has never been saved; pass a full .map path");
	}
	if (path.Right(4).CompareNoCase(".map") != 0) {
		mcpFail("path must end in .map");
	}
	if (!doc->DoSave(path, TRUE)) {
		mcpFail("saving to %s failed", (LPCTSTR)path);
	}
	return McpJson::makeObject().set("path", (LPCTSTR)doc->GetPathName());
}

McpJson cmdMapSetInfo(const McpJson &args)
{
	mcpDoc();
	if (args.has("name")) {
		MapObject::getWorldDict()->setAsciiString(TheKey_mapName, AsciiString(mcpArgString(args, "name").c_str()));
	}
	if (args.has("time_of_day")) {
		std::string tod = mcpArgString(args, "time_of_day");
		Int found = -1;
		for (Int i = TIME_OF_DAY_FIRST; i < TIME_OF_DAY_COUNT; i++) {
			if (_stricmp(tod.c_str(), TimeOfDayNames[i]) == 0) found = i;
		}
		if (found < 0) {
			mcpFail("time_of_day must be MORNING, AFTERNOON, EVENING or NIGHT");
		}
		TheWritableGlobalData->setTimeOfDay((TimeOfDay)found);
	}
	if (args.has("weather")) {
		std::string weather = mcpArgString(args, "weather");
		Int found = -1;
		for (Int i = 0; i < WEATHER_COUNT; i++) {
			if (_stricmp(weather.c_str(), WeatherNames[i]) == 0) found = i;
		}
		if (found < 0) {
			mcpFail("weather must be NORMAL or SNOWY");
		}
		TheWritableGlobalData->m_weather = (Weather)found;
	}
	mcpDoc()->SetModifiedFlag();
	WbView3d *view = CWorldBuilderDoc::GetActive3DView();
	if (view) {
		view->resetRenderObjects();
		view->invalObjectInView(nullptr);
	}
	return cmdMapInfo(args);
}

McpJson cmdUndoRedo(const McpJson &args, UINT commandId)
{
	mcpDoc();
	const Int count = mcpArgInt(args, "count", 1);
	if (count < 1 || count > MAX_UNDOS) {
		mcpFail("count must be 1..%d", MAX_UNDOS);
	}
	for (Int i = 0; i < count; i++) {
		AfxGetMainWnd()->SendMessage(WM_COMMAND, commandId);
	}
	return McpJson::makeObject().set("steps", count);
}

McpJson cmdUndo(const McpJson &args)
{
	return cmdUndoRedo(args, ID_EDIT_UNDO);
}

McpJson cmdRedo(const McpJson &args)
{
	return cmdUndoRedo(args, ID_EDIT_REDO);
}

WbView3d *require3DView()
{
	mcpDoc();
	WbView3d *view = CWorldBuilderDoc::GetActive3DView();
	if (view == nullptr) {
		mcpFail("no 3D view is open");
	}
	return view;
}

McpJson describeCamera(WbView3d *view)
{
	McpJson j = McpJson::makeObject();
	j.set("angle_deg", view->getCameraAngle() * 180.0 / PI);
	j.set("pitch", view->getCameraPitch());
	j.set("zoom", view->getZoomOffset());
	Vector3 target = view->getCameraTarget();
	j.set("target_x", target.X).set("target_y", target.Y).set("target_z", target.Z);
	return j;
}

void applyCamera(WbView3d *view, const McpJson &args)
{
	if (mcpArgBool(args, "reset", false)) {
		view->setDefaultCamera();
	}
	if (args.has("angle_deg")) {
		view->setCameraAngle((Real)(mcpArgNumber(args, "angle_deg") * PI / 180.0));
	}
	if (args.has("zoom")) {
		view->setZoomOffset((Real)mcpArgNumber(args, "zoom"));
	}
	if (args.has("pitch")) {
		view->setCameraPitch((Real)mcpArgNumber(args, "pitch"));
	}
	if (args.has("x") || args.has("y")) {
		Vector3 target = view->getCameraTarget();
		Real x = (Real)mcpArgNumber(args, "x", target.X);
		Real y = (Real)mcpArgNumber(args, "y", target.Y);
		// The view center is kept in cell units.
		view->setCenterInView(x / MAP_XY_FACTOR, y / MAP_XY_FACTOR);
	}
	view->redraw();
}

McpJson cmdSetCamera(const McpJson &args)
{
	WbView3d *view = require3DView();
	applyCamera(view, args);
	return describeCamera(view);
}

McpJson cmdScreenshot(const McpJson &args)
{
	WbView3d *view = require3DView();
	applyCamera(view, args);

	const std::string format = mcpArgString(args, "format", "jpg");
	if (format != "jpg" && format != "png") {
		mcpFail("format must be jpg or png");
	}
	const Int maxWidth = mcpArgInt(args, "max_width", 1280);
	const Int quality = mcpArgInt(args, "quality", 85);
	if (quality < 1 || quality > 100) {
		mcpFail("quality must be 1..100");
	}

	std::string path = mcpArgString(args, "path", "");
	if (path.empty()) {
		static Int s_shotCounter = 0;
		char tempDir[MAX_PATH];
		::GetTempPath(MAX_PATH, tempDir);
		char name[MAX_PATH];
		sprintf(name, "%swb_mcp_%u_%d.%s", tempDir, (unsigned)::GetCurrentProcessId(), ++s_shotCounter, format.c_str());
		path = name;
	}
	mcpSetScreenshotOptions(maxWidth, format == "jpg" ? quality : 0);
	Int width = 0, height = 0;
	if (!view->captureToFile(path.c_str(), &width, &height)) {
		mcpFail("could not capture the 3D view (is the WorldBuilder window visible and not minimized?)");
	}
	McpJson j = describeCamera(view);
	j.set("path", path).set("format", format).set("width", width).set("height", height);
	return j;
}

} // namespace

void mcpRegisterMapCommands()
{
	mcpRegisterCommand("map.info", cmdMapInfo, "Map size, borders, world extents, counts, name, weather.");
	mcpRegisterCommand("map.new", cmdMapNew, "{width,height,initial_height,border,discard_changes} Creates a new map (sizes in cells, excluding border).");
	mcpRegisterCommand("map.open", cmdMapOpen, "{path,discard_changes} Opens a .map file.");
	mcpRegisterCommand("map.save", cmdMapSave, "{path?} Saves the map; path required for untitled maps.");
	mcpRegisterCommand("map.set_info", cmdMapSetInfo, "{name?,time_of_day?,weather?} Changes map settings.");
	mcpRegisterCommand("edit.undo", cmdUndo, "{count?} Undoes the last edits.");
	mcpRegisterCommand("edit.redo", cmdRedo, "{count?} Redoes undone edits.");
	mcpRegisterCommand("view.set_camera", cmdSetCamera, "{x?,y?,angle_deg?,pitch?,zoom?,reset?} Moves the 3D camera.");
	mcpRegisterCommand("view.screenshot", cmdScreenshot, "{path?,format?:jpg|png,max_width?,quality?,x?,y?,angle_deg?,pitch?,zoom?,reset?} Renders the 3D view to an image.");
}
