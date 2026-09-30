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

// McpCmdTerrain.cpp
// MCP bridge commands for terrain heights, textures and passability.
//
// Every edit follows the pattern of the interactive tools: duplicate the heightmap, modify the
// copy, refresh the views from it, then commit it with a WBDocUndoable so it can be undone.

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "CUndoable.h"
#include "TerrainMaterial.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"

#include <math.h>
#include <vector>

namespace
{

const Int MAX_HEIGHT_VALUES = 256 * 256;

/// Tracks the touched vertex range of an edit so only that part of the views is refreshed.
struct EditRange
{
	EditRange() : minX(0x7fffffff), minY(0x7fffffff), maxX(-1), maxY(-1), fullUpdate(false) {}

	void add(Int x, Int y)
	{
		if (x < minX) minX = x;
		if (y < minY) minY = y;
		if (x > maxX) maxX = x;
		if (y > maxY) maxY = y;
	}
	bool empty() const { return maxX < 0; }

	Int minX, minY, maxX, maxY;
	bool fullUpdate;
};

/// Refreshes the views from the edited copy and commits it as one undo step.
McpJson commitHeightMapEdit(WorldHeightMapEdit *copy, const EditRange &range)
{
	CWorldBuilderDoc *doc = mcpDoc();
	McpJson result = McpJson::makeObject();
	if (range.empty()) {
		REF_PTR_RELEASE(copy);
		result.set("changed", false);
		return result;
	}
	if (range.fullUpdate) {
		copy->optimizeTiles();
	}
	IRegion2D partialRange;
	partialRange.lo.x = range.minX;
	partialRange.lo.y = range.minY;
	partialRange.hi.x = range.maxX + 1;
	partialRange.hi.y = range.maxY + 1;
	doc->updateHeightMap(copy, !range.fullUpdate, partialRange);
	mcpCommit(new WBDocUndoable(doc, copy));
	REF_PTR_RELEASE(copy);
	if (range.fullUpdate) {
		TerrainMaterial::updateTextures(doc->GetHeightMap());
	}
	result.set("changed", true);
	result.set("index_min_x", range.minX).set("index_min_y", range.minY);
	result.set("index_max_x", range.maxX).set("index_max_y", range.maxY);
	return result;
}

UnsignedByte clampHeight(double h)
{
	if (h < 0) return 0;
	if (h > 255) return 255;
	return (UnsignedByte)floor(h + 0.5);
}

Int findTextureClass(const McpJson &args, const char *key)
{
	const McpJson &v = args.get(key);
	if (v.isNumber()) {
		Int ndx = (Int)v.asNumber();
		if (ndx < 0 || ndx >= WorldHeightMapEdit::getNumTexClasses()) {
			mcpFail("texture index %d out of range", ndx);
		}
		return ndx;
	}
	if (!v.isString()) {
		mcpFail("argument '%s' must be a texture name or index (see terrain.list_textures)", key);
	}
	for (Int i = 0; i < WorldHeightMapEdit::getNumTexClasses(); i++) {
		if (_stricmp(WorldHeightMapEdit::getTexClassName(i).str(), v.asString().c_str()) == 0) {
			return i;
		}
	}
	for (Int i = 0; i < WorldHeightMapEdit::getNumTexClasses(); i++) {
		if (_stricmp(WorldHeightMapEdit::getTexClassUiName(i).str(), v.asString().c_str()) == 0) {
			return i;
		}
	}
	mcpFail("unknown texture '%s' (see terrain.list_textures)", v.asString().c_str());
	return -1;
}

/// A world-space shape for area operations. Circles fall off linearly over 'feather'.
struct Shape
{
	bool circle;
	double cx, cy, radius, feather;
	double x0, y0, x1, y1;

	void parse(const McpJson &args, bool allowFeather)
	{
		std::string kind = mcpArgString(args, "shape", args.has("radius") ? "circle" : "rect");
		feather = allowFeather ? mcpArgNumber(args, "feather", 0) : 0;
		if (feather < 0) feather = 0;
		if (kind == "circle") {
			circle = true;
			cx = mcpArgNumber(args, "x");
			cy = mcpArgNumber(args, "y");
			radius = mcpArgNumber(args, "radius");
			if (radius < 0) mcpFail("radius must be >= 0");
			x0 = cx - radius - feather;
			y0 = cy - radius - feather;
			x1 = cx + radius + feather;
			y1 = cy + radius + feather;
		} else if (kind == "rect") {
			circle = false;
			x0 = mcpArgNumber(args, "x0");
			y0 = mcpArgNumber(args, "y0");
			x1 = mcpArgNumber(args, "x1");
			y1 = mcpArgNumber(args, "y1");
			if (x1 < x0) { double t = x0; x0 = x1; x1 = t; }
			if (y1 < y0) { double t = y0; y0 = y1; y1 = t; }
			cx = (x0 + x1) / 2;
			cy = (y0 + y1) / 2;
			radius = 0;
			x0 -= feather; y0 -= feather; x1 += feather; y1 += feather;
		} else {
			mcpFail("shape must be 'circle' (x,y,radius) or 'rect' (x0,y0,x1,y1)");
		}
	}

	/// 1 inside the shape, falling to 0 across the feather band, 0 outside.
	double weight(double wx, double wy) const
	{
		double dist;
		if (circle) {
			double dx = wx - cx, dy = wy - cy;
			dist = sqrt(dx * dx + dy * dy) - radius;
		} else {
			double dx = 0, dy = 0;
			if (wx < x0 + feather) dx = x0 + feather - wx;
			if (wx > x1 - feather) dx = wx - (x1 - feather);
			if (wy < y0 + feather) dy = y0 + feather - wy;
			if (wy > y1 - feather) dy = wy - (y1 - feather);
			dist = sqrt(dx * dx + dy * dy);
		}
		const double CLOSE_ENOUGH = 0.01;
		if (dist <= CLOSE_ENOUGH) return 1.0;
		if (feather <= 0 || dist >= feather) return 0.0;
		return (feather - dist) / feather;
	}

	void indexRange(WorldHeightMapEdit *map, Int &ix0, Int &iy0, Int &ix1, Int &iy1) const
	{
		ix0 = (Int)floor(x0 / MAP_XY_FACTOR) + map->getBorderSize();
		iy0 = (Int)floor(y0 / MAP_XY_FACTOR) + map->getBorderSize();
		ix1 = (Int)ceil(x1 / MAP_XY_FACTOR) + map->getBorderSize();
		iy1 = (Int)ceil(y1 / MAP_XY_FACTOR) + map->getBorderSize();
		if (ix0 < 0) ix0 = 0;
		if (iy0 < 0) iy0 = 0;
		if (ix1 > map->getXExtent() - 1) ix1 = map->getXExtent() - 1;
		if (iy1 > map->getYExtent() - 1) iy1 = map->getYExtent() - 1;
	}
};

//-------------------------------------------------------------------------------------------------
// Heights
//-------------------------------------------------------------------------------------------------

McpJson cmdGetHeights(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int step = mcpArgInt(args, "step", 1);
	const Int x0 = mcpArgInt(args, "x0", 0);
	const Int y0 = mcpArgInt(args, "y0", 0);
	const Int w = mcpArgInt(args, "w", map->getXExtent() - x0);
	const Int h = mcpArgInt(args, "h", map->getYExtent() - y0);
	if (step < 1) mcpFail("step must be >= 1");
	if (x0 < 0 || y0 < 0 || w < 1 || h < 1 || x0 + w > map->getXExtent() || y0 + h > map->getYExtent()) {
		mcpFail("region is outside the heightmap (%d x %d vertices)", map->getXExtent(), map->getYExtent());
	}
	const Int cols = (w + step - 1) / step;
	const Int rows = (h + step - 1) / step;
	if (cols * rows > MAX_HEIGHT_VALUES) {
		mcpFail("region too large (%d values); use a larger step or a smaller region", cols * rows);
	}
	McpJson data = McpJson::makeArray();
	for (Int y = y0; y < y0 + h; y += step) {
		McpJson row = McpJson::makeArray();
		for (Int x = x0; x < x0 + w; x += step) {
			row.push((int)map->getHeight(x, y));
		}
		data.push(row);
	}
	McpJson j = McpJson::makeObject();
	j.set("x0", x0).set("y0", y0).set("step", step).set("cols", cols).set("rows", rows);
	j.set("heights", data);
	return j;
}

McpJson cmdSetHeights(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int x0 = mcpArgInt(args, "x0");
	const Int y0 = mcpArgInt(args, "y0");
	const McpJson &rows = mcpArgArray(args, "heights");
	WorldHeightMapEdit *copy = map->duplicate();
	EditRange range;
	for (size_t r = 0; r < rows.size(); r++) {
		const McpJson &row = rows.at(r);
		if (!row.isArray()) {
			REF_PTR_RELEASE(copy);
			mcpFail("heights must be an array of rows (arrays of numbers)");
		}
		const Int y = y0 + (Int)r;
		for (size_t c = 0; c < row.size(); c++) {
			const Int x = x0 + (Int)c;
			if (x < 0 || y < 0 || x >= copy->getXExtent() || y >= copy->getYExtent()) {
				continue;
			}
			const McpJson &v = row.at(c);
			if (!v.isNumber()) {
				continue; // null leaves the vertex unchanged.
			}
			copy->setHeight(x, y, clampHeight(v.asNumber()));
			range.add(x, y);
		}
	}
	return commitHeightMapEdit(copy, range);
}

McpJson cmdBrush(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const std::string op = mcpArgString(args, "op");
	Shape shape;
	shape.parse(args, true);
	const double strength = mcpArgNumber(args, "strength", 1.0);
	const Int iterations = mcpArgInt(args, "iterations", 1);
	if (iterations < 1 || iterations > 50) mcpFail("iterations must be 1..50");

	double amount = 0;
	double target = 0;
	UnsignedInt seed = 0;
	if (op == "raise" || op == "lower" || op == "noise") {
		amount = mcpArgNumber(args, "amount");
		if (op == "lower") amount = -amount;
		seed = (UnsignedInt)mcpArgInt(args, "seed", 1);
	} else if (op == "set") {
		target = mcpArgNumber(args, "height");
	} else if (op == "flatten") {
		if (args.has("height")) {
			target = mcpArgNumber(args, "height");
		} else {
			target = map->getHeight(mcpWorldToIndexX(shape.cx), mcpWorldToIndexY(shape.cy));
		}
	} else if (op != "smooth") {
		mcpFail("op must be raise, lower, set, flatten, smooth or noise");
	}

	Int ix0, iy0, ix1, iy1;
	shape.indexRange(map, ix0, iy0, ix1, iy1);
	WorldHeightMapEdit *copy = map->duplicate();
	EditRange range;
	for (Int pass = 0; pass < iterations; pass++) {
		// Smoothing reads from a snapshot so the result does not depend on iteration order.
		std::vector<Int> snapshot;
		if (op == "smooth") {
			snapshot.resize((ix1 - ix0 + 3) * (iy1 - iy0 + 3));
			for (Int y = iy0 - 1; y <= iy1 + 1; y++) {
				for (Int x = ix0 - 1; x <= ix1 + 1; x++) {
					// Clamp so vertices on the map edge are not pulled toward zero.
					Int sx = x < 0 ? 0 : (x >= copy->getXExtent() ? copy->getXExtent() - 1 : x);
					Int sy = y < 0 ? 0 : (y >= copy->getYExtent() ? copy->getYExtent() - 1 : y);
					snapshot[(y - iy0 + 1) * (ix1 - ix0 + 3) + (x - ix0 + 1)] = copy->getHeight(sx, sy);
				}
			}
		}
		for (Int y = iy0; y <= iy1; y++) {
			for (Int x = ix0; x <= ix1; x++) {
				const double wgt = shape.weight(mcpIndexToWorldX(x), mcpIndexToWorldY(y)) * strength;
				if (wgt <= 0) {
					continue;
				}
				const double cur = copy->getHeight(x, y);
				double next = cur;
				if (op == "raise" || op == "lower") {
					next = cur + amount * wgt;
				} else if (op == "noise") {
					// Hash the vertex so the same seed gives the same terrain.
					UnsignedInt hsh = (UnsignedInt)x * 73856093u ^ (UnsignedInt)y * 19349663u ^ (seed + pass) * 83492791u;
					hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
					const double r = ((hsh >> 8) & 0xFFFF) / 65535.0 * 2.0 - 1.0;
					next = cur + amount * r * wgt;
				} else if (op == "set" || op == "flatten") {
					next = cur + (target - cur) * (wgt > 1 ? 1 : wgt);
				} else {
					double sum = 0;
					const Int stride = ix1 - ix0 + 3;
					for (Int dy = -1; dy <= 1; dy++) {
						for (Int dx = -1; dx <= 1; dx++) {
							sum += snapshot[(y + dy - iy0 + 1) * stride + (x + dx - ix0 + 1)];
						}
					}
					next = cur + (sum / 9.0 - cur) * (wgt > 1 ? 1 : wgt);
				}
				UnsignedByte newHeight = clampHeight(next);
				if (newHeight != copy->getHeight(x, y)) {
					copy->setHeight(x, y, newHeight);
					range.add(x, y);
				}
			}
		}
	}
	return commitHeightMapEdit(copy, range);
}

//-------------------------------------------------------------------------------------------------
// Textures and passability
//-------------------------------------------------------------------------------------------------

McpJson cmdListTextures(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	std::string filter = mcpArgString(args, "filter", "");
	McpJson list = McpJson::makeArray();
	for (Int i = 0; i < WorldHeightMapEdit::getNumTexClasses(); i++) {
		AsciiString name = WorldHeightMapEdit::getTexClassName(i);
		AsciiString uiName = WorldHeightMapEdit::getTexClassUiName(i);
		if (!mcpContainsNoCase(name.str(), filter.c_str()) && !mcpContainsNoCase(uiName.str(), filter.c_str())) {
			continue;
		}
		McpJson t = McpJson::makeObject();
		t.set("index", i).set("name", name.str()).set("ui_name", uiName.str());
		t.set("used_in_map", map->isTexClassUsed(i) != 0);
		t.set("can_fit", map->canFitTexture(i) != 0);
		t.set("blend_edge", WorldHeightMapEdit::getTexClassIsBlendEdge(i) != 0);
		list.push(t);
	}
	return McpJson::makeObject().set("textures", list);
}

McpJson cmdPaintTexture(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int texClass = findTextureClass(args, "texture");
	Shape shape;
	shape.parse(args, false); // Tiles are either painted or not.
	if (!map->isTexClassUsed(texClass) && !map->canFitTexture(texClass)) {
		mcpFail("the map has no room for another texture; paint over or remove an existing one first");
	}
	Int ix0, iy0, ix1, iy1;
	shape.indexRange(map, ix0, iy0, ix1, iy1);
	WorldHeightMapEdit *copy = map->duplicate();
	EditRange range;
	for (Int y = iy0; y <= iy1; y++) {
		for (Int x = ix0; x <= ix1; x++) {
			// Tiles are addressed by their lower left vertex; test the tile center.
			if (shape.weight(mcpIndexToWorldX(x) + MAP_XY_FACTOR / 2, mcpIndexToWorldY(y) + MAP_XY_FACTOR / 2) <= 0) {
				continue;
			}
			if (copy->setTileNdx(x, y, texClass, false)) {
				range.fullUpdate = true;
			}
			range.add(x, y);
		}
	}
	return commitHeightMapEdit(copy, range);
}

McpJson cmdFloodFill(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int texClass = findTextureClass(args, "texture");
	const Int x = mcpWorldToIndexX(mcpArgNumber(args, "x"));
	const Int y = mcpWorldToIndexY(mcpArgNumber(args, "y"));
	if (x < 0 || y < 0 || x >= map->getXExtent() || y >= map->getYExtent()) {
		mcpFail("point is outside the map");
	}
	WorldHeightMapEdit *copy = map->duplicate();
	EditRange range;
	if (copy->floodFill(x, y, texClass, mcpArgBool(args, "replace_all", false))) {
		range.add(0, 0);
		range.add(copy->getXExtent() - 1, copy->getYExtent() - 1);
		range.fullUpdate = true;
	}
	return commitHeightMapEdit(copy, range);
}

McpJson cmdAutoBlend(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int x = mcpWorldToIndexX(mcpArgNumber(args, "x"));
	const Int y = mcpWorldToIndexY(mcpArgNumber(args, "y"));
	if (x < 0 || y < 0 || x >= map->getXExtent() || y >= map->getYExtent()) {
		mcpFail("point is outside the map");
	}
	const Int edgeClass = args.has("edge_texture") ? findTextureClass(args, "edge_texture") : -1;
	WorldHeightMapEdit *copy = map->duplicate();
	copy->autoBlendOut(x, y, edgeClass);
	EditRange range;
	range.add(0, 0);
	range.add(copy->getXExtent() - 1, copy->getYExtent() - 1);
	range.fullUpdate = true;
	return commitHeightMapEdit(copy, range);
}

McpJson cmdSetPassability(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const bool impassable = mcpArgBool(args, "impassable", true);
	Shape shape;
	shape.parse(args, false);
	Int ix0, iy0, ix1, iy1;
	shape.indexRange(map, ix0, iy0, ix1, iy1);
	WorldHeightMapEdit *copy = map->duplicate();
	EditRange range;
	for (Int y = iy0; y <= iy1; y++) {
		for (Int x = ix0; x <= ix1; x++) {
			if (shape.weight(mcpIndexToWorldX(x) + MAP_XY_FACTOR / 2, mcpIndexToWorldY(y) + MAP_XY_FACTOR / 2) <= 0) {
				continue;
			}
			copy->setCliff(x, y, impassable);
			range.add(x, y);
		}
	}
	return commitHeightMapEdit(copy, range);
}

McpJson cmdSample(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const double wx = mcpArgNumber(args, "x");
	const double wy = mcpArgNumber(args, "y");
	const Int x = mcpWorldToIndexX(wx);
	const Int y = mcpWorldToIndexY(wy);
	if (x < 0 || y < 0 || x >= map->getXExtent() || y >= map->getYExtent()) {
		mcpFail("point is outside the map");
	}
	McpJson j = McpJson::makeObject();
	j.set("index_x", x).set("index_y", y);
	const Int raw = map->getHeight(x, y);
	j.set("height_raw", raw).set("height_world", raw * MAP_HEIGHT_SCALE);
	const Int tex = map->getTextureClass(x, y, true);
	if (tex >= 0 && tex < WorldHeightMapEdit::getNumTexClasses()) {
		j.set("texture", WorldHeightMapEdit::getTexClassName(tex).str());
	}
	j.set("impassable", map->getCliffState(x, y) != 0);
	return j;
}

} // namespace

void mcpRegisterTerrainCommands()
{
	mcpRegisterCommand("terrain.get_heights", cmdGetHeights, "{x0?,y0?,w?,h?,step?} Raw vertex heights (0..255) as rows; indices include the border.");
	mcpRegisterCommand("terrain.set_heights", cmdSetHeights, "{x0,y0,heights:[[...]]} Writes raw vertex heights; null entries are skipped.");
	mcpRegisterCommand("terrain.brush", cmdBrush, "{op:raise|lower|set|flatten|smooth|noise, shape, x,y,radius | x0,y0,x1,y1, feather?, amount?, height?, strength?, iterations?, seed?}");
	mcpRegisterCommand("terrain.list_textures", cmdListTextures, "{filter?} Available terrain texture classes.");
	mcpRegisterCommand("terrain.paint_texture", cmdPaintTexture, "{texture, shape, x,y,radius | x0,y0,x1,y1} Paints a texture.");
	mcpRegisterCommand("terrain.flood_fill", cmdFloodFill, "{texture,x,y,replace_all?} Flood fills the texture region under a point.");
	mcpRegisterCommand("terrain.auto_blend", cmdAutoBlend, "{x,y,edge_texture?} Blends the edges of the texture region under a point.");
	mcpRegisterCommand("terrain.set_passability", cmdSetPassability, "{impassable, shape, x,y,radius | x0,y0,x1,y1} Paints impassable cells.");
	mcpRegisterCommand("terrain.sample", cmdSample, "{x,y} Height, texture and passability at a world point.");
}
