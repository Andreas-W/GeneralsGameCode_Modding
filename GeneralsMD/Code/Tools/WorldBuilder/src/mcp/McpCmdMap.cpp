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
#include "Common/MapData.h"
#include "Common/MapObject.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameClient/PostProcess.h"
#include "W3DDevice/GameClient/W3DMaterials.h"
#include "W3DDevice/GameClient/W3DPostProcess.h"
#include "W3DDevice/GameClient/W3DShadowMap.h"

#include <math.h>
#include <string>
#include <vector>

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
	// EnableShips from the map.ini next to the map: whether the AI is to use ships here (SKIRMISH_SHIPS_ENABLED).
	j.set("ships_enabled", (bool)(TheMapData->m_enableShips != 0));

	McpJson grid = McpJson::makeObject();
	grid.set("x_extent", xExtent).set("y_extent", yExtent).set("border", border);
	grid.set("playable_cells_x", xExtent - 1 - 2 * border).set("playable_cells_y", yExtent - 1 - 2 * border);
	// HeightMapScale from the map.ini next to the map: raw heights are multiples of it, up to 255 times it.
	grid.set("height_map_scale", TheMapData->m_HeightmapScale).set("height_raw_limit", WorldHeightMap::getMaxHeightValue());
	j.set("heightmap", grid);

	McpJson world = McpJson::makeObject();
	world.set("min_x", -border * MAP_XY_FACTOR).set("min_y", -border * MAP_XY_FACTOR);
	world.set("max_x", (xExtent - 1 - border) * MAP_XY_FACTOR).set("max_y", (yExtent - 1 - border) * MAP_XY_FACTOR);
	world.set("playable_max_x", (xExtent - 1 - 2 * border) * MAP_XY_FACTOR);
	world.set("playable_max_y", (yExtent - 1 - 2 * border) * MAP_XY_FACTOR);
	world.set("cell_size", MAP_XY_FACTOR).set("height_scale", MAP_HEIGHT_SCALE);
	j.set("world", world);

	Int minH = WorldHeightMap::getMaxHeightValue(), maxH = 0;
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
	// MFC only activates a document that is already open under the same path. Forget the path so
	// the map (and its map.ini) is really read again.
	CWorldBuilderDoc *current = CWorldBuilderDoc::GetActiveDoc();
	if (current != nullptr && current->GetPathName().CompareNoCase(path.c_str()) == 0) {
		current->forgetPathName();
	}
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

/// Sets a field of the MapData block in the map.ini next to the map, keeping the rest of the file.
void setMapIniField(const char *key, const char *value)
{
	CString mapPath = mcpDoc()->GetPathName();
	const int slash = mapPath.ReverseFind('\\');
	if (mapPath.IsEmpty() || slash < 0) {
		mcpFail("save the map first: %s is kept in the map.ini next to it", key);
	}
	const std::string iniPath = std::string((LPCTSTR)mapPath.Left(slash + 1)) + "map.ini";

	std::vector<std::string> lines;
	if (FILE *in = fopen(iniPath.c_str(), "r")) {
		char buffer[1024];
		while (fgets(buffer, sizeof(buffer), in)) {
			std::string line = buffer;
			while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r')) line.erase(line.size() - 1);
			lines.push_back(line);
		}
		fclose(in);
	}

	// Find the MapData block and the field in it.
	const std::string newLine = std::string("  ") + key + " = " + value;
	int blockStart = -1, blockEnd = -1, field = -1;
	for (size_t i = 0; i < lines.size(); i++) {
		std::string code = lines[i].substr(0, lines[i].find(';'));
		const size_t first = code.find_first_not_of(" \t");
		code = first == std::string::npos ? "" : code.substr(first);
		while (!code.empty() && (code[code.size() - 1] == ' ' || code[code.size() - 1] == '\t')) code.erase(code.size() - 1);
		if (blockStart < 0) {
			if (_stricmp(code.c_str(), "MapData") == 0) blockStart = (int)i;
		} else if (_stricmp(code.c_str(), "End") == 0) {
			blockEnd = (int)i;
			break;
		} else if (_strnicmp(code.c_str(), key, strlen(key)) == 0 && code.find('=') != std::string::npos) {
			field = (int)i;
		}
	}
	if (field >= 0) {
		lines[field] = newLine;
	} else if (blockStart >= 0 && blockEnd >= 0) {
		lines.insert(lines.begin() + blockEnd, newLine);
	} else {
		lines.push_back("MapData");
		lines.push_back(newLine);
		lines.push_back("End");
	}

	FILE *out = fopen(iniPath.c_str(), "w");
	if (out == nullptr) {
		mcpFail("cannot write %s", iniPath.c_str());
	}
	for (size_t i = 0; i < lines.size(); i++) {
		fprintf(out, "%s\n", lines[i].c_str());
	}
	fclose(out);
}

McpJson cmdMapSetInfo(const McpJson &args)
{
	mcpDoc();
	if (args.has("ships_enabled")) {
		const bool enable = mcpArgBool(args, "ships_enabled", false);
		setMapIniField("EnableShips", enable ? "Yes" : "No");
		TheWriteableMapData->m_enableShips = enable;
	}
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

/// Ground point at the center of the screen. WorldBuilder applies pitch by tilting the view ray away
/// from the camera target (see WbView3d::setupCamera), so for pitch != 1 the screen center lies at
/// target + (source - target) * (1 - 1/pitch) rather than at the target itself.
void getLookAtPoint(WbView3d *view, Real *x, Real *y)
{
	const Vector3 source = view->getCameraSource();
	const Vector3 target = view->getCameraTarget();
	const Real k = 1.0f - 1.0f / view->getCameraPitch();
	*x = target.X + (source.X - target.X) * k;
	*y = target.Y + (source.Y - target.Y) * k;
}

McpJson describeCamera(WbView3d *view)
{
	McpJson j = McpJson::makeObject();
	j.set("angle_deg", view->getCameraAngle() * 180.0 / PI);
	j.set("pitch", view->getCameraPitch());
	j.set("zoom", view->getZoomOffset());
	Real lookX, lookY;
	getLookAtPoint(view, &lookX, &lookY);
	j.set("x", lookX).set("y", lookY);
	return j;
}

void applyCamera(WbView3d *view, const McpJson &args)
{
	// Validate before changing anything so a bad argument leaves the camera untouched.
	const Real pitch = (Real)mcpArgNumber(args, "pitch", 1.0);
	if (pitch <= 0) {
		mcpFail("pitch must be > 0 (1.0 is the default tilt, larger looks more top-down)");
	}
	Real lookX, lookY;
	getLookAtPoint(view, &lookX, &lookY);
	const Real x = (Real)mcpArgNumber(args, "x", lookX);
	const Real y = (Real)mcpArgNumber(args, "y", lookY);

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
		view->setCameraPitch(pitch);
	}
	// Keep the requested (or previous) point in the middle of the screen. The camera offset depends a
	// little on the ground height under the new center, so refine it a few times.
	for (Int i = 0; i < 3; i++) {
		view->redraw();
		const Vector3 source = view->getCameraSource();
		const Vector3 target = view->getCameraTarget();
		const Real k = 1.0f - 1.0f / view->getCameraPitch();
		const Real centerX = x - (source.X - target.X) * k;
		const Real centerY = y - (source.Y - target.Y) * k;
		// The view center is kept in cell units.
		view->setCenterInView(centerX / MAP_XY_FACTOR, centerY / MAP_XY_FACTOR);
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


/// The Direct3D 12 renderer's effects: on or off, and whether they run.
static McpJson cmdRender(const McpJson &args)
{
	if (mcpArgBool(args, "reload", false)) {
		if (WbView3d *view = CWorldBuilderDoc::GetActive3DView()) {
			view->reloadRendering();
		}
	}
	if (args.has("post_processing")) {
		TheWritableGlobalData->m_postProcessing = mcpArgBool(args, "post_processing", true);
	}
	if (args.has("materials") && TheW3DMaterials != nullptr) {
		TheW3DMaterials->setEnabled(mcpArgBool(args, "materials", true));
	}
	if (args.has("shadow_maps") && TheShadowMapSettings != nullptr) {
		TheShadowMapSettings->m_enabled = mcpArgBool(args, "shadow_maps", true);
	}
	if (args.has("shadow_debug") && TheW3DShadowMap != nullptr) {
		TheW3DShadowMap->setDebug(mcpArgInt(args, "shadow_debug", 0));
	}
	if (WbView3d *view = CWorldBuilderDoc::GetActive3DView()) {
		view->redraw();
	}
	McpJson result = McpJson::makeObject()
		.set("dx12", TheW3DPostProcess != nullptr && TheW3DPostProcess->isAvailable())
		.set("post_processing", TheGlobalData->m_postProcessing != 0)
		.set("post_processing_running", TheW3DPostProcess != nullptr && TheW3DPostProcess->isRunning())
		.set("post_processing_passes", TheW3DPostProcess != nullptr ? (int)TheW3DPostProcess->getPasses().size() : 0)
		.set("materials_running", TheW3DMaterials != nullptr && TheW3DMaterials->isRunning())
		.set("shadow_maps", TheW3DShadowMap != nullptr && TheW3DShadowMap->isActive())
		.set("shadow_maps_drawn", TheW3DShadowMap != nullptr && TheW3DShadowMap->wasRendered());
	if (TheW3DPostProcess != nullptr && TheW3DPostProcess->getError().isNotEmpty()) {
		result.set("post_processing_error", TheW3DPostProcess->getError().str());
	}
	if (TheW3DMaterials != nullptr && TheW3DMaterials->getError().isNotEmpty()) {
		result.set("materials_error", TheW3DMaterials->getError().str());
	}
	return result;
}

void mcpRegisterMapCommands()
{
	mcpRegisterCommand("map.info", cmdMapInfo, "Map size, borders, world extents, counts, name, weather.");
	mcpRegisterCommand("map.new", cmdMapNew, "{width,height,initial_height,border,discard_changes} Creates a new map (sizes in cells, excluding border).");
	mcpRegisterCommand("map.open", cmdMapOpen, "{path,discard_changes} Opens a .map file.");
	mcpRegisterCommand("map.save", cmdMapSave, "{path?} Saves the map; path required for untitled maps.");
	mcpRegisterCommand("map.set_info", cmdMapSetInfo, "{name?,time_of_day?,weather?,ships_enabled?} Changes map settings. ships_enabled writes EnableShips to the map.ini next to the saved map.");
	mcpRegisterCommand("edit.undo", cmdUndo, "{count?} Undoes the last edits.");
	mcpRegisterCommand("edit.redo", cmdRedo, "{count?} Redoes undone edits.");
	mcpRegisterCommand("view.set_camera", cmdSetCamera, "{x?,y?,angle_deg?,pitch?,zoom?,reset?} Moves the 3D camera. pitch: 1 default, larger = more top-down. zoom: wheel offset, negative zooms out.");
	mcpRegisterCommand("view.render", cmdRender, "{post_processing?,materials?,shadow_maps?,shadow_debug?,reload?} Turns the Direct3D 12 renderer's post-processing, materials and shadow maps on or off (start WorldBuilder with -dx12) and returns their state; shadow_debug 1 shows the amount of shadow as grey, 2 also the detail cascade; reload reads the INI files, shaders and the map's map.ini again.");
	mcpRegisterCommand("view.screenshot", cmdScreenshot, "{path?,format?:jpg|png,max_width?,quality?,x?,y?,angle_deg?,pitch?,zoom?,reset?} Renders the 3D view to an image.");
}
