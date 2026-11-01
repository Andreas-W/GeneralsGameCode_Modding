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

// McpCmdGenerate.cpp
// MCP bridge commands for procedural terrain: layered noise heights, slope limiting, automatic
// texturing by height/slope with noise overlays, and bulk blending. Modeled on the
// TerrainGenerator, TerrainSmoother and TextureGenerator processors of Genesis (The CWC Team).

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpNoise.h"
#include "mcp/McpShape.h"

#include "WHeightMapEdit.h"

#include <math.h>
#include <map>
#include <set>
#include <vector>

namespace
{

/// Optional area limit: without shape arguments the whole map is affected at full weight.
struct Area
{
	bool limited;
	McpShape shape;

	void parse(const McpJson &args, bool allowFeather)
	{
		limited = args.has("x") || args.has("x0") || args.has("shape");
		if (limited) shape.parse(args, allowFeather);
	}
	double weight(WorldHeightMapEdit *map, double vx, double vy) const
	{
		if (!limited) return 1.0;
		const Int border = map->getBorderSize();
		return shape.weight((vx - border) * MAP_XY_FACTOR, (vy - border) * MAP_XY_FACTOR);
	}
	void range(WorldHeightMapEdit *map, Int *x0, Int *y0, Int *x1, Int *y1) const
	{
		if (limited) {
			shape.indexRange(map, *x0, *y0, *x1, *y1);
		} else {
			*x0 = 0;
			*y0 = 0;
			*x1 = map->getXExtent() - 1;
			*y1 = map->getYExtent() - 1;
		}
	}
};

Int clampHeight(double h)
{
	const Int maxHeight = WorldHeightMap::getMaxHeightValue();
	if (h < 0) return 0;
	if (h > maxHeight) return maxHeight;
	return (Int)floor(h + 0.5);
}

//-------------------------------------------------------------------------------------------------
// terrain.generate
//-------------------------------------------------------------------------------------------------

struct NoiseLayer
{
	double height, frequency, persistence, amplitude;
	Int octaves;
	bool ridged, signedResult;
};

McpJson cmdGenerate(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const unsigned int seed = (unsigned int)mcpArgInt(args, "seed", 1);
	const std::string mode = mcpArgString(args, "mode", "set");
	if (mode != "set" && mode != "add") {
		mcpFail("mode must be set or add");
	}
	const double baseHeight = mcpArgNumber(args, "base_height", 40);

	std::vector<NoiseLayer> layers;
	const McpJson &layerArgs = mcpArgArray(args, "layers");
	for (size_t i = 0; i < layerArgs.size(); i++) {
		const McpJson &l = layerArgs.at(i);
		if (!l.isObject()) mcpFail("layers must be objects");
		NoiseLayer layer;
		layer.height = mcpArgNumber(l, "height");
		layer.frequency = mcpArgNumber(l, "frequency");
		layer.octaves = mcpArgInt(l, "octaves", 3);
		layer.persistence = mcpArgNumber(l, "persistence", 0.5);
		layer.amplitude = mcpArgNumber(l, "amplitude", 1.0);
		layer.ridged = mcpArgBool(l, "ridged", false);
		layer.signedResult = mcpArgBool(l, "signed", false);
		if (layer.frequency <= 0 || layer.octaves < 1 || layer.octaves > 10) {
			mcpFail("layer %d: frequency must be > 0 and octaves 1..10", (int)i);
		}
		layers.push_back(layer);
	}
	if (layers.empty()) mcpFail("pass at least one layer");

	Area area;
	area.parse(args, true);
	std::vector<McpShape> protect;
	const McpJson &protectArgs = args.get("protect");
	if (protectArgs.isArray()) {
		for (size_t i = 0; i < protectArgs.size(); i++) {
			McpShape s;
			s.parse(protectArgs.at(i), true);
			protect.push_back(s);
		}
	}

	std::vector<McpNoise *> noises;
	for (size_t i = 0; i < layers.size(); i++) {
		noises.push_back(new McpNoise(seed * 31u + (unsigned int)i * 7919u));
	}
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	const Int border = map->getBorderSize();
	WorldHeightMapEdit *copy = map->duplicate();
	Int changed = 0;
	for (Int y = y0; y <= y1; y++) {
		for (Int x = x0; x <= x1; x++) {
			double w = area.weight(map, x, y);
			for (size_t p = 0; p < protect.size() && w > 0; p++) {
				w *= 1.0 - protect[p].weight((x - border) * MAP_XY_FACTOR, (y - border) * MAP_XY_FACTOR);
			}
			if (w <= 0) continue;
			const double cur = map->getHeight(x, y);
			double target = mode == "set" ? baseHeight : cur;
			for (size_t i = 0; i < layers.size(); i++) {
				const NoiseLayer &l = layers[i];
				const double v = l.ridged
					? noises[i]->ridged(x, y, l.frequency, l.octaves, l.persistence, l.amplitude)
					: noises[i]->fractal(x, y, l.frequency, l.octaves, l.persistence, l.amplitude, l.signedResult);
				target += v * l.height;
			}
			const Int h = clampHeight(cur + (target - cur) * (w > 1 ? 1 : w));
			if (h != map->getHeight(x, y)) {
				copy->setHeight(x, y, h);
				changed++;
			}
		}
	}
	for (size_t i = 0; i < noises.size(); i++) delete noises[i];
	if (changed == 0) {
		REF_PTR_RELEASE(copy);
		return McpJson::makeObject().set("changed", false);
	}
	McpJson j = mcpCommitHeightMapEdit(copy, false);
	j.set("vertices_changed", changed);
	return j;
}

//-------------------------------------------------------------------------------------------------
// terrain.limit_slope
//-------------------------------------------------------------------------------------------------

McpJson cmdLimitSlope(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const double maxStep = mcpArgNumber(args, "max_step", 15);
	const Int iterations = mcpArgInt(args, "iterations", 20);
	if (maxStep < 1 || iterations < 1 || iterations > 100) {
		mcpFail("max_step must be >= 1 and iterations 1..100");
	}
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	const Int w = x1 - x0 + 1, h = y1 - y0 + 1;

	std::vector<double> heights(w * h);
	for (Int y = 0; y < h; y++) {
		for (Int x = 0; x < w; x++) {
			heights[y * w + x] = map->getHeight(x0 + x, y0 + y);
		}
	}
	// Like Genesis' TerrainSmoother, but symmetric: every too-steep neighbour pair is pulled toward
	// each other until the height step is at most max_step. Diagonals allow sqrt(2) times more.
	static const Int dirs[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
	Int passes = 0;
	for (; passes < iterations; passes++) {
		bool any = false;
		for (Int y = 0; y < h; y++) {
			for (Int x = 0; x < w; x++) {
				for (Int d = 0; d < 4; d++) {
					const Int nx = x + dirs[d][0], ny = y + dirs[d][1];
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					const double limit = (dirs[d][0] && dirs[d][1]) ? maxStep * 1.4142 : maxStep;
					double &a = heights[y * w + x];
					double &b = heights[ny * w + nx];
					const double diff = a - b;
					if (fabs(diff) > limit + 0.01) {
						const double excess = (fabs(diff) - limit) / 2;
						if (diff > 0) { a -= excess; b += excess; } else { a += excess; b -= excess; }
						any = true;
					}
				}
			}
		}
		if (!any) break;
	}

	WorldHeightMapEdit *copy = map->duplicate();
	Int changed = 0;
	for (Int y = 0; y < h; y++) {
		for (Int x = 0; x < w; x++) {
			const Int nh = clampHeight(heights[y * w + x]);
			if (nh != map->getHeight(x0 + x, y0 + y)) {
				// setHeight also updates the cliff (impassable) flags from the new slopes.
				copy->setHeight(x0 + x, y0 + y, nh);
				changed++;
			}
		}
	}
	if (changed == 0) {
		REF_PTR_RELEASE(copy);
		return McpJson::makeObject().set("changed", false).set("passes", passes);
	}
	McpJson j = mcpCommitHeightMapEdit(copy, false);
	j.set("vertices_changed", changed).set("passes", passes);
	return j;
}

//-------------------------------------------------------------------------------------------------
// Blending
//-------------------------------------------------------------------------------------------------

Int optionalTexture(const McpJson &args, const char *key)
{
	return args.has(key) ? mcpFindTextureClass(args, key) : -1;
}

/// The cell range that blend commands work on: blending reads the cell's right and upper neighbours.
void blendRange(WorldHeightMapEdit *map, const McpJson &args, Int *x0, Int *y0, Int *x1, Int *y1)
{
	Area area;
	area.parse(args, false);
	area.range(map, x0, y0, x1, y1);
	if (*x1 >= map->getXExtent() - 1) *x1 = map->getXExtent() - 2;
	if (*y1 >= map->getYExtent() - 1) *y1 = map->getYExtent() - 2;
}

/// The given textures, by default all in the range except the most common, which is taken to be the ground.
std::set<Int> blendClasses(WorldHeightMapEdit *map, const McpJson &args, Int x0, Int y0, Int x1, Int y1)
{
	std::set<Int> classes;
	if (args.has("textures")) {
		const McpJson &list = mcpArgArray(args, "textures");
		for (size_t i = 0; i < list.size(); i++) {
			McpJson one = McpJson::makeObject().set("texture", list.at(i));
			classes.insert(mcpFindTextureClass(one, "texture"));
		}
	} else {
		std::vector<Int> counts(WorldHeightMapEdit::getNumTexClasses(), 0);
		for (Int y = y0; y <= y1; y++) {
			for (Int x = x0; x <= x1; x++) {
				const Int cls = map->getTextureClass(x, y, true);
				if (cls >= 0 && cls < (Int)counts.size()) counts[cls]++;
			}
		}
		Int ground = -1;
		for (Int i = 0; i < (Int)counts.size(); i++) {
			if (counts[i] > 0) classes.insert(i);
			if (counts[i] > 0 && (ground < 0 || counts[i] > counts[ground])) ground = i;
		}
		classes.erase(ground);
	}
	return classes;
}

void setBlendStats(McpJson &j, WorldHeightMapEdit *map, Int x0, Int y0, Int x1, Int y1)
{
	Int blended, threeWay, hardEdges;
	map->getBlendStats(x0, y0, x1, y1, &blended, &threeWay, &hardEdges);
	j.set("cells_blended", blended).set("three_way_tiles", threeWay).set("hard_edges", hardEdges);
	j.set("blend_table_used", map->getNumBlendTilesUsed()).set("blend_table_size", NUM_BLEND_TILES);
}

McpJson cmdBlendAll(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Int x0, y0, x1, y1;
	blendRange(map, args, &x0, &y0, &x1, &y1);
	const std::set<Int> classes = blendClasses(map, args, x0, y0, x1, y1);
	const Int edgeClass = optionalTexture(args, "edge_texture");
	WorldHeightMapEdit *copy = map->duplicate();
	copy->blendAllEdges(classes, edgeClass, x0, y0, x1, y1);
	McpJson j = mcpCommitHeightMapEdit(copy, true);
	setBlendStats(j, mcpHeightMap(), x0, y0, x1, y1);
	return j;
}

McpJson cmdReblend(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	WorldHeightMapEdit *copy = map->duplicate();
	copy->blendCorners(x0, y0, x1, y1);
	McpJson j = mcpCommitHeightMapEdit(copy, true);
	setBlendStats(j, mcpHeightMap(), x0, y0, x1, y1);
	return j;
}

McpJson cmdBlendReport(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Int x0, y0, x1, y1;
	blendRange(map, args, &x0, &y0, &x1, &y1);
	McpJson j = McpJson::makeObject();
	setBlendStats(j, map, x0, y0, x1, y1);
	return j;
}

McpJson cmdRemoveBlends(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	WorldHeightMapEdit *copy = map->duplicate();
	copy->clearBlends(x0, y0, x1, y1);
	return mcpCommitHeightMapEdit(copy, true);
}

//-------------------------------------------------------------------------------------------------
// terrain.auto_texture
//-------------------------------------------------------------------------------------------------

struct Overlay
{
	Int texClass;
	double frequency, threshold, persistence, amplitude;
	Int octaves;
	McpNoise *noise;
};

/// A base texture plus noise overlays (Genesis scenery layer).
struct TextureLayer
{
	bool present;
	Int base;
	std::vector<Overlay> overlays;

	void parse(const McpJson &arg, const char *what, unsigned int seed)
	{
		present = arg.isObject();
		if (!present) return;
		base = mcpFindTextureClass(arg, "texture");
		const McpJson &list = arg.get("overlays");
		if (list.isNull()) return;
		if (!list.isArray()) mcpFail("%s.overlays must be a list", what);
		for (size_t i = 0; i < list.size(); i++) {
			const McpJson &o = list.at(i);
			Overlay ov;
			ov.texClass = mcpFindTextureClass(o, "texture");
			// Defaults match the overlay noise of Genesis' TextureGenerator.
			ov.frequency = mcpArgNumber(o, "frequency", 0.07);
			ov.threshold = mcpArgNumber(o, "threshold", 0.5);
			ov.octaves = mcpArgInt(o, "octaves", 1);
			ov.persistence = mcpArgNumber(o, "persistence", 0.25);
			ov.amplitude = mcpArgNumber(o, "amplitude", 1.45);
			ov.noise = new McpNoise(seed * 131u + (unsigned int)(overlays.size() + 1) * 104729u + (unsigned int)strlen(what));
			overlays.push_back(ov);
		}
	}
	void free()
	{
		for (size_t i = 0; i < overlays.size(); i++) delete overlays[i].noise;
		overlays.clear();
	}
	Int pick(Int x, Int y) const
	{
		Int cls = base;
		for (size_t i = 0; i < overlays.size(); i++) {
			const Overlay &o = overlays[i];
			if (o.noise->fractal(x, y, o.frequency, o.octaves, o.persistence, o.amplitude, false) > o.threshold) {
				cls = o.texClass;
			}
		}
		return cls;
	}
	void collect(std::vector<Int> &all, bool includeBase) const
	{
		if (!present) return;
		if (includeBase) all.push_back(base);
		for (size_t i = 0; i < overlays.size(); i++) all.push_back(overlays[i].texClass);
	}
};

McpJson cmdAutoTexture(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	const unsigned int seed = (unsigned int)mcpArgInt(args, "seed", 1);
	TextureLayer ground, cliff, water;
	ground.parse(args.get("base"), "base", seed);
	if (!ground.present) mcpFail("base must be an object with a texture (and optional overlays)");
	cliff.parse(args.get("cliff"), "cliff", seed);
	water.parse(args.get("water"), "water", seed);
	const McpJson &cliffArgs = args.get("cliff");
	const McpJson &waterArgs = args.get("water");
	const double cliffSlope = cliff.present ? mcpArgNumber(cliffArgs, "slope", 12) : 0;
	const bool cliffImpassable = cliff.present && mcpArgBool(cliffArgs, "use_impassable", true);
	const bool waterPolygons = water.present && !waterArgs.has("below");
	const double waterBelow = water.present && waterArgs.has("below") ? mcpArgNumber(waterArgs, "below") : 0;

	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	if (x1 >= map->getXExtent() - 1) x1 = map->getXExtent() - 2;
	if (y1 >= map->getYExtent() - 1) y1 = map->getYExtent() - 2;

	WorldHeightMapEdit *copy = map->duplicate();
	// Make sure every texture fits before painting anything, and allocate them in order.
	std::vector<Int> textures;
	ground.collect(textures, true);
	cliff.collect(textures, true);
	water.collect(textures, true);
	std::vector<std::string> missing;
	// Painting a texture is the only public way to allocate it; use the area center, which is
	// always repainted below.
	const Int allocX = (x0 + x1) / 2, allocY = (y0 + y1) / 2;
	for (size_t i = 0; i < textures.size(); i++) {
		if (!copy->isTexClassUsed(textures[i])) {
			if (!copy->canFitTexture(textures[i])) {
				missing.push_back(WorldHeightMapEdit::getTexClassName(textures[i]).str());
				continue;
			}
			copy->setTileNdx(allocX, allocY, textures[i], false);
		}
	}
	if (!missing.empty()) {
		std::string list;
		for (size_t i = 0; i < missing.size(); i++) list += (i ? ", " : "") + missing[i];
		ground.free(); cliff.free(); water.free();
		REF_PTR_RELEASE(copy);
		mcpFail("the map has no room for these textures: %s (use fewer textures or replace existing ones)", list.c_str());
	}

	Int numCliff = 0, numWater = 0, numGround = 0;
	for (Int y = y0; y <= y1; y++) {
		for (Int x = x0; x <= x1; x++) {
			if (area.limited && area.weight(map, x + 0.5, y + 0.5) <= 0) continue;
			const Int h00 = map->getHeight(x, y), h10 = map->getHeight(x + 1, y);
			const Int h01 = map->getHeight(x, y + 1), h11 = map->getHeight(x + 1, y + 1);
			Int lo = h00, hi = h00;
			const Int hs[3] = { h10, h01, h11 };
			for (Int i = 0; i < 3; i++) { if (hs[i] < lo) lo = hs[i]; if (hs[i] > hi) hi = hs[i]; }
			const double avg = (h00 + h10 + h01 + h11) / 4.0;
			Int cls;
			if (cliff.present && (hi - lo >= cliffSlope || (cliffImpassable && map->getCliffState(x, y)))) {
				cls = cliff.pick(x, y);
				numCliff++;
			} else if (water.present && (waterPolygons ? mcpIsCellUnderWater(map, x, y) : avg < waterBelow)) {
				cls = water.pick(x, y);
				numWater++;
			} else {
				cls = ground.pick(x, y);
				numGround++;
			}
			copy->setTileNdx(x, y, cls, false);
		}
	}

	if (mcpArgBool(args, "blend", true)) {
		// Blend every texture except the ground base outward, so overlays, cliffs and water
		// fade into their surroundings (Genesis GenerateBlendTiles).
		std::vector<Int> toBlend;
		ground.collect(toBlend, false);
		cliff.collect(toBlend, true);
		water.collect(toBlend, true);
		std::set<Int> classes(toBlend.begin(), toBlend.end());
		classes.erase(ground.base);
		const Int blendX1 = x1 >= copy->getXExtent() - 1 ? copy->getXExtent() - 2 : x1;
		const Int blendY1 = y1 >= copy->getYExtent() - 1 ? copy->getYExtent() - 2 : y1;
		copy->blendAllEdges(classes, optionalTexture(args, "edge_texture"), x0, y0, blendX1, blendY1);
	}
	ground.free(); cliff.free(); water.free();

	McpJson j = mcpCommitHeightMapEdit(copy, true);
	j.set("cells_ground", numGround).set("cells_cliff", numCliff).set("cells_water", numWater);
	return j;
}

//-------------------------------------------------------------------------------------------------
// terrain.cliff_*
//-------------------------------------------------------------------------------------------------

const Real CLIFF_UV_EPSILON = 0.25f / TEXTURE_WIDTH;

void minMax4(Real a, Real b, Real c, Real d, Real *pMin, Real *pMax)
{
	*pMin = a; *pMax = a;
	if (b < *pMin) *pMin = b;
	if (b > *pMax) *pMax = b;
	if (c < *pMin) *pMin = c;
	if (c > *pMax) *pMax = c;
	if (d < *pMin) *pMin = d;
	if (d > *pMax) *pMax = d;
}

bool outsideRange(Real a, Real b, Real c, Real d, Real lo, Real hi)
{
	Real minValue, maxValue;
	minMax4(a, b, c, d, &minValue, &maxValue);
	return minValue < lo - CLIFF_UV_EPSILON || maxValue > hi + CLIFF_UV_EPSILON;
}

struct CliffClassStats
{
	Int cells, outsideU, outsideV, mutants;
	Real minV, maxV, extent;
	CliffClassStats() : cells(0), outsideU(0), outsideV(0), mutants(0), minV(0), maxV(0), extent(0) {}
};

void setCliffStats(McpJson &j, WorldHeightMapEdit *map, Int x0, Int y0, Int x1, Int y1)
{
	const Int border = map->getBorderSize();
	std::map<Int, CliffClassStats> classes;
	Int cliffCells = 0, unmapped = 0, mapped = 0, outsideU = 0, outsideV = 0, mutants = 0;
	Real worstV = 0;
	McpJson samples = McpJson::makeArray();
	for (Int y = y0; y <= y1 && y < map->getYExtent() - 1; y++) {
		for (Int x = x0; x <= x1 && x < map->getXExtent() - 1; x++) {
			TCliffInfo info;
			Int texClass;
			Real extent;
			const bool steep = map->isCliffCell(x, y);
			if (steep) cliffCells++;
			if (!map->getCliffCell(x, y, &info, &texClass, &extent)) {
				if (steep) unmapped++;
				continue;
			}
			mapped++;
			CliffClassStats &s = classes[texClass];
			Real minV, maxV;
			minMax4(info.v0, info.v1, info.v2, info.v3, &minV, &maxV);
			if (s.cells == 0 || minV < s.minV) s.minV = minV;
			if (s.cells == 0 || maxV > s.maxV) s.maxV = maxV;
			s.extent = extent;
			s.cells++;
			if (info.mutant) { s.mutants++; mutants++; }
			const bool badU = outsideRange(info.u0, info.u1, info.u2, info.u3, 0, extent);
			const bool badV = outsideRange(info.v0, info.v1, info.v2, info.v3, -extent, 0);
			if (badU) { s.outsideU++; outsideU++; }
			if (badV) {
				s.outsideV++;
				outsideV++;
				const Real beyond = max(maxV, -extent - minV);
				if (beyond > worstV) worstV = beyond;
			}
			if ((badU || badV) && samples.size() < 12) {
				samples.push(McpJson::makeObject().set("x", (x - border) * MAP_XY_FACTOR).set("y", (y - border) * MAP_XY_FACTOR)
					.set("min_v_px", minV * TEXTURE_WIDTH).set("max_v_px", maxV * TEXTURE_WIDTH).set("height_px", extent * TEXTURE_WIDTH).set("mutant", info.mutant != 0));
			}
		}
	}
	McpJson textures = McpJson::makeArray();
	for (std::map<Int, CliffClassStats>::const_iterator it = classes.begin(); it != classes.end(); ++it) {
		const CliffClassStats &s = it->second;
		textures.push(McpJson::makeObject().set("texture", WorldHeightMapEdit::getTexClassName(it->first).str())
			.set("height_px", s.extent * TEXTURE_WIDTH).set("cells", s.cells).set("outside_u", s.outsideU).set("outside_v", s.outsideV)
			.set("mutants", s.mutants).set("min_v_px", s.minV * TEXTURE_WIDTH).set("max_v_px", s.maxV * TEXTURE_WIDTH));
	}
	j.set("cliff_cells", cliffCells).set("cliff_cells_unmapped", unmapped).set("cells_mapped", mapped);
	j.set("outside_u", outsideU).set("outside_v", outsideV).set("mutants", mutants).set("worst_v_px", worstV * TEXTURE_WIDTH);
	j.set("textures", textures).set("samples", samples);
}

// Moves one axis of a cell by whole texture sizes into lo..hi, where the texture repeats seamlessly.
// A cell that still does not fit crosses a repeat: with slide it moves on inside the texture, keeping its
// scale and leaving a small jump to its neighbours. Otherwise, or if it is taller than the texture, its
// corners are clamped, as adjustForTiling does for U.
// Returns 0 if nothing changed, 1 if moved, 2 if clamped or slid.
Int foldCliffAxis(Real *c0, Real *c1, Real *c2, Real *c3, Real lo, Real hi, bool slide)
{
	if (!outsideRange(*c0, *c1, *c2, *c3, lo, hi)) return 0;
	Real minValue, maxValue;
	minMax4(*c0, *c1, *c2, *c3, &minValue, &maxValue);
	const Real size = hi - lo;
	const Real shift = -(Real)ceil((maxValue - hi) / size) * size;
	*c0 += shift; *c1 += shift; *c2 += shift; *c3 += shift;
	if (minValue + shift >= lo - CLIFF_UV_EPSILON) return 1;
	if (slide && maxValue - minValue <= size) {
		const Real slideBy = lo - (minValue + shift);
		*c0 += slideBy; *c1 += slideBy; *c2 += slideBy; *c3 += slideBy;
		return 2;
	}
	if (*c0 < lo) *c0 = lo;
	if (*c1 < lo) *c1 = lo;
	if (*c2 < lo) *c2 = lo;
	if (*c3 < lo) *c3 = lo;
	return 2;
}

// Squeezes V of each connected mapped region of one texture so that the region fits the texture once.
// Everything stays continuous, at the price of a texture that is finer vertically on tall regions.
void fitCliffRegions(WorldHeightMapEdit *copy, Int *pRegions, Int *pSqueezed, Real *pMaxSqueeze)
{
	const Int w = copy->getXExtent() - 1;
	const Int h = copy->getYExtent() - 1;
	std::vector<char> seen(w * h, 0);
	std::vector<Int> region, stack;
	*pRegions = 0;
	*pSqueezed = 0;
	*pMaxSqueeze = 1;
	for (Int start = 0; start < w * h; start++) {
		TCliffInfo info;
		Int texClass;
		Real extent;
		if (seen[start] || !copy->getCliffCell(start % w, start / w, &info, &texClass, &extent)) continue;
		region.clear();
		stack.assign(1, start);
		seen[start] = 1;
		Real minV = info.v0, maxV = info.v0;
		while (!stack.empty()) {
			const Int cur = stack.back();
			stack.pop_back();
			region.push_back(cur);
			const Int cx = cur % w, cy = cur / w;
			TCliffInfo c;
			Int cClass;
			Real cExtent;
			copy->getCliffCell(cx, cy, &c, &cClass, &cExtent);
			Real lo, hi;
			minMax4(c.v0, c.v1, c.v2, c.v3, &lo, &hi);
			if (lo < minV) minV = lo;
			if (hi > maxV) maxV = hi;
			const Int nx[4] = { cx - 1, cx + 1, cx, cx };
			const Int ny[4] = { cy, cy, cy - 1, cy + 1 };
			for (Int k = 0; k < 4; k++) {
				if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
				const Int next = ny[k] * w + nx[k];
				if (seen[next] || !copy->getCliffCell(nx[k], ny[k], &c, &cClass, &cExtent) || cClass != texClass) continue;
				seen[next] = 1;
				stack.push_back(next);
			}
		}
		const Real span = maxV - minV;
		const Real factor = span > extent ? extent / span : 1.0f;
		const Real margin = (extent - span * factor) / 2;
		for (size_t i = 0; i < region.size(); i++) {
			const Int cx = region[i] % w, cy = region[i] / w;
			TCliffInfo c;
			Int cClass;
			Real cExtent;
			copy->getCliffCell(cx, cy, &c, &cClass, &cExtent);
			c.v0 = (c.v0 - minV) * factor - extent + margin;
			c.v1 = (c.v1 - minV) * factor - extent + margin;
			c.v2 = (c.v2 - minV) * factor - extent + margin;
			c.v3 = (c.v3 - minV) * factor - extent + margin;
			foldCliffAxis(&c.u0, &c.u1, &c.u2, &c.u3, 0, extent, false);
			copy->setCliffCell(cx, cy, c);
		}
		(*pRegions)++;
		if (factor < 1) {
			(*pSqueezed)++;
			if (1 / factor > *pMaxSqueeze) *pMaxSqueeze = 1 / factor;
		}
	}
}

void repairCliffCells(WorldHeightMapEdit *copy, Int x0, Int y0, Int x1, Int y1, bool slide, Int *pMoved, Int *pClamped)
{
	*pMoved = 0;
	*pClamped = 0;
	for (Int y = y0; y <= y1 && y < copy->getYExtent() - 1; y++) {
		for (Int x = x0; x <= x1 && x < copy->getXExtent() - 1; x++) {
			TCliffInfo info;
			Int texClass;
			Real extent;
			if (!copy->getCliffCell(x, y, &info, &texClass, &extent)) continue;
			const Int v = foldCliffAxis(&info.v0, &info.v1, &info.v2, &info.v3, -extent, 0, slide);
			const Int u = foldCliffAxis(&info.u0, &info.u1, &info.u2, &info.u3, 0, extent, false);
			if (u == 0 && v == 0) continue;
			copy->setCliffCell(x, y, info);
			if (u == 2 || v == 2) (*pClamped)++;
			else (*pMoved)++;
		}
	}
}

void applyCliffRepair(WorldHeightMapEdit *copy, const std::string &repair, Int x0, Int y0, Int x1, Int y1, McpJson &result)
{
	Int moved = 0, clamped = 0;
	if (repair == "fold" || repair == "slide") {
		repairCliffCells(copy, x0, y0, x1, y1, repair == "slide", &moved, &clamped);
		result.set("cells_moved", moved).set("cells_changed_scale_or_jump", clamped);
	} else if (repair == "fit") {
		Int regions, squeezed;
		Real maxSqueeze;
		fitCliffRegions(copy, &regions, &squeezed, &maxSqueeze);
		result.set("fit_regions", regions).set("regions_squeezed", squeezed).set("max_squeeze", maxSqueeze);
	} else if (repair != "none") {
		mcpFail("repair must be none, fold, slide or fit");
	}
}

McpJson cmdCliffMap(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	const std::string repair = mcpArgString(args, "repair", "none");
	WorldHeightMapEdit *copy = map->duplicate();
	copy->clearCliffMapping(x0, y0, x1, y1);
	const Int regions = copy->mapCliffs(x0, y0, x1, y1);
	McpJson stats = McpJson::makeObject();
	applyCliffRepair(copy, repair, x0, y0, x1, y1, stats);
	McpJson j = mcpCommitHeightMapEdit(copy, true);
	j.set("regions", regions).set("repair", stats);
	setCliffStats(j, mcpHeightMap(), x0, y0, x1, y1);
	return j;
}

McpJson cmdCliffRepair(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	WorldHeightMapEdit *copy = map->duplicate();
	McpJson stats = McpJson::makeObject();
	applyCliffRepair(copy, mcpArgString(args, "repair", "fit"), x0, y0, x1, y1, stats);
	McpJson j = mcpCommitHeightMapEdit(copy, true);
	j.set("repair", stats);
	setCliffStats(j, mcpHeightMap(), x0, y0, x1, y1);
	return j;
}

McpJson cmdCliffReport(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	McpJson j = McpJson::makeObject();
	setCliffStats(j, map, x0, y0, x1, y1);
	return j;
}

McpJson cmdCliffClear(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	WorldHeightMapEdit *copy = map->duplicate();
	copy->clearCliffMapping(x0, y0, x1, y1);
	return mcpCommitHeightMapEdit(copy, true);
}

} // namespace

void mcpRegisterGenerateCommands()
{
	mcpRegisterCommand("terrain.generate", cmdGenerate, "{layers:[{height,frequency,octaves?,persistence?,amplitude?,ridged?,signed?}], seed?, mode?:set|add, base_height?, shape?, feather?, protect?:[{x,y,radius,feather?}]} Noise terrain.");
	mcpRegisterCommand("terrain.limit_slope", cmdLimitSlope, "{max_step?=15, iterations?=20, shape?} Reduces height steps between neighbours; cliff flags follow the slope.");
	mcpRegisterCommand("terrain.auto_texture", cmdAutoTexture, "{base:{texture,overlays?}, cliff?:{texture,overlays?,slope?,use_impassable?}, water?:{texture,overlays?,below?}, seed?, blend?, edge_texture?, shape?} Textures cells by slope/water with noise overlays.");
	mcpRegisterCommand("terrain.blend_all", cmdBlendAll, "{textures?, edge_texture?, shape?} Blends every edge of the given (default: all but the most common) textures outward, keeping existing blends.");
	mcpRegisterCommand("terrain.reblend", cmdReblend, "{shape?} Removes all blends in the area and blends all textures again from the cell corners, as one undo step.");
	mcpRegisterCommand("terrain.blend_report", cmdBlendReport, "{shape?} Counts blended cells, 3-way tiles and edges that neither side blends across.");
	mcpRegisterCommand("terrain.remove_blends", cmdRemoveBlends, "{shape?} Removes texture blends.");
	mcpRegisterCommand("terrain.cliff_map", cmdCliffMap, "{repair?:none|fold|slide|fit, shape?} Clears and redoes cliff texture mapping on all steep cells. Reports cells whose coordinates leave their texture.");
	mcpRegisterCommand("terrain.cliff_repair", cmdCliffRepair, "{repair?:fold|slide|fit, shape?} Brings mapped cells that leave their texture back into it. fit always works on whole regions.");
	mcpRegisterCommand("terrain.cliff_report", cmdCliffReport, "{shape?} Counts steep and mapped cells and cells whose coordinates leave their texture.");
	mcpRegisterCommand("terrain.cliff_clear", cmdCliffClear, "{shape?} Removes cliff texture mapping.");
}
