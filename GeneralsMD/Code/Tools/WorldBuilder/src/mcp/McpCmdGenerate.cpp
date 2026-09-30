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

UnsignedByte clampHeight(double h)
{
	if (h < 0) return 0;
	if (h > 255) return 255;
	return (UnsignedByte)floor(h + 0.5);
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
			const UnsignedByte h = clampHeight(cur + (target - cur) * (w > 1 ? 1 : w));
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
			const UnsignedByte nh = clampHeight(heights[y * w + x]);
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

/// Runs autoBlendOut once per connected region of each listed texture class inside the range.
Int blendRegions(WorldHeightMapEdit *map, const std::set<Int> &classes, Int edgeClass, Int x0, Int y0, Int x1, Int y1)
{
	const Int w = x1 - x0 + 1, h = y1 - y0 + 1;
	std::vector<unsigned char> seen(w * h, 0);
	std::vector<Int> stack;
	Int regions = 0;
	for (Int y = y0; y <= y1; y++) {
		for (Int x = x0; x <= x1; x++) {
			if (seen[(y - y0) * w + (x - x0)]) continue;
			const Int cls = map->getTextureClass(x, y, true);
			if (!classes.count(cls)) continue;
			// Flood the region, remembering a cell that is not blended yet: autoBlendOut needs one.
			Int seedX = -1, seedY = -1;
			stack.clear();
			stack.push_back(y * 100000 + x);
			seen[(y - y0) * w + (x - x0)] = 1;
			while (!stack.empty()) {
				const Int packed = stack.back();
				stack.pop_back();
				const Int cx = packed % 100000, cy = packed / 100000;
				if (seedX < 0 && map->getTextureClass(cx, cy, false) == cls) {
					seedX = cx;
					seedY = cy;
				}
				static const Int n4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
				for (Int d = 0; d < 4; d++) {
					const Int nx = cx + n4[d][0], ny = cy + n4[d][1];
					if (nx < x0 || ny < y0 || nx > x1 || ny > y1) continue;
					unsigned char &s = seen[(ny - y0) * w + (nx - x0)];
					if (s || map->getTextureClass(nx, ny, true) != cls) continue;
					s = 1;
					stack.push_back(ny * 100000 + nx);
				}
			}
			if (seedX >= 0) {
				map->autoBlendOut(seedX, seedY, edgeClass);
				regions++;
			}
		}
	}
	return regions;
}

Int optionalTexture(const McpJson &args, const char *key)
{
	return args.has(key) ? mcpFindTextureClass(args, key) : -1;
}

McpJson cmdBlendAll(const McpJson &args)
{
	WorldHeightMapEdit *map = mcpHeightMap();
	Area area;
	area.parse(args, false);
	Int x0, y0, x1, y1;
	area.range(map, &x0, &y0, &x1, &y1);
	if (x1 >= map->getXExtent() - 1) x1 = map->getXExtent() - 2;
	if (y1 >= map->getYExtent() - 1) y1 = map->getYExtent() - 2;

	std::set<Int> classes;
	if (args.has("textures")) {
		const McpJson &list = mcpArgArray(args, "textures");
		for (size_t i = 0; i < list.size(); i++) {
			McpJson one = McpJson::makeObject().set("texture", list.at(i));
			classes.insert(mcpFindTextureClass(one, "texture"));
		}
	} else {
		// Blend everything except the most common texture, which is taken to be the ground.
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
	WorldHeightMapEdit *copy = map->duplicate();
	const Int regions = blendRegions(copy, classes, optionalTexture(args, "edge_texture"), x0, y0, x1, y1);
	McpJson j = mcpCommitHeightMapEdit(copy, true);
	j.set("regions_blended", regions);
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

	Int regions = 0;
	if (mcpArgBool(args, "blend", true)) {
		// Blend every texture except the ground base outward, so overlays, cliffs and water
		// fade into their surroundings (Genesis GenerateBlendTiles).
		std::vector<Int> toBlend;
		ground.collect(toBlend, false);
		cliff.collect(toBlend, true);
		water.collect(toBlend, true);
		std::set<Int> classes(toBlend.begin(), toBlend.end());
		classes.erase(ground.base);
		regions = blendRegions(copy, classes, optionalTexture(args, "edge_texture"), x0, y0, x1, y1);
	}
	ground.free(); cliff.free(); water.free();

	McpJson j = mcpCommitHeightMapEdit(copy, true);
	j.set("cells_ground", numGround).set("cells_cliff", numCliff).set("cells_water", numWater);
	j.set("regions_blended", regions);
	return j;
}

} // namespace

void mcpRegisterGenerateCommands()
{
	mcpRegisterCommand("terrain.generate", cmdGenerate, "{layers:[{height,frequency,octaves?,persistence?,amplitude?,ridged?,signed?}], seed?, mode?:set|add, base_height?, shape?, feather?, protect?:[{x,y,radius,feather?}]} Noise terrain.");
	mcpRegisterCommand("terrain.limit_slope", cmdLimitSlope, "{max_step?=15, iterations?=20, shape?} Reduces height steps between neighbours; cliff flags follow the slope.");
	mcpRegisterCommand("terrain.auto_texture", cmdAutoTexture, "{base:{texture,overlays?}, cliff?:{texture,overlays?,slope?,use_impassable?}, water?:{texture,overlays?,below?}, seed?, blend?, edge_texture?, shape?} Textures cells by slope/water with noise overlays.");
	mcpRegisterCommand("terrain.blend_all", cmdBlendAll, "{textures?, edge_texture?, shape?} Auto-blends every region of the given (default: all but the most common) textures.");
	mcpRegisterCommand("terrain.remove_blends", cmdRemoveBlends, "{shape?} Removes texture blends.");
}
