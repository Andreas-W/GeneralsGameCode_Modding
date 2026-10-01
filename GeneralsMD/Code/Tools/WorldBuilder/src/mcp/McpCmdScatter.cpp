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

// McpCmdScatter.cpp
// MCP bridge command that scatters decoration objects (trees, rocks, props) over an area while
// keeping clear of cliffs, water, roads, existing objects and start positions. The idea follows
// the TreeGenerator and RockGenerator of Genesis (The CWC Team).

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpNoise.h"
#include "mcp/McpShape.h"

#include "CUndoable.h"
#include "PointerTool.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "Common/ThingFactory.h"
#include "Common/ThingSort.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"

#include <math.h>
#include <set>
#include <vector>

namespace
{

struct Blocker
{
	double x, y, radius;
};

struct RoadSegment
{
	double x0, y0, x1, y1;
};

double distanceToSegment(double px, double py, const RoadSegment &s)
{
	const double dx = s.x1 - s.x0, dy = s.y1 - s.y0;
	const double len2 = dx * dx + dy * dy;
	double t = len2 > 0 ? ((px - s.x0) * dx + (py - s.y0) * dy) / len2 : 0;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	const double cx = s.x0 + t * dx - px, cy = s.y0 + t * dy - py;
	return sqrt(cx * cx + cy * cy);
}

/// Small deterministic generator so the same seed gives the same scatter.
class Random
{
public:
	explicit Random(unsigned int seed) : m_state(seed * 2654435761u + 0x9E3779B9u) { if (m_state == 0) m_state = 1; }
	double next()	///< 0..1
	{
		m_state ^= m_state << 13;
		m_state ^= m_state >> 17;
		m_state ^= m_state << 5;
		return (m_state & 0xFFFFFF) / (double)0x1000000;
	}
private:
	unsigned int m_state;
};

struct WeightedTemplate
{
	const ThingTemplate *tt;
	double weight;
};

std::vector<PolygonTrigger *> parseAreas(const McpJson &args, const char *key)
{
	std::vector<PolygonTrigger *> areas;
	const McpJson &list = args.get(key);
	if (list.isNull()) return areas;
	if (!list.isArray()) mcpFail("%s must be a list of area names", key);
	for (size_t i = 0; i < list.size(); i++) {
		if (!list.at(i).isString()) mcpFail("%s must be a list of area names", key);
		PolygonTrigger *found = nullptr;
		for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
			if (trig->getTriggerName().compareNoCase(list.at(i).asString().c_str()) == 0) found = trig;
		}
		if (!found) mcpFail("unknown area '%s' (see polygons.list)", list.at(i).asString().c_str());
		areas.push_back(found);
	}
	return areas;
}

bool insideAny(const std::vector<PolygonTrigger *> &areas, double x, double y)
{
	ICoord3D pt;
	pt.x = (Int)floor(x + 0.5);
	pt.y = (Int)floor(y + 0.5);
	pt.z = 0;
	for (size_t i = 0; i < areas.size(); i++) {
		if (areas[i]->pointInTrigger(pt)) return true;
	}
	return false;
}

McpJson cmdScatter(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	const Int border = map->getBorderSize();
	const double maxX = (map->getXExtent() - 1 - 2 * border) * MAP_XY_FACTOR;
	const double maxY = (map->getYExtent() - 1 - 2 * border) * MAP_XY_FACTOR;

	// Templates, optionally weighted.
	std::vector<WeightedTemplate> templates;
	double totalWeight = 0;
	const McpJson &list = mcpArgArray(args, "templates");
	for (size_t i = 0; i < list.size(); i++) {
		const McpJson &entry = list.at(i);
		WeightedTemplate wt;
		std::string name;
		if (entry.isString()) {
			name = entry.asString();
			wt.weight = 1;
		} else if (entry.isObject()) {
			name = mcpArgString(entry, "template");
			wt.weight = mcpArgNumber(entry, "weight", 1);
		} else {
			mcpFail("templates must be names or {template, weight} objects");
		}
		wt.tt = TheThingFactory->findTemplate(AsciiString(name.c_str()), FALSE);
		if (wt.tt == nullptr) mcpFail("unknown object template '%s' (see objects.list_templates)", name.c_str());
		if (wt.weight <= 0) mcpFail("weights must be > 0");
		totalWeight += wt.weight;
		templates.push_back(wt);
	}
	if (templates.empty()) mcpFail("pass at least one template");

	// Area: a shape, or the whole playable map.
	const bool limited = args.has("x") || args.has("x0") || args.has("shape");
	McpShape shape;
	double bx0 = 0, by0 = 0, bx1 = maxX, by1 = maxY, areaSize = maxX * maxY;
	if (limited) {
		shape.parse(args, true);
		bx0 = shape.x0 < 0 ? 0 : shape.x0;
		by0 = shape.y0 < 0 ? 0 : shape.y0;
		bx1 = shape.x1 > maxX ? maxX : shape.x1;
		by1 = shape.y1 > maxY ? maxY : shape.y1;
		if (bx1 <= bx0 || by1 <= by0) mcpFail("the area is outside the playable map");
		areaSize = shape.circle ? PI * shape.radius * shape.radius : (bx1 - bx0) * (by1 - by0);
	}

	const unsigned int seed = (unsigned int)mcpArgInt(args, "seed", 1);
	const double density = mcpArgNumber(args, "density", 40);	// objects per 1000 x 1000 world units
	Int count = args.has("count") ? mcpArgInt(args, "count") : (Int)floor(areaSize / 1000000.0 * density + 0.5);
	if (count < 1) count = 1;
	if (count > 5000) mcpFail("at most 5000 objects per call (requested %d)", count);
	const double minSpacing = mcpArgNumber(args, "min_spacing", 30);
	const double roadClearance = mcpArgNumber(args, "road_clearance", 40);
	const double objectClearance = mcpArgNumber(args, "object_clearance", 40);
	const double startClearance = mcpArgNumber(args, "start_clearance", 300);
	const double edgeMargin = mcpArgNumber(args, "edge_margin", 10);
	const bool avoidCliffs = mcpArgBool(args, "avoid_cliffs", true);
	const bool avoidWater = mcpArgBool(args, "avoid_water", true);
	const double maxSlope = mcpArgNumber(args, "max_slope", -1);
	const std::vector<PolygonTrigger *> avoidAreas = parseAreas(args, "avoid_areas");
	const std::vector<PolygonTrigger *> insideAreas = parseAreas(args, "inside_areas");

	// Optional texture filter.
	std::set<Int> onlyTextures;
	if (args.has("only_textures")) {
		const McpJson &tex = mcpArgArray(args, "only_textures");
		for (size_t i = 0; i < tex.size(); i++) {
			McpJson one = McpJson::makeObject().set("texture", tex.at(i));
			onlyTextures.insert(mcpFindTextureClass(one, "texture"));
		}
	}

	// Optional clustering: only place where a noise field exceeds the threshold (groves, rock fields).
	const McpJson &cluster = args.get("cluster");
	McpNoise noise(seed * 977u + 13u);
	double clusterFrequency = 0, clusterThreshold = 0;
	if (cluster.isObject()) {
		clusterFrequency = mcpArgNumber(cluster, "frequency", 0.03);
		clusterThreshold = mcpArgNumber(cluster, "threshold", 0.5);
	} else if (!cluster.isNull()) {
		mcpFail("cluster must be an object {frequency, threshold}");
	}

	// Things to keep away from.
	std::vector<Blocker> blockers;	// existing objects with their own radius
	std::vector<Blocker> starts;
	std::vector<RoadSegment> roads;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		const Coord3D *loc = obj->getLocation();
		if (obj->getFlag(FLAG_ROAD_POINT1) || obj->getFlag(FLAG_BRIDGE_POINT1)) {
			MapObject *second = obj->getNext();
			if (second) {
				RoadSegment seg = { loc->x, loc->y, second->getLocation()->x, second->getLocation()->y };
				roads.push_back(seg);
				obj = second;
			}
			continue;
		}
		if (obj->getFlags() & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS)) continue;
		if (obj->isWaypoint()) {
			int number = 0, consumed = 0;
			const AsciiString name = obj->getWaypointName();
			if (sscanf(name.str(), "Player_%d_Start%n", &number, &consumed) == 1 && consumed == name.getLength()) {
				Blocker b = { loc->x, loc->y, startClearance };
				starts.push_back(b);
			}
			continue;
		}
		const ThingTemplate *tt = obj->getThingTemplate();
		Blocker b = { loc->x, loc->y, minSpacing };
		// Decoration only needs the normal spacing; everything else keeps its footprint plus clearance free.
		if (tt && tt->getEditorSorting() != ES_SHRUBBERY && tt->getEditorSorting() != ES_MISC_NATURAL) {
			b.radius = tt->getTemplateGeometryInfo().getBoundingCircleRadius() + objectClearance;
		}
		blockers.push_back(b);
	}

	Random rng(seed);
	std::vector<Blocker> placed;
	MapObject *head = nullptr, *tail = nullptr;
	Int attempts = 0;
	Int rejectedTerrain = 0, rejectedSpacing = 0, rejectedRoad = 0, rejectedArea = 0;
	const Int maxAttempts = count * 40 + 200;
	McpJson perTemplate = McpJson::makeObject();
	std::vector<Int> templateCounts(templates.size(), 0);

	while ((Int)placed.size() < count && attempts < maxAttempts) {
		attempts++;
		const double x = bx0 + rng.next() * (bx1 - bx0);
		const double y = by0 + rng.next() * (by1 - by0);
		const double roll = rng.next();
		const double pick = rng.next() * totalWeight;
		const double angle = (rng.next() * 2 - 1) * PI;

		if (x < edgeMargin || y < edgeMargin || x > maxX - edgeMargin || y > maxY - edgeMargin) { rejectedArea++; continue; }
		if (limited && shape.weight(x, y) < roll) { rejectedArea++; continue; }
		if (!insideAreas.empty() && !insideAny(insideAreas, x, y)) { rejectedArea++; continue; }
		if (insideAny(avoidAreas, x, y)) { rejectedArea++; continue; }
		if (clusterFrequency > 0 && noise.fractal(x / MAP_XY_FACTOR, y / MAP_XY_FACTOR, clusterFrequency, 2, 0.5, 1.3, false) <= clusterThreshold) {
			rejectedArea++;
			continue;
		}

		const Int cx = (Int)floor(x / MAP_XY_FACTOR) + border;
		const Int cy = (Int)floor(y / MAP_XY_FACTOR) + border;
		bool terrainOk = true;
		if (avoidCliffs && map->getCliffState(cx, cy)) terrainOk = false;
		if (terrainOk && avoidWater && mcpIsCellUnderWater(map, cx, cy)) terrainOk = false;
		if (terrainOk && maxSlope >= 0) {
			Int lo = 255, hi = 0;
			for (Int dy = 0; dy <= 1; dy++) {
				for (Int dx = 0; dx <= 1; dx++) {
					const Int h = map->getHeight(cx + dx, cy + dy);
					if (h < lo) lo = h;
					if (h > hi) hi = h;
				}
			}
			if (hi - lo > maxSlope) terrainOk = false;
		}
		if (terrainOk && !onlyTextures.empty() && !onlyTextures.count(map->getTextureClass(cx, cy, true))) terrainOk = false;
		if (!terrainOk) { rejectedTerrain++; continue; }

		bool roadOk = true;
		for (size_t i = 0; i < roads.size() && roadOk; i++) {
			if (distanceToSegment(x, y, roads[i]) < roadClearance) roadOk = false;
		}
		if (!roadOk) { rejectedRoad++; continue; }

		bool spacingOk = true;
		for (size_t i = 0; i < starts.size() && spacingOk; i++) {
			if (hypot(starts[i].x - x, starts[i].y - y) < starts[i].radius) spacingOk = false;
		}
		for (size_t i = 0; i < blockers.size() && spacingOk; i++) {
			if (hypot(blockers[i].x - x, blockers[i].y - y) < blockers[i].radius) spacingOk = false;
		}
		for (size_t i = 0; i < placed.size() && spacingOk; i++) {
			if (hypot(placed[i].x - x, placed[i].y - y) < minSpacing) spacingOk = false;
		}
		if (!spacingOk) { rejectedSpacing++; continue; }

		size_t which = 0;
		double acc = 0;
		for (size_t i = 0; i < templates.size(); i++) {
			acc += templates[i].weight;
			which = i;
			if (pick < acc) break;
		}
		Coord3D loc;
		loc.x = (Real)x;
		loc.y = (Real)y;
		loc.z = 0;
		MapObject *obj = newInstance(MapObject)(loc, templates[which].tt->getName(), (Real)angle, 0, nullptr, templates[which].tt);
		obj->getProperties()->setAsciiString(TheKey_originalOwner, "team");
		if (tail) tail->setNextMap(obj); else head = obj;
		tail = obj;
		Blocker b = { x, y, minSpacing };
		placed.push_back(b);
		templateCounts[which]++;
	}

	if (head) {
		PointerTool::clearSelection();
		mcpCommit(new AddObjectUndoable(doc, head));
		PointerTool::clearSelection();
	}
	for (size_t i = 0; i < templates.size(); i++) {
		perTemplate.set(templates[i].tt->getName().str(), templateCounts[i]);
	}
	McpJson j = McpJson::makeObject();
	j.set("placed", (int)placed.size()).set("requested", count).set("attempts", attempts);
	j.set("per_template", perTemplate);
	j.set("rejected", McpJson::makeObject()
		.set("outside_area_or_cluster", rejectedArea).set("terrain", rejectedTerrain)
		.set("road", rejectedRoad).set("spacing", rejectedSpacing));
	return j;
}

} // namespace

void mcpRegisterScatterCommands()
{
	mcpRegisterCommand("objects.scatter", cmdScatter, "{templates:[name|{template,weight}], count?|density?, seed?, shape?, feather?, min_spacing?, road_clearance?, object_clearance?, start_clearance?, avoid_cliffs?, avoid_water?, max_slope?, only_textures?, avoid_areas?, inside_areas?, cluster?:{frequency,threshold}} Scatters decoration objects.");
}
