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

// McpCmdRoute.cpp
// MCP bridge command that routes a road across the terrain: A* over the heightmap vertices that
// avoids cliffs, water and structures and prefers gentle slopes, straightened afterwards, with
// optional flattening of the ground under the road. The idea follows the RoadGenerator and
// RoadFlattener of Genesis (The CWC Team).

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "CUndoable.h"
#include "PointerTool.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "Common/ThingSort.h"
#include "Common/ThingTemplate.h"
#include "GameClient/TerrainRoads.h"
#include "GameLogic/PolygonTrigger.h"

#include <math.h>
#include <queue>
#include <vector>

namespace
{

struct Pt
{
	double x, y;	///< heightmap vertex coordinates (fractional allowed)
};

/// Terrain grid with the cells a road may not cross.
class RouteGrid
{
public:
	RouteGrid(WorldHeightMapEdit *map) : m_map(map), m_w(map->getXExtent()), m_h(map->getYExtent()),
		m_border(map->getBorderSize()), m_blocked(map->getXExtent() * map->getYExtent(), 0),
		m_slopeWeight(0.5), m_maxGrade(0) {}

	Int width() const { return m_w; }
	Int height() const { return m_h; }
	Int border() const { return m_border; }
	void setCosts(double slopeWeight, double maxGrade) { m_slopeWeight = slopeWeight; m_maxGrade = maxGrade; }

	bool inPlayable(Int x, Int y) const
	{
		return x >= m_border && y >= m_border && x <= m_w - 1 - m_border && y <= m_h - 1 - m_border;
	}
	bool isBlocked(Int x, Int y) const { return !inPlayable(x, y) || m_blocked[y * m_w + x] != 0; }
	void block(Int x, Int y) { if (x >= 0 && y >= 0 && x < m_w && y < m_h) m_blocked[y * m_w + x] = 1; }
	double heightAt(Int x, Int y) const { return m_map->getHeight(x, y); }

	void blockCliffsAndWater(bool cliffs, bool water)
	{
		for (Int y = 0; y < m_h - 1; y++) {
			for (Int x = 0; x < m_w - 1; x++) {
				if ((cliffs && m_map->getCliffState(x, y)) || (water && mcpIsCellUnderWater(m_map, x, y))) {
					// A blocked cell blocks its four corner vertices.
					block(x, y); block(x + 1, y); block(x, y + 1); block(x + 1, y + 1);
				}
			}
		}
	}
	void blockCircle(double wx, double wy, double radius)
	{
		const double cx = wx / MAP_XY_FACTOR + m_border, cy = wy / MAP_XY_FACTOR + m_border;
		const double r = radius / MAP_XY_FACTOR;
		for (Int y = (Int)floor(cy - r); y <= (Int)ceil(cy + r); y++) {
			for (Int x = (Int)floor(cx - r); x <= (Int)ceil(cx + r); x++) {
				if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) block(x, y);
			}
		}
	}
	void blockArea(PolygonTrigger *trig)
	{
		for (Int y = m_border; y < m_h - m_border; y++) {
			for (Int x = m_border; x < m_w - m_border; x++) {
				ICoord3D pt;
				pt.x = (Int)((x - m_border) * MAP_XY_FACTOR);
				pt.y = (Int)((y - m_border) * MAP_XY_FACTOR);
				pt.z = 0;
				if (trig->pointInTrigger(pt)) block(x, y);
			}
		}
	}

	/// Finds the closest unblocked vertex within maxRadius cells (the vertex itself if it is free).
	bool nearestFree(Int x, Int y, Int maxRadius, Int *fx, Int *fy) const
	{
		double best = 1e30;
		bool found = false;
		for (Int r = 0; r <= maxRadius && !found; r++) {
			for (Int dy = -r; dy <= r; dy++) {
				for (Int dx = -r; dx <= r; dx++) {
					if (abs(dx) != r && abs(dy) != r) continue;	// ring only
					if (isBlocked(x + dx, y + dy)) continue;
					const double d = dx * dx + dy * dy;
					if (d < best) {
						best = d;
						*fx = x + dx;
						*fy = y + dy;
						found = true;
					}
				}
			}
		}
		return found;
	}

	/// Height change per cell of distance for one step between neighbouring vertices.
	double stepGrade(Int x0, Int y0, Int x1, Int y1) const
	{
		const double d = (x0 != x1 && y0 != y1) ? 1.41421356 : 1.0;
		return fabs(heightAt(x1, y1) - heightAt(x0, y0)) / d;
	}

	/// Cost of one step between neighbouring vertices, or a negative value if it is too steep.
	double stepCost(Int x0, Int y0, Int x1, Int y1) const
	{
		const double d = (x0 != x1 && y0 != y1) ? 1.41421356 : 1.0;
		const double grade = stepGrade(x0, y0, x1, y1);
		if (m_maxGrade > 0 && grade > m_maxGrade) return -1;
		return d * (1.0 + m_slopeWeight * grade);
	}

	/// Cost of the straight line between two vertices, or negative if it crosses something blocked.
	/// maxGrade, if given, receives the steepest step along the line.
	double lineCost(const Pt &a, const Pt &b, double *maxGrade = nullptr) const
	{
		if (maxGrade) *maxGrade = 0;
		const double len = hypot(b.x - a.x, b.y - a.y);
		const Int steps = (Int)ceil(len * 2);
		if (steps == 0) return 0;
		double cost = 0;
		Int px = (Int)floor(a.x + 0.5), py = (Int)floor(a.y + 0.5);
		for (Int i = 1; i <= steps; i++) {
			const double t = (double)i / steps;
			const Int x = (Int)floor(a.x + (b.x - a.x) * t + 0.5);
			const Int y = (Int)floor(a.y + (b.y - a.y) * t + 0.5);
			if (x == px && y == py) continue;
			if (isBlocked(x, y)) return -1;
			const double c = stepCost(px, py, x, y);
			if (c < 0) return -1;
			cost += c;
			if (maxGrade) {
				const double grade = stepGrade(px, py, x, y);
				if (grade > *maxGrade) *maxGrade = grade;
			}
			px = x;
			py = y;
		}
		return cost;
	}

	/// A* between two vertices. Returns the vertex path and its accumulated costs.
	bool findPath(Int sx, Int sy, Int gx, Int gy, std::vector<Pt> *path, std::vector<double> *costs) const
	{
		const Int n = m_w * m_h;
		std::vector<double> g(n, 1e30);
		std::vector<Int> parent(n, -1);
		std::vector<unsigned char> closed(n, 0);
		typedef std::pair<double, Int> Entry;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry> > open;
		const Int start = sy * m_w + sx, goal = gy * m_w + gx;
		g[start] = 0;
		open.push(Entry(0, start));
		static const Int dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
		while (!open.empty()) {
			const Int cur = open.top().second;
			open.pop();
			if (closed[cur]) continue;
			closed[cur] = 1;
			if (cur == goal) break;
			const Int cx = cur % m_w, cy = cur / m_w;
			for (Int d = 0; d < 8; d++) {
				const Int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
				if (isBlocked(nx, ny)) continue;
				// Don't cut corners diagonally past a blocked vertex.
				if (dirs[d][0] && dirs[d][1] && (isBlocked(cx + dirs[d][0], cy) || isBlocked(cx, cy + dirs[d][1]))) continue;
				const double step = stepCost(cx, cy, nx, ny);
				if (step < 0) continue;
				const Int next = ny * m_w + nx;
				if (closed[next] || g[cur] + step >= g[next]) continue;
				g[next] = g[cur] + step;
				parent[next] = cur;
				const Int dx = abs(nx - gx), dy = abs(ny - gy);
				const double heuristic = (dx > dy ? dx : dy) + 0.41421356 * (dx < dy ? dx : dy);
				open.push(Entry(g[next] + heuristic, next));
			}
		}
		if (!closed[goal]) return false;
		std::vector<Int> reversed;
		for (Int cur = goal; cur != -1; cur = parent[cur]) reversed.push_back(cur);
		for (size_t i = reversed.size(); i-- > 0;) {
			Pt p = { (double)(reversed[i] % m_w), (double)(reversed[i] / m_w) };
			path->push_back(p);
			costs->push_back(g[reversed[i]]);
		}
		return true;
	}

private:
	WorldHeightMapEdit *m_map;
	Int m_w, m_h, m_border;
	std::vector<unsigned char> m_blocked;
	double m_slopeWeight, m_maxGrade;
};

/// A shortcut may be at most this much steeper than the piece of route it replaces. Without it a
/// long straight line could cut across a short steep feature the pathfinder had gone around.
bool gradeAcceptable(double shortcutGrade, double replacedGrade)
{
	const double allowed = replacedGrade * 1.5 > 3.0 ? replacedGrade * 1.5 : 3.0;
	return shortcutGrade <= allowed;
}

/// Removes bends the road does not need: keeps a vertex only when the straight line past it would
/// be blocked, clearly more expensive or clearly steeper than the routed path.
std::vector<Pt> straighten(const RouteGrid &grid, const std::vector<Pt> &path, const std::vector<double> &costs, double tolerance)
{
	std::vector<double> grades(path.size(), 0);	// grade of the step leading to each vertex
	for (size_t i = 1; i < path.size(); i++) {
		grades[i] = grid.stepGrade((Int)path[i - 1].x, (Int)path[i - 1].y, (Int)path[i].x, (Int)path[i].y);
	}
	std::vector<Pt> out;
	size_t i = 0;
	out.push_back(path[0]);
	while (i + 1 < path.size()) {
		size_t best = i + 1;
		for (size_t j = path.size() - 1; j > i + 1; j--) {
			double lineGrade = 0;
			const double line = grid.lineCost(path[i], path[j], &lineGrade);
			if (line < 0 || line > (costs[j] - costs[i]) * tolerance + 0.5) continue;
			double pathGrade = 0;
			for (size_t k = i + 1; k <= j; k++) {
				if (grades[k] > pathGrade) pathGrade = grades[k];
			}
			if (gradeAcceptable(lineGrade, pathGrade)) {
				best = j;
				break;
			}
		}
		out.push_back(path[best]);
		i = best;
	}
	return out;
}

/// Drops points that lie within maxOffset cells of the line between their neighbours, as long as
/// that line is neither blocked nor clearly costlier (steeper) than going through the point. This
/// removes the small wiggles a grid path leaves behind.
std::vector<Pt> dropNearlyStraightPoints(const RouteGrid &grid, std::vector<Pt> pts, double maxOffset, double tolerance)
{
	bool changed = true;
	while (changed && pts.size() > 2) {
		changed = false;
		for (size_t i = 1; i + 1 < pts.size(); i++) {
			const Pt &a = pts[i - 1], &b = pts[i + 1], &p = pts[i];
			const double len = hypot(b.x - a.x, b.y - a.y);
			if (len < 0.001) continue;
			const double offset = fabs((b.x - a.x) * (a.y - p.y) - (a.x - p.x) * (b.y - a.y)) / len;
			if (offset > maxOffset) continue;
			double directGrade = 0, grade1 = 0, grade2 = 0;
			const double direct = grid.lineCost(a, b, &directGrade);
			const double around = grid.lineCost(a, p, &grade1) + grid.lineCost(p, b, &grade2);
			if (direct >= 0 && direct <= around * tolerance + 0.5
				&& gradeAcceptable(directGrade, grade1 > grade2 ? grade1 : grade2)) {
				pts.erase(pts.begin() + i);
				changed = true;
				break;
			}
		}
	}
	return pts;
}

bool parsePoint(const McpJson &v, double *x, double *y)
{
	if (!v.isArray() || v.size() < 2 || !v.at(0).isNumber() || !v.at(1).isNumber()) return false;
	*x = v.at(0).asNumber();
	*y = v.at(1).asNumber();
	return true;
}

McpJson cmdRoute(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	const std::string type = mcpArgString(args, "type");
	AsciiString roadName(type.c_str());
	if (TheTerrainRoads->findRoad(roadName) == nullptr) {
		mcpFail("unknown road type '%s' (see roads.list_types; bridges are placed with roads.add)", type.c_str());
	}
	const std::string corners = mcpArgString(args, "corners", "auto");
	if (corners != "auto" && corners != "curved" && corners != "angled" && corners != "tight") {
		mcpFail("corners must be auto, curved, angled or tight");
	}

	// Stops: from, optional via points, to.
	std::vector<Pt> stops;
	double wx, wy;
	if (!parsePoint(args.get("from"), &wx, &wy)) mcpFail("from must be [x, y] in world units");
	Pt p = { wx, wy };
	stops.push_back(p);
	const McpJson &via = args.get("via");
	if (via.isArray()) {
		for (size_t i = 0; i < via.size(); i++) {
			if (!parsePoint(via.at(i), &wx, &wy)) mcpFail("via must be a list of [x, y] points");
			Pt v = { wx, wy };
			stops.push_back(v);
		}
	}
	if (!parsePoint(args.get("to"), &wx, &wy)) mcpFail("to must be [x, y] in world units");
	Pt t = { wx, wy };
	stops.push_back(t);

	// Snap the ends to existing road points so the new road joins them.
	const double snap = mcpArgNumber(args, "snap", 25);
	for (size_t s = 0; s < stops.size(); s += stops.size() - 1) {
		double bestDist = snap;
		for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
			if (!(obj->getFlags() & FLAG_ROAD_FLAGS)) continue;
			const double d = hypot(obj->getLocation()->x - stops[s].x, obj->getLocation()->y - stops[s].y);
			if (d <= bestDist) {
				bestDist = d;
				stops[s].x = obj->getLocation()->x;
				stops[s].y = obj->getLocation()->y;
			}
		}
	}

	RouteGrid grid(map);
	grid.setCosts(mcpArgNumber(args, "slope_weight", 0.5), mcpArgNumber(args, "max_grade", 10));
	grid.blockCliffsAndWater(mcpArgBool(args, "avoid_cliffs", true), mcpArgBool(args, "avoid_water", true));
	const double clearance = mcpArgNumber(args, "object_clearance", 20);
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		const ThingTemplate *tt = obj->getThingTemplate();
		if (tt && tt->getEditorSorting() == ES_STRUCTURE) {
			grid.blockCircle(obj->getLocation()->x, obj->getLocation()->y, tt->getTemplateGeometryInfo().getBoundingCircleRadius() + clearance);
		}
	}
	const McpJson &avoidAreas = args.get("avoid_areas");
	if (avoidAreas.isArray()) {
		for (size_t i = 0; i < avoidAreas.size(); i++) {
			PolygonTrigger *found = nullptr;
			for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
				if (avoidAreas.at(i).isString() && trig->getTriggerName().compareNoCase(avoidAreas.at(i).asString().c_str()) == 0) found = trig;
			}
			if (!found) mcpFail("unknown area in avoid_areas (see polygons.list)");
			grid.blockArea(found);
		}
	}

	// Route each leg and straighten it.
	const double tolerance = mcpArgNumber(args, "straighten", 1.25);
	const double mergeDistance = mcpArgNumber(args, "merge_distance", 20) / MAP_XY_FACTOR;
	std::vector<Pt> route;
	for (size_t s = 0; s + 1 < stops.size(); s++) {
		// A stop may lie on blocked ground (e.g. right at a building). Route between the nearest
		// free vertices and connect the stop itself with a straight piece.
		Int ends[2][2], freeEnds[2][2];
		for (Int e = 0; e < 2; e++) {
			ends[e][0] = (Int)floor(stops[s + e].x / MAP_XY_FACTOR + 0.5) + grid.border();
			ends[e][1] = (Int)floor(stops[s + e].y / MAP_XY_FACTOR + 0.5) + grid.border();
			if (!grid.inPlayable(ends[e][0], ends[e][1])) {
				mcpFail("point (%.0f, %.0f) is outside the playable map", stops[s + e].x, stops[s + e].y);
			}
			if (!grid.nearestFree(ends[e][0], ends[e][1], 40, &freeEnds[e][0], &freeEnds[e][1])) {
				mcpFail("point (%.0f, %.0f) is deep inside blocked ground (cliffs, water or structures)", stops[s + e].x, stops[s + e].y);
			}
		}
		std::vector<Pt> path;
		std::vector<double> costs;
		if (!grid.findPath(freeEnds[0][0], freeEnds[0][1], freeEnds[1][0], freeEnds[1][1], &path, &costs)) {
			mcpFail("no route from (%.0f, %.0f) to (%.0f, %.0f): blocked by cliffs, water, structures or max_grade. "
				"Relax avoid_cliffs/avoid_water/max_grade, add via points, or bridge the gap with roads.add.",
				stops[s].x, stops[s].y, stops[s + 1].x, stops[s + 1].y);
		}
		std::vector<Pt> leg = tolerance > 0 ? straighten(grid, path, costs, tolerance) : path;
		if (tolerance > 0) {
			leg = dropNearlyStraightPoints(grid, leg, mergeDistance, tolerance);
		}
		// Only the real start and end get the connecting piece; a blocked via point would
		// otherwise become a dead-end spur.
		if (s == 0 && (freeEnds[0][0] != ends[0][0] || freeEnds[0][1] != ends[0][1])) {
			Pt stop = { (double)ends[0][0], (double)ends[0][1] };
			leg.insert(leg.begin(), stop);
		}
		if (s + 2 == stops.size() && (freeEnds[1][0] != ends[1][0] || freeEnds[1][1] != ends[1][1])) {
			Pt stop = { (double)ends[1][0], (double)ends[1][1] };
			leg.push_back(stop);
		}
		for (size_t i = (s == 0 ? 0 : 1); i < leg.size(); i++) route.push_back(leg[i]);
	}

	// World-space polyline; the exact end points replace the snapped grid vertices.
	std::vector<Coord2D> points;
	for (size_t i = 0; i < route.size(); i++) {
		Coord2D c;
		c.x = (Real)((route[i].x - grid.border()) * MAP_XY_FACTOR);
		c.y = (Real)((route[i].y - grid.border()) * MAP_XY_FACTOR);
		points.push_back(c);
	}
	points.front().x = (Real)stops.front().x;
	points.front().y = (Real)stops.front().y;
	points.back().x = (Real)stops.back().x;
	points.back().y = (Real)stops.back().y;

	// Corner style per point; "auto" tightens only the sharp bends.
	std::vector<Int> flags(points.size(), 0);
	for (size_t i = 0; i < points.size(); i++) {
		if (corners == "angled") flags[i] = FLAG_ROAD_CORNER_ANGLED;
		else if (corners == "tight") flags[i] = FLAG_ROAD_CORNER_TIGHT;
		else if (corners == "auto" && i > 0 && i + 1 < points.size()) {
			const double a1 = atan2(points[i].y - points[i - 1].y, points[i].x - points[i - 1].x);
			const double a2 = atan2(points[i + 1].y - points[i].y, points[i + 1].x - points[i].x);
			double turn = fabs(a2 - a1);
			if (turn > PI) turn = 2 * PI - turn;
			if (turn > PI / 3) flags[i] = FLAG_ROAD_CORNER_TIGHT;
		}
	}

	double length = 0;
	for (size_t i = 0; i + 1 < points.size(); i++) {
		length += hypot(points[i + 1].x - points[i].x, points[i + 1].y - points[i].y);
	}

	// Optional flattening: level the ground across the road to a smoothed height profile along it.
	MultipleUndoable *undo = new MultipleUndoable;
	MapObject *tail = nullptr;
	MapObject *head = mcpBuildRoadChain(roadName, false, points, flags, &tail);
	undo->addUndoable(new AddObjectUndoable(doc, head));
	WorldHeightMapEdit *copy = nullptr;
	Int flattened = 0;
	if (mcpArgBool(args, "flatten", true)) {
		const double halfWidth = mcpArgNumber(args, "flatten_width", 50) / 2 / MAP_XY_FACTOR;
		const double feather = mcpArgNumber(args, "flatten_feather", 40) / MAP_XY_FACTOR;
		std::vector<Pt> samples;
		std::vector<double> profile;
		for (size_t i = 0; i + 1 < route.size(); i++) {
			const double len = hypot(route[i + 1].x - route[i].x, route[i + 1].y - route[i].y);
			const Int steps = (Int)ceil(len * 2);
			for (Int k = (i == 0 ? 0 : 1); k <= steps; k++) {
				const double f = steps ? (double)k / steps : 0;
				Pt sp = { route[i].x + (route[i + 1].x - route[i].x) * f, route[i].y + (route[i + 1].y - route[i].y) * f };
				samples.push_back(sp);
				profile.push_back(grid.heightAt((Int)floor(sp.x + 0.5), (Int)floor(sp.y + 0.5)));
			}
		}
		for (Int pass = 0; pass < 6; pass++) {
			std::vector<double> prev = profile;
			for (size_t i = 1; i + 1 < profile.size(); i++) {
				profile[i] = (prev[i - 1] + prev[i] * 2 + prev[i + 1]) / 4;
			}
		}
		copy = map->duplicate();
		const double reach = halfWidth + feather;
		Int minX = grid.width(), minY = grid.height(), maxX = 0, maxY = 0;
		for (size_t i = 0; i < samples.size(); i++) {
			if (samples[i].x - reach < minX) minX = (Int)floor(samples[i].x - reach);
			if (samples[i].y - reach < minY) minY = (Int)floor(samples[i].y - reach);
			if (samples[i].x + reach > maxX) maxX = (Int)ceil(samples[i].x + reach);
			if (samples[i].y + reach > maxY) maxY = (Int)ceil(samples[i].y + reach);
		}
		if (minX < 0) minX = 0;
		if (minY < 0) minY = 0;
		if (maxX > grid.width() - 1) maxX = grid.width() - 1;
		if (maxY > grid.height() - 1) maxY = grid.height() - 1;
		for (Int y = minY; y <= maxY; y++) {
			for (Int x = minX; x <= maxX; x++) {
				double best = 1e30;
				size_t bestNdx = 0;
				for (size_t i = 0; i < samples.size(); i++) {
					const double d = (samples[i].x - x) * (samples[i].x - x) + (samples[i].y - y) * (samples[i].y - y);
					if (d < best) { best = d; bestNdx = i; }
				}
				const double dist = sqrt(best);
				if (dist > reach) continue;
				const double w = dist <= halfWidth ? 1.0 : (feather > 0 ? 1.0 - (dist - halfWidth) / feather : 0);
				const double cur = map->getHeight(x, y);
				double h = floor(cur + (profile[bestNdx] - cur) * w + 0.5);
				if (h < 0) h = 0;
				if (h > 255) h = 255;
				if ((Int)h != map->getHeight(x, y)) {
					copy->setHeight(x, y, (UnsignedByte)h);
					flattened++;
				}
			}
		}
		if (flattened > 0) {
			IRegion2D all = { 0, 0, 0, 0 };
			doc->updateHeightMap(copy, false, all);
			undo->addUndoable(new WBDocUndoable(doc, copy));
		}
	}
	PointerTool::clearSelection();
	mcpCommit(undo);
	if (copy) REF_PTR_RELEASE(copy);

	McpJson pts = McpJson::makeArray();
	for (size_t i = 0; i < points.size(); i++) {
		McpJson pair = McpJson::makeArray();
		pair.push(McpJson((double)points[i].x));
		pair.push(McpJson((double)points[i].y));
		pts.push(pair);
	}
	McpJson ids = McpJson::makeArray();
	for (MapObject *obj = head; obj; obj = obj->getNext()) {
		ids.push(mcpObjectHandle(obj));
		if (obj == tail) break;
	}
	McpJson j = McpJson::makeObject();
	j.set("type", type).set("points", pts).set("segments", (int)points.size() - 1).set("length", length);
	j.set("flattened_vertices", flattened).set("point_ids", ids);
	return j;
}

} // namespace

void mcpRegisterRouteCommands()
{
	mcpRegisterCommand("roads.route", cmdRoute, "{type, from:[x,y], to:[x,y], via?:[[x,y]...], corners?:auto|curved|angled|tight, flatten?, flatten_width?, flatten_feather?, slope_weight?, max_grade?, avoid_cliffs?, avoid_water?, avoid_areas?, object_clearance?, straighten?, snap?} Routes a road over the terrain.");
}
