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

// McpCmdReplace.cpp
// MCP bridge commands to list what a map uses and to replace textures, object templates and road
// types in bulk, e.g. when converting a map made for another mod. The idea follows the Replace
// command of the Genesis map tools (The CWC Team).

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpUndoables.h"

#include "TerrainMaterial.h"
#include "WHeightMapEdit.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/TerrainRoads.h"

#include <map>
#include <string>
#include <vector>

namespace
{

struct Mapping
{
	std::string from, to;
};

std::string lower(const std::string &s)
{
	std::string out = s;
	for (size_t i = 0; i < out.size(); i++) out[i] = (char)tolower((unsigned char)out[i]);
	return out;
}

/// Reads a {from: to} object and rejects chains (a target that is also a source), whose result
/// would depend on the order the replacements run in.
std::vector<Mapping> parseMapping(const McpJson &args, const char *key)
{
	std::vector<Mapping> out;
	const McpJson &obj = args.get(key);
	if (obj.isNull()) return out;
	if (!obj.isObject()) mcpFail("%s must be an object {from: to, ...}", key);
	for (size_t i = 0; i < obj.numKeys(); i++) {
		if (!obj.valueAt(i).isString()) mcpFail("%s values must be names", key);
		Mapping m;
		m.from = obj.keyAt(i);
		m.to = obj.valueAt(i).asString();
		if (lower(m.from) == lower(m.to)) continue;
		out.push_back(m);
	}
	for (size_t i = 0; i < out.size(); i++) {
		for (size_t k = 0; k < out.size(); k++) {
			if (lower(out[i].to) == lower(out[k].from)) {
				mcpFail("%s: '%s' is both a replacement and a source; split chained or swapped replacements into separate calls",
					key, out[i].to.c_str());
			}
		}
	}
	return out;
}

Int textureClassByName(const std::string &name)
{
	McpJson arg = McpJson::makeObject().set("texture", McpJson(name));
	return mcpFindTextureClass(arg, "texture");
}

bool isRoadPoint(MapObject *obj)
{
	return (obj->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS)) != 0;
}

//-------------------------------------------------------------------------------------------------
// map.usage
//-------------------------------------------------------------------------------------------------

McpJson cmdUsage(const McpJson &)
{
	WorldHeightMapEdit *map = mcpHeightMap();

	std::vector<Int> cells(WorldHeightMapEdit::getNumTexClasses(), 0);
	for (Int y = 0; y < map->getYExtent() - 1; y++) {
		for (Int x = 0; x < map->getXExtent() - 1; x++) {
			const Int cls = map->getTextureClass(x, y, true);
			if (cls >= 0 && cls < (Int)cells.size()) cells[cls]++;
		}
	}
	McpJson textures = McpJson::makeArray();
	for (Int i = 0; i < (Int)cells.size(); i++) {
		if (cells[i] > 0) {
			textures.push(McpJson::makeObject().set("name", WorldHeightMapEdit::getTexClassName(i).str()).set("cells", cells[i]));
		}
	}

	std::map<std::string, Int> objectCounts, roadCounts;
	std::map<std::string, bool> objectKnown;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->isWaypoint() || obj->isLight() || obj->isScorch()) continue;
		const std::string name = obj->getName().str();
		if (isRoadPoint(obj)) {
			roadCounts[name]++;
		} else {
			objectCounts[name]++;
			objectKnown[name] = obj->getThingTemplate() != nullptr;
		}
	}
	McpJson objects = McpJson::makeArray();
	Int unknown = 0;
	for (std::map<std::string, Int>::const_iterator it = objectCounts.begin(); it != objectCounts.end(); ++it) {
		const bool known = objectKnown[it->first];
		if (!known) unknown += it->second;
		objects.push(McpJson::makeObject().set("template", it->first).set("count", it->second).set("known", known));
	}
	McpJson roads = McpJson::makeArray();
	for (std::map<std::string, Int>::const_iterator it = roadCounts.begin(); it != roadCounts.end(); ++it) {
		const AsciiString name(it->first.c_str());
		const bool known = TheTerrainRoads->findRoad(name) != nullptr || TheTerrainRoads->findBridge(name) != nullptr;
		roads.push(McpJson::makeObject().set("type", it->first).set("points", it->second).set("known", known));
	}
	McpJson j = McpJson::makeObject();
	j.set("textures", textures).set("objects", objects).set("roads", roads);
	j.set("objects_with_unknown_template", unknown);
	return j;
}

//-------------------------------------------------------------------------------------------------
// map.replace
//-------------------------------------------------------------------------------------------------

/// Replaces one texture everywhere in the (copied) map. Returns the number of cells changed.
Int replaceTexture(WorldHeightMapEdit *copy, Int from, Int to, bool dryRun)
{
	Int count = 0, seedX = -1, seedY = -1, seed2X = -1, seed2Y = -1;
	for (Int y = 0; y < copy->getYExtent() - 1; y++) {
		for (Int x = 0; x < copy->getXExtent() - 1; x++) {
			if (copy->getTextureClass(x, y, true) != from) continue;
			if (count == 0) { seedX = x; seedY = y; }
			else if (count == 1) { seed2X = x; seed2Y = y; }
			count++;
		}
	}
	if (count == 0 || dryRun) return count;

	// floodFill in replace mode swaps the texture in place when the new one is not in the map yet,
	// which only works if it needs no more tiles. Allocating it first (when it fits) avoids that limit.
	if (!copy->isTexClassUsed(to) && copy->canFitTexture(to)) {
		copy->setTileNdx(seedX, seedY, to, false);
		if (count == 1) return count;
		seedX = seed2X;
		seedY = seed2Y;
	}
	if (!copy->floodFill(seedX, seedY, to, true, false)) {
		return -1;
	}
	return count;
}

McpJson cmdReplace(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	const bool dryRun = mcpArgBool(args, "dry_run", false);
	const std::vector<Mapping> textures = parseMapping(args, "textures");
	const std::vector<Mapping> objects = parseMapping(args, "objects");
	const std::vector<Mapping> roads = parseMapping(args, "roads");
	if (textures.empty() && objects.empty() && roads.empty()) {
		mcpFail("pass textures, objects and/or roads as {from: to} mappings");
	}

	// Validate every target before changing anything.
	std::vector<std::pair<Int, Int> > textureClasses;
	for (size_t i = 0; i < textures.size(); i++) {
		textureClasses.push_back(std::make_pair(textureClassByName(textures[i].from), textureClassByName(textures[i].to)));
	}
	std::vector<const ThingTemplate *> objectTargets;
	for (size_t i = 0; i < objects.size(); i++) {
		const ThingTemplate *tt = TheThingFactory->findTemplate(AsciiString(objects[i].to.c_str()), FALSE);
		if (tt == nullptr) mcpFail("unknown object template '%s' (see objects.list_templates)", objects[i].to.c_str());
		objectTargets.push_back(tt);
	}
	for (size_t i = 0; i < roads.size(); i++) {
		const AsciiString name(roads[i].to.c_str());
		if (TheTerrainRoads->findRoad(name) == nullptr && TheTerrainRoads->findBridge(name) == nullptr) {
			mcpFail("unknown road or bridge type '%s' (see roads.list_types)", roads[i].to.c_str());
		}
	}

	McpJson result = McpJson::makeObject();
	MultipleUndoable *undo = new MultipleUndoable;
	bool changed = false;

	// Objects and roads.
	McpModifyObjectsUndoable *modify = new McpModifyObjectsUndoable(doc);
	std::vector<Int> objectCounts(objects.size(), 0), roadCounts(roads.size(), 0);
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (obj->isWaypoint() || obj->isLight() || obj->isScorch()) continue;
		const std::string name = lower(obj->getName().str());
		if (isRoadPoint(obj)) {
			for (size_t i = 0; i < roads.size(); i++) {
				if (name != lower(roads[i].from)) continue;
				const AsciiString target(roads[i].to.c_str());
				// A road point must stay a road and a bridge point a bridge.
				const bool targetIsBridge = TheTerrainRoads->findRoad(target) == nullptr;
				if (targetIsBridge != ((obj->getFlags() & FLAG_BRIDGE_FLAGS) != 0)) continue;
				roadCounts[i]++;
				if (!dryRun) {
					MoveInfo *info = modify->add(obj);
					info->m_oldName = obj->getName();
					info->m_newName = target;
				}
			}
		} else {
			for (size_t i = 0; i < objects.size(); i++) {
				if (name != lower(objects[i].from)) continue;
				objectCounts[i]++;
				if (!dryRun) {
					MoveInfo *info = modify->add(obj);
					info->m_oldName = obj->getName();
					info->m_newName = objectTargets[i]->getName();
					info->m_newThing = objectTargets[i];
					modify->setTemplateChanged();
				}
			}
		}
	}
	McpJson objectResult = McpJson::makeArray(), roadResult = McpJson::makeArray();
	Int totalObjects = 0;
	for (size_t i = 0; i < objects.size(); i++) {
		objectResult.push(McpJson::makeObject().set("from", objects[i].from).set("to", objects[i].to).set("count", objectCounts[i]));
		totalObjects += objectCounts[i];
	}
	for (size_t i = 0; i < roads.size(); i++) {
		roadResult.push(McpJson::makeObject().set("from", roads[i].from).set("to", roads[i].to).set("points", roadCounts[i]));
		totalObjects += roadCounts[i];
	}
	if (totalObjects > 0 && !dryRun) {
		undo->addUndoable(modify);
		changed = true;
	} else {
		REF_PTR_RELEASE(modify);
	}

	// Textures.
	McpJson textureResult = McpJson::makeArray();
	WorldHeightMapEdit *copy = textures.empty() ? nullptr : map->duplicate();
	Int totalCells = 0;
	for (size_t i = 0; i < textures.size(); i++) {
		const Int cells = replaceTexture(copy, textureClasses[i].first, textureClasses[i].second, dryRun);
		if (cells < 0) {
			REF_PTR_RELEASE(copy);
			REF_PTR_RELEASE(undo);
			mcpFail("cannot replace texture '%s' with '%s': the map has no room for the new texture and it needs more tiles than the old one",
				textures[i].from.c_str(), textures[i].to.c_str());
		}
		textureResult.push(McpJson::makeObject().set("from", textures[i].from).set("to", textures[i].to).set("cells", cells));
		totalCells += cells;
	}
	if (copy && totalCells > 0 && !dryRun) {
		copy->optimizeTiles();
		IRegion2D all = { 0, 0, 0, 0 };
		doc->updateHeightMap(copy, false, all);
		undo->addUndoable(new WBDocUndoable(doc, copy));
		changed = true;
	}

	if (changed) {
		mcpCommit(undo);
	} else {
		REF_PTR_RELEASE(undo);
	}
	if (copy) REF_PTR_RELEASE(copy);
	if (changed && totalCells > 0) {
		TerrainMaterial::updateTextures(doc->GetHeightMap());
	}

	result.set("dry_run", dryRun).set("changed", changed);
	result.set("textures", textureResult).set("objects", objectResult).set("roads", roadResult);
	return result;
}

} // namespace

void mcpRegisterReplaceCommands()
{
	mcpRegisterCommand("map.usage", cmdUsage, "Lists the textures (with cell counts), object templates and road types the map uses, flagging unknown ones.");
	mcpRegisterCommand("map.replace", cmdReplace, "{textures?:{from:to}, objects?:{from:to}, roads?:{from:to}, dry_run?} Replaces textures, object templates and road types everywhere.");
}
